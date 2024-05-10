## Process this file with automake to produce Makefile.in

SUBDIRS =


if CLIENT_GUI_GTK_3_22
SUBDIRS += gtk3.22
endif

if CLIENT_GUI_GTK_4
SUBDIRS += gtk4
else
if CLIENT_GUI_GTK_5
SUBDIRS += gtk4
endif
endif

if CLIENT_GUI_SDL2
SUBDIRS += gui-sdl2
endif

SUBDIRS += gui-sdl3

if CLIENT_GUI_QT
SUBDIRS += gui-qt
endif
