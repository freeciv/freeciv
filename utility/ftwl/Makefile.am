## Process this file with automake to produce Makefile.in

if FTWL
FTWLLIB=libftwl.a
else
FTWLLIB=
endif

noinst_LIBRARIES = $(FTWLLIB)

AM_CPPFLAGS = -I. -I$(top_srcdir)/utility  -I../../intl -I/usr/include/freetype2 @CLIENT_CFLAGS@

BACKEND_COMMON_FILES=			\
	be_common_24.c		\
	be_common_24.h		\
	be_common_24_sprite.c

ALL_BACKEND_X11_FILES=$(BACKEND_COMMON_FILES) be_x11_ximage.c
ALL_BACKEND_SDL_FILES=$(BACKEND_COMMON_FILES) be_sdl.c
ALL_OTHER_FILES=	\
	back_end.h	\
	common_types.c	\
	common_types.h	\
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
	text_renderer.h	\
	widget_core.c	\
	widget_window.c \
	theme_engine.c	\
	theme_engine.h

if FTWL_X11
BACKEND_X11_FILES=$(ALL_BACKEND_X11_FILES)
endif
if FTWL_SDL
BACKEND_SDL_FILES=$(ALL_BACKEND_SDL_FILES)
endif
if FTWL
OTHER_FILES=$(ALL_OTHER_FILES)
endif

## Above, note -I../../intl instead of -I$(top_srdir)/intl is deliberate.

libftwl_a_SOURCES = $(BACKEND_X11_FILES) \
	$(BACKEND_SDL_FILES) $(OTHER_FILES)
