## Process this file with automake to produce Makefile.in

bin_PROGRAMS = civserver
INCLUDES = -I$(srcdir)/../common -I$(srcdir)/../ai

civserver_SOURCES = \
		cityhand.c	\
		cityhand.h	\
		citytools.c	\
		citytools.h	\
		cityturn.c	\
		cityturn.h	\
		civserver.c	\
		civserver.h	\
		diplhand.c	\
		diplhand.h	\
		gamehand.c	\
		gamehand.h	\
		gamelog.c	\
		gamelog.h	\
		gotohand.c	\
		gotohand.h	\
		handchat.c	\
		mapgen.c	\
		mapgen.h	\
		maphand.c	\
		maphand.h	\
		meta.c		\
		meta.h		\
		plrhand.c	\
		plrhand.h	\
		registry.c	\
		registry.h	\
		sernet.c	\
		sernet.h	\
		settlers.c	\
		settlers.h	\
		stdinhand.c	\
		stdinhand.h	\
		unitfunc.c	\
		unitfunc.h	\
		unithand.c	\
		unithand.h	\
		unittools.c	\
		unittools.h


civserver_DEPENDENCIES = ../common/libcivcommon.a ../ai/libcivai.a
civserver_LDADD        = ../common/libcivcommon.a ../ai/libcivai.a  $(SERVER_LIBS)

