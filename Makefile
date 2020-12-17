CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lpthread -lm
DEFINES=-DP_FILE_GMAP='"'$(shell pwd)'/gmap.html"'
CC?=gcc
PROGNAME=dump1090

all: dump1090

%.o: %.c
	$(CC) $(CFLAGS) $(DEFINES) -c $<

dump1090: dump1090.o anet.o
	$(CC) -g -o dump1090 dump1090.o anet.o $(LDFLAGS) $(LDLIBS) $(DEFINES)

clean:
	rm -f *.o dump1090

install: dump1090
	cp ./dump1090 /usr/bin/

uninstall:
	rm -f /usr/bin/dump1090

