## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libagents.a

AM_CPPFLAGS = -I. -I$(srcdir)/.. -I$(top_srcdir)/common/aicore -I$(srcdir)/../include -I$(top_srcdir)/utility -I$(top_srcdir)/common -I../../intl -I$(srcdir)/../gui-gtk @CLIENT_CFLAGS@

libagents_a_SOURCES = 		\
	agents.c		\
	agents.h		\
	cma_core.c 		\
	cma_core.h 		\
	cma_fec.c		\
	cma_fec.h		\
	sha.c                   \
	sha.h                   
