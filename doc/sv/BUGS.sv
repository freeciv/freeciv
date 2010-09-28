==========
PROGRAMFEL
==========

Freeciv 1.14.0 är en "driftssäker" utgåva och anses vara tillräckligt
fri från programfel för dagligt bruk. Om man ändå hittar ett
programfel skulle vi vilja få reda på det så att vi kan rätta
det. Denna fil listar kända programfel i denna utgåva och ger
information om att anmäla programfel.

Listan innehåller endast de tydligaste programfelen. För en
fullständig lista, se:

    http://gna.org/bugs/?group=freeciv

KÄNDA PROGRAMFEL:
=================

 - I XAW-klienten fungerar inte valmöjligheten -t för val av
   rutbildsuppsättningar. Använd --tiles i stället i detta fall.

 - Visningen av enheter i fältet under "Avsluta omgång"-knappen är
   lite felaktig. Datorstyrda spelare kan flytta enheter utan att
   fältet uppdateras. Så man kan till exempel se 3 - 4 enheter där när
   det skall vara endast 1. Se ärende PR#2625 för ytterligare
   upplysningar (på engelska).

 - Stadsförvaltningsinställningarna skickas endast till servern när
   man trycker på "Avsluta omgång". Ändringarna som man gör i
   stadsförvaltningsinställningarna samma omgång som man sparar går
   därför förlorade.

 - Om man använder stadsförvaltningen så blir de sparade spelen inte
   endian- och 64-bitarssäkra, så man kan inte använda filerna på
   datorer med annan arkitektur.

 - De enkla datorstyrda fienderna är inte tillräckligt enkla för
   nybörjare. Om de datorstyrda spelarna besegrar dig tidigt i spelet,
   prova att sätta sververvalmöjligheten "generator" till 2 eller 3,
   det vill säga skriv "set generator 2" eller "set generator 3" innan
   spelet har satts igång.

 - De svåra datorstyrda fienderna är inte tillräckligt svåra för
   erfarna spelare. De gör fortfarande dumma saker, till exempel så
   föredrar de att lämna städer i upplopp i stället för att svälta ner
   dem.

 - Ibland är det för många framsteg i "Mål"-menyn i
   forskningsredogörelsen så att menyn sträcker sig utanför skärmens
   underkant så att man inte kan välja alla framsteg. Detta berör
   endast GTK+-1.2-klienten.

 - Alla valmöjligheter som ges till klienten vidarebefordras även till
   användargränssnittet. Detta innebär att när valmöjligheter är lika
   så stör de varandra. Detta orsakar problem för valmöjligheten -d
   och i bland -t vid användande av XAW-klienten. Problemet kan
   undvikas genom att använda de långa versionerna i stället (--debug
   och --tiles i detta fall). Se ärende PR#1752 för ytterligare
   upplysningar (på engelska).

 - I bland kan man få meddelandena
   {ss} player for sample <01> not found
	 {ss} player for sample <01> not found
   när man använder ljuddrivrutinen esound. Detta är inget att oroa
   sig för.

 - Om man trycker Ctrl+C i klienten när man använder ljuddrivrutinen
   esound kan det hända att den ljudslinga som för närvarande spelas
   upp inte avbryts ordentligt.

 - Vissa världsunder börjar inte verka förrän omgången efter att de
   har färdigställts. När man till exempel färdigställer Fyrtornet får
   vissa triremer sitt extra drag först nästa omgång.

 - XAW-klienten kan endast visa 25 städer i stadsredogörelsen.

 - Självständigt angrepp fungerar i allmänhet inte bra.

 - När en förflyttningsväg planeras i servern, till exempel för en
   självständig bosättare eller ett flygplan, används information som
   inte är tillgänglig för spelaren.

 - Forskningsdialogen uppdateras inte när man lär sig en teknologi.
   Man måste stänga och öppna den.

 - I GTK+-klienten förekommer i bland skräp i området nära den lilla
   kartan.

 - Triremer hanteras inte bra i självständigt läge.

 - LOG_DEBUG fungerar inte med andra kompilatorer än GCC.

 - När man sätter servervariabler kontrollerar servern ofta inte
   värdena så bra som den skulle kunna.

 - Dåliga saker händer om man ändrar flera övergripande arbetslistor
   samtidigt.

 - Även i spel där datormotståndare är de enda motståndarna får de
   möjlighet att utföra drag både före och efter den mänskliga
   spelaren varje omgång. Detta ger i bland sken av att datorn gör 2
   drag.

 - XAW-klienten fungerar inte bra ben KDEs fönsterhanterare. Försök
   med GTK+-klienten eller en annan fönsterhanterare.

ANMÄLA PROGRAMFEL
=================

Om det är ett fel i en översättning ska det anmälas till översättaren
för språket i fråga.
Se <http://freeciv.org/wiki/Translations> för namn på och epostadresser
till övriga översättare.

Så här gör man:

- Ser efter att det inte är något av programfelen i listan ovan! :-)

- Tittar på <http://www.freeciv.org> och försäkrar sig om att man har
  den nyaste versionen. (Vi kanske redan har rättat felet.)

  Man kanske vill prova en utvecklarversion från CVS-upplaget. De kan
  hämtas från:

        http://download.gna.org/freeciv/latest/

- Tittar på Freecivs FAQ på <http://www.freeciv.org/faq> för att se om
  något sätt att upphäva problemet har offentliggjorts.

- Tittar på Freecivs system för spårning av programfel vid:

        http://gna.org/bugs/?group=freeciv

  för att se om programfelet redan har anmälts.

- Anmäler programfelet

   Om man vill skicka förslag till freecivutvecklarna utan att göra en
   programfelsanmälan kan man skicka epost till
   <freeciv-dev@gna.org>, freecivutvecklarnas sändlista.

   Vad man ska nämna i sin programfelsanmälan:

   - Beskrivning av problemet och i förekommande fall det felmeddeland
     man får.

   - Vilken klient man använder (Gtk+ eller Xaw).

   - Namn och versionsnummer:

       - Det operativsystem som man använder. Kommandot "uname -a" kan
         vara användbart.

       - Versionsnumret för Freeciv.

       - Om man använder Gtk+-klienten, versionsnumren (om man känner
         till dem) för sina Gtk+-, glib- och imlibbibliotek.

       - Om man använder Xaw-klienten, versionsnumren (om man känner
         till dem) för X-biblioteken, Xpm-biblioteket och
         Xaw-biblioteket och i synnerhet om det är en variant såsom
         Xaw3d, Xaw95 eller Nextaw.

       - Om man kompilerar från källkod, namnet och versionsnumret för
         kompilatorn.

       - Om man installerar från ett färdigkompilerat paket, dess
         namn, vilken distribution det är för och varifrån man hämtat
         det.

   - Om Freeciv "dumpar core", kan vi efterfråga en "stackspårning",
     vilken ges vid användning av en avlusare. För detta behövs
     "core"-filen, så var god behåll den ett tag.

YTTERLIGARE INFORMATION:
========================

För mer information se Freecivs plats på världsväven:

        http://www.freeciv.org/
