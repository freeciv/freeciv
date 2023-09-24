## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = \
	libscripting_client.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/common/scriptcore \
	-I$(top_srcdir)/client \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums \
	$(LUA_CFLAGS) $(LUASQL_CFLAGS) $(TOLUA_CFLAGS)

# tolua_client_gen.[ch] are now distributed to aid in cross-compiling.
dist_libscripting_client_la_SOURCES = \
	api_client_base.c	\
	api_client_base.h	\
	script_client.c		\
	script_client.h		\
	tolua_client_gen.c	\
	tolua_client_gen.h

EXTRA_DIST = \
	tolua_client.pkg

BUILT_SOURCES = \
	tolua_client_gen.c	\
	tolua_client_gen.h

libscripting_client_la_LIBADD = \
	$(LUA_LIBS) $(TOLUA_LIBS) $(LUASQL_LIBS)

tolua_client_gen.c tolua_client_gen.h: tolua_client.pkg
	@if ! $(AM_V_P) ; then echo "  tolua    tolua_client" ; fi
	$(AM_V_at) $(TOLUA) -n client -o $(srcdir)/tolua_client_gen.c \
	-H $(srcdir)/tolua_client_gen.h $(srcdir)/tolua_client.pkg

# These files are not generated to builddir, but to srcdir */
MAINTAINERCLEANFILES = \
	$(srcdir)/tolua_client_gen.c \
	$(srcdir)/tolua_client_gen.h
