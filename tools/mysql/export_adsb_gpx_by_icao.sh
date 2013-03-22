#!/bin/bash
#
# export_adsb_gpx_by_icao.sh
#
# copyright 2013, Oliver Goldenstein, DL6KBG
#
mysql -upi -praspberry dump1090 -B -e "select \`alt\`,\`lat\`,\`lon\`,\`last_update\`  from \`tracks\` where \`icao\`='3C5EE8';" | sed 's/\t/,/g;s/^//;s/$//;s/\n//g' > 48434F.csv 
gpsbabel -t -i unicsv -f 48434F.csv -o gpx -F 48434F.gpx
rm 48434F.csv
