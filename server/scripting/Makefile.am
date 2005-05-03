## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libscripting.a

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility -I$(top_srcdir)/common \
	-I$(top_srcdir)/intl -I$(top_srcdir)/server \
	-I$(top_srcdir)/dependencies/lua/include \
	-I$(top_srcdir)/dependencies/tolua

libscripting_a_SOURCES = \
		api_actions.c	\
		api_actions.h	\
		api_find.c	\
		api_find.h	\
		api_gen.c	\
		api_gen.h	\
		api_intl.c	\
		api_intl.h	\
		api_notify.c	\
		api_notify.h	\
		api_utilities.c	\
		api_utilities.h	\
		api_types.h	\
		script.c	\
		script.h	\
		script_signal.c	\
		script_signal.h

BUILT_SOURCES = api_gen.c api_gen.h
CLEANFILES = api_gen.c api_gen.h

api_gen.c api_gen.h: api.pkg
	$(top_srcdir)/dependencies/tolua/tolua -o api_gen.c -H api_gen.h api.pkg

