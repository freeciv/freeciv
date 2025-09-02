#!/usr/bin/env bash

# Freeciv - Copyright (C) 2022-2023 - The Freeciv Team
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

BROOT="$(pwd)"
SROOT="$(cd $(dirname "$0") && pwd)"

if test "${BROOT}" = "${SROOT}" ; then
  echo "Run $0 from a separate build directory." >&2
  exit 1
fi

cd "$(dirname "$0")" || exit 1
FCVER=$(../../fc_version)

if ! flatpak-builder --user --repo="${BROOT}/repo" --state-dir="${BROOT}/state" --force-clean "${BROOT}/build" org.freeciv.gtk4.yml ||
   ! flatpak-builder --user --repo="${BROOT}/repo" --state-dir="${BROOT}/state" --force-clean "${BROOT}/build" org.freeciv.gtk322.yml ||
   ! flatpak-builder --user --repo="${BROOT}/repo" --state-dir="${BROOT}/state" --force-clean "${BROOT}/build" org.freeciv.gtk4.mp.yml ||
   ! flatpak-builder --user --repo="${BROOT}/repo" --state-dir="${BROOT}/state" --force-clean "${BROOT}/build" org.freeciv.gtk3.mp.yml ||
   ! flatpak-builder --user --repo="${BROOT}/repo" --state-dir="${BROOT}/state" --force-clean "${BROOT}/build" org.freeciv.qt.yml ||
   ! flatpak-builder --user --repo="${BROOT}/repo" --state-dir="${BROOT}/state" --force-clean "${BROOT}/build" org.freeciv.sdl2.yml ||
   ! flatpak-builder --user --repo="${BROOT}/repo" --state-dir="${BROOT}/state" --force-clean "${BROOT}/build" org.freeciv.ruledit.yml ||
   ! flatpak-builder --user --repo="${BROOT}/repo" --state-dir="${BROOT}/state" --force-clean "${BROOT}/build" org.freeciv.qt.mp.yml ||
   ! flatpak build-update-repo "${BROOT}/repo" ||
   ! flatpak build-bundle "${BROOT}/repo" "${BROOT}/freeciv-gtk4-${FCVER}.flatpak" org.freeciv.gtk4 ||
   ! flatpak build-bundle "${BROOT}/repo" "${BROOT}/freeciv-gtk3.22-${FCVER}.flatpak" org.freeciv.gtk322 ||
   ! flatpak build-bundle "${BROOT}/repo" "${BROOT}/freeciv-gtk4-mp-${FCVER}.flatpak" org.freeciv.gtk4.mp ||
   ! flatpak build-bundle "${BROOT}/repo" "${BROOT}/freeciv-gtk3-mp-${FCVER}.flatpak" org.freeciv.gtk3.mp ||
   ! flatpak build-bundle "${BROOT}/repo" "${BROOT}/freeciv-qt-${FCVER}.flatpak" org.freeciv.qt ||
   ! flatpak build-bundle "${BROOT}/repo" "${BROOT}/freeciv-sdl2-${FCVER}.flatpak" org.freeciv.sdl2 ||
   ! flatpak build-bundle "${BROOT}/repo" "${BROOT}/freeciv-ruledit-${FCVER}.flatpak" org.freeciv.ruledit ||
   ! flatpak build-bundle "${BROOT}/repo" "${BROOT}/freeciv-qt-mp-${FCVER}.flatpak" org.freeciv.qt.mp
then
  echo "FAILURE" >&2
  exit 1
fi
