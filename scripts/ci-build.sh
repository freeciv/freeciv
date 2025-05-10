#!/usr/bin/env bash
#/***********************************************************************
# Freeciv - Copyright (C) 2017-2023
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#***********************************************************************/

#
# Freeciv CI Script
#
# https://github.com/freeciv/freeciv/actions
#

echo "Running CI job $1 for Freeciv."
basedir=$(pwd)
logfile="${basedir}/freeciv-CI.log"


# Redirect copy of output to a log file.
exec > >(tee ${logfile})
exec 2>&1
set -e

uname -a

case $1 in
"dist")
mkdir build
cd build
../autogen.sh \
 || (let config_exit_status=$? \
     && echo "Config exit status: $config_exit_status" \
     && cat config.log \
     && exit $config_exit_status)
make -s -j$(nproc) distcheck
echo "Freeciv distribution check successful!"
;;

"meson")
FC_MESON_VER=""
if test "$FC_MESON_VER" != "" ; then
  mkdir meson-install
  cd meson-install
  wget "https://github.com/mesonbuild/meson/releases/download/${FC_MESON_VER}/meson-${FC_MESON_VER}.tar.gz"
  tar xzf "meson-${FC_MESON_VER}.tar.gz"
  ln -s meson.py "meson-${FC_MESON_VER}/meson"
  export PATH="$(pwd)/meson-${FC_MESON_VER}:$PATH"
  cd ..
fi

mkdir build
cd build
meson setup .. \
  -Dprefix=${HOME}/freeciv/meson \
  -Ddebug=true \
  -Dclients='gtk3.22','qt','sdl2','gtk4','stub' \
  -Dfcmp='gtk3','qt','cli','gtk4' \
  -Dqtver=qt6
ninja
ninja install
echo "Freeciv build successful!"
;;

"mac-meson")

GETTEXT_PREFIX="$(brew --prefix gettext)"
READLINE_PREFIX="$(brew --prefix readline)"
ICU4C_PREFIX="$(brew --prefix icu4c)"
SDL2_PREFIX="$(brew --prefix sdl2)"
SDL2_MIXER_PREFIX="$(brew --prefix sdl2_mixer)"
SDL2_TTF_PREFIX="$(brew --prefix sdl2_ttf)"
SDL2_IMAGE_PREFIX="$(brew --prefix sdl2_image)"

export CPPFLAGS="-I${GETTEXT_PREFIX}/include -I${READLINE_PREFIX}/include -I${SDL2_PREFIX}/include -I${SDL2_PREFIX}/include/SDL2 -I${SDL2_MIXER_PREFIX}/include -I${SDL2_TTF_PREFIX}/include -I${SDL2_IMAGE_PREFIX}/include"
export LDFLAGS="-L${GETTEXT_PREFIX}/lib -L${ICU4C_PREFIX}/lib -L${READLINE_PREFIX}/lib -L${SDL2_PREFIX}/lib -L${SDL2_MIXER_PREFIX}/lib -L${SDL2_TTF_PREFIX}/lib -L${SDL2_IMAGE_PREFIX}/lib"
export PKG_CONFIG_PATH="${ICU4C_PREFIX}/lib/pkgconfig"

mkdir build
cd build
meson setup .. \
  -Ddebug=true \
  -Dtools=ruledit,manual,ruleup \
  -Dsyslua=true \
  -Dclients=gtk3.22,sdl2,gtk4,qt,stub \
  -Dfcmp=gtk3,gtk4,qt,cli \
  -Dfollowtag=macos \
  -Dprefix=${HOME}/freeciv/mac-meson \
  || (let meson_exit_status=$? \
      && echo "meson.log:" \
      && cat meson-logs/meson-log.txt \
      && exit $meson_exit_status)
ninja
ninja install
echo "Freeciv build successful!"

echo "Running Freeciv server autogame"
cd ${HOME}/freeciv/mac-meson/bin/
./freeciv-server --Announce none -e -F --read ${basedir}/scripts/test-autogame.serv

echo "Freeciv server autogame successful!"
;;

"clang_debug")
# Configure and build Freeciv
mkdir build
cd build
../autogen.sh \
 CC="clang" \
 CXX="clang++" \
 --enable-debug \
 --enable-sys-lua \
 --enable-sys-tolua-cmd \
 --disable-fcdb \
 --with-qtver=qt6 \
 --enable-client=gtk3.22,qt,sdl2,gtk4,stub \
 --enable-fcmp=cli,gtk3,qt,gtk4 \
 --enable-fcdb=sqlite3,mysql,postgres,odbc \
 --enable-freeciv-manual \
 --enable-ai-static=classic,tex,stub \
 --prefix=${HOME}/freeciv/clang \
 || (let config_exit_status=$? \
     && echo "Config exit status: $config_exit_status" \
     && cat config.log \
     && exit $config_exit_status)
make -s -j$(nproc)
make install
;;

tcc)

mkdir build
cd build
../autogen.sh \
 CC="tcc" \
 LD="tcc" \
 --enable-debug \
 --enable-client=gtk3.22,stub,gtk4 \
 --enable-fcmp=cli,gtk3,gtk4 \
 --enable-fcdb=sqlite3,mysql,odbc \
 --disable-ruledit \
 --disable-sdl-mixer \
 --prefix=${HOME}/freeciv/tcc \
 || (let config_exit_status=$? \
     && echo "Config exit status: $config_exit_status" \
     && cat config.log \
     && exit $config_exit_status)
make -s -j$(nproc)
make install
echo "Freeciv build successful!"
;;

emsdk)
(
  SRCROOT=$(pwd)

  # Outside source tree
  cd ..

  if ! ${SRCROOT}/platforms/emscripten/emssetup.sh emsdk ; then
    exit 1
  fi
)

mkdir build
cd build
../platforms/emscripten/emsbuild.sh "../../emsdk"

echo "Freeciv build successful!"
;;

*)
# Fetch S3_1 in the background for the ruleset upgrade test
git fetch --no-tags --quiet https://github.com/freeciv/freeciv.git S3_1:S3_1 &

# Configure and build Freeciv
mkdir build
cd build
../autogen.sh \
 CFLAGS="-O3" \
 CXXFLAGS="-O3" \
 --with-qtver=qt6 \
 --enable-client=gtk3.22,qt,sdl2,gtk4,stub \
 --enable-fcmp=cli,gtk3,qt,gtk4 \
 --enable-fcdb=sqlite3,mysql,postgres,odbc \
 --enable-freeciv-manual \
 --enable-ruledit=experimental \
 --enable-ai-static=classic,tex,stub \
 --prefix=${HOME}/freeciv/default \
 || (let config_exit_status=$? \
     && echo "Config exit status: $config_exit_status" \
     && cat config.log \
     && exit $config_exit_status)
make -s -j$(nproc)
make install
echo "Freeciv build successful!"

# Check that each ruleset loads
echo "Checking rulesets"
./tests/rulesets_not_broken.sh

# Check ruleset saving
echo "Checking ruleset saving"
./tests/rulesets_save.sh

# Check ruleset upgrade
echo "Ruleset upgrade"
echo "Preparing test data"
../tests/rs_test_res/upgrade_ruleset_sync.bash
echo "Checking ruleset upgrade"
./tests/rulesets_upgrade.sh

# Check ruleset autohelp generation
echo "Checking ruleset auto help generation"
./tests/rulesets_autohelp.sh

echo "Running Freeciv server autogame"
cd ${HOME}/freeciv/default/bin/
./freeciv-server --Announce none -e -F --read ${basedir}/scripts/test-autogame.serv

echo "Freeciv server autogame successful!"
;;
esac
