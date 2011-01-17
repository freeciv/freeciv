## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = \
		libscripting_common.la	\
		libscripting_game.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility -I$(top_srcdir)/common \
	-I$(top_srcdir)/server \
	$(FCDB_MYSQL_CFLAGS) $(FCDB_POSTGRES_CFLAGS) $(FCDB_SQLITE_CFLAGS) \
	$(LUA_CFLAGS) $(LUASQL_CFLAGS) $(TOLUA_CFLAGS)

# tolua_common_gen.[ch] and tolua_game_gen.[ch] are now
# distributed to aid in cross-compiling. See PR#13571.
dist_libscripting_common_la_SOURCES = \
		api_intl.c	\
		api_intl.h	\
		api_utilities.c	\
		api_utilities.h	\
		luascript.c	\
		luascript.h	\
		script_types.h	\
		tolua_common_a_gen.c	\
		tolua_common_a_gen.h	\
		tolua_common_z_gen.c	\
		tolua_common_z_gen.h

dist_libscripting_game_la_SOURCES = \
		api_actions.c	\
		api_actions.h	\
		api_effects.c	\
		api_effects.h	\
		api_find.c	\
		api_find.h	\
		api_methods.c	\
		api_methods.h	\
		api_notify.c	\
		api_notify.h	\
		api_specenum.c  \
		api_specenum.h  \
		script_game.c	\
		script_game.h	\
		script_signal.c	\
		script_signal.h	\
		script_types.h	\
		tolua_game_gen.c	\
		tolua_game_gen.h

EXTRA_DIST = \
		tolua_common_a.pkg	\
		tolua_common_z.pkg	\
		tolua_game.pkg

BUILT_SOURCES = \
		tolua_common_a_gen.c	\
		tolua_common_a_gen.h	\
		tolua_common_z_gen.c	\
		tolua_common_z_gen.h	\
		tolua_game_gen.c	\
		tolua_game_gen.h

tolua_common_a_gen.c tolua_common_a_gen.h: tolua_common_a.pkg
	$(TOLUA) -n common_a -o $(srcdir)/tolua_common_a_gen.c \
	-H $(srcdir)/tolua_common_a_gen.h $(srcdir)/tolua_common_a.pkg

tolua_common_z_gen.c tolua_common_z_gen.h: tolua_common_z.pkg
	$(TOLUA) -n common_z -o $(srcdir)/tolua_common_z_gen.c \
	-H $(srcdir)/tolua_common_z_gen.h $(srcdir)/tolua_common_z.pkg

tolua_game_gen.c tolua_game_gen.h: tolua_game.pkg
	$(TOLUA) -n game -o $(srcdir)/tolua_game_gen.c \
	-H $(srcdir)/tolua_game_gen.h $(srcdir)/tolua_game.pkg
