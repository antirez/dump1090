#!/bin/bash
#
# delete database
#
# copyright 2013, Oliver Goldenstein, DL6KBG
#
mysql -upi -praspberry -B -e "drop database \`dump1090\`;" 
