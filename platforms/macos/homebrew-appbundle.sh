#!/usr/bin/env bash

# Freeciv - Copyright (C) 2022-2023 - Freeciv team
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

# TODO: Remove this if not needed here, and if it IS needed,
#       investigate why meson.build isn't using proper definition
#       by itself.
export CXXFLAGS=-std=c++17
export CPPFLAGS="-I$(brew --prefix readline)/include"
export LDFLAGS="-L$(brew --prefix icu4c)/lib -L$(brew --prefix readline)/lib"
export PKG_CONFIG_PATH="$(brew --prefix icu4c)/lib/pkgconfig"

CONTENTSDIR="/Applications/Freeciv.app/Contents/"

if ! [ -e "${CONTENTSDIR}" ] ; then

	if ! mkdir -p "${CONTENTSDIR}" ; then
	  echo "Failed to create \"${CONTENTSDIR}" >&2
	  exit 1
	fi

	if ! mkdir -p "${CONTENTSDIR}Resources" ; then
	  echo "Failed to create directory \"${CONTENTSDIR}Resources\"" >&2
	  exit 1
	fi

	if ! cp data/freeciv-client.icns "${CONTENTSDIR}Resources" ; then
	  echo "Failed to copy file \"freeciv-client.icns\"" >&2
	  exit 1
	fi

	if ! mkdir -p "${CONTENTSDIR}MacOS" ; then
	  echo "Failed to create directory \"${CONTENTSDIR}MacOS\"" >&2
	  exit 1
	fi

	if ! echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist SYSTEM \"file://localhost/System/Library/DTDs/PropertyList.dtd\"
<plist version=\"0.9\">
<dict>
	<key>LSEnvironment</key>
	<dict>
		<key>LD_LIBRARY_PATH</key>
		<string>/Applications/Freeciv.app/Contents/lib</string>
	</dict>
	<key>CFBundleName</key>
	<string>freeciv</string>
	<key>CFBundleIdentifier</key>
	<string>freeciv</string>
	<key>CFBundleVersion</key>
	<string>VER</string>
	<key>CFBundleShortVersionString</key>
	<string>VER</string>
	<key>CFBundleExecutable</key>
	<string>../bin/freeciv-gtk4</string>
	<key>CFBundleIconFile</key>
	<string>freeciv-client.icns</string>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
</dict>
</plist>" > "${CONTENTSDIR}Info.plist" ; then
	  echo "Failed to create file \"${CONTENTSDIR}Info.plist\"" >&2
	  exit 1
	fi

	VSTRING=`./fc_version`

	# Substitute VSTRING into Info.plist
	sed -i '' -e "s/VER/${VSTRING}/g" "${CONTENTSDIR}Info.plist"

	if ! echo -n "APPL????" > "${CONTENTSDIR}PkgInfo" ; then
	  echo "Failed to create file \"${CONTENTSDIR}PkgInfo\"" >&2
	  exit 1
	fi

fi

if ! [ -e build ] ; then

	if ! mkdir build ; then
	  echo "Failed to create build directory" >&2
	  exit 1
	fi

fi

cd build || exit 1

if ! meson setup .. \
       -Dtools=manual,ruleup \
       -Dsyslua=true \
       -Ddebug=false \
       -Dclients=gtk4,qt \
       -Dfcmp=qt \
       -Dfollowtag=macos-S3_4 \
       -Ddefault_library=static \
       -Dprefix="${CONTENTSDIR}" ||
   ! ninja ||
   ! ninja install
then
  echo "Build failed!" >&2
  exit 1
fi
