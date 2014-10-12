// ppup1090, a Mode S PlanePlotter Uploader for dump1090 devices.
//
// Copyright (C) 2013 by Malcolm Robb <Support@ATTAvionics.com>
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
#include "coaa.h"
#include "ppup1090.h"
//
// ============================= Utility functions ==========================
//
void sigintHandler(int dummy) {
    NOTUSED(dummy);
    signal(SIGINT, SIG_DFL);  // reset signal handler - bit extra safety
    Modes.exit = 1;           // Signal to threads that we are done
}
//
// =============================== Initialization ===========================
//
void ppup1090InitConfig(void) {

    int iErr;

    // Default everything to zero/NULL
    memset(&Modes,    0, sizeof(Modes));
    memset(&ppup1090, 0, sizeof(ppup1090));

    // Now initialise things that should not be 0/NULL to their defaults
    Modes.check_crc               = 1;
    Modes.quiet                   = 1;
    Modes.bEnableDFLogging        = 1;
    strcpy(ppup1090.net_input_beast_ipaddr,PPUP1090_NET_OUTPUT_IP_ADDRESS);
    Modes.net_input_beast_port    = MODES_NET_OUTPUT_BEAST_PORT;
    Modes.interactive_delete_ttl  = MODES_INTERACTIVE_DELETE_TTL;
    Modes.interactive_display_ttl = MODES_INTERACTIVE_DISPLAY_TTL;
    Modes.fUserLat                = MODES_USER_LATITUDE_DFLT;
    Modes.fUserLon                = MODES_USER_LONGITUDE_DFLT;

    if ((iErr = openCOAA()))
    {
        fprintf(stderr, "Error 0x%X initialising uploader\n", iErr);
        exit(1);
    }
}
//
//=========================================================================
//
void ppup1090Init(void) {

    int iErr;

    pthread_mutex_init(&Modes.pDF_mutex,NULL);
    pthread_mutex_init(&Modes.data_mutex,NULL);
    pthread_cond_init(&Modes.data_cond,NULL);

    // Allocate the various buffers used by Modes
    if ( NULL == (Modes.icao_cache = (uint32_t *) malloc(sizeof(uint32_t) * MODES_ICAO_CACHE_LEN * 2)))
    {
        fprintf(stderr, "Out of memory allocating data buffer.\n");
        exit(1);
    }

    // Clear the buffers that have just been allocated, just in-case
    memset(Modes.icao_cache, 0,   sizeof(uint32_t) * MODES_ICAO_CACHE_LEN * 2);

    // Validate the users Lat/Lon home location inputs
    if ( (Modes.fUserLat >   90.0)  // Latitude must be -90 to +90
      || (Modes.fUserLat <  -90.0)  // and 
      || (Modes.fUserLon >  360.0)  // Longitude must be -180 to +360
      || (Modes.fUserLon < -180.0) ) {
        Modes.fUserLat = Modes.fUserLon = 0.0;
    } else if (Modes.fUserLon > 180.0) { // If Longitude is +180 to +360, make it -180 to 0
        Modes.fUserLon -= 360.0;
    }
    // If both Lat and Lon are 0.0 then the users location is either invalid/not-set, or (s)he's in the 
    // Atlantic ocean off the west coast of Africa. This is unlikely to be correct. 
    // Set the user LatLon valid flag only if either Lat or Lon are non zero. Note the Greenwich meridian 
    // is at 0.0 Lon,so we must check for either fLat or fLon being non zero not both. 
    // Testing the flag at runtime will be much quicker than ((fLon != 0.0) || (fLat != 0.0))
    Modes.bUserFlags &= ~MODES_USER_LATLON_VALID;
    if ((Modes.fUserLat != 0.0) || (Modes.fUserLon != 0.0)) {
        Modes.bUserFlags |= MODES_USER_LATLON_VALID;
    }

    // Prepare error correction tables
    modesInitErrorInfo();

    // Setup the uploader - read the user paramaters from the coaa.h header file
    coaa1090.ppIPAddr = ppup1090.net_pp_ipaddr;
    coaa1090.fUserLat = MODES_USER_LATITUDE_DFLT;
    coaa1090.fUserLon = MODES_USER_LONGITUDE_DFLT;
    strcpy(coaa1090.strAuthCode,STR(USER_AUTHCODE));
    strcpy(coaa1090.strRegNo,   STR(USER_REGNO));
    strcpy(coaa1090.strVersion, MODES_DUMP1090_VERSION);

    if ((iErr = initCOAA (coaa1090)))
    {
        fprintf(stderr, "Error 0x%X initialising uploader\n", iErr);
        exit(1);
    }
}
//
// ================================ Main ====================================
//
void showHelp(void) {
    printf(
"-----------------------------------------------------------------------------\n"
"|    ppup1090 RPi Uploader for COAA Planeplotter         Ver : "MODES_DUMP1090_VERSION " |\n"
"-----------------------------------------------------------------------------\n"
  "--net-bo-ipaddr <IPv4>   TCP Beast output listen IPv4 (default: 127.0.0.1)\n"
  "--net-bo-port <port>     TCP Beast output listen port (default: 30005)\n"
  "--net-pp-ipaddr <IPv4>   Plane Plotter LAN IPv4 Address (default: 0.0.0.0)\n"
  "--quiet                  Disable output to stdout. Use for daemon applications\n"
  "--help                   Show this help\n"
    );
}

#ifdef _WIN32
void showCopyright(void) {
    uint64_t llTime = time(NULL) + 1;

    printf(
"-----------------------------------------------------------------------------\n"
"|    ppup1090 RPi Uploader for COAA Planeplotter         Ver : "MODES_DUMP1090_VERSION " |\n"
"-----------------------------------------------------------------------------\n"
"\n"
" Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>\n"
" Copyright (C) 2014 by Malcolm Robb <support@attavionics.com>\n"
"\n"
" All rights reserved.\n"
"\n"
" THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n"
" ""AS IS"" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n"
" LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n"
" A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT\n"
" HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n"
" SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT\n"
" LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n"
" DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n"
" THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
" (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n"
" OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
"\n"
" For further details refer to <https://github.com/MalcolmRobb/dump1090>\n" 
"\n"
    );

  // delay for 1 second to give the user a chance to read the copyright
  while (llTime >= time(NULL)) {}
}
#endif
//
//=========================================================================
//
int main(int argc, char **argv) {
    int j, fd;
    struct client *c;

    // Set sane defaults

    ppup1090InitConfig();
    signal(SIGINT, sigintHandler); // Define Ctrl/C handler (exit program)

    // Parse the command line options
    for (j = 1; j < argc; j++) {
        int more = ((j + 1) < argc); // There are more arguments

        if        (!strcmp(argv[j],"--net-bo-port") && more) {
            Modes.net_input_beast_port = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--net-bo-ipaddr") && more) {
            strcpy(ppup1090.net_input_beast_ipaddr, argv[++j]);
        } else if (!strcmp(argv[j],"--net-pp-ipaddr") && more) {
            inet_aton(argv[++j], (void *)&ppup1090.net_pp_ipaddr);
        } else if (!strcmp(argv[j],"--quiet")) {
            ppup1090.quiet = 1;
        } else if (!strcmp(argv[j],"--help")) {
            showHelp();
            exit(0);
        } else {
            fprintf(stderr, "Unknown or not enough arguments for option '%s'.\n\n", argv[j]);
            showHelp();
            exit(1);
        }
    }

#ifdef _WIN32
    // Try to comply with the Copyright license conditions for binary distribution
    if (!ppup1090.quiet) {showCopyright();}
#endif

    // Initialization
    ppup1090Init();

    // Try to connect to the selected ip address and port. We only support *ONE* input connection which we initiate.here.
    if ((fd = anetTcpConnect(Modes.aneterr, ppup1090.net_input_beast_ipaddr, Modes.net_input_beast_port)) == ANET_ERR) {
        fprintf(stderr, "Failed to connect to %s:%d\n", ppup1090.net_input_beast_ipaddr, Modes.net_input_beast_port);
        exit(1);
    }
    //
    // Setup a service callback client structure for a beast binary input (from dump1090)
    // This is a bit dodgy under Windows. The fd parameter is a handle to the internet
    // socket on which we are receiving data. Under Linux, these seem to start at 0 and
    // count upwards. However, Windows uses "HANDLES" and these don't nececeriy start at 0.
    // dump1090 limits fd to values less than 1024, and then uses the fd parameter to
    // index into an array of clients. This is ok-ish if handles are allocated up from 0.
    // However, there is no gaurantee that Windows will behave like this, and if Windows
    // allocates a handle greater than 1024, then dump1090 won't like it. On my test machine,
    // the first Windows handle is usually in the 0x54 (84 decimal) region.

    c = (struct client *) malloc(sizeof(*c));
    c->next    = NULL;
    c->buflen  = 0;
    c->fd      =
    c->service =
    Modes.bis  = fd;
    Modes.clients = c;

    // Keep going till the user does something that stops us
    while (!Modes.exit) {
        modesReadFromClient(c,"",decodeBinMessage);
        interactiveRemoveStaleAircrafts();
        postCOAA ();
    }

    // The user has stopped us, so close any socket we opened
    if (fd != ANET_ERR)
      {close(fd);}

    closeCOAA ();
#ifndef _WIN32
    pthread_exit(0);
#else
    return (0);
#endif
}
//
//=========================================================================
//
