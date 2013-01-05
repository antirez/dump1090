LIBUSB_INC_PATH=/usr/local/Cellar/libusb/1.0.9/include/libusb-1.0
LIBUSB_LIB_PATH=/usr/local/Cellar/libusb/1.0.9/lib
LIBRTLSDR_INC_PATH=/usr/local/Cellar/rtlsdr/HEAD/include
LIBRTLSDR_LIB_PATH=/usr/local/Cellar/rtlsdr/HEAD/lib
LIBS=-lusb-1.0 -lrtlsdr -lpthread -lm
CC=gcc
PROGNAME=mode1090

all: mode1090

mode1090.o: mode1090.c
	$(CC) -O2 -g -Wall -W -I$(LIBUSB_INC_PATH) -I$(LIBRTLSDR_INC_PATH) mode1090.c -c -o mode1090.o

mode1090: mode1090.o
	$(CC) -g -L$(LIBUSB_LIB_PATH) -L$(LIBRTLSDR_LIB_PATH) -o mode1090 mode1090.o $(LIBS)

clean:
	rm -f *.o mode1090
