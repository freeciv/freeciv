## Process this file with automake to produce Makefile.in

SUBDIRS=	aicore

noinst_LIBRARIES = libcivcommon.a

INCLUDES = -I../intl -I$(srcdir)/aicore

## Above, note -I../intl instead of -I$(top_srdir/intl) is deliberate.

libcivcommon_a_SOURCES = \
		astring.c       \
		astring.h       \
		capability.c	\
		capability.h	\
		capstr.c	\
		capstr.h	\
		city.c		\
		city.h		\
		combat.c	\
		combat.h	\
		connection.c	\
		connection.h	\
		dataio.c	\
		dataio.h	\
		diptreaty.c	\
		diptreaty.h	\
		effects.c	\
		effects.h	\
		events.h	\
		fcintl.c	\
		fcintl.h	\
		game.c		\
		game.h		\
		generate_packets.py \
		genlist.c	\
		genlist.h	\
		government.c	\
		government.h	\
		hash.c		\
		hash.h		\
		idex.c		\
		idex.h		\
		improvement.c	\
		improvement.h	\
		inputfile.c	\
		inputfile.h	\
		ioz.c		\
		ioz.h		\
		log.c		\
		log.h		\
		map.c		\
		map.h		\
		nation.c        \
		nation.h        \
		netintf.c	\
		netintf.h	\
		packets.c	\
		packets.h	\
		packets_gen.c	\
		packets_gen.h	\
		packets.def	\
		player.c	\
		player.h	\
		pqueue.c	\
		pqueue.h	\
		mem.c           \
		mem.h		\
		rand.c		\
		rand.h		\
		registry.c	\
		registry.h	\
		sbuffer.c	\
		sbuffer.h	\
		shared.c	\
		shared.h	\
		spaceship.c	\
		spaceship.h	\
		speclist.h	\
		speclist_c.h	\
		specvec.h	\
		specvec_c.h	\
		support.c	\
		support.h	\
		tech.c		\
		tech.h		\
		terrain.c	\
		terrain.h	\
		timing.c	\
		timing.h	\
		unit.c		\
		unit.h		\
		unittype.c	\
		unittype.h	\
		worklist.c	\
		worklist.h	\
		version.c	\
		version.h

packets_gen.h packets_gen.c: packets.def generate_packets.py
	./generate_packets.py
