## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4 -I dependencies/m4
AM_DIST_FORMATS = gzip xz zip

if CLIENT
CSUBDIRS = client
else
CSUBDIRS = 
endif

if SERVER
SSUBDIRS = ai server
else
if RULEDIT
SSUBDIRS = ai server
else
SSUBDIRS =
endif
endif

if MAKE_DATA
DSUBDIRS = data
else
DSUBDIRS =
endif

SUBDIRS = $(DSUBDIRS) dependencies utility common tests win32 $(SSUBDIRS) $(CSUBDIRS) translations doc tools gen_headers

## Automake should distribute config.rpath automatically - and new
## versions probably do. We still support versions that do not, so
## it's in the list below.
## It should also install it automatically when running autogen.sh.
EXTRA_DIST=	autogen.sh 			\
		configure.ac			\
		bootstrap/fcgui.in		\
		bootstrap/fcser.in		\
		bootstrap/fcruledit.in		\
		bootstrap/fc_svnrev_gen.h.tmpl	\
		bootstrap/generate_svnrev.sh	\
		bootstrap/fc_gitrev_gen.h.tmpl	\
		bootstrap/generate_gitrev.sh	\
		bootstrap/freeciv.desktop.in	\
		bootstrap/freeciv-gtk2.desktop.in \
		bootstrap/freeciv-sdl.desktop.in \
		bootstrap/freeciv-sdl2.desktop.in \
		bootstrap/freeciv-qt.desktop.in \
		bootstrap/freeciv-server.desktop.in	\
		bootstrap/freeciv-mp-gtk2.desktop.in	\
		bootstrap/freeciv-mp-gtk3.desktop.in	\
		bootstrap/freeciv-mp-qt.desktop.in	\
		bootstrap/freeciv-ruledit.desktop.in	\
		bootstrap/freeciv-gtk2.appdata.xml.in \
		bootstrap/freeciv-gtk3.appdata.xml.in \
		bootstrap/freeciv-sdl.appdata.xml.in \
		bootstrap/freeciv-sdl2.appdata.xml.in \
		bootstrap/freeciv-qt.appdata.xml.in \
		bootstrap/freeciv-server.appdata.xml.in \
		bootstrap/freeciv-mp-gtk2.appdata.xml.in \
		bootstrap/freeciv-mp-gtk3.appdata.xml.in \
		bootstrap/freeciv-mp-qt.appdata.xml.in \
		bootstrap/freeciv-ruledit.appdata.xml.in \
		bootstrap/freeciv.spec.in	\
		bootstrap/config.rpath		\
		bootstrap/Makerules.mk		\
		bootstrap/langnames.txt		\
		m4/c99.m4			\
		m4/c11.m4			\
		m4/c++11.m4			\
		m4/codeset.m4			\
		m4/compiler.m4			\
		m4/debug.m4			\
		m4/fcdb-mysql.m4		\
		m4/fcdb-postgres.m4		\
		m4/fcdb-sqlite3.m4		\
		m4/features.m4			\
		m4/freetype2.m4			\
		m4/gettext.m4			\
		m4/gettimeofday.m4		\
		m4/gtk-2.0.m4			\
		m4/gtk-3.0.m4			\
		m4/gtk2-client.m4		\
		m4/gtk3-client.m4		\
		m4/iconv.m4			\
		m4/lib-ld.m4			\
		m4/lib-link.m4			\
		m4/lib-prefix.m4		\
		m4/locale.m4			\
		m4/mysql.m4			\
		m4/magickwand.m4		\
		m4/mapimg-magickwand.m4		\
		m4/nls.m4			\
		m4/no-client.m4			\
		m4/pkg.m4			\
		m4/po.m4			\
		m4/postgres.m4			\
		m4/progtest.m4			\
		m4/qt5.m4			\
		m4/qt-client.m4			\
		m4/readline.m4			\
		m4/sdl-client.m4		\
		m4/sdl.m4			\
		m4/sdl2-client.m4		\
		m4/sdl2.m4			\
		m4/sqlite3.m4			\
		m4/sound.m4			\
		m4/vsnprintf.m4			\
		m4/web-client.m4		\
		m4/xaw-client.m4		\
		m4/x.m4				\
		scripts/mapimg2anim		\
		scripts/setup_auth_server.sh	\
		scripts/diff_ignore
# On branch: add NEWS-x.x here and remove this comment (1 of 2)

noinst_HEADERS= fc_version

docs =			\
	ABOUT-NLS	\
	AUTHORS		\
	ChangeLog	\
	COPYING		\
	INSTALL		\
	NEWS
# On branch: add NEWS-x.x here and remove this comment (2 of 2)

doc_DATA = $(docs)

src-check:
	cd tests && $(MAKE) $(AM_MAKEFLAGS) src-check

doc:
	doxy_srcdir="$(top_srcdir)/" doxygen $(top_srcdir)/doc/freeciv.doxygen

.PHONY: src-check doc
