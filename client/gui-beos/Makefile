# specify the name of the binary
NAME= libguiclient.a

# specify the type of binary
TYPE= STATIC

#	specify the source files to use
SRCS=	gui_main.c	\
	colors.cpp	\
	graphics.cpp	\
	chatline.cpp	\
	citydlg.cpp	\
	cityrep.cpp	\
	connectdlg.cpp	\
	dialogs.cpp	\
	diplodlg.cpp	\
	finddlg.cpp	\
	gotodlg.cpp	\
	helpdlg.cpp	\
	inteldlg.cpp	\
	mapctrl.cpp	\
	mapview.cpp	\
	menu.cpp		\
	messagedlg.cpp	\
	messagewin.cpp	\
	optiondlg.cpp \
	plrdlg.cpp	\
	ratesdlg.cpp	\
	repodlgs.cpp	\
	spaceshipdlg.cpp	\
	wldlg.cpp	\
	App.cpp

#	specify the resource files to use
RSRCS= 

#	specify additional libraries to link against
LIBS= translation be 

#	specify additional paths to directories following the standard
#	libXXX.so or libXXX.a naming scheme
LIBPATHS= 

#	additional paths to look for system headers
topsrc = $(shell cd ../.. ; pwd)
SYSTEM_INCLUDE_PATHS = $(topsrc)/intl

#	additional paths to look for local headers
LOCAL_INCLUDE_PATHS = .. ../include $(topsrc) $(topsrc)/common 

#	specify the level of optimization that you desire
#	NONE, SOME, FULL
OPTIMIZE= 

#	specify any preprocessor symbols to be defined
DEFINES= DEBUG=1

#	specify special warning levels (NONE, ALL)
WARNINGS = ALL

#	specify whether image symbols will be created
#	so that stack crawls in the debugger are meaningful
#	if TRUE symbols will be created
SYMBOLS = TRUE

#	specify debug settings
#	if TRUE will allow application to be run from a source-level
#	debugger.  Note that this will disable all optimzation.
DEBUGGER = TRUE

#	specify additional compiler flags for all files
COMPILER_FLAGS =

#	specify additional linker flags
LINKER_FLAGS =

## include the makefile-engine
include $(BUILDHOME)/etc/makefile-engine

BEOS_DOCS = README.BeOS ../../COPYING ../../HOWTOPLAY
BEOS_LIBS = 
BEOS_EXES = FreeCiv
BEOS_DATA =	data/


all : $(BEOS_LIBS) $(TARGET)
	if [ ! -h $(NAME) ]; then ln -s $(OBJ_DIR)/$(NAME) . ; fi

# $(BEOS_LIBS) :
# 	cd lib ; make

data :
	if [ ! -d data/. ]; then ln -s ../../data . ; fi

clean ::
	rm -f FreeCiv $(NAME)

FreeCiv : $(BEOS_LIBS) ../civclient
	cp ../civclient $@
	xres -o $@ FreeCiv.rsrc
	mimeset -f $@

PKGDIR = FreeCiv-0.1-x86
TMPZIP = /tmp/tmp,FreeCiv.x86.zip
pkg : $(BEOS_DOCS) $(BEOS_LIBS) $(BEOS_EXES) $(BEOS_DATA)
	rm -rf $(PKGDIR) $(TMPZIP)
	mkdir $(PKGDIR)
	@: documentation
	if [ -n "$(BEOS_DOCS)" ]; then \
		cp $(BEOS_DOCS) $(PKGDIR) ; \
	fi
	@: supporting libraries
	if [ -n "$(BEOS_LIBS)" ]; then \
		zip -j $(TMPZIP) $(BEOS_LIBS) ; \
		mkdir -p $(PKGDIR)/lib ; \
		cd $(PKGDIR)/lib/ ; unzip $(TMPZIP) ; rm -f $(TMPZIP) ; \
	fi
	@: executables
	zip -j $(TMPZIP) $(BEOS_EXES)
	cd $(PKGDIR) ; unzip $(TMPZIP) ; rm -f $(TMPZIP)
	@: data
	zip -r $(TMPZIP) $(BEOS_DATA)
	cd $(PKGDIR) ; unzip $(TMPZIP) ; rm -f $(TMPZIP)
	@: strip out any CVS stuff still left
	cd $(PKGDIR) ; find . -name CVS -print | xargs rm -rf
	cd $(PKGDIR) ; find . -name .cvsignore -print | xargs rm -rf
	@: create manifest and zip up into home directory
	find $(PKGDIR) -type f -print | \
		sed -e 's:$(PKGDIR)/::' > $(PKGDIR)/MANIFEST
	zip -r $(HOME)/$(PKGDIR).zip $(PKGDIR)

