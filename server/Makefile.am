## Process this file with automake to produce Makefile.in

SUBDIRS=	userdb

bin_PROGRAMS = civserver
noinst_LIBRARIES = libcivserver.a
INCLUDES = -I$(top_srcdir)/utility -I$(srcdir)/../common -I$(srcdir)/../ai -I../intl -I$(top_srcdir)/common/aicore -I$(srcdir)/userdb

## Above, note -I../intl instead of -I$(top_srdir/intl) is deliberate.

civserver_SOURCES = \
		civserver.c	\
		civserver.h

libcivserver_a_SOURCES = \
		airgoto.c	\
		airgoto.h	\
		autoattack.c	\
		autoattack.h	\
		barbarian.c	\
		barbarian.h	\
		cityhand.c	\
		cityhand.h	\
		citytools.c	\
		citytools.h	\
		cityturn.c	\
		cityturn.h	\
		connecthand.c	\
		connecthand.h	\
		console.c	\
		console.h	\
		diplhand.c	\
		diplhand.h	\
		diplomats.c	\
		diplomats.h	\
		gamehand.c	\
		gamehand.h	\
		gamelog.c	\
		gamelog.h	\
		gotohand.c	\
		gotohand.h	\
		handchat.c	\
		handchat.h	\
		hand_gen.h	\
		hand_gen.c	\
		mapgen.c	\
		mapgen.h	\
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
		settlers.c	\
		settlers.h	\
		spacerace.c	\
		spacerace.h	\
		srv_main.c	\
		srv_main.h	\
		stdinhand.c	\
		stdinhand.h	\
		unithand.c	\
		unithand.h	\
		unittools.c	\
		unittools.h

civserver_DEPENDENCIES = ../utility/libcivutility.a ../common/libcivcommon.a \
      ../ai/libcivai.a ../utility/libcivutility.a ./libcivserver.a \
      ../utility/libcivutility.a ../common/aicore/libaicore.a $(USER_DB_DEP)
civserver_LDADD        = ../utility/libcivutility.a ../common/libcivcommon.a \
      ../ai/libcivai.a ../utility/libcivutility.a ./libcivserver.a @INTLLIBS@ \
      ../utility/libcivutility.a ../common/libcivcommon.a ../ai/libcivai.a \
      ../utility/libcivutility.a ./libcivserver.a ../utility/libcivutility.a \
      ../common/aicore/libaicore.a $(USER_DB_LIB) $(SERVER_LIBS)

