## Process this file with automake to produce Makefile.in

if AI_MODULES
module_dirs = stub
else
module_dirs =
endif

SUBDIRS = default $(module_dirs)

noinst_LTLIBRARIES = libcivai.la

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(srcdir)/../common -I$(top_srcdir)/server -I$(top_srcdir)/server/advisors -I$(top_srcdir)/common/aicore -I$(top_srcdir)/server/generator -I$(srcdir)/default

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
