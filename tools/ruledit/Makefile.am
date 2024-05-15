## Process this file with automake to produce Makefile.in

MOCCMD=@MOCCMD@

include $(top_srcdir)/bootstrap/Makerules.mk

if MINGW
REICON = $(top_builddir)/platforms/windows/icons/rulediticon.o
endif

bin_PROGRAMS = freeciv-ruledit

AM_CPPFLAGS = \
	$(ruledit_cppflags) \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/server \
	-I$(top_srcdir)/server/ruleset \
	-I$(top_srcdir)/tools/shared \
	-I$(top_srcdir)/tools/ruleutil \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums

freeciv_ruledit_CXXFLAGS = $(ruledit_cxxflags)

MOC_FILES = \
            meta_conversion_log.cpp \
            meta_edit_extra.cpp	\
            meta_edit_gov.cpp	\
            meta_edit_impr.cpp	\
            meta_edit_tech.cpp	\
            meta_edit_terrain.cpp	\
            meta_edit_utype.cpp \
	    meta_effect_edit.cpp	\
	    meta_helpeditor.cpp	\
	    meta_req_edit.cpp	\
	    meta_req_vec_fix.cpp	\
            meta_requirers_dlg.cpp \
            meta_ruledit_qt.cpp \
            meta_tab_achievement.cpp	\
            meta_tab_building.cpp \
            meta_tab_counters.cpp \
            meta_tab_enablers.cpp \
	    meta_tab_extras.cpp	\
	    meta_tab_good.cpp	\
	    meta_tab_gov.cpp	\
            meta_tab_misc.cpp	\
            meta_tab_multiplier.cpp \
            meta_tab_nation.cpp	\
            meta_tab_tech.cpp	\
	    meta_tab_terrains.cpp \
            meta_tab_unit.cpp	\
	    meta_values_dlg.cpp

freeciv_ruledit_SOURCES =	\
		conversion_log.cpp \
		conversion_log.h \
		edit_extra.cpp	\
		edit_extra.h	\
		edit_gov.cpp	\
		edit_gov.h	\
		edit_impr.cpp	\
		edit_impr.h	\
		edit_tech.cpp	\
		edit_tech.h	\
		edit_terrain.cpp	\
		edit_terrain.h	\
		edit_utype.cpp	\
		edit_utype.h	\
		effect_edit.cpp	\
		effect_edit.h	\
		helpeditor.cpp	\
		helpeditor.h	\
		tab_achievement.cpp	\
		tab_achievement.h	\
		tab_building.cpp \
		tab_building.h	\
		tab_counters.cpp \
		tab_counters.h \
		tab_enablers.cpp \
		tab_enablers.h	\
		tab_extras.cpp	\
		tab_extras.h	\
		tab_good.cpp	\
		tab_good.h	\
		tab_gov.cpp	\
		tab_gov.h	\
		tab_misc.cpp	\
		tab_misc.h	\
		tab_multiplier.cpp \
		tab_multiplier.h \
		tab_nation.cpp	\
		tab_nation.h	\
                tab_tech.cpp	\
		tab_tech.h	\
		tab_terrains.cpp \
		tab_terrains.h	\
                tab_unit.cpp	\
		tab_unit.h	\
		req_edit.cpp	\
		req_edit.h	\
		req_vec_fix.h	\
		req_vec_fix.cpp	\
		requirers_dlg.cpp \
		requirers_dlg.h	\
		ruledit.cpp	\
		ruledit.h	\
		ruledit_qt.cpp	\
		ruledit_qt.h	\
		univ_value.c	\
		univ_value.h	\
		validity.c	\
		validity.h	\
		values_dlg.cpp	\
		values_dlg.h

nodist_freeciv_ruledit_SOURCES = $(MOC_FILES)

freeciv_ruledit_LDADD = \
 $(ruledit_libs) \
 $(top_builddir)/server/libfreeciv-srv.la \
 $(top_builddir)/common/libfreeciv.la \
 $(top_builddir)/tools/ruleutil/libfcruleutil.la \
 $(top_builddir)/tools/shared/libtoolsshared.la \
 $(top_builddir)/dependencies/cvercmp/libcvercmp.la \
 $(TINYCTHR_LIBS) $(MAPIMG_WAND_LIBS) $(SERVER_LIBS) \
 $(REICON)

CLEANFILES = $(MOC_FILES)
