#!/usr/bin/env bash
#/***********************************************************************
# Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#***********************************************************************/

SRCDIR="$(dirname "$0")"

if test "$1" = "-h" || test "$1" = "--help" ; then
  echo "Usage: $(basename $0) [domain=all]"
  exit
fi

DOMAINLIST_FULL="core nations ruledit"

cd "${SRCDIR}"

if test "$1" != "" && test "$1" != "all" ; then
  for domain in $1
  do
    domain_found=no
    for other_domain in ${DOMAINLIST_FULL}
    do
      if test ${domain} = ${other_domain} ; then
        domain_found=yes
      fi
    done
    if test ${domain_found} != yes ; then
      echo "Unknown domain \"${domain}\" given!" >&2
      exit 1
    fi
  done
  DOMAINLIST="$1"
else
  DOMAINLIST="${DOMAINLIST_FULL}"
fi

VERSION_SCRIPT_SILENT=yes . ../fc_version

for domain in ${DOMAINLIST}
do
  if ! ( if ! cd "${domain}" ; then
      echo "Failed to move to \"${domain}\" directory!" >&2
      exit 1
    fi
    if test "${domain}" = "ruledit" ; then
      KW_BASIC="R__"
      KW_Q="RQ_"
    else
      KW_BASIC="_"
      KW_Q="Q_"
    fi
    if ! xgettext --default-domain="freeciv-${domain}" --directory=../.. \
                  --add-comments=TRANSLATORS: \
                  --files-from=POTFILES.in \
                  --copyright-holder='Freeciv Project' \
                  --package-name="Freeciv" \
                  --package-version="${VERSION_STRING}${VERSION_REV}" \
                  --msgid-bugs-address="https://redmine.freeciv.org/projects/freeciv" \
                  --keyword="${KW_BASIC}" --flag=_:1:pass-c-format --keyword=N_ \
                  --flag=N_:1:pass-c-format --flag=error:3:c-format \
                  --flag=error_at_line:5:c-format --flag=asprintf:2:c-format \
                  --flag=vasprintf:2:c-format --flag=xasprintf:1:c-format \
                  --flag=error_logger:1:c-format --flag=po_error:3:c-format \
                  --flag=po_error_at_line:5:c-format --flag=po_gram_error:1:c-format \
                  --flag=po_gram_error_at_line:2:c-format --keyword="${KW_Q}" \
                  --keyword=PL_:1,2 --language=C --escape --add-comments=TRANS: \
                  --from-code=UTF-8 ||
       ! mv "freeciv-${domain}.po" "freeciv-${domain}.pot"
    then
      echo "Failed to create freeciv-${domain}.pot" >&2
      exit 1
    fi
    if ! cat "LINGUAS" | \
      ( while read LANG
        do
          if ! msgmerge --quiet --previous --lang="${LANG}" "${LANG}.po" \
                        "freeciv-${domain}.pot" -o "${LANG}.new.po" ||
             ! mv "${LANG}.new.po" "${LANG}.po"
          then
            echo "Creation of \"${LANG}.po\" failed!" >&2
            exit 1
          fi
        done
      ) ; then
      exit 1
    fi
  ) ; then
    exit 1
  fi
done
