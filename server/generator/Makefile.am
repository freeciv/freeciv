## Process this file with automake to produce Makefile.in
noinst_LIBRARIES = libgenerator.a

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(top_srcdir)/common -I$(top_srcdir)/server -I$(top_builddir)/intl -I$(top_srcdir)/server/generator

libgenerator_a_SOURCES = \
	mapgen.c \
	mapgen.h \
	mapgen_topology.c \
	mapgen_topology.h \
	utilities.c \
	utilities.h \
	height_map.c \
	height_map.h \
	startpos.c \
	startpos.h \
	temperature_map.c \
	temperature_map.h
