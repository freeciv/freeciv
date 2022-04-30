## Process this file with automake to produce Makefile.in

## Automake should distribute config.rpath automatically - and new
## versions probably do. We still support versions that do not, so
## it's in the list below.
## It should also install it automatically when running autogen.sh.
EXTRA_DIST =	freeciv.project		\
		snapshot.project	\
		fcgui.in		\
		fcser.in		\
		fcruledit.in		\
		fcruleup.in		\
		fc_gitrev_gen.h.tmpl	\
		generate_gitrev.sh	\
		generate_langstat.sh	\
		generate_meson_run.sh	\
		langstat_core.txt	\
		langstat_nations.txt	\
		langstat_ruledit.txt	\
		org.freeciv.gtk3.desktop.in	\
		org.freeciv.gtk322.desktop.in	\
		org.freeciv.sdl2.desktop.in	\
		org.freeciv.qt.desktop.in \
		org.freeciv.server.desktop.in	\
		org.freeciv.mp.gtk3.desktop.in	\
		org.freeciv.mp.gtk4.desktop.in	\
		org.freeciv.mp.qt.desktop.in	\
		org.freeciv.ruledit.desktop.in	\
		org.freeciv.gtk3.appdata.xml.in \
		org.freeciv.gtk322.appdata.xml.in \
		org.freeciv.sdl2.appdata.xml.in \
		org.freeciv.qt.appdata.xml.in \
		org.freeciv.server.appdata.xml.in \
		org.freeciv.mp.gtk3.appdata.xml.in \
		org.freeciv.mp.gtk4.appdata.xml.in \
		org.freeciv.mp.qt.appdata.xml.in \
		org.freeciv.ruledit.appdata.xml.in \
		config.rpath		\
		Makerules.mk		\
		langnames.txt

langstat_core.txt:
	"$(top_srcdir)/bootstrap/generate_langstat.sh" core "$(top_srcdir)" "$(top_builddir)/bootstrap"

langstat_nations.txt:
	"$(top_srcdir)/bootstrap/generate_langstat.sh" nations "$(top_srcdir)" "$(top_builddir)/bootstrap"

langstat_ruledit.txt:
	"$(top_srcdir)/bootstrap/generate_langstat.sh" ruledit "$(top_srcdir)" "$(top_builddir)/bootstrap"

.PHONY: langstat_core.txt langstat_nations.txt langstat_ruledit.txt

MAINTAINERCLEANFILES = langstat_core.txt langstat_nations.txt langstat_ruledit.txt
