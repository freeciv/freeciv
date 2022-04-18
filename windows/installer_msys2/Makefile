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
ARCHDIR=mingw64
LIBGCCDLL=libgcc_s_seh-1.dll
GCCDLL=gcc_s-seh-1.dll
CRYPTOLIB_SUFFIX=-x64
else
ARCHDIR=mingw32
LIBGCCDLL=libgcc_s_dw2-1.dll
GCCDLL=gcc_s-1.dll
CRYPTOLIB_SUFFIX=
endif

ifeq ($(MIN_WIN_VER),)
MIN_WIN_VER=0x0603
endif

ifeq ($(MAKE_PARAMS),)
MAKE_PARAMS="-j$(shell nproc)"
endif

ifeq ($(MOCCMD),)
QT6_MOCCMD="/$(ARCHDIR)/share/qt6/bin/moc.exe"
else
QT6_MOCCMD="$(MOCCMD)"
endif

ICUVER=69

INSTSUB=${ARCHDIR}

# msys2 has dlls in...
DLLPATH_PREFIX=/$(ARCHDIR)/bin/

ifeq ($(CI),yes)
MAGICKVER=7.Q16HDRI-10
else
MAGICKVER=7.Q16HDRI-7
endif

#
# Make rules
#

# by default build all installers.
all: all-$(WINARCH)

all-win64: gtk3.22-installer sdl2-installer ruledit-installer qt5-installer qt6-installer

all-win32: gtk3.22-installer sdl2-installer ruledit-installer-qt5 qt5-installer

gtk3.22-installer:
	make GUI=gtk3.22 CLIENT=gtk3.22 FCMP=gtk3 EXTRA_CONFIG="--disable-ruledit $(EXTRA_CONFIG)" wrap-gtk3.22

sdl2-installer:
	make GUI=sdl2 CLIENT=sdl2 FCMP=gtk4 EXTRA_CONFIG="--disable-ruledit $(EXTRA_CONFIG)" wrap-sdl2

qt-installer: qt5-installer

qt5-installer:
	make GUI=qt5 CLIENT=qt FCMP=qt EXTRA_CONFIG="--with-qtver=qt5 --with-qt5-includes=/$(ARCHDIR)/include --with-qt5-libs=/$(ARCHDIR)/lib $(EXTRA_CONFIG)" wrap-qt5

qt6-installer:
	make GUI=qt6 CLIENT=qt FCMP=qt EXTRA_CONFIG="--with-qtver=qt6 --with-qt6-includes=/$(ARCHDIR)/include/qt6 --with-qt6-libs=/$(ARCHDIR)/lib $(EXTRA_CONFIG)" MOCCMD="$(QT6_MOCCMD)" wrap-qt6

ruledit-installer: ruledit-installer-qt6

ruledit-installer-qt5:
	# EXTRA_CONFIG="$(EXTRA_CONFIG)" *does* make a difference - it unwraps one layer
	# of escapes, needed for snapshot builds.
	make EXTRA_CONFIG="$(EXTRA_CONFIG)" wrap-ruledit-qt5

ruledit-installer-qt6:
	# EXTRA_CONFIG="$(EXTRA_CONFIG)" *does* make a difference - it unwraps one layer
	# of escapes, needed for snapshot builds.
	make EXTRA_CONFIG="$(EXTRA_CONFIG)" MOCCMD="$(QT6_MOCCMD)" wrap-ruledit-qt6

# Autogen.sh here will make ./win_arch.sh to work.
# The $(WINARCH) value we got for this run is not good. The new run
# launched from this target will pick correct value.
snapshot:
	cd ../.. ; ./autogen.sh --no-configure-run
	make VERSION_REVTYPE="git" EXTRA_CONFIG="--enable-gitrev --with-project-definition=../../../bootstrap/snapshot.project CFLAGS=\\\"-Werror $$CFLAGS\\\" CXXFLAGS=\\\"-Werror $$CXXFLAGS\\\"" all

ci:
	cd ../.. ; ./autogen.sh --no-configure-run
	make VERSION_REVTYPE="git" CI=yes EXTRA_CONFIG="--enable-gitrev --with-project-definition=../../../bootstrap/snapshot.project CFLAGS=\\\"-Werror $$CFLAGS\\\" CXXFLAGS=\\\"-Werror $$CXXFLAGS\\\"" gtk3.22-installer sdl2-installer ruledit-installer-qt6 qt6-installer

# configure Freeciv

configure-freeciv-common:
	# create build directory
	mkdir -p build-$(WINARCH)-$(GUI)
	# configure
	cd build-$(WINARCH)-$(GUI); ../../../configure FREECIV_LABEL_FORCE="<base>-msys2" CPPFLAGS="-D_WIN32_WINNT=$(MIN_WIN_VER)" --enable-client=$(CLIENT) --with-followtag="windows-S3_1" --with-tinycthread --without-readline --disable-sys-lua --enable-fcdb=sqlite3 --with-sqlite3-prefix=/$(ARCHDIR) --enable-fcmp=$(FCMP) --with-cacert-path="./ssl/certs/ca-bundle.crt" $(EXTRA_CONFIG)

configure-freeciv-ruledit-qt5:
	# create build directory
	mkdir -p build-$(WINARCH)-ruledit-qt5
	# configure
	cd build-$(WINARCH)-ruledit-qt5; ../../../configure FREECIV_LABEL_FORCE="<base>-msys2" CPPFLAGS="-D_WIN32_WINNT=$(MIN_WIN_VER)" --with-qtver=qt5 --disable-client --disable-server --disable-fcmp --disable-freeciv-manual --disable-sys-lua --enable-ruledit --enable-ai-static=stub --with-cacert-path="./ssl/certs/ca-bundle.crt" --with-qt5-includes=/$(ARCHDIR)/include --with-qt5-libs=/$(ARCHDIR)/lib $(EXTRA_CONFIG)

configure-freeciv-ruledit-qt6:
	# create build directory
	mkdir -p build-$(WINARCH)-ruledit-qt6
	# configure
	cd build-$(WINARCH)-ruledit-qt6; ../../../configure FREECIV_LABEL_FORCE="<base>-msys2" CPPFLAGS="-D_WIN32_WINNT=$(MIN_WIN_VER)" --with-qtver=qt6 --disable-client --disable-server --disable-fcmp --disable-freeciv-manual --disable-sys-lua --enable-ruledit --enable-ai-static=stub --with-cacert-path="./ssl/certs/ca-bundle.crt" --with-qt6-includes=/$(ARCHDIR)/include/qt6 --with-qt6-libs=/$(ARCHDIR)/lib $(EXTRA_CONFIG)

# install Freeciv

install-freeciv-common: configure-freeciv-common
	# make .gmo and langstat files. For the former, this needs to be before 'make install'
	make -C build-$(WINARCH)-$(GUI)/translations/core update-po
	make -C build-$(WINARCH)-$(GUI)/bootstrap langstat_core.txt
	# Restore .po files modified by the above update-po, so that repository does not
	# appear modified, in case of --enable-gitrev
	./cleanup_checkout.sh ../..
	# make install
	make -C build-$(WINARCH)-$(GUI) DESTDIR=`pwd`/install-$(WINARCH)-$(GUI) $(MAKE_PARAMS) install
	# reorder directory layout
	mv install-$(WINARCH)-$(GUI)/$(INSTSUB)/bin/* install-$(WINARCH)-$(GUI)/
	mv install-$(WINARCH)-$(GUI)/$(INSTSUB)/etc install-$(WINARCH)-$(GUI)/
	mv install-$(WINARCH)-$(GUI)/$(INSTSUB)/share/freeciv install-$(WINARCH)-$(GUI)/data
	mkdir -p install-$(WINARCH)-$(GUI)/share
	mv install-$(WINARCH)-$(GUI)/$(INSTSUB)/share/locale install-$(WINARCH)-$(GUI)/share/
	mv install-$(WINARCH)-$(GUI)/$(INSTSUB)/share/doc install-$(WINARCH)-$(GUI)/doc
	mkdir -p install-$(WINARCH)-$(GUI)/debuginfo
	cp build-$(WINARCH)-$(GUI)/gen_headers/fc_config.h install-$(WINARCH)-$(GUI)/debuginfo
	mkdir -p install-$(WINARCH)-$(GUI)/doc/freeciv/installer
	cat licenses/header.txt ../../COPYING > install-$(WINARCH)-$(GUI)/doc/freeciv/installer/COPYING.installer
	# delete unneeded files
	rm -r install-$(WINARCH)-$(GUI)/$(INSTSUB)
	# strip 'freeciv-manual' and 'freeciv-mp-<gui>' executables
	strip install-$(WINARCH)-$(GUI)/freeciv-m*
	# add start menu files
	cp freeciv-server.cmd freeciv-mp-$(FCMP).cmd freeciv-$(CLIENT).cmd Freeciv.url install-$(WINARCH)-$(GUI)/

install-freeciv-gtk3.22: install-freeciv-common

install-freeciv-sdl2: install-freeciv-common

install-freeciv-qt5: install-freeciv-common
	# strip 'freeciv-ruledit' executable
	strip install-$(WINARCH)-$(GUI)/freeciv-ruledit.exe
	cp freeciv-ruledit.cmd install-$(WINARCH)-$(GUI)/

install-freeciv-qt6: install-freeciv-common
	# strip 'freeciv-ruledit' executable
	strip install-$(WINARCH)-$(GUI)/freeciv-ruledit.exe
	cp freeciv-ruledit.cmd install-$(WINARCH)-$(GUI)/

install-ruledit-dir-qt5:
	mkdir -p install-$(WINARCH)-ruledit-qt5/share

install-ruledit-dir-qt6:
	mkdir -p install-$(WINARCH)-ruledit-qt6/share

install-ruledit-qt5: configure-freeciv-ruledit-qt5 install-ruledit-dir-qt5
	# make .gmo and langstat files. For the former, this needs to be before 'make install'
	make -C build-$(WINARCH)-ruledit-qt5/translations/ruledit update-po
	make -C build-$(WINARCH)-ruledit-qt5/bootstrap langstat_ruledit.txt
	# Restore .po files modified by the above update-po, so that repository does not
	# appear modified, in case of --enable-gitrev
	./cleanup_checkout.sh ../..
	# make install
	make -C build-$(WINARCH)-ruledit-qt5 DESTDIR=`pwd`/install-$(WINARCH)-ruledit-qt5/ $(MAKE_PARAMS) install
	# reorder directory layout
	mv install-$(WINARCH)-ruledit-qt5/$(INSTSUB)/bin/* install-$(WINARCH)-ruledit-qt5/
	mv install-$(WINARCH)-ruledit-qt5/$(INSTSUB)/share/freeciv install-$(WINARCH)-ruledit-qt5/data
	mv install-$(WINARCH)-ruledit-qt5/$(INSTSUB)/share/locale install-$(WINARCH)-ruledit-qt5/share/
	mv install-$(WINARCH)-ruledit-qt5/$(INSTSUB)/share/doc install-$(WINARCH)-ruledit-qt5/doc
	mkdir -p install-$(WINARCH)-ruledit-qt5/debuginfo
	cp build-$(WINARCH)-ruledit-qt5/gen_headers/fc_config.h install-$(WINARCH)-ruledit-qt5/debuginfo
	mkdir -p install-$(WINARCH)-ruledit-qt5/doc/freeciv/installer
	cat licenses/header.txt ../../COPYING > install-$(WINARCH)-ruledit-qt5/doc/freeciv/installer/COPYING.installer
	# delete unneeded files
	rm -r install-$(WINARCH)-ruledit-qt5/$(INSTSUB)
	# strip 'freeciv-ruledit' executable
	strip install-$(WINARCH)-ruledit-qt5/freeciv-ruledit.exe
	# add start menu files
	cp freeciv-ruledit.cmd Freeciv.url install-$(WINARCH)-ruledit-qt5/

install-ruledit-qt6: configure-freeciv-ruledit-qt6 install-ruledit-dir-qt6
	# make .gmo and langstat files. For the former, this needs to be before 'make install'
	make -C build-$(WINARCH)-ruledit-qt6/translations/ruledit update-po
	make -C build-$(WINARCH)-ruledit-qt6/bootstrap langstat_ruledit.txt
	# Restore .po files modified by the above update-po, so that repository does not
	# appear modified, in case of --enable-gitrev
	./cleanup_checkout.sh ../..
	# make install
	make -C build-$(WINARCH)-ruledit-qt6 DESTDIR=`pwd`/install-$(WINARCH)-ruledit-qt6/ $(MAKE_PARAMS) install
	# reorder directory layout
	mv install-$(WINARCH)-ruledit-qt6/$(INSTSUB)/bin/* install-$(WINARCH)-ruledit-qt6/
	mv install-$(WINARCH)-ruledit-qt6/$(INSTSUB)/share/freeciv install-$(WINARCH)-ruledit-qt6/data
	mv install-$(WINARCH)-ruledit-qt6/$(INSTSUB)/share/locale install-$(WINARCH)-ruledit-qt6/share/
	mv install-$(WINARCH)-ruledit-qt6/$(INSTSUB)/share/doc install-$(WINARCH)-ruledit-qt6/doc
	mkdir -p install-$(WINARCH)-ruledit-qt6/debuginfo
	cp build-$(WINARCH)-ruledit-qt6/gen_headers/fc_config.h install-$(WINARCH)-ruledit-qt6/debuginfo
	mkdir -p install-$(WINARCH)-ruledit-qt6/doc/freeciv/installer
	cat licenses/header.txt ../../COPYING > install-$(WINARCH)-ruledit-qt6/doc/freeciv/installer/COPYING.installer
	# delete unneeded files
	rm -r install-$(WINARCH)-ruledit-qt6/$(INSTSUB)
	# strip 'freeciv-ruledit' executable
	strip install-$(WINARCH)-ruledit-qt6/freeciv-ruledit.exe
	# add start menu files
	cp freeciv-ruledit.cmd Freeciv.url install-$(WINARCH)-ruledit-qt6/

# install Freeciv environment

SOUND_DLLS := \
	SDL2.dll \
	SDL2_mixer.dll \
	libFLAC.dll \
	libfluidsynth-3.dll \
	libmodplug-1.dll \
	libvorbis-0.dll \
	libvorbisenc-2.dll \
	libvorbisfile-3.dll \
	libogg-0.dll \
	libportaudio.dll \
	libsndfile-1.dll \
	libmpg123-0.dll \
	libopusfile-0.dll \
	libopus-0.dll

COMMON_DLLS := \
	libstdc++-6.dll \
	libcrypto-1_1${CRYPTOLIB_SUFFIX}.dll \
	libpsl-5.dll \
	libssl-1_1${CRYPTOLIB_SUFFIX}.dll \
	libidn2-0.dll \
	libnghttp2-14.dll \
	libssh2-1.dll \
	libcurl-4.dll \
	libgmp-10.dll \
	libp11-kit-0.dll \
	libtasn1-6.dll \
	libatk-1.0-0.dll \
	libbz2-1.dll \
	libcairo-2.dll \
	libcairo-gobject-2.dll \
	libffi-7.dll \
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
	libfribidi-0.dll \
	libpixman-1-0.dll \
	libpng16-16.dll \
	libxml2-2.dll \
	zlib1.dll \
	libwinpthread-1.dll \
	libicuuc${ICUVER}.dll \
	libicudt${ICUVER}.dll \
	libsqlite3-0.dll \
	exchndl.dll \
	mgwhelp.dll \
	libbrotlidec.dll \
	libbrotlicommon.dll \
	libreadline8.dll \
	libtermcap-0.dll \
	libssp-0.dll \
	libzstd.dll \
	libMagickWand-${MAGICKVER}.dll \
	libMagickCore-${MAGICKVER}.dll \
	libgomp-1.dll \
	libfftw3-3.dll \
	liblcms2-2.dll \
	liblqr-1-0.dll \
	libltdl-7.dll \
	libraqm-0.dll \
	libexpat-1.dll \
	$(SOUND_DLLS)

install-env-common:
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(COMMON_DLLS)) install-$(WINARCH)-$(GUI)/
	mkdir -p install-$(WINARCH)-$(GUI)/ssl/certs
	cp -R /$(ARCHDIR)/ssl/certs/ca-bundle.crt install-$(WINARCH)-$(GUI)/ssl/certs/

DEFAULT_FCMP_DLLS := \
	libgdk-3-0.dll \
	libgtk-3-0.dll \
	libharfbuzz-0.dll \
	libepoxy-0.dll \
	libgraphite2.dll \
	libpangoft2-1.0-0.dll \
	libthai-0.dll \
	libdatrie-1.dll \
	libpcre-1.dll \
	libunistring-2.dll \
	librsvg-2-2.dll

install-env-default-fcmp: install-env-common
	cp -R $(addprefix $(DLLPATH_PREFIX), $(DEFAULT_FCMP_DLLS)) install-$(WINARCH)-$(GUI)/
	# add additional GTK+3 files
	mkdir -p install-$(WINARCH)-$(GUI)/etc
	cp -R /$(ARCHDIR)/etc/gtk-3.0 install-$(WINARCH)-$(GUI)/etc/
	mkdir -p install-$(WINARCH)-$(GUI)/share/icons
	cp -R /$(ARCHDIR)/share/icons/Adwaita install-$(WINARCH)-$(GUI)/share/icons/
	mkdir -p install-$(WINARCH)-$(GUI)/share/glib-2.0/schemas
	cp /$(ARCHDIR)/share/glib-2.0/schemas/gschemas.compiled install-$(WINARCH)-$(GUI)/share/glib-2.0/schemas/
	mkdir -p install-$(WINARCH)-$(GUI)/lib/gtk-3.0/
	cp -R /$(ARCHDIR)/lib/gtk-3.0/* install-$(WINARCH)-$(GUI)/lib/gtk-3.0/
	mkdir -p install-$(WINARCH)-$(GUI)/lib/gdk-pixbuf-2.0/
	cp -R /$(ARCHDIR)/lib/gdk-pixbuf-2.0/* install-$(WINARCH)-$(GUI)/lib/gdk-pixbuf-2.0/
	cp -R /$(ARCHDIR)/share/locale install-$(WINARCH)-$(GUI)/share/
	find install-$(WINARCH)-$(GUI)/share/locale -type f -not -name "freeciv-*.mo" -and -not -name gtk30.mo -delete

install-env-gtk3.22: install-env-default-fcmp

GTK4_FCMP_DLLS := \
	libgtk-4-1.dll \
	libpcre-1.dll \
	libcairo-script-interpreter-2.dll \
	libepoxy-0.dll \
	libgraphene-1.0-0.dll \
	libharfbuzz-0.dll \
	libpangoft2-1.0-0.dll \
	libunistring-2.dll \
	libthai-0.dll \
	liblzo2-2.dll \
	libgraphite2.dll \
	libdatrie-1.dll

install-env-gtk4-fcmp: install-env-common
	cp -R $(addprefix $(DLLPATH_PREFIX), $(GTK4_FCMP_DLLS)) install-$(WINARCH)-$(GUI)/
	# add additional GTK4 files
	mkdir -p install-$(WINARCH)-$(GUI)/lib/gdk-pixbuf-2.0/
	cp -R /$(ARCHDIR)/lib/gdk-pixbuf-2.0/* install-$(WINARCH)-$(GUI)/lib/gdk-pixbuf-2.0/
	cp -R /$(ARCHDIR)/share/locale install-$(WINARCH)-$(GUI)/share/
	find install-$(WINARCH)-$(GUI)/share/locale -type f -not -name "freeciv-*.mo" -and -not -name gtk40.mo -delete

SDL2_DLLS := \
	libSDL2_gfx-1-0-0.dll \
	SDL2_image.dll \
	SDL2_ttf.dll \
	libjpeg-8.dll \
	libtiff-5.dll \
	libwebp-7.dll \
	libzstd.dll \
	libdeflate.dll \
	libjbig-0.dll \
	libLerc.dll

install-env-sdl2: install-env-gtk4-fcmp
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(SDL2_DLLS)) install-$(WINARCH)-$(GUI)/

QT5_DLLS := \
	libicuin${ICUVER}.dll \
	libicuuc${ICUVER}.dll \
	libicudt${ICUVER}.dll \
	libpcre16-0.dll \
	libharfbuzz-0.dll \
	libsqlite3-0.dll \
	Qt5Core.dll \
	Qt5Gui.dll \
	Qt5Widgets.dll \
	libgraphite2.dll \
	libpcre-1.dll \
	libpcre2-16-0.dll \
	libunistring-2.dll \
	libbrotlidec.dll \
	libbrotlicommon.dll \
	libdouble-conversion.dll \
	libzstd.dll \
	libmd4c.dll

install-env-qt5: install-env-common
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(QT5_DLLS)) install-$(WINARCH)-$(GUI)/
	# add additional Qt5 files
	cp -R /$(ARCHDIR)/share/qt5/plugins install-$(WINARCH)-$(GUI)/
	cp -R /$(ARCHDIR)/share/locale install-$(WINARCH)-$(GUI)/share/
	find install-$(WINARCH)-$(GUI)/share/locale -type f -not -name "freeciv-*.mo" -delete

QT6_DLLS := \
	libicuin${ICUVER}.dll \
	libicuuc${ICUVER}.dll \
	libicudt${ICUVER}.dll \
	Qt6Core.dll \
	Qt6Gui.dll \
	Qt6Widgets.dll \
	libunistring-2.dll \
	libpcre2-16-0.dll \
	libpcre-1.dll \
	libharfbuzz-0.dll \
	libgraphite2.dll \
	libdouble-conversion.dll \
	libmd4c.dll \
	libb2-1.dll

install-env-qt6: install-env-common
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(QT6_DLLS)) install-$(WINARCH)-$(GUI)/
	# add additional Qt6 files
	cp -R /$(ARCHDIR)/share/qt6/plugins install-$(WINARCH)-$(GUI)/
	cp -R /$(ARCHDIR)/share/locale install-$(WINARCH)-$(GUI)/share/
	find install-$(WINARCH)-$(GUI)/share/locale -type f -not -name "freeciv-*.mo" -delete

RULEDIT_QT5_DLLS := \
        libstdc++-6.dll \
	libcrypto-1_1${CRYPTOLIB_SUFFIX}.dll \
	libpsl-5.dll \
	libssl-1_1${CRYPTOLIB_SUFFIX}.dll \
	libidn2-0.dll \
	libnghttp2-14.dll \
	libssh2-1.dll \
	libcurl-4.dll \
	libgmp-10.dll \
	libp11-kit-0.dll \
	libtasn1-6.dll \
	libatk-1.0-0.dll \
	libbz2-1.dll \
	libcairo-2.dll \
	libcairo-gobject-2.dll \
	libffi-7.dll \
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
	libwinpthread-1.dll \
	libgraphite2.dll \
	libpcre-1.dll \
	libdouble-conversion.dll \
	libzstd.dll \
	libMagickWand-${MAGICKVER}.dll \
	libMagickCore-${MAGICKVER}.dll \
	libgomp-1.dll \
	libfftw3-3.dll \
	liblcms2-2.dll \
	liblqr-1-0.dll \
	libltdl-7.dll \
	libraqm-0.dll \
	libexpat-1.dll \
	libfribidi-0.dll \
	libmd4c.dll

install-env-ruledit-qt5:
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(RULEDIT_QT5_DLLS)) install-$(WINARCH)-ruledit-qt5/
	cp -R $(addprefix $(DLLPATH_PREFIX), $(QT5_DLLS)) install-$(WINARCH)-ruledit-qt5/
	# add additional Qt files
	cp -R /$(ARCHDIR)/share/qt5/plugins install-$(WINARCH)-ruledit-qt5/
	cp -R /usr/share/locale install-$(WINARCH)-ruledit-qt5/share/
	find install-$(WINARCH)-ruledit-qt5/share/locale -type f -not -name "freeciv-*.mo" -delete

RULEDIT_QT6_DLLS := \
        libstdc++-6.dll \
	libcrypto-1_1${CRYPTOLIB_SUFFIX}.dll \
	libpsl-5.dll \
	libssl-1_1${CRYPTOLIB_SUFFIX}.dll \
	libidn2-0.dll \
	libnghttp2-14.dll \
	libssh2-1.dll \
	libcurl-4.dll \
	libgmp-10.dll \
	libp11-kit-0.dll \
	libtasn1-6.dll \
	libatk-1.0-0.dll \
	libbz2-1.dll \
	libcairo-2.dll \
	libcairo-gobject-2.dll \
	libffi-7.dll \
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
	libwinpthread-1.dll \
	libgraphite2.dll \
	libpcre-1.dll \
	libdouble-conversion.dll \
	libzstd.dll \
	libMagickWand-${MAGICKVER}.dll \
	libMagickCore-${MAGICKVER}.dll \
	libgomp-1.dll \
	libfftw3-3.dll \
	liblcms2-2.dll \
	liblqr-1-0.dll \
	libltdl-7.dll \
	libraqm-0.dll \
	libexpat-1.dll \
	libfribidi-0.dll \
	libsqlite3-0.dll \
	libbrotlidec.dll \
	libbrotlicommon.dll

install-env-ruledit-qt6:
	# add DLLs
	cp -R $(addprefix $(DLLPATH_PREFIX), $(RULEDIT_QT6_DLLS)) install-$(WINARCH)-ruledit-qt6/
	cp -R $(addprefix $(DLLPATH_PREFIX), $(QT6_DLLS)) install-$(WINARCH)-ruledit-qt6/
	# add additional Qt files
	cp -R /$(ARCHDIR)/share/qt6/plugins install-$(WINARCH)-ruledit-qt6/
	cp -R /usr/share/locale install-$(WINARCH)-ruledit-qt6/share/
	find install-$(WINARCH)-ruledit-qt6/share/locale -type f -not -name "freeciv-*.mo" -delete

# build installer

installer-common: clean-install-common install-freeciv-$(GUI) install-env-$(GUI)
	# extract Freeciv version
	$(eval FREECIV_VERSION := $(shell ../../fc_version))
	# create NSIS script
	./create-freeciv-$(GUI)-nsi.sh install-$(WINARCH)-$(GUI) $(FREECIV_VERSION) $(WINARCH) > Freeciv-$(WINARCH)-$(FREECIV_VERSION)-$(GUI).nsi
	# create installer executable
	mkdir -p Output
	makensis Freeciv-$(WINARCH)-$(FREECIV_VERSION)-$(GUI).nsi

installer-ruledit-qt5: clean-install-ruledit-qt5 install-ruledit-qt5 install-env-ruledit-qt5
	# extract Freeciv version
	$(eval FREECIV_VERSION := $(shell ../../fc_version))
	# create NSIS script
	./create-freeciv-ruledit-nsi.sh install-$(WINARCH)-ruledit-qt5 $(FREECIV_VERSION) $(WINARCH) > Freeciv-$(WINARCH)-$(FREECIV_VERSION)-ruledit-qt5.nsi
	# create installer executable
	mkdir -p Output
	makensis Freeciv-$(WINARCH)-$(FREECIV_VERSION)-ruledit-qt5.nsi

installer-ruledit-qt6: clean-install-ruledit-qt6 install-ruledit-qt6 install-env-ruledit-qt6
	# extract Freeciv version
	$(eval FREECIV_VERSION := $(shell ../../fc_version))
	# create NSIS script
	./create-freeciv-ruledit-nsi.sh install-$(WINARCH)-ruledit-qt6 $(FREECIV_VERSION) $(WINARCH) > Freeciv-$(WINARCH)-$(FREECIV_VERSION)-ruledit-qt6.nsi
	# create installer executable
	mkdir -p Output
	makensis Freeciv-$(WINARCH)-$(FREECIV_VERSION)-ruledit-qt6.nsi

wrap-gtk3.22: installer-common

wrap-sdl2: installer-common

wrap-qt5: installer-common

wrap-qt6: installer-common

wrap-ruledit-qt5: installer-ruledit-qt5

wrap-ruledit-qt6: installer-ruledit-qt6

# cleanup

clean-build-common:
	rm -rf build-*-$(GUI)

clean-build-ruledit-qt5:
	rm -rf build-*-ruledit-qt5

clean-build-ruledit-qt6:
	rm -rf build-*-ruledit-qt6

clean-install-common:
	rm -rf install-*-$(GUI)
	./cleanup_checkout.sh ../..

clean-install-ruledit-qt5:
	rm -rf install-*-ruledit-qt5
	./cleanup_checkout.sh ../..

clean-install-ruledit-qt6:
	rm -rf install-*-ruledit-qt6
	./cleanup_checkout.sh ../..

clean-installer-common:
	rm -f Output/Freeciv-*-$(GUI)-setup.exe
	rm -f Freeciv-*-$(GUI).nsi

clean-installer-ruledit-qt5:
	rm -f Output/Freeciv-ruledit-*-setup.exe
	rm -f Freeciv-*-ruledit-qt5.nsi

clean-installer-ruledit-qt6:
	rm -f Output/Freeciv-ruledit-*-setup.exe
	rm -f Freeciv-*-ruledit-qt6.nsi

clean:
	make GUI=gtk3.22 CLIENT=gtk3.22 clean-build-common clean-install-common clean-installer-common
	make GUI=sdl2 CLIENT=sdl2 clean-build-common clean-install-common clean-installer-common
	make GUI=qt5 CLIENT=qt clean-build-common clean-install-common clean-installer-common
	make GUI=qt6 CLIENT=qt clean-build-common clean-install-common clean-installer-common
	make clean-build-ruledit-qt5 clean-install-ruledit-qt5 clean-installer-ruledit-qt5
	make clean-build-ruledit-qt6 clean-install-ruledit-qt6 clean-installer-ruledit-qt6
