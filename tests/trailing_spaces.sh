#!/bin/sh

files=$(find $1 -name "*.ruleset" \
             -o -name "*.tileset" \
             -o -name "*.spec" \
             -o -name "README*" | sort)

echo "# Check for trailing spaces:"
for file in $files; do
  COUNT=$(cat $file | grep "[ 	]$" | wc -l)
  if [ "$COUNT" != "0" ]; then
    echo $file
  fi
done
echo
