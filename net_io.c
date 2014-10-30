// dump1090, a Mode S messages decoder for RTLSDR devices.
//
// Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//  *  Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//
//  *  Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "dump1090.h"
//
// ============================= Networking =============================
//
// Note: here we disregard any kind of good coding practice in favor of
// extreme simplicity, that is:
//
// 1) We only rely on the kernel buffers for our I/O without any kind of
//    user space buffering.
// 2) We don't register any kind of event handler, from time to time a
//    function gets called and we accept new connections. All the rest is
//    handled via non-blocking I/O and manually polling clients to see if
//    they have something new to share with us when reading is needed.
//
//=========================================================================
//
// Networking "stack" initialization
//
struct service {
	char *descr;
	int *socket;
	int port;
	int enabled;
};

struct service services[MODES_NET_SERVICES_NUM];

void modesInitNet(void) {
    int j;

	struct service svc[MODES_NET_SERVICES_NUM] = {
		{"Raw TCP output", &Modes.ros, Modes.net_output_raw_port, 1},
		{"Raw TCP input", &Modes.ris, Modes.net_input_raw_port, 1},
		{"Beast TCP output", &Modes.bos, Modes.net_output_beast_port, 1},
		{"Beast TCP input", &Modes.bis, Modes.net_input_beast_port, 1},
		{"HTTP server", &Modes.https, Modes.net_http_port, 1},
		{"Basestation TCP output", &Modes.sbsos, Modes.net_output_sbs_port, 1}
	};

	memcpy(&services, &svc, sizeof(svc));//services = svc;

    Modes.clients = NULL;

#ifdef _WIN32
    if ( (!Modes.wsaData.wVersion) 
      && (!Modes.wsaData.wHighVersion) ) {
      // Try to start the windows socket support
      if (WSAStartup(MAKEWORD(2,1),&Modes.wsaData) != 0) 
        {
        fprintf(stderr, "WSAStartup returned Error\n");
        }
      }
#endif

    for (j = 0; j < MODES_NET_SERVICES_NUM; j++) {
		services[j].enabled = (services[j].port != 0);
		if (services[j].enabled) {
			int s = anetTcpServer(Modes.aneterr, services[j].port, Modes.net_bind_address);
			if (s == -1) {
				fprintf(stderr, "Error opening the listening port %d (%s): %s\n",
					services[j].port, services[j].descr, Modes.aneterr);
				exit(1);
			}
			anetNonBlock(Modes.aneterr, s);
			*services[j].socket = s;
		} else {
			if (Modes.debug & MODES_DEBUG_NET) printf("%s port is disabled\n", services[j].descr);
		}
    }

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
}
//
//=========================================================================
//
// This function gets called from time to time when the decoding thread is
// awakened by new data arriving. This usually happens a few times every second
//
struct client * modesAcceptClients(void) {
    int fd, port;
    unsigned int j;
    struct client *c;

    for (j = 0; j < MODES_NET_SERVICES_NUM; j++) {
		if (services[j].enabled) {
			fd = anetTcpAccept(Modes.aneterr, *services[j].socket, NULL, &port);
			if (fd == -1) continue;

			anetNonBlock(Modes.aneterr, fd);
			c = (struct client *) malloc(sizeof(*c));
			c->service    = *services[j].socket;
			c->next       = Modes.clients;
			c->fd         = fd;
			c->buflen     = 0;
			Modes.clients = c;
			anetSetSendBuffer(Modes.aneterr,fd, (MODES_NET_SNDBUF_SIZE << Modes.net_sndbuf_size));

			if (*services[j].socket == Modes.sbsos) Modes.stat_sbs_connections++;
			if (*services[j].socket == Modes.ros)   Modes.stat_raw_connections++;
			if (*services[j].socket == Modes.bos)   Modes.stat_beast_connections++;

			j--; // Try again with the same listening port

			if (Modes.debug & MODES_DEBUG_NET)
				printf("Created new client %d\n", fd);
		}
    }
    return Modes.clients;
}
//
//=========================================================================
//
// On error free the client, collect the structure, adjust maxfd if needed.
//
void modesFreeClient(struct client *c) {

    // Unhook this client from the linked list of clients
    struct client *p = Modes.clients;
    if (p) {
        if (p == c) {
            Modes.clients = c->next;
        } else {
            while ((p) && (p->next != c)) {
                p = p->next;
            }
            if (p) {
                p->next = c->next;
            }
        }
    }

    free(c);
}
//
//=========================================================================
//
// Close the client connection and mark it as closed
//
void modesCloseClient(struct client *c) {
	close(c->fd);
    if (c->service == Modes.sbsos) {
        if (Modes.stat_sbs_connections) Modes.stat_sbs_connections--;
    } else if (c->service == Modes.ros) {
        if (Modes.stat_raw_connections) Modes.stat_raw_connections--;
    } else if (c->service == Modes.bos) {
        if (Modes.stat_beast_connections) Modes.stat_beast_connections--;
    }

    if (Modes.debug & MODES_DEBUG_NET)
        printf("Closing client %d\n", c->fd);

    c->fd = -1;
}
//
//=========================================================================
//
// Send the specified message to all clients listening for a given service
//
void modesSendAllClients(int service, void *msg, int len) {
    struct client *c = Modes.clients;

    while (c) {
        // Read next before servicing client incase the service routine deletes the client! 
        struct client *next = c->next;

        if (c->fd != -1) {
            if (c->service == service) {
#ifndef _WIN32
                int nwritten = write(c->fd, msg, len);
#else
                int nwritten = send(c->fd, msg, len, 0 );
#endif
                if (nwritten != len) {
                    modesCloseClient(c);
                }
            }
        } else {
            modesFreeClient(c);
        }
        c = next;
    }
}
//
//=========================================================================
//
// Write raw output in Beast Binary format with Timestamp to TCP clients
//
void modesSendBeastOutput(struct modesMessage *mm) {
    char *p = &Modes.beastOut[Modes.beastOutUsed];
    int  msgLen = mm->msgbits / 8;
    char * pTimeStamp;
    char ch;
    int  j;
    int  iOutLen = msgLen + 9; // Escape, msgtype, timestamp, sigLevel, msg

    *p++ = 0x1a;
    if      (msgLen == MODES_SHORT_MSG_BYTES)
      {*p++ = '2';}
    else if (msgLen == MODES_LONG_MSG_BYTES)
      {*p++ = '3';}
    else if (msgLen == MODEAC_MSG_BYTES)
      {*p++ = '1';}
    else
      {return;}

    pTimeStamp = (char *) &mm->timestampMsg;
    for (j = 5; j >= 0; j--) {
        *p++ = (ch = pTimeStamp[j]);
        if (0x1A == ch) {*p++ = ch; iOutLen++;}
    }

    *p++ = (ch = mm->signalLevel);
    if (0x1A == ch) {*p++ = ch; iOutLen++;}

    for (j = 0; j < msgLen; j++) {
        *p++ = (ch = mm->msg[j]);
        if (0x1A == ch) {*p++ = ch; iOutLen++;} 
    }

    Modes.beastOutUsed +=  iOutLen;
    if (Modes.beastOutUsed >= Modes.net_output_raw_size)
      {
      modesSendAllClients(Modes.bos, Modes.beastOut, Modes.beastOutUsed);
      Modes.beastOutUsed = 0;
      Modes.net_output_raw_rate_count = 0;
      }
}
//
//=========================================================================
//
// Write raw output to TCP clients
//
void modesSendRawOutput(struct modesMessage *mm) {
    char *p = &Modes.rawOut[Modes.rawOutUsed];
    int  msgLen = mm->msgbits / 8;
    int j;
    unsigned char * pTimeStamp;

    if (Modes.mlat && mm->timestampMsg) {
        *p++ = '@';
        pTimeStamp = (unsigned char *) &mm->timestampMsg;
        for (j = 5; j >= 0; j--) {
            sprintf(p, "%02X", pTimeStamp[j]);
            p += 2;
        }
        Modes.rawOutUsed += 12; // additional 12 characters for timestamp
    } else
        *p++ = '*';

    for (j = 0; j < msgLen; j++) {
        sprintf(p, "%02X", mm->msg[j]);
        p += 2;
    }

    *p++ = ';';
    *p++ = '\n';

    Modes.rawOutUsed += ((msgLen*2) + 3);
    if (Modes.rawOutUsed >= Modes.net_output_raw_size)
      {
      modesSendAllClients(Modes.ros, Modes.rawOut, Modes.rawOutUsed);
      Modes.rawOutUsed = 0;
      Modes.net_output_raw_rate_count = 0;
      }
}
//
//=========================================================================
//
// Write SBS output to TCP clients
// The message structure mm->bFlags tells us what has been updated by this message
//
void modesSendSBSOutput(struct modesMessage *mm) {
    char msg[256], *p = msg;
    uint32_t     offset;
    struct timeb epocTime_receive, epocTime_now;
    struct tm    stTime_receive, stTime_now;
    int          msgType;

    //
    // SBS BS style output checked against the following reference
    // http://www.homepages.mcb.net/bones/SBS/Article/Barebones42_Socket_Data.htm - seems comprehensive
    //

    // Decide on the basic SBS Message Type
    if        ((mm->msgtype ==  4) || (mm->msgtype == 20)) {
        msgType = 5;
    } else if ((mm->msgtype ==  5) || (mm->msgtype == 21)) {
        msgType = 6;
    } else if ((mm->msgtype ==  0) || (mm->msgtype == 16)) {
        msgType = 7;
    } else if  (mm->msgtype == 11) {
        msgType = 8;
    } else if ((mm->msgtype != 17) && (mm->msgtype != 18)) {
        return;
    } else if ((mm->metype >= 1) && (mm->metype <=  4)) {
        msgType = 1;
    } else if ((mm->metype >= 5) && (mm->metype <=  8)) {
        if (mm->bFlags & MODES_ACFLAGS_LATLON_VALID)
            {msgType = 2;}
        else
            {msgType = 7;}
    } else if ((mm->metype >= 9) && (mm->metype <= 18)) {
        if (mm->bFlags & MODES_ACFLAGS_LATLON_VALID)
            {msgType = 3;}
        else
            {msgType = 7;}
    } else if (mm->metype !=  19) {
        return;
    } else if ((mm->mesub == 1) || (mm->mesub == 2)) {
        msgType = 4;
    } else {
        return;
    }

    // Fields 1 to 6 : SBS message type and ICAO address of the aircraft and some other stuff
    p += sprintf(p, "MSG,%d,111,11111,%06X,111111,", msgType, mm->addr); 

    // Find current system time
    ftime(&epocTime_now);                                         // get the current system time & date
    stTime_now = *localtime(&epocTime_now.time);

    // Find message reception time
    if (mm->timestampMsg && !mm->remote) {                        // Make sure the records' timestamp is valid before using it
        epocTime_receive = Modes.stSystemTimeBlk;                 // This is the time of the start of the Block we're processing
        offset   = (int) (mm->timestampMsg - Modes.timestampBlk); // This is the time (in 12Mhz ticks) into the Block
        offset   = offset / 12000;                                // convert to milliseconds
        epocTime_receive.millitm += offset;                       // add on the offset time to the Block start time
        if (epocTime_receive.millitm > 999) {                     // if we've caused an overflow into the next second...
            epocTime_receive.millitm -= 1000;
            epocTime_receive.time ++;                             //    ..correct the overflow
        }
        stTime_receive = *localtime(&epocTime_receive.time);
    } else {
        epocTime_receive = epocTime_now;                          // We don't have a usable reception time; use the current system time
        stTime_receive = stTime_now;
    }

    // Fields 7 & 8 are the message reception time and date
    p += sprintf(p, "%04d/%02d/%02d,", (stTime_receive.tm_year+1900),(stTime_receive.tm_mon+1), stTime_receive.tm_mday);
    p += sprintf(p, "%02d:%02d:%02d.%03d,", stTime_receive.tm_hour, stTime_receive.tm_min, stTime_receive.tm_sec, epocTime_receive.millitm);

    // Fields 9 & 10 are the current time and date
    p += sprintf(p, "%04d/%02d/%02d,", (stTime_now.tm_year+1900),(stTime_now.tm_mon+1), stTime_now.tm_mday);
    p += sprintf(p, "%02d:%02d:%02d.%03d", stTime_now.tm_hour, stTime_now.tm_min, stTime_now.tm_sec, epocTime_now.millitm);

    // Field 11 is the callsign (if we have it)
    if (mm->bFlags & MODES_ACFLAGS_CALLSIGN_VALID) {p += sprintf(p, ",%s", mm->flight);}
    else                                           {p += sprintf(p, ",");}

    // Field 12 is the altitude (if we have it) - force to zero if we're on the ground
    if ((mm->bFlags & MODES_ACFLAGS_AOG_GROUND) == MODES_ACFLAGS_AOG_GROUND) {
        p += sprintf(p, ",0");
    } else if (mm->bFlags & MODES_ACFLAGS_ALTITUDE_VALID) {
        p += sprintf(p, ",%d", mm->altitude);
    } else {
        p += sprintf(p, ",");
    }

    // Field 13 is the ground Speed (if we have it)
    if (mm->bFlags & MODES_ACFLAGS_SPEED_VALID) {
        p += sprintf(p, ",%d", mm->velocity);
    } else {
        p += sprintf(p, ","); 
    }

    // Field 14 is the ground Heading (if we have it)       
    if (mm->bFlags & MODES_ACFLAGS_HEADING_VALID) {
        p += sprintf(p, ",%d", mm->heading);
    } else {
        p += sprintf(p, ",");
    }

    // Fields 15 and 16 are the Lat/Lon (if we have it)
    if (mm->bFlags & MODES_ACFLAGS_LATLON_VALID) {p += sprintf(p, ",%1.5f,%1.5f", mm->fLat, mm->fLon);}
    else                                         {p += sprintf(p, ",,");}

    // Field 17 is the VerticalRate (if we have it)
    if (mm->bFlags & MODES_ACFLAGS_VERTRATE_VALID) {p += sprintf(p, ",%d", mm->vert_rate);}
    else                                           {p += sprintf(p, ",");}

    // Field 18 is  the Squawk (if we have it)
    if (mm->bFlags & MODES_ACFLAGS_SQUAWK_VALID) {p += sprintf(p, ",%x", mm->modeA);}
    else                                         {p += sprintf(p, ",");}

    // Field 19 is the Squawk Changing Alert flag (if we have it)
    if (mm->bFlags & MODES_ACFLAGS_FS_VALID) {
        if ((mm->fs >= 2) && (mm->fs <= 4)) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 20 is the Squawk Emergency flag (if we have it)
    if (mm->bFlags & MODES_ACFLAGS_SQUAWK_VALID) {
        if ((mm->modeA == 0x7500) || (mm->modeA == 0x7600) || (mm->modeA == 0x7700)) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 21 is the Squawk Ident flag (if we have it)
    if (mm->bFlags & MODES_ACFLAGS_FS_VALID) {
        if ((mm->fs >= 4) && (mm->fs <= 5)) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 22 is the OnTheGround flag (if we have it)
    if (mm->bFlags & MODES_ACFLAGS_AOG_VALID) {
        if (mm->bFlags & MODES_ACFLAGS_AOG) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    p += sprintf(p, "\r\n");
    modesSendAllClients(Modes.sbsos, msg, p-msg);
}
//
//=========================================================================
//
void modesQueueOutput(struct modesMessage *mm) {
    if (Modes.stat_sbs_connections)   {modesSendSBSOutput(mm);}
    if (Modes.stat_beast_connections) {modesSendBeastOutput(mm);}
    if (Modes.stat_raw_connections)   {modesSendRawOutput(mm);}
}
//
//=========================================================================
//
// This function decodes a Beast binary format message
//
// The message is passed to the higher level layers, so it feeds
// the selected screen output, the network output and so forth.
//
// If the message looks invalid it is silently discarded.
//
// The function always returns 0 (success) to the caller as there is no
// case where we want broken messages here to close the client connection.
//
int decodeBinMessage(struct client *c, char *p) {
    int msgLen = 0;
    int  j;
    char ch;
    char * ptr;
    unsigned char msg[MODES_LONG_MSG_BYTES];
    struct modesMessage mm;
    MODES_NOTUSED(c);
    memset(&mm, 0, sizeof(mm));

    ch = *p++; /// Get the message type
    if (0x1A == ch) {p++;} 

    if       ((ch == '1') && (Modes.mode_ac)) { // skip ModeA/C unless user enables --modes-ac
        msgLen = MODEAC_MSG_BYTES;
    } else if (ch == '2') {
        msgLen = MODES_SHORT_MSG_BYTES;
    } else if (ch == '3') {
        msgLen = MODES_LONG_MSG_BYTES;
    }

    if (msgLen) {
        // Mark messages received over the internet as remote so that we don't try to
        // pass them off as being received by this instance when forwarding them
        mm.remote      =    1;

        ptr = (char*) &mm.timestampMsg;
        for (j = 0; j < 6; j++) { // Grab the timestamp (big endian format)
            ptr[5-j] = ch = *p++; 
            if (0x1A == ch) {p++;}
        }

        mm.signalLevel = ch = *p++;  // Grab the signal level
        if (0x1A == ch) {p++;}

        for (j = 0; j < msgLen; j++) { // and the data
            msg[j] = ch = *p++;
            if (0x1A == ch) {p++;}
        }

        if (msgLen == MODEAC_MSG_BYTES) { // ModeA or ModeC
            decodeModeAMessage(&mm, ((msg[0] << 8) | msg[1]));
        } else {
            decodeModesMessage(&mm, msg);
        }

        useModesMessage(&mm);
    }
    return (0);
}
//
//=========================================================================
//
// Turn an hex digit into its 4 bit decimal value.
// Returns -1 if the digit is not in the 0-F range.
//
int hexDigitVal(int c) {
    c = tolower(c);
    if (c >= '0' && c <= '9') return c-'0';
    else if (c >= 'a' && c <= 'f') return c-'a'+10;
    else return -1;
}
//
//=========================================================================
//
// This function decodes a string representing message in raw hex format
// like: *8D4B969699155600E87406F5B69F; The string is null-terminated.
// 
// The message is passed to the higher level layers, so it feeds
// the selected screen output, the network output and so forth.
// 
// If the message looks invalid it is silently discarded.
//
// The function always returns 0 (success) to the caller as there is no 
// case where we want broken messages here to close the client connection.
//
int decodeHexMessage(struct client *c, char *hex) {
    int l = strlen(hex), j;
    unsigned char msg[MODES_LONG_MSG_BYTES];
    struct modesMessage mm;
    MODES_NOTUSED(c);
    memset(&mm, 0, sizeof(mm));

    // Mark messages received over the internet as remote so that we don't try to
    // pass them off as being received by this instance when forwarding them
    mm.remote      =    1;
    mm.signalLevel = 0xFF;

    // Remove spaces on the left and on the right
    while(l && isspace(hex[l-1])) {
        hex[l-1] = '\0'; l--;
    }
    while(isspace(*hex)) {
        hex++; l--;
    }

    // Turn the message into binary.
    // Accept *-AVR raw @-AVR/BEAST timeS+raw %-AVR timeS+raw (CRC good) <-BEAST timeS+sigL+raw
    // and some AVR records that we can understand
    if (hex[l-1] != ';') {return (0);} // not complete - abort

    switch(hex[0]) {
        case '<': {
            mm.signalLevel = (hexDigitVal(hex[13])<<4) | hexDigitVal(hex[14]);
            hex += 15; l -= 16; // Skip <, timestamp and siglevel, and ;
            break;}

        case '@':     // No CRC check
        case '%': {   // CRC is OK
            hex += 13; l -= 14; // Skip @,%, and timestamp, and ;
            break;}

        case '*':
        case ':': {
            hex++; l-=2; // Skip * and ;
            break;}

        default: {
            return (0); // We don't know what this is, so abort
            break;}
    }

    if ( (l != (MODEAC_MSG_BYTES      * 2)) 
      && (l != (MODES_SHORT_MSG_BYTES * 2)) 
      && (l != (MODES_LONG_MSG_BYTES  * 2)) )
        {return (0);} // Too short or long message... broken

    if ( (0 == Modes.mode_ac) 
      && (l == (MODEAC_MSG_BYTES * 2)) ) 
        {return (0);} // Right length for ModeA/C, but not enabled

    for (j = 0; j < l; j += 2) {
        int high = hexDigitVal(hex[j]);
        int low  = hexDigitVal(hex[j+1]);

        if (high == -1 || low == -1) return 0;
        msg[j/2] = (high << 4) | low;
    }

    if (l == (MODEAC_MSG_BYTES * 2)) {  // ModeA or ModeC
        decodeModeAMessage(&mm, ((msg[0] << 8) | msg[1]));
    } else {       // Assume ModeS
        decodeModesMessage(&mm, msg);
    }

    useModesMessage(&mm);
    return (0);
}
//
//=========================================================================
//
// Return a description of planes in json. No metric conversion
//
char *aircraftsToJson(int *len) {
    time_t now = time(NULL);
    struct aircraft *a = Modes.aircrafts;
    int buflen = 1024; // The initial buffer is incremented as needed
    char *buf = (char *) malloc(buflen), *p = buf;
    int l;

    l = snprintf(p,buflen,"[\n");
    p += l; buflen -= l;
    while(a) {
        int position = 0;
        int track = 0;

        if (a->modeACflags & MODEAC_MSG_FLAG) { // skip any fudged ICAO records Mode A/C
            a = a->next;
            continue;
        }

        if (a->bFlags & MODES_ACFLAGS_LATLON_VALID) {
            position = 1;
        }
        
        if (a->bFlags & MODES_ACFLAGS_HEADING_VALID) {
            track = 1;
        }
        
        // No metric conversion
        l = snprintf(p,buflen,
            "{\"hex\":\"%06x\", \"squawk\":\"%04x\", \"flight\":\"%s\", \"lat\":%f, "
            "\"lon\":%f, \"validposition\":%d, \"altitude\":%d,  \"vert_rate\":%d,\"track\":%d, \"validtrack\":%d,"
            "\"speed\":%d, \"messages\":%ld, \"seen\":%d},\n",
            a->addr, a->modeA, a->flight, a->lat, a->lon, position, a->altitude, a->vert_rate, a->track, track,
            a->speed, a->messages, (int)(now - a->seen));
        p += l; buflen -= l;
        
        //Resize if needed
        if (buflen < 256) {
            int used = p-buf;
            buflen += 1024; // Our increment.
            buf = (char *) realloc(buf,used+buflen);
            p = buf+used;
        }
        
        a = a->next;
    }

    //Remove the final comma if any, and closes the json array.
    if (*(p-2) == ',') {
        *(p-2) = '\n';
        p--;
        buflen++;
    }

    l = snprintf(p,buflen,"]\n");
    p += l; buflen -= l;

    *len = p-buf;
    return buf;
}
//
//=========================================================================
//
#define MODES_CONTENT_TYPE_HTML "text/html;charset=utf-8"
#define MODES_CONTENT_TYPE_CSS  "text/css;charset=utf-8"
#define MODES_CONTENT_TYPE_JSON "application/json;charset=utf-8"
#define MODES_CONTENT_TYPE_JS   "application/javascript;charset=utf-8"
//
// Get an HTTP request header and write the response to the client.
// gain here we assume that the socket buffer is enough without doing
// any kind of userspace buffering.
//
// Returns 1 on error to signal the caller the client connection should
// be closed.
//
int handleHTTPRequest(struct client *c, char *p) {
    char hdr[512];
    int clen, hdrlen;
    int httpver, keepalive;
    int statuscode = 500;
    char *url, *content;
    char ctype[48];
    char getFile[1024];
    char *ext;

    if (Modes.debug & MODES_DEBUG_NET)
        printf("\nHTTP request: %s\n", c->buf);

    // Minimally parse the request.
    httpver = (strstr(p, "HTTP/1.1") != NULL) ? 11 : 10;
    if (httpver == 10) {
        // HTTP 1.0 defaults to close, unless otherwise specified.
        //keepalive = strstr(p, "Connection: keep-alive") != NULL;
    } else if (httpver == 11) {
        // HTTP 1.1 defaults to keep-alive, unless close is specified.
        //keepalive = strstr(p, "Connection: close") == NULL;
    }
    keepalive = 0;

    // Identify he URL.
    p = strchr(p,' ');
    if (!p) return 1; // There should be the method and a space
    url = ++p;        // Now this should point to the requested URL
    p = strchr(p, ' ');
    if (!p) return 1; // There should be a space before HTTP/
    *p = '\0';

    if (Modes.debug & MODES_DEBUG_NET) {
        printf("\nHTTP keep alive: %d\n", keepalive);
        printf("HTTP requested URL: %s\n\n", url);
    }
    
    if (strlen(url) < 2) {
        snprintf(getFile, sizeof getFile, "%s/gmap.html", HTMLPATH); // Default file
    } else {
        snprintf(getFile, sizeof getFile, "%s/%s", HTMLPATH, url);
    }

    // Select the content to send, we have just two so far:
    // "/" -> Our google map application.
    // "/data.json" -> Our ajax request to update planes.
    if (strstr(url, "/data.json")) {
        statuscode = 200;
        content = aircraftsToJson(&clen);
        //snprintf(ctype, sizeof ctype, MODES_CONTENT_TYPE_JSON);
    } else {
        struct stat sbuf;
        int fd = -1;
        char *rp, *hrp;

        rp = realpath(getFile, NULL);
        hrp = realpath(HTMLPATH, NULL);
        hrp = (hrp ? hrp : HTMLPATH);
        clen = -1;
        content = strdup("Server error occured");
        if (rp && (!strncmp(hrp, rp, strlen(hrp)))) {
            if (stat(getFile, &sbuf) != -1 && (fd = open(getFile, O_RDONLY)) != -1) {
                content = (char *) realloc(content, sbuf.st_size);
                if (read(fd, content, sbuf.st_size) != -1) {
                    clen = sbuf.st_size;
                    statuscode = 200;
                }
            }
        } else {
            errno = ENOENT;
        }

        if (clen < 0) {
            content = realloc(content, 128);
            clen = snprintf(content, 128,"Error opening HTML file: %s", strerror(errno));
            statuscode = 404;
        }
        
        if (fd != -1) {
            close(fd);
        }
    }

    // Get file extension and content type
    snprintf(ctype, sizeof ctype, MODES_CONTENT_TYPE_HTML); // Default content type
    ext = strrchr(getFile, '.');

    if (strlen(ext) > 0) {
        if (strstr(ext, ".json")) {
            snprintf(ctype, sizeof ctype, MODES_CONTENT_TYPE_JSON);
        } else if (strstr(ext, ".css")) {
            snprintf(ctype, sizeof ctype, MODES_CONTENT_TYPE_CSS);
        } else if (strstr(ext, ".js")) {
            snprintf(ctype, sizeof ctype, MODES_CONTENT_TYPE_JS);
        }
    }

    // Create the header and send the reply
    hdrlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d \r\n"
        "Server: Dump1090\r\n"
        "Content-Type: %s\r\n"
        "Connection: %s\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-cache, must-revalidate\r\n"
        "Expires: Sat, 26 Jul 1997 05:00:00 GMT\r\n"
        "\r\n",
        statuscode,
        ctype,
        keepalive ? "keep-alive" : "close",
        clen);

    if (Modes.debug & MODES_DEBUG_NET) {
        printf("HTTP Reply header:\n%s", hdr);
    }

    // Send header and content.
#ifndef _WIN32
    if ( (write(c->fd, hdr, hdrlen) != hdrlen) 
      || (write(c->fd, content, clen) != clen) ) {
#else
    if ( (send(c->fd, hdr, hdrlen, 0) != hdrlen) 
      || (send(c->fd, content, clen, 0) != clen) ) {
#endif
        free(content);
        return 1;
    }
    free(content);
    Modes.stat_http_requests++;
    return !keepalive;
}
//
//=========================================================================
//
// This function polls the clients using read() in order to receive new
// messages from the net.
//
// The message is supposed to be separated from the next message by the
// separator 'sep', which is a null-terminated C string.
//
// Every full message received is decoded and passed to the higher layers
// calling the function's 'handler'.
//
// The handler returns 0 on success, or 1 to signal this function we should
// close the connection with the client in case of non-recoverable errors.
//
void modesReadFromClient(struct client *c, char *sep,
                         int(*handler)(struct client *, char *)) {
    int left;
    int nread;
    int fullmsg;
    int bContinue = 1;
    char *s, *e, *p;

    while(bContinue) {

        fullmsg = 0;
        left = MODES_CLIENT_BUF_SIZE - c->buflen;
        // If our buffer is full discard it, this is some badly formatted shit
        if (left <= 0) {
            c->buflen = 0;
            left = MODES_CLIENT_BUF_SIZE;
            // If there is garbage, read more to discard it ASAP
        }
#ifndef _WIN32
        nread = read(c->fd, c->buf+c->buflen, left);
#else
        nread = recv(c->fd, c->buf+c->buflen, left, 0);
        if (nread < 0) {errno = WSAGetLastError();}
#endif
        if (nread == 0) {
			modesCloseClient(c);
			return;
		}

        // If we didn't get all the data we asked for, then return once we've processed what we did get.
        if (nread != left) {
            bContinue = 0;
        }
#ifndef _WIN32
        if ( (nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || nread == 0 ) { // Error, or end of file
#else
        if ( (nread < 0) && (errno != EWOULDBLOCK)) { // Error, or end of file
#endif
            modesCloseClient(c);
            return;
        }
        if (nread <= 0) {
            break; // Serve next client
        }
        c->buflen += nread;

        // Always null-term so we are free to use strstr() (it won't affect binary case)
        c->buf[c->buflen] = '\0';

        e = s = c->buf;                                // Start with the start of buffer, first message

        if (c->service == Modes.bis) {
            // This is the Beast Binary scanning case.
            // If there is a complete message still in the buffer, there must be the separator 'sep'
            // in the buffer, note that we full-scan the buffer at every read for simplicity.

            left = c->buflen;                                  // Length of valid search for memchr()
            while (left && ((s = memchr(e, (char) 0x1a, left)) != NULL)) { // The first byte of buffer 'should' be 0x1a
                s++;                                           // skip the 0x1a
                if        (*s == '1') {
                    e = s + MODEAC_MSG_BYTES      + 8;         // point past remainder of message
                } else if (*s == '2') {
                    e = s + MODES_SHORT_MSG_BYTES + 8;
                } else if (*s == '3') {
                    e = s + MODES_LONG_MSG_BYTES  + 8;
                } else {
                    e = s;                                     // Not a valid beast message, skip
                    left = &(c->buf[c->buflen]) - e;
                    continue;
                }
                // we need to be careful of double escape characters in the message body
                for (p = s; p < e; p++) {
                    if (0x1A == *p) {
                        p++; e++;
                        if (e > &(c->buf[c->buflen])) {
                            break;
                        }
                    }
                }
                left = &(c->buf[c->buflen]) - e;
                if (left < 0) {                                // Incomplete message in buffer
                    e = s - 1;                                 // point back at last found 0x1a.
                    break;
                }
                // Have a 0x1a followed by 1, 2 or 3 - pass message less 0x1a to handler.
                if (handler(c, s)) {
                    modesCloseClient(c);
                    return;
                }
                fullmsg = 1;
            }
            s = e;     // For the buffer remainder below

        } else {
            //
            // This is the ASCII scanning case, AVR RAW or HTTP at present
            // If there is a complete message still in the buffer, there must be the separator 'sep'
            // in the buffer, note that we full-scan the buffer at every read for simplicity.
            //
            while ((e = strstr(s, sep)) != NULL) { // end of first message if found
                *e = '\0';                         // The handler expects null terminated strings
                if (handler(c, s)) {               // Pass message to handler.
                    modesCloseClient(c);           // Handler returns 1 on error to signal we .
                    return;                        // should close the client connection
                }
                s = e + strlen(sep);               // Move to start of next message
                fullmsg = 1;
            }
        }

        if (fullmsg) {                             // We processed something - so
            c->buflen = &(c->buf[c->buflen]) - s;  //     Update the unprocessed buffer length
            memmove(c->buf, s, c->buflen);         //     Move what's remaining to the start of the buffer
        } else {                                   // If no message was decoded process the next client
            break;
        }
    }
}
//
//=========================================================================
//
// Read data from clients. This function actually delegates a lower-level
// function that depends on the kind of service (raw, http, ...).
//
void modesReadFromClients(void) {

    struct client *c = modesAcceptClients();

    while (c) {
            // Read next before servicing client incase the service routine deletes the client! 
            struct client *next = c->next;

        if (c->fd >= 0) {
            if (c->service == Modes.ris) {
                modesReadFromClient(c,"\n",decodeHexMessage);
            } else if (c->service == Modes.bis) {
                modesReadFromClient(c,"",decodeBinMessage);
            } else if (c->service == Modes.https) {
                modesReadFromClient(c,"\r\n\r\n",handleHTTPRequest);
            }
        } else {
            modesFreeClient(c);
        }
        c = next;
    }
}
//
// =============================== Network IO ===========================
//
