#!/bin/bash
#
# Freeciv Travis CI Bootstrap Script 
#
# https://travis-ci.org/freeciv/freeciv
echo "Building Freeciv on Travis CI."
basedir=$(pwd)
logfile="${basedir}/freeciv-travis.log"


# Redirect copy of output to a log file.
exec > >(tee ${logfile})
exec 2>&1
set -e

uname -a

case $1 in
"dist")
mkdir build
cd build
../autogen.sh --disable-client --disable-fcmp --disable-ruledit --disable-server
make -s -j$(nproc) dist
echo "Freeciv distribution build successful!"
;;

"meson")
mkdir build
cd build
meson .. -Dprefix=${HOME}/freeciv/ -Dack_experimental=true -Dfcmp='gtk3','cli'
ninja
ninja install
;;

"os_x")
# gcc is an alias for clang on OS X

export PATH="/usr/local/opt/gettext/bin:/usr/local/opt/icu4c/bin:$PATH"
export CPPFLAGS="-I/usr/local/opt/gettext/include -I/usr/local/opt/icu4c/include $CPPFLAGS"
export LDFLAGS="-L/usr/local/opt/gettext/lib -L/usr/local/opt/icu4c/lib"
export PKG_CONFIG_PATH="/usr/local/opt/icu4c/lib/pkgconfig:$PKG_CONFIG_PATH"

mkdir build
cd build
../autogen.sh \
 --enable-client=gtk3.22,sdl2 \
 --enable-freeciv-manual
make
make install
;;

*)
# Configure and build Freeciv
mkdir build
cd build
../autogen.sh CFLAGS="-O3" CXXFLAGS="-O3" --enable-client=gtk3.22,gtk3,qt,sdl2,stub --enable-fcmp=cli,gtk3,qt --enable-freeciv-manual --enable-ai-static=classic,threaded,tex,stub --enable-fcdb=sqlite3,mysql --prefix=${HOME}/freeciv/ && make -s -j$(nproc)
sudo -u travis make install
echo "Freeciv build successful!"

# Check that each ruleset loads
echo "Checking rulesets"
sudo -u travis ./tests/rulesets_not_broken.sh

# Check ruleset saving
echo "Checking ruleset saving"
sudo -u travis ./tests/rulesets_save.sh

echo "Running Freeciv server autogame"
cd ${HOME}/freeciv/bin/
sudo -u travis ./freeciv-server --Announce none -e --read ${basedir}/scripts/test-autogame.serv

echo "Freeciv server autogame successful!"
;;
esac
