## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libaicore.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I.. \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums

libaicore_la_SOURCES = 		\
	aiactions.c		\
	aiactions.h		\
	aisupport.c		\
	aisupport.h		\
	caravan.c               \
	caravan.h		\
	citymap.c		\
	citymap.h               \
	cm.c	 		\
	cm.h			\
	path_finding.c		\
	path_finding.h		\
	pf_tools.c		\
	pf_tools.h
