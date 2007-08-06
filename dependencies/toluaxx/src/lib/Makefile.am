# SUBDIRS = none

noinst_LIBRARIES = libtolua.a

AM_CPPFLAGS = \
	-I$(top_srcdir)/dependencies/lua/include \
	-I$(top_srcdir)/dependencies/toluaxx/include \
	-I.

libtolua_a_SOURCES = \
	$(top_srcdir)/dependencies/tolua/include/toluaxx.h \
	tolua_event.c \
	tolua_event.h \
	tolua_is.c \
	tolua_map.c \
	tolua_push.c \
	tolua_to.c
