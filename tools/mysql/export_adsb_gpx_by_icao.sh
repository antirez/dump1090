#!/bin/bash
#
# export_adsb_gpx_by_icao.sh
#
# copyright 2013, Oliver Goldenstein, DL6KBG
#
mysql -upi -praspberry dump1090 -B -e "select \`alt\`,\`lat\`,\`lon\`,\`last_update\`  from \`tracks\` where \`icao\`='3C5EE8';" | sed 's/\t/,/g;s/^//;s/$//;s/\n//g' > 3C5EE8.csv 
gpsbabel -t -i unicsv -f 3C5EE8.csv -o gpx -F 3C5EE8.gpx
rm 3C5EE8.csv
