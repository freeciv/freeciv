===========================================================================
 Ljudstöd
===========================================================================

Servern skickar en lista över förstahands- och andrahandsljudmärken
till klienten för vissa händelser. Förstahandslmärkena är de som
ändringsuppsättningen i fråga föredrar. Klienten behöver inte ha dessa
ljud. Andrahandsmärkena ska hänvisa till standardljud som alla
installationer av Freeciv ska ha.

Märkena änvänds för att ge ett enkelt sätt att byta ljud. En
beskrivningsfil änvänds för att ange ljudfiler till märkena. Ett byte
av beskrivningsfil ändrar således ljuden. Det görs med
kommandoradsargument enligt följande:

  civclient --Sound minaljud.spec

Då änvänds de ljudfiler som anges i filen "minaljud.spec". Man måste
se till att ljudfilerna finns där det står att de ska finnas genom att
kopiera eller länka dem dit, eller genom att ändra i
beskrivningsfilen. Alla sökvägar är som standard relativa till
katalogen "data/". Ljuduppsättningar kan laddas ner i tar-format från
Freecivs webserver. Man packar upp dem med kommandot
"tar zxvf stdsoundsX.tar.gz" (eller med WinZip för Windows) och lägger
filerna i ovannämnda datakatalog.

CVS-versionen innehåller varken ljudfiler eller beskrivningsfil. Man
kan hämta ljuduppsättningar (ljudfiler med beskrivningsfil) från
<ftp://ftp.freeciv.org/freeciv/contrib/sounds/>. På denna adress kan
man även hitta extra ljudfiler för att ändra en befintlig
ljuduppsättning eller skapa en ny.

================================
 Insticksprogram
================================

Ljudutmatningen i klienten görs av insticksprogram. Vilka
insticksprogram som är tillgängliga avgörs av de bibliotek som hittas
på värdsystemet. Man kan välja vilket insticksprogram som ska användas
med kommandoradsargument:

  civclient --Plugin sdl

Man kan välja "none" för att tysta klienten. Freeciv stödjer för
närvarande följande insticksprogram:
  - dummy (none)
  - Esound (esd)
  - SDL with SDL_mixer library (sdl)

För att lägga till stöd för ett nytt insticksprogram ändrar man dessa
filer (där "något" är namnet på det nya insticksprogrammet):
	configure.in			/* lägg till nytt prov */
	acconfig.h			/* lägg till ny konfigurationsmetavariabel */
	client/audio.c			/* länka in nytt insticksprogram */
	client/Makefile.am		/* lägg till filerna nedan */
	client/audio_whatever.c		/* insticksprogram */
	client/audio_whatever.h		/* insticksprograms huvud */

================================
 Märken
================================

Det fins 2 slags ljudmärken:
 - angivna i regeluppsättningarna
 - angivna i programkoden

Emedan det första kan väljas fritt, kan det andra inte ändras.

Ljudmärkena som hör till världsunder och stadsförbättringar,
enhetsförflyttningar och strid måste anges i regeluppsättningarna.
Freeciv skickar över dessa ljudmärken till klienten, där de översätts
till filnamn med hjälp av beskrivningsfilen. Varje beskrivningsfil ska
ha allmänna ljudmärken för världsunder ("w_generic"),
stadsförbättringar ("b_generic"), enhetsförflyttningar ("m_generic")
och strid ("f_generic").

Ljudmärken som hör till vissa händelser kommer från freecivkoden och
kan inte ändras utifrån. Beskrivningsfilen tillordnar även ljudfiler
för dessa märken. Märkenas namn är uppräkningsnamn (se
common/events.h) bestående av gemena bokstäver. Således blir
"E_POLLUTION" märket "e_pollution". Det finns ingen allmänt märke och
ej heller några andrahandsmärken.

Det finns för närvarande endast ett musikstycke; inledningsstycket.
Det spelas tills spelet börjar. Märket för detta musikstycke är
"music_start".

================================
 ATT GÖRA
================================

Det finns några saker som kan göras för att förbättra Freecivs
ljudstöd:
  * lägg till fler insticksprogram (gstreamer, arts, windows, ...)
  * lägg till ett ljudmärke för varje forskningsframsteg
  * lägg till stöd för ogg-filer
  * lägg till fler händelsemärken
  * hitta eller skapa bättre ljud och förbättra beskrivningsfilen

================================
 Övrigt
================================

Ljudskapare; vänligen namnge ljudfilerna på ett intelligent sätt.
Bifoga en informationsfil med namnet README. Den måste innehålla
licensvillkoren. Om ljuden är fria från upphovsrättsanspråk ska det
framgå.

Skapare av ändringsuppsättnigar; vänligen gör andrahandsmärken som
hänvisar till standardmärken så att de som inte har laddat ner den
senaste och bästa ljuduppsättningen ändå kan njuta av spelet.
