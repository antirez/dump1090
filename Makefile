CFLAGS?=-O2 -g -Wall -W $(shell pkg-config --cflags librtlsdr)
LDLIBS+=$(shell pkg-config --libs librtlsdr) -lpthread -lm
CC?=gcc
PROGNAME=dump1090

OBJDIR ?= .
VPATH := .

OBJS = $(OBJDIR)/dump1090.o $(OBJDIR)/anet.o

all: $(PROGNAME)

$(OBJDIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(PROGNAME): $(OBJS)
	$(CC) -g -o $(OBJDIR)/$(PROGNAME) $(OBJS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(OBJDIR)/*.o $(OBJDIR)/$(PROGNAME)
