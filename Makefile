CFLAGS=-O2 -g -Wall -W `pkg-config --cflags librtlsdr` `mysql_config --cflags --libs`
LIBS=`pkg-config --libs librtlsdr` `mysql_config --libs` -lpthread -lm

CC=gcc
PROGNAME=dump1090

all: dump1090

%.o: %.c
	$(CC) $(CFLAGS) -c $<

dump1090: dump1090.o anet.o
	$(CC) -g -o dump1090 dump1090.o anet.o $(LIBS)

clean:
	rm -f *.o dump1090

