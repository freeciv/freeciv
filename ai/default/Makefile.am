## Process this file with automake to produce Makefile.in

if AI_MODULES
aimodule_LTLIBRARIES = fc_ai_default.la
else
noinst_LTLIBRARIES = libdefaultai.la
endif

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(top_srcdir)/common -I$(top_srcdir)/server -I$(top_srcdir)/server/advisors -I$(top_srcdir)/ai

da_sources = \
		defaultai.c	\
		defaultai.h

if AI_MODULES
fc_ai_default_la_SOURCES = $(da_sources)

fc_ai_default_la_LDFLAGS = -module
else
libdefaultai_la_SOURCES = $(da_sources)
endif
