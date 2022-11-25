CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr libhackrf libairspy soxr)
LDLIBS+=$(shell pkg-config --libs librtlsdr libhackrf libairspy soxr) -lpthread -lm

ifeq ($(NoSDRplay),1)
CFLAGS+= -DNoSDRplay
else
LDLIBS+= -lmirsdrapi-rsp
endif

CC?=gcc
PROGNAME=dump1090

all: $(PROGNAME)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

$(PROGNAME): $(PROGNAME).o anet.o
	$(CC) -g -o $@ $^ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f *.o $(PROGNAME)
