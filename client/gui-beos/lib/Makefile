SUBDIRS = Bdh BdhGame
LIBS    = libBdh.so  libBdhGame.so

all : $(LIBS)

libBdh.so : 
	cd Bdh ; make
	cd Bdh ; make install

libBdhGame.so :
	cd BdhGame ; make 
	cd BdhGame ; make install


clean :
	for d in $(SUBDIRS) ; do ( cd $$d ; make $@ ); done

