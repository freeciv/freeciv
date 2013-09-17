## Process this file with automake to produce Makefile.in

bin_PROGRAMS = freeciv-ruledit

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/server

freeciv_ruledit_SOURCES = \
		ruledit.c

freeciv_ruledit_LDADD = \
 $(top_builddir)/server/libfreeciv-srv.la \
 $(top_builddir)/common/libfreeciv.la \
 $(LUA_LIBS) $(TOLUA_LIBS) $(INTLLIBS) \
 $(MAPIMG_WAND_LIBS) $(SERVER_LIBS)
