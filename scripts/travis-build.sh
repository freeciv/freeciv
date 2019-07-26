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

# Based on fresh install of Ubuntu 14.04
dependencies="gettext libgtk-3-dev libcurl4-openssl-dev libtool automake autoconf autotools-dev language-pack-en python3.7 liblzma-dev libicu-dev libsqlite3-dev qt5-default libsdl2-mixer-dev libsdl2-gfx-dev libsdl2-image-dev libsdl2-ttf-dev libmysqlclient-dev"

## Dependencies
echo "==== Installing Updates and Dependencies ===="
echo "apt update"
apt -y update
echo "apt install dependencies"
apt -y install ${dependencies}

# Setup python3 to use
update-alternatives --install /usr/bin/python python /usr/bin/python3 2
update-alternatives --install /usr/bin/python python /usr/bin/python2 1
update-alternatives --set python /usr/bin/python3

# Configure and build Freeciv
mkdir build
cd build
../autogen.sh CFLAGS="-O3" CXXFLAGS="-O3" --enable-client=gtk3,qt,sdl2,stub --enable-fcmp=cli,gtk3,qt --enable-freeciv-manual --enable-ai-static=classic,threaded,tex,stub --enable-fcdb=sqlite3,mysql --prefix=${HOME}/freeciv/ && make -s -j$(nproc)
sudo -u travis make install
echo "Freeciv build successful!"

make -s -j$(nproc) dist
echo "Freeciv distribution build successful!"

# Check that each ruleset loads
echo "Checking rulesets"
sudo -u travis ./tests/rulesets_not_broken.sh

echo "Running Freeciv server autogame"
cd ${HOME}/freeciv/bin/
sudo -u travis ./freeciv-server --Announce none -e --read ${basedir}/scripts/test-autogame.serv

echo "Freeciv server autogame successful!"
