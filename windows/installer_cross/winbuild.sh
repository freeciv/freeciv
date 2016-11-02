#!/bin/sh

# winbuild.sh: Cross-compiling freeciv from linux to Windows using Crosser dllstack
#
# (c) 2008-2016 Marko Lindqvist
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.

# Version 2.0 (31-Oct-16)

WINBUILD_VERSION="2.0"
MIN_WINVER=0x0600 # Vista

if test "x$1" = x || test "x$1" = "x-h" || test "x$1" = "x--help" ; then
  echo "Usage: $0 <crosser dir>"
  exit 1
fi

if test "x$1" = "x-v" || test "x$1" = "x--version" ; then
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

if test -d ../../.svn ; then
  VERREV="$(../../fc_version)-r$(cd ../.. && svn info | grep Revision | sed 's/Revision: //')"
else
  VERREV="$(../../fc_version)"
fi

SETUP=$(grep "Setup=" $DLLSPATH/crosser.txt | sed -e 's/Setup="//' -e 's/"//')

if ! mkdir -p build-$SETUP ; then
  echo "Can't create build directory \"build-$SETUP\"!" >&2
  exit 1
fi

if test x$SETUP = xwin64 ; then
  TARGET=x86_64-w64-mingw32
  CLIENTS="gtk3,sdl2"
  FCMP="gtk3,cli"
  VERREV="win64-$VERREV"
else
  TARGET=i686-w64-mingw32
  CLIENTS="gtk2,gtk3,sdl2"
  FCMP="gtk2,gtk3,cli"
  VERREV="win32-$VERREV"
fi

if grep "CROSSER_QT" $DLLSPATH/crosser.txt | grep yes > /dev/null
then
    CLIENTS="$CLIENTS,qt"
    FCMP="$FCMP,qt"
fi

echo "----------------------------------"
echo "Building for $SETUP"
echo "Freeciv version $VERREV"
echo "Clients: $CLIENTS"
echo "----------------------------------"

export CC="$TARGET-gcc -static-libgcc -static-libstdc++"
export CXX="$TARGET-g++ -static-libgcc -static-libstdc++"

if ! ../../autogen.sh --no-configure-run ; then
  echo "Autogen failed" >&2
  exit 1
fi

INSTALL_DIR="$(pwd)/freeciv-${VERREV}"

(
cd build-$SETUP

if ! ../../../configure CPPFLAGS="-I${DLLSPATH}/include -D_WIN32_WINNT=${MIN_WINVER}" CFLAGS="-Wno-error" PKG_CONFIG_LIBDIR="${DLLSPATH}/lib/pkgconfig" --enable-sys-tolua-cmd --with-magickwand="${DLLSPATH}/bin" --prefix="/" --enable-client=$CLIENTS --enable-fcmp=$FCMP --enable-svnrev --enable-debug --host=$TARGET --build=$(../../../bootstrap/config.guess) --with-libiconv-prefix=${DLLSPATH} --with-sqlite3-prefix=${DLLSPATH} --with-followtag="crosser" --enable-crosser --enable-ai-static=classic --disable-freeciv-manual --enable-sdl-mixer=sdl2 --with-qt5-includes=${DLLSPATH}/include --with-qt5-libs=${DLLSPATH}/lib --with-tinycthread
then
  echo "Configure failed" >&2
  exit 1
fi

if ! make DESTDIR="$INSTALL_DIR" clean install
then
  echo "Build failed" >&2
  exit 1
fi

if ! cp gen_headers/fc_config.h $INSTALL_DIR/share/freeciv/ ; then
  echo "Storing fc_config.h failed" >&2
  exit 1
fi
)

if ! 7z a -r freeciv-${VERREV}.7z freeciv-${VERREV}
then
  echo "7z failed" >&2
  exit 1
fi
