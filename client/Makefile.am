## Process this file with automake to produce Makefile.in

bin_PROGRAMS = civclient
INCLUDES = -I $(srcdir)/../common -I $(srcdir)/../ai

BUILT_SOURCES = Freeciv.h

Freeciv.h: ../data/Freeciv
	./ad2c ../data/Freeciv > Freeciv.h

civclient_DEPENDENCIES = ad2c cityicon.ico freeciv.ico ../common/libcivcommon.a
civclient_LDADD        = ../common/libcivcommon.a

civclient_SOURCES = \
		cityicon.ico freeciv.ico ad2c \
		Freeciv.h	\
		canvas.c	\
		canvas.h	\
		canvasp.h	\
		chatline.c	\
		chatline.h	\
		citydlg.c	\
		citydlg.h	\
		civclient.c	\
		civclient.h	\
		climisc.c	\
		climisc.h	\
		clinet.c	\
		clinet.h	\
		colors.c	\
		colors.h	\
		connectdlg.c	\
		connectdlg.h	\
		dialogs.c	\
		dialogs.h	\
		diplodlg.c	\
		diplodlg.h	\
		finddlg.c	\
		finddlg.h	\
		gotodlg.c	\
		gotodlg.h	\
		graphics.c	\
		graphics.h	\
		helpdlg.c	\
		helpdlg.h	\
		inputdlg.c	\
		inputdlg.h	\
		inteldlg.c	\
		inteldlg.h	\
		mapctrl.c	\
		mapctrl.h	\
		mapview.c	\
		mapview.h	\
		menu.c		\
		menu.h		\
		messagedlg.c	\
		messagedlg.h	\
		messagewin.c	\
		meswindlg.h	\
		optiondlg.c	\
		optiondlg.h	\
		packhand.c	\
		packhand.h	\
		pixcomm.c	\
		pixcomm.h	\
		pixcommp.h	\
		plrdlg.c	\
		plrdlg.h	\
		ratesdlg.c	\
		ratesdlg.h	\
		repodlgs.c	\
		repodlgs.h	\
		resources.c	\
		resources.h	\
		xmain.c		\
		xmain.h		\
		xstuff.c	\
		xstuff.h
