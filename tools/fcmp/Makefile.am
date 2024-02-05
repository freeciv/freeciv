## Process this file with automake to produce Makefile.in

MOCCMD=@MOCCMD@

include $(top_srcdir)/bootstrap/Makerules.mk

bin_PROGRAMS =

if MODINST_CLI
bin_PROGRAMS += freeciv-mp-cli
endif
if MODINST_GTK3
bin_PROGRAMS += freeciv-mp-gtk3
endif
if MODINST_GTK4
bin_PROGRAMS += freeciv-mp-gtk4
endif
if MODINST_GTK5
bin_PROGRAMS += freeciv-mp-gtk4x
endif
if MODINST_QT
bin_PROGRAMS += freeciv-mp-qt
endif

if MODINST
noinst_LTLIBRARIES = libfcmp.la
endif

common_cppflags = \
	-I$(top_srcdir)/dependencies/cvercmp \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/client \
	-I$(top_srcdir)/client/include \
	-I$(top_srcdir)/tools/ruleutil \
	-I$(top_srcdir)/tools/shared \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums

AM_CPPFLAGS = $(common_cppflags)

if MINGW
MPICON = $(top_builddir)/platforms/windows/icons/mpicon.o
endif

libfcmp_la_SOURCES = \
		download.c	\
		download.h	\
		modinst.c	\
		modinst.h	\
		mpcmdline.c	\
		mpcmdline.h	\
		mpdb.c		\
		mpdb.h

if MODINST_CLI
freeciv_mp_cli_CPPFLAGS = $(common_cppflags) $(mp_cli_cflags)

freeciv_mp_cli_SOURCES = mpcli.c

freeciv_mp_cli_LDFLAGS = $(mp_cli_ldflags)

freeciv_mp_cli_LDADD = \
		libfcmp.la \
		$(top_builddir)/dependencies/cvercmp/libcvercmp.la \
		$(top_builddir)/common/libfreeciv.la \
		$(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
		$(mp_cli_libs) -lsqlite3
endif

if MODINST_GTK3
freeciv_mp_gtk3_CPPFLAGS = $(common_cppflags) $(mp_gtk3_cflags)

freeciv_mp_gtk3_SOURCES = mpgui_gtk3.c

freeciv_mp_gtk3_LDFLAGS = $(mp_gtk3_ldflags)

freeciv_mp_gtk3_LDADD = \
		libfcmp.la \
		$(top_builddir)/dependencies/cvercmp/libcvercmp.la \
		$(top_builddir)/common/libfreeciv.la \
		$(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
		$(mp_gtk3_libs) -lsqlite3 \
		$(MPICON)
endif

if MODINST_GTK4
freeciv_mp_gtk4_CPPFLAGS = $(common_cppflags) $(mp_gtk4_cflags)

freeciv_mp_gtk4_SOURCES = mpgui_gtk4.c

freeciv_mp_gtk4_LDFLAGS = $(mp_gtk4_ldflags)

freeciv_mp_gtk4_LDADD = \
		libfcmp.la \
		$(top_builddir)/dependencies/cvercmp/libcvercmp.la \
		$(top_builddir)/common/libfreeciv.la \
		$(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
		$(mp_gtk4_libs) -lsqlite3 \
		$(MPICON)
endif

if MODINST_GTK5
freeciv_mp_gtk4x_CPPFLAGS = $(common_cppflags) $(mp_gtk5_cflags)

freeciv_mp_gtk4x_SOURCES = mpgui_gtk5.c

freeciv_mp_gtk4x_LDFLAGS = $(mp_gtk5_ldflags)

freeciv_mp_gtk4x_LDADD = \
		libfcmp.la \
		$(top_builddir)/dependencies/cvercmp/libcvercmp.la \
		$(top_builddir)/common/libfreeciv.la \
		$(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
		$(mp_gtk5_libs) -lsqlite3 \
		$(MPICON)
endif

if MODINST_QT

MOC_FILES = \
	meta_mpgui_qt.cpp \
	meta_mpgui_qt_worker.cpp

freeciv_mp_qt_CPPFLAGS = $(common_cppflags) $(mp_qt_cppflags)
freeciv_mp_qt_CXXFLAGS = $(mp_qt_cxxflags)

freeciv_mp_qt_SOURCES = \
	mpgui_qt.cpp	\
	mpgui_qt.h	\
	mpgui_qt_worker.cpp \
	mpgui_qt_worker.h

nodist_freeciv_mp_qt_SOURCES = $(MOC_FILES)

CLEANFILES = $(MOC_FILES)

freeciv_mp_qt_LDADD = \
		libfcmp.la \
		$(top_builddir)/dependencies/cvercmp/libcvercmp.la \
		$(top_builddir)/common/libfreeciv.la \
		$(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) \
		$(mp_qt_libs) -lsqlite3 \
		$(MPICON)
endif

EXTRA_DIST = mpgui_gtk5.c
