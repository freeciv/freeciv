## Process this file with automake to produce Makefile.in

## Override automake so that "make install" puts these in proper place:
pkgdatadir = $(datadir)/$(PACKAGE)/alien

SUBDIRS = nation

pkgdata_DATA =          	\
	actions.ruleset		\
	buildings.ruleset 	\
	cities.ruleset		\
	effects.ruleset		\
	nation_effects.ruleset	\
	script.lua		\
	parser.lua		\
	styles.ruleset		\
	game.ruleset		\
	governments.ruleset	\
	nations.ruleset		\
	techs.ruleset		\
	terrain.ruleset		\
	units.ruleset		\
	README.alien

EXTRA_DIST = $(pkgdata_DATA)
