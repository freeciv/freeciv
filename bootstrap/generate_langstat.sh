#!/bin/bash

"$1/translations/stats.sh" release | (
    while read CODE PRCT ; do
        NLANG=$(grep "^$CODE " "$1/bootstrap/langnames.txt" 2>/dev/null | sed "s/$CODE //")
        echo "$CODE $PRCT $NLANG"
    done ) > "$2/bootstrap/langstat.txt.tmp"

if ! test -f "$1/bootstrap/langstat.txt" ||
   ! cmp "$1/bootstrap/langstat.txt" "$2/bootstrap/langstat.txt.tmp" ; then
    mv "$2/bootstrap/langstat.txt.tmp" "$1/bootstrap/langstat.txt"
else
    rm -f "$2/bootstrap/langstat.txt.tmp"
fi
