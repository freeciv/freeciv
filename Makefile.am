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

EXTRA_DIST=	acconfig.old			\
		autogen.sh 			\
		civ				\
		config.mac.h			\
		configure.ac			\
		configure.in			\
		diff_ignore			\
		ser				\
		amiga/Freeciv.start		\
		amiga/amigaclient.c		\
		amiga/amigaserver.c		\
		amiga/config.h			\
		amiga/gettext.c			\
		amiga/libintl.h			\
		amiga/ReadMe			\
		amiga/ReadMe.2Amigas		\
		amiga/ReadMe.developer		\
		amiga/SDI_compiler.h		\
		amiga/smakefile			\
		amiga/vmakefile			\
		amiga/vsnprintf.c		\
		m4/ac_path_lib.m4		\
		m4/locale.m4			\
		m4/debug.m4			\
		m4/esd.m4			\
		m4/gettext.m4			\
		m4/glib-2.0.m4			\
		m4/glib-gettext.m4		\
		m4/glib.m4			\
		m4/glibc21.m4			\
		m4/gtk-2.0.m4			\
		m4/gtk.m4			\
		m4/iconv.m4			\
		m4/imlib.m4			\
		m4/isc-posix.m4			\
		m4/lcmessage.m4			\
		m4/lib-ld.m4			\
		m4/lib-link.m4			\
		m4/lib-prefix.m4		\
		m4/ngettext.m4			\
		m4/progtest.m4			\
		m4/readline.m4			\
		m4/sdl.m4			\
		m4/sound.m4			\
		m4/vsnprintf.m4			\
		m4/x.213			\
		m4/x.252			\
		vms/config.h_vms		\
		vms/freeciv_h.vms		\
		vms/imlib_config.h_vms		\
		vms/readme.vms			\
		vms/vms_build.com
