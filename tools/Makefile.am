## Process this file with automake to produce Makefile.in

SUBDIRS = shared ruleutil fcmp

if RULEDIT
SUBDIRS += ruledit
endif

if FCMANUAL
SUBDIRS += manual
endif

include $(top_srcdir)/bootstrap/Makerules.mk

bin_PROGRAMS =

if FCRULEUP
bin_PROGRAMS += freeciv-ruleup
endif

common_cppflags = \
	-I$(top_srcdir)/dependencies/cvercmp \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/server/ruleset \
	-I$(top_srcdir)/tools/ruleutil \
	-I$(top_srcdir)/tools/shared \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums

AM_CPPFLAGS = $(common_cppflags)

freeciv_ruleup_SOURCES =	\
		ruleup.c

freeciv_ruleup_LDADD = \
 $(top_builddir)/server/libfreeciv-srv.la \
 $(top_builddir)/common/libfreeciv.la \
 $(top_builddir)/tools/ruleutil/libfcruleutil.la \
 $(top_builddir)/tools/shared/libtoolsshared.la \
 $(top_builddir)/dependencies/cvercmp/libcvercmp.la \
 $(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) $(SERVER_LIBS)
