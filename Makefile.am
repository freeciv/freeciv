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

SUBDIRS=	$(DSUBDIRS) common $(SSUBDIRS) $(CSUBDIRS)

EXTRA_DIST=	PEOPLE BUGS HOWTOPLAY README.AI README.rulesets \
		civ ser freeciv_hackers_guide.txt
