## Process this file with automake to produce Makefile.in

dist_noinst_DATA = \
		client.ico 	\
		mp.ico		\
		ruledit.ico	\
		server.ico 	\
		clienticon.rc 	\
		mpicon.rc	\
		rulediticon.rc	\
		servericon.rc

if MINGW
nodist_noinst_DATA = 		\
		clienticon.o 	\
		mpicon.o	\
		rulediticon.o	\
		servericon.o

CLEANFILES = $(nodist_noinst_DATA)

clienticon.o: clienticon.rc
	$(WINDRES) -I$(srcdir) -i $(srcdir)/clienticon.rc -o clienticon.o

mpicon.o: mpicon.rc
	$(WINDRES) -I$(srcdir) -i $(srcdir)/mpicon.rc -o mpicon.o

rulediticon.o: rulediticon.rc
	$(WINDRES) -I$(srcdir) -i $(srcdir)/rulediticon.rc -o rulediticon.o

servericon.o: servericon.rc
	$(WINDRES) -I$(srcdir) -i $(srcdir)/servericon.rc -o servericon.o
endif
