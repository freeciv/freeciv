## Process this file with automake to produce Makefile.in

dist_noinst_DATA = \
		client.ico \
		server.ico \
		clienticon.rc \
		servericon.rc

if MINGW32
nodist_noinst_DATA = \
		clienticon.o \
		servericon.o

CLEANFILES = $(nodist_noinst_DATA)

clienticon.o: clienticon.rc
	windres -i clienticon.rc -o clienticon.o

servericon.o: servericon.rc
	windres -i servericon.rc -o servericon.o
endif

