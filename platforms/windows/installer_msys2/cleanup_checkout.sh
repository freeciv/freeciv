#!/bin/sh

if test "${VERSION_REVTYPE}" = "git" ; then
  git checkout "$1/translations"
fi
