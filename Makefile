# warn about (roughly):
#  * deviations from standard
#  * global functions that are not exported
#  * empty argument list in declarations
COM_WARNS=      -Wall \
		-Wextra \
		-Wpedantic \
		-Wmissing-declarations \
		-Wdouble-promotion \
		-Wnull-dereference \
		-Wmissing-include-dirs \
		-Wswitch-default \
		-Wunused-parameter \
		-Wuninitialized \
		-Wshadow \
		-Wbad-function-cast \
		-Wcast-qual \
		-Wcast-align \
		-Wwrite-strings \
		-Wstrict-prototypes \
		-Wredundant-decls \
		-Wnested-externs \
		-Wincompatible-pointer-types
# TODO: These require some work.
#	-Wconversion \
#	-Wfloat-equal
GCC_WARNS=      -Wchkp \
		-Walloc-zero \
		-Wduplicated-branches \
		-Wduplicated-cond \
		-Wc99-c11-compat \
		-Wjump-misses-init \
		-Wlogical-op \
		-Wrestrict
CLANG_WARNS=


COM_OFLGS=      -fstrict-aliasing \
		-fstack-protector
GCC_OFLGS=      -fdelete-null-pointer-checks \
		-ftree-vrp \
		-funsafe-loop-optimizations \
		-flto-odr-type-merging
CLANG_OFLGS=


CC ?= gcc
CID=$(shell $(CC) -v 2>&1 | grep "version" | cut -d' ' -f1)

CFLAGS ?= -std=gnu99 -O2 -g
CFLAGS += $(COM_WARNS) $(COM_OFLGS)
ifeq "$(CID)" "gcc"
	CFLAGS += $(GCC_WARNS) $(GCC_OFLGS)
else
	CFLAGS += $(CLANG_WARNS) $(CLANG_OFLGS)
endif
	
CFLAGS += $(shell pkg-config --cflags librtlsdr)
LDLIBS += $(shell pkg-config --libs librtlsdr) -lpthread -lm
PROGNAME=dump1090

all: $(PROGNAME)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

$(PROGNAME): dump1090.o anet.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LDLIBS)

clean:
	@$(RM) -v *.o $(PROGNAME)
