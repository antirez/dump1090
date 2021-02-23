CFLAGS?= -O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)  -I/opt/intel/system_studio_2019/opencl/SDK/include
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lpthread -lm
CC?=g++
PROGNAME=dump1090
OPENCL_FLAGS=
all: dump1090

%.o: %.c
	$(CC) $(CFLAGS) ${OPENCL_FLAGS} -c $<

dump1090: dump1090.o anet.o
	$(CC) -g -o dump1090 dump1090.o anet.o $(LDFLAGS) $(LDLIBS) ${OPENCL_FLAGS}

clean:
	rm -f *.o dump1090

install: dump1090
	cp ./dump1090 /usr/bin/
	cp ./gmap.html /srv/

uninstall:
	rm -f /usr/bin/dump1090
	rm -f /srv/gmap.html

