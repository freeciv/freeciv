## Process this file with automake to produce Makefile.in

if CLIENT
CSUBDIRS = client
else
CSUBDIRS = 
endif

if SERVER
SSUBDIRS = ai server
else
SSUBDIRS =
endif

if MAKE_DATA
DSUBDIRS = data
else
DSUBDIRS =
endif


SUBDIRS=	$(DSUBDIRS) intl common $(SSUBDIRS) $(CSUBDIRS) po doc

EXTRA_DIST=	PEOPLE BUGS HOWTOPLAY freeciv_hackers_guide.txt \
		README.AI README.rulesets README.graphics \
		civ ser config.mac.h
