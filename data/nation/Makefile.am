## Process this file with automake to produce Makefile.in
# Note: After adding a new nation file, 'make Makefile.am'

## Override automake so that "make install" puts these in proper place:
pkgdatadir = $(datadir)/@PACKAGE@/nation

pkgdata_DATA = \
		afghani.ruleset \
		american.ruleset \
		arab.ruleset \
		argentine.ruleset \
		assyrian.ruleset \
		australian.ruleset \
		austrian.ruleset \
		aztec.ruleset \
		babylonian.ruleset \
		barbarian.ruleset \
		bavarian.ruleset \
		belgian.ruleset \
		boer.ruleset \
		bosnia.ruleset \
		brazilian.ruleset \
		canadian.ruleset \
		carthaginian.ruleset \
		catalan.ruleset \
		chilean.ruleset \
		chinese.ruleset \
		columbian.ruleset \
		cornish.ruleset \
		croatian.ruleset \
		czech.ruleset \
		danish.ruleset \
		dunedain.ruleset \
		dutch.ruleset \
		egyptian.ruleset \
		elvish.ruleset \
		english.ruleset \
		estonian.ruleset \
		ethiopian.ruleset \
		filipino.ruleset \
		finnish.ruleset \
		french.ruleset \
		galician.ruleset \
		german.ruleset \
		greek.ruleset \
		hobbits.ruleset \
		hungarian.ruleset \
		icelandic.ruleset \
		inca.ruleset \
		indian.ruleset \
		indonesian.ruleset \
		irish.ruleset \
		israeli.ruleset \
		italian.ruleset \
		japanese.ruleset \
		kampuchean.ruleset \
		kenyan.ruleset \
		korean.ruleset \
		latvian.ruleset \
		lithuanian.ruleset \
		malaysian.ruleset \
		martian.ruleset \
		mexican.ruleset \
		mongol.ruleset \
		mordor.ruleset \
		nigerian.ruleset \
		observer.ruleset \
		persian.ruleset \
		phoenician.ruleset \
		polish.ruleset \
		portuguese.ruleset \
		quebecois.ruleset \
		roman.ruleset \
		russian.ruleset \
		scottish.ruleset \
		serbian.ruleset \
		silesian.ruleset \
		singaporean.ruleset \
		sioux.ruleset \
		slovenian.ruleset \
		spanish.ruleset \
		sumerian.ruleset \
		swedish.ruleset \
		swiss.ruleset \
		taiwanese.ruleset \
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

