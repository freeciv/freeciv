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

SUBDIRS=	data common $(SSUBDIRS) $(CSUBDIRS)

EXTRA_DIST=	AUTHORS COPYING ChangeLog HOWTOPLAY \
		INSTALL Imakefile Makefile.in Makefile.noimake \
		NEWS README README.AI \
		acconfig.h config.h.in configure configure.in \
		acinclude.m4 install-sh mkinstalldirs \
		civ ser freeciv_hackers_guide.txt
