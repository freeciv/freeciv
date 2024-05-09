## Process this file with automake to produce Makefile.in

bin_PROGRAMS = freeciv-manual

common_cppflags = \
	-I$(top_srcdir)/dependencies/cvercmp \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/server/ruleset \
	-I$(top_srcdir)/client \
	-I$(top_srcdir)/client/include \
	-I$(top_srcdir)/tools/shared \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums

AM_CPPFLAGS = $(common_cppflags)

freeciv_manual_SOURCES =		\
		fc_manual.c		\
		fc_manual.h		\
		manual_buildings.c	\
		manual_commands.c	\
		manual_extras.c		\
		manual_governments.c	\
		manual_settings.c	\
		manual_techs.c		\
		manual_terrain.c	\
		manual_uclass.c		\
		manual_units.c

# This is a bit of a hack. The program links in with the server lib
# but also uses some files from the client.
freeciv_manual_LDADD = \
 $(top_builddir)/server/libfreeciv-srv.la \
 $(top_builddir)/client/libfc_helpdata.la \
 $(top_builddir)/common/libfreeciv.la \
 $(top_builddir)/tools/shared/libtoolsshared.la \
 $(top_builddir)/dependencies/cvercmp/libcvercmp.la \
 $(INTLLIBS) $(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
 $(SERVER_LIBS)
