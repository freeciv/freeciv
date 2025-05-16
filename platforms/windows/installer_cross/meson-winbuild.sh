#!/bin/sh

# meson-winbuild.sh: Cross-compiling freeciv from linux to Windows using Crosser dllstack
#                    and Meson
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.
#

MESON_WINBUILD_VERSION="3.3.0-dev"
MIN_WINVER=0x0603 # Windows 8.1. Qt6-client and Qt6-ruledit builds override this
CROSSER_FEATURE_LEVEL=2.10

if test "$1" = "" || test "$1" = "-h" || test "$1" = "--help" ; then
  echo "Usage: $0 <crosser dir> <gui>"
  exit 1
fi

if test "$1" = "-v" || test "$1" = "--version" ; then
  echo "meson-winbuild.sh version ${MESON_WINBUILD_VERSION}"
  exit
fi

GUI="$2"

if test "${GUI}" != "gtk3.22" && test "${GUI}" != "gtk4" &&
   test "${GUI}" != "gtk4x" &&
   test "${GUI}" != "sdl2" &&
   test "${GUI}" != "qt6" &&
   test "${GUI}" != "qt6x" &&
   test "${GUI}" != "ruledit" ; then
  echo "Unknown gui \"$2\"" >&2
  exit 1
fi

DLLSPATH="$1"

if ! test -d "${DLLSPATH}" ; then
  echo "Dllstack directory \"${DLLSPATH}\" not found!" >&2
  exit 1
fi

if ! test -f "${DLLSPATH}/crosser.txt" ; then
  echo "Directory \"${DLLSPATH}\" does not look like crosser environment!" >&2
  exit 1
fi

SRC_DIR="$(cd "$(dirname "$0")" || exit 1 ; pwd)"
SRC_ROOT="$(cd "${SRC_DIR}/../../.." || exit 1 ; pwd)"
BUILD_ROOT="$(pwd)"

VERREV="$("${SRC_ROOT}/fc_version")"
if test "${INST_CROSS_MODE}" != "release" ; then
  if test -d "${SRC_ROOT}/.git" || test -f "${SRC_ROOT}/.git" ; then
    VERREV="${VERREV}-$(cd "${SRC_ROOT}" && git rev-parse --short HEAD)"
  fi
fi

FLVL=$(grep "CrosserFeatureLevel=" "${DLLSPATH}/crosser.txt" | sed -e 's/CrosserFeatureLevel="//' -e 's/"//')

if test "${FLVL}" != "${CROSSER_FEATURE_LEVEL}" ; then
  echo "Crosser feature level \"${FLVL}\", required \"${CROSSER_FEATURE_LEVEL}\"!" >&2
  exit 1
fi

CSET=$(grep "CrosserSet=" "${DLLSPATH}/crosser.txt" | sed -e 's/CrosserSet="//' -e 's/"//')

if test "${CSET}" != "current" ; then
  echo "Crosser set is \"${CSET}\", only \"current\" is supported!" >&2
  exit 1
fi

SETUP=$(grep "CrosserSetup=" "${DLLSPATH}/crosser.txt" | sed -e 's/CrosserSetup="//' -e 's/"//')

if ! test -f "${SRC_DIR}/setups/cross-${SETUP}.tmpl" ; then
  echo "Unsupported crosser setup \"${SETUP}\"!" >&2
  exit 1
fi

QTPARAMS=""

TOOLS=manual,ruleup
case "${GUI}" in
  gtk3.22) FCMP="gtk3" ;;
  gtk4) FCMP="gtk4" ;;
  gtk4x) FCMP="gtk4x" ;;
  sdl2) FCMP="gtk4" ;;
  qt6) CLIENT="qt"
       FCMP="qt"
       TOOLS="${TOOLS},ruledit"
       MIN_WINVER=0x0A00
       QTPARAMS="-Dqtver=qt6" ;;
  qt6x) CLIENT="qt"
        FCMP="qt"
        TOOLS="${TOOLS},ruledit"
        MIN_WINVER=0x0A00
        QTPARAMS="-Dqtver=qt6x" ;;
  ruledit) CLIENT="[]"
           FCMP="[]"
           TOOLS="ruledit"
           MIN_WINVER=0x0A00
           QTPARAMS="-Dqtver=qt6" ;;
esac

if test "${CLIENT}" = "" ; then
  CLIENT="${GUI}"
fi

if test "${GUI}" = "ruledit" ; then
  SERVER="disabled"
  MWAND="false"
else
  SERVER="enabled"
  MWAND="true"
fi

BUILD_DIR="${BUILD_ROOT}/meson/build/${SETUP}-${GUI}"

if ! rm -Rf "${BUILD_DIR}" ; then
  echo "Failed to clear out old build directory!" >&2
  exit 1
fi

if ! mkdir -p "${BUILD_DIR}" ; then
  echo "Can't create build directory \"${BUILD_DIR}\"!" >&2
  exit 1
fi

if ! sed "s,<PREFIX>,${DLLSPATH},g" "${SRC_DIR}/setups/cross-${SETUP}.tmpl" \
     > "${BUILD_DIR}/cross.txt"
then
  echo "Failed to create cross-file for ${SETUP} build!" >&2
  exit 1
fi

PACKAGENAME="freeciv-${VERREV}-${SETUP}-${GUI}"
MESON_INSTALL_DIR="${BUILD_ROOT}/meson/install/${PACKAGENAME}"

if ! rm -Rf "${MESON_INSTALL_DIR}" ; then
  echo "Failed to clear out old install directory!" >&2
  exit 1
fi

echo "----------------------------------"
echo "Building for ${SETUP}"
echo "Freeciv version ${VERREV}"
echo "----------------------------------"

if ! (
cd "${BUILD_DIR}"

export PKG_CONFIG_PATH="${DLLSPATH}/lib/pkgconfig"

export PATH="${DLLSPATH}/linux/bin:${DLLSPATH}/linux/libexec:${PATH}"

if ! meson setup \
     --cross-file=cross.txt \
     -Dprefix="${MESON_INSTALL_DIR}" \
     -Dstrip=true \
     -Dmin-win-ver="${MIN_WINVER}" \
     -Dclients="${CLIENT}" -Dfcmp="${FCMP}" \
     -Dsyslua=false \
     -Dmwand="${MWAND}" \
     -Dreadline=false \
     -Dserver="${SERVER}" \
     -Dtools="${TOOLS}" \
     -Ddefault_library=static \
     -Dsvgflags=true \
     ${QTPARAMS} \
     ${EXTRA_CONFIG} \
     "${SRC_ROOT}" ; then
  echo "Meson run failed!" >&2
  exit 1
fi

if ! ninja ; then
  echo "Ninja build failed!" >&2
  exit 1
fi

if ! ninja install; then
  echo "Ninja install failed!" >&2
  exit 1
fi

if ! cp fc_config.h "${MESON_INSTALL_DIR}/share/freeciv/" ; then
  echo "Storing fc_config.h failed" >&2
  exit 1
fi

if ! cp "${DLLSPATH}/crosser.txt" "${MESON_INSTALL_DIR}/share/freeciv/" ; then
  echo "Storing crosser.txt failed" >&2
  exit 1
fi

if ! cp "${DLLSPATH}/ComponentVersions.txt" \
        "${MESON_INSTALL_DIR}/share/freeciv/CrosserComponents.txt"
then
  echo "Storing CrosserComponents.txt failed" >&2
  exit 1
fi

) then
  exit 1
fi

if ! mkdir -p "${BUILD_ROOT}/meson/output" ; then
  echo "Creating meson/output directory failed" >&2
  exit 1
fi

( cd "${BUILD_ROOT}/meson/install"

  if ! 7z a -r "../output/${PACKAGENAME}.7z" "${PACKAGENAME}"
  then
    echo "7z failed" >&2
    exit 1
  fi
)
