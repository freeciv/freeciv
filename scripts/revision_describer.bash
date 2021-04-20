#!/bin/bash

if test x$1 = x ; then
  REVISION="HEAD"
else
  REVISION=$1
fi

if test x$2 = x ; then
  ONLY_CARE_ABOUT=
else
  ONLY_CARE_ABOUT="-- $2"
fi

FORMAT="%h %cI!%ce %s"
FORMAT="${FORMAT}%n%n"
FORMAT="${FORMAT}commit: %H%n"
FORMAT="${FORMAT}action stamp: %cI!%ce%n"
FORMAT="${FORMAT}author: %aN <%aE>"
FORMAT="${FORMAT} %ai%n"
FORMAT="${FORMAT}committer: %cN <%cE>"
FORMAT="${FORMAT} %ci%n"
FORMAT="${FORMAT}commit message:%n"
FORMAT="${FORMAT}%w(0, 2, 2)%B"


git log \
 --format="${FORMAT}" \
 ${REVISION} -1 \
 ${ONLY_CARE_ABOUT}
