CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr json-c)
LDLIBS+=$(shell pkg-config --libs librtlsdr json-c ) -lpthread -lm -lcurl
CC?=gcc
PROGNAME=dump1090

all: dump1090

%.o: %.c
	$(CC) $(CFLAGS) -c $<

dump1090: dump1090.o anet.o rest.o
	$(CC) -g -o dump1090 dump1090.o anet.o rest.o $(LDFLAGS) $(LDLIBS)

clean:
	rm -f *.o dump1090

release:
	$(CC) -o dump1090 dump1090.o anet.o rest.o $(LDFLAGS) $(LDLIBS)

