## Process this file with automake to produce Makefile.in

## Below used to be just 
##    SUBDIRS = @gui_sources@
## but that doesn't work properly for "make dist"

if CLIENT_GUI_GTK
GUI_SUBDIR = gui-gtk
else
GUI_SUBDIR = gui-xaw
endif

## This is always false, so "include" is never recursed into 
## by 'make', but is included in the dist.
if CLIENT_INCLUDE_DIST
CLI_INCLUDE = include
else
CLI_INCLUDE = 
endif

SUBDIRS = $(GUI_SUBDIR) $(CLI_INCLUDE)

bin_PROGRAMS = civclient

INCLUDES = -I$(srcdir)/include -I$(top_srcdir)/common -I$(srcdir)/@gui_sources@ @CLIENT_CFLAGS@

civclient_SOURCES = \
	civclient.c	\
	climisc.c	\
	packhand.c	\
	options.c

civclient_DEPENDENCIES = @gui_sources@/libguiclient.a ../common/libcivcommon.a
civclient_LDADD        = @gui_sources@/libguiclient.a ../common/libcivcommon.a @CLIENT_LIBS@
