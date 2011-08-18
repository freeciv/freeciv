## Process this file with automake to produce Makefile.in

if AI_MOD_STATIC_THREADED
noinst_LTLIBRARIES = libthreadedai.la
else
aimodule_LTLIBRARIES = fc_ai_threaded.la
endif

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(top_srcdir)/common

da_sources = \
	taiplayer.c		\
	taiplayer.h		\
	threadedai.c

if AI_MOD_STATIC_THREADED
libthreadedai_la_SOURCES = $(da_sources)
else
fc_ai_threaded_la_SOURCES = $(da_sources)
fc_ai_threaded_la_LDFLAGS = -module
endif
