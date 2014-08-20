## Process this file with automake to produce Makefile.in

bin_PROGRAMS = freeciv-modpack

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(top_srcdir)/common $(MP_gtk_CFLAGS)

if MINGW32
MPICON = $(top_builddir)/win32/mpicon.o
else
desktopfiledir = @DESKTOPDIR@
desktopfile_DATA = \
	freeciv-modpack.desktop

appdatafiledir = @APPDATADIR@
appdatafile_DATA = \
	freeciv-modpack.appdata.xml
endif

freeciv_modpack_SOURCES = \
		download.c	\
		download.h	\
		modinst.c	\
		modinst.h	\
		mpcmdline.c	\
		mpcmdline.h

freeciv_modpack_LDFLAGS = $(MP_gtk_LDFLAGS)

freeciv_modpack_LDADD = \
		$(top_builddir)/common/libfreeciv.la \
		$(MAPIMG_WAND_LIBS) \
		$(MP_gtk_LIBS) \
		$(MPICON)
