## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libdefaultai.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/server/advisors \
	-I$(top_srcdir)/ai \
	-I$(top_srcdir)/server/generator \
	-I$(top_srcdir)/dependencies/tinycthread

libdefaultai_la_SOURCES = \
		aiair.c		\
		aiair.h		\
		aidiplomat.c	\
		aidiplomat.h	\
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
		aitech.c	\
		aitech.h	\
		aitools.c	\
		aitools.h	\
		daiactions.c	\
		daiactions.h	\
		daicity.c	\
		daicity.h	\
		daidata.c	\
		daidata.h	\
		daidiplomacy.c	\
		daidiplomacy.h	\
		daidomestic.c	\
		daidomestic.h	\
		daieffects.c	\
		daieffects.h	\
		dailog.c	\
		dailog.h	\
		daimilitary.c	\
		daimilitary.h	\
		daiplayer.c	\
		daiplayer.h	\
		daisettler.c	\
		daisettler.h	\
		daiunit.c	\
		daiunit.h


libdefaultai_la_LIBADD = -lm
