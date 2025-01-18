## Process this file with automake to produce Makefile.in

## Must be set before adding anything
GUI_SUBDIRS =

if CLIENT_GUI_SDL2
GUI_SUBDIRS += gui-sdl2
endif

GUI_SUBDIRS += gui-sdl3

if CLIENT_GUI_GTK_3_22
GUI_SUBDIRS += gui-gtk-3.22
endif

if CLIENT_GUI_GTK_4
GUI_SUBDIRS += gui-gtk-4.0
endif

if CLIENT_GUI_GTK_5
GUI_SUBDIRS += gui-gtk-5.0
endif

if CLIENT_GUI_QT
GUI_SUBDIRS += gui-qt
endif

if CLIENT_GUI_STUB
GUI_SUBDIRS += gui-stub
endif

ALL_AUDIO_SDL_FILES = audio_sdl.c audio_sdl.h

if AUDIO_SDL
AUDIO_SDL_FILES = $(ALL_AUDIO_SDL_FILES)
endif

if MINGW
CLIENTICON = $(top_builddir)/platforms/windows/icons/clienticon.o
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

if CLIENT
SUBDIRS = $(GUI_SUBDIRS) $(CLI_INCLUDE) agents luascript
endif

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(srcdir)/include \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/common/scriptcore \
	-I$(srcdir)/luascript \
	-I$(srcdir)/agents \
	$(CLIENT_CFLAGS) $(SOUND_CFLAGS) \
	-I$(top_srcdir)/dependencies/cvercmp \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums

AM_LIBTOOLFLAGS = --preserve-dup-deps

# Build client common functionality in to library
# instead linking them directly to the client. This is necessary
# for static linking.
freeciv_client_src = $(AUDIO_SDL_FILES) \
	attribute.h	\
	attribute.c	\
	audio.c         \
	audio.h         \
	audio_none.c    \
	audio_none.h	\
	chatline_common.c \
	chatline_common.h \
	citydlg_common.c \
	citydlg_common.h \
	cityrepdata.c	\
	cityrepdata.h	\
	client_main.c	\
	client_main.h	\
	climap.c	\
	climap.h	\
	climisc.c	\
	climisc.h	\
	clinet.c	\
	clinet.h	\
	clitreaty.c	\
	clitreaty.h	\
	colors_common.c		\
	colors_common.h		\
	connectdlg_common.c \
	connectdlg_common.h \
	control.c	\
	control.h	\
	editor.c 		\
	editor.h 		\
	global_worklist.c	\
	global_worklist.h	\
	goto.c		\
	goto.h		\
	gui_properties.c	\
	gui_properties.h	\
	luaconsole_common.c	\
	luaconsole_common.h	\
	mapctrl_common.c \
	mapctrl_common.h \
	mapview_common.c \
	mapview_common.h \
	messagewin_common.c \
	messagewin_common.h \
	music.c		\
	music.h		\
	options.c	\
	options.h	\
	overview_common.c	\
	overview_common.h	\
	packhand.c	\
	packhand.h	\
	packhand_gen.h	\
	packhand_gen.c	\
	plrdlg_common.c \
	plrdlg_common.h \
	repodlgs_common.c \
	repodlgs_common.h \
	reqtree.c \
	reqtree.h \
	servers.c	\
	servers.h	\
	svgflag.c	\
	svgflag.h	\
	text.c	\
	text.h	\
	themes_common.c	\
	themes_common.h	\
	tilespec.c	\
	tilespec.h	\
	unitselect_common.c	\
	unitselect_common.h	\
	update_queue.c	\
	update_queue.h	\
	voteinfo.c	\
	voteinfo.h	\
	zoom.c		\
	zoom.h

gui_interface_src = \
	gui_interface.c \
	gui_interface.h

gui_cbs_src = \
	gui_cbsetter.c \
	gui_cbsetter.h

# packhand_gen.c & packhand_gen.h are generated files, but as they are
# generated outside this directory (when building common) there's no point in
# setting them to BUILT_SOURCES.

# These files are not generated to builddir, but to srcdir */
MAINTAINERCLEANFILES = $(srcdir)/packhand_gen.c $(srcdir)/packhand_gen.h

bin_PROGRAMS =
noinst_LTLIBRARIES = libfc_helpdata.la

libfc_helpdata_la_SOURCES =	\
	helpdata.c		\
	helpdata.h

clientlibs = \
 $(top_builddir)/common/scriptcore/libscriptcore.la \
 $(top_builddir)/client/agents/libagents.la \
 $(top_builddir)/client/luascript/libscripting_client.la \
 $(top_builddir)/dependencies/cvercmp/libcvercmp.la \
 libfc_helpdata.la

if CLIENT_GUI_GTK_3_22
bin_PROGRAMS += freeciv-gtk3.22
noinst_LTLIBRARIES += libfcgui-gtk3_22.la
libfcgui_gtk3_22_la_SOURCES = $(freeciv_client_src)
libfcgui_gtk3_22_la_LIBADD = gui-gtk-3.22/libgui-gtk3_22.la $(clientlibs)
freeciv_gtk3_22_SOURCES = dummy.c
freeciv_gtk3_22_LDFLAGS = $(gui_gtk3_22_ldflags)
freeciv_gtk3_22_LDADD = \
 libfcgui-gtk3_22.la $(SOUND_LIBS) gui-gtk-3.22/gui_main.lo \
 $(top_builddir)/common/libfreeciv.la \
 $(INTLLIBS) $(CLIENT_LIBS) $(CLIENTICON) \
 $(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
 $(gui_3d_libs) \
 $(gui_gtk3_22_libs)
endif

if CLIENT_GUI_GTK_4
bin_PROGRAMS += freeciv-gtk4
noinst_LTLIBRARIES += libfcgui-gtk4.la
libfcgui_gtk4_la_SOURCES = $(freeciv_client_src)
libfcgui_gtk4_la_LIBADD = gui-gtk-4.0/libgui-gtk4.la $(clientlibs)
freeciv_gtk4_SOURCES = dummy.c
freeciv_gtk4_LDFLAGS = $(gui_gtk4_ldflags)
freeciv_gtk4_LDADD = \
 libfcgui-gtk4.la $(SOUND_LIBS) gui-gtk-4.0/gui_main.lo \
 $(top_builddir)/common/libfreeciv.la \
 $(INTLLIBS) $(CLIENT_LIBS) $(CLIENTICON) \
 $(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
 $(gui_3d_libs) \
 $(gui_gtk4_libs)
endif

if CLIENT_GUI_GTK_5
bin_PROGRAMS += freeciv-gtk4x
noinst_LTLIBRARIES += libfcgui-gtk5.la
libfcgui_gtk5_la_SOURCES = $(freeciv_client_src)
libfcgui_gtk5_la_LIBADD = gui-gtk-5.0/libgui-gtk5.la $(clientlibs)
freeciv_gtk4x_SOURCES = dummy.c
freeciv_gtk4x_LDFLAGS = $(gui_gtk4_ldflags)
freeciv_gtk4x_LDADD = \
 libfcgui-gtk5.la $(SOUND_LIBS) gui-gtk-5.0/gui_main.lo \
 $(top_builddir)/common/libfreeciv.la \
 $(INTLLIBS) $(CLIENT_LIBS) $(CLIENTICON) \
 $(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
 $(gui_3d_libs) \
 $(gui_gtk5_libs)
endif

if CLIENT_GUI_SDL2
bin_PROGRAMS += freeciv-sdl2
noinst_LTLIBRARIES += libfcgui-sdl2.la
libfcgui_sdl2_la_SOURCES = $(freeciv_client_src)
libfcgui_sdl2_la_LIBADD = gui-sdl2/libgui-sdl2.la $(clientlibs)
freeciv_sdl2_SOURCES = dummy.c
freeciv_sdl2_LDFLAGS = $(gui_sdl2_ldflags)
freeciv_sdl2_LDADD = \
 $(gui_sdl2_libs) \
 libfcgui-sdl2.la $(SOUND_LIBS) gui-sdl2/gui_main.lo \
 $(top_builddir)/common/libfreeciv.la \
 $(INTLLIBS) $(CLIENT_LIBS) $(CLIENTICON) \
 $(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
 $(gui_sdl2_libs)
endif

if CLIENT_GUI_QT
bin_PROGRAMS += freeciv-qt
noinst_LTLIBRARIES += libfcgui-qt.la
libfcgui_qt_la_SOURCES = $(freeciv_client_src)
libfcgui_qt_la_LIBADD = gui-qt/libgui-qt.la $(clientlibs)
freeciv_qt_SOURCES = dummycxx.cpp $(gui_interface_src)
freeciv_qt_LDFLAGS = $(gui_qt_ldflags)
freeciv_qt_LDADD = \
 libfcgui-qt.la $(SOUND_LIBS) gui-qt/gui_main.lo \
 $(top_builddir)/common/libfreeciv.la \
 $(INTLLIBS) $(CLIENT_LIBS) $(CLIENTICON) \
 $(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
 $(gui_qt_libs)
endif

if CLIENT_GUI_STUB
bin_PROGRAMS += freeciv-stub
noinst_LTLIBRARIES += libfcgui-stub.la
libfcgui_stub_la_SOURCES = $(freeciv_client_src)
libfcgui_stub_la_LIBADD = gui-stub/libgui-stub.la $(clientlibs)
freeciv_stub_SOURCES = $(gui_interface_src) $(gui_cbs_src)
freeciv_stub_LDFLAGS = $(gui_stub_ldflags)
freeciv_stub_LDADD = \
 libfcgui-stub.la $(SOUND_LIBS) gui-stub/gui_main.lo \
 $(top_builddir)/common/libfreeciv.la \
 $(INTLLIBS) $(CLIENT_LIBS) $(CLIENTICON) \
 $(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
 $(gui_stub_libs)
endif
