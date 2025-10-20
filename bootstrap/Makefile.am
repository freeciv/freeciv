## Process this file with automake to produce Makefile.in

metainfofiledir = @METAINFODIR@
desktopfiledir  = @DESKTOPDIR@

## Automake should distribute config.rpath automatically - and new
## versions probably do. We still support versions that do not, so
## it's in the list below.
## It should also install it automatically when running autogen.sh.
EXTRA_DIST =	freeciv.project		\
		freeciv.fcproj		\
		fcgui.in		\
		fcser.in		\
		fcruledit.in		\
		fcruleup.in		\
		fc_gitrev_gen.h.tmpl	\
		generate_gitrev.sh	\
		generate_langstat.sh	\
		generate_meson_run.sh	\
		generate_metainfo.sh	\
		org.freeciv.gtk322.desktop	\
		org.freeciv.gtk4.desktop	\
		org.freeciv.gtk4x.desktop	\
		org.freeciv.sdl2.desktop	\
		org.freeciv.sdl3.desktop	\
		org.freeciv.qt.desktop \
		org.freeciv.server.desktop	\
		org.freeciv.gtk3.mp.desktop	\
		org.freeciv.gtk4.mp.desktop	\
		org.freeciv.gtk4x.mp.desktop	\
		org.freeciv.qt.mp.desktop	\
		org.freeciv.ruledit.desktop	\
		org.freeciv.gtk322.metainfo.xml.in \
		org.freeciv.gtk4.metainfo.xml.in \
		org.freeciv.gtk4x.metainfo.xml.in \
		org.freeciv.sdl2.metainfo.xml.in \
		org.freeciv.sdl3.metainfo.xml.in \
		org.freeciv.qt.metainfo.xml.in \
		org.freeciv.server.metainfo.xml.in \
		org.freeciv.gtk3.mp.metainfo.xml.in \
		org.freeciv.gtk4.mp.metainfo.xml.in \
		org.freeciv.gtk4x.mp.metainfo.xml.in \
		org.freeciv.qt.mp.metainfo.xml.in \
		org.freeciv.ruledit.metainfo.xml.in \
		config.rpath		\
		Makerules.mk		\
		langnames.txt

metainfofile_DATA =
desktopfile_DATA  =

if SERVER
metainfofile_DATA += org.freeciv.server.metainfo.xml
desktopfile_DATA  += org.freeciv.server.desktop
endif

if CLIENT_GUI_SDL2
metainfofile_DATA += org.freeciv.sdl2.metainfo.xml
desktopfile_DATA  += org.freeciv.sdl2.desktop
endif

if CLIENT_GUI_GTK_3_22
metainfofile_DATA += org.freeciv.gtk322.metainfo.xml
desktopfile_DATA  += org.freeciv.gtk322.desktop
endif

if CLIENT_GUI_GTK_4
metainfofile_DATA += org.freeciv.gtk4.metainfo.xml
desktopfile_DATA  += org.freeciv.gtk4.desktop
endif

if CLIENT_GUI_GTK_5
metainfofile_DATA += org.freeciv.gtk4x.metainfo.xml
desktopfile_DATA  += org.freeciv.gtk4x.desktop
endif

if CLIENT_GUI_QT
metainfofile_DATA += org.freeciv.qt.metainfo.xml
desktopfile_DATA  += org.freeciv.qt.desktop
endif

if MODINST_GTK3
metainfofile_DATA += org.freeciv.gtk3.mp.metainfo.xml
desktopfile_DATA  += org.freeciv.gtk3.mp.desktop
endif

if MODINST_GTK4
metainfofile_DATA += org.freeciv.gtk4.mp.metainfo.xml
desktopfile_DATA  += org.freeciv.gtk4.mp.desktop
endif

if MODINST_QT
metainfofile_DATA += org.freeciv.qt.mp.metainfo.xml
desktopfile_DATA  += org.freeciv.qt.mp.desktop
endif

if RULEDIT
metainfofile_DATA += org.freeciv.ruledit.metainfo.xml
desktopfile_DATA  += org.freeciv.ruledit.desktop
endif

langstat_core.txt:
	"$(top_srcdir)/bootstrap/generate_langstat.sh" core "$(top_srcdir)" "$(top_builddir)/bootstrap"

langstat_nations.txt:
	"$(top_srcdir)/bootstrap/generate_langstat.sh" nations "$(top_srcdir)" "$(top_builddir)/bootstrap"

langstat_ruledit.txt:
	"$(top_srcdir)/bootstrap/generate_langstat.sh" ruledit "$(top_srcdir)" "$(top_builddir)/bootstrap"

$(metainfofile_DATA) : %.metainfo.xml: %.metainfo.xml.in generate_metainfo.sh $(top_srcdir)/fc_version
	@if ! $(AM_V_P) ; then echo "  metainfo $@" ; fi
	$(AM_V_at) "$(top_srcdir)/bootstrap/generate_metainfo.sh" "$@" "$(top_builddir)/bootstrap"

metainfo_stamp: $(metainfofile_DATA)

.PHONY: langstat_core.txt langstat_nations.txt langstat_ruledit.txt metainfo_stamp

all-local: metainfo_stamp

MAINTAINERCLEANFILES = langstat_core.txt langstat_nations.txt langstat_ruledit.txt metainfo_stamp

CLEANFILES = $(metainfofile_DATA)
