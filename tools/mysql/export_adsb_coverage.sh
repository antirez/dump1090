mysql -uroot -proot dump1090 -B -e "select \`alt\`,\`lat\`,\`lon\`,\`last_update\`  from \`flights\`;" | sed 's/\t/,/g;s/^//;s/$//;s/\n//g' > adsb_coverage.csv 

