## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4 -I dependencies/m4
AM_DIST_FORMATS = gzip bzip2 zip

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

SUBDIRS = $(DSUBDIRS) dependencies utility common tests win32 $(SSUBDIRS) $(CSUBDIRS) translations doc tools gen_headers lua bootstrap

EXTRA_DIST =	autogen.sh 			\
		configure.ac			\
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
		m4/gtk2-client.m4		\
		m4/gtk3.22-client.m4		\
		m4/gtk3-client.m4		\
		m4/iconv.m4			\
		m4/intlmacosx.m4		\
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
		m4/qt5-darwin.m4		\
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
		m4/winsock2.m4			\
		m4/xaw-client.m4		\
		m4/x.m4				\
		m4/testmatic.m4			\
		scripts/mapimg2anim		\
		scripts/setup_auth_server.sh	\
		scripts/diff_ignore		\
		scripts/freeciv.supp		\
		NEWS-2.6

noinst_HEADERS= fc_version

docs =			\
	ABOUT-NLS	\
	AUTHORS		\
	ChangeLog	\
	COPYING		\
	INSTALL		\
	NEWS		\
	NEWS-2.6

doc_DATA = $(docs)

src-check:
	cd tests && $(MAKE) $(AM_MAKEFLAGS) src-check

doc:
	doxy_srcdir="$(top_srcdir)/" doxygen $(top_srcdir)/doc/freeciv.doxygen

.PHONY: src-check doc
