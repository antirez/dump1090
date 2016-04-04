#!/usr/bin/perl

# Purpose: Generate javscript with Base64 encoded images of flags.
# References:
#    https://github.com/antirez/dump1090/pull/101
#    http://stackoverflow.com/questions/11301854/perl-convert-image-to-base64
#

use strict;
use MIME::Base64;

my @pngs = glob("*.png");
foreach (@pngs) {
   my $curr_png = $_;
   my $no_ext = $curr_png;
   $no_ext =~ s/\.png$//;
   print "            case \"".uc($no_ext)."\": img += '";
   if (open (PNG, "<".$curr_png)) {
      binmode PNG;
      my $encoded = encode_base64(join("", <PNG>));
      $encoded =~ s/\n//g;
      print $encoded."'; break;\n";
   } else {
      print "ERROR: Failed to open ".$curr_png.".\n";
   }
}
