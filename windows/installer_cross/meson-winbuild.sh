#!/bin/sh

# meson-winbuild.sh: Cross-compiling freeciv from linux to Windows using Crosser dllstack
#                    and Meson
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.
#

MESON_WINBUILD_VERSION="3.0.0-alpha"
CROSSER_FEATURE_LEVEL=1.8

if test "x$1" = x || test "x$1" = "x-h" || test "x$1" = "x--help" ; then
  echo "Usage: $0 <crosser dir>"
  exit 1
fi

if test "x$1" = "x-v" || test "x$1" = "x--version" ; then
  echo "meson-winbuild.sh version $MESON_WINBUILD_VERSION"
  exit
fi

DLLSPATH="$1"

if ! test -d "$DLLSPATH" ; then
  echo "Dllstack directory \"$DLLSPATH\" not found!" >&2
  exit 1
fi

if ! test -f "$DLLSPATH/crosser.txt" ; then
  echo "Directory \"$DLLSPATH\" does not look like crosser environment!" >&2
  exit 1
fi

VERREV="$(../../fc_version)"
if test "x$INST_CROSS_MODE" != "xrelease" ; then
  if test -d ../../.git || test -f ../../.git ; then
    VERREV="$VERREV-$(cd ../.. && git rev-parse --short HEAD)"
  fi
fi

FLVL=$(grep "FeatureLevel=" $DLLSPATH/crosser.txt | sed -e 's/FeatureLevel="//' -e 's/"//')

if test "x$FLVL" != "x$CROSSER_FEATURE_LEVEL" ; then
  echo "Crosser feature level \"$FLVL\", required \"$CROSSER_FEATURE_LEVEL\"!"
  exit 1
fi

CSET=$(grep "Set=" $DLLSPATH/crosser.txt | sed -e 's/Set="//' -e 's/"//')

if test "x$CSET" != "xcurrent" ; then
  echo "Crosser set is \"$CSET\", only \"current\" is supported!"
  exit 1
fi

SETUP=$(grep "Setup=" $DLLSPATH/crosser.txt | sed -e 's/Setup="//' -e 's/"//')

if ! rm -Rf meson-build-${SETUP} ; then
  echo "Failed to clear out old build directory!" >&2
  exit 1
fi

if ! mkdir -p meson-build-${SETUP} ; then
  echo "Can't create build directory \"meson-build-${SETUP}\"!" >&2
  exit 1
fi

if ! sed "s,<PREFIX>,$DLLSPATH,g" meson/cross-${SETUP}.tmpl > meson-build-${SETUP}/cross.txt
then
  echo "Failed to create cross-file for $SETUP build!" >&2
  exit 1
fi

echo "----------------------------------"
echo "Building for $SETUP"
echo "Freeciv version $VERREV"
echo "----------------------------------"

if ! (
cd meson-build-${SETUP}

if ! meson --cross-file=cross.txt ../../.. ; then
  echo "Meson run failed!" >&2
  exit 1
fi

if ! ninja ; then
  echo "Ninja run failed!" >&2
  exit 1
fi

) then
  exit 1
fi
