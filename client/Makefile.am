## Process this file with automake to produce Makefile.in

SUBDIRS = @gui_sources@

bin_PROGRAMS = civclient

INCLUDES = -I$(srcdir)/include -I$(top_srcdir)/common -I$(srcdir)/@gui_sources@ @CLIENT_CFLAGS@

civclient_SOURCES = \
	civclient.c	\
	climisc.c	\
	packhand.c

civclient_DEPENDENCIES = @gui_sources@/libguiclient.a ../common/libcivcommon.a
civclient_LDADD        = @gui_sources@/libguiclient.a ../common/libcivcommon.a @CLIENT_LIBS@
