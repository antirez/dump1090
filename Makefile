CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lpthread -lm
CC?=gcc
PROGNAME=dump1090

all: dump1090

gmap.c: gmap.html
	xxd -i $^ > $@
	sed -i "s/^unsigned/const unsigned/" $@

%.o: %.c
	$(CC) $(CFLAGS) -c $<

dump1090: dump1090.o anet.o gmap.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f *.o dump1090
