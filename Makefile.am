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

EXTRA_DIST=	civ ser config.mac.h diff_ignore \
		amiga/amigaclient.c    \
		amiga/amigaserver.c    \
		amiga/config.h         \
		amiga/gettext.c        \
		amiga/libintl.h        \
		amiga/ReadMe           \
		amiga/ReadMe.2Amigas   \
		amiga/ReadMe.developer \
		amiga/SDI_compiler.h   \
		amiga/smakefile        \
		amiga/vmakefile        \
		amiga/vsnprintf.c      \
		vms/config.h_vms       \
		vms/freeciv_h.vms      \
		vms/imlib_config.h_vms \
		vms/readme.vms         \
		vms/vms_build.com
