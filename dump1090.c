/* dump1090, a Mode S messages decoder for RTLSDR devices.
 *
 * Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _WIN32
    #include <stdio.h>
    #include <string.h>
    #include <stdlib.h>
    #include <pthread.h>
    #include <stdint.h>
    #include <errno.h>
    #include <unistd.h>
    #include <math.h>
    #include <sys/time.h>
    #include <sys/timeb.h>
    #include <signal.h>
    #include <fcntl.h>
    #include <ctype.h>
    #include <sys/stat.h>
    #include "rtl-sdr.h"
    #include "anet.h"
#else
    #include "dump1090.h" //Put everything Windows specific in here
    #include "rtl-sdr.h"
#endif

// File Version number 
// ====================
// Format is : MajorVer.MinorVer.DayMonth.Year"
// MajorVer changes only with significant changes
// MinorVer changes when additional features are added, but not for bug fixes (range 00-99)
// DayDate & Year changes for all changes, including for bug fixes. It represent the release date of the update
//
#define MODES_DUMP1090_VERSION     "1.02.2604.13"

#define MODES_DEFAULT_RATE         2000000
#define MODES_DEFAULT_FREQ         1090000000
#define MODES_DEFAULT_WIDTH        1000
#define MODES_DEFAULT_HEIGHT       700
#define MODES_ASYNC_BUF_NUMBER     12
#define MODES_ASYNC_BUF_SIZE       (16*16384)   /* 256k */
#define MODES_ASYNC_BUF_SAMPLES    (MODES_ASYNC_BUF_SIZE / 2) /* Each sample is 2 bytes */
#define MODES_AUTO_GAIN            -100         /* Use automatic gain. */
#define MODES_MAX_GAIN             999999       /* Use max available gain. */
#define MODES_MSG_SQUELCH_LEVEL    0x02FF       /* Average signal strength limit */
#define MODES_MSG_ENCODER_ERRS     3            /* Maximum number of encoding errors */

#define MODEAC_MSG_SAMPLES       (25 * 2)        /* include up to the SPI bit */
#define MODEAC_MSG_BYTES          2
#define MODEAC_MSG_SQUELCH_LEVEL  0x07FF         /* Average signal strength limit */
#define MODEAC_MSG_FLAG          (1<<0)
#define MODEAC_MSG_MODES_HIT     (1<<1)
#define MODEAC_MSG_MODEA_HIT     (1<<2)
#define MODEAC_MSG_MODEC_HIT     (1<<3)
#define MODEAC_MSG_MODEA_ONLY    (1<<4)
#define MODEAC_MSG_MODEC_OLD     (1<<5)

#define MODES_PREAMBLE_US        8              /* microseconds = bits */
#define MODES_PREAMBLE_SAMPLES  (MODES_PREAMBLE_US       * 2)
#define MODES_PREAMBLE_SIZE     (MODES_PREAMBLE_SAMPLES  * sizeof(uint16_t))
#define MODES_LONG_MSG_BYTES     14
#define MODES_SHORT_MSG_BYTES    7
#define MODES_LONG_MSG_BITS     (MODES_LONG_MSG_BYTES    * 8)
#define MODES_SHORT_MSG_BITS    (MODES_SHORT_MSG_BYTES   * 8)
#define MODES_LONG_MSG_SAMPLES  (MODES_LONG_MSG_BITS     * 2)
#define MODES_SHORT_MSG_SAMPLES (MODES_SHORT_MSG_BITS    * 2)
#define MODES_LONG_MSG_SIZE     (MODES_LONG_MSG_SAMPLES  * sizeof(uint16_t))
#define MODES_SHORT_MSG_SIZE    (MODES_SHORT_MSG_SAMPLES * sizeof(uint16_t))

#define MODES_RAWOUT_BUF_SIZE   (1500)           
#define MODES_RAWOUT_BUF_FLUSH  (MODES_RAWOUT_BUF_SIZE - 200)
#define MODES_RAWOUT_BUF_RATE   (1000)            // 1000 * 64mS = 1 Min approx

#define MODES_ICAO_CACHE_LEN 1024 /* Power of two required. */
#define MODES_ICAO_CACHE_TTL 60   /* Time to live of cached addresses. */
#define MODES_UNIT_FEET 0
#define MODES_UNIT_METERS 1

#define MODES_SBS_LAT_LONG_FRESH (1<<0)

#define MODES_DEBUG_DEMOD (1<<0)
#define MODES_DEBUG_DEMODERR (1<<1)
#define MODES_DEBUG_BADCRC (1<<2)
#define MODES_DEBUG_GOODCRC (1<<3)
#define MODES_DEBUG_NOPREAMBLE (1<<4)
#define MODES_DEBUG_NET (1<<5)
#define MODES_DEBUG_JS (1<<6)

/* When debug is set to MODES_DEBUG_NOPREAMBLE, the first sample must be
 * at least greater than a given level for us to dump the signal. */
#define MODES_DEBUG_NOPREAMBLE_LEVEL 25

#define MODES_INTERACTIVE_REFRESH_TIME 250      /* Milliseconds */
#define MODES_INTERACTIVE_ROWS 15               /* Rows on screen */
#define MODES_INTERACTIVE_TTL 60                /* TTL before being removed */

#define MODES_NET_MAX_FD 1024
#define MODES_NET_OUTPUT_SBS_PORT 30003
#define MODES_NET_OUTPUT_RAW_PORT 30002
#define MODES_NET_INPUT_RAW_PORT 30001
#define MODES_NET_HTTP_PORT 8080
#define MODES_CLIENT_BUF_SIZE 1024
#define MODES_NET_SNDBUF_SIZE (1024*64)

#define MODES_NOTUSED(V) ((void) V)

/* Structure used to describe a networking client. */
struct client {
    int fd;         /* File descriptor. */
    int service;    /* TCP port the client is connected to. */
    char buf[MODES_CLIENT_BUF_SIZE+1];    /* Read buffer. */
    int buflen;                         /* Amount of data on buffer. */
};

/* Structure used to describe an aircraft in iteractive mode. */
struct aircraft {
    uint32_t addr;      /* ICAO address */
    char flight[9];     /* Flight number */
    int altitude;       /* Altitude */
    int speed;          /* Velocity computed from EW and NS components. */
    int track;          /* Angle of flight. */
    time_t seen;        /* Time at which the last packet was received. */
    long messages;      /* Number of Mode S messages received. */
    int  modeA;         /* Squawk */
    int  modeC;         /* Altitude */
    long modeAcount;    /* Mode A Squawk hit Count */
    long modeCcount;    /* Mode C Altitude hit Count */
    int  modeACflags;   /* Flags for mode A/C recognition */
    /* Encoded latitude and longitude as extracted by odd and even
     * CPR encoded messages. */
    int odd_cprlat;
    int odd_cprlon;
    int even_cprlat;
    int even_cprlon;
    double lat, lon;    /* Coordinated obtained from CPR encoded data. */
    int sbsflags;
    uint64_t odd_cprtime, even_cprtime;
    struct aircraft *next; /* Next aircraft in our linked list. */
};

/* Program global state. */
struct {
    /* Internal state */
    pthread_t reader_thread;
    pthread_mutex_t data_mutex;     /* Mutex to synchronize buffer access. */
    pthread_cond_t data_cond;       /* Conditional variable associated. */
    uint16_t *data;                 /* Raw IQ samples buffer */
    uint16_t *magnitude;            /* Magnitude vector */
    struct timeb stSystemTimeRTL;   /* System time when RTL passed us the Latest block */
    uint64_t timestampBlk;          /* Timestamp of the start of the current block */
    struct timeb stSystemTimeBlk;   /* System time when RTL passed us currently processing this block */
    int fd;                         /* --ifile option file descriptor. */
    int data_ready;                 /* Data ready to be processed. */
    uint32_t *icao_cache;           /* Recently seen ICAO addresses cache. */
    uint16_t *maglut;               /* I/Q -> Magnitude lookup table. */
    int exit;                       /* Exit from the main loop when true. */

    /* RTLSDR */
    int dev_index;
    int gain;
    int enable_agc;
    rtlsdr_dev_t *dev;
    int freq;
    int ppm_error;

    /* Networking */
    char aneterr[ANET_ERR_LEN];
    struct client *clients[MODES_NET_MAX_FD]; /* Our clients. */
    int maxfd;                      /* Greatest fd currently active. */
    int sbsos;                      /* SBS output listening socket. */
    int ros;                        /* Raw output listening socket. */
    int ris;                        /* Raw input listening socket. */
    int https;                      /* HTTP listening socket. */
    char * rawOut;                  /* Buffer for building raw output data */
    int rawOutUsed;                 /* How much if the buffer is currently used */

    /* Configuration */
    char *filename;                 /* Input form file, --ifile option. */
    int fix_errors;                 /* Single bit error correction if true. */
    int check_crc;                  /* Only display messages with good CRC. */
    int raw;                        /* Raw output format. */
    int beast;                      /* Beast binary format output. */
    int mode_ac;                    /* Enable decoding of SSR Modes A & C. */
    int debug;                      /* Debugging mode. */
    int net;                        /* Enable networking. */
    int net_only;                   /* Enable just networking. */
    int net_output_sbs_port;        /* SBS output TCP port. */
    int net_output_raw_size;        /* Minimum Size of the output raw data */     
    int net_output_raw_rate;        /* Rate (in 64mS increments) of output raw data */     
    int net_output_raw_rate_count;  /* Rate (in 64mS increments) of output raw data */     
    int net_output_raw_port;        /* Raw output TCP port. */
    int net_input_raw_port;         /* Raw input TCP port. */
    int net_http_port;              /* HTTP port. */
    int quiet;                      /* Suppress stdout */
    int interactive;                /* Interactive mode */
    int interactive_rows;           /* Interactive mode: max number of rows. */
    int interactive_ttl;            /* Interactive mode: TTL before deletion. */
    int stats;                      /* Print stats at exit in --ifile mode. */
    int onlyaddr;                   /* Print only ICAO addresses. */
    int metric;                     /* Use metric units. */
    int aggressive;                 /* Aggressive detection algorithm. */
    int mlat;                       /* Use Beast ascii format for raw data output, i.e. @...; iso *...; */
    int interactive_rtl1090;        /* flight table in interactive mode is formatted like RTL1090 */

    /* Interactive mode */
    struct aircraft *aircrafts;
    uint64_t interactive_last_update;  /* Last screen update in milliseconds */

    /* Statistics */
    unsigned int stat_valid_preamble;
    unsigned int stat_demodulated;
    unsigned int stat_goodcrc;
    unsigned int stat_badcrc;
    unsigned int stat_fixed;
    unsigned int stat_single_bit_fix;
    unsigned int stat_two_bits_fix;
    unsigned int stat_http_requests;
    unsigned int stat_sbs_connections;
    unsigned int stat_out_of_phase;
    unsigned int stat_DF_Corrected;
    unsigned int stat_ModeAC;
} Modes;

/* The struct we use to store information about a decoded message. */
struct modesMessage {
    /* Generic fields */
    unsigned char msg[MODES_LONG_MSG_BYTES]; /* Binary message. */
    int msgbits;                /* Number of bits in message */
    int msgtype;                /* Downlink format # */
    int crcok;                  /* True if CRC was valid */
    uint32_t crc;               /* Message CRC */
    int errorbit;               /* Bit corrected. -1 if no bit corrected. */
    uint32_t addr;              /* ICAO Address from bytes 1 2 and 3 */
    int phase_corrected;        /* True if phase correction was applied. */
    uint64_t timestampMsg;      /* Timestamp of the message. */  
    unsigned char signalLevel;  /* Signal Amplitude */

    /* DF 11 */
    int ca;                     /* Responder capabilities. */
    int iid;

    /* DF 17 */
    int metype;                 /* Extended squitter message type. */
    int mesub;                  /* Extended squitter message subtype. */
    int heading_is_valid;
    int heading;
    int aircraft_type;
    int fflag;                  /* 1 = Odd, 0 = Even CPR message. */
    int tflag;                  /* UTC synchronized? */
    int raw_latitude;           /* Non decoded latitude */
    int raw_longitude;          /* Non decoded longitude */
    char flight[9];             /* 8 chars flight number. */
    int ew_dir;                 /* 0 = East, 1 = West. */
    int ew_velocity;            /* E/W velocity. */
    int ns_dir;                 /* 0 = North, 1 = South. */
    int ns_velocity;            /* N/S velocity. */
    int vert_rate_source;       /* Vertical rate source. */
    int vert_rate_sign;         /* Vertical rate sign. */
    int vert_rate;              /* Vertical rate. */
    int velocity;               /* Computed from EW and NS velocity. */

    /* DF4, DF5, DF20, DF21 */
    int fs;                     /* Flight status for DF4,5,20,21 */
    int dr;                     /* Request extraction of downlink request. */
    int um;                     /* Request extraction of downlink request. */
    int modeA;                  /* 13 bits identity (Squawk). */

    /* Fields used by multiple message types. */
    int altitude, unit; 
};

void interactiveShowData(void);
struct aircraft* interactiveReceiveData(struct modesMessage *mm);
void modesSendAllClients(int service, void *msg, int len);
void modesSendRawOutput(struct modesMessage *mm);
void modesSendBeastOutput(struct modesMessage *mm);
void modesSendSBSOutput(struct modesMessage *mm, struct aircraft *a);
void useModesMessage(struct modesMessage *mm);
int fixSingleBitErrors(unsigned char *msg, int bits, struct modesMessage *mm);
int fixTwoBitsErrors(unsigned char *msg, int bits, struct modesMessage *mm);
int modesMessageLenByType(int type);

/* ============================= Utility functions ========================== */

static uint64_t mstime(void) {
    struct timeval tv;
    uint64_t mst;

    gettimeofday(&tv, NULL);
    mst = ((uint64_t)tv.tv_sec)*1000;
    mst += tv.tv_usec/1000;
    return mst;
}

/* =============================== Initialization =========================== */

void modesInitConfig(void) {
    Modes.gain = MODES_MAX_GAIN;
    Modes.dev_index = 0;
    Modes.enable_agc = 0;
    Modes.ppm_error = 0;
    Modes.freq = MODES_DEFAULT_FREQ;
    Modes.filename = NULL;
    Modes.fix_errors = 0;
    Modes.check_crc = 1;
    Modes.raw = 0;
    Modes.beast = 0;
    Modes.mode_ac = 0;
    Modes.net = 0;
    Modes.net_only = 0;
    Modes.net_output_sbs_port = MODES_NET_OUTPUT_SBS_PORT;
    Modes.net_output_raw_size = 0;
    Modes.net_output_raw_rate = 0;
    Modes.net_output_raw_port = MODES_NET_OUTPUT_RAW_PORT;
    Modes.net_input_raw_port = MODES_NET_INPUT_RAW_PORT;
    Modes.net_http_port = MODES_NET_HTTP_PORT;
    Modes.onlyaddr = 0;
    Modes.debug = 0;
    Modes.interactive = 0;
    Modes.interactive_rows = MODES_INTERACTIVE_ROWS;
    Modes.interactive_ttl = MODES_INTERACTIVE_TTL;
    Modes.quiet = 0;
    Modes.aggressive = 0;
    Modes.mlat = 0;
    Modes.interactive_rtl1090 = 0;
}

void modesInit(void) {
    int i, q;

    pthread_mutex_init(&Modes.data_mutex,NULL);
    pthread_cond_init(&Modes.data_cond,NULL);

    // Allocate the various buffers used by Modes
    if ( ((Modes.icao_cache = (uint32_t *) malloc(sizeof(uint32_t) * MODES_ICAO_CACHE_LEN * 2)                  ) == NULL) ||
         ((Modes.data       = (uint16_t *) malloc(MODES_ASYNC_BUF_SIZE)                                         ) == NULL) ||
         ((Modes.magnitude  = (uint16_t *) malloc(MODES_ASYNC_BUF_SIZE+MODES_PREAMBLE_SIZE+MODES_LONG_MSG_SIZE) ) == NULL) ||
         ((Modes.maglut     = (uint16_t *) malloc(sizeof(uint16_t) * 256 * 256)                                 ) == NULL) ||
         ((Modes.rawOut     = (char     *) malloc(MODES_RAWOUT_BUF_SIZE)                                        ) == NULL) ) 
    {
        fprintf(stderr, "Out of memory allocating data buffer.\n");
        exit(1);
    }

    // Limit the maximum requested raw output size to less than one Ethernet Block 
    Modes.net_output_raw_rate_count = 0;
    if (Modes.net_output_raw_size > (MODES_RAWOUT_BUF_FLUSH))
      {Modes.net_output_raw_size = MODES_RAWOUT_BUF_FLUSH;}
    if (Modes.net_output_raw_rate > (MODES_RAWOUT_BUF_RATE))
      {Modes.net_output_raw_rate = MODES_RAWOUT_BUF_RATE;}

    // Clear the buffers that have just been allocated, just in-case
    memset(Modes.icao_cache, 0,   sizeof(uint32_t) * MODES_ICAO_CACHE_LEN * 2);
    memset(Modes.data,       127, MODES_ASYNC_BUF_SIZE);
    memset(Modes.magnitude,  0,   MODES_ASYNC_BUF_SIZE+MODES_PREAMBLE_SIZE+MODES_LONG_MSG_SIZE);

    // The ICAO address cache. We use two uint32_t for every
    // entry because it's a addr / timestamp pair for every entry.
    Modes.timestampBlk            = 0;
    Modes.data_ready              = 0;
    Modes.aircrafts               = NULL;
    Modes.interactive_last_update = 0;
    Modes.rawOutUsed              = 0;
    ftime(&Modes.stSystemTimeRTL);
    Modes.stSystemTimeBlk         = Modes.stSystemTimeRTL;

    /* Populate the I/Q -> Magnitude lookup table. It is used because
     * sqrt or round may be expensive and may vary a lot depending on
     * the libc used.
     *
     * We scale to 0-255 range multiplying by 1.4 in order to ensure that
     * every different I/Q pair will result in a different magnitude value,
     * not losing any resolution. */
/*
    for (i = 0; i <= 255; i++) {
        for (q = 0; q <= 255; q++) {
            int mag_i = i - 127;
            int mag_q = q - 127;
            int mag   = 0;           
            mag = (int) round(sqrt((mag_i*mag_i)+(mag_q*mag_q)) * 360);
            Modes.maglut[(i*256)+q] = (uint16_t) min(mag,65535);
        }
    }
*/
    // Each I and Q value varies from 0 to 255, which represents a range from -1 to +1. To get from the 
    // unsigned (0-255) range you therefore subtract 127 (or 128 or 127.5) from each I and Q, giving you 
    // a range from -127 to +128 (or -128 to +127, or -127.5 to +127.5)..
    //
    // To decode the AM signal, you need the magnitude of the waveform, which is given by sqrt((I^2)+(Q^2))
    // The most this could be is if I&Q are both 128 (or 127 or 127.5), so you could end up with a magnitude 
    // of 181.019 (or 179.605, or 180.312)
    //
    // However, in reality the magnitude of the signal should never exceed the range -1 to +1, because the 
    // values are I = rCos(w) and Q = rSin(w). Therefore the integer computed magnitude should (can?) never 
    // exceed 128 (or 127, or 127.5 or whatever)
    //
    // If we scale up the results so that they range from 0 to 65535 (16 bits) then we need to multiply 
    // by 511.99, (or 516.02 or 514). antirez's original code multiplies by 360, presumably because he's 
    // assuming the maximim calculated amplitude is 181.019, and (181.019 * 360) = 65166.
    //
    // So lets see if we can improve things by subtracting 127.5, Well in integer arithmatic we can't
    // subtract half, so, we'll double everything up and subtract one, and then compensate for the doubling 
    // in the multiplier at the end.
    //
    // If we do this we can never have I or Q equal to 0 - they can only be as small as +/- 1.
    // This gives us a minimum magnitude of root 2 (0.707), so the dynamic range becomes (1.414-255). This 
    // also affects our scaling value, which is now 65535/(255 - 1.414), or 258.433254
    //
    // The sums then become mag = 258.433254 * (sqrt((I*2-255)^2 + (Q*2-255)^2) - 1.414)
    //                   or mag = (258.433254 * sqrt((I*2-255)^2 + (Q*2-255)^2)) - 365.4798
    //
    // We also need to clip mag just incaes any rogue I/Q values somehow do have a magnitude greater than 255.
    //

    for (i = 0; i <= 255; i++) {
        for (q = 0; q <= 255; q++) {
            int mag, mag_i, mag_q;

            mag_i = (i * 2) - 255;
            mag_q = (q * 2) - 255;

            mag = (int) round((sqrt((mag_i*mag_i)+(mag_q*mag_q)) * 258.433254) - 365.4798);

            Modes.maglut[(i*256)+q] = (uint16_t) ((mag < 65535) ? mag : 65535);
        }
    }

    /* Statistics */
    Modes.stat_valid_preamble = 0;
    Modes.stat_demodulated = 0;
    Modes.stat_goodcrc = 0;
    Modes.stat_badcrc = 0;
    Modes.stat_fixed = 0;
    Modes.stat_single_bit_fix = 0;
    Modes.stat_two_bits_fix = 0;
    Modes.stat_http_requests = 0;
    Modes.stat_sbs_connections = 0;
    Modes.stat_out_of_phase = 0;
    Modes.stat_DF_Corrected = 0;
    Modes.stat_ModeAC = 0;
    Modes.exit = 0;
}

/* =============================== RTLSDR handling ========================== */

void modesInitRTLSDR(void) {
    int j;
    int device_count;
    char vendor[256], product[256], serial[256];

    device_count = rtlsdr_get_device_count();
    if (!device_count) {
        fprintf(stderr, "No supported RTLSDR devices found.\n");
        exit(1);
    }

    fprintf(stderr, "Found %d device(s):\n", device_count);
    for (j = 0; j < device_count; j++) {
        rtlsdr_get_device_usb_strings(j, vendor, product, serial);
        fprintf(stderr, "%d: %s, %s, SN: %s %s\n", j, vendor, product, serial,
            (j == Modes.dev_index) ? "(currently selected)" : "");
    }

    if (rtlsdr_open(&Modes.dev, Modes.dev_index) < 0) {
        fprintf(stderr, "Error opening the RTLSDR device: %s\n",
            strerror(errno));
        exit(1);
    }

    /* Set gain, frequency, sample rate, and reset the device. */
    rtlsdr_set_tuner_gain_mode(Modes.dev,
        (Modes.gain == MODES_AUTO_GAIN) ? 0 : 1);
    if (Modes.gain != MODES_AUTO_GAIN) {
        if (Modes.gain == MODES_MAX_GAIN) {
            /* Find the maximum gain available. */
            int numgains;
            int gains[100];

            numgains = rtlsdr_get_tuner_gains(Modes.dev, gains);
            Modes.gain = gains[numgains-1];
            fprintf(stderr, "Max available gain is: %.2f\n", Modes.gain/10.0);
        }
        rtlsdr_set_tuner_gain(Modes.dev, Modes.gain);
        fprintf(stderr, "Setting gain to: %.2f\n", Modes.gain/10.0);
    } else {
        fprintf(stderr, "Using automatic gain control.\n");
    }
    rtlsdr_set_freq_correction(Modes.dev, Modes.ppm_error);
    if (Modes.enable_agc) rtlsdr_set_agc_mode(Modes.dev, 1);
    rtlsdr_set_center_freq(Modes.dev, Modes.freq);
    rtlsdr_set_sample_rate(Modes.dev, MODES_DEFAULT_RATE);
    rtlsdr_reset_buffer(Modes.dev);
    fprintf(stderr, "Gain reported by device: %.2f\n",
        rtlsdr_get_tuner_gain(Modes.dev)/10.0);
}

/* We use a thread reading data in background, while the main thread
 * handles decoding and visualization of data to the user.
 *
 * The reading thread calls the RTLSDR API to read data asynchronously, and
 * uses a callback to populate the data buffer.
 * A Mutex is used to avoid races with the decoding thread. */
void rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx) {
    MODES_NOTUSED(ctx);

    pthread_mutex_lock(&Modes.data_mutex);
    ftime(&Modes.stSystemTimeRTL);
    if (len > MODES_ASYNC_BUF_SIZE) len = MODES_ASYNC_BUF_SIZE;
    /* Read the new data. */
    memcpy(Modes.data, buf, len);
    Modes.data_ready = 1;
    /* Signal to the other thread that new data is ready */
    pthread_cond_signal(&Modes.data_cond);
    pthread_mutex_unlock(&Modes.data_mutex);
}

/* This is used when --ifile is specified in order to read data from file
 * instead of using an RTLSDR device. */
void readDataFromFile(void) {
    pthread_mutex_lock(&Modes.data_mutex);
    while(1) {
        ssize_t nread, toread;
        unsigned char *p;

        if (Modes.data_ready) {
            pthread_cond_wait(&Modes.data_cond,&Modes.data_mutex);
            continue;
        }

        if (Modes.interactive) {
            /* When --ifile and --interactive are used together, slow down
             * playing at the natural rate of the RTLSDR received. */
            pthread_mutex_unlock(&Modes.data_mutex);
            usleep(64000);
            pthread_mutex_lock(&Modes.data_mutex);
        }

        toread = MODES_ASYNC_BUF_SIZE;
        p = (unsigned char *) Modes.data;
        while(toread) {
            nread = read(Modes.fd, p, toread);
            if (nread <= 0) {
                Modes.exit = 1; /* Signal the other thread to exit. */
                break;
            }
            p += nread;
            toread -= nread;
        }
        if (toread) {
            /* Not enough data on file to fill the buffer? Pad with
             * no signal. */
            memset(p,127,toread);
        }
        Modes.data_ready = 1;
        /* Signal to the other thread that new data is ready */
        pthread_cond_signal(&Modes.data_cond);
    }
}

/* We read data using a thread, so the main thread only handles decoding
 * without caring about data acquisition. */
void *readerThreadEntryPoint(void *arg) {
    MODES_NOTUSED(arg);

    if (Modes.filename == NULL) {
        rtlsdr_read_async(Modes.dev, rtlsdrCallback, NULL,
                              MODES_ASYNC_BUF_NUMBER,
                              MODES_ASYNC_BUF_SIZE);
    } else {
        readDataFromFile();
    }
    return NULL;
}

/* ============================== Debugging ================================= */

/* Helper function for dumpMagnitudeVector().
 * It prints a single bar used to display raw signals.
 *
 * Since every magnitude sample is between 0-255, the function uses
 * up to 63 characters for every bar. Every character represents
 * a length of 4, 3, 2, 1, specifically:
 *
 * "O" is 4
 * "o" is 3
 * "-" is 2
 * "." is 1
 */
void dumpMagnitudeBar(int index, int magnitude) {
    char *set = " .-o";
    char buf[256];
    int div = magnitude / 256 / 4;
    int rem = magnitude / 256 % 4;

    memset(buf,'O',div);
    buf[div] = set[rem];
    buf[div+1] = '\0';

    if (index >= 0)
        printf("[%.3d] |%-66s %d\n", index, buf, magnitude);
    else
        printf("[%.2d] |%-66s %d\n", index, buf, magnitude);
}

/* Display an ASCII-art alike graphical representation of the undecoded
 * message as a magnitude signal.
 *
 * The message starts at the specified offset in the "m" buffer.
 * The function will display enough data to cover a short 56 bit message.
 *
 * If possible a few samples before the start of the messsage are included
 * for context. */

void dumpMagnitudeVector(uint16_t *m, uint32_t offset) {
    uint32_t padding = 5; /* Show a few samples before the actual start. */
    uint32_t start = (offset < padding) ? 0 : offset-padding;
    uint32_t end = offset + (MODES_PREAMBLE_SAMPLES)+(MODES_SHORT_MSG_SAMPLES) - 1;
    uint32_t j;

    for (j = start; j <= end; j++) {
        dumpMagnitudeBar(j-offset, m[j]);
    }
}

/* Produce a raw representation of the message as a Javascript file
 * loadable by debug.html. */
void dumpRawMessageJS(char *descr, unsigned char *msg,
                      uint16_t *m, uint32_t offset, int fixable)
{
    int padding = 5; /* Show a few samples before the actual start. */
    int start = offset - padding;
    int end = offset + (MODES_PREAMBLE_SAMPLES)+(MODES_LONG_MSG_SAMPLES) - 1;
    FILE *fp;
    int j, fix1 = -1, fix2 = -1;

    if (fixable != -1) {
        fix1 = fixable & 0xff;
        if (fixable > 255) fix2 = fixable >> 8;
    }

    if ((fp = fopen("frames.js","a")) == NULL) {
        fprintf(stderr, "Error opening frames.js: %s\n", strerror(errno));
        exit(1);
    }

    fprintf(fp,"frames.push({\"descr\": \"%s\", \"mag\": [", descr);
    for (j = start; j <= end; j++) {
        fprintf(fp,"%d", j < 0 ? 0 : m[j]);
        if (j != end) fprintf(fp,",");
    }
    fprintf(fp,"], \"fix1\": %d, \"fix2\": %d, \"bits\": %d, \"hex\": \"",
        fix1, fix2, modesMessageLenByType(msg[0]>>3));
    for (j = 0; j < MODES_LONG_MSG_BYTES; j++)
        fprintf(fp,"\\x%02x",msg[j]);
    fprintf(fp,"\"});\n");
    fclose(fp);
}

/* This is a wrapper for dumpMagnitudeVector() that also show the message
 * in hex format with an additional description.
 *
 * descr  is the additional message to show to describe the dump.
 * msg    points to the decoded message
 * m      is the original magnitude vector
 * offset is the offset where the message starts
 *
 * The function also produces the Javascript file used by debug.html to
 * display packets in a graphical format if the Javascript output was
 * enabled.
 */
void dumpRawMessage(char *descr, unsigned char *msg,
                    uint16_t *m, uint32_t offset)
{
    int j;
    int msgtype = msg[0]>>3;
    int fixable = -1;

    if (msgtype == 11 || msgtype == 17) {
        int msgbits = (msgtype == 11) ? MODES_SHORT_MSG_BITS :
                                        MODES_LONG_MSG_BITS;
        fixable = fixSingleBitErrors(msg, msgbits, NULL);
        if (fixable == -1)
            fixable = fixTwoBitsErrors(msg, msgbits, NULL);
    }

    if (Modes.debug & MODES_DEBUG_JS) {
        dumpRawMessageJS(descr, msg, m, offset, fixable);
        return;
    }

    printf("\n--- %s\n    ", descr);
    for (j = 0; j < MODES_LONG_MSG_BYTES; j++) {
        printf("%02x",msg[j]);
        if (j == MODES_SHORT_MSG_BYTES-1) printf(" ... ");
    }
    printf(" (DF %d, Fixable: %d)\n", msgtype, fixable);
    dumpMagnitudeVector(m,offset);
    printf("---\n\n");
}

/* ===================== Mode A/C detection and decoding  =================== */

//
// This table is used to build the Mode A/C variable called ModeABits.Each 
// bit period is inspected, and if it's value exceeds the threshold limit, 
// then the value in this table is or-ed into ModeABits.
//
// At the end of message processing, ModeABits will be the decoded ModeA value.
//
// We can also flag noise in bits that should be zeros - the xx bits. Noise in
// these bits cause bits (31-16) in ModeABits to be set. Then at the end of message
// processing we can test for errors by looking at these bits.
//
uint32_t ModeABitTable[24] = {
0x00000000, // F1 = 1
0x00000010, // C1
0x00001000, // A1
0x00000020, // C2 
0x00002000, // A2
0x00000040, // C4
0x00004000, // A4
0x40000000, // xx = 0  Set bit 30 if we see this high
0x00000100, // B1 
0x00000001, // D1
0x00000200, // B2
0x00000002, // D2
0x00000400, // B4
0x00000004, // D4
0x00000000, // F2 = 1
0x08000000, // xx = 0  Set bit 27 if we see this high
0x04000000, // xx = 0  Set bit 26 if we see this high
0x00000080, // SPI
0x02000000, // xx = 0  Set bit 25 if we see this high
0x01000000, // xx = 0  Set bit 24 if we see this high
0x00800000, // xx = 0  Set bit 23 if we see this high
0x00400000, // xx = 0  Set bit 22 if we see this high
0x00200000, // xx = 0  Set bit 21 if we see this high
0x00100000, // xx = 0  Set bit 20 if we see this high
};
//
// This table is used to produce an error variable called ModeAErrs.Each 
// inter-bit period is inspected, and if it's value falls outside of the 
// expected range, then the value in this table is or-ed into ModeAErrs.
//
// At the end of message processing, ModeAErrs will indicate if we saw 
// any inter-bit anomolies, and the bits that are set will show which 
// bits had them.
//
uint32_t ModeAMidTable[24] = {
0x80000000, // F1 = 1  Set bit 31 if we see F1_C1  error
0x00000010, // C1      Set bit  4 if we see C1_A1  error
0x00001000, // A1      Set bit 12 if we see A1_C2  error
0x00000020, // C2      Set bit  5 if we see C2_A2  error
0x00002000, // A2      Set bit 13 if we see A2_C4  error
0x00000040, // C4      Set bit  6 if we see C3_A4  error
0x00004000, // A4      Set bit 14 if we see A4_xx  error
0x40000000, // xx = 0  Set bit 30 if we see xx_B1  error
0x00000100, // B1      Set bit  8 if we see B1_D1  error
0x00000001, // D1      Set bit  0 if we see D1_B2  error
0x00000200, // B2      Set bit  9 if we see B2_D2  error
0x00000002, // D2      Set bit  1 if we see D2_B4  error
0x00000400, // B4      Set bit 10 if we see B4_D4  error
0x00000004, // D4      Set bit  2 if we see D4_F2  error
0x20000000, // F2 = 1  Set bit 29 if we see F2_xx  error
0x08000000, // xx = 0  Set bit 27 if we see xx_xx  error
0x04000000, // xx = 0  Set bit 26 if we see xx_SPI error
0x00000080, // SPI     Set bit 15 if we see SPI_xx error
0x02000000, // xx = 0  Set bit 25 if we see xx_xx  error
0x01000000, // xx = 0  Set bit 24 if we see xx_xx  error
0x00800000, // xx = 0  Set bit 23 if we see xx_xx  error
0x00400000, // xx = 0  Set bit 22 if we see xx_xx  error
0x00200000, // xx = 0  Set bit 21 if we see xx_xx  error
0x00100000, // xx = 0  Set bit 20 if we see xx_xx  error
};
//
// The "off air" format is,,
// _F1_C1_A1_C2_A2_C4_A4_xx_B1_D1_B2_D2_B4_D4_F2_xx_xx_SPI_
//
// Bit spacing is 1.45uS, with 0.45uS high, and 1.00us low. This is a problem
// because we ase sampling at 2Mhz (500nS) so we are below Nyquist. 
//
// The bit spacings are..
// F1 :  0.00,   
//       1.45,  2.90,  4.35,  5.80,  7.25,  8.70, 
// X  : 10.15, 
//    : 11.60, 13.05, 14.50, 15.95, 17.40, 18.85, 
// F2 : 20.30, 
// X  : 21.75, 23.20, 24.65 
//
// This equates to the following sample point centers at 2Mhz.
// [ 0.0], 
// [ 2.9], [ 5.8], [ 8.7], [11.6], [14.5], [17.4], 
// [20.3], 
// [23.2], [26.1], [29.0], [31.9], [34.8], [37.7]
// [40.6]
// [43.5], [46.4], [49.3]
//
// We know that this is a supposed to be a binary stream, so the signal
// should either be a 1 or a 0. Therefore, any energy above the noise level 
// in two adjacent samples must be from the same pulse, so we can simply 
// add the values together.. 
// 
int detectModeA(uint16_t *m, struct modesMessage *mm)
  {
  int j, lastBitWasOne;
  int ModeABits = 0;
  int ModeAErrs = 0;
  int byte, bit;
  int thisSample, lastBit, lastSpace = 0; 
  int m0, m1, m2, m3, mPhase;
  int n0, n1, n2 ,n3;
  int F1_sig, F1_noise;
  int F2_sig, F2_noise;
  int fSig, fNoise, fLevel, fLoLo;

  // m[0] contains the energy from    0 ->  499 nS
  // m[1] contains the energy from  500 ->  999 nS
  // m[2] contains the energy from 1000 -> 1499 nS
  // m[3] contains the energy from 1500 -> 1999 nS
  //
  // We are looking for a Frame bit (F1) whose width is 450nS, followed by
  // 1000nS of quiet.
  //
  // The width of the frame bit is 450nS, which is 90% of our sample rate.
  // Therefore, in an ideal world, all the energy for the frame bit will be
  // in a single sample, preceeded by (at least) one zero, and followed by 
  // two zeros, Best case we can look for ...
  //
  // 0 - 1 - 0 - 0
  //
  // However, our samples are not phase aligned, so some of the energy from 
  // each bit could be spread over two consecutive samples. Worst case is
  // that we sample half in one bit, and half in the next. In that case, 
  // we're looking for 
  //
  // 0 - 0.5 - 0.5 - 0.

  m0 = m[0]; m1 = m[1];

  if (m0 >= m1)   // m1 *must* be bigger than m0 for this to be F1
    {return (0);}

  m2 = m[2]; m3 = m[3];

  // 
  // if (m2 <= m0), then assume the sample bob on (Phase == 0), so don't look at m3 
  if ((m2 <= m0) || (m2 < m3))
    {m3 = m2; m2 = m0;}

  if (  (m3 >= m1)   // m1 must be bigger than m3
     || (m0 >  m2)   // m2 can be equal to m0 if ( 0,1,0,0 )
     || (m3 >  m2) ) // m2 can be equal to m3 if ( 0,1,0,0 )
    {return (0);}

  // m0 = noise
  // m1 = noise + (signal *    X))
  // m2 = noise + (signal * (1-X))
  // m3 = noise
  //
  // Hence, assuming all 4 samples have similar amounts of noise in them 
  //      signal = (m1 + m2) - ((m0 + m3) * 2)
  //      noise  = (m0 + m3) / 2
  //
  F1_sig   = (m1 + m2) - ((m0 + m3) << 1);
  F1_noise = (m0 + m3) >> 1;

  if ( (F1_sig < MODEAC_MSG_SQUELCH_LEVEL) // minimum required  F1 signal amplitude
    || (F1_sig < (F1_noise << 2)) )        // minimum allowable Sig/Noise ratio 4:1
    {return (0);}

  // If we get here then we have a potential F1, so look for an equally valid F2 20.3uS later
  //
  // Our F1 is centered somewhere between samples m[1] and m[2]. We can guestimate where F2 is 
  // by comparing the ratio of m1 and m2, and adding on 20.3 uS (40.6 samples)
  //
  mPhase = ((m2 * 20) / (m1 + m2));
  byte   = (mPhase + 812) / 20; 
  n0     = m[byte++]; n1 = m[byte++]; 

  if (n0 >= n1)   // n1 *must* be bigger than n0 for this to be F2
    {return (0);}

  n2 = m[byte++];
  // 
  // if the sample bob on (Phase == 0), don't look at n3 
  //
  if ((mPhase + 812) % 20)
    {n3 = m[byte++];}
  else
    {n3 = n2; n2 = n0;}

  if (  (n3 >= n1)   // n1 must be bigger than n3
     || (n0 >  n2)   // n2 can be equal to n0 ( 0,1,0,0 )
     || (n3 >  n2) ) // n2 can be equal to n3 ( 0,1,0,0 )
    {return (0);}

  F2_sig   = (n1 + n2) - ((n0 + n3) << 1);
  F2_noise = (n0 + n3) >> 1;

  if ( (F2_sig < MODEAC_MSG_SQUELCH_LEVEL) // minimum required  F2 signal amplitude
    || (F2_sig < (F2_noise << 2)) )       // maximum allowable Sig/Noise ratio 4:1
    {return (0);}

  fSig          = (F1_sig   + F2_sig)   >> 1;
  fNoise        = (F1_noise + F2_noise) >> 1;
  fLoLo         = fNoise    + (fSig >> 2);       // 1/2
  fLevel        = fNoise    + (fSig >> 1);
  lastBitWasOne = 1;
  lastBit       = F1_sig;
  //
  // Now step by a half ModeA bit, 0.725nS, which is 1.45 samples, which is 29/20
  // No need to do bit 0 because we've already selected it as a valid F1
  // Do several bits past the SPI to increase error rejection
  //
  for (j = 1, mPhase += 29; j < 48; mPhase += 29, j ++)
    {
    byte  = 1 + (mPhase / 20);
    
    thisSample = m[byte] - fNoise;
    if (mPhase % 20)                     // If the bit is split over two samples...
      {thisSample += (m[byte+1] - fNoise);}  //    add in the second sample's energy

     // If we're calculating a space value
    if (j & 1)               
      {lastSpace = thisSample;}

    else 
      {// We're calculating a new bit value
      bit = j >> 1;
      if (thisSample >= fLevel)
        {// We're calculating a new bit value, and its a one
        ModeABits |= ModeABitTable[bit--];  // or in the correct bit

        if (lastBitWasOne)
          { // This bit is one, last bit was one, so check the last space is somewhere less than one
          if ( (lastSpace >= (thisSample>>1)) || (lastSpace >= lastBit) )
            {ModeAErrs |= ModeAMidTable[bit];}
          }

        else              
          {// This bit,is one, last bit was zero, so check the last space is somewhere less than one
          if (lastSpace >= (thisSample >> 1))
            {ModeAErrs |= ModeAMidTable[bit];}
          }

        lastBitWasOne = 1;
        }

      
      else 
        {// We're calculating a new bit value, and its a zero
        if (lastBitWasOne)
          { // This bit is zero, last bit was one, so check the last space is somewhere in between
          if (lastSpace >= lastBit)
            {ModeAErrs |= ModeAMidTable[bit];}
          }

        else              
          {// This bit,is zero, last bit was zero, so check the last space is zero too
          if (lastSpace >= fLoLo)
            {ModeAErrs |= ModeAMidTable[bit];}
          }

        lastBitWasOne = 0;   
        }

      lastBit = (thisSample >> 1); 
      }
    }

  //
  // Output format is : 00:A4:A2:A1:00:B4:B2:B1:00:C4:C2:C1:00:D4:D2:D1
  //
  if ((ModeABits < 3) || (ModeABits & 0xFFFF8808) || (ModeAErrs) )
    {return (ModeABits = 0);}

  fSig            = (fSig + 0x7F) >> 8;
  mm->signalLevel = ((fSig < 255) ? fSig : 255);

  return ModeABits;
  }

// Input format is : 00:A4:A2:A1:00:B4:B2:B1:00:C4:C2:C1:00:D4:D2:D1
int ModeAToModeC(unsigned int ModeA) 
  { 
  unsigned int FiveHundreds = 0;
  unsigned int OneHundreds  = 0;

  if (  (ModeA & 0xFFFF888B)         // D1 set is illegal. D2 set is > 62700ft which is unlikely
    || ((ModeA & 0x000000F0) == 0) ) // C1,,C4 cannot be Zero
    {return -9999;}

  if (ModeA & 0x0010) {OneHundreds ^= 0x007;} // C1
  if (ModeA & 0x0020) {OneHundreds ^= 0x003;} // C2
  if (ModeA & 0x0040) {OneHundreds ^= 0x001;} // C4

  // Remove 7s from OneHundreds (Make 7->5, snd 5->7). 
  if ((OneHundreds & 5) == 5) {OneHundreds ^= 2;}

  // Check for invalid codes, only 1 to 5 are valid 
  if (OneHundreds > 5)
    {return -9999;} 

//if (ModeA & 0x0001) {FiveHundreds ^= 0x1FF;} // D1 never used for altitude
  if (ModeA & 0x0002) {FiveHundreds ^= 0x0FF;} // D2
  if (ModeA & 0x0004) {FiveHundreds ^= 0x07F;} // D4

  if (ModeA & 0x1000) {FiveHundreds ^= 0x03F;} // A1
  if (ModeA & 0x2000) {FiveHundreds ^= 0x01F;} // A2
  if (ModeA & 0x4000) {FiveHundreds ^= 0x00F;} // A4

  if (ModeA & 0x0100) {FiveHundreds ^= 0x007;} // B1 
  if (ModeA & 0x0200) {FiveHundreds ^= 0x003;} // B2
  if (ModeA & 0x0400) {FiveHundreds ^= 0x001;} // B4
    
  // Correct order of OneHundreds. 
  if (FiveHundreds & 1) {OneHundreds = 6 - OneHundreds;} 

  return ((FiveHundreds * 5) + OneHundreds - 13); 
  } 

void decodeModeAMessage(struct modesMessage *mm, int ModeA)
  {
  mm->msgtype = 32; // Valid Mode S DF's are DF-00 to DF-31.
                    // so use 32 to indicate Mode A/C

  mm->msgbits = 16; // Fudge up a Mode S style data stream
  mm->msg[0] = (ModeA >> 8);
  mm->msg[1] = (ModeA);

  // Fudge an ICAO address based on Mode A (remove the Ident bit)
  // Use an upper address byte of FF, since this is ICAO unallocated
  mm->addr = 0x00FF0000 | (ModeA & 0x0000FF7F);

  // Set the Identity field to ModeA
  mm->modeA =  ModeA & 0x7777;

  // Flag ident in flight status
  mm->fs = ModeA & 0x0080;

  // Not much else we can tell from a Mode A/C reply.
  // Just fudge up a few bits to keep other code happy
  mm->crcok = 1;
  mm->errorbit = -1;
  }

/* ===================== Mode S detection and decoding  ===================== */

/* Parity table for MODE S Messages.
 * The table contains 112 elements, every element corresponds to a bit set
 * in the message, starting from the first bit of actual data after the
 * preamble.
 *
 * For messages of 112 bit, the whole table is used.
 * For messages of 56 bits only the last 56 elements are used.
 *
 * The algorithm is as simple as xoring all the elements in this table
 * for which the corresponding bit on the message is set to 1.
 *
 * The latest 24 elements in this table are set to 0 as the checksum at the
 * end of the message should not affect the computation.
 *
 * Note: this function can be used with DF11 and DF17, other modes have
 * the CRC xored with the sender address as they are reply to interrogations,
 * but a casual listener can't split the address from the checksum.
 */
uint32_t modes_checksum_table[112] = {
0x3935ea, 0x1c9af5, 0xf1b77e, 0x78dbbf, 0xc397db, 0x9e31e9, 0xb0e2f0, 0x587178,
0x2c38bc, 0x161c5e, 0x0b0e2f, 0xfa7d13, 0x82c48d, 0xbe9842, 0x5f4c21, 0xd05c14,
0x682e0a, 0x341705, 0xe5f186, 0x72f8c3, 0xc68665, 0x9cb936, 0x4e5c9b, 0xd8d449,
0x939020, 0x49c810, 0x24e408, 0x127204, 0x093902, 0x049c81, 0xfdb444, 0x7eda22,
0x3f6d11, 0xe04c8c, 0x702646, 0x381323, 0xe3f395, 0x8e03ce, 0x4701e7, 0xdc7af7,
0x91c77f, 0xb719bb, 0xa476d9, 0xadc168, 0x56e0b4, 0x2b705a, 0x15b82d, 0xf52612,
0x7a9309, 0xc2b380, 0x6159c0, 0x30ace0, 0x185670, 0x0c2b38, 0x06159c, 0x030ace,
0x018567, 0xff38b7, 0x80665f, 0xbfc92b, 0xa01e91, 0xaff54c, 0x57faa6, 0x2bfd53,
0xea04ad, 0x8af852, 0x457c29, 0xdd4410, 0x6ea208, 0x375104, 0x1ba882, 0x0dd441,
0xf91024, 0x7c8812, 0x3e4409, 0xe0d800, 0x706c00, 0x383600, 0x1c1b00, 0x0e0d80,
0x0706c0, 0x038360, 0x01c1b0, 0x00e0d8, 0x00706c, 0x003836, 0x001c1b, 0xfff409,
0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000
};

uint32_t modesChecksum(unsigned char *msg, int bits) {
    uint32_t   crc = 0;
    int        offset = (bits == 112) ? 0 : (112-56);
    uint8_t    theByte = *msg;
    uint32_t * pCRCTable = &modes_checksum_table[offset];
    int j;

    for(j = 0; j < bits; j++) {
        if ((j & 7) == 0)
            {theByte = *msg++;}

        // If bit is set, xor with corresponding table entry.
        if (theByte & 0x80) {crc ^= *pCRCTable;} 
        pCRCTable++;
        theByte = theByte << 1; 
    }
    return crc; // 24 bit checksum.
}

/* Given the Downlink Format (DF) of the message, return the message length
 * in bits. */
int modesMessageLenByType(int type) {
    if (type == 16 || type == 17 ||
        type == 19 || type == 20 ||
        type == 21)
        return MODES_LONG_MSG_BITS;
    else
        return MODES_SHORT_MSG_BITS;
}

/* Try to fix single bit errors using the checksum. On success modifies
 * the original buffer with the fixed version, and returns the position
 * of the error bit. Otherwise if fixing failed -1 is returned. */
int fixSingleBitErrors(unsigned char *msg, int bits, struct modesMessage *mm) {
    int j;
    unsigned char aux[MODES_LONG_MSG_BYTES];

    memcpy(aux, msg,bits/8);

    for (j = 0; j < bits; j++) {
        int byte = j/8;
        int bitmask = 1 << (7-(j%8));
        uint32_t crc1, crc2;

        aux[byte] ^= bitmask; /* Flip j-th bit. */

        crc1 = ((uint32_t)aux[(bits/8)-3] << 16) |
               ((uint32_t)aux[(bits/8)-2] << 8) |
                (uint32_t)aux[(bits/8)-1];
        crc2 = modesChecksum(aux,bits);

        if (crc1 == crc2) {
            /* The error is fixed. Overwrite the original buffer with
             * the corrected sequence, and returns the error bit
             * position. */
            memcpy(msg,aux,bits/8);
            if (mm)
               {
               mm->crc   = crc2;
               mm->iid   = 0;
               mm->crcok = 1;
               }
            return j;
        }

        aux[byte] ^= bitmask; /* Flip j-th bit back again. */
    }
    return -1;
}

/* Similar to fixSingleBitErrors() but try every possible two bit combination.
 * This is very slow and should be tried only against DF17 messages that
 * don't pass the checksum, and only in Aggressive Mode. */
int fixTwoBitsErrors(unsigned char *msg, int bits, struct modesMessage *mm) {
    int j, i;
    unsigned char aux[MODES_LONG_MSG_BYTES];

    memcpy(aux,msg, bits/8);

    for (j = 0; j < bits; j++) {
        int byte1 = j/8;
        int bitmask1 = 1 << (7-(j%8));
        aux[byte1] ^= bitmask1; /* Flip j-th bit. */

        /* Don't check the same pairs multiple times, so i starts from j+1 */
        for (i = j+1; i < bits; i++) {
            int byte2 = i/8;
            int bitmask2 = 1 << (7-(i%8));
            uint32_t crc1, crc2;

            aux[byte2] ^= bitmask2; /* Flip i-th bit. */

            crc1 = ((uint32_t)aux[(bits/8)-3] << 16) |
                   ((uint32_t)aux[(bits/8)-2] << 8) |
                    (uint32_t)aux[(bits/8)-1];
            crc2 = modesChecksum(aux,bits);

            if (crc1 == crc2) {
                /* The error is fixed. Overwrite the original buffer with
                 * the corrected sequence, and returns the error bit
                 * position. */
                memcpy(msg,aux,bits/8);
                if (mm)
                   {
                   mm->crc   = crc2;
                   mm->iid   = 0;
                   mm->crcok = 1;
                   }
                /* We return the two bits as a 16 bit integer by shifting
                 * 'i' on the left. This is possible since 'i' will always
                 * be non-zero because i starts from j+1. */
                return j | (i<<8);

            aux[byte2] ^= bitmask2; /* Flip i-th bit back. */
            }

        aux[byte1] ^= bitmask1; /* Flip j-th bit back. */
        }
    }
    return -1;
}

/* Hash the ICAO address to index our cache of MODES_ICAO_CACHE_LEN
 * elements, that is assumed to be a power of two. */
uint32_t ICAOCacheHashAddress(uint32_t a) {
    /* The following three rounds wil make sure that every bit affects
     * every output bit with ~ 50% of probability. */
    a = ((a >> 16) ^ a) * 0x45d9f3b;
    a = ((a >> 16) ^ a) * 0x45d9f3b;
    a = ((a >> 16) ^ a);
    return a & (MODES_ICAO_CACHE_LEN-1);
}

/* Add the specified entry to the cache of recently seen ICAO addresses.
 * Note that we also add a timestamp so that we can make sure that the
 * entry is only valid for MODES_ICAO_CACHE_TTL seconds. */
void addRecentlySeenICAOAddr(uint32_t addr) {
    uint32_t h = ICAOCacheHashAddress(addr);
    Modes.icao_cache[h*2] = addr;
    Modes.icao_cache[h*2+1] = (uint32_t) time(NULL);
}

/* Returns 1 if the specified ICAO address was seen in a DF format with
 * proper checksum (not xored with address) no more than * MODES_ICAO_CACHE_TTL
 * seconds ago. Otherwise returns 0. */
int ICAOAddressWasRecentlySeen(uint32_t addr) {
    uint32_t h = ICAOCacheHashAddress(addr);
    uint32_t a = Modes.icao_cache[h*2];
    uint32_t t = Modes.icao_cache[h*2+1];

    return a && a == addr && time(NULL)-t <= MODES_ICAO_CACHE_TTL;
}

/* If the message type has the checksum xored with the ICAO address, try to
 * brute force it using a list of recently seen ICAO addresses.
 *
 * Do this in a brute-force fashion by xoring the predicted CRC with
 * the address XOR checksum field in the message. This will recover the
 * address: if we found it in our cache, we can assume the message is ok.
 *
 * This function expects mm->msgtype and mm->msgbits to be correctly
 * populated by the caller.
 *
 * On success the correct ICAO address is stored in the modesMessage
 * structure in the addr field.
 *
 * If the function successfully recovers a message with a correct checksum
 * it returns 1. Otherwise 0 is returned. */
int bruteForceAP(unsigned char *msg, struct modesMessage *mm) {
    unsigned char aux[MODES_LONG_MSG_BYTES];
    int msgtype = mm->msgtype;
    int msgbits = mm->msgbits;

    if (msgtype == 0 ||         /* Short air surveillance */
        msgtype == 4 ||         /* Surveillance, altitude reply */
        msgtype == 5 ||         /* Surveillance, identity reply */
        msgtype == 16 ||        /* Long Air-Air survillance */
        msgtype == 20 ||        /* Comm-A, altitude request */
        msgtype == 21 ||        /* Comm-A, identity request */
        msgtype == 24)          /* Comm-C ELM */
    {
        uint32_t addr;
        uint32_t crc;
        int lastbyte = (msgbits/8)-1;

        /* Work on a copy. */
        memcpy(aux,msg,msgbits/8);

        /* Compute the CRC of the message and XOR it with the AP field
         * so that we recover the address, because:
         *
         * (ADDR xor CRC) xor CRC = ADDR. */
        crc = modesChecksum(aux,msgbits);
        aux[lastbyte]   ^=  crc        & 0xff;
        aux[lastbyte-1] ^= (crc >> 8)  & 0xff;
        aux[lastbyte-2] ^= (crc >> 16) & 0xff;
        
        /* If the obtained address exists in our cache we consider
         * the message valid. */
        addr = aux[lastbyte] | (aux[lastbyte-1] << 8) | (aux[lastbyte-2] << 16);
        if (ICAOAddressWasRecentlySeen(addr)) {
            mm->addr = addr;
            return (1);
        }
    }
    return (0);
}

/* Decode the 13 bit AC altitude field (in DF 20 and others).
 * Returns the altitude, and set 'unit' to either MODES_UNIT_METERS
 * or MDOES_UNIT_FEETS. */
int decodeAC13Field(unsigned char *msg, int *unit) {
    int m_bit = msg[3] & (1<<6);
    int q_bit = msg[3] & (1<<4);

    if (!m_bit) {
        *unit = MODES_UNIT_FEET;
        if (q_bit) {
            /* N is the 11 bit integer resulting from the removal of bit
             * Q and M */
            int n = ((msg[2]&31)<<6) |
                    ((msg[3]&0x80)>>2) |
                    ((msg[3]&0x20)>>1) |
                     (msg[3]&15);
            /* The final altitude is due to the resulting number multiplied
             * by 25, minus 1000. */
            return n*25-1000;
        } else {
            /* TODO: Implement altitude where Q=0 and M=0 */
        }
    } else {
        *unit = MODES_UNIT_METERS;
        /* TODO: Implement altitude when meter unit is selected. */
    }
    return 0;
}

/* Decode the 12 bit AC altitude field (in DF 17 and others).
 * Returns the altitude or 0 if it can't be decoded. */
int decodeAC12Field(unsigned char *msg, int *unit) {
    int q_bit = msg[5] & 1;

    if (q_bit) {
        /* N is the 11 bit integer resulting from the removal of bit
         * Q */
        int n = ((msg[5]>>1)<<4) | ((msg[6]&0xF0) >> 4);
        *unit = MODES_UNIT_FEET;
        /* The final altitude is due to the resulting number multiplied
         * by 25, minus 1000. */
        return n*25-1000;
    } else {
        return 0;
    }
}

/* Capability table. */
char *ca_str[8] = {
    /* 0 */ "Level 1 (Survillance Only)",
    /* 1 */ "Level 2 (DF0,4,5,11)",
    /* 2 */ "Level 3 (DF0,4,5,11,20,21)",
    /* 3 */ "Level 4 (DF0,4,5,11,20,21,24)",
    /* 4 */ "Level 2+3+4 (DF0,4,5,11,20,21,24,code7 - is on ground)",
    /* 5 */ "Level 2+3+4 (DF0,4,5,11,20,21,24,code7 - is on airborne)",
    /* 6 */ "Level 2+3+4 (DF0,4,5,11,20,21,24,code7)",
    /* 7 */ "Level 7 ???"
};

/* Flight status table. */
char *fs_str[8] = {
    /* 0 */ "Normal, Airborne",
    /* 1 */ "Normal, On the ground",
    /* 2 */ "ALERT,  Airborne",
    /* 3 */ "ALERT,  On the ground",
    /* 4 */ "ALERT & Special Position Identification. Airborne or Ground",
    /* 5 */ "Special Position Identification. Airborne or Ground",
    /* 6 */ "Value 6 is not assigned",
    /* 7 */ "Value 7 is not assigned"
};

char *getMEDescription(int metype, int mesub) {
    char *mename = "Unknown";

    if (metype >= 1 && metype <= 4)
        mename = "Aircraft Identification and Category";
    else if (metype >= 5 && metype <= 8)
        mename = "Surface Position";
    else if (metype >= 9 && metype <= 18)
        mename = "Airborne Position (Baro Altitude)";
    else if (metype == 19 && mesub >=1 && mesub <= 4)
        mename = "Airborne Velocity";
    else if (metype >= 20 && metype <= 22)
        mename = "Airborne Position (GNSS Height)";
    else if (metype == 23 && mesub == 0)
        mename = "Test Message";
    else if (metype == 24 && mesub == 1)
        mename = "Surface System Status";
    else if (metype == 28 && mesub == 1)
        mename = "Extended Squitter Aircraft Status (Emergency)";
    else if (metype == 28 && mesub == 2)
        mename = "Extended Squitter Aircraft Status (1090ES TCAS RA)";
    else if (metype == 29 && (mesub == 0 || mesub == 1))
        mename = "Target State and Status Message";
    else if (metype == 31 && (mesub == 0 || mesub == 1))
        mename = "Aircraft Operational Status Message";
    return mename;
}

/* Decode a raw Mode S message demodulated as a stream of bytes by
 * detectModeS(), and split it into fields populating a modesMessage
 * structure. */
void decodeModesMessage(struct modesMessage *mm, unsigned char *msg) {
    uint32_t crc2;   /* Computed CRC, used to verify the message CRC. */
    char *ais_charset = "?ABCDEFGHIJKLMNOPQRSTUVWXYZ????? ???????????????0123456789??????";

    /* Work on our local copy */
    memcpy(mm->msg, msg, MODES_LONG_MSG_BYTES);
    msg = mm->msg;

    /* Get the message type ASAP as other operations depend on this */
    mm->msgtype = msg[0] >> 3;  /* Downlink Format */
    mm->msgbits = modesMessageLenByType(mm->msgtype);

    /* CRC is always the last three bytes. */
    mm->crc = ((uint32_t)msg[(mm->msgbits/8)-3] << 16) |
              ((uint32_t)msg[(mm->msgbits/8)-2] << 8) |
               (uint32_t)msg[(mm->msgbits/8)-1];
    crc2 = modesChecksum(msg, mm->msgbits);
    mm->iid = (mm->crc ^ crc2);

    /* Check CRC and fix single bit errors using the CRC when
     * possible (DF 11 and 17). */
    mm->errorbit = -1;  /* No error */
    if (mm->msgtype == 11)
        {mm->crcok = (mm->iid < 80);}
    else
        {mm->crcok = (mm->iid == 0);}

    if (!mm->crcok && Modes.fix_errors && (mm->msgtype == 17)){
//    if (!mm->crcok && Modes.fix_errors && ((mm->msgtype == 11) || (mm->msgtype == 17))){
        //
        // Fixing single bit errors in DF-11 is a bit dodgy because we have no way to 
        // know for sure if the crc is supposed to be 0 or not - it could be any value 
        // less than 80. Therefore, attempting to fix DF-11 errors can result in a 
        // multitude of possible crc solutions, only one of which is correct.
        // 
        // We should probably perform some sanity checks on corrected DF-11's before 
        // using the results. Perhaps check the ICAO against known aircraft, and check
        // IID against known good IID's. That's a TODO.
        //
        mm->errorbit = fixSingleBitErrors(msg, mm->msgbits, mm);
        if ((mm->errorbit == -1) && (Modes.aggressive)) {
            mm->errorbit = fixTwoBitsErrors(msg, mm->msgbits, mm);
        }
    }

    /* Note that most of the other computation happens *after* we fix
     * the single bit errors, otherwise we would need to recompute the
     * fields again. */
    mm->ca = msg[0] & 7;        /* Responder capabilities. */

    // ICAO address
    mm->addr = (msg[1] << 16) | (msg[2] << 8) | (msg[3]); 

    /* DF 17 type (assuming this is a DF17, otherwise not used) */
    mm->metype = msg[4] >> 3;   /* Extended squitter message type. */
    mm->mesub = msg[4] & 7;     /* Extended squitter message subtype. */

    /* Fields for DF4,5,20,21 */
    mm->fs = msg[0] & 7;        /* Flight status for DF4,5,20,21 */
    mm->dr = msg[1] >> 3 & 31;  /* Request extraction of downlink request. */
    mm->um = ((msg[1] & 7)<<3)| /* Request extraction of downlink request. */
              msg[2]>>5;

    /* In the squawk (identity) field bits are interleaved like that
     * (message bit 20 to bit 32):
     *
     * C1-A1-C2-A2-C4-A4-ZERO-B1-D1-B2-D2-B4-D4
     *
     * So every group of three bits A, B, C, D represent an integer
     * from 0 to 7.
     *
     * The actual meaning is just 4 octal numbers, but we convert it
     * into a hex number tha happens to represent the four
     * octal numbers.
     *
     * For more info: http://en.wikipedia.org/wiki/Gillham_code */
    {
        int           hexSquawk = 0;
        unsigned char rawSquawk;

        rawSquawk = msg[2];
        if (rawSquawk & 0x01) {hexSquawk |= 0x0040;} // C4
        if (rawSquawk & 0x02) {hexSquawk |= 0x2000;} // A2
        if (rawSquawk & 0x04) {hexSquawk |= 0x0020;} // C2
        if (rawSquawk & 0x08) {hexSquawk |= 0x1000;} // A1
        if (rawSquawk & 0x10) {hexSquawk |= 0x0010;} // C1

        rawSquawk = msg[3];
        if (rawSquawk & 0x01) {hexSquawk |= 0x0004;} // D4
        if (rawSquawk & 0x02) {hexSquawk |= 0x0400;} // B4
        if (rawSquawk & 0x04) {hexSquawk |= 0x0002;} // D2
        if (rawSquawk & 0x08) {hexSquawk |= 0x0200;} // B2
        if (rawSquawk & 0x10) {hexSquawk |= 0x0001;} // D1
        if (rawSquawk & 0x20) {hexSquawk |= 0x0100;} // B1 
        if (rawSquawk & 0x80) {hexSquawk |= 0x4000;} // A4

        mm->modeA = hexSquawk;
    }

    /* DF 11 & 17: try to populate our ICAO addresses whitelist.
     * DFs with an AP field (xored addr and crc), try to decode it. */
    if (mm->msgtype != 11 && mm->msgtype != 17) {
        /* Check if we can check the checksum for the Downlink Formats where
         * the checksum is xored with the aircraft ICAO address. We try to
         * brute force it using a list of recently seen aircraft addresses. */
        if (bruteForceAP(msg,mm)) {
            /* We recovered the message, mark the checksum as valid. */
            mm->crcok = 1;
        } else {
            mm->crcok = 0;
        }
    } else {
        /* If this is DF 11 or DF 17 and the checksum was ok,
         * we can add this address to the list of recently seen
         * addresses. */
        if (mm->crcok && mm->errorbit == -1) {
            addRecentlySeenICAOAddr(mm->addr);
        }
    }

    /* Decode 13 bit altitude for DF0, DF4, DF16, DF20 */
    if (mm->msgtype == 0 || mm->msgtype == 4 ||
        mm->msgtype == 16 || mm->msgtype == 20) {
        mm->altitude = decodeAC13Field(msg, &mm->unit);
    }

    /* Decode extended squitter specific stuff. */
    if (mm->msgtype == 17) {
        /* Decode the extended squitter message. */

        if (mm->metype >= 1 && mm->metype <= 4) {
            /* Aircraft Identification and Category */
            mm->aircraft_type = mm->metype-1;
            mm->flight[0] = ais_charset[msg[5]>>2];
            mm->flight[1] = ais_charset[((msg[5]&3)<<4)|(msg[6]>>4)];
            mm->flight[2] = ais_charset[((msg[6]&15)<<2)|(msg[7]>>6)];
            mm->flight[3] = ais_charset[msg[7]&63];
            mm->flight[4] = ais_charset[msg[8]>>2];
            mm->flight[5] = ais_charset[((msg[8]&3)<<4)|(msg[9]>>4)];
            mm->flight[6] = ais_charset[((msg[9]&15)<<2)|(msg[10]>>6)];
            mm->flight[7] = ais_charset[msg[10]&63];
            mm->flight[8] = '\0';
        } else if (mm->metype >= 9 && mm->metype <= 18) {
            /* Airborne position Message */
            mm->fflag = msg[6] & (1<<2);
            mm->tflag = msg[6] & (1<<3);
            mm->altitude = decodeAC12Field(msg,&mm->unit);
            mm->raw_latitude = ((msg[6] & 3) << 15) |
                                (msg[7] << 7) |
                                (msg[8] >> 1);
            mm->raw_longitude = ((msg[8]&1) << 16) |
                                 (msg[9] << 8) |
                                 msg[10];
        } else if (mm->metype == 19 && mm->mesub >= 1 && mm->mesub <= 4) {
            /* Airborne Velocity Message */
            if (mm->mesub == 1 || mm->mesub == 2) {
                mm->ew_dir = (msg[5]&4) >> 2;
                mm->ew_velocity = ((msg[5]&3) << 8) | msg[6];
                mm->ns_dir = (msg[7]&0x80) >> 7;
                mm->ns_velocity = ((msg[7]&0x7f) << 3) | ((msg[8]&0xe0) >> 5);
                mm->vert_rate_source = (msg[8]&0x10) >> 4;
                mm->vert_rate_sign = (msg[8]&0x8) >> 3;
                mm->vert_rate = ((msg[8]&7) << 6) | ((msg[9]&0xfc) >> 2);
                /* Compute velocity and angle from the two speed
                 * components. */
                mm->velocity = (int) sqrt(mm->ns_velocity*mm->ns_velocity+
                                          mm->ew_velocity*mm->ew_velocity);
                if (mm->velocity) {
                    int ewv = mm->ew_velocity;
                    int nsv = mm->ns_velocity;
                    double heading;

                    if (mm->ew_dir) ewv *= -1;
                    if (mm->ns_dir) nsv *= -1;
                    heading = atan2(ewv,nsv);

                    /* Convert to degrees. */
                    mm->heading = (int) (heading * 360 / (M_PI*2));
                    /* We don't want negative values but a 0-360 scale. */
                    if (mm->heading < 0) mm->heading += 360;
                } else {
                    mm->heading = 0;
                }
            } else if (mm->mesub == 3 || mm->mesub == 4) {
                mm->heading_is_valid = msg[5] & (1<<2);
                mm->heading = (int) (360.0/128) * (((msg[5] & 3) << 5) |
                                                    (msg[6] >> 3));
            }
        }
    }
    mm->phase_corrected = 0; /* Set to 1 by the caller if needed. */
}

/* This function gets a decoded Mode S Message and prints it on the screen
 * in a human readable format. */
void displayModesMessage(struct modesMessage *mm) {
    int j;
    char * pTimeStamp;

    /* Handle only addresses mode first. */
    if (Modes.onlyaddr) {
        printf("%06x\n", mm->addr);
        return;
    }

    /* Show the raw message. */
    if (Modes.mlat) {
        printf("@");
        pTimeStamp = (char *) &mm->timestampMsg;
        for (j=5; j>=0;j--) {
            printf("%02X",pTimeStamp[j]);
        } 
    } else
        printf("*");

    for (j = 0; j < mm->msgbits/8; j++) printf("%02x", mm->msg[j]);
    printf(";\n");

    if (Modes.raw) {
        fflush(stdout); /* Provide data to the reader ASAP. */
        return; /* Enough for --raw mode */
    }

    if (mm->msgtype < 32)
        printf("CRC: %06x (%s)\n", (int)mm->crc, mm->crcok ? "ok" : "wrong");

    if (mm->errorbit != -1)
        printf("Single bit error fixed, bit %d\n", mm->errorbit);

    if (mm->msgtype == 0) {
        /* DF 0 */
        printf("DF 0: Short Air-Air Surveillance.\n");
        printf("  Altitude       : %d %s\n", mm->altitude,
            (mm->unit == MODES_UNIT_METERS) ? "meters" : "feet");
        printf("  ICAO Address   : %06x\n", mm->addr);
    } else if (mm->msgtype == 4 || mm->msgtype == 20) {
        printf("DF %d: %s, Altitude Reply.\n", mm->msgtype,
            (mm->msgtype == 4) ? "Surveillance" : "Comm-B");
        printf("  Flight Status  : %s\n", fs_str[mm->fs]);
        printf("  DR             : %d\n", mm->dr);
        printf("  UM             : %d\n", mm->um);
        printf("  Altitude       : %d %s\n", mm->altitude,
            (mm->unit == MODES_UNIT_METERS) ? "meters" : "feet");
        printf("  ICAO Address   : %06x\n", mm->addr);

        if (mm->msgtype == 20) {
            /* TODO: 56 bits DF20 MB additional field. */
        }
    } else if (mm->msgtype == 5 || mm->msgtype == 21) {
        printf("DF %d: %s, Identity Reply.\n", mm->msgtype,
            (mm->msgtype == 5) ? "Surveillance" : "Comm-B");
        printf("  Flight Status  : %s\n", fs_str[mm->fs]);
        printf("  DR             : %d\n", mm->dr);
        printf("  UM             : %d\n", mm->um);
        printf("  Squawk         : %x\n", mm->modeA);
        printf("  ICAO Address   : %06x\n", mm->addr);

        if (mm->msgtype == 21) {
            /* TODO: 56 bits DF21 MB additional field. */
        }
    } else if (mm->msgtype == 11) {
        /* DF 11 */
        printf("DF 11: All Call Reply.\n");
        printf("  Capability  : %s\n", ca_str[mm->ca]);
        printf("  ICAO Address: %06x\n", mm->addr);
        if (mm->iid > 16)
            {printf("  IID         : SI-%02d\n", mm->iid-16);}
        else
            {printf("  IID         : II-%02d\n", mm->iid);}
    } else if (mm->msgtype == 17) {
        /* DF 17 */
        printf("DF 17: ADS-B message.\n");
        printf("  Capability     : %d (%s)\n", mm->ca, ca_str[mm->ca]);
        printf("  ICAO Address   : %06x\n", mm->addr);
        printf("  Extended Squitter  Type: %d\n", mm->metype);
        printf("  Extended Squitter  Sub : %d\n", mm->mesub);
        printf("  Extended Squitter  Name: %s\n",
            getMEDescription(mm->metype,mm->mesub));

        /* Decode the extended squitter message. */
        if (mm->metype >= 1 && mm->metype <= 4) {
            /* Aircraft identification. */
            char *ac_type_str[4] = {
                "Aircraft Type D",
                "Aircraft Type C",
                "Aircraft Type B",
                "Aircraft Type A"
            };

            printf("    Aircraft Type  : %s\n", ac_type_str[mm->aircraft_type]);
            printf("    Identification : %s\n", mm->flight);
        } else if (mm->metype >= 9 && mm->metype <= 18) {
            printf("    F flag   : %s\n", mm->fflag ? "odd" : "even");
            printf("    T flag   : %s\n", mm->tflag ? "UTC" : "non-UTC");
            printf("    Altitude : %d feet\n", mm->altitude);
            printf("    Latitude : %d (not decoded)\n", mm->raw_latitude);
            printf("    Longitude: %d (not decoded)\n", mm->raw_longitude);
        } else if (mm->metype == 19 && mm->mesub >= 1 && mm->mesub <= 4) {
            if (mm->mesub == 1 || mm->mesub == 2) {
                /* Velocity */
                printf("    EW direction      : %d\n", mm->ew_dir);
                printf("    EW velocity       : %d\n", mm->ew_velocity);
                printf("    NS direction      : %d\n", mm->ns_dir);
                printf("    NS velocity       : %d\n", mm->ns_velocity);
                printf("    Vertical rate src : %d\n", mm->vert_rate_source);
                printf("    Vertical rate sign: %d\n", mm->vert_rate_sign);
                printf("    Vertical rate     : %d\n", mm->vert_rate);
            } else if (mm->mesub == 3 || mm->mesub == 4) {
                printf("    Heading status: %d", mm->heading_is_valid);
                printf("    Heading: %d", mm->heading);
            }
        } else {
            printf("    Unrecognized ME type: %d subtype: %d\n", 
                mm->metype, mm->mesub);
        }
    } else if (mm->msgtype == 32) {
        // DF 32 is special code we use for Mode A/C
        printf("SSR : Mode A/C Reply.\n");
        if (mm->fs & 0x0080) {
            printf("  Mode A : %04x IDENT\n", mm->modeA);
        } else {
            int modeC = ModeAToModeC(mm->modeA);
            printf("  Mode A : %04x\n", mm->modeA);
            if (modeC >= -13)
                {printf("  Mode C : %d feet\n", (modeC * 100));}
        }

    } else {
        if (Modes.check_crc)
            printf("DF %d with good CRC received "
                   "(decoding still not implemented).\n",
                mm->msgtype);
    }
}

/* Turn I/Q samples pointed by Modes.data into the magnitude vector
 * pointed by Modes.magnitude. */
void computeMagnitudeVector(void) {
    uint16_t *m = &Modes.magnitude[MODES_PREAMBLE_SAMPLES+MODES_LONG_MSG_SAMPLES];
    uint16_t *p = Modes.data;
    uint32_t j;

    memcpy(Modes.magnitude,&Modes.magnitude[MODES_ASYNC_BUF_SAMPLES], MODES_PREAMBLE_SIZE+MODES_LONG_MSG_SIZE);

    /* Compute the magnitudo vector. It's just SQRT(I^2 + Q^2), but
     * we rescale to the 0-255 range to exploit the full resolution. */
    for (j = 0; j < MODES_ASYNC_BUF_SAMPLES; j ++) {
        *m++ = Modes.maglut[*p++];
    }
}

/* Return -1 if the message is out of fase left-side
 * Return  1 if the message is out of fase right-size
 * Return  0 if the message is not particularly out of phase.
 *
 * Note: this function will access pPreamble[-1], so the caller should make sure to
 * call it only if we are not at the start of the current buffer. */
int detectOutOfPhase(uint16_t *pPreamble) {
    if (pPreamble[ 3] > pPreamble[2]/3) return  1;
    if (pPreamble[10] > pPreamble[9]/3) return  1;
    if (pPreamble[ 6] > pPreamble[7]/3) return -1;
    if (pPreamble[-1] > pPreamble[1]/3) return -1;
    return 0;
}

/* This function does not really correct the phase of the message, it just
 * applies a transformation to the first sample representing a given bit:
 *
 * If the previous bit was one, we amplify it a bit.
 * If the previous bit was zero, we decrease it a bit.
 *
 * This simple transformation makes the message a bit more likely to be
 * correctly decoded for out of phase messages:
 *
 * When messages are out of phase there is more uncertainty in
 * sequences of the same bit multiple times, since 11111 will be
 * transmitted as continuously altering magnitude (high, low, high, low...)
 * 
 * However because the message is out of phase some part of the high
 * is mixed in the low part, so that it is hard to distinguish if it is
 * a zero or a one.
 *
 * However when the message is out of phase passing from 0 to 1 or from
 * 1 to 0 happens in a very recognizable way, for instance in the 0 -> 1
 * transition, magnitude goes low, high, high, low, and one of of the
 * two middle samples the high will be *very* high as part of the previous
 * or next high signal will be mixed there.
 *
 * Applying our simple transformation we make more likely if the current
 * bit is a zero, to detect another zero. Symmetrically if it is a one
 * it will be more likely to detect a one because of the transformation.
 * In this way similar levels will be interpreted more likely in the
 * correct way. */
void applyPhaseCorrection(uint16_t *pPayload) {
    int j;

    for (j = 0; j < MODES_LONG_MSG_SAMPLES; j += 2, pPayload += 2) {
        if (pPayload[0] > pPayload[1]) { /* One */
            pPayload[2] = (pPayload[2] * 5) / 4;
        } else {                           /* Zero */
            pPayload[2] = (pPayload[2] * 4) / 5;
        }
    }
}

/* Detect a Mode S messages inside the magnitude buffer pointed by 'm' and of
 * size 'mlen' bytes. Every detected Mode S message is convert it into a
 * stream of bits and passed to the function to display it. */
void detectModeS(uint16_t *m, uint32_t mlen) {
    unsigned char msg[MODES_LONG_MSG_BYTES], *pMsg;
    uint16_t aux[MODES_LONG_MSG_SAMPLES];
    uint32_t j;
    int use_correction = 0;

    /* The Mode S preamble is made of impulses of 0.5 microseconds at
     * the following time offsets:
     *
     * 0   - 0.5 usec: first impulse.
     * 1.0 - 1.5 usec: second impulse.
     * 3.5 - 4   usec: third impulse.
     * 4.5 - 5   usec: last impulse.
     * 
     * Since we are sampling at 2 Mhz every sample in our magnitude vector
     * is 0.5 usec, so the preamble will look like this, assuming there is
     * an impulse at offset 0 in the array:
     *
     * 0   -----------------
     * 1   -
     * 2   ------------------
     * 3   --
     * 4   -
     * 5   --
     * 6   -
     * 7   ------------------
     * 8   --
     * 9   -------------------
     */
    for (j = 0; j < mlen; j++) {
        int high, i, errors, errors56, errorsTy; 
        int good_message = 0;
        uint16_t *pPreamble, *pPayload, *pPtr;
        uint8_t  theByte, theErrs;
        int msglen, sigStrength;

        pPreamble = &m[j];
        pPayload  = &m[j+MODES_PREAMBLE_SAMPLES];

        if (!use_correction)  // This is not a re-try with phase correction
            {                 // so try to find a new preamble

            if (Modes.mode_ac) 
                {
                struct modesMessage mm;
                int ModeA = detectModeA(pPreamble, &mm);

                if (ModeA) // We have found a valid ModeA/C in the data                    
                    {
                    mm.timestampMsg = Modes.timestampBlk + ((j+1) * 6);

                    // Decode the received message
                    decodeModeAMessage(&mm, ModeA);

                    // Pass data to the next layer
                    useModesMessage(&mm);

                    j += MODEAC_MSG_SAMPLES;
                    Modes.stat_ModeAC++;
                    continue;
                    }
                }

            /* First check of relations between the first 10 samples
             * representing a valid preamble. We don't even investigate further
             * if this simple test is not passed. */
            if (!(pPreamble[0] > pPreamble[1] &&
                  pPreamble[1] < pPreamble[2] &&
                  pPreamble[2] > pPreamble[3] &&
                  pPreamble[3] < pPreamble[0] &&
                  pPreamble[4] < pPreamble[0] &&
                  pPreamble[5] < pPreamble[0] &&
                  pPreamble[6] < pPreamble[0] &&
                  pPreamble[7] > pPreamble[8] &&
                  pPreamble[8] < pPreamble[9] &&
                  pPreamble[9] > pPreamble[6]))
            {
                if (Modes.debug & MODES_DEBUG_NOPREAMBLE &&
                    *pPreamble  > MODES_DEBUG_NOPREAMBLE_LEVEL)
                    dumpRawMessage("Unexpected ratio among first 10 samples", msg, m, j);
                continue;
            }

            /* The samples between the two spikes must be < than the average
             * of the high spikes level. We don't test bits too near to
             * the high levels as signals can be out of phase so part of the
             * energy can be in the near samples. */
            high = (pPreamble[0] + pPreamble[2] + pPreamble[7] + pPreamble[9]) / 6;
            if (pPreamble[4] >= high ||
                pPreamble[5] >= high)
            {
                if (Modes.debug & MODES_DEBUG_NOPREAMBLE &&
                    *pPreamble  > MODES_DEBUG_NOPREAMBLE_LEVEL)
                    dumpRawMessage("Too high level in samples between 3 and 6", msg, m, j);
                continue;
            }

            /* Similarly samples in the range 11-14 must be low, as it is the
             * space between the preamble and real data. Again we don't test
             * bits too near to high levels, see above. */
            if (pPreamble[11] >= high ||
                pPreamble[12] >= high ||
                pPreamble[13] >= high ||
                pPreamble[14] >= high)
            {
                if (Modes.debug & MODES_DEBUG_NOPREAMBLE &&
                    *pPreamble  > MODES_DEBUG_NOPREAMBLE_LEVEL)
                    dumpRawMessage("Too high level in samples between 10 and 15", msg, m, j);
                continue;
            }
            Modes.stat_valid_preamble++;
        } 

        else {
            /* If the previous attempt with this message failed, retry using
             * magnitude correction. */
            // Make a copy of the Payload, and phase correct the copy
            memcpy(aux, pPayload, sizeof(aux));
            applyPhaseCorrection(aux);
            Modes.stat_out_of_phase++;
            pPayload = aux;
            /* TODO ... apply other kind of corrections. */
            }

        /* Decode all the next 112 bits, regardless of the actual message
         * size. We'll check the actual message type later. */     
        pMsg    = &msg[0];
        pPtr    = pPayload;
        theByte = 0;
        theErrs = 0; errorsTy = 0;
        errors  = 0; errors56 = 0;

        // We should have 4 'bits' of 0/1 and 1/0 samples in the preamble, 
        // so include these in the signal strength 
        sigStrength = (pPreamble[0]-pPreamble[1])
                    + (pPreamble[2]-pPreamble[3])
                    + (pPreamble[7]-pPreamble[6])
                    + (pPreamble[9]-pPreamble[8]);

        msglen = MODES_LONG_MSG_BITS;
        for (i = 0; i < msglen; i++) {
            uint32_t a = *pPtr++;
            uint32_t b = *pPtr++;

            if      (a > b) 
                {sigStrength += (a-b); theByte |= 1;} 
            else if (a < b) 
                {sigStrength += (b-a); /*theByte |= 0;*/} 
            else if (i >= MODES_SHORT_MSG_BITS) //(a == b), and we're in the long part of a frame
                {errors++;  /*theByte |= 0;*/} 
            else if (i >= 5)                    //(a == b), and we're in the short part of a frame
                {errors56 = ++errors;/*theByte |= 0;*/}            
            else                                //(a == b), and we're in the message type part of a frame
                {errorsTy = errors56 = ++errors; theErrs |= 1; /*theByte |= 0;*/} 

            if ((i & 7) == 7) 
              {*pMsg++ = theByte;}
            else if ((i == 4) && (errors == 0))
              {msglen  = modesMessageLenByType(theByte);}

            theByte = theByte << 1;
            if (i < 8)
              {theErrs = theErrs << 1;}


            // If we've exceeded the permissible number of encoding errors, abandon ship now
            if (errors > MODES_MSG_ENCODER_ERRS)
                {
                // If we're in the long frame when it went to pot, but it was still ok-ish when we
                // were in the short part of the frame, then try for a mis-identified short frame
                // we must believe that this should've been a long frame to get this far. 
                if (i >= MODES_SHORT_MSG_BITS)
                    {
                    // If we did see some errors in the first byte of the frame, then it's possible 
                    // we guessed wrongly about the value of the bit. If we only saw one error, we may
                    // be able to correct it by guessing the other way.
                    if (errorsTy == 1)
                        {
                        // See if inverting the bit we guessed at would change the message type from a
                        // long to a short. If it would, invert the bit, cross your fingers and carry on.
                        theByte = pMsg[0] ^ theErrs;
                        if (MODES_SHORT_MSG_BITS == modesMessageLenByType(theByte))
                            {
                            pMsg[0] = theByte;  // write the modified type back to the msg buffer
                            errors  = errors56; // revert to the number of errors prior to bit 56
                            msglen  = MODES_SHORT_MSG_BITS;
                            i--;                // this latest sample was zero, so we can ignore it.
                            Modes.stat_DF_Corrected++;
                            }
                        }
                    }
                break;
                }
        }

        // Don't forget to add 4 for the preamble samples. This also removes any risk of dividing by zero.
        sigStrength /= (msglen+4);

        /* If we reached this point, and error is zero, we are very likely
         * with a Mode S message in our hands, but it may still be broken
         * and CRC may not be correct. This is handled by the next layer. */
        if ( (sigStrength > MODES_MSG_SQUELCH_LEVEL) && (errors <= MODES_MSG_ENCODER_ERRS) )
            {
            struct modesMessage mm;

            /* Decode the received message and update statistics */
            mm.timestampMsg = Modes.timestampBlk + (j*6);
            sigStrength    = (sigStrength + 0x7F) >> 8;
            mm.signalLevel = ((sigStrength < 255) ? sigStrength : 255);
            decodeModesMessage(&mm,msg);

            /* Update statistics. */
            if (mm.crcok || use_correction) {
                if (errors == 0) Modes.stat_demodulated++;
                if (mm.errorbit == -1) {
                    if (mm.crcok)
                        Modes.stat_goodcrc++;
                    else
                        Modes.stat_badcrc++;
                } else {
                    Modes.stat_badcrc++;
                    Modes.stat_fixed++;
                    if (mm.errorbit < MODES_LONG_MSG_BITS)
                        Modes.stat_single_bit_fix++;
                    else
                        Modes.stat_two_bits_fix++;
                }
            }

            /* Output debug mode info if needed. */
            if (use_correction) {
                if (Modes.debug & MODES_DEBUG_DEMOD)
                    dumpRawMessage("Demodulated with 0 errors", msg, m, j);
                else if (Modes.debug & MODES_DEBUG_BADCRC &&
                         mm.msgtype == 17 &&
                         (!mm.crcok || mm.errorbit != -1))
                    dumpRawMessage("Decoded with bad CRC", msg, m, j);
                else if (Modes.debug & MODES_DEBUG_GOODCRC && mm.crcok &&
                         mm.errorbit == -1)
                    dumpRawMessage("Decoded with good CRC", msg, m, j);
            }

            /* Skip this message if we are sure it's fine. */
            if (mm.crcok) {
                j += (MODES_PREAMBLE_US+msglen)*2;
                good_message = 1;
                if (use_correction)
                    mm.phase_corrected = 1;
            }

            /* Pass data to the next layer */
            useModesMessage(&mm);

        } else {
            if (Modes.debug & MODES_DEBUG_DEMODERR && use_correction) {
                printf("The following message has %d demod errors\n", errors);
                dumpRawMessage("Demodulated with errors", msg, m, j);
            }
        }

        // Retry with phase correction if possible.
        if (!good_message && !use_correction && j && detectOutOfPhase(pPreamble)) {
            use_correction = 1; j--;
        } else {
            use_correction = 0; 
        }
    }

    //Send any remaining partial raw buffers now
    if (Modes.rawOutUsed)
      {
      Modes.net_output_raw_rate_count++;
      if (Modes.net_output_raw_rate_count > Modes.net_output_raw_rate)
        {
        modesSendAllClients(Modes.ros, Modes.rawOut, Modes.rawOutUsed);
        Modes.rawOutUsed = 0;
        Modes.net_output_raw_rate_count = 0;
        }
      }
}

/* When a new message is available, because it was decoded from the
 * RTL device, file, or received in the TCP input port, or any other
 * way we can receive a decoded message, we call this function in order
 * to use the message.
 *
 * Basically this function passes a raw message to the upper layers for
 * further processing and visualization. */
void useModesMessage(struct modesMessage *mm) {
    if (!Modes.stats && (Modes.check_crc == 0 || mm->crcok)) {
        // Track aircrafts if...
        if ( (Modes.interactive)              //       in interactive mode
          || (Modes.stat_http_requests > 0)   // or if the HTTP interface is enabled
          || (Modes.stat_sbs_connections > 0) // or if sbs connections are established 
          || (Modes.mode_ac) ) {              // or if mode A/C decoding is enabled
            struct aircraft *a = interactiveReceiveData(mm);
            if ( (a) 
              && (mm->msgtype < 32)           // don't even try to send ModesA/C to SBS clients
              && (Modes.stat_sbs_connections > 0) ) 
                {modesSendSBSOutput(mm, a);}  // Feed SBS output clients
        }

        // In non-interactive mode, and non-quiet mode, display messages on 
        // standard output as they occur.
        if (!Modes.interactive && !Modes.quiet) {
            displayModesMessage(mm);
            if (!Modes.raw && !Modes.onlyaddr) printf("\n");
        }

        // Send data to connected network clients
        if (Modes.net) {
            if (Modes.beast)
                modesSendBeastOutput(mm);
            else
                modesSendRawOutput(mm);
        }
    }
}

/* ========================= Interactive mode =============================== */
//
// Return a new aircraft structure for the interactive mode linked list
// of aircraft
//
struct aircraft *interactiveCreateAircraft(struct modesMessage *mm) {
    struct aircraft *a = (struct aircraft *) malloc(sizeof(*a));

    a->addr         = mm->addr;
    a->flight[0]    = '\0';
    a->speed        = 0;
    a->track        = 0;
    a->odd_cprlat   = 0;
    a->odd_cprlon   = 0;
    a->odd_cprtime  = 0;
    a->even_cprlat  = 0;
    a->even_cprlon  = 0;
    a->even_cprtime = 0;
    a->lat          = 0;
    a->lon          = 0;
    a->sbsflags     = 0;
    a->seen         = time(NULL);
    a->messages     = 0;
    if (mm->msgtype == 32) {
        a->modeACflags = MODEAC_MSG_FLAG;
        a->modeA       = mm->modeA;
        a->modeC       = ModeAToModeC(mm->modeA | mm->fs);
        a->altitude    = a->modeC * 100;
        if (a->modeC < -12)
            {a->modeACflags |= MODEAC_MSG_MODEA_ONLY;}  
    } else {
        a->modeACflags = 0;
        a->modeA       = 0;
        a->modeC       = 0;
        a->altitude    = 0;
    }
    a->modeAcount   = 0;
    a->modeCcount   = 0;
    a->next         = NULL;
    return (a);
}
//
// Return the aircraft with the specified address, or NULL if no aircraft
// exists with this address.
//
struct aircraft *interactiveFindAircraft(uint32_t addr) {
    struct aircraft *a = Modes.aircrafts;

    while(a) {
        if (a->addr == addr) return (a);
        a = a->next;
    }
    return (NULL);
}
//
// We have received a Mode A or C response. 
//
// Search through the list of known Mode-S aircraft and tag them if this Mode A/C 
// matches their known Mode S Squawks or Altitudes(+/- 50feet).
//
// A Mode S equipped aircraft may also respond to Mode A and Mode C SSR interrogations.
// We can't tell if this is a Mode A or C, so scan through the entire aircraft list
// looking for matches on Mode A (squawk) and Mode C (altitude). Flag in the Mode S
// records that we have had a potential Mode A or Mode C response from this aircraft. 
//
// If an aircraft responds to Mode A then it's highly likely to be responding to mode C 
// too, and vice verca. Therefore, once the mode S record is tagged with both a Mode A
// and a Mode C flag, we can be fairly confident that this Mode A/C frame relates to that
// Mode S aircraft.
//
// Mode C's are more likely to clash than Mode A's; There could be several aircraft 
// cruising at FL370, but it's less likely (though not impossible) that there are two 
// aircraft on the same squawk. Therefore, give precidence to Mode A record matches
//
// Note : It's theoretically possible for an aircraft to have the same value for Mode A 
// and Mode C. Therefore we have to check BOTH A AND C for EVERY S.
//  
void interactiveUpdateAircraftModeA(struct aircraft *a) {
    struct aircraft *b = Modes.aircrafts;

    while(b) {
        if ((b->modeACflags & MODEAC_MSG_FLAG) == 0) {// skip any fudged ICAO records 

            // First check for Mode-A <=> Mode-S Squawk matches
            if (a->modeA == b->modeA) { // If a 'real' Mode-S ICAO exists using this Mode-A Squawk
                b->modeAcount   = a->messages;
                a->modeACflags |= MODEAC_MSG_MODEA_HIT;
                if ( (b->modeAcount > 0) && 
                   ( (b->modeCcount > 1) 
                  || (a->modeACflags & MODEAC_MSG_MODEA_ONLY)) ) // Allow Mode-A only matches if this Mode-A is invalid Mode-C
                    {a->modeACflags |= MODEAC_MSG_MODES_HIT;}    // flag this ModeA/C probably belongs to a known Mode S                    
            } 

            // Next check for Mode-C <=> Mode-S Altitude matches
            if (  (a->modeC     == b->modeC    )     // If a 'real' Mode-S ICAO exists at this Mode-C Altitude
               || (a->modeC     == b->modeC + 1)     //          or this Mode-C - 100 ft
               || (a->modeC + 1 == b->modeC    ) ) { //          or this Mode-C + 100 ft
                b->modeCcount   = a->messages;
                a->modeACflags |= MODEAC_MSG_MODEC_HIT;
                if ( (b->modeAcount > 0) && 
                     (b->modeCcount > 1) )
                    {a->modeACflags |= (MODEAC_MSG_MODES_HIT | MODEAC_MSG_MODEC_OLD);} // flag this ModeA/C probably belongs to a known Mode S                    
            }
        }
        b = b->next;
    }
}

void interactiveUpdateAircraftModeS() {
    struct aircraft *a = Modes.aircrafts;

    while(a) {
        int flags = a->modeACflags;
        if (flags & MODEAC_MSG_FLAG) { // find any fudged ICAO records 

            // clear the current A,C and S hit bits ready for this attempt
            a->modeACflags = flags & ~(MODEAC_MSG_MODEA_HIT | MODEAC_MSG_MODEC_HIT | MODEAC_MSG_MODES_HIT);

            interactiveUpdateAircraftModeA(a);  // and attempt to match them with Mode-S
        }
        a = a->next;
    }
}

/* Always positive MOD operation, used for CPR decoding. */
int cprModFunction(int a, int b) {
    int res = a % b;
    if (res < 0) res += b;
    return res;
}

/* The NL function uses the precomputed table from 1090-WP-9-14 */
int cprNLFunction(double lat) {
    if (lat < 0) lat = -lat; /* Table is simmetric about the equator. */
    if (lat < 10.47047130) return 59;
    if (lat < 14.82817437) return 58;
    if (lat < 18.18626357) return 57;
    if (lat < 21.02939493) return 56;
    if (lat < 23.54504487) return 55;
    if (lat < 25.82924707) return 54;
    if (lat < 27.93898710) return 53;
    if (lat < 29.91135686) return 52;
    if (lat < 31.77209708) return 51;
    if (lat < 33.53993436) return 50;
    if (lat < 35.22899598) return 49;
    if (lat < 36.85025108) return 48;
    if (lat < 38.41241892) return 47;
    if (lat < 39.92256684) return 46;
    if (lat < 41.38651832) return 45;
    if (lat < 42.80914012) return 44;
    if (lat < 44.19454951) return 43;
    if (lat < 45.54626723) return 42;
    if (lat < 46.86733252) return 41;
    if (lat < 48.16039128) return 40;
    if (lat < 49.42776439) return 39;
    if (lat < 50.67150166) return 38;
    if (lat < 51.89342469) return 37;
    if (lat < 53.09516153) return 36;
    if (lat < 54.27817472) return 35;
    if (lat < 55.44378444) return 34;
    if (lat < 56.59318756) return 33;
    if (lat < 57.72747354) return 32;
    if (lat < 58.84763776) return 31;
    if (lat < 59.95459277) return 30;
    if (lat < 61.04917774) return 29;
    if (lat < 62.13216659) return 28;
    if (lat < 63.20427479) return 27;
    if (lat < 64.26616523) return 26;
    if (lat < 65.31845310) return 25;
    if (lat < 66.36171008) return 24;
    if (lat < 67.39646774) return 23;
    if (lat < 68.42322022) return 22;
    if (lat < 69.44242631) return 21;
    if (lat < 70.45451075) return 20;
    if (lat < 71.45986473) return 19;
    if (lat < 72.45884545) return 18;
    if (lat < 73.45177442) return 17;
    if (lat < 74.43893416) return 16;
    if (lat < 75.42056257) return 15;
    if (lat < 76.39684391) return 14;
    if (lat < 77.36789461) return 13;
    if (lat < 78.33374083) return 12;
    if (lat < 79.29428225) return 11;
    if (lat < 80.24923213) return 10;
    if (lat < 81.19801349) return 9;
    if (lat < 82.13956981) return 8;
    if (lat < 83.07199445) return 7;
    if (lat < 83.99173563) return 6;
    if (lat < 84.89166191) return 5;
    if (lat < 85.75541621) return 4;
    if (lat < 86.53536998) return 3;
    if (lat < 87.00000000) return 2;
    else return 1;
}

int cprNFunction(double lat, int fflag) {
    int nl = cprNLFunction(lat) - (fflag ? 1 : 0);
    if (nl < 1) nl = 1;
    return nl;
}

double cprDlonFunction(double lat, int fflag, int surface) {
    return (surface ? 90.0 : 360.0) / cprNFunction(lat, fflag);
}

/* This algorithm comes from:
 * http://www.lll.lu/~edward/edward/adsb/DecodingADSBposition.html.
 *
 * A few remarks:
 * 1) 131072 is 2^17 since CPR latitude and longitude are encoded in 17 bits.
 * 2) We assume that we always received the odd packet as last packet for
 *    simplicity. This may provide a position that is less fresh of a few
 *    seconds.
 */
void decodeCPR(struct aircraft *a, int fflag, int surface) {
    double AirDlat0 = (surface ? 90.0 : 360.0) / 60.0;
    double AirDlat1 = (surface ? 90.0 : 360.0) / 59.0;
    double lat0 = a->even_cprlat;
    double lat1 = a->odd_cprlat;
    double lon0 = a->even_cprlon;
    double lon1 = a->odd_cprlon;

    // Compute the Latitude Index "j"
    int    j     = (int) floor(((59*lat0 - 60*lat1) / 131072) + 0.5);
    double rlat0 = AirDlat0 * (cprModFunction(j,60) + lat0 / 131072);
    double rlat1 = AirDlat1 * (cprModFunction(j,59) + lat1 / 131072);

    if (rlat0 >= 270) rlat0 -= 360;
    if (rlat1 >= 270) rlat1 -= 360;

    // Check that both are in the same latitude zone, or abort.
    if (cprNLFunction(rlat0) != cprNLFunction(rlat1)) return;

    // Compute ni and the Longitude Index "m"
    if (fflag) { // Use odd packet.
        int ni = cprNFunction(rlat1,1);
        int m = (int) floor((((lon0 * (cprNLFunction(rlat1)-1)) -
                              (lon1 * cprNLFunction(rlat1))) / 131072.0) + 0.5);
        a->lon = cprDlonFunction(rlat1, 1, surface) * (cprModFunction(m, ni)+lon1/131072);
        a->lat = rlat1;
    } else {     // Use even packet.
        int ni = cprNFunction(rlat0,0);
        int m = (int) floor((((lon0 * (cprNLFunction(rlat0)-1)) -
                              (lon1 * cprNLFunction(rlat0))) / 131072) + 0.5);
        a->lon = cprDlonFunction(rlat0, 0, surface) * (cprModFunction(m, ni)+lon0/131072);
        a->lat = rlat0;
    }
    if (a->lon > 180) a->lon -= 360;

    a->sbsflags |= MODES_SBS_LAT_LONG_FRESH;
}

/* This algorithm comes from:
 * 1090-WP29-07-Draft_CPR101 (which also defines decodeCPR() )
 *
 * There is an error in this document related to CPR relative decode.
 * Should use trunc() rather than the floor() function in Eq 38 and related for deltaZI.
 * floor() returns integer less than argument
 * trunc() returns integer closer to zero than argument.
 * Note:   text of document describes trunc() functionality for deltaZI calculation
 *         but the formulae use floor().
 */
int decodeCPRrelative(struct aircraft *a, int fflag, int surface, double latr, double lonr) {
    double AirDlat;
    double AirDlon;
    double lat;
    double lon;
    double rlon, rlat;
    int j,m;

    // If not passed a lat/long, we must be using aircraft relative
    if ( (latr == 0) && (lonr == 0) ) {
        latr = a->lat;
        lonr = a->lon;
    }
    if ( (latr == 0) && (lonr == 0) )
        return (-1); // Exit with error - can't do relative if we don't have ref.

    if (fflag) { // odd
        AirDlat = (surface ? 90.0 : 360.0) / 59.0;
        lat = a->odd_cprlat;
        lon = a->odd_cprlon;
    } else {    // even
        AirDlat = (surface ? 90.0 : 360.0) / 60.0;
        lat = a->even_cprlat;
        lon = a->even_cprlon;
    }

    // Compute the Latitude Index "j"
    j = (int) (floor(latr/AirDlat) +
               trunc(0.5 + cprModFunction((int)latr, (int)AirDlat)/AirDlat - lat/131072));
    rlat = AirDlat * (j + lat/131072);
    if (rlat >= 270) rlat -= 360;

    // Check to see that answer is reasonable - ie no more than 1/2 cell away 
    if (fabs(rlat - a->lat) > (AirDlat/2)) {
        a->lat = a->lon = 0; // This will cause a quick exit next time if no global has been done
        return (-1);         // Time to give up - Latitude error 
    }

    // Compute the Longitude Index "m"
    AirDlon = cprDlonFunction(rlat, fflag, surface);
    m = (int) (floor(lonr/AirDlon) +
               trunc(0.5 + cprModFunction((int)lonr, (int)AirDlon)/AirDlon - lon/131072));
    rlon = AirDlon * (m + lon/131072);
    if (rlon > 180) rlon -= 360;

    // Check to see that answer is reasonable - ie no more than 1/2 cell away
    if (fabs(rlon - a->lon) > (AirDlon/2)) {
        a->lat = a->lon = 0; // This will cause a quick exit next time if no global has been done
        return (-1);         // Time to give up - Longitude error
    }

    a->lat = rlat;
    a->lon = rlon;
    a->sbsflags |= MODES_SBS_LAT_LONG_FRESH;

    return (0);
}

/* Receive new messages and populate the interactive mode with more info. */
struct aircraft *interactiveReceiveData(struct modesMessage *mm) {
    struct aircraft *a, *aux;

    if (Modes.check_crc && mm->crcok == 0) return NULL;

    // Loookup our aircraft or create a new one
    a = interactiveFindAircraft(mm->addr);
    if (!a) {                              // If it's a currently unknown aircraft....
        a = interactiveCreateAircraft(mm); // ., create a new record for it,
        a->next = Modes.aircrafts;         // .. and put it at the head of the list
        Modes.aircrafts = a;
    } else {
        /* If it is an already known aircraft, move it on head
         * so we keep aircrafts ordered by received message time.
         *
         * However move it on head only if at least one second elapsed
         * since the aircraft that is currently on head sent a message,
         * othewise with multiple aircrafts at the same time we have an
         * useless shuffle of positions on the screen. */
        if (0 && Modes.aircrafts != a && (time(NULL) - a->seen) >= 1) {
            aux = Modes.aircrafts;
            while(aux->next != a) aux = aux->next;
            /* Now we are a node before the aircraft to remove. */
            aux->next = aux->next->next; /* removed. */
            /* Add on head */
            a->next = Modes.aircrafts;
            Modes.aircrafts = a;
        }
    }

    a->seen = time(NULL);
    a->messages++;

    if (mm->msgtype == 0 || mm->msgtype == 4 || mm->msgtype == 20) {
        if ( (a->modeCcount)                   // if we've a modeCcount already
          && (a->altitude  != mm->altitude ) ) // and Altitude has changed
//        && (a->modeC     != mm->modeC + 1)   // and Altitude not changed by +100 feet
//        && (a->modeC + 1 != mm->modeC    ) ) // and Altitude not changes by -100 feet
            {a->modeCcount = 0;}               //....zero the hit count
        a->altitude =  mm->altitude;
        a->modeC    = (mm->altitude + 49) / 100;
    } else if(mm->msgtype == 5 || mm->msgtype == 21) {
        if (a->modeA != mm->modeA) {
            a->modeAcount = 0; // Squawk has changed, so zero the hit count
        }
        a->modeA = mm->modeA;

    } else if (mm->msgtype == 17) {
        if (mm->metype >= 1 && mm->metype <= 4) {
            memcpy(a->flight, mm->flight, sizeof(a->flight));
        } else if (mm->metype >= 9 && mm->metype <= 18) {
            if ( (a->modeCcount)                   // if we've a modeCcount already
              && (a->altitude  != mm->altitude ) ) // and Altitude has changed
//            && (a->modeC     != mm->modeC + 1)   // and Altitude not changed by +100 feet
//            && (a->modeC + 1 != mm->modeC    ) ) // and Altitude not changes by -100 feet
                {a->modeCcount = 0;}               //....zero the hit count
            a->altitude =  mm->altitude;
            a->modeC    = (mm->altitude + 49) / 100;
            if (mm->fflag) {
                a->odd_cprlat = mm->raw_latitude;
                a->odd_cprlon = mm->raw_longitude;
                a->odd_cprtime = mstime();
            } else {
                a->even_cprlat = mm->raw_latitude;
                a->even_cprlon = mm->raw_longitude;
                a->even_cprtime = mstime();
            }
            // Try relative CPR first
            if (decodeCPRrelative(a, mm->fflag, 0, 0, 0)) {
                // If it fails then try global if the two data are less than 10 seconds apart, compute
                // the position.
                if (abs((int)(a->even_cprtime - a->odd_cprtime)) <= 10000) {
                      decodeCPR(a, mm->fflag, 0);
                }
            }
        } else if (mm->metype == 19) {
            if (mm->mesub == 1 || mm->mesub == 2) {
                a->speed = mm->velocity;
                a->track = mm->heading;
            }
        }
    } else if(mm->msgtype == 32) {

        int flags = a->modeACflags;

        if ((flags & (MODEAC_MSG_MODEC_HIT | MODEAC_MSG_MODEC_OLD)) == MODEAC_MSG_MODEC_OLD) { 
            //
            // This Mode-C doesn't currently hit any known Mode-S, but it used to because MODEAC_MSG_MODEC_OLD is
            // set  So the aircraft it used to match has either changed altitude, or gone out of our receiver range
            //
            // We've now received this Mode-A/C again, so it must be a new aircraft. It could be another aircraft
            // at the same Mode-C altitude, or it could be a new airctraft with a new Mods-A squawk. 
            //
            // To avoid masking this aircraft from the interactive display, clear the MODEAC_MSG_MODES_OLD flag
            // and set messages to 1;
            //
            a->modeACflags = flags & ~MODEAC_MSG_MODEC_OLD;
            a->messages    = 1;
        }  

    }
    return a;
}

/* Show the currently captured interactive data on screen. */
void interactiveShowData(void) {
    struct aircraft *a = Modes.aircrafts;
    time_t now = time(NULL);
    int count = 0;
    char progress;
    char spinner[4] = "|/-\\";
    
    progress = spinner[time(NULL)%4];

    printf("\x1b[H\x1b[2J");    /* Clear the screen */
 
    if (Modes.interactive_rtl1090 == 0) {
        printf (
"Hex     ModeA  Flight   Alt     Speed   Lat       Lon       Track  Msgs   Seen %c\n", progress);
    } else {
        printf (
"Hex    Flight   Alt      V/S GS  TT  SSR  G*456^ Msgs    Seen %c\n", progress);
    }
    printf(
"--------------------------------------------------------------------------------\n");

    while(a && count < Modes.interactive_rows) {
        int altitude = a->altitude, speed = a->speed, msgs = a->messages;
        char squawk[5] = "    ";
        char fl[5] = "    ";
        char tt[5] = "   ";
        char gs[5] = "   ";
        char spacer = '\0';

        if ( (((a->modeACflags & (MODEAC_MSG_FLAG                             )) == 0                    )                 )
          || (((a->modeACflags & (MODEAC_MSG_MODES_HIT | MODEAC_MSG_MODEA_ONLY)) == MODEAC_MSG_MODEA_ONLY) && (msgs > 4  ) ) 
          || (((a->modeACflags & (MODEAC_MSG_MODES_HIT | MODEAC_MSG_MODEC_OLD )) == 0                    ) && (msgs > 127) ) 
           ) {

            /* Convert units to metric if --metric was specified. */
            if (Modes.metric) {
                altitude = (int) (altitude / 3.2828);
                speed    = (int) (speed * 1.852);
            }
        
            if (altitude > 99999) {
                altitude = 99999;
            } else if (altitude < -9999) {
                altitude = -9999;
            }
        
            if (a->modeA) {
                sprintf(squawk, "%04x", a->modeA);
            }
        
            if (msgs > 99999) {
                msgs = 99999;
            }
        
            if ((int)(now - a->seen) < 10) {
                spacer = ' ';
            }

            if (Modes.interactive_rtl1090 != 0) {
                if (altitude>0) {
                    altitude=altitude/100; 
                    sprintf(fl,"F%03d",altitude);
                }
                if (speed > 0) {
                    sprintf (gs,"%3d",speed);
                }
                if (a->track > 0) {
                    sprintf (tt,"%03d",a->track);
                }
                printf("%06x %-8s %-4s         %-3s %-3s %4s        %-6d  %d %c \n", 
                a->addr, a->flight, fl, gs, tt, squawk, msgs, (int)(now - a->seen), spacer);
            } else {
                printf("%06x  %-4s   %-8s %-7d %-6d %-7.03f   %-7.03f   %-3d    %-6d %d%c sec\n",
                a->addr, squawk, a->flight, altitude, speed,
                a->lat, a->lon, a->track, msgs, (int)(now - a->seen), spacer);
            }
            count++;
        }        
        a = a->next;
    }
}

/* When in interactive mode If we don't receive new nessages within
 * MODES_INTERACTIVE_TTL seconds we remove the aircraft from the list. */
void interactiveRemoveStaleAircrafts(void) {
    struct aircraft *a = Modes.aircrafts;
    struct aircraft *prev = NULL;
    time_t now = time(NULL);

    while(a) {
        if ((now - a->seen) > Modes.interactive_ttl) {
            struct aircraft *next = a->next;
            /* Remove the element from the linked list, with care
             * if we are removing the first element. */
            free(a);
            if (!prev)
                Modes.aircrafts = next;
            else
                prev->next = next;
            a = next;
        } else {
            prev = a;
            a = a->next;
        }
    }
}

/* ============================== Snip mode ================================= */

/* Get raw IQ samples and filter everything is < than the specified level
 * for more than 256 samples in order to reduce example file size. */
void snipMode(int level) {
    int i, q;
    uint64_t c = 0;

    while ((i = getchar()) != EOF && (q = getchar()) != EOF) {
        if (abs(i-127) < level && abs(q-127) < level) {
            c++;
            if (c > MODES_PREAMBLE_SIZE) continue;
        } else {
            c = 0;
        }
        putchar(i);
        putchar(q);
    }
}

/* ============================= Networking =================================
 * Note: here we risregard any kind of good coding practice in favor of
 * extreme simplicity, that is:
 *
 * 1) We only rely on the kernel buffers for our I/O without any kind of
 *    user space buffering.
 * 2) We don't register any kind of event handler, from time to time a
 *    function gets called and we accept new connections. All the rest is
 *    handled via non-blocking I/O and manually pullign clients to see if
 *    they have something new to share with us when reading is needed.
 */

/* Networking "stack" initialization. */
void modesInitNet(void) {
    struct {
        char *descr;
        int *socket;
        int port;
    } services[4] = {
        {"Raw TCP output", &Modes.ros, Modes.net_output_raw_port},
        {"Raw TCP input", &Modes.ris, Modes.net_input_raw_port},
        {"HTTP server", &Modes.https, Modes.net_http_port},
        {"Basestation TCP output", &Modes.sbsos, Modes.net_output_sbs_port}
    };
    int j;

    memset(Modes.clients,0,sizeof(Modes.clients));
    Modes.maxfd = -1;

    for (j = 0; j < 4; j++) {
        int s = anetTcpServer(Modes.aneterr, services[j].port, NULL);
        if (s == -1) {
            fprintf(stderr, "Error opening the listening port %d (%s): %s\n",
                services[j].port, services[j].descr, strerror(errno));
            exit(1);
        }
        anetNonBlock(Modes.aneterr, s);
        *services[j].socket = s;
    }

    signal(SIGPIPE, SIG_IGN);
}

/* This function gets called from time to time when the decoding thread is
 * awakened by new data arriving. This usually happens a few times every
 * second. */
void modesAcceptClients(void) {
    int fd, port;
    unsigned int j;
    struct client *c;
    int services[4];

    services[0] = Modes.ros;
    services[1] = Modes.ris;
    services[2] = Modes.https;
    services[3] = Modes.sbsos;

    for (j = 0; j < sizeof(services)/sizeof(int); j++) {
        fd = anetTcpAccept(Modes.aneterr, services[j], NULL, &port);
        if (fd == -1) continue;

        if (fd >= MODES_NET_MAX_FD) {
            close(fd);
            return; /* Max number of clients reached. */
        }

        anetNonBlock(Modes.aneterr, fd);
        c = (struct client *) malloc(sizeof(*c));
        c->service = services[j];
        c->fd = fd;
        c->buflen = 0;
        Modes.clients[fd] = c;
        anetSetSendBuffer(Modes.aneterr,fd,MODES_NET_SNDBUF_SIZE);

        if (Modes.maxfd < fd) Modes.maxfd = fd;
        if (services[j] == Modes.sbsos) Modes.stat_sbs_connections++;

        j--; /* Try again with the same listening port. */

        if (Modes.debug & MODES_DEBUG_NET)
            printf("Created new client %d\n", fd);
    }
}

/* On error free the client, collect the structure, adjust maxfd if needed. */
void modesFreeClient(int fd) {
    close(fd);
    free(Modes.clients[fd]);
    Modes.clients[fd] = NULL;

    if (Modes.debug & MODES_DEBUG_NET)
        printf("Closing client %d\n", fd);

    /* If this was our maxfd, rescan the full clients array to check what's
     * the new max. */
    if (Modes.maxfd == fd) {
        int j;

        Modes.maxfd = -1;
        for (j = 0; j < MODES_NET_MAX_FD; j++) {
            if (Modes.clients[j]) Modes.maxfd = j;
        }
    }
}

/* Send the specified message to all clients listening for a given service. */
void modesSendAllClients(int service, void *msg, int len) {
    int j;
    struct client *c;

    for (j = 0; j <= Modes.maxfd; j++) {
        c = Modes.clients[j];
        if (c && c->service == service) {
            int nwritten = write(j, msg, len);
            if (nwritten != len) {
                modesFreeClient(j);
            }
        }
    }
}

/* Write raw output in Beast Binary format with Timestamp to TCP clients */
void modesSendBeastOutput(struct modesMessage *mm) {
    char *p = &Modes.rawOut[Modes.rawOutUsed];
    int  msgLen = mm->msgbits / 8;
    char * pTimeStamp;
    int  j;

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
        *p++ = pTimeStamp[j];
    }

    *p++ = mm->signalLevel;

    memcpy(p, mm->msg, msgLen);

    Modes.rawOutUsed += (msgLen + 9);
    if (Modes.rawOutUsed >= Modes.net_output_raw_size)
      {
      modesSendAllClients(Modes.ros, Modes.rawOut, Modes.rawOutUsed);
      Modes.rawOutUsed = 0;
      Modes.net_output_raw_rate_count = 0;
      }
}

/* Write raw output to TCP clients. */
void modesSendRawOutput(struct modesMessage *mm) {
    char *p = &Modes.rawOut[Modes.rawOutUsed];
    int  msgLen = mm->msgbits / 8;
    int j;
    char * pTimeStamp;

    if (Modes.mlat) {
        *p++ = '@';
        pTimeStamp = (char *) &mm->timestampMsg;
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

/* Write SBS output to TCP clients. */
void modesSendSBSOutput(struct modesMessage *mm, struct aircraft *a) {
    char msg[256], *p = msg;
    char strCommon[128], *pCommon = strCommon;
    int emergency = 0, ground = 0, alert = 0, spi = 0;
    uint32_t offset;
    struct timeb epocTime;
    struct tm stTime;

    if (mm->msgtype == 4 || mm->msgtype == 5 || mm->msgtype == 21) {
        if (mm->modeA == 0x7500 || mm->modeA == 0x7600 ||
            mm->modeA == 0x7700) emergency = -1;
        if (mm->fs == 1 || mm->fs == 3) ground = -1;
        if (mm->fs == 2 || mm->fs == 3 || mm->fs == 4) alert = -1;
        if (mm->fs == 4 || mm->fs == 5) spi = -1;
    }

    // ICAO address of the aircraft
    pCommon += sprintf(pCommon, "111,11111,%06X,111111,", mm->addr); 

    // Make sure the records' timestamp is valid before outputing it
    if (mm->timestampMsg != (uint64_t)(-1)) {
        // Do the records' time and date now
        epocTime = Modes.stSystemTimeBlk;                         // This is the time of the start of the Block we're processing
        offset   = (int) (mm->timestampMsg - Modes.timestampBlk); // This is the time (in 12Mhz ticks) into the Block
        offset   = offset / 12000;                                // convert to milliseconds
        epocTime.millitm += offset;                               // add on the offset time to the Block start time
        if (epocTime.millitm > 999)                               // if we've caused an overflow into the next second...
            {epocTime.millitm -= 1000; epocTime.time ++;}         //    ..correct the overflow
        stTime   = *localtime(&epocTime.time);                    // convert the time to year, month  day, hours, min, sec
        pCommon += sprintf(pCommon, "%04d/%02d/%02d,", (stTime.tm_year+1900),(stTime.tm_mon+1), stTime.tm_mday); 
        pCommon += sprintf(pCommon, "%02d:%02d:%02d.%03d,", stTime.tm_hour, stTime.tm_min, stTime.tm_sec, epocTime.millitm); 
    } else {
        pCommon += sprintf(pCommon, ",,");
    }  

    // Do the current time and date now
    ftime(&epocTime);                                         // get the current system time & date
    stTime = *localtime(&epocTime.time);                      // convert the time to year, month  day, hours, min, sec
    pCommon += sprintf(pCommon, "%04d/%02d/%02d,", (stTime.tm_year+1900),(stTime.tm_mon+1), stTime.tm_mday); 
    pCommon += sprintf(pCommon, "%02d:%02d:%02d.%03d", stTime.tm_hour, stTime.tm_min, stTime.tm_sec, epocTime.millitm); 

    if (mm->msgtype == 0) {
        p += sprintf(p, "MSG,5,%s,,%d,,,,,,,,,,",               strCommon, mm->altitude);

    } else if (mm->msgtype == 4) {
        p += sprintf(p, "MSG,5,%s,,%d,,,,,,,%d,%d,%d,%d",       strCommon, mm->altitude, alert, emergency, spi, ground);

    } else if (mm->msgtype == 5) {
        p += sprintf(p, "MSG,6,%s,,,,,,,,%x,%d,%d,%d,%d",       strCommon, mm->modeA, alert, emergency, spi, ground);

    } else if (mm->msgtype == 11) {
        p += sprintf(p, "MSG,8,%s,,,,,,,,,,,,",                 strCommon);

    } else if (mm->msgtype == 17 && mm->metype == 4) {
        p += sprintf(p, "MSG,1,%s,%s,,,,,,,,0,0,0,0",           strCommon, mm->flight);

    } else if (mm->msgtype == 17 && mm->metype >= 9 && mm->metype <= 18) {
      if ( ((a->lat == 0) && (a->lon == 0)) || ((a->sbsflags & MODES_SBS_LAT_LONG_FRESH) == 0) ){
        p += sprintf(p, "MSG,3,%s,,%d,,,,,,,0,0,0,0",           strCommon, mm->altitude);
      } else {
        p += sprintf(p, "MSG,3,%s,,%d,,,%1.5f,%1.5f,,,0,0,0,0", strCommon, mm->altitude, a->lat, a->lon);
        a->sbsflags &= ~MODES_SBS_LAT_LONG_FRESH;
      }

    } else if (mm->msgtype == 17 && mm->metype == 19 && mm->mesub == 1) {
        int vr = (mm->vert_rate_sign==0?1:-1) * (mm->vert_rate-1) * 64;

        p += sprintf(p, "MSG,4,%s,,,%d,%d,,,%i,,0,0,0,0",       strCommon, mm->velocity, mm->heading, vr);

    } else if (mm->msgtype == 21) {
        p += sprintf(p, "MSG,6,%s,,,,,,,,%x,%d,%d,%d,%d",       strCommon, mm->modeA, alert, emergency, spi, ground);

    } else {
        return;
    }

    *p++ = '\r'; *p++ = '\n'; // <CRLF> or just <LF> ??
    modesSendAllClients(Modes.sbsos, msg, p-msg);
}

/* Turn an hex digit into its 4 bit decimal value.
 * Returns -1 if the digit is not in the 0-F range. */
int hexDigitVal(int c) {
    c = tolower(c);
    if (c >= '0' && c <= '9') return c-'0';
    else if (c >= 'a' && c <= 'f') return c-'a'+10;
    else return -1;
}

/* This function decodes a string representing a Mode S message in
 * raw hex format like: *8D4B969699155600E87406F5B69F;
 * The string is supposed to be at the start of the client buffer
 * and null-terminated.
 * 
 * The message is passed to the higher level layers, so it feeds
 * the selected screen output, the network output and so forth.
 * 
 * If the message looks invalid is silently discarded.
 *
 * The function always returns 0 (success) to the caller as there is
 * no case where we want broken messages here to close the client
 * connection. */
int decodeHexMessage(struct client *c) {
    char *hex = c->buf;
    int l = strlen(hex), j;
    unsigned char msg[MODES_LONG_MSG_BYTES];
    struct modesMessage mm;

    // Always mark the timestamp as invalid for packets received over the internet
    // Mixing of data from two or more different receivers and publishing
    // as coming from one would lead to corrupt mlat data
    // Non timemarked internet data has indeterminate delay
    mm.timestampMsg = -1;
    mm.signalLevel  = -1;

    // Remove spaces on the left and on the right
    while(l && isspace(hex[l-1])) {
        hex[l-1] = '\0'; l--;
    }
    while(isspace(*hex)) {
        hex++; l--;
    }

    // Turn the message into binary.
    // Accept *-AVR raw @-AVR/BEAST timeS+raw %-AVR timeS+raw (CRC good) <-BEAST timeS+sigL+raw
    // and some AVR recorer that we can understand
    if (hex[l-1] != ';') {return (0);} // not complete - abort

    switch(hex[0]) {
        case '<': {
            mm.signalLevel = (hexDigitVal(hex[13])<<4) | hexDigitVal(hex[14]);
            hex += 15; l -= 16; // Skip <, timestamp and siglevel, and ;
            break;}

        case '@':
        case '%':
        case '#':
        case '$': {
            hex += 13; l -= 14; // Skip @,%,#,$, and timestamp, and ;
            break;}

        case '*':
        case ':': {
            hex++; l-=2; // Skip * and ;
            break;}

        default: {
            return (0); // We don't know what this is, so abort
            break;}
    }

    if ( (l < 4) || (l > MODES_LONG_MSG_BYTES*2) ) return (0); // Too short or long message... broken
    for (j = 0; j < l; j += 2) {
        int high = hexDigitVal(hex[j]);
        int low  = hexDigitVal(hex[j+1]);

        if (high == -1 || low == -1) return 0;
        msg[j/2] = (high << 4) | low;
    }

    if (l < 5) {decodeModeAMessage(&mm, ((msg[0] << 8) | msg[1]));} // ModeA or ModeC
    else       {decodeModesMessage(&mm, msg);}

    useModesMessage(&mm);
    return (0);
}

/* Return a description of planes in json. */
char *aircraftsToJson(int *len) {
    struct aircraft *a = Modes.aircrafts;
    int buflen = 1024; /* The initial buffer is incremented as needed. */
    char *buf = (char *) malloc(buflen), *p = buf;
    int l;

    l = snprintf(p,buflen,"[\n");
    p += l; buflen -= l;
    while(a) {
        int altitude = a->altitude, speed = a->speed;

        /* Convert units to metric if --metric was specified. */
        if (Modes.metric) {
            altitude = (int) (altitude / 3.2828);
            speed    = (int) (speed * 1.852);
        }

        if (a->lat != 0 && a->lon != 0) {
            l = snprintf(p,buflen,
                "{\"hex\":\"%06x\", \"flight\":\"%s\", \"lat\":%f, "
                "\"lon\":%f, \"altitude\":%d, \"track\":%d, "
                "\"speed\":%d},\n",
                a->addr, a->flight, a->lat, a->lon, a->altitude, a->track,
                a->speed);
            p += l; buflen -= l;
            /* Resize if needed. */
            if (buflen < 256) {
                int used = p-buf;
                buflen += 1024; /* Our increment. */
                buf = (char *) realloc(buf,used+buflen);
                p = buf+used;
            }
        }
        a = a->next;
    }
    /* Remove the final comma if any, and closes the json array. */
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

#define MODES_CONTENT_TYPE_HTML "text/html;charset=utf-8"
#define MODES_CONTENT_TYPE_JSON "application/json;charset=utf-8"

/* Get an HTTP request header and write the response to the client.
 * Again here we assume that the socket buffer is enough without doing
 * any kind of userspace buffering.
 *
 * Returns 1 on error to signal the caller the client connection should
 * be closed. */
int handleHTTPRequest(struct client *c) {
    char hdr[512];
    int clen, hdrlen;
    int httpver, keepalive;
    char *p, *url, *content;
    char *ctype;

    if (Modes.debug & MODES_DEBUG_NET)
        printf("\nHTTP request: %s\n", c->buf);

    /* Minimally parse the request. */
    httpver = (strstr(c->buf, "HTTP/1.1") != NULL) ? 11 : 10;
    if (httpver == 10) {
        /* HTTP 1.0 defaults to close, unless otherwise specified. */
        keepalive = strstr(c->buf, "Connection: keep-alive") != NULL;
    } else if (httpver == 11) {
        /* HTTP 1.1 defaults to keep-alive, unless close is specified. */
        keepalive = strstr(c->buf, "Connection: close") == NULL;
    }

    /* Identify he URL. */
    p = strchr(c->buf,' ');
    if (!p) return 1; /* There should be the method and a space... */
    url = ++p; /* Now this should point to the requested URL. */
    p = strchr(p, ' ');
    if (!p) return 1; /* There should be a space before HTTP/... */
    *p = '\0';

    if (Modes.debug & MODES_DEBUG_NET) {
        printf("\nHTTP keep alive: %d\n", keepalive);
        printf("HTTP requested URL: %s\n\n", url);
    }

    /* Select the content to send, we have just two so far:
     * "/" -> Our google map application.
     * "/data.json" -> Our ajax request to update planes. */
    if (strstr(url, "/data.json")) {
        content = aircraftsToJson(&clen);
        ctype = MODES_CONTENT_TYPE_JSON;
    } else {
        struct stat sbuf;
        int fd = -1;

        if (stat("gmap.html",&sbuf) != -1 &&
            (fd = open("gmap.html",O_RDONLY)) != -1)
        {
            content = (char *) malloc(sbuf.st_size);
            if (read(fd,content,sbuf.st_size) == -1) {
                snprintf(content,sbuf.st_size,"Error reading from file: %s",
                    strerror(errno));
            }
            clen = sbuf.st_size;
        } else {
            char buf[128];

            clen = snprintf(buf,sizeof(buf),"Error opening HTML file: %s",
                strerror(errno));
            content = strdup(buf);
        }
        if (fd != -1) close(fd);
        ctype = MODES_CONTENT_TYPE_HTML;
    }

    /* Create the header and send the reply. */
    hdrlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Server: Dump1090\r\n"
        "Content-Type: %s\r\n"
        "Connection: %s\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        ctype,
        keepalive ? "keep-alive" : "close",
        clen);

    if (Modes.debug & MODES_DEBUG_NET)
        printf("HTTP Reply header:\n%s", hdr);

    /* Send header and content. */
    if (write(c->fd, hdr, hdrlen) == -1 ||
        write(c->fd, content, clen) == -1)
    {
        free(content);
        return 1;
    }
    free(content);
    Modes.stat_http_requests++;
    return !keepalive;
}

/* This function polls the clients using read() in order to receive new
 * messages from the net.
 *
 * The message is supposed to be separated by the next message by the
 * separator 'sep', that is a null-terminated C string.
 *
 * Every full message received is decoded and passed to the higher layers
 * calling the function 'handler'.
 *
 * The handelr returns 0 on success, or 1 to signal this function we
 * should close the connection with the client in case of non-recoverable
 * errors. */
void modesReadFromClient(struct client *c, char *sep,
                         int(*handler)(struct client *))
{
    while(1) {
        int left = MODES_CLIENT_BUF_SIZE - c->buflen;
        int nread = read(c->fd, c->buf+c->buflen, left);
        int fullmsg = 0;
        int i;
        char *p;

        if (nread <= 0) {
            if (nread == 0 || errno != EAGAIN) {
                /* Error, or end of file. */
                modesFreeClient(c->fd);
            }
            break; /* Serve next client. */
        }
        c->buflen += nread;

        /* Always null-term so we are free to use strstr() */
        c->buf[c->buflen] = '\0';

        /* If there is a complete message there must be the separator 'sep'
         * in the buffer, note that we full-scan the buffer at every read
         * for simplicity. */
        while ((p = strstr(c->buf, sep)) != NULL) {
            i = p - c->buf; /* Turn it as an index inside the buffer. */
            c->buf[i] = '\0'; /* Te handler expects null terminated strings. */
            /* Call the function to process the message. It returns 1
             * on error to signal we should close the client connection. */
            if (handler(c)) {
                modesFreeClient(c->fd);
                return;
            }
            /* Move what's left at the start of the buffer. */
            i += strlen(sep); /* The separator is part of the previous msg. */
            memmove(c->buf,c->buf+i,c->buflen-i);
            c->buflen -= i;
            c->buf[c->buflen] = '\0';
            /* Maybe there are more messages inside the buffer.
             * Start looping from the start again. */
            fullmsg = 1;
        }
        /* If our buffer is full discard it, this is some badly
         * formatted shit. */
        if (c->buflen == MODES_CLIENT_BUF_SIZE) {
            c->buflen = 0;
            /* If there is garbage, read more to discard it ASAP. */
            continue;
        }
        /* If no message was decoded process the next client, otherwise
         * read more data from the same client. */
        if (!fullmsg) break;
    }
}

/* Read data from clients. This function actually delegates a lower-level
 * function that depends on the kind of service (raw, http, ...). */
void modesReadFromClients(void) {
    int j;
    struct client *c;

    for (j = 0; j <= Modes.maxfd; j++) {
        if ((c = Modes.clients[j]) == NULL) continue;
        if (c->service == Modes.ris)
            modesReadFromClient(c,"\n",decodeHexMessage);
        else if (c->service == Modes.https)
            modesReadFromClient(c,"\r\n\r\n",handleHTTPRequest);
    }
}

/* ================================ Main ==================================== */

void showHelp(void) {
    printf(
"-----------------------------------------------------------------------------\n"
"|                        dump1090 ModeS Receiver         Ver : " MODES_DUMP1090_VERSION " |\n"
"-----------------------------------------------------------------------------\n"
"--device-index <index>   Select RTL device (default: 0)\n"
"--gain <db>              Set gain (default: max gain. Use -100 for auto-gain)\n"
"--enable-agc             Enable the Automatic Gain Control (default: off)\n"
"--freq <hz>              Set frequency (default: 1090 Mhz)\n"
"--ifile <filename>       Read data from file (use '-' for stdin)\n"
"--interactive            Interactive mode refreshing data on screen\n"
"--interactive-rows <num> Max number of rows in interactive mode (default: 15)\n"
"--interactive-ttl <sec>  Remove from list if idle for <sec> (default: 60)\n"
"--interactive-rtl1090    Display flight table in RTL1090 format\n"
"--raw                    Show only messages hex values\n"
"--net                    Enable networking\n"
"--modeac                 Enable decoding of SSR Modes 3/A & 3/C\n"
"--net-beast              TCP raw output in Beast binary format\n"
"--net-only               Enable just networking, no RTL device or file used\n"
"--net-ro-size <size>     TCP raw output minimum size (default: 0)\n"
"--net-ro-rate <rate>     TCP raw output memory flush rate (default: 0)\n"
"--net-ro-port <port>     TCP raw output listen port (default: 30002)\n"
"--net-ri-port <port>     TCP raw input listen port  (default: 30001)\n"
"--net-http-port <port>   HTTP server port (default: 8080)\n"
"--net-sbs-port <port>    TCP BaseStation output listen port (default: 30003)\n"
"--fix                    Enable single-bits error correction using CRC\n"
"--no-fix                 Disable single-bits error correction using CRC\n"
"--no-crc-check           Disable messages with broken CRC (discouraged)\n"
"--aggressive             More CPU for more messages (two bits fixes, ...)\n"
"--mlat                   display raw messages in Beast ascii mode\n"
"--stats                  With --ifile print stats at exit. No other output\n"
"--onlyaddr               Show only ICAO addresses (testing purposes)\n"
"--metric                 Use metric units (meters, km/h, ...)\n"
"--snip <level>           Strip IQ file removing samples < level\n"
"--debug <flags>          Debug mode (verbose), see README for details\n"
"--quiet                  Disable output to stdout. Use for daemon applications\n"
"--ppm <error>            Set receiver error in parts per million (default 0)\n"
"--help                   Show this help\n"
"\n"
"Debug mode flags: d = Log frames decoded with errors\n"
"                  D = Log frames decoded with zero errors\n"
"                  c = Log frames with bad CRC\n"
"                  C = Log frames with good CRC\n"
"                  p = Log frames with bad preamble\n"
"                  n = Log network debugging info\n"
"                  j = Log frames to frames.js, loadable by debug.html\n"
    );
}

/* This function is called a few times every second by main in order to
 * perform tasks we need to do continuously, like accepting new clients
 * from the net, refreshing the screen in interactive mode, and so forth. */
void backgroundTasks(void) {
    if (Modes.net) {
        modesAcceptClients();
        modesReadFromClients();
    }    

   // If Modes.aircrafts is not NULL, remove any stale aircraft
   if (Modes.aircrafts)
        {interactiveRemoveStaleAircrafts();}

    // Refresh screen when in interactive mode
    if ((Modes.interactive) && 
        ((mstime() - Modes.interactive_last_update) > MODES_INTERACTIVE_REFRESH_TIME) ) {

        // Attempt to reconsile any ModeA/C with known Mode-S
        // We can't condition on Modes.modeac because ModeA/C could be comming 
        // in from a raw input port which we can't turn off.
        interactiveUpdateAircraftModeS();

        // Now display Mode-S and any non-reconsiled Modes-A/C  
        interactiveShowData();

        Modes.interactive_last_update = mstime();    
    }
}

int main(int argc, char **argv) {
    int j;

    /* Set sane defaults. */
    modesInitConfig();

    /* Parse the command line options */
    for (j = 1; j < argc; j++) {
        int more = j+1 < argc; /* There are more arguments. */

        if (!strcmp(argv[j],"--device-index") && more) {
            Modes.dev_index = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--gain") && more) {
            Modes.gain = (int) atof(argv[++j])*10; /* Gain is in tens of DBs */
        } else if (!strcmp(argv[j],"--enable-agc")) {
            Modes.enable_agc++;
        } else if (!strcmp(argv[j],"--freq") && more) {
            Modes.freq = (int) strtoll(argv[++j],NULL,10);
        } else if (!strcmp(argv[j],"--ifile") && more) {
            Modes.filename = strdup(argv[++j]);
        } else if (!strcmp(argv[j],"--fix")) {
            Modes.fix_errors = 1;
        } else if (!strcmp(argv[j],"--no-fix")) {
            Modes.fix_errors = 0;
        } else if (!strcmp(argv[j],"--no-crc-check")) {
            Modes.check_crc = 0;
        } else if (!strcmp(argv[j],"--raw")) {
            Modes.raw = 1;
        } else if (!strcmp(argv[j],"--net")) {
            Modes.net = 1;
        } else if (!strcmp(argv[j],"--modeac")) {
            Modes.mode_ac = 1;
        } else if (!strcmp(argv[j],"--net-beast")) {
            Modes.beast = 1;
        } else if (!strcmp(argv[j],"--net-only")) {
            Modes.net = 1;
            Modes.net_only = 1;
        } else if (!strcmp(argv[j],"--net-ro-size") && more) {
            Modes.net_output_raw_size = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--net-ro-rate") && more) {
            Modes.net_output_raw_rate = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--net-ro-port") && more) {
            Modes.net_output_raw_port = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--net-ri-port") && more) {
            Modes.net_input_raw_port = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--net-http-port") && more) {
            Modes.net_http_port = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--net-sbs-port") && more) {
            Modes.net_output_sbs_port = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--onlyaddr")) {
            Modes.onlyaddr = 1;
        } else if (!strcmp(argv[j],"--metric")) {
            Modes.metric = 1;
        } else if (!strcmp(argv[j],"--aggressive")) {
            Modes.aggressive++;
        } else if (!strcmp(argv[j],"--interactive")) {
            Modes.interactive = 1;
        } else if (!strcmp(argv[j],"--interactive-rows")) {
            Modes.interactive_rows = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--interactive-ttl")) {
            Modes.interactive_ttl = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--debug") && more) {
            char *f = argv[++j];
            while(*f) {
                switch(*f) {
                case 'D': Modes.debug |= MODES_DEBUG_DEMOD; break;
                case 'd': Modes.debug |= MODES_DEBUG_DEMODERR; break;
                case 'C': Modes.debug |= MODES_DEBUG_GOODCRC; break;
                case 'c': Modes.debug |= MODES_DEBUG_BADCRC; break;
                case 'p': Modes.debug |= MODES_DEBUG_NOPREAMBLE; break;
                case 'n': Modes.debug |= MODES_DEBUG_NET; break;
                case 'j': Modes.debug |= MODES_DEBUG_JS; break;
                default:
                    fprintf(stderr, "Unknown debugging flag: %c\n", *f);
                    exit(1);
                    break;
                }
                f++;
            }
        } else if (!strcmp(argv[j],"--stats")) {
            Modes.stats = 1;
        } else if (!strcmp(argv[j],"--snip") && more) {
            snipMode(atoi(argv[++j]));
            exit(0);
        } else if (!strcmp(argv[j],"--help")) {
            showHelp();
            exit(0);
        } else if (!strcmp(argv[j],"--ppm") && more) {
            Modes.ppm_error = atoi(argv[++j]);
        } else if (!strcmp(argv[j],"--quiet")) {
            Modes.quiet = 1;
        } else if (!strcmp(argv[j],"--mlat")) {
            Modes.mlat = 1;
        } else if (!strcmp(argv[j],"--interactive-rtl1090")) {
            Modes.interactive = 1;
            Modes.interactive_rtl1090 = 1;
        } else {
            fprintf(stderr,
                "Unknown or not enough arguments for option '%s'.\n\n",
                argv[j]);
            showHelp();
            exit(1);
        }
    }

    /* Initialization */
    modesInit();
    if (Modes.net_only) {
        fprintf(stderr,"Net-only mode, no RTL device or file open.\n");
    } else if (Modes.filename == NULL) {
        modesInitRTLSDR();
    } else {
        if (Modes.filename[0] == '-' && Modes.filename[1] == '\0') {
            Modes.fd = STDIN_FILENO;
        } else if ((Modes.fd = open(Modes.filename,O_RDONLY)) == -1) {
            perror("Opening data file");
            exit(1);
        }
    }
    if (Modes.net) modesInitNet();

    /* If the user specifies --net-only, just run in order to serve network
     * clients without reading data from the RTL device. */
    while (Modes.net_only) {
        backgroundTasks();
        usleep(100000);
    }

    /* Create the thread that will read the data from the device. */
    pthread_create(&Modes.reader_thread, NULL, readerThreadEntryPoint, NULL);

    pthread_mutex_lock(&Modes.data_mutex);
    while(1) {
        if (!Modes.data_ready) {
            pthread_cond_wait(&Modes.data_cond,&Modes.data_mutex);
            continue;
        }
        computeMagnitudeVector();
        Modes.stSystemTimeBlk = Modes.stSystemTimeRTL;

        /* Signal to the other thread that we processed the available data
         * and we want more (useful for --ifile). */
        Modes.data_ready = 0;
        pthread_cond_signal(&Modes.data_cond);

        /* Process data after releasing the lock, so that the capturing
         * thread can read data while we perform computationally expensive
         * stuff * at the same time. (This should only be useful with very
         * slow processors). */
        pthread_mutex_unlock(&Modes.data_mutex);
        detectModeS(Modes.magnitude, MODES_ASYNC_BUF_SAMPLES);
        Modes.timestampBlk += (MODES_ASYNC_BUF_SAMPLES*6);
        backgroundTasks();
        pthread_mutex_lock(&Modes.data_mutex);
        if (Modes.exit) break;
    }

    /* If --ifile and --stats were given, print statistics. */
    if (Modes.stats && Modes.filename) {
        printf("%d ModeA/C detected\n",                         Modes.stat_ModeAC);
        printf("%d valid preambles\n",                          Modes.stat_valid_preamble);
        printf("%d DF-?? fields corrected for length\n",        Modes.stat_DF_Corrected);
        printf("%d demodulated again after phase correction\n", Modes.stat_out_of_phase);
        printf("%d demodulated with zero errors\n",             Modes.stat_demodulated);
        printf("%d with good crc\n",                            Modes.stat_goodcrc);
        printf("%d with bad crc\n",                             Modes.stat_badcrc);
        printf("%d errors corrected\n",                         Modes.stat_fixed);
        printf("%d single bit errors\n",                        Modes.stat_single_bit_fix);
        printf("%d two bits errors\n",                          Modes.stat_two_bits_fix);
        printf("%d total usable messages\n",                    Modes.stat_goodcrc + Modes.stat_fixed);
    }

    rtlsdr_close(Modes.dev);
    return 0;
}
