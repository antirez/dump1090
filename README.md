Dump1090 README
===

Dump 1090 is a Mode S decoder specifically designed for RTLSDR devices.

The main features are:

* Robust decoding of weak messages, with mode1090 many users observed
  improved range compared to other popular decoders.
* Network support: TCP30003 stream (MSG5...), Raw packets, HTTP.
* Embedded HTTP server that displays the currently detected aircrafts on
  Google Map.
* Single bit errors correction using the 24 bit CRC.
* Ability to decode DF11, DF17 messages.
* Ability to decode DF formats like DF0, DF4, DF5, DF16, DF20 and DF21
  where the checksum is xored with the ICAO address by brute forcing the
  checksum field using recently seen ICAO addresses.
* Decode raw IQ samples from file (using --ifile command line switch).
* Interactive command-line-interfae mode where aircrafts currently detected
  are shown as a list refreshing as more data arrives.
* CPR coordinates decoding and track calculation from velocity.
* TCP server streaming and recceiving raw data to/from connected clients
  (using --net).

Installation
---

Type "make".

Normal usage
---

To capture traffic directly from your RTL device and show the captured traffic
on standard output, just run the program without options at all:

    ./dump1090

To just output hexadecimal messages:

    ./dump1090 --raw

To run the program in interactive mode:

    ./dump1090 --interactive

To run the program in interactive mode, with networking support, and connect
with your browser to http://localhost:8080 to see live traffic:

    ./dump1090 --interactive --net

In iteractive mode it is possible to have a less information dense but more
"arcade style" output, where the screen is refreshed every second displaying
all the recently seen aircrafts with some additional information such as
altitude and flight number, extracted from the received Mode S packets.

Using files as source of data
---

To decode data from file, use:

    ./dump1090 --ifile /path/to/binfile

The binary file should be created using `rtl_sdr` like this (or with any other
program that is able to output 8-bit unsigned IQ samples at 2Mhz sample rate).

    rtl_sdr -f 1090000000 -s 2000000 -g 50 output.bin

In the example `rtl_sdr` a gain of 50 is used, simply you should use the highest
gain availabe for your tuner. This is not needed when calling Dump1090 itself
as it is able to select the highest gain supported automatically.

It is possible to feed the program with data via standard input using
the --ifile option with "-" as argument.

Additional options
---

Dump1090 can be called with other command line options to set a different
gain, frequency, and so forth. For a list of options use:

    ./dump1090 --help

Everything is not documented here should be obvious, and for most users calling
it without arguments at all is the best thing to do.

Reliability
---

By default Dump1090 checks for decoding errors using the 24-bit CRC checksum,
where available. Messages with errors are discarded.

The --fix command line switch enables fixing single bit error correction
based on the CRC checksum. Technically, it uses a table of precomputed
checksum differences resulting from single bit errors to look up the
wrong bit position.

This is indeed able to fix errors and works reliably in my experience,
however if you are interested in very reliable data I suggest to use
the --no-fix command line switch in order to disable error fixing.

Performances and sensibility of detection
---

In my limited experience Dump1090 was able to decode a big number of messages
even in conditions where I encountered problems using other programs, however
no formal test was performed so I can't really claim that this program is
better or worse compared to other similar programs.

If you can capture traffic that Dump1090 is not able to decode properly, drop
me an email with a download link. I may try to improve the detection during
my free time (this is just an hobby project).

Network server features
---

By enabling the networking support with --net Dump1090 starts listening
for clients connections on port 30002 and 30001 (you can change both the
ports if you want, see --help output).

Port 30002
---

Connected clients are served with data ASAP as they arrive from the device
(or from file if --ifile is used) in the raw format similar to the following:

    *8D451E8B99019699C00B0A81F36E;

Every entry is separated by a simple newline (LF character, hex 0x0A).

Port 30001
---

Port 30001 is the raw input port, and can be used to feed Dump1090 with
data in the same format as specified above, with hex messages starting with
a `*` and ending with a `;` character.

So for instance if there is another remote Dump1090 instance collecting data
it is possible to sum the output to a local Dump1090 instance doing something
like this:

    nc remote-dump1090.example.net 30002 | nc localhost 30001

It is important to note that what is received via port 30001 is also
broadcasted to clients listening to port 30002.

In general everything received from port 30001 is handled exactly like the
normal traffic from RTL devices or from file when --ifile is used.

It is possible to use Dump1090 just as an hub using --ifile with /dev/zero
as argument as in the following example:

    ./dump1090 --net-only

Or alternatively to see what's happening on the screen:

    ./dump1090 --net-only --interactive

Then you can feed it from different data sources from the internet.

Port 30003
---

Connected clients are served with messages in SBS1 (BaseStation) format,
similar to:

    MSG,4,,,738065,,,,,,,,420,179,,,0,,0,0,0,0
    MSG,3,,,738065,,,,,,,35000,,,34.81609,34.07810,,,0,0,0,0

This can be used to feed data to various sharing sites without the need to use another decoder.

Antenna
---

Mode S messages are transmitted in the 1090 Mhz frequency. If you have a decent
antenna you'll be able to pick up signals from aircrafts pretty far from your
position, especially if you are outdoor and in a position with a good sky view.

You can easily build a very cheap antenna following the istructions at:

    http://antirez.com/news/46

With this trivial antenna I was able to pick up signals of aircrafts 200+ Km
away from me.

If you are interested in a more serious antenna check the following
resources:

* http://gnuradio.org/redmine/attachments/download/246/06-foster-adsb.pdf
* http://www.lll.lu/~edward/edward/adsb/antenna/ADSBantenna.html
* http://modesbeast.com/pix/adsb-ant-drawing.gif

Aggressive mode
---

With --aggressive it is possible to activate the *aggressive mode* that is a
modified version of the Mode S packet detection and decoding.
The aggresive mode uses more CPU usually (especially if there are many planes
sending DF17 packets), but can detect a few more messages.

The algorithm in aggressive mode is modified in the following ways:

* Up to two demodulation errors are tolerated (adjacent entires in the
  magnitude vector with the same eight). Normally only messages without
  errors are checked.
* It tries to fix DF17 messages with CRC errors resulting from any two bit
  errors.

The use of aggressive mdoe is only advised in places where there is
low traffic in order to have a chance to capture some more messages.

Debug mode
---

The Debug mode is a visual help to improve the detection algorithm or to
understand why the program is not working for a given input.

In this mode messages are displayed in an ASCII-art style graphical
representation, where the individial magnitude bars sampled at 2Mhz are
displayed.

An index shows the sample number, where 0 is the sample where the first
Mode S peak was found. Some additional background noise is also added
before the first peak to provide some context.

To enable debug mode and check what combinations of packets you can
log, use `mode1090 --help` to obtain a list of available debug flags.

Debug mode includes an optional javascript output that is used to visualize
packets using a web browser, you can use the file debug.html under the
'tools' directory to load the generated frames.js file.

How this program works?
---

The code is very documented and written in order to be easy to understand.
For the diligent programmer with a Mode S specification on his hands it
should be trivial to understand how it works.

The algorithms I used were obtained basically looking at many messages
as displayed using a trow-away SDL program, and trying to model the algorithm
based on how the messages look graphically.

How to test the program?
---

If you have an RTLSDR device and you happen to be in an area where there
are aircrafts flying over your head, just run the program and check for signals.

However if you don't have an RTLSDR device, or if in your area the presence
of aircrafts is very limited, you may want to try the sample file distributed
with the Dump1090 distribution under the "testfiles" directory.

Just run it like this:

    ./dump1090 --ifile testfiles/modes1.bin

What is --strip mode?
---

It is just a simple filter that will get raw IQ 8 bit samples in input
and will output a file missing all the parts of the file where I and Q
are lower than the specified <level> for more than 32 samples.

Use it like this:

    cat big.bin | ./dump1090 --snip 25 > small.bin

I used it in order to create a small test file to include inside this
program source code distribution.

Contributing
---

Dump1090 was written during some free time during xmas 2012, it is an hobby
project so I'll be able to address issues and improve it only during
free time, however you are incouraged to send pull requests in order to
improve the program. A good starting point can be the TODO list included in
the source distribution.

Credits
---

Dump1090 was written by Salvatore Sanfilippo <antirez@gmail.com> and is
released under the BSD three clause license.
