## Process this file with automake to produce Makefile.in

include $(top_srcdir)/bootstrap/Makerules.mk

bin_PROGRAMS = 

if SERVER
if CLIENT
bin_PROGRAMS += freeciv-manual
endif
endif

if MODINST_GTK
bin_PROGRAMS += freeciv-modpack
endif
if MODINST_QT
bin_PROGRAMS += freeciv-mp-qt
endif

if MODINST
noinst_LTLIBRARIES = libfcmp.la
endif

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/client \
	-I$(top_srcdir)/client/include \
	$(MP_gtk_CFLAGS) \
	$(MP_qt_CPPFLAGS)

if MINGW32
MPICON = $(top_builddir)/win32/mpicon.o
else
desktopfiledir = $(prefix)/share/applications
desktopfile_DATA = \
	freeciv-modpack.desktop
endif
freeciv_manual_SOURCES = \
		civmanual.c

# This is a bit of a hack.  The program links in with the server lib
# but also uses some files from the client.
EXTRA_freeciv_manual_DEPENDENCIES = \
 $(top_builddir)/client/helpdata.lo

freeciv_manual_LDFLAGS = $(GGZDMOD_LDFLAGS)

freeciv_manual_LDADD = \
 $(top_builddir)/server/libfreeciv-srv.la \
 ../client/helpdata.lo \
 $(top_builddir)/common/libfreeciv.la \
 $(LUA_LIBS) $(TOLUA_LIBS) $(LUASQL_LIBS) \
 $(INTLLIBS)  $(MAPIMG_WAND_LIBS) \
 $(FCDB_MYSQL_LIBS) $(FCDB_POSTGRES_LIBS) $(FCDB_SQLITE3_LIBS) \
 $(SERVER_LIBS) $(LIB_GGZDMOD)

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
		$(LUA_LIBS) $(TOLUA_LIBS) $(LUASQL_LIBS) \
		$(MAPIMG_WAND_LIBS) \
		$(MP_gtk_LIBS) \
		$(MPICON)
endif

MOC_FILES = meta_mpgui_qt.cpp

if MODINST_QT

freeciv_mp_qt_SOURCES = \
	mpgui_qt.cpp	\
	mpgui_qt.h	\
	$(MOC_FILES)

CLEANFILES = $(MOC_FILES)

freeciv_mp_qt_LDADD = \
		libfcmp.la \
		$(top_builddir)/common/libfreeciv.la \
		$(LUA_LIBS) $(TOLUA_LIBS) $(LUASQL_LIBS) \
		$(MAPIMG_WAND_LIBS) \
		$(MP_qt_LIBS) \
		$(MPICON)
endif
