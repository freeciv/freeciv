## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgui-qt.la
AM_CPPFLAGS = -I. -I$(srcdir)/.. -I$(srcdir)/../include -I$(top_srcdir)/utility -I$(top_srcdir)/common $(GUI_qt_CPPFLAGS)

AM_CFLAGS = $(GUI_qt_CFLAGS) $(SOUND_CFLAGS)

AM_CXXFLAGS = $(GUI_qt_CXXFLAGS) $(SOUND_CFLAGS)

MOC_FILES = \
	meta_fc_client.cpp

libgui_qt_la_SOURCES = \
	canvas.cpp	\
	canvas.h	\
	chatline.cpp	\
	chatline.h	\
	citydlg.cpp	\
	cityrep.cpp	\
	cityrep.h	\
	colors.cpp	\
	colors.h	\
	connectdlg.cpp	\
	connectdlg.h	\
	dialogs.cpp	\
	dialogs.h	\
	diplodlg.cpp	\
	diplodlg.h	\
	fc_client.cpp	\
	fc_client.h	\
	finddlg.cpp	\
	finddlg.h	\
	gotodlg.cpp	\
	gotodlg.h	\
	graphics.cpp	\
	graphics.h	\
	gui_main.cpp	\
	helpdlg.cpp	\
	helpdlg.h	\
	inteldlg.cpp	\
	inteldlg.h	\
	luaconsole.cpp	\
	luaconsole.h	\
	mapctrl.cpp	\
	mapctrl.h	\
	mapview.cpp	\
	mapview.h	\
	menu.cpp	\
	menu.h		\
	messagedlg.cpp	\
	messagedlg.h	\
	messagewin.cpp	\
	messagewin.h	\
	optiondlg.cpp	\
	optiondlg.h	\
	pages.cpp	\
	pages.h		\
	plrdlg.cpp	\
	plrdlg.h	\
	qtg_cside.c	\
	qtg_cside.h	\
	qtg_cxxside.cpp \
	qtg_cxxside.h	\
	ratesdlg.cpp	\
	ratesdlg.h	\
	repodlgs.cpp	\
	repodlgs.h	\
	spaceshipdlg.cpp \
	spaceshipdlg.h	\
	sprite.cpp	\
	sprite.h	\
	themes.cpp	\
	voteinfo_bar.cpp \
	voteinfo_bar.h	\
	wldlg.cpp	\
	wldlg.h		\
	$(MOC_FILES)

# Generate meta object source from header
$(MOC_FILES) : meta_%.cpp: %.h
	moc -o $@ $<

CLEANFILES = $(MOC_FILES)
