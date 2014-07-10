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
#ifndef __PPUP1090_H
#define __PPUP1090_H

// ============================= Include files ==========================

#include "dump1090.h"

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
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <signal.h>
    #include <fcntl.h>
    #include <ctype.h>
    #include <sys/stat.h>
    #include "rtl-sdr.h"
    #include "anet.h"
    #include <netdb.h>
#else
    #include "winstubs.h" //Put everything Windows specific in here
#endif

// ============================= #defines ===============================

#define PPUP1090_NET_OUTPUT_IP_ADDRESS "127.0.0.1"

#define NOTUSED(V) ((void) V)

#define STR_HELPER(x)         #x
#define STR(x)                STR_HELPER(x)

// ======================== structure declarations ========================

// Program global state
struct {                           // Internal state
    int      quiet;
    // Networking
    uint32_t net_pp_ipaddr;              // IPv4 address of PP instance
    char     net_input_beast_ipaddr[32]; // IPv4 address or network name of server/RPi
}  ppup1090;


// COAA Initialisation structure
struct _coaa1090 {
    uint32_t ppIPAddr;
    double   fUserLat;
    double   fUserLon;
    char     strAuthCode[16];
    char     strRegNo[16];
    char     strVersion[16];
}  coaa1090;

// ======================== function declarations =========================

#ifdef __cplusplus
extern "C" {
#endif

//
// Functions exported from coaa1090.c
//
int  openCOAA  (void);
int  closeCOAA (void);
int  initCOAA  (struct _coaa1090 coaa1090);
void postCOAA  (void);

#ifdef __cplusplus
}
#endif

#endif // __PPUP1090_H
