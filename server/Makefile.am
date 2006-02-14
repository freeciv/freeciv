## Process this file with automake to produce Makefile.in

SUBDIRS=	generator scripting

bin_PROGRAMS = civserver
noinst_LIBRARIES = libcivserver.a
AM_CPPFLAGS = \
	-I$(top_srcdir)/utility -I$(srcdir)/../common -I$(srcdir)/../ai \
	-I../intl -I$(top_srcdir)/common/aicore \
	-I$(srcdir)/generator -I$(srcdir)/scripting \
	@LIBGGZ_INCLUDES@ @GGZDMOD_INCLUDES@

## Above, note -I../intl instead of -I$(top_srdir/intl) is deliberate.

civserver_SOURCES = \
		civserver.c	\
		civserver.h

libcivserver_a_SOURCES = \
		airgoto.c	\
		airgoto.h	\
		auth.c		\
		auth.h		\
		barbarian.c	\
		barbarian.h	\
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
		gamehand.c	\
		gamehand.h	\
		gotohand.c	\
		gotohand.h	\
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
		score.c		\
		score.h		\
		sernet.c	\
		sernet.h	\
		settings.c	\
		settings.h	\
		settlers.c	\
		settlers.h	\
		spacerace.c	\
		spacerace.h	\
		srv_main.c	\
		srv_main.h	\
		stdinhand.c	\
		stdinhand.h	\
		techtools.h	\
		techtools.c	\
		unithand.c	\
		unithand.h	\
		unittools.c	\
		unittools.h

civserver_DEPENDENCIES = ../utility/libcivutility.a ../common/libcivcommon.a \
      ../ai/libcivai.a ../utility/libcivutility.a ./libcivserver.a \
      ../utility/libcivutility.a ../common/aicore/libaicore.a \
      ./scripting/libscripting.a \
      ../dependencies/lua/src/liblua.a \
      ../dependencies/lua/src/lib/liblualib.a \
      ../dependencies/tolua/libtolua.a \
      ./generator/libgenerator.a
civserver_LDFLAGS = @GGZDMOD_LDFLAGS@
civserver_LDADD        = ../utility/libcivutility.a ../common/libcivcommon.a \
      ../ai/libcivai.a ../utility/libcivutility.a ./libcivserver.a @INTLLIBS@ \
      ../utility/libcivutility.a ../common/libcivcommon.a ../ai/libcivai.a \
      ../utility/libcivutility.a ./libcivserver.a ../utility/libcivutility.a \
      ../common/aicore/libaicore.a ./generator/libgenerator.a \
      ./scripting/libscripting.a \
      ../dependencies/lua/src/liblua.a \
      ../dependencies/lua/src/lib/liblualib.a \
      ../dependencies/tolua/libtolua.a \
      $(AUTH_LIBS) $(SERVER_LIBS) @LIB_GGZDMOD@



