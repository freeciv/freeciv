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
dependencies="libgtk-3-dev libcurl4-openssl-dev libtool automake autoconf autotools-dev language-pack-en python3.4 python3.4-dev liblzma-dev libicu-dev libsdl1.2-dev libsqlite3-dev qt5-default libsdl2-mixer-dev"

## Dependencies
echo "==== Installing Updates and Dependencies ===="
echo "apt-get update"
apt-get -y update
echo "apt-get install dependencies"
apt-get -y install ${dependencies}

# Configure and build Freeciv
./autogen.sh CFLAGS="-O3" --disable-nls --enable-client=gtk3,qt --enable-fcmp=cli,gtk3,qt --enable-freeciv-manual=html --enable-ai-static=classic,threaded,tex --prefix=${HOME}/freeciv/ && make -s -j$(nproc)
sudo -u travis make install
echo "Freeciv build successful!"

# Check that each ruleset loads
echo "Checking rulesets"
sudo -u travis ./tests/rulesets_not_broken.sh

echo "Running Freeciv server autogame"
cd ${HOME}/freeciv/bin/
sudo -u travis ./freeciv-server --Announce none -e --read ${basedir}/scripts/test-autogame.serv

echo "Freeciv server autogame successful!"
