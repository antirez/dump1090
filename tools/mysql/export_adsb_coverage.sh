#!/bin/bash
#
# export_adsb_coverage.sh 
#
# copyright 2013, Oliver Goldenstein, DL6KBG
#
mysql -upi -praspberry dump1090 -B -e "select \`alt\`,\`lat\`,\`lon\`,\`last_update\`  from \`tracks\`;" | sed 's/\t/,/g;s/^//;s/$//;s/\n//g' > adsb_coverage.csv 

