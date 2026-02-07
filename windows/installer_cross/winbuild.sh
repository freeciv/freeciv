#!/bin/sh

# winbuild.sh: Cross-compiling freeciv from linux to Windows using Crosser dllstack
#
# (c) 2008-2016 Marko Lindqvist
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.

# Version 2.4.3 (28-Jan-23)

WINBUILD_VERSION="2.4.3"
MIN_WINVER=0x0603 # Windows 8.1, Qt6-client and Qt6-ruledit builds override this
CROSSER_FEATURE_LEVEL=2.11

if test "$1" = "" || test "$1" = "-h" || test "$1" = "--help" ; then
  echo "Usage: $0 <crosser dir> [gui]"
  exit 1
fi

if test "$1" = "-v" || test "$1" = "--version" ; then
  echo "winbuild.sh version $WINBUILD_VERSION"
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

SRC_ROOT="$(cd ../.. || exit 1 ; pwd)"

VERREV="$(${SRC_ROOT}/fc_version)"
if test "$INST_CROSS_MODE" != "release" ; then
  if test -d ${SRC_ROOT}/.git || test -f ${SRC_ROOT}/.git ; then
    VERREV="$VERREV-$(cd ${SRC_ROOT} && git rev-parse --short HEAD)"
  fi
fi

FLVL=$(grep "CrosserFeatureLevel=" $DLLSPATH/crosser.txt | sed -e 's/CrosserFeatureLevel="//' -e 's/"//')

if test "$FLVL" != "$CROSSER_FEATURE_LEVEL" ; then
  echo "Crosser feature level \"$FLVL\", required \"$CROSSER_FEATURE_LEVEL\"!" >&2
  exit 1
fi

CSET=$(grep "CrosserSet=" $DLLSPATH/crosser.txt | sed -e 's/CrosserSet="//' -e 's/"//')

if test "$CSET" != "current" ; then
  echo "Crosser set is \"$CSET\", only \"current\" is supported!" >&2
  exit 1
fi

SETUP=$(grep "CrosserSetup=" $DLLSPATH/crosser.txt | sed -e 's/CrosserSetup="//' -e 's/"//')

# Make -Wno-error=attributes Qt-client/Ruledit specific as
# upstream updates to Qt headers allow. Currently needed
# in all cases.
CXXFLAGS="-Wno-error=attributes -Wl,-allow-multiple-definition"

if test "$2" = "ruledit" ; then
  SINGLE_GUI=true
  GUIP="-ruledit"
  RULEDIT="yes"
  CLIENTS="no"
  FCMP="no"
  SERVER="no"
  AIS="--enable-ai-static=stub"
elif test "$2" != "" ; then
  SINGLE_GUI=true
  GUIP="-client-$2"
  SERVER="yes"
  if test "$2" = "qt6" ; then
    RULEDIT="yes"
    CLIENTS="qt"
  else
    RULEDIT="no"
    CLIENTS="$2"
  fi
  case $2 in
    sdl2) FCMP="gtk4" ;;
    gtk3.22) FCMP="gtk3" ;;
    gtk4) FCMP="gtk4" ;;
    qt6) FCMP="qt"
         QTVER="Qt6"
         MIN_WINVER="0x0A00" ;; # Qt6 requires Win10 anyway
    *) echo "Unknown gui \"$2\"!" >&2
       exit 1 ;;
  esac
else
  GUIP=""
  SERVER="yes"
  RULEDIT="yes"
fi

if test "$NAMEP" = "" ; then
  NAMEP="$GUIP"
fi

if test "$MAKE_PARAMS" = "" ; then
  MAKE_PARAMS="-j$(nproc)"
fi

BUILD_DIR="autotools/build/${SETUP}${NAMEP}"

if ! rm -Rf "${BUILD_DIR}" ; then
  echo "Failed to clear out old build directory!" >&2
  exit 1
fi

if ! mkdir -p "${BUILD_DIR}" ; then
  echo "Can't create build directory \"${BUILD_DIR}\"!" >&2
  exit 1
fi

if test "$SETUP" = "win64" ; then
  TARGET=x86_64-w64-mingw32
  if test "$SINGLE_GUI" != "true" ; then
    CLIENTS="sdl2,gtk3.22"
    FCMP="gtk3,cli"
  fi
  VERREV="win64-$VERREV"
elif test "$SETUP" = "win32" ; then
  TARGET=i686-w64-mingw32
  if test "$SINGLE_GUI" != "true" ; then
    CLIENTS="sdl2,gtk3.22"
    FCMP="gtk3,cli"
  fi
  VERREV="win32-$VERREV"
else
  echo "Unsupported crosser setup \"${SETUP}\"!" >&2
  exit 1
fi

if test "${SINGLE_GUI}" != "true" || test "$2" = "ruledit" ; then
  if grep "CROSSER_QT6" "${DLLSPATH}/crosser.txt" | grep yes > /dev/null
  then
    QT6="yes"
    QTVER="Qt6"
    if test "${SINGLE_GUI}" = "true" ; then
      # Build is ONLY about Qt6 programs
      MIN_WINVER="0x0A00" # Qt6 requires Win10 anyway
    fi
  fi
fi

if test "${SINGLE_GUI}" != "true" ; then
  if test "${QT6}" = "yes"
  then
    CLIENTS="${CLIENTS},qt"
    FCMP="${FCMP},qt"
  fi
fi

if test "${QTVER}" = "Qt6"; then
  QTPARAMS="--with-qtver=Qt6 --with-qt6-includes=${DLLSPATH}/qt6/include --with-qt6-libs=${DLLSPATH}/lib"
  MOC_CROSSER="${DLLSPATH}/linux/libexec/moc-qt6"
fi

echo "----------------------------------"
echo "Building for ${SETUP}"
echo "Freeciv version ${VERREV}"
echo "Clients: ${CLIENTS}"
echo "----------------------------------"

export CC="$TARGET-gcc -static-libgcc -static-libstdc++"
export CXX="$TARGET-g++ -static-libgcc -static-libstdc++"

if ! ${SRC_ROOT}/autogen.sh --no-configure-run ; then
  echo "Autogen failed" >&2
  exit 1
fi

INSTALL_DIR="$(pwd)/autotools/install/freeciv-${VERREV}${NAMEP}"

if ! (
cd "${BUILD_DIR}"

if test "$INST_CROSS_MODE" = "snapshot" ; then
  GITREVP="--enable-gitrev"
else
  GITREVP=""
fi

if test "$MOCCMD" = "" && test "$MOC_CROSSER" != "" ; then
  MOCPARAM="MOCCMD=${MOC_CROSSER}"
fi

PATH="${DLLSPATH}/linux/bin:${PATH}"
hash -r

if ! ${SRC_ROOT}/configure \
     ac_cv_working_gettimeofday=yes \
     $MOCPARAM \
     FREECIV_LABEL_FORCE="<base>-crs" \
     CPPFLAGS="-I${DLLSPATH}/include -D_WIN32_WINNT=${MIN_WINVER}" \
     CFLAGS="-Wno-error" \
     CXXFLAGS="${CXXFLAGS}" \
     PKG_CONFIG_LIBDIR="${DLLSPATH}/lib/pkgconfig" \
     --enable-sys-tolua-cmd \
     --with-magickwand="${DLLSPATH}/bin" \
     --prefix="/" \
     $GITREVP \
     --enable-client=$CLIENTS \
     --enable-fcmp=$FCMP \
     --enable-debug \
     --host=$TARGET \
     --build=$(${SRC_ROOT}/bootstrap/config.guess) \
     --with-libiconv-prefix=${DLLSPATH} \
     --with-sqlite3-prefix=${DLLSPATH} \
     --with-followtag="crosser" \
     --enable-crosser \
     ${AIS} \
     --disable-freeciv-manual \
     --enable-sdl-mixer=sdl2 \
     ${QTPARAMS} \
     --with-tinycthread \
     --without-readline \
     --enable-server=$SERVER \
     --enable-ruledit=$RULEDIT \
     $EXTRA_CONFIG
then
  echo "Configure failed" >&2
  exit 1
fi

rm -R "$INSTALL_DIR"

# Make each of 'clean', build, and 'install' in separate steps as
# relying on install to depend on all the build activities was not always working.
if ! make $MAKE_PARAMS DESTDIR="$INSTALL_DIR" clean
then
  echo "Make clean failed" >&2
  exit 1
fi

if ! make $MAKE_PARAMS DESTDIR="$INSTALL_DIR"
then
  echo "Build failed" >&2
  exit 1
fi

if ! make $MAKE_PARAMS DESTDIR="$INSTALL_DIR" install
then
  echo "Make install failed" >&2
  exit 1
fi

if ! cp gen_headers/fc_config.h $INSTALL_DIR/share/freeciv/ ; then
  echo "Storing fc_config.h failed" >&2
  exit 1
fi

if ! cp $DLLSPATH/crosser.txt $INSTALL_DIR/share/freeciv/ ; then
  echo "Storing crosser.txt failed" >&2
  exit 1
fi

if ! cp $DLLSPATH/ComponentVersions.txt $INSTALL_DIR/share/freeciv/CrosserComponents.txt ; then
  echo "Storing CrosserComponents.txt failed" >&2
  exit 1
fi
) then
  exit 1
fi

if ! mkdir -p autotools/output ; then
  echo "Creating autotools/output directory failed" >&2
  exit 1
fi

if ! 7z a -r autotools/output/freeciv-${VERREV}${NAMEP}.7z \
             freeciv-${VERREV}${NAMEP}
then
  echo "7z failed" >&2
  exit 1
fi
