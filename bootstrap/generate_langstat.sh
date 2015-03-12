#!/bin/bash

rm -f "$2/bootstrap/langstat.txt"

(
    cd "$1/translations"
    ./stats.sh release | (
        while read CODE PRCT ; do
            NLANG=$(grep "^$CODE " ../bootstrap/langnames.txt 2>/dev/null | sed "s/$CODE //")
            echo "$CODE $PRCT $NLANG"
        done )
) >> "$2/bootstrap/langstat.txt"
