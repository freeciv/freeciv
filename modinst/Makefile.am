## Process this file with automake to produce Makefile.in

bin_PROGRAMS = freeciv-modpack

noinst_LTLIBRARIES = libfcmp.la

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(top_srcdir)/common $(MP_gtk_CFLAGS)

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
		modinst.h	\
		mpcmdline.c	\
		mpcmdline.h

freeciv_modpack_SOURCES = mpgui_gtk.c

freeciv_modpack_LDFLAGS = $(MP_gtk_LDFLAGS)

freeciv_modpack_LDADD = \
		libfcmp.la \
		$(top_builddir)/common/libfreeciv.la \
		$(MAPIMG_WAND_LIBS) \
		$(LUA_LIBS) $(TOLUA_LIBS) $(LUASQL_LIBS) \
		$(MP_gtk_LIBS) \
		$(MPICON)
