# SUBDIRS = lib

EXTRA_DIST= \
	COPYRIGHT.tolua

noinst_PROGRAMS = tolua
noinst_LIBRARIES = libtolua.a

AM_CPPFLAGS = -I$(top_srcdir)/dependencies/lua/src \
	-I$(top_srcdir)/dependencies/lua/include \
	-I$(top_srcdir)/dependencies/lua/src/lib

tolua_SOURCES = \
	tolua.c \
	tolua.h \
	toluabind.c

EXTRA_tolua_SOURCES = \
	array.lua \
	basic.lua \
	class.lua \
	clean.lua \
	code.lua \
	compat.lua \
	container.lua \
	declaration.lua \
	define.lua \
	doit.lua \
	enumerate.lua \
	feature.lua \
	function.lua \
	module.lua \
	namespace.lua \
	operator.lua \
	package.lua \
	typedef.lua \
	variable.lua \
	verbatim.lua

tolua_DEPENDENCIES = ../lua/src/liblua.a ../lua/src/lib/liblualib.a ./libtolua.a
tolua_LDADD = ../lua/src/liblua.a ../lua/src/lib/liblualib.a ./libtolua.a -lm

libtolua_a_SOURCES = \
	tolua.h \
	tolua_event.c \
	tolua_event.h \
	tolua_is.c \
	tolua_map.c \
	tolua_push.c \
	tolua_to.c
