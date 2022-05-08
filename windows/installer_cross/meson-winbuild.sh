#!/bin/sh

# meson-winbuild.sh: Cross-compiling freeciv from linux to Windows using Crosser dllstack
#                    and Meson
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.
#

MESON_WINBUILD_VERSION="3.0.94-alpha"
CROSSER_FEATURE_LEVEL=2.3

if test "x$1" = x || test "x$1" = "x-h" || test "x$1" = "x--help" ; then
  echo "Usage: $0 <crosser dir> <gui>"
  exit 1
fi

if test "x$1" = "x-v" || test "x$1" = "x--version" ; then
  echo "meson-winbuild.sh version $MESON_WINBUILD_VERSION"
  exit
fi

GUI="$2"

if test "$GUI" != "gtk3.22" && test "$GUI" != "sdl2" &&
   test "$GUI" != "qt5" ; then
  echo "Unknown gui \"$2\"" >&2
  exit 1
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

FLVL=$(grep "CrosserFeatureLevel=" $DLLSPATH/crosser.txt | sed -e 's/CrosserFeatureLevel="//' -e 's/"//')

if test "x$FLVL" != "x$CROSSER_FEATURE_LEVEL" ; then
  echo "Crosser feature level \"$FLVL\", required \"$CROSSER_FEATURE_LEVEL\"!"
  exit 1
fi

CSET=$(grep "CrosserSet=" $DLLSPATH/crosser.txt | sed -e 's/CrosserSet="//' -e 's/"//')

if test "x$CSET" != "xcurrent" ; then
  echo "Crosser set is \"$CSET\", only \"current\" is supported!"
  exit 1
fi

SETUP=$(grep "CrosserSetup=" $DLLSPATH/crosser.txt | sed -e 's/CrosserSetup="//' -e 's/"//')

QTPARAMS=""

case $GUI in
  gtk3.22) FCMP="gtk3"
           RULEDIT=false ;;
  sdl2) FCMP="gtk4"
        RULEDIT=false ;;
  qt5) CLIENT="qt"
       FCMP="qt"
       NLS="-Dnls=false"
       RULEDIT=true
       QTPARAMS="-Dqtver=qt5" ;;
esac

if test "x$CLIENT" = "x" ; then
  CLIENT="$GUI"
fi

if ! rm -Rf meson-build-${SETUP}-${GUI} ; then
  echo "Failed to clear out old build directory!" >&2
  exit 1
fi

if ! mkdir -p meson-build-${SETUP}-${GUI} ; then
  echo "Can't create build directory \"meson-build-${SETUP}-${GUI}\"!" >&2
  exit 1
fi

if ! sed "s,<PREFIX>,$DLLSPATH,g" meson/cross-${SETUP}.tmpl > meson-build-${SETUP}-${GUI}/cross.txt
then
  echo "Failed to create cross-file for $SETUP build!" >&2
  exit 1
fi

PACKAGENAME=freeciv-${VERREV}-${SETUP}-${GUI}
MESON_INSTALL_DIR=$(pwd)/meson-install/${PACKAGENAME}

if ! rm -Rf $MESON_INSTALL_DIR ; then
  echo "Failed to clear out old install directory!" >&2
  exit 1
fi

echo "----------------------------------"
echo "Building for $SETUP"
echo "Freeciv version $VERREV"
echo "----------------------------------"

if ! (
cd meson-build-${SETUP}-${GUI}

export PKG_CONFIG_PATH=${DLLSPATH}/lib/pkgconfig

if ! meson --cross-file=cross.txt -Dprefix=$MESON_INSTALL_DIR -Dclients=$CLIENT -Dfcmp=$FCMP \
           ${NLS} -Dsyslua=false -Druledit=$RULEDIT $QTPARAMS \
           ../../.. $EXTRA_CONFIG ; then
  echo "Meson run failed!" >&2
  exit 1
fi

if ! ninja ; then
  echo "Ninja build failed!" >&2
  exit 1
fi

if ! ninja install; then
  echo "Ninja install failed!" >&2
  exit 1
fi

if ! cp fc_config.h ${MESON_INSTALL_DIR}/share/freeciv/ ; then
  echo "Storing fc_config.h failed" >&2
  exit 1
fi

if ! cp ${DLLSPATH}/crosser.txt ${MESON_INSTALL_DIR}/share/freeciv/ ; then
  echo "Storing crosser.txt failed" >&2
  exit 1
fi

if ! cp ${DLLSPATH}/ComponentVersions.txt ${MESON_INSTALL_DIR}/share/freeciv/CrosserComponents.txt
then
  echo "Storing CrosserComponents.txt failed" >&2
  exit 1
fi

) then
  exit 1
fi

if ! mkdir -p Output-meson ; then
  echo "Creating Output-meson directory failed" >&2
  exit 1
fi

( cd meson-install

  if ! 7z a -r ../Output-meson/${PACKAGENAME}.7z ${PACKAGENAME}
  then
    echo "7z failed" >&2
    exit 1
  fi
)
