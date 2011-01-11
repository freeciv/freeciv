## Process this file with automake to produce Makefile.in

LIBLIST = libcivai.la

if AI_MODULES
aimodule_LTLIBRARIES = fc_ai_default.la
else
LIBLIST += libdefaultai.la
endif

noinst_LTLIBRARIES = $(LIBLIST)

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(srcdir)/../common -I$(top_srcdir)/server -I$(top_srcdir)/server/advisors -I$(top_srcdir)/common/aicore -I$(top_srcdir)/server/generator

libcivai_la_SOURCES = \
		advdomestic.c	\
		advdomestic.h	\
		advmilitary.c	\
		advmilitary.h	\
		advdiplomacy.c	\
		advdiplomacy.h	\
		advspace.c	\
		advspace.h	\
		aiair.c		\
		aiair.h		\
		aicity.c	\
		aicity.h	\
		aiferry.c	\
		aiferry.h	\
		aiguard.c	\
		aiguard.h	\
		aihand.c	\
		aihand.h	\
		aihunt.c	\
		aihunt.h	\
		aiparatrooper.c \
		aiparatrooper.h \
		aiplayer.c	\
		aiplayer.h	\
		aisettler.c	\
		aisettler.h	\
		aitech.c	\
		aitech.h	\
		aitools.c	\
		aitools.h	\
		aiunit.c	\
		aiunit.h	\
		aidiplomat.c	\
		aidiplomat.h

libcivai_la_LIBADD = -lm

da_sources = \
		defaultai.c	\
		defaultai.h

if AI_MODULES
fc_ai_default_la_SOURCES = $(da_sources)

fc_ai_default_la_LDFLAGS = -module
else
libdefaultai_la_SOURCES = $(da_sources)
endif
