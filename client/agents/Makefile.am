## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libagents.a

## INCLUDES = -I.. -I../gui-gtk -I../include -I$(top_srcdir)/common -I../intl -I/usr/lib/glib/include @CLIENT_CFLAGS@

INCLUDES = -I. -I$(srcdir)/.. -I$(srcdir)/../include -I$(top_srcdir)/common -I../../intl -I$(srcdir)/../gui-gtk @CLIENT_CFLAGS@

libagents_a_SOURCES = 		\
	agents.c		\
	agents.h		\
	cma_core.c 		\
	cma_core.h 		\
	cma_fec.c		\
	cma_fec.h 
