#!/bin/bash

FCVER=$(../fc_version)

if ! flatpak-builder --user --repo=repo --force-clean build org.freeciv.gtk322.yml ||
   ! flatpak-builder --user --repo=repo --force-clean build org.freeciv.mp.gtk3.yml ||
   ! flatpak-builder --user --repo=repo --force-clean build org.freeciv.qt.yml ||
   ! flatpak build-update-repo repo ||
   ! flatpak build-bundle repo "freeciv-gtk3.22-${FCVER}.flatpak" org.freeciv.gtk322 ||
   ! flatpak build-bundle repo "freeciv-mp-gtk3-${FCVER}.flatpak" org.freeciv.mp.gtk3 ||
   ! flatpak build-bundle repo "freeciv-qt-${FCVER}.flatpak" org.freeciv.qt
then
  echo "FAILURE" >&2
  exit 1
fi
