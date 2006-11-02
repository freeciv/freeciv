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

if SERVER
if CLIENT
MSUBDIRS = manual
else
MSUBDIRS = 
endif
else
MSUBDIRS = 
endif

if MAKE_DATA
DSUBDIRS = data
else
DSUBDIRS =
endif


SUBDIRS=	$(DSUBDIRS) intl utility common tests win32 $(SSUBDIRS) $(CSUBDIRS) po doc $(MSUBDIRS)

EXTRA_DIST=	acconfig.old			\
		autogen.sh 			\
		config.mac.h			\
		configure.ac			\
		configure.in			\
		diff_ignore			\
		amiga/Freeciv.start		\
		amiga/amigaclient.c		\
		amiga/amigaserver.c		\
		amiga/config.h			\
		amiga/declgate.h		\
		amiga/gettext.c			\
		amiga/libintl.h			\
		amiga/myregargs.h		\
		amiga/ReadMe			\
		amiga/ReadMe.2Amigas		\
		amiga/ReadMe.developer		\
		amiga/SDI_compiler.h		\
		amiga/smakefile			\
		amiga/vmakefile			\
		amiga/vsnprintf.c		\
		bootstrap/civ.in		\
		bootstrap/ser.in		\
		bootstrap/freeciv.desktop.in	\
		bootstrap/freeciv.spec.in	\
		bootstrap/undep.sh.in		\
		m4/ac_path_lib.m4		\
		m4/auth.m4			\
		m4/c99.m4			\
		m4/debug.m4			\
		m4/esd.m4			\
		m4/alsa.m4			\
		m4/freetype2.m4			\
		m4/gettext.m4			\
		m4/gettimeofday.m4		\
		m4/glib-2.0.m4			\
		m4/glib-gettext.m4		\
		m4/glib.m4			\
		m4/glibc21.m4			\
		m4/gtk-2.0.m4			\
		m4/gtk.m4			\
		m4/gtk-client.m4		\
		m4/gtk2-client.m4		\
		m4/iconv.m4			\
		m4/imlib.m4			\
		m4/isc-posix.m4			\
		m4/lcmessage.m4			\
		m4/locale.m4			\
		m4/ngettext.m4			\
		m4/no-client.m4			\
		m4/progtest.m4			\
		m4/readline.m4			\
		m4/sdl-client.m4		\
		m4/sdl.m4			\
		m4/sound.m4			\
		m4/version.m4			\
		m4/vsnprintf.m4			\
		m4/win32-client.m4		\
		m4/x.213			\
		m4/x.252			\
		m4/xaw-client.m4		\
		vms/config.h_vms		\
		vms/freeciv_h.vms		\
		vms/imlib_config.h_vms		\
		vms/readme.vms			\
		vms/vms_build.com

noinst_HEADERS= version.in

src-check:
	cd tests && $(MAKE) $(AM_MAKEFLAGS) src-check

.PHONY: src-check

