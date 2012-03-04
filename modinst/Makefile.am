## Process this file with automake to produce Makefile.in

bin_PROGRAMS =

if MODINST_GTK
bin_PROGRAMS += freeciv-modpack
endif
if MODINST_QT
bin_PROGRAMS += freeciv-mp-qt
endif

noinst_LTLIBRARIES = libfcmp.la

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(top_srcdir)/common $(MP_gtk_CFLAGS) $(MP_qt_CPPFLAGS)

if MINGW32
MPICON = $(top_builddir)/win32/mpicon.o
else
desktopfiledir = $(prefix)/share/applications
desktopfile_DATA = \
	freeciv-modpack.desktop
endif

libfcmp_la_SOURCES = \
		download.c	\
		download.h	\
		modinst.c	\
		modinst.h	\
		mpcmdline.c	\
		mpcmdline.h	\
		mpdb.c		\
		mpdb.h

if MODINST_GTK
freeciv_modpack_SOURCES = mpgui_gtk.c

freeciv_modpack_LDFLAGS = $(MP_gtk_LDFLAGS)

freeciv_modpack_LDADD = \
		libfcmp.la \
		$(top_builddir)/common/libfreeciv.la \
		$(MAPIMG_WAND_LIBS) \
		$(LUA_LIBS) $(TOLUA_LIBS) $(LUASQL_LIBS) \
		$(MP_gtk_LIBS) \
		$(MPICON)
endif

if MODINST_QT

MOC_FILES = meta_mpgui_qt.cpp

freeciv_mp_qt_SOURCES = \
	mpgui_qt.cpp	\
	mpgui_qt.h	\
	$(MOC_FILES)

CLEANFILES = $(MOC_FILES)

freeciv_mp_qt_LDADD = \
		libfcmp.la \
		$(top_builddir)/common/libfreeciv.la \
		$(MAPIMG_WAND_LIBS) \
		$(LUA_LIBS) $(TOLUA_LIBS) $(LUASQL_LIBS) \
		$(MP_qt_LIBS) \
		$(MPICON)
endif

# Generate meta object source from header
meta_%.cpp: %.h
	moc -o $@ $<
