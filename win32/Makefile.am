## Process this file with automake to produce Makefile.in

dist_noinst_DATA = \
		client.ico 	\
		mp.ico		\
		server.ico 	\
		clienticon.rc 	\
		mpicon.rc	\
		servericon.rc	\
		installer/licenses/COPYING.installer	\
		installer/create-freeciv-gtk-qt-nsi.sh	\
		installer/create-freeciv-gtk2-nsi.sh	\
		installer/create-freeciv-gtk3-nsi.sh	\
		installer/create-freeciv-qt-nsi.sh	\
		installer/create-freeciv-sdl2-nsi.sh	\
		installer/create-freeciv-ruledit-nsi.sh	\
		installer/freeciv-gtk2.cmd		\
		installer/freeciv-gtk3.cmd		\
		installer/freeciv-mp-gtk2.cmd		\
		installer/freeciv-mp-gtk3.cmd		\
		installer/freeciv-mp-qt.cmd		\
		installer/freeciv-qt.cmd		\
		installer/freeciv-sdl2.cmd		\
		installer/freeciv-server.cmd		\
		installer/freeciv-ruledit.cmd		\
		installer/Freeciv.url			\
		installer/Makefile			\
		installer/cleanup_checkout.sh		\
		installer_msys2/licenses/COPYING.installer	\
		installer_msys2/create-freeciv-gtk-qt-nsi.sh	\
		installer_msys2/create-freeciv-gtk2-nsi.sh	\
		installer_msys2/create-freeciv-gtk3-nsi.sh	\
		installer_msys2/create-freeciv-gtk3x-nsi.sh	\
		installer_msys2/create-freeciv-qt-nsi.sh	\
		installer_msys2/create-freeciv-sdl2-nsi.sh	\
		installer_msys2/create-freeciv-ruledit-nsi.sh	\
		installer_msys2/freeciv-gtk2.cmd		\
		installer_msys2/freeciv-gtk3.cmd		\
		installer_msys2/freeciv-gtk3x.cmd		\
		installer_msys2/freeciv-mp-gtk2.cmd		\
		installer_msys2/freeciv-mp-gtk3.cmd		\
		installer_msys2/freeciv-mp-gtk3x.cmd		\
		installer_msys2/freeciv-mp-qt.cmd		\
		installer_msys2/freeciv-qt.cmd			\
		installer_msys2/freeciv-sdl2.cmd		\
		installer_msys2/freeciv-server.cmd		\
		installer_msys2/freeciv-ruledit.cmd		\
		installer_msys2/Freeciv.url			\
		installer_msys2/Makefile			\
		installer_msys2/cleanup_checkout.sh		\
		installer_msys2/win_arch.sh

if MINGW
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
