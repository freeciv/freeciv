## Process this file with automake to produce Makefile.in

bin_PROGRAMS = freeciv-modpack

AM_CPPFLAGS = -I$(top_srcdir)/utility $(MP_gtk_CFLAGS)

freeciv_modpack_SOURCES = \
		download.c	\
		download.h	\
		modinst.c

freeciv_modpack_LDFLAGS = $(MP_gtk_LDFLAGS)

freeciv_modpack_LDADD = \
		$(top_builddir)/common/libfreeciv.la \
		$(MAPIMG_WAND_LIBS) \
		$(MP_gtk_LIBS)
