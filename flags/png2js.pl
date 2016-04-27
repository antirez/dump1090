#!/usr/bin/perl

# Purpose: Generate javscript with Base64 encoded images of flags.
# References:
#    https://github.com/antirez/dump1090/pull/101
#    http://stackoverflow.com/questions/11301854/perl-convert-image-to-base64
#

use strict;
use MIME::Base64;

# Thanks simul, see http://www.perlmonks.org/?node_id=305753
sub common_prefix {
   my $comm = shift @_;
   while ($_ = shift @_) {
      $_ = substr($_,0,length($comm)) if (length($_) > length($comm));
      $comm = substr($comm,0,length($_)) if (length($_) < length($comm));
      if (( $_ ^ $comm ) =~ /^(\0*)/) {
         $comm = substr($comm,0,length($1));
      } else {
         return undef;
      }
   }
   return $comm;
}

my %encodings; # Hash of code to encoded value
my @pngs = glob("*.png");
foreach (@pngs) {
   my $curr_png = $_;
   my $no_ext = $curr_png;
   $no_ext =~ s/\.png$//;
   if (open (PNG, "<".$curr_png)) {
      binmode PNG;
      my $encoded = encode_base64(join("", <PNG>));
      $encoded =~ s/\n//g;
      $encodings{uc($no_ext)} = $encoded;
      close PNG;
   } else {
      print "ERROR: Failed to open ".$curr_png.".\n";
   }
}
my $prefix = ""; # String common to all encoded values 
foreach (sort(keys(%encodings))) {
   my $curr_key = $_;
   if ($prefix eq "") {
      $prefix = $encodings{$curr_key};
   } else {
      $prefix = common_prefix($prefix, $encodings{$curr_key});
   }
}
open JS, ">../flags.js" || die("Couldn't open ../flags.js for writing.");

print JS "function getFlagIcon(reg) {
   var img = ' <img src=\\\"data:image/png;base64,".$prefix."';\n";
print JS "   switch (reg) {\n";
foreach (sort(keys(%encodings))) {
   my $curr_key = $_;
   print JS "      case \"".$curr_key."\": img += '".
            (substr $encodings{$curr_key},length($prefix))."'; break;\n";
}
print JS "      default: return(\"\");
   }
   img += '\\\" />';
   return (img);
}
";
close JS;
