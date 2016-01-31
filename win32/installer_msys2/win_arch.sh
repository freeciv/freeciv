#!/bin/sh

if ! test -f ../../bootstrap/config.guess ; then
    echo "win"
fi

case $(../../bootstrap/config.guess 2>/dev/null) in
    *i686*) echo "win32" ;;
    *x86_64*) echo "win64" ;;
    *) echo "win" ;;
esac
