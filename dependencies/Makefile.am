## Process this file with automake to produce Makefile.in

if SYS_LUA
LUASUBDIR =
else
LUASUBDIR = lua-5.4
endif

if TINYCTHREAD
TINYCTHRDIR = tinycthread
else
TINYCTHRDIR =
endif

SUBDIRS = $(LUASUBDIR) tolua-5.2 cvercmp $(TINYCTHRDIR)

if FCDB_DB
SUBDIRS += luasql
endif

EXTRA_DIST = \
		SDL2_gfx/Version.txt \
		SDL2_gfx/freeciv_sdl2_gfx.patch \
		SDL2_gfx/SDL2_rotozoom.c \
		SDL2_gfx/SDL2_rotozoom.h \
		m4/gnulib-common.m4 \
		m4/locale-en.m4 \
		m4/locale-fr.m4 \
		m4/mkstemp.m4 \
		m4/stdlib_h.m4
