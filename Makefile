CFLAGS=-O2 -g -Wall -W `pkg-config --cflags librtlsdr`
LIBS=`pkg-config --libs librtlsdr` -lpthread -lm
CC=gcc
PROGNAME=mode1090

all: mode1090

mode1090.o: mode1090.c
	$(CC) $(CFLAGS) mode1090.c -c -o mode1090.o

mode1090: mode1090.o
	$(CC) -g -o mode1090 mode1090.o $(LIBS)

clean:
	rm -f *.o mode1090
