## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4 -I dependencies/m4

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

if MODINST
MODSUBDIRS = modinst
else
MODSUBDIRS =
endif

SUBDIRS = $(DSUBDIRS) dependencies utility common tests win32 $(SSUBDIRS) $(CSUBDIRS) po doc $(MSUBDIRS) $(MODSUBDIRS)

## Automake should distribute config.rpath automatically - and new
## versions probably do. We still support versions that do not, so
## it's in the list below.
## It should also install it automatically when running autogen.sh.
EXTRA_DIST=	autogen.sh 			\
		config.mac.h			\
		configure.ac			\
		diff_ignore			\
		bootstrap/fcgui.in		\
		bootstrap/fcser.in		\
		bootstrap/fc_svnrev_gen.h.in	\
		bootstrap/generate_svnrev.sh	\
		bootstrap/freeciv.desktop.in	\
		bootstrap/freeciv-server.desktop.in	\
		bootstrap/freeciv-modpack.desktop.in	\
		bootstrap/freeciv.spec.in	\
		bootstrap/config.rpath		\
		bootstrap/langnames.txt		\
		m4/ac_path_lib.m4		\
		m4/alsa.m4			\
		m4/c99.m4			\
		m4/codeset.m4			\
		m4/compiler.m4			\
		m4/debug.m4			\
		m4/fcdb-mysql.m4		\
		m4/fcdb-postgres.m4		\
		m4/fcdb-sqlite3.m4		\
		m4/freetype2.m4			\
		m4/gettimeofday.m4		\
		m4/ggz.m4			\
		m4/glib-2.0.m4			\
		m4/glibc21.m4			\
		m4/glib-gettext.m4		\
		m4/glib.m4			\
		m4/gtk-2.0.m4			\
		m4/gtk-3.0.m4			\
		m4/gtk2-client.m4		\
		m4/gtk3-client.m4		\
		m4/iconv.m4			\
		m4/imlib.m4			\
		m4/isc-posix.m4			\
		m4/lcmessage.m4			\
		m4/lib-ld.m4			\
		m4/lib-link.m4			\
		m4/lib-prefix.m4		\
		m4/locale.m4			\
		m4/mysql.m4			\
		m4/magickwand.m4		\
		m4/mapimg-magickwand.m4		\
		m4/ngettext.m4			\
		m4/no-client.m4			\
		m4/pkg.m4			\
		m4/postgres.m4			\
		m4/progtest.m4			\
		m4/qt-client.m4			\
		m4/readline.m4			\
		m4/sdl-client.m4		\
		m4/sdl.m4			\
		m4/sqlite3.m4			\
		m4/sound.m4			\
		m4/vsnprintf.m4			\
		m4/xaw-client.m4		\
		m4/x.m4				\
		vms/config.h_vms		\
		vms/freeciv_h.vms		\
		vms/imlib_config.h_vms		\
		vms/readme.vms			\
		vms/vms_build.com		\
		scripts/mapimg2anim		\
		scripts/setup_auth_server.sh	\
		NEWS-2.4

noinst_HEADERS= fc_version

src-check:
	cd tests && $(MAKE) $(AM_MAKEFLAGS) src-check

.PHONY: src-check
