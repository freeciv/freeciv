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
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums

libdefaultai_la_SOURCES = \
		daiair.c	\
		daiair.h	\
		daiactions.c	\
		daiactions.h	\
		daicity.c	\
		daicity.h	\
		daidata.c	\
		daidata.h	\
		daidiplomacy.c	\
		daidiplomacy.h	\
		daidiplomat.c	\
		daidiplomat.h	\
		daidomestic.c	\
		daidomestic.h	\
		daieffects.c	\
		daieffects.h	\
		daiferry.c	\
		daiferry.h	\
		daiguard.c	\
		daiguard.h	\
		daihand.c	\
		daihand.h	\
		daihunter.c	\
		daihunter.h	\
		dailog.c	\
		dailog.h	\
		daimilitary.c	\
		daimilitary.h	\
		daiparadrop.c	\
		daiparadrop.h	\
		daiplayer.c	\
		daiplayer.h	\
		daisettler.c	\
		daisettler.h	\
		daitech.c	\
		daitech.h	\
		daitools.c	\
		daitools.h	\
		daiunit.c	\
		daiunit.h


libdefaultai_la_LIBADD = -lm
