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

# Setup python3 to use
update-alternatives --install /usr/bin/python python /usr/bin/python3 2
update-alternatives --install /usr/bin/python python /usr/bin/python2 1
update-alternatives --set python /usr/bin/python3

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
meson .. -Dprefix=${HOME}/freeciv/ -Dack_experimental=true
ninja
ninja install
;;

*)
# Configure and build Freeciv
mkdir build
cd build
../autogen.sh CFLAGS="-O3" CXXFLAGS="-O3" --enable-client=gtk3.22,gtk3,sdl2,stub --enable-fcmp=cli,gtk3 --enable-freeciv-manual --enable-ai-static=classic,threaded,tex,stub --enable-fcdb=sqlite3,mysql --disable-ruledit --prefix=${HOME}/freeciv/ && make -s -j$(nproc)
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
