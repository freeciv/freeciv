## Process this file with automake to produce Makefile.in

## Below used to be just 
##    SUBDIRS = @gui_sources@
## but that doesn't work properly for "make dist"

if CLIENT_GUI_GTK
GUI_SUBDIR = gui-gtk
endif
if CLIENT_GUI_XAW
GUI_SUBDIR = gui-xaw
endif
if CLIENT_GUI_BEOS
GUI_SUBDIR = gui-beos
endif
if CLIENT_GUI_STUB
GUI_SUBDIR = gui-stub
endif
if CLIENT_GUI_WIN32
GUI_SUBDIR = gui-win32
endif

## This is usually false, so "include" is not recursed into 
## by 'make', but it can be enabled in configure, and it is
## necessary to have something like this so that the include 
## directory is included for 'make dist'.
if MAKE_CLIENT_INCLUDE
CLI_INCLUDE = include
else
CLI_INCLUDE = 
endif

SUBDIRS = $(GUI_SUBDIR) $(CLI_INCLUDE)

bin_PROGRAMS = civclient

INCLUDES = -I$(srcdir)/include -I$(top_srcdir)/common -I../intl

## Above, note -I../intl instead of -I$(top_srdir/intl) is deliberate.

civclient_SOURCES = \
	cityrepdata.c	\
	cityrepdata.h	\
	civclient.c	\
	civclient.h	\
	climisc.c	\
	climisc.h	\
	clinet.c	\
	clinet.h	\
	control.c	\
	control.h	\
	helpdata.c	\
	helpdata.h	\
	packhand.c	\
	packhand.h	\
	options.c	\
	options.h	\
	tilespec.c	\
	tilespec.h

civclient_DEPENDENCIES = @gui_sources@/libguiclient.a ../common/libcivcommon.a @INTLDEPS@
civclient_LDADD        = @gui_sources@/libguiclient.a ../common/libcivcommon.a @INTLLIBS@ @gui_sources@/libguiclient.a ../common/libcivcommon.a @CLIENT_LIBS@
