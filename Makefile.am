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

EXTRA_DIST=	acconfig.h			\
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
		debian/INSTALL.debian		\
		debian/README.debian		\
		debian/build.debian		\
		debian/changelog		\
		debian/civclient.6		\
		debian/civserver.6		\
		debian/control			\
		debian/copyright		\
		debian/dirs			\
		debian/docs			\
		debian/freeciv-gtk.dirs		\
		debian/freeciv-gtk.menu		\
		debian/freeciv-gtk.postinst	\
		debian/freeciv-gtk.postrm	\
		debian/freeciv-xaw3d.dirs	\
		debian/freeciv-xaw3d.menu	\
		debian/freeciv-xaw3d.postinst	\
		debian/freeciv-xaw3d.postrm	\
		debian/freeciv.desktop		\
		debian/menu			\
		debian/rules			\
		debian/watch			\
		m4/ac_path_lib.m4		\
		m4/esd.m4			\
		m4/gettext.m4			\
		m4/glib-2.0.m4			\
		m4/glib-gettext.m4		\
		m4/glib.m4			\
		m4/glibc.m4			\
		m4/gtk-2.0.m4			\
		m4/gtk.m4			\
		m4/iconv.m4			\
		m4/imlib.m4			\
		m4/langinfo.m4			\
		m4/lcmessage.m4			\
		m4/ngettext.m4			\
		m4/posix.m4			\
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
