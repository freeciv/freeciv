==========
PROGRAMFEL
==========

Freeciv 1.11.4 är en "driftssäker" utgåva och anses var tillräckligt
fri från programfel för dagligt bruk. Om man ändå hittar ett
programfel skulle vi vilja få reda på det så att vi kan rätta
det. Denna fil listar kända programfel i denna utgåva och ger
information om att anmäla programfel.

Listan innehåller endast de tydligaste programfelen. För en
fullständig lista, se:

    http://www.freeciv.org/cgi-bin/bugs

KÄNDA PROGRAMFEL:
=================

 - Gtk+-klienten har ibland ett problem med at fokuset fastnar på
   tjattraden. Då fungerar pilknapparna inte längre för att flytta
   enheter i huvudanblicken. Ett sätt att komma tillbaka till
   huvudanblicken är att använda <Tab>-knappen för att flytta fram
   fokuset till någon annan skärmbeståndsdel. Då fungerar pilknapparna
   igen. (Att klicka i huvudanblicken är också ett sätt att upphäva
   problemet.)

 - De enkla datorstyrda fienderna är inte tillräckligt enkla för
   nybörjare. Om de datorstyrda spelarna besegrar dig tidigt i spelet,
   prova att sätta sververvalmöjligheten "generator" till 2 eller 3,
   det vill säga skriv "set generator 2" eller "set generator 3" innan
   spelet har satts igång.

 - De svåra datorstyrda fienderna är inte tillräckligt svåra för
   erfarna spelare. De gör fortfarande dumma saker, till exempel så
   föredrar de att lämna städer i upplopp i stället för att svälta ner
   dem.

 - Ibland är det för många framsteg i "mål"-menyn i
   forskningsredogörelsen så att menyn sträcker sig utanför skärmens
   underkant så att man inte kan välja alla framsteg. 

 - Servern visar inte hela namnet på värdar med mycket långa namn.

 - Världsundret "Stora Muren", vilket verkar som en stadsmur i varje
   stad, förhindrar byggandet av riktiga stadsmurar som inte
   försvinner när världsundret blir föråldrat.

ANMÄLA PROGRAMFEL
=================

(Om det är ett fel i en översättning ska det anmälas till översättaren
för språket i fråga. Se <http://www.freeciv.org/l10n.phtml> för namn
på och epostadresser till översättarna.)

Så här gör man:

- Ser efter att det inte är något av programfelen i listan ovan! :-)

- Tittar på <http://www.freeciv.org> och försäkrar sig om att man har
  den nyaste versionen. (Vi kanske redan har rättat felet.)

  Man kanske vill prova en utvecklarversion från CVS-upplaget. De kan
  hämtas från:

        http://www.freeciv.org/latest.html

- Tittar på Freecivs FAQ på <http://www.freeciv.org/faq> för att se om
  något sätt att upphäva problemet har offentliggjorts.

- Tittar på Freecivs system för spårning av programfel vid:

        http://www.freeciv.org/cgi-bin/bugs

  för att se om programfelet redan har anmälts.

- Anmäl programfelet!

   Man kan änmäla ett programfel genom att skicka epost till
    <bugs@freeciv.freeciv.org> eller på väven vid
    <http://www.freeciv.org/cgi-bin/bugs>.

   Om man vill skicka förslag till freecivutvecklarna utan att göra en
   programfelsanmälan kan man skicka epost till
   <freeciv-dev@freeciv.org>, freecivutvecklarnas sändlista.

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
