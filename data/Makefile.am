## Process this file with automake to produce Makefile.in

if CLIENT
CLIENT_FILES =				\
	amplio2.modpack			\
	amplio2.tilespec		\
	cimpletoon.modpack		\
	cimpletoon.tilespec		\
	hex2t.modpack			\
	hex2t.tilespec			\
	hexemplio.modpack		\
	hexemplio.tilespec		\
	isophex.modpack			\
	isophex.tilespec		\
	isotrident.modpack		\
	isotrident.tilespec		\
	toonhex.modpack			\
	toonhex.tilespec		\
	alio.modpack			\
	alio.tilespec			\
	trident.modpack			\
	trident.tilespec		\
	stdmusic.musicspec		\
	stdsounds.soundspec		\
	helpdata.txt

if CLIENT_GUI_GTK_3_22
CLIENT_FILES += \
	gtk3.22_menus.xml
endif
else
CLIENT_FILES =
endif

SRV_RE_FILES = \
	civ1.modpack \
	civ2.modpack \
	classic.modpack \
	default.modpack \
	default.serv \
	multiplayer.modpack \
	alien.modpack \
	civ2civ3.modpack \
	sandbox.modpack \
	granularity.modpack

if SERVER
SERVER_FILES = $(SRV_RE_FILES)
else
if RULEDIT
SERVER_FILES = $(SRV_RE_FILES)
else
SERVER_FILES =
endif
endif

## Icons to be installed under FREECIV_DATA_PATH
if RULEDIT
DATAPATH_ICONS = freeciv-ruledit.png
RULEDITDIRS = ruledit
else
DATAPATH_ICONS =
RULEDITDIRS =
endif
if MODINST
DATAPATH_ICONS += freeciv-modpack.png
endif

pkgdata_DATA = $(SERVER_FILES) $(CLIENT_FILES) $(DATAPATH_ICONS)

EXTRA_DIST = \
	$(SRV_RE_FILES) \
	$(CLIENT_FILES) \
	freeciv-client.png \
	freeciv-ruledit.png \
	freeciv-modpack.png \
	freeciv-client.icns \
	freeciv-server.icns \
	gtk3.22_menus.xml

if CLIENT
CLIENTDATADIRS = amplio2 misc flags buildings cimpletoon isotrident trident\
 isophex hex2t hexemplio alio stdsounds stdmusic themes wonders
else
CLIENTDATADIRS =
endif

SRV_RE_DATADIRS = default classic civ1 civ2 sandbox multiplayer \
                  alien civ2civ3 granularity stub nation
if SERVER
SERVERDATADIRS = $(SRV_RE_DATADIRS) scenarios
else
if RULEDIT
SERVERDATADIRS = $(SRV_RE_DATADIRS)
else
SERVERDATADIRS =
endif
endif

ICONS = $(DATAPATH_ICONS)

if CLIENT
ICONS += freeciv-client.png
endif

icondir = $(prefix)/share/pixmaps
icon_DATA=$(ICONS)

SUBDIRS = icons $(CLIENTDATADIRS) $(SERVERDATADIRS) $(RULEDITDIRS) override
