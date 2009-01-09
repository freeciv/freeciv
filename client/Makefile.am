## Process this file with automake to produce Makefile.in

## Below used to be just 
##    SUBDIRS = $(gui_sources)
## but that doesn't work properly for "make dist"

noinst_LTLIBRARIES = libfreeciv-client.la

if CLIENT_GUI_SDL
GUI_SUBDIR = gui-sdl
endif
if CLIENT_GUI_GTK_2_0
GUI_SUBDIR = gui-gtk-2.0
endif
if CLIENT_GUI_XAW
GUI_SUBDIR = gui-xaw
endif
## if CLIENT_GUI_BEOS
## GUI_SUBDIR = gui-beos
## endif
if CLIENT_GUI_STUB
GUI_SUBDIR = gui-stub
endif
if CLIENT_GUI_WIN32
GUI_SUBDIR = gui-win32
endif
if CLIENT_GUI_FTWL
GUI_SUBDIR = gui-ftwl
LIBFTWL = $(top_builddir)/utility/ftwl/libftwl.la
endif

ALL_AUDIO_SDL_FILES=audio_sdl.c audio_sdl.h

if AUDIO_SDL
AUDIO_SDL_FILES=$(ALL_AUDIO_SDL_FILES)
endif

if MINGW32
CLIENTICON=../win32/clienticon.o
endif

EXTRA_DIST= $(ALL_AUDIO_SDL_FILES)

## This is usually false, so "include" is not recursed into 
## by 'make', but it can be enabled in configure, and it is
## necessary to have something like this so that the include 
## directory is included for 'make dist'.
if MAKE_CLIENT_INCLUDE
CLI_INCLUDE = include
else
CLI_INCLUDE = 
endif

SUBDIRS = $(GUI_SUBDIR) $(CLI_INCLUDE) agents

bin_PROGRAMS = civclient

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(srcdir)/include -I$(top_srcdir)/common -I$(top_srcdir)/common/aicore -I$(srcdir)/agents $(CLIENT_CFLAGS) $(SOUND_CFLAGS) $(LIBGGZ_INCLUDES) $(GGZMOD_INCLUDES) $(GGZ_GTK_INCLUDES)

# Build client common functionality in to library
# instead linking them directly to civclient. This is necessary
# for static linking.
libfreeciv_client_la_SOURCES = $(AUDIO_SDL_FILES) \
	attribute.h	\
	attribute.c	\
	citydlg_common.c \
	citydlg_common.h \
	cityrepdata.c	\
	cityrepdata.h	\
	client_main.c	\
	client_main.h	\
	chatline_common.c \
	chatline_common.h \
	connectdlg_common.c \
	connectdlg_common.h \
	climisc.c	\
	climisc.h	\
	climap.c	\
	climap.h	\
	clinet.c	\
	clinet.h	\
	colors_common.c		\
	colors_common.h		\
	control.c	\
	control.h	\
	editor.c 		\
	editor.h 		\
	ggzclient.c	\
	ggzclient.h	\
	goto.c		\
	goto.h		\
	helpdata.c	\
	helpdata.h	\
	mapctrl_common.c \
	mapctrl_common.h \
	mapview_common.c \
	mapview_common.h \
	messagewin_common.c \
	messagewin_common.h \
	overview_common.c	\
	overview_common.h	\
	packhand.c	\
	packhand.h	\
	packhand_gen.h	\
	packhand_gen.c	\
	plrdlg_common.c \
	plrdlg_common.h \
	options.c	\
	options.h	\
	repodlgs_common.c \
	repodlgs_common.h \
	reqtree.c \
	reqtree.h \
	servers.c		\
	servers.h		\
	text.c	\
	text.h	\
	themes_common.c	\
	themes_common.h	\
	tilespec.c	\
	tilespec.h	\
	audio.c         \
	audio.h         \
	audio_none.c    \
	audio_none.h

# packhand_gen.c & packhand_gen.h are generated files, but as they are
# generated outside this directory (when building common) there's no point in
# setting them to BUILT_SOURCES.

# Automake requires binary to have some sources directly.
civclient_SOURCES = \
	civclient.c

# There is no dependency against changes to gui_sources -variable.
# There might be no relinking with correct gui, if it already exist
# and is not recompiled.
libfreeciv_client_la_DEPENDENCIES = \
 $(top_builddir)/client/agents/libagents.la \
 $(gui_sources)/libguiclient.la

libfreeciv_client_la_LIBADD = \
 $(top_builddir)/client/agents/libagents.la \
 $(gui_sources)/libguiclient.la

fc_civclient_libs = \
 ./libfreeciv-client.la		\
 $(LIBFTWL)			\
 $(top_builddir)/common/libfreeciv.la
civclient_LDFLAGS = $(CLIENT_LDFLAGS) $(GGZMOD_LDFLAGS)
civclient_DEPENDENCIES = $(fc_civclient_libs)
civclient_LDADD = \
 $(fc_civclient_libs) \
 $(fc_civclient_libs) \
 $(INTLLIBS) $(CLIENT_LIBS) $(SOUND_LIBS) $(LIB_GGZMOD) \
 $(fc_civclient_libs) \
 $(INTLLIBS) $(CLIENT_LIBS) $(SOUND_LIBS) $(LIB_GGZMOD) $(CLIENTICON)
desktopfiledir = $(prefix)/share/applications
desktopfile_DATA = \
	freeciv.desktop
