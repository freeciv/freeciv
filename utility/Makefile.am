## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libcivutility.la

AM_CFLAGS = $(UTILITY_CFLAGS)

libcivutility_la_SOURCES = \
		astring.c       \
		astring.h       \
		bitvector.c     \
		bitvector.h     \
		capability.c	\
		capability.h	\
		distribute.c	\
		distribute.h	\
		fc_utf8.c	\
		fc_utf8.h	\
		fcbacktrace.c	\
		fcbacktrace.h	\
		fciconv.c	\
		fciconv.h	\
		fcintl.c	\
		fcintl.h	\
		fcthread.c	\
		fcthread.h	\
		generate_specenum.py	\
		genhash.c	\
		genhash.h	\
		genlist.c	\
		genlist.h	\
		inputfile.c	\
		inputfile.h	\
		ioz.c		\
		ioz.h		\
		iterator.c	\
		iterator.h	\
		log.c		\
		log.h		\
		netfile.c	\
		netfile.h	\
		netintf.c	\
		netintf.h	\
		pqueue.c	\
		pqueue.h	\
		mem.c           \
		mem.h		\
		rand.c		\
		rand.h		\
		registry.c	\
		registry.h	\
		registry_ini.c	\
		registry_ini.h	\
		shared.c	\
		shared.h	\
		specenum_gen.h	\
		spechash.h	\
		speclist.h	\
		specvec.h	\
		string_vector.c	\
		string_vector.h	\
		support.c	\
		support.h	\
		timing.c	\
		timing.h	\
		md5.c		\
		md5.h

libcivutility_la_LIBADD = $(UTILITY_LIBS)

BUILT_SOURCES = specenum_gen.h

# specenum.h must be generated before it can be included.
%.o : specenum_gen.h

specenum_gen.h: specenum_generate
.INTERMEDIATE: specenum_generate
specenum_generate: generate_specenum.py
	cd $(srcdir) && ./generate_specenum.py
	touch specenum_generate

# These files are not generated to builddir, but to srcdir */
MAINTAINERCLEANFILES = $(srcdir)/specenum_gen.h
