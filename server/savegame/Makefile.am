## Process this file with automake to produce Makefile.in
noinst_LTLIBRARIES = libsavegame.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/common/scriptcore \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/server/advisors \
	-I$(top_srcdir)/server/generator \
	-I$(top_srcdir)/server/scripting \
	-I$(top_srcdir)/ai \
	-I$(top_srcdir)/dependencies/tinycthread

libsavegame_la_SOURCES = \
	savecompat.c	\
	savecompat.h	\
	savegame2.c	\
	savegame2.h	\
	savegame3.c	\
	savegame3.h	\
	savemain.c	\
	savemain.h
