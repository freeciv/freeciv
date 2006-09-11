## Process this file with automake to produce Makefile.in

## Below used to be just 
##    SUBDIRS = $(gui_sources)
## but that doesn't work properly for "make dist"

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
LIBFTWL = ../utility/ftwl/libftwl.a
endif

ALL_SDL_FILES=audio_sdl.c audio_sdl.h

if SDL
SDL_FILES=$(ALL_SDL_FILES)
endif

EXTRA_DIST= 	gui-mui/autogroupclass.c         \
		gui-mui/autogroupclass.h         \
		gui-mui/chatline.c               \
		gui-mui/chatline.h               \
		gui-mui/citydlg.c                \
		gui-mui/citydlg.h                \
		gui-mui/cityrep.c                \
		gui-mui/cityrep.h                \
		gui-mui/colors.c                 \
		gui-mui/colors.h                 \
		gui-mui/colortextclass.c         \
		gui-mui/colortextclass.h         \
		gui-mui/connectdlg.c             \
		gui-mui/connectdlg.h             \
		gui-mui/dialogs.c                \
		gui-mui/dialogs.h                \
		gui-mui/diplodlg.c               \
		gui-mui/diplodlg.h               \
		gui-mui/finddlg.c                \
		gui-mui/finddlg.h                \
		gui-mui/gamedlgs.c               \
		gui-mui/gotodlg.c                \
		gui-mui/gotodlg.h                \
		gui-mui/graphics.c               \
		gui-mui/graphics.h               \
		gui-mui/gui_main.c               \
		gui-mui/gui_main.h               \
		gui-mui/helpdlg.c                \
		gui-mui/helpdlg.h                \
		gui-mui/historystringclass.c     \
		gui-mui/historystringclass.h     \
		gui-mui/inputdlg.c               \
		gui-mui/inputdlg.h               \
		gui-mui/inteldlg.c               \
		gui-mui/inteldlg.h               \
		gui-mui/mapclass.c               \
		gui-mui/mapclass.h               \
		gui-mui/mapctrl.c                \
		gui-mui/mapctrl.h                \
		gui-mui/mapview.c                \
		gui-mui/mapview.h                \
		gui-mui/messagedlg.c             \
		gui-mui/messagedlg.h             \
		gui-mui/messagewin.c             \
		gui-mui/messagewin.h             \
		gui-mui/muistuff.c               \
		gui-mui/muistuff.h               \
		gui-mui/objecttreeclass.c        \
		gui-mui/objecttreeclass.h        \
		gui-mui/optiondlg.h              \
		gui-mui/overviewclass.c          \
		gui-mui/overviewclass.h          \
		gui-mui/plrdlg.c                 \
		gui-mui/plrdlg.h                 \
		gui-mui/ratesdlg.h               \
		gui-mui/repodlgs.c               \
		gui-mui/repodlgs.h               \
		gui-mui/scrollbuttonclass.c      \
		gui-mui/scrollbuttonclass.h      \
		gui-mui/spaceshipdlg.c           \
		gui-mui/spaceshipdlg.h           \
		gui-mui/transparentstringclass.c \
		gui-mui/transparentstringclass.h \
		gui-mui/wldlg.c                  \
		gui-mui/wldlg.h                  \
		gui-mui/worklistclass.c          \
		gui-mui/worklistclass.h          \
		\
		$(ALL_SDL_FILES)


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

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(srcdir)/include -I$(top_srcdir)/common -I$(top_srcdir)/common/aicore -I../intl -I$(srcdir)/agents $(SOUND_CFLAGS) $(LIBGGZ_INCLUDES) $(GGZMOD_INCLUDES) $(GGZ_GTK_INCLUDES)

## Above, note -I../intl instead of -I$(top_srdir/intl) is deliberate.

civclient_SOURCES = $(ESD_FILES) $(SDL_FILES) $(ALSA_FILES) $(WINMM_FILES) \
	attribute.h	\
	attribute.c	\
	citydlg_common.c \
	citydlg_common.h \
	cityrepdata.c	\
	cityrepdata.h	\
	civclient.c	\
	civclient.h	\
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

civclient_LDFLAGS = $(CLIENT_LDFLAGS) $(GGZMOD_LDFLAGS)
fc_civclient_libs =	../utility/libcivutility.a	\
			$(LIBFTWL)			\
			../common/libcivcommon.a	\
			../common/aicore/libaicore.a	\
			agents/libagents.a		\
		 	$(gui_sources)/libguiclient.a
civclient_DEPENDENCIES = $(fc_civclient_libs)
civclient_LDADD        = $(fc_civclient_libs) $(fc_civclient_libs) \
	$(INTLLIBS) $(CLIENT_LIBS) $(SOUND_LIBS) $(LIB_GGZMOD)
desktopfiledir = $(prefix)/share/applications
desktopfile_DATA = \
	freeciv.desktop
