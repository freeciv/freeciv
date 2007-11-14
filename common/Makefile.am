## Process this file with automake to produce Makefile.in

SUBDIRS=	aicore

noinst_LIBRARIES = libcivcommon.a

AM_CPPFLAGS = -I$(top_srcdir)/utility -I$(srcdir)/aicore

libcivcommon_a_SOURCES = \
		base.c		\
		base.h		\
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
		events.c	\
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
		movement.c	\
		movement.h	\
		nation.c        \
		nation.h        \
		packets.c	\
		packets.h	\
		packets_gen.c	\
		packets_gen.h	\
		packets.def	\
		player.c	\
		player.h	\
		requirements.c	\
		requirements.h	\
		spaceship.c	\
		spaceship.h	\
		specialist.c	\
		specialist.h	\
		team.c		\
		team.h		\
		tech.c		\
		tech.h		\
		terrain.c	\
		terrain.h	\
		tile.c		\
		tile.h		\
		unit.c		\
		unit.h		\
		unitlist.c	\
		unitlist.h	\
		unittype.c	\
		unittype.h	\
		vision.c	\
		vision.h	\
		worklist.c	\
		worklist.h	\
		version.c	\
		version.h

BUILT_SOURCES = packets_gen.c packets_gen.h

# packets_gen.h must be generated before it can be included.
# Automatic dependencies are only generated during first
# build. We need dependency for that first build.
%.o : packets_gen.h

# ./generate_packets makes both packets_gen.[ch]
#
# Feed the dependence through a fake intermediate file to avoid
# a race condition in a parallel build (like make -j2).
#
# Currently we run the generation directly into the source directory.
# Ideally we'd generate the new files into the build directory but first we'd
# have to resolve conflicts (since there could then be 2 copies).
packets_gen.h packets_gen.c: packets_generate
.INTERMEDIATE: packets_generate
packets_generate: packets.def generate_packets.py
	cd $(srcdir) && ./generate_packets.py
	touch packets_generate

if SVNREV
# fc_svnrev_gen.h must be generated before it can be included.
# Automatic dependencies are only generated during first
# build. We need dependency for that first build.
version.o : fc_svnrev_gen.h

endif

.PHONY : fc_svnrev_gen.h
fc_svnrev_gen.h :
	$(top_srcdir)/bootstrap/generate_svnrev.sh $(top_srcdir) $(top_builddir)

#libcivcommon_a_DEPENDENCIES = ../utility/libcivutility.a
#libcivcommon_a_LIBADD       = ../utility/libcivutility.a
