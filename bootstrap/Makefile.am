## Process this file with automake to produce Makefile.in

metainfofiledir = @METAINFODIR@

## Automake should distribute config.rpath automatically - and new
## versions probably do. We still support versions that do not, so
## it's in the list below.
## It should also install it automatically when running autogen.sh.
EXTRA_DIST =	freeciv.project		\
		snapshot.project	\
		http.project		\
		fcgui.in		\
		fcser.in		\
		fcruledit.in		\
		fcruleup.in		\
		fc_gitrev_gen.h.tmpl	\
		generate_gitrev.sh	\
		generate_langstat.sh	\
		langstat_core.txt	\
		langstat_nations.txt	\
		langstat_ruledit.txt	\
		org.freeciv.gtk3.desktop.in	\
		org.freeciv.gtk2.desktop.in	\
		org.freeciv.gtk322.desktop.in	\
		org.freeciv.sdl2.desktop.in	\
		org.freeciv.qt.desktop.in	\
		org.freeciv.server.desktop.in	\
		org.freeciv.mp.gtk2.desktop.in	\
		org.freeciv.mp.gtk3.desktop.in	\
		org.freeciv.mp.gtk4.desktop.in	\
		org.freeciv.mp.qt.desktop.in	\
		org.freeciv.ruledit.desktop.in	\
		org.freeciv.gtk2.appdata.xml.in \
		org.freeciv.gtk3.appdata.xml.in \
		org.freeciv.gtk322.appdata.xml.in \
		org.freeciv.sdl2.appdata.xml.in \
		org.freeciv.qt.appdata.xml.in \
		org.freeciv.server.appdata.xml.in \
		org.freeciv.mp.gtk2.appdata.xml.in \
		org.freeciv.mp.gtk3.appdata.xml.in \
		org.freeciv.mp.gtk4.appdata.xml.in \
		org.freeciv.mp.qt.appdata.xml.in \
		org.freeciv.ruledit.appdata.xml.in \
		config.rpath		\
		Makerules.mk		\
		langnames.txt

metainfofile_DATA =

if SERVER
metainfofile_DATA += org.freeciv.server.appdata.xml
endif

if CLIENT_GUI_SDL2
metainfofile_DATA += org.freeciv.sdl2.appdata.xml
endif

if CLIENT_GUI_GTK_2_0
metainfofile_DATA += org.freeciv.gtk2.appdata.xml
endif

if CLIENT_GUI_GTK_3_0
metainfofile_DATA += org.freeciv.gtk3.appdata.xml
endif

if CLIENT_GUI_GTK_3_22
metainfofile_DATA += org.freeciv.gtk322.appdata.xml
endif

if CLIENT_GUI_QT
metainfofile_DATA += org.freeciv.qt.appdata.xml
endif

if MODINST_GTK2
metainfofile_DATA += org.freeciv.mp.gtk2.appdata.xml
endif

if MODINST_GTK3
metainfofile_DATA += org.freeciv.mp.gtk3.appdata.xml
endif

if MODINST_GTK4
metainfofile_DATA += org.freeciv.mp.gtk4.appdata.xml
endif

if MODINST_QT
metainfofile_DATA += org.freeciv.mp.qt.appdata.xml
endif

if RULEDIT
metainfofile_DATA += org.freeciv.ruledit.appdata.xml
endif

langstat_core.txt:
	"$(top_srcdir)/bootstrap/generate_langstat.sh" core "$(top_srcdir)" "$(top_builddir)"

langstat_nations.txt:
	"$(top_srcdir)/bootstrap/generate_langstat.sh" nations "$(top_srcdir)" "$(top_builddir)"

langstat_ruledit.txt:
	"$(top_srcdir)/bootstrap/generate_langstat.sh" ruledit "$(top_srcdir)" "$(top_builddir)"

.PHONY: langstat_core.txt langstat_nations.txt langstat_ruledit.txt

MAINTAINERCLEANFILES = langstat_core.txt langstat_nations.txt langstat_ruledit.txt
