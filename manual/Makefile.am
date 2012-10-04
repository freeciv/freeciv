## Process this file with automake to produce Makefile.in

bin_PROGRAMS = freeciv-manual
AM_CPPFLAGS = \
	-I$(top_srcdir)/ai \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/scriptcore \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/server/generator \
	-I$(top_srcdir)/client \
	-I$(top_srcdir)/client/include \
	-I$(top_srcdir)/utility

freeciv_manual_SOURCES = \
		civmanual.c

# This is a bit of a hack.  The program links in with the server lib
# but also uses some files from the client.
EXTRA_freeciv_manual_DEPENDENCIES = \
 $(top_builddir)/client/helpdata.lo

freeciv_manual_LDFLAGS = $(GGZDMOD_LDFLAGS)

freeciv_manual_LDADD = \
 $(top_builddir)/server/libfreeciv-srv.la \
 ../client/helpdata.lo \
 $(top_builddir)/common/libfreeciv.la \
 $(LUA_LIBS) $(TOLUA_LIBS) $(LUASQL_LIBS) \
 $(INTLLIBS) $(MAPIMG_WAND_LIBS) \
 $(FCDB_MYSQL_LIBS) $(FCDB_POSTGRES_LIBS) $(FCDB_SQLITE3_LIBS) \
 $(SERVER_LIBS) $(LIB_GGZDMOD)
