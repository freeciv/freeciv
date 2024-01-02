## Process this file with automake to produce Makefile.in
noinst_LTLIBRARIES = libruleload.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/common/scriptcore \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/server/advisors \
	-I$(top_srcdir)/server/scripting \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums

libruleload_la_SOURCES = \
	rscompat.c	\
	rscompat.h	\
	rssanity.c	\
	rssanity.h	\
	ruleload.c	\
	ruleload.h
