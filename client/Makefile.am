## Process this file with automake to produce Makefile.in

SUBDIRS = @gui_sources@

bin_PROGRAMS = civclient

INCLUDES = -Iinclude -I$(top_srcdir)/common -I@gui_sources@ @CLIENT_CFLAGS@

civclient_SOURCES = \
	civclient.c	\
	packhand.c

civclient_DEPENDENCIES = @gui_sources@/libguiclient.a ../common/libcivcommon.a
civclient_LDADD        = @gui_sources@/libguiclient.a ../common/libcivcommon.a @CLIENT_LIBS@
