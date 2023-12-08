## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libfcruleutil.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/server/ruleset \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums

libfcruleutil_la_SOURCES = \
		comments.c	\
		comments.h	\
		rulesave.c	\
		rulesave.h
