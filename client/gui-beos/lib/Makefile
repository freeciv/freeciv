SUBDIRS = Bdh BdhDialog BdhGame
LIBS    = libBdh.so  libBdhDialog.so  libBdhGame.so

all : $(LIBS)

libBdh.so : 
	cd Bdh ; make
	cd Bdh ; make install

libBdhDialog.so :
	cd BdhDialog ; make
	cd BdhDialog ; make install

libBdhGame.so :
	cd BdhGame ; make 
	cd BdhGame ; make install


clean :
	for d in $(SUBDIRS) ; do ( cd $$d ; make $@ ); done
	rm -f include/*.h $(LIBS)
