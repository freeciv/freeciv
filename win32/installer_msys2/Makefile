#
# Makefile for Freeciv Windows installer creation
#
# See doc/README.msys2 for building instructions.
#

#
# Variable setup
#

WINARCH=$(shell ./win_arch.sh)

ifeq ($(WINARCH),win64)
ARCHDIR=/mingw64
LIBGCCDLL=libgcc_s_seh-1.dll
GCCDLL=gcc_s-seh-1.dll
else
ARCHDIR=/mingw32
LIBGCCDLL=libgcc_s_dw2-1.dll
GCCDLL=gcc_s-1.dll
endif

ifeq ($(MIN_WIN_VER),)
MIN_WIN_VER=0x0600
endif

# msys2 has dlls in...
DLLPATH_PREFIX=$(ARCHDIR)/bin/

#
# Make rules
#

# by default build all installers
all: gtk3-installer gtk2-installer sdl2-installer qt-installer ruledit-installer

gtk3x-installer:
	make GUI=gtk3x FCMP=gtk3x EXTRA_CONFIG="--disable-ruledit $(EXTRA_CONFIG)" wrap-gtk3x

# These are what can be built in current official environment version
buildable-win32: gtk3-installer gtk3x-installer

buildable-win64: gtk3-installer gtk3x-installer

# See comments on 'snapshot' target why this wrapping is needed
buildable: buildable-$(WINARCH)

gtk3-installer:
	make GUI=gtk3 FCMP=gtk3 EXTRA_CONFIG="--disable-ruledit $(EXTRA_CONFIG)" wrap-gtk3

gtk2-installer:
	make GUI=gtk2 FCMP=gtk2 EXTRA_CONFIG="--disable-ruledit $(EXTRA_CONFIG)" wrap-gtk2

sdl2-installer:
	make GUI=sdl2 FCMP=gtk3 EXTRA_CONFIG="--disable-ruledit $(EXTRA_CONFIG)" wrap-sdl2

qt-installer:
	make GUI=qt FCMP=qt EXTRA_CONFIG="--with-qt5-includes=$(ARCHDIR)/include --with-qt5-libs=$(ARCHDIR)/lib $(EXTRA_CONFIG)" wrap-qt

ruledit-installer:
	make wrap-ruledit

# Autogen.sh here will make ./win_arch.sh to work.
# The $(WINACRH) value we got for this run is not good. The new run
# launched from this target will pick correct value, and can make 'buildable' target
# to depend on correct arch version.
snapshot:
	cd ../.. ; ./autogen.sh --no-configure-run
	make VERSION_REVTYPE="svn" EXTRA_CONFIG="--enable-svnrev" buildable


# build Freeciv

build-freeciv-common:
	# create build directory
	mkdir -p build-$(WINARCH)-$(GUI)
	# configure
	cd build-$(WINARCH)-$(GUI); ../../../configure FREECIV_LABEL_FORCE="<base>-msys2" CPPFLAGS="-D_WIN32_WINNT=$(MIN_WIN_VER)" --with-project-definition=../../../bootstrap/snapshot.project --enable-client=$(GUI) --with-followtag="win32-S3_0" --without-readline --disable-sdl-mixer --without-libbz2 --enable-fcmp=$(FCMP) $(EXTRA_CONFIG)
	# make
	make -C build-$(WINARCH)-$(GUI)
	make -C build-$(WINARCH)-$(GUI)/translations/core update-po
	make -C build-$(WINARCH)-$(GUI)/bootstrap langstat_core.txt

build-freeciv-ruledit:
	# create build directory
	mkdir -p build-$(WINARCH)-ruledit
	# configure
	cd build-$(WINARCH)-ruledit; ../../../configure FREECIV_LABEL_FORCE="<base>-msys2" CPPFLAGS="-D_WIN32_WINNT=$(MIN_WIN_VER)" --with-project-definition=../../../bootstrap/snapshot.project --without-libbz2 --disable-client --disable-server --disable-fcmp --disable-freeciv-manual --enable-ruledit --with-qt5-includes=$(ARCHDIR)/include --with-qt5-libs=$(ARCHDIR)/lib $(EXTRA_CONFIG)
	# make
	make -C build-$(WINARCH)-ruledit
	make -C build-$(WINARCH)-ruledit/translations/ruledit update-po
	make -C build-$(WINARCH)-ruledit/bootstrap langstat_ruledit.txt

build-freeciv-gtk3x: build-freeciv-common

build-freeciv-gtk3: build-freeciv-common

build-freeciv-gtk2: build-freeciv-common

build-freeciv-sdl2: build-freeciv-common

build-freeciv-qt: build-freeciv-common

# install Freeciv

install-freeciv-common:
	# make install
	make -C build-$(WINARCH)-$(GUI) DESTDIR=`pwd`/install-$(WINARCH)-$(GUI) install
	# reorder directory layout
	mv install-$(WINARCH)-$(GUI)/usr/local/bin/* install-$(WINARCH)-$(GUI)/
	mv install-$(WINARCH)-$(GUI)/usr/local/etc install-$(WINARCH)-$(GUI)/
	mv install-$(WINARCH)-$(GUI)/usr/local/share/freeciv install-$(WINARCH)-$(GUI)/data
	mkdir -p install-$(WINARCH)-$(GUI)/share
	mv install-$(WINARCH)-$(GUI)/usr/local/share/locale install-$(WINARCH)-$(GUI)/share/
	mv install-$(WINARCH)-$(GUI)/usr/local/share/doc install-$(WINARCH)-$(GUI)/doc
	mkdir -p install-$(WINARCH)-$(GUI)/debuginfo
	cp build-$(WINARCH)-$(GUI)/gen_headers/fc_config.h install-$(WINARCH)-$(GUI)/debuginfo
	mkdir -p install-$(WINARCH)-$(GUI)/doc/freeciv/installer
	cp licenses/COPYING.installer install-$(WINARCH)-$(GUI)/doc/freeciv/installer/
	# delete unneeded files
	rm -r install-$(WINARCH)-$(GUI)/usr
	# strip 'freeciv-manual' and 'freeciv-mp-<gui>' executables
	strip install-$(WINARCH)-$(GUI)/freeciv-m*
	# add start menu files
	cp freeciv-server.cmd freeciv-mp-$(FCMP).cmd freeciv-$(GUI).cmd Freeciv.url install-$(WINARCH)-$(GUI)/

install-freeciv-gtk3x: install-freeciv-common

install-freeciv-gtk3: install-freeciv-common

install-freeciv-gtk2: install-freeciv-common

install-freeciv-sdl2: install-freeciv-common
	# add CJK fonts
	cp /opt/fireflysung-1.3.0/fireflysung.ttf install-$(WINARCH)-sdl2/data/themes/gui-sdl2/human/
	cp /opt/fireflysung-1.3.0/COPYRIGHT install-$(WINARCH)-sdl2/data/themes/gui-sdl2/human/COPYING.fireflysung
	cp /opt/sazanami-20040629/sazanami-gothic.ttf install-$(WINARCH)-sdl2/data/themes/gui-sdl2/human/
	cp /opt/sazanami-20040629/README install-$(WINARCH)-sdl2/data/themes/gui-sdl2/human/COPYING.sazanami
	cp /opt/un-fonts/UnDotum.ttf install-$(WINARCH)-sdl2/data/themes/gui-sdl2/human/
	cp /opt/un-fonts/COPYING install-$(WINARCH)-sdl2/data/themes/gui-sdl2/human/COPYING.UnDotum

install-freeciv-qt: install-freeciv-common
	# strip 'freeciv-ruledit' executable
	strip install-$(WINARCH)-$(GUI)/freeciv-ruledit.exe
	cp freeciv-ruledit.cmd install-$(WINARCH)-$(GUI)/

install-ruledit-dir:
	mkdir -p install-$(WINARCH)-ruledit/share

install-ruledit: install-ruledit-dir
	# make install
	make -C build-$(WINARCH)-ruledit DESTDIR=`pwd`/install-$(WINARCH)-ruledit install
	# reorder directory layout
	mv install-$(WINARCH)-ruledit/usr/local/bin/* install-$(WINARCH)-ruledit/
	mv install-$(WINARCH)-ruledit/usr/local/share/freeciv install-$(WINARCH)-ruledit/data
	mv install-$(WINARCH)-ruledit/usr/local/share/locale install-$(WINARCH)-ruledit/share/
	mv install-$(WINARCH)-ruledit/usr/local/share/doc install-$(WINARCH)-ruledit/doc
	mkdir -p install-$(WINARCH)-ruledit/debuginfo
	cp build-$(WINARCH)-ruledit/gen_headers/fc_config.h install-$(WINARCH)-ruledit/debuginfo
	mkdir -p install-$(WINARCH)-ruledit/doc/freeciv/installer
	cp licenses/COPYING.installer install-$(WINARCH)-ruledit/doc/freeciv/installer/
	# delete unneeded files
	rm -r install-$(WINARCH)-ruledit/usr
	# strip 'freeciv-ruledit' executable
	strip install-$(WINARCH)-ruledit/freeciv-ruledit.exe
	# add start menu files
	cp freeciv-ruledit.cmd Freeciv.url install-$(WINARCH)-ruledit/

# install Freeciv environment

COMMON_DLLS := \
	libstdc++-6.dll \
	libatk-1.0-0.dll \
	libbz2-1.dll \
	libcairo-2.dll \
	libcairo-gobject-2.dll \
	libffi-6.dll \
	libfontconfig-1.dll \
	libfreetype-6.dll \
	$(LIBGCCDLL) \
	libgdk_pixbuf-2.0-0.dll \
	libgio-2.0-0.dll \
	libglib-2.0-0.dll \
	libgmodule-2.0-0.dll \
	libgobject-2.0-0.dll \
	libgthread-2.0-0.dll \
	libiconv-2.dll \
	libintl-8.dll \
	liblzma-5.dll \
	libpango-1.0-0.dll \
	libpangocairo-1.0-0.dll \
	libpangowin32-1.0-0.dll \
	libpixman-1-0.dll \
	libpng16-16.dll \
	libxml2-2.dll \
	zlib1.dll \
	libwinpthread-1.dll \
	libicuuc57.dll \
	libicudt57.dll

COMMON_MSYS_DLLS := \
	stdc++-6.dll \
	2.0.dll \
	curl-4.dll \
	idn-11.dll \
	sqlite3-0.dll \
	ssl-1.0.0.dll \
	$(GCCDLL) \
	z.dll \
	bz2-1.dll \
	crypto-1.0.0.dll \
	gssapi-3.dll \
	heimntlm-0.dll \
	krb5-26.dll \
	asn1-8.dll \
	com_err-1.dll \
	roken-18.dll \
	crypt-0.dll \
	wind-0.dll \
	hx509-5.dll \
	heimbase-1.dll \
	iconv-2.dll \
	intl-8.dll \
	ssh2-1.dll \
	lzma-5.dll \
	icuuc56.dll \
	icudata56.dll

install-env-common:
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(COMMON_DLLS)) install-$(WINARCH)-$(GUI)
	cp -R $(addprefix /bin/msys-, $(COMMON_MSYS_DLLS)) install-$(WINARCH)-$(GUI)

DEFAULT_FCMP_DLLS := \
	libgdk-3-0.dll \
	libgtk-3-0.dll \
	libexpat-1.dll \
	libharfbuzz-0.dll \
	libepoxy-0.dll \
	libgraphite2.dll \
	libpangoft2-1.0-0.dll \
	libpcre-1.dll

install-env-default-fcmp: install-env-common
	cp -R $(addprefix $(DLLPATH_PREFIX), $(DEFAULT_FCMP_DLLS)) install-$(WINARCH)-$(GUI)
	# add additional GTK+3 files
	mkdir -p install-$(WINARCH)-$(GUI)/etc
	cp -R $(ARCHDIR)/etc/gtk-3.0 install-$(WINARCH)-$(GUI)/etc/
	mkdir -p install-$(WINARCH)-$(GUI)/share/icons
	cp -R $(ARCHDIR)/share/icons/Adwaita install-$(WINARCH)-$(GUI)/share/icons/
	mkdir -p install-$(WINARCH)-$(GUI)/lib/gtk-3.0/
	cp -R $(ARCHDIR)/lib/gtk-3.0/* install-$(WINARCH)-$(GUI)/lib/gtk-3.0/
	cp -R $(ARCHDIR)/share/locale install-$(WINARCH)-$(GUI)/share/
	find install-$(WINARCH)-$(GUI)/share/locale -type f -not -name "freeciv*.mo" -and -not -name gtk30.mo -delete

install-env-gtk3x: install-env-default-fcmp

install-env-gtk3: install-env-default-fcmp

GTK2_DLLS := \
	libgdk-win32-2.0-0.dll \
	libgtk-win32-2.0-0.dll

install-env-gtk2: install-env-common
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(GTK2_DLLS)) install-$(WINARCH)-$(GUI)
	# add additional GTK+2 files
	mkdir -p install-$(WINARCH)-$(GUI)/etc
	cp -R /etc/gtk-2.0 install-$(WINARCH)-$(GUI)/etc/
	mkdir -p install-$(WINARCH)-$(GUI)/lib/gtk-2.0/
	cp -R /lib/gtk-2.0/2.10.0 install-$(WINARCH)-$(GUI)/lib/gtk-2.0/
	cp -R $(ARCHDIR)/share/locale install-$(WINARCH)-$(GUI)/share/
	find install-$(WINARCH)-$(GUI)/share/locale -type f -not -name "freeciv*.mo" -and -not -name gtk20.mo -delete

SDL2_DLLS := \
	libpng12-0.dll \
	libSDL2_gfx-15.dll \
	SDL2_image.dll \
	SDL2_ttf.dll

install-env-sdl2: install-env-default-fcmp
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(SDL2_DLLS)) install-$(WINARCH)-$(GUI)

QT_DLLS := \
	libicuin57.dll \
	libicuuc57.dll \
	libicudt57.dll \
	libpcre16-0.dll \
	libharfbuzz-0.dll \
	libsqlite3-0.dll \
	Qt5Core.dll \
	Qt5Gui.dll \
	Qt5Widgets.dll

QT_MSYS_DLLS := \
	stdc++-6.dll

install-env-qt: install-env-common
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(QT_DLLS)) install-$(WINARCH)-$(GUI)
	cp -R $(addprefix /bin/msys-, $(QT_MSYS_DLLS)) install-$(WINARCH)-$(GUI)
	# add additional Qt files
	cp -R $(ARCHDIR)/share/qt5/plugins install-$(WINARCH)-$(GUI)/
	cp -R $(ARCHDIR)/share/locale install-$(WINARCH)-$(GUI)/share/
	find install-$(WINARCH)-$(GUI)/share/locale -type f -not -name "freeciv*.mo" -delete

# TODO: Clean out unnecessary ones. These are just copies of COMMON_DLLS and
#       COMMON_MSYS_DLLS with most obviously unnecessary things left out.
RULEDIT_DLLS := \
	libatk-1.0-0.dll \
	libbz2-1.dll \
	libcairo-2.dll \
	libcairo-gobject-2.dll \
	libffi-6.dll \
	libfontconfig-1.dll \
	libfreetype-6.dll \
	$(LIBGCCDLL) \
	libglib-2.0-0.dll \
	libgdk_pixbuf-2.0-0.dll \
	libgio-2.0-0.dll \
	libiconv-2.dll \
	libintl-8.dll \
	liblzma-5.dll \
	libpango-1.0-0.dll \
	libpangocairo-1.0-0.dll \
	libpangowin32-1.0-0.dll \
	libpixman-1-0.dll \
	libpng16-16.dll \
	libxml2-2.dll \
	zlib1.dll \
	libwinpthread-1.dll

RULEDIT_MSYS_DLLS := \
	2.0.dll \
	curl-4.dll \
	idn-11.dll \
	sqlite3-0.dll \
	ssl-1.0.0.dll \
	$(GCCDLL) \
	z.dll \
	bz2-1.dll \
	crypto-1.0.0.dll \
	gssapi-3.dll \
	heimntlm-0.dll \
	krb5-26.dll \
	asn1-8.dll \
	com_err-1.dll \
	roken-18.dll \
	crypt-0.dll \
	wind-0.dll \
	hx509-5.dll \
	heimbase-1.dll \
	iconv-2.dll \
	intl-8.dll \
	ssh2-1.dll \
	lzma-5.dll

install-env-ruledit:
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(RULEDIT_DLLS)) install-$(WINARCH)-ruledit
	cp -R $(addprefix $(DLLPATH_PREFIX), $(QT_DLLS)) install-$(WINARCH)-ruledit
	cp -R $(addprefix /bin/msys-, $(RULEDIT_MSYS_DLLS)) install-$(WINARCH)-ruledit
	cp -R $(addprefix /bin/msys-, $(QT_MSYS_DLLS)) install-$(WINARCH)-ruledit
	# add additional Qt files
	cp -R $(ARCHDIR)/share/qt5/plugins install-$(WINARCH)-ruledit/
	cp -R /usr/share/locale install-$(WINARCH)-ruledit/share/
	find install-$(WINARCH)-ruledit/share/locale -type f -not -name "freeciv-ruledit.mo" -delete

# build installer

installer-common: clean-install-common install-freeciv-$(GUI) install-env-$(GUI)
	# extract Freeciv version
	$(eval FREECIV_VERSION := $(shell ../../fc_version))
	# create NSIS script
	./create-freeciv-$(GUI)-nsi.sh install-$(WINARCH)-$(GUI) $(FREECIV_VERSION) $(WINARCH) > Freeciv-$(FREECIV_VERSION)-$(GUI).nsi
	# create installer executable
	mkdir -p Output
	makensis Freeciv-$(FREECIV_VERSION)-$(GUI).nsi

installer-ruledit: clean-install-ruledit install-ruledit install-env-ruledit
	# extract Freeciv version
	$(eval FREECIV_VERSION := $(shell ../../fc_version))
	# create NSIS script
	./create-freeciv-ruledit-nsi.sh install-$(WINARCH)-ruledit $(FREECIV_VERSION) $(WINARCH) > Freeciv-$(FREECIV_VERSION)-ruledit.nsi
	# create installer executable
	mkdir -p Output
	makensis Freeciv-$(FREECIV_VERSION)-ruledit.nsi

wrap-gtk3x: build-freeciv-gtk3x installer-common

wrap-gtk3: build-freeciv-gtk3 installer-common

wrap-gtk2: build-freeciv-gtk2 installer-common

wrap-sdl2: build-freeciv-sdl2 installer-common

wrap-qt: build-freeciv-qt installer-common

wrap-ruledit: build-freeciv-ruledit installer-ruledit


# cleanup

clean-build-common:
	rm -rf build-*-$(GUI)

clean-build-ruledit:
	rm -rf build-*-ruledit

clean-install-common:
	rm -rf install-*-$(GUI)
	./cleanup_checkout.sh ../..

clean-install-ruledit:
	rm -rf install-*-ruledit
	./cleanup_checkout.sh ../..

clean-installer-common:
	rm -f Output/Freeciv-*-$(GUI)-setup.exe
	rm -f Freeciv-*-$(GUI).nsi

clean-installer-ruledit:
	rm -f Output/Freeciv-ruledit-*-setup.exe
	rm -f Freeciv-*-ruledit.nsi

clean:
	make GUI=gtk3 clean-build-common clean-install-common clean-installer-common
	make GUI=gtk3x clean-build-common clean-install-common clean-installer-common
	make GUI=gtk2 clean-build-common clean-install-common clean-installer-common
	make GUI=sdl2 clean-build-common clean-install-common clean-installer-common
	make GUI=qt clean-build-common clean-install-common clean-installer-common
	make clean-build-ruledit clean-install-ruledit clean-installer-ruledit
