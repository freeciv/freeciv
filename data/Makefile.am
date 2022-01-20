## Process this file with automake to produce Makefile.in

if CLIENT
CLIENT_FILES =				\
	amplio2.tilespec		\
	cimpletoon.tilespec		\
	hex2t.tilespec			\
	hexemplio.tilespec		\
	isophex.tilespec		\
	isotrident.tilespec		\
	toonhex.tilespec		\
	alio.tilespec			\
	trident.tilespec		\
	stdmusic.musicspec		\
	stdsounds.soundspec		\
	helpdata.txt
if CLIENT_GUI_GTK_3_22
CLIENT_FILES += \
	gtk3.22_menus.xml
endif
if CLIENT_GUI_GTK_4
	gtk4_menus.xml
endif
else
CLIENT_FILES =
endif

SRV_RE_FILES = \
	civ1.modpack \
	civ1.serv \
	civ2.modpack \
	civ2.serv \
	classic.modpack \
	classic.serv \
	default.serv \
	multiplayer.modpack \
	multiplayer.serv \
	alien.modpack \
	alien.serv \
	civ2civ3.modpack \
	civ2civ3.serv \
	sandbox.serv

if SERVER
SERVER_FILES = $(SRV_RE_FILES)
else
if RULEDIT
SERVER_FILES = $(SRV_RE_FILES)
else
SERVER_FILES =
endif
endif

pkgdata_DATA = $(SERVER_FILES) $(CLIENT_FILES)

EXTRA_DIST = \
	$(SRV_RE_FILES) \
	freeciv-client.png \
	gtk3.22_menus.xml	\
	gtk4_menus.xml	\
	amplio2.tilespec \
	cimpletoon.tilespec \
	hex2t.tilespec	\
	hexemplio.tilespec	\
	isophex.tilespec	\
	isotrident.tilespec \
	toonhex.tilespec	\
	alio.tilespec	\
	trident.tilespec	\
	stdmusic.musicspec	\
	stdsounds.soundspec	\
	helpdata.txt

if CLIENT
CLIENTDATADIRS = amplio2 misc flags buildings cimpletoon isotrident trident\
 isophex hex2t hexemplio alio stdsounds stdmusic themes wonders
else
CLIENTDATADIRS = 
endif

SRV_RE_DATADIRS = default classic civ1 civ2 sandbox multiplayer \
                  alien civ2civ3 stub nation
if SERVER
SERVERDATADIRS = $(SRV_RE_DATADIRS) scenarios
else
if RULEDIT
SERVERDATADIRS = $(SRV_RE_DATADIRS)
else
SERVERDATADIRS =
endif
endif

if RULEDIT
RULEDITDIRS = ruledit
else
RULEDITDIRS =
endif

if CLIENT
ICONS = freeciv-client.png
else
ICONS =
endif

icondir = $(prefix)/share/pixmaps
icon_DATA=$(ICONS)

SUBDIRS = icons $(CLIENTDATADIRS) $(SERVERDATADIRS) $(RULEDITDIRS) override
