## Process this file with automake to produce Makefile.in

bin_PROGRAMS = civserver
noinst_LIBRARIES = libcivserver.a
INCLUDES = -I$(srcdir)/../common -I$(srcdir)/../ai -I../intl

## Above, note -I../intl instead of -I$(top_srdir/intl) is deliberate.

civserver_SOURCES = \
		civserver.c	\
		civserver.h

libcivserver_a_SOURCES = \
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


civserver_DEPENDENCIES = ../common/libcivcommon.a ../ai/libcivai.a ./libcivserver.a
civserver_LDADD        = ../common/libcivcommon.a ../ai/libcivai.a ./libcivserver.a @INTLLIBS@ ../common/libcivcommon.a ../ai/libcivai.a ./libcivserver.a $(SERVER_LIBS)
