## Process this file with automake to produce Makefile.in

## Override automake so that "make install" puts these in proper place:
pkgdatadir = $(datadir)/@PACKAGE@/nation

pkgdata_DATA = $(shell ls -1 *.ruleset)

EXTRA_DIST = $(pkgdata_DATA)
