#!/bin/bash
#
# csv2gpx.sh
#
# copyright 2013, Oliver Goldenstein, DL6KBG
#
gpsbabel -t -i unicsv -f adsb_coverage.csv -o gpx -F adsb_coverage.gpx
