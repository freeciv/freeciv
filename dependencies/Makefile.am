## Process this file with automake to produce Makefile.in

if SYS_LUA
LUASUBDIR =
else
LUASUBDIR = lua-5.2
endif

SUBDIRS = $(LUASUBDIR) tolua-5.2 cvercmp

if FCDB_DB
SUBDIRS += luasql
endif

EXTRA_DIST = \
		m4/mkstemp.m4 \
		m4/stdlib_h.m4
