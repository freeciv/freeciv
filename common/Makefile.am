## Process this file with automake to produce Makefile.in

SUBDIRS=	aicore

noinst_LIBRARIES = libcivcommon.a

AM_CPPFLAGS = -I../intl -I$(top_srcdir)/utility -I$(srcdir)/aicore

## Above, note -I../intl instead of -I$(top_srcdir/intl) is deliberate.

libcivcommon_a_SOURCES = \
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
		fc_types.h	\
		game.c		\
		game.h		\
		generate_packets.py \
		government.c	\
		government.h	\
		idex.c		\
		idex.h		\
		improvement.c	\
		improvement.h	\
		map.c		\
		map.h		\
		nation.c        \
		nation.h        \
		packets.c	\
		packets.h	\
		packets_gen.c	\
		packets_gen.h	\
		packets.def	\
		player.c	\
		player.h	\
		spaceship.c	\
		spaceship.h	\
		tech.c		\
		tech.h		\
		terrain.c	\
		terrain.h	\
		unit.c		\
		unit.h		\
		unittype.c	\
		unittype.h	\
		worklist.c	\
		worklist.h	\
		version.c	\
		version.h

BUILT_SOURCES = packets_gen.c packets_gen.h

# ./generate_packets makes both packets_gen.[ch]
# Feed the dependence through a fake intermediate file to avoid
# a race condition in a parallel build (like make -j2).
packets_gen.h packets_gen.c: packets_generate
.INTERMEDIATE: packets_generate
packets_generate: packets.def generate_packets.py
	./generate_packets.py
	touch packets_generate

#libcivcommon_a_DEPENDENCIES = ../utility/libcivutility.a
#libcivcommon_a_LIBADD       = ../utility/libcivutility.a

