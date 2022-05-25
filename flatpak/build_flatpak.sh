#!/bin/bash

FCVER=$(../fc_version)

if ! flatpak-builder --user --repo=repo --force-clean build org.freeciv.gtk322.yml ||
   ! flatpak build-update-repo repo ||
   ! flatpak build-bundle repo "freeciv-gtk3.22-${FCVER}.flatpak" org.freeciv.gtk322
then
  echo "FAILURE" >&2
  exit 1
fi
