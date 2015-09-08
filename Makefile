CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lpthread -lm -lpaho-mqtt3c
CC?=gcc
PROGNAME=dump1090

all: dump1090

%.o: %.c
	$(CC) $(CFLAGS) -c $<

dump1090: dump1090.o anet.o mqtt.o
	$(CC) -g -o dump1090 dump1090.o anet.o mqtt.o $(LDFLAGS) $(LDLIBS)

clean:
	rm -f *.o dump1090

release:
	$(CC) -o dump1090 dump1090.o anet.o mqtt.o $(LDFLAGS) $(LDLIBS)

