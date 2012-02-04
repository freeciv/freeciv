## Process this file with automake to produce Makefile.in

if !AI_MOD_STATIC_CLASSIC
aimodule_LTLIBRARIES = fc_ai_classic.la
else
noinst_LTLIBRARIES = libclassicai.la
endif

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(top_srcdir)/common -I$(top_srcdir)/common/aicore -I$(top_srcdir)/server -I$(top_srcdir)/server/advisors -I$(top_srcdir)/ai -I$(top_srcdir)/server/generator

da_sources = \
		defaultai.c	\
		defaultai.h	\
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
		aidata.c	\
		aidata.h	\
		aiferry.c	\
		aiferry.h	\
		aiguard.c	\
		aiguard.h	\
		aihand.c	\
		aihand.h	\
		aihunt.c	\
		aihunt.h	\
		ailog.c		\
		ailog.h		\
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

if !AI_MOD_STATIC_CLASSIC
fc_ai_classic_la_SOURCES = $(da_sources)

fc_ai_classic_la_LDFLAGS = -module
fc_ai_classic_la_LIBADD = -lm
else
libclassicai_la_SOURCES = $(da_sources)
libclassicai_la_LIBADD = -lm
endif
