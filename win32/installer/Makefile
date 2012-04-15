#
# Makefile for Freeciv Windows installer creation
#
# Usage:
#
# 1. download and extract the MinGW build environment from
#    http://download.gna.org/freeciv/packages/windows/gnuwin32/
# 2. double click on "gnuwin32/msys.bat"
# 3. install the development packages:
#    $ hg qpush -a
# 4. change into the Freeciv directory where this Makefile resides
#    $ cd <path_to_freeciv_source>/win32/installer
# 5. run make
#    $ make
# 6. find the installer executables in the 'Output' directory
#


#
# Make rules
#


# build all installers

all: 
	make GUI=gtk2 installer-gtk2
	make GUI=sdl installer-sdl


# build Freeciv

build-freeciv-common:
	# create build directory
	mkdir -p build-$(GUI)
	# configure
	cd build-$(GUI); ../../../configure --enable-client=$(GUI)
	# make
	make -C build-$(GUI)

build-freeciv-gtk2: build-freeciv-common

build-freeciv-sdl: build-freeciv-common


# install Freeciv

install-freeciv-common:
	# make install
	make -C build-$(GUI) DESTDIR=`pwd`/install-$(GUI) install
	# reorder directory layout
	mv install-$(GUI)/usr/local/bin/* install-$(GUI)/
	mv install-$(GUI)/usr/local/share/freeciv install-$(GUI)/data
	mkdir -p install-$(GUI)/share
	mv install-$(GUI)/usr/local/share/locale install-$(GUI)/share/
	# delete unneeded files
	rm -r install-$(GUI)/usr
	# strip 'freeciv-manual' and 'freeciv-modpack' executables
	strip install-$(GUI)/freeciv-m*
	# add start menu files
	cp freeciv-server.cmd freeciv-modpack.cmd freeciv-$(GUI).cmd Freeciv.url install-$(GUI)/
	# add documentation
	cp -R $(addprefix ../../, doc ABOUT-NLS AUTHORS ChangeLog COPYING INSTALL NEWS README) install-$(GUI)/
	find install-$(GUI)/doc -name 'Makefile.*' -delete

install-freeciv-gtk2: install-freeciv-common

install-freeciv-sdl: install-freeciv-common
	# add CJK fonts
	cp /opt/fireflysung-1.3.0/fireflysung.ttf install-sdl/data/themes/gui-sdl/human/
	cp /opt/fireflysung-1.3.0/COPYRIGHT install-sdl/data/themes/gui-sdl/human/COPYING.fireflysung
	cp /opt/sazanami-20040629/sazanami-gothic.ttf install-sdl/data/themes/gui-sdl/human/
	cp /opt/sazanami-20040629/README install-sdl/data/themes/gui-sdl/human/COPYING.sazanami
	cp /opt/un-fonts/UnDotum.ttf install-sdl/data/themes/gui-sdl/human/
	cp /opt/un-fonts/COPYING install-sdl/data/themes/gui-sdl/human/COPYING.UnDotum
	# add Freeciv GTK+ theme for modpack installer
	cp -R ../../data/themes/gui-gtk-2.0 install-sdl/data/themes/
	find install-sdl/data/themes/gui-gtk-2.0 -name 'Makefile.*' -delete


# install Freeciv environment

COMMON_DLLS := \
	SDL.dll \
	SDL_mixer.dll \
	exchndl.dll \
	freetype6.dll \
	intl.dll \
	libatk-1.0-0.dll \
	libbz2-2.dll \
	libcairo-2.dll \
	libexpat-1.dll \
	libfontconfig-1.dll \
	libgcc_s_dw2-1.dll \
	libgdk-win32-2.0-0.dll \
	libgdk_pixbuf-2.0-0.dll \
	libgio-2.0-0.dll \
	libglib-2.0-0.dll \
	libgmodule-2.0-0.dll \
	libgobject-2.0-0.dll \
	libgthread-2.0-0.dll \
	libgtk-win32-2.0-0.dll \
	libiconv-2.dll \
	libogg-0.dll \
	libpango-1.0-0.dll \
	libpangocairo-1.0-0.dll \
	libpangoft2-1.0-0.dll \
	libpangowin32-1.0-0.dll \
	libpng14-14.dll \
	libvorbis-0.dll \
	libvorbisfile-3.dll \
	zlib1.dll

install-env-common:
	# add DLLs
	cp -R $(addprefix /bin/, $(COMMON_DLLS)) install-$(GUI)
	# add GTK+ config files
	mkdir -p install-$(GUI)/etc
	cp -R /etc/gtk-2.0 install-$(GUI)/etc/
	mkdir -p install-$(GUI)/lib/gtk-2.0/
	cp -R /lib/gtk-2.0/2.10.0 install-$(GUI)/lib/gtk-2.0/
	echo "include \"../../data/themes/gui-gtk-2.0/Freeciv/gtk-2.0/gtkrc\"" > install-$(GUI)/etc/gtk-2.0/gtkrc
	# add GTK+ locale files
	cp -R /share/locale install-$(GUI)/share/
	find install-$(GUI)/share/locale -type f -not -name freeciv.mo -and -not -name gtk20.mo -delete

install-env-gtk2: install-env-common

SDL_DLLS := \
	libpng12-0.dll \
	SDL_image.dll

install-env-sdl: install-env-common
	# add DLLs
	cp -R $(addprefix /bin/, $(SDL_DLLS)) install-$(GUI)


# build installer
	
installer-common: clean-install-common install-freeciv-$(GUI) install-env-$(GUI)
	# extract Freeciv version
	$(eval FREECIV_VERSION := $(shell grep "#define VERSION \"" build-$(GUI)/config.h | sed -e 's/#define VERSION //'))
	# create NSIS script
	./create-freeciv-$(GUI)-nsi.sh install-$(GUI) $(FREECIV_VERSION) > Freeciv-$(FREECIV_VERSION)-$(GUI).nsi
	# create installer executable
	mkdir -p Output
	makensis Freeciv-$(FREECIV_VERSION)-$(GUI).nsi

installer-gtk2: build-freeciv-gtk2 installer-common

installer-sdl: build-freeciv-sdl installer-common


# cleanup

clean-build-common:
	rm -rf build-$(GUI)

clean-install-common:
	rm -rf install-$(GUI)

clean-installer-common:
	rm Output/Freeciv-*-$(GUI)-setup.exe
	rm Freeciv-*-$(GUI).nsi

clean:
	make GUI=gtk2 clean-build-common clean-install-common clean-installer-common
	make GUI=sdl clean-build-common clean-install-common clean-installer-common
