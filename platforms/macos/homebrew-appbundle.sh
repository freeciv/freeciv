#!/bin/bash

# Freeciv - Copyright (C) 2022 - Freeciv team
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

export CPPFLAGS="-I$(brew --prefix readline)/include"
export LDFLAGS="-L$(brew --prefix icu4c)/lib -L$(brew --prefix readline)/lib"
export PKG_CONFIG_PATH="$(brew --prefix icu4c)/lib/pkgconfig"

CONTENTSDIR="/Applications/Freeciv.app/Contents/"

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

if ! mkdir -p "${CONTENTSDIR}lib" ; then
  echo "Failed to create directory \"${CONTENTSDIR}lib\"" >&2
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
	<string>3.1.0-alpha</string>
	<key>CFBundleShortVersionString</key>
	<string>3.1.0-alpha</string>
	<key>CFBundleExecutable</key>
	<string>../bin/freeciv-gtk3.22</string>
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

if ! echo -n "APPL????" > "${CONTENTSDIR}PkgInfo" ; then
  echo "Failed to create file \"${CONTENTSDIR}PkgInfo\"" >&2
  exit 1
fi

if ! mkdir build ; then
  echo "Failed to create build directory" >&2
  exit 1
fi

cd build || exit 1

if ! meson setup .. \
       -Druledit=false \
       -Dsyslua=true \
       -Dclients=gtk3.22 \
       -Dfcmp=gtk3 \
       -Dprefix="$CONTENTSDIR" ||
   ! ninja ||
   ! ninja install
then
  echo "Build failed!" >&2
  exit 1
fi
