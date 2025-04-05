#!/bin/sh

# Freeciv - Copyright (C) 2007-2025 - The Freeciv Project

# This script generates fc_gitrev_gen.h from fc_gitrev_gen.h.tmpl.
# See fc_gitrev_gen.h.tmpl for details.

# Parameters - $1 - top srcdir
#              $2 - output file
#

# Absolute paths
SRCROOT="$(cd "$1" ; pwd)"
INPUTDIR="$(cd "$1/bootstrap" ; pwd)"
OUTPUTFILE="$2"

REVSTATE="OFF"
REV1=""
REV2="dist"

(cd "$INPUTDIR"
 # Check that all commands required by this script are available
 # If not, we will not claim to know which git revision this is
 # (REVSTATE will be OFF)
 if command -v git >/dev/null ; then
   REVTMP="$(git rev-parse --short HEAD 2>/dev/null)"
   if test "$REVTMP" != "" ; then
     # This is a git repository. Check for local modifications
     if (cd "$SRCROOT" ; git diff --quiet); then
       REVSTATE=ON
       REV2="$REVTMP"
     else
       REVSTATE=MOD
       REV1="modified "
       REV2="$REVTMP"
     fi
   fi
 fi

 sed -e "s,<GITREV1>,$REV1," -e "s,<GITREV2>,$REV2," -e "s,<GITREVSTATE>,$REVSTATE," fc_gitrev_gen.h.tmpl) > "${OUTPUTFILE}.tmp"

if ! test -f "${OUTPUTFILE}" ||
   ! cmp "${OUTPUTFILE}" "${OUTPUTFILE}.tmp" >/dev/null
then
  mv "${OUTPUTFILE}.tmp" "${OUTPUTFILE}"
fi
rm -f "${OUTPUTFILE}.tmp"
