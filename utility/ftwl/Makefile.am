## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libftwl.a

INCLUDES = -I. -I$(top_srcdir)/utility  -I../../intl -I/usr/include/freetype2 @CLIENT_CFLAGS@

BACKEND_COMMON_FILES=			\
	be_common_24.c		\
	be_common_24.h		\
	be_common_24_sprite.c

ALL_BACKEND_X11_FILES=$(BACKEND_COMMON_FILES) be_x11_ximage.c
ALL_BACKEND_SDL_FILES=$(BACKEND_COMMON_FILES) be_sdl.c
ALL_OTHER_FILES=	\
	back_end.h	\
	common_types.h	\
	common_types.c	\
	widget_p.h	\
	widget.h	\
	widget_timeout.c	\
	widget.c	\
	widget_button.c	\
	widget_edit.c	\
	widget_label.c	\
	widget_list.c	\
	widget_slider.c	\
	text_renderer.c \
	widget_core.c	\
	widget_window.c \
	theme_engine.c

if CLIENT_GUI_FTWL_X11
BACKEND_X11_FILES=$(ALL_BACKEND_X11_FILES)
endif
if CLIENT_GUI_FTWL_SDL
BACKEND_SDL_FILES=$(ALL_BACKEND_SDL_FILES)
endif
if CLIENT_GUI_FTWL
OTHER_FILES=$(ALL_OTHER_FILES)
endif

## Above, note -I../../intl instead of -I$(top_srdir)/intl is deliberate.

libftwl_a_SOURCES = $(BACKEND_X11_FILES) \
	$(BACKEND_SDL_FILES) $(OTHER_FILES)
