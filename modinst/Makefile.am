## Process this file with automake to produce Makefile.in

bin_PROGRAMS = freeciv-modpack

AM_CPPFLAGS = -I$(top_srcdir)/utility $(MP_gtk2_CFLAGS)

freeciv_modpack_SOURCES = \
		download.c	\
		download.h	\
		modinst.c

freeciv_modpack_LDFLAGS = $(MP_gtk2_LDFLAGS)

freeciv_modpack_LDADD = $(top_builddir)/common/libfreeciv.la $(MP_gtk2_LIBS)
