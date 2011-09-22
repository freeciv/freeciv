## Process this file with automake to produce Makefile.in

SUBDIRS = advisors generator scripting

bin_PROGRAMS = freeciv-server
lib_LTLIBRARIES = libfreeciv-srv.la
AM_CPPFLAGS = \
	-I$(top_srcdir)/ai/default \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/scriptcore \
	-I$(top_srcdir)/utility \
	-I$(srcdir)/advisors \
	-I$(srcdir)/generator \
	-I$(srcdir)/scripting \
	$(MAPIMG_WAND_CFLAGS) \
	$(LIBGGZ_INCLUDES) $(GGZDMOD_INCLUDES) \
	$(FCDB_MYSQL_CFLAGS) $(FCDB_POSTGRES_CFLAGS) $(FCDB_SQLITE3_CFLAGS)

if MINGW32
SERVERICON=../win32/servericon.o
endif

freeciv_server_SOURCES = \
		civserver.c	\
		civserver.h

libfreeciv_srv_la_SOURCES = \
		aiiface.c	\
		aiiface.h	\
		auth.c		\
		auth.h		\
		barbarian.c	\
		barbarian.h	\
		citizenshand.c	\
		citizenshand.h	\
		cityhand.c	\
		cityhand.h	\
		citytools.c	\
		citytools.h	\
		cityturn.c	\
		cityturn.h	\
		commands.c	\
		commands.h	\
		connecthand.c	\
		connecthand.h	\
		console.c	\
		console.h	\
		diplhand.c	\
		diplhand.h	\
		diplomats.c	\
		diplomats.h	\
		edithand.c	\
		edithand.h	\
		fcdb.c		\
		fcdb.h		\
		gamehand.c	\
		gamehand.h	\
		ggzserver.c	\
		ggzserver.h	\
		handchat.c	\
		handchat.h	\
		hand_gen.h	\
		hand_gen.c	\
		maphand.c	\
		maphand.h	\
		meta.c		\
		meta.h		\
		notify.c	\
		notify.h	\
		plrhand.c	\
		plrhand.h	\
		report.c	\
		report.h	\
		ruleset.c	\
		ruleset.h	\
		sanitycheck.c	\
		sanitycheck.h	\
		savegame.c	\
		savegame.h	\
		savegame2.c	\
		savegame2.h	\
		score.c		\
		score.h		\
		sernet.c	\
		sernet.h	\
		settings.c	\
		settings.h	\
		spacerace.c	\
		spacerace.h	\
		srv_log.c	\
		srv_log.h	\
		srv_main.c	\
		srv_main.h	\
		stdinhand.c	\
		stdinhand.h	\
		techtools.h	\
		techtools.c	\
		unithand.c	\
		unithand.h	\
		unittools.c	\
		unittools.h	\
		voting.c	\
		voting.h

# hand_gen.c & hand_gen.h are generated files, but as they are generated
# outside this directory (when building common) there's no point in
# setting them to BUILT_SOURCES.

da_libs =

if !AI_MODULES
da_libs += $(top_builddir)/ai/default/libdefaultai.la
else
if AI_MOD_STATIC_DEFAULT
da_libs += $(top_builddir)/ai/default/libdefaultai.la
endif
endif
if AI_MOD_STATIC_THREADED
da_libs += $(top_builddir)/ai/threaded/libthreadedai.la
endif

# These files are not generated to builddir, but to srcdir */
MAINTAINERCLEANFILES = \
	$(srcdir)/hand_gen.c \
	$(srcdir)/hand_gen.h

srvlibs = \
 $(da_libs) \
 $(top_builddir)/server/advisors/libadvisors.la \
 $(top_builddir)/server/generator/libgenerator.la \
 $(top_builddir)/server/scripting/libscripting_fcdb.la \
 $(top_builddir)/server/scripting/libscripting_server.la

libfreeciv_srv_la_DEPENDENCIES = \
 $(srvlibs)

libfreeciv_srv_la_LIBADD = \
 $(srvlibs)

freeciv_server_DEPENDENCIES = \
 $(top_builddir)/common/libfreeciv.la \
 ./libfreeciv-srv.la \
 $(LUA_AS_DEPENDENCY) $(TOLUA_AS_DEPENDENCY) $(LUASQL_AS_DEPENDENCY)

freeciv_server_LDFLAGS = $(GGZDMOD_LDFLAGS)

freeciv_server_LDADD = \
 ./libfreeciv-srv.la \
 $(top_builddir)/common/libfreeciv.la \
 $(INTLLIBS) \
 $(MAPIMG_WAND_LIBS) \
 $(LUA_LIBS) $(TOLUA_LIBS) $(LUASQL_LIBS) \
 $(FCDB_MYSQL_LIBS) $(FCDB_POSTGRES_LIBS) $(FCDB_SQLITE3_LIBS) \
 $(SERVER_LIBS) $(LIB_GGZDMOD) $(SERVERICON)

desktopfiledir = $(prefix)/share/applications
desktopfile_DATA = \
	freeciv-server.desktop
