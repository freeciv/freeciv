## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4 -I dependencies/m4
AM_DIST_FORMATS = gzip xz zip

if SRV_LIB
SSUBDIRS = ai server
else
SSUBDIRS =
endif

if MAKE_DATA
DSUBDIRS = data
else
DSUBDIRS =
endif

SUBDIRS = gen_headers $(DSUBDIRS) dependencies utility common tests windows $(SSUBDIRS) client translations doc tools lua bootstrap

docs =			\
	ABOUT-NLS	\
	AUTHORS		\
	ChangeLog	\
	ChangeLog-1.0-S3_0 \
	COPYING		\
	INSTALL		\
	NEWS		\
	NEWS-3.2

EXTRA_DIST =	autogen.sh 			\
		configure.ac			\
		m4/ax_cxx_compile_stdcxx.m4	\
		m4/build-to-host.m4		\
		m4/c99.m4			\
		m4/c11.m4			\
		m4/c23.m4			\
		m4/c++11.m4			\
		m4/c++20.m4			\
		m4/codeset.m4			\
		m4/compiler.m4			\
		m4/debug.m4			\
		m4/expanddir.m4			\
		m4/fc-sdl2.m4			\
		m4/fcdb-mysql.m4		\
		m4/fcdb-odbc.m4			\
		m4/fcdb-postgres.m4		\
		m4/fcdb-sqlite3.m4		\
		m4/features.m4			\
		m4/gettext.m4			\
		m4/gettimeofday.m4		\
		m4/gtk3.22-client.m4		\
		m4/gtk4-client.m4		\
		m4/host-cpu-c-abi.m4		\
		m4/iconv.m4			\
		m4/intl.m4			\
		m4/intlmacosx.m4		\
		m4/lib-ld.m4			\
		m4/lib-link.m4			\
		m4/lib-prefix.m4		\
		m4/locale.m4			\
		m4/mysql.m4			\
		m4/magickwand.m4		\
		m4/mapimg-magickwand.m4		\
		m4/multiarch.m4			\
		m4/nls.m4			\
		m4/no-client.m4			\
		m4/pkg.m4			\
		m4/po.m4			\
		m4/postgres.m4			\
		m4/progtest.m4			\
		m4/qt.m4			\
		m4/qt5.m4			\
		m4/qt6.m4			\
		m4/qt5-darwin.m4		\
		m4/qt6-darwin.m4		\
		m4/qt-client.m4			\
		m4/readline.m4			\
		m4/sdl2-client.m4		\
		m4/sqlite3.m4			\
		m4/sound.m4			\
                m4/tcc.m4			\
		m4/vsnprintf.m4			\
		m4/web-client.m4		\
		m4/winsock2.m4			\
		m4/testmatic.m4			\
		meson.build			\
		meson_options.txt		\
		platforms/appimage/build_appimages.sh		\
		platforms/emscripten/emsbuild.sh		\
		platforms/emscripten/emssetup.sh		\
		platforms/emscripten/setups/cross-ems.tmpl	\
		platforms/flatpak/build_flatpak.sh	\
		platforms/flatpak/org.freeciv.gtk322.yml	\
		platforms/flatpak/org.freeciv.gtk4.yml	\
		platforms/flatpak/org.freeciv.gtk3.mp.yml	\
		platforms/flatpak/org.freeciv.gtk4.mp.yml	\
		platforms/flatpak/org.freeciv.qt.yml	\
		platforms/flatpak/org.freeciv.qt.mp.yml	\
		platforms/flatpak/org.freeciv.sdl2.yml	\
		platforms/flatpak/org.freeciv.ruledit.yml	\
		platforms/macos/homebrew-appbundle.sh \
		scripts/generate_doc.sh		\
		scripts/mapimg2anim		\
		scripts/setup_auth_server.sh	\
		scripts/replace			\
		scripts/diff_ignore		\
		scripts/freeciv.supp		\
		$(docs)

noinst_HEADERS = fc_version

doc_DATA = $(docs)

src-check:
	cd tests && $(MAKE) $(AM_MAKEFLAGS) src-check

doc:
	"$(top_srcdir)/scripts/generate_doc.sh" "$(top_srcdir)"

.PHONY: src-check doc
