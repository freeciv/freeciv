## Process this file with automake to produce Makefile.in
# Note: After dding a new nation file, 'make Makefile.am'

pkgdata_DATA = \
		american.ruleset \
		arab.ruleset \
		argentine.ruleset \
		australian.ruleset \
		aztec.ruleset \
		babylonian.ruleset \
		barbarian.ruleset \
		bavarian.ruleset \
		boer.ruleset \
		brazilian.ruleset \
		canadian.ruleset \
		carthaginian.ruleset \
		catalan.ruleset \
		chilean.ruleset \
		chinese.ruleset \
		cornish.ruleset \
		croatian.ruleset \
		czech.ruleset \
		danish.ruleset \
		dunedain.ruleset \
		dutch.ruleset \
		egyptian.ruleset \
		english.ruleset \
		estonian.ruleset \
		filipino.ruleset \
		finnish.ruleset \
		french.ruleset \
		german.ruleset \
		greek.ruleset \
		hungarian.ruleset \
		inca.ruleset \
		indian.ruleset \
		irish.ruleset \
		israeli.ruleset \
		italian.ruleset \
		japanese.ruleset \
		kenyan.ruleset \
		korean.ruleset \
		latvian.ruleset \
		lithuanian.ruleset \
		misc_cities.ruleset \
		mongol.ruleset \
		mordor.ruleset \
		persian.ruleset \
		polish.ruleset \
		portuguese.ruleset \
		roman.ruleset \
		russian.ruleset \
		scottish.ruleset \
		serbian.ruleset \
		silesian.ruleset \
		singaporean.ruleset \
		sioux.ruleset \
		slovenian.ruleset \
		spanish.ruleset \
		swedish.ruleset \
		thai.ruleset \
		turk.ruleset \
		ukrainian.ruleset \
		vietnamese.ruleset \
		viking.ruleset \
		welsh.ruleset \
		zulu.ruleset 

EXTRA_DIST = $(pkgdata_DATA) Makefile.am.sh

Makefile.am: Makefile.am.sh $(shell echo *.ruleset)
	sh Makefile.am.sh >Makefile.am

