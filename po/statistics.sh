#!/bin/sh
for i in *.po;do echo $i >> /tmp/freeciv_language_stat; msgfmt -c -v --stat $i >> /tmp/freeciv_language_stat 2>&1 ;done
vim -c '%s,o\n,o ' -c '%s,\(.\+\).po \(.\+$\),\2 \1.po' -c 'wq' /tmp/freeciv_language_stat
sort -rn < /tmp/freeciv_language_stat
rm /tmp/freeciv_language_stat
