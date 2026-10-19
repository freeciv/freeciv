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

# Variant of homebrew-appbundle.sh: GTK4 client only, libraries bundled
# inside the package. Output: dist/Freeciv.app, dist/Freeciv Server.app and
# dist/Freeciv-<version>.dmg
#
# Required:
#   brew install dylibbundler meson ninja pkg-config gettext icu4c \
#     readline sqlite curl xz zstd gtk4 librsvg adwaita-icon-theme lua@5.4 \
#     sdl3 sdl3_mixer gdk-pixbuf
#
# Work from the top-level source directory, regardless of where the
# script was started from
cd "$(dirname "$0")/../.." || exit 1

BREW="$(brew --prefix)"

export CXXFLAGS=-std=c++17
export CPPFLAGS="-I$(brew --prefix readline)/include -I$(brew --prefix gettext)/include -I${BREW}/include"
export LDFLAGS="-L$(brew --prefix icu4c)/lib -L$(brew --prefix readline)/lib -L$(brew --prefix gettext)/lib"
export PKG_CONFIG_PATH="$(brew --prefix icu4c)/lib/pkgconfig:$(brew --prefix lua@5.4)/lib/pkgconfig"
export PATH="$(brew --prefix gettext)/bin:$PATH"

if ! command -v dylibbundler >/dev/null ; then
  echo "dylibbundler not found. Install it with: brew install dylibbundler" >&2
  exit 1
fi

DISTDIR="${PWD}/dist"
APPDIR="${DISTDIR}/Freeciv.app"
CONTENTSDIR="${APPDIR}/Contents"

# Clean start (changing the configuration requires a fresh build directory)
rm -rf "${DISTDIR}" build

if ! mkdir -p "${CONTENTSDIR}/Resources" "${CONTENTSDIR}/MacOS" ; then
  echo "Failed to create \"${CONTENTSDIR}\"" >&2
  exit 1
fi

if ! cp data/freeciv-client.icns "${CONTENTSDIR}/Resources/" ; then
  echo "Failed to copy file \"freeciv-client.icns\"" >&2
  exit 1
fi

VSTRING=$(./fc_version)

# Info.plist - the executable is a launcher script that sets up the environment
cat > "${CONTENTSDIR}/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>
	<string>freeciv</string>
	<key>CFBundleIdentifier</key>
	<string>freeciv</string>
	<key>CFBundleVersion</key>
	<string>${VSTRING}</string>
	<key>CFBundleShortVersionString</key>
	<string>${VSTRING}</string>
	<key>CFBundleExecutable</key>
	<string>Freeciv</string>
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
	<key>NSHighResolutionCapable</key>
	<true/>
</dict>
</plist>
EOF

if ! echo -n "APPL????" > "${CONTENTSDIR}/PkgInfo" ; then
  echo "Failed to create file \"${CONTENTSDIR}/PkgInfo\"" >&2
  exit 1
fi

# Launcher: points the game to data, icons, schemas and loaders inside the bundle
cat > "${CONTENTSDIR}/MacOS/Freeciv" <<'EOF'
#!/bin/bash
CONTENTS="$(cd "$(dirname "$0")/.." && pwd)"

export FREECIV_DATA_PATH="$CONTENTS/share/freeciv"
export FREECIV_LOCALE_PATH="$CONTENTS/share/locale"
export XDG_DATA_DIRS="$CONTENTS/share"
export GSETTINGS_SCHEMA_DIR="$CONTENTS/share/glib-2.0/schemas"
export PATH="$CONTENTS/bin:$PATH"

# The gdk-pixbuf loaders cache needs absolute paths, so it is generated
# at startup (the bundle may live on a read-only disk image)
CACHE_IN="$CONTENTS/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache.in"
if [ -f "$CACHE_IN" ]; then
  CACHE_OUT="${TMPDIR:-/tmp}/freeciv-pixbuf-loaders.cache"
  sed "s|@CONTENTS@|$CONTENTS|g" "$CACHE_IN" > "$CACHE_OUT"
  export GDK_PIXBUF_MODULE_FILE="$CACHE_OUT"
fi

exec "$CONTENTS/bin/freeciv-gtk4" "$@"
EOF
chmod +x "${CONTENTSDIR}/MacOS/Freeciv"

mkdir build || { echo "Failed to create build directory" >&2; exit 1; }

cd build || exit 1

if ! meson setup .. \
       -Dtools=manual,ruleup \
       -Dsyslua=true \
       -Ddebug=false \
       -Dclients=gtk4 \
       -Dfcmp=gtk4 \
       -Daudio=sdl3 \
       -Dfollowtag=macos \
       -Ddefault_library=static \
       -Dprefix="${CONTENTSDIR}" ||
   ! ninja ||
   ! ninja install
then
  echo "Build failed!" >&2
  exit 1
fi

cd .. || exit 1

# --- GTK4 resources -------------------------------------------------------

# GSettings schemas (needed e.g. by the file chooser dialog)
mkdir -p "${CONTENTSDIR}/share/glib-2.0/schemas"
cp "${BREW}"/share/glib-2.0/schemas/*.xml "${CONTENTSDIR}/share/glib-2.0/schemas/"
glib-compile-schemas "${CONTENTSDIR}/share/glib-2.0/schemas"

# Icon themes
mkdir -p "${CONTENTSDIR}/share/icons"
for T in Adwaita hicolor ; do
  if [ -d "${BREW}/share/icons/${T}" ] ; then
    cp -R "${BREW}/share/icons/${T}" "${CONTENTSDIR}/share/icons/"
  fi
done

# gdk-pixbuf loaders
PIXDIR="lib/gdk-pixbuf-2.0/2.10.0"
PIXSRC="$(brew --prefix gdk-pixbuf)/${PIXDIR}/loaders"
if [ -d "${PIXSRC}" ] ; then
  mkdir -p "${CONTENTSDIR}/${PIXDIR}"
  cp -R "${PIXSRC}" "${CONTENTSDIR}/${PIXDIR}/"
  "$(brew --prefix gdk-pixbuf)/bin/gdk-pixbuf-query-loaders" \
      "${CONTENTSDIR}/${PIXDIR}"/loaders/*.so |
    sed "s|${CONTENTSDIR}|@CONTENTS@|g" > "${CONTENTSDIR}/${PIXDIR}/loaders.cache.in"
else
  echo "Warning: gdk-pixbuf loaders directory not found, skipping" >&2
fi

chmod -R u+w "${CONTENTSDIR}"

# --- Bundling libraries ---------------------------------------------------

# Collect all Mach-O binaries (executables, plugins, loaders)
MACHO=()
while IFS= read -r F ; do
  if file -b "$F" | grep -q 'Mach-O' ; then
    MACHO+=("$F")
  fi
done < <(find "${CONTENTSDIR}/bin" "${CONTENTSDIR}/lib" -type f 2>/dev/null)

ARGS=()
for F in "${MACHO[@]}" ; do
  ARGS+=(-x "$F")
done

if ! dylibbundler -od -b -cd "${ARGS[@]}" \
       -d "${CONTENTSDIR}/Frameworks/" \
       -p "@executable_path/../Frameworks/" \
       -s "${BREW}/lib" ; then
  echo "dylibbundler failed!" >&2
  exit 1
fi

# --- Ad-hoc code signing (required on Apple Silicon) ----------------------

while IFS= read -r F ; do
  if file -b "$F" | grep -q 'Mach-O' ; then
    codesign --force -s - "$F"
  fi
done < <(find "${CONTENTSDIR}/Frameworks" "${CONTENTSDIR}/bin" "${CONTENTSDIR}/lib" -type f)

codesign --force --deep -s - "${APPDIR}"

# Sanity check: nothing should reference /opt/homebrew any more
LEFT=$(find "${CONTENTSDIR}/bin" "${CONTENTSDIR}/Frameworks" "${CONTENTSDIR}/lib" -type f -exec sh -c \
  'file -b "$1" | grep -q Mach-O && otool -L "$1" | grep -H --label="$1" "/opt/homebrew"' _ {} \;)
if [ -n "${LEFT}" ] ; then
  echo "Warning: references to /opt/homebrew remain:" >&2
  echo "${LEFT}" >&2
fi

# --- Freeciv Server.app -----------------------------------------------------
# A separate small bundle, so that the server gets its own icon in Launchpad.
# It contains no binaries: it opens freeciv-server from Freeciv.app (next to
# it, or in /Applications) in a Terminal window.

SERVERAPP="${DISTDIR}/Freeciv Server.app"
SERVERCONTENTS="${SERVERAPP}/Contents"

if ! mkdir -p "${SERVERCONTENTS}/MacOS" "${SERVERCONTENTS}/Resources" ; then
  echo "Failed to create \"${SERVERCONTENTS}\"" >&2
  exit 1
fi

if ! cp data/freeciv-server.icns "${SERVERCONTENTS}/Resources/" ; then
  echo "Failed to copy file \"freeciv-server.icns\"" >&2
  exit 1
fi

cat > "${SERVERCONTENTS}/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>
	<string>Freeciv Server</string>
	<key>CFBundleIdentifier</key>
	<string>freeciv.server</string>
	<key>CFBundleVersion</key>
	<string>${VSTRING}</string>
	<key>CFBundleShortVersionString</key>
	<string>${VSTRING}</string>
	<key>CFBundleExecutable</key>
	<string>FreecivServer</string>
	<key>CFBundleIconFile</key>
	<string>freeciv-server.icns</string>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>NSHighResolutionCapable</key>
	<true/>
</dict>
</plist>
EOF

# Bundle executable: opens the .command file below in a Terminal window
cat > "${SERVERCONTENTS}/MacOS/FreecivServer" <<'EOF'
#!/bin/bash
CONTENTS="$(cd "$(dirname "$0")/.." && pwd)"
exec open -a Terminal "$CONTENTS/Resources/freeciv-server.command"
EOF
chmod +x "${SERVERCONTENTS}/MacOS/FreecivServer"

# Script run inside Terminal: finds Freeciv.app and starts the server
cat > "${SERVERCONTENTS}/Resources/freeciv-server.command" <<'EOF'
#!/bin/bash
# Locate Freeciv.app: next to this Freeciv Server.app first, then in /Applications
SELF="$(cd "$(dirname "$0")/../../.." && pwd)"
APP=""
for CAND in "$SELF/Freeciv.app" "/Applications/Freeciv.app" ; do
  if [ -x "$CAND/Contents/bin/freeciv-server" ] ; then
    APP="$CAND"
    break
  fi
done

if [ -z "$APP" ] ; then
  echo "Freeciv.app not found next to Freeciv Server.app or in /Applications."
  echo "Press Enter to close."
  read -r
  exit 1
fi

export FREECIV_DATA_PATH="$APP/Contents/share/freeciv"
export FREECIV_LOCALE_PATH="$APP/Contents/share/locale"
exec "$APP/Contents/bin/freeciv-server" "$@"
EOF
chmod +x "${SERVERCONTENTS}/Resources/freeciv-server.command"

codesign --force --deep -s - "${SERVERAPP}"

# --- DMG ------------------------------------------------------------------

STAGE="${DISTDIR}/dmg-stage"
mkdir -p "${STAGE}"
cp -R "${APPDIR}" "${STAGE}/"
cp -R "${SERVERAPP}" "${STAGE}/"
ln -s /Applications "${STAGE}/Applications"

DMG="${DISTDIR}/Freeciv-${VSTRING}.dmg"
rm -f "${DMG}"
# hdiutil create is deprecated in macOS 27 in favour of diskutil image;
# fall back to hdiutil on older systems that lack "diskutil image"
if ! diskutil image create from --format UDZO \
       --volumeName "Freeciv ${VSTRING}" "${STAGE}" "${DMG}" &&
   ! hdiutil create -volname "Freeciv ${VSTRING}" -srcfolder "${STAGE}" \
       -ov -format UDZO "${DMG}" ; then
  echo "Failed to create DMG" >&2
  exit 1
fi
rm -rf "${STAGE}"

echo "Done: ${DMG}"
