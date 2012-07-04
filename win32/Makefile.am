## Process this file with automake to produce Makefile.in

dist_noinst_DATA = \
		client.ico 	\
		mp.ico		\
		server.ico 	\
		clienticon.rc 	\
		mpicon.rc	\
		servericon.rc	\
		installer/create-freeciv-gtk2-nsi.sh	\
		installer/create-freeciv-sdl-nsi.sh	\
		installer/freeciv-gtk2.cmd		\
		installer/freeciv-modpack.cmd		\
		installer/freeciv-sdl.cmd		\
		installer/freeciv-server.cmd		\
		installer/Freeciv.url			\
		installer/Makefile

if MINGW32
nodist_noinst_DATA = 		\
		clienticon.o 	\
		mpicon.o	\
		servericon.o

CLEANFILES = $(nodist_noinst_DATA)

clienticon.o: clienticon.rc
	$(WINDRES) -I$(srcdir) -i $(srcdir)/clienticon.rc -o clienticon.o

mpicon.o: mpicon.rc
	$(WINDRES) -I$(srcdir) -i $(srcdir)/mpicon.rc -o mpicon.o

servericon.o: servericon.rc
	$(WINDRES) -I$(srcdir) -i $(srcdir)/servericon.rc -o servericon.o
endif
