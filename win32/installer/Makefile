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
# 4. close current session and restart msys.bat. This is required
#    for PATH to be set correctly after development packages
#    installation.
# 5. change into the Freeciv directory where this Makefile resides
#    $ cd <path_to_freeciv_source>/win32/installer
# 6. run make
#    $ make
# 7. find the installer executables in the 'Output' directory
#


#
# Make rules
#


# by default build all installers
all: gtk3-installer gtk2-installer sdl-installer qt-installer

gtk3-installer:
	make GUI=gtk3 FCMP=gtk3 EXTRA_CONFIG="--disable-ruledit $(EXTRA_CONFIG)" wrap-gtk3

gtk2-installer:
	make GUI=gtk2 FCMP=gtk2 EXTRA_CONFIG="--disable-ruledit $(EXTRA_CONFIG)" wrap-gtk2

sdl-installer:
	make GUI=sdl FCMP=gtk3 EXTRA_CONFIG="--disable-ruledit $(EXTRA_CONFIG)" wrap-sdl

qt-installer:
	make GUI=qt FCMP=qt wrap-qt

snapshot:
	cd ../.. ; ./autogen.sh --no-configure-run
	make EXTRA_CONFIG="--enable-svnrev" all


# build Freeciv

build-freeciv-common:
	# create build directory
	mkdir -p build-$(GUI)
	# configure
	cd build-$(GUI); ../../../configure --enable-client=$(GUI) --with-followtag="win32-S2_6" --enable-fcdb=sqlite3 --enable-fcmp=$(FCMP) $(EXTRA_CONFIG)
	# make
	make -C build-$(GUI)

build-freeciv-gtk3: build-freeciv-common

build-freeciv-gtk2: build-freeciv-common

build-freeciv-sdl: build-freeciv-common

build-freeciv-qt: build-freeciv-common

# install Freeciv

install-freeciv-common:
	# make install
	make -C build-$(GUI) DESTDIR=`pwd`/install-$(GUI) install
	# reorder directory layout
	mv install-$(GUI)/usr/local/bin/* install-$(GUI)/
	mv install-$(GUI)/usr/local/etc install-$(GUI)/
	mv install-$(GUI)/usr/local/share/freeciv install-$(GUI)/data
	mkdir -p install-$(GUI)/share
	mv install-$(GUI)/usr/local/share/locale install-$(GUI)/share/
	mv install-$(GUI)/usr/local/share/doc install-$(GUI)/doc
	# delete unneeded files
	rm -r install-$(GUI)/usr
	# strip 'freeciv-manual' and 'freeciv-mp-<gui>' executables
	strip install-$(GUI)/freeciv-m*
	# add start menu files
	cp freeciv-server.cmd freeciv-mp-$(FCMP).cmd freeciv-$(GUI).cmd Freeciv.url install-$(GUI)/

install-freeciv-gtk3: install-freeciv-common

install-freeciv-gtk2: install-freeciv-common

install-freeciv-sdl: install-freeciv-common
	# add CJK fonts
	cp /opt/fireflysung-1.3.0/fireflysung.ttf install-sdl/data/themes/gui-sdl/human/
	cp /opt/fireflysung-1.3.0/COPYRIGHT install-sdl/data/themes/gui-sdl/human/COPYING.fireflysung
	cp /opt/sazanami-20040629/sazanami-gothic.ttf install-sdl/data/themes/gui-sdl/human/
	cp /opt/sazanami-20040629/README install-sdl/data/themes/gui-sdl/human/COPYING.sazanami
	cp /opt/un-fonts/UnDotum.ttf install-sdl/data/themes/gui-sdl/human/
	cp /opt/un-fonts/COPYING install-sdl/data/themes/gui-sdl/human/COPYING.UnDotum

install-freeciv-qt: install-freeciv-common

# install Freeciv environment

COMMON_DLLS := \
	SDL.dll \
	SDL_mixer.dll \
	exchndl.dll \
	freetype6.dll \
	intl.dll \
	libMagickCore-6.Q16-1.dll \
	libMagickWand-6.Q16-1.dll \
	libatk-1.0-0.dll \
	libbz2-2.dll \
	libcurl.dll \
	libcairo-2.dll \
	libcairo-gobject-2.dll \
	libeay32.dll \
	libffi-6.dll \
	libfontconfig-1.dll \
	libfreetype-6.dll \
	libgcc_s_dw2-1.dll \
	libgdk_pixbuf-2.0-0.dll \
	libgio-2.0-0.dll \
	libglib-2.0-0.dll \
	libgmodule-2.0-0.dll \
	libgobject-2.0-0.dll \
	libgthread-2.0-0.dll \
	libiconv-2.dll \
	libidn-11.dll \
	libintl-8.dll \
	liblzma-5.dll \
	libogg-0.dll \
	libpango-1.0-0.dll \
	libpangocairo-1.0-0.dll \
	libpangoft2-1.0-0.dll \
	libpangowin32-1.0-0.dll \
	libpixman-1-0.dll \
	libpng14-14.dll \
	libpng15-15.dll \
	libpthread-2.dll \
	libsqlite3-0.dll \
	libssl32.dll \
	libvorbis-0.dll \
	libvorbisfile-3.dll \
	libxml2-2.dll \
	pthreadGC2.dll \
	zlib1.dll

install-env-common:
	# add DLLs
	cp -R $(addprefix /bin/, $(COMMON_DLLS)) install-$(GUI)

DEFAULT_FCMP_DLLS := \
	libgdk-3-0.dll \
	libgtk-3-0.dll

install-env-default-fcmp: install-env-common
	cp -R $(addprefix /bin/, $(DEFAULT_FCMP_DLLS)) install-$(GUI)
	# add additional GTK+3 files
	mkdir -p install-$(GUI)/etc
	cp -R /etc/gtk-3.0 install-$(GUI)/etc/
	mkdir -p install-$(GUI)/lib/gtk-3.0/
	cp -R /lib/gtk-3.0/3.0.0 install-$(GUI)/lib/gtk-3.0/
	cp -R /share/locale install-$(GUI)/share/
	find install-$(GUI)/share/locale -type f -not -name "freeciv*.mo" -and -not -name gtk30.mo -delete

install-env-gtk3: install-env-default-fcmp

GTK2_DLLS := \
	libgdk-win32-2.0-0.dll \
	libgtk-win32-2.0-0.dll

install-env-gtk2: install-env-common
	# add DLLs
	cp -R $(addprefix /bin/, $(GTK2_DLLS)) install-$(GUI)
	# add additional GTK+2 files
	mkdir -p install-$(GUI)/etc
	cp -R /etc/gtk-2.0 install-$(GUI)/etc/
	mkdir -p install-$(GUI)/lib/gtk-2.0/
	cp -R /lib/gtk-2.0/2.10.0 install-$(GUI)/lib/gtk-2.0/
	cp -R /share/locale install-$(GUI)/share/
	find install-$(GUI)/share/locale -type f -not -name "freeciv*.mo" -and -not -name gtk20.mo -delete

SDL_DLLS := \
	libpng12-0.dll \
	libSDL_gfx-15.dll \
	SDL_image.dll \
	SDL_ttf.dll

install-env-sdl: install-env-default-fcmp
	# add DLLs
	cp -R $(addprefix /bin/, $(SDL_DLLS)) install-$(GUI)

QT_DLLS := \
	libstdc++-6.dll \
	Qt5Core.dll \
	Qt5Gui.dll \
	Qt5Widgets.dll

install-env-qt: install-env-common
	# add DLLs
	cp -R $(addprefix /bin/, $(QT_DLLS)) install-$(GUI)
	# add additional Qt files
	cp -R /plugins install-$(GUI)/
	cp -R /share/locale install-$(GUI)/share/
	find install-$(GUI)/share/locale -type f -not -name "freeciv*.mo" -delete

# build installer
	
installer-common: clean-install-common install-freeciv-$(GUI) install-env-$(GUI)
	# extract Freeciv version
	$(eval FREECIV_VERSION := $(shell ../../fc_version))
	# create NSIS script
	./create-freeciv-$(GUI)-nsi.sh install-$(GUI) $(FREECIV_VERSION) > Freeciv-$(FREECIV_VERSION)-$(GUI).nsi
	# create installer executable
	mkdir -p Output
	makensis Freeciv-$(FREECIV_VERSION)-$(GUI).nsi

wrap-gtk3: build-freeciv-gtk3 installer-common

wrap-gtk2: build-freeciv-gtk2 installer-common

wrap-sdl: build-freeciv-sdl installer-common

wrap-qt: build-freeciv-qt installer-common


# cleanup

clean-build-common:
	rm -rf build-$(GUI)

clean-install-common:
	rm -rf install-$(GUI)

clean-installer-common:
	rm -f Output/Freeciv-*-$(GUI)-setup.exe
	rm -f Freeciv-*-$(GUI).nsi

clean:
	make GUI=gtk3 clean-build-common clean-install-common clean-installer-common
	make GUI=gtk2 clean-build-common clean-install-common clean-installer-common
	make GUI=sdl clean-build-common clean-install-common clean-installer-common
	make GUI=qt clean-build-common clean-install-common clean-installer-common
