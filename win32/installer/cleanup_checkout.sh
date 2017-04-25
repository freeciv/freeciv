#!/bin/sh

if test "x$VERSION_REVTYPE" = "xgit" ; then
    git checkout $1/translations
elif test "x$VERSION_REVTYPE" = "xsvn" ; then
    cd $1/translations
    svn revert -R .
fi
