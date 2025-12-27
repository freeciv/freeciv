#!/bin/sh

files=$(find $1 -name "*.ruleset" \
             -o -name "*.tilespec" \
             -o -name "*.spec" \
             -o -name "*.lua" \
             -o -name "*.modpack" \
             -o -name "*.serv" \
             -o -name "*.txt" \
             -o -name "README*" \
             -o -name "*.xml" | sort)

echo "# Check for trailing spaces:"
for file in $files; do
  COUNT=$(cat $file | grep "[ 	]$" | wc -l)
  if [ "$COUNT" != "0" ]; then
    echo $file
  fi
done
echo
