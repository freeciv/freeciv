## Process this file with automake to produce Makefile.in

bin_PROGRAMS = freeciv-manual
AM_CPPFLAGS = -I$(top_srcdir)/server -I$(top_srcdir)/utility -I$(top_srcdir)/common -I$(top_srcdir)/ai -I$(top_srcdir)/common/aicore -I$(top_srcdir)/server/generator -I$(top_srcdir)/client -I$(top_srcdir)/client/include

freeciv_manual_SOURCES = \
		civmanual.c

# This is a bit of a hack.  The program links in with the server lib
# but also uses some files from the client.
freeciv_manual_DEPENDENCIES = \
 $(top_builddir)/common/libfreeciv.la \
 $(top_builddir)/server/libfreeciv-srv.la \
 $(top_builddir)/client/helpdata.lo \
 $(LUA_AS_DEPENDENCY) $(TOLUA_AS_DEPENDENCY)

freeciv_manual_LDFLAGS = $(GGZDMOD_LDFLAGS)

freeciv_manual_LDADD = \
 $(top_builddir)/server/libfreeciv-srv.la \
 ../client/helpdata.lo \
 $(LUA_LIBS) $(TOLUA_LIBS) \
 $(top_builddir)/common/libfreeciv.la $(INTLLIBS) \
 $(SERVER_LIBS) $(AUTH_LIBS) $(LIB_GGZDMOD)
