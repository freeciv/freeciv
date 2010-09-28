================
BUGS c.q. FOUTEN
================

Freeciv 1.14.0 is een "stabiele" versie en wordt bug/foutvrij genoeg geacht
voor dagelijks gebruik. Indien u echter een bug ontdekt dan willen we dat echt
graag weten, zodat we dat kunnen verhelpen. Dit document is een opsomming van
ons bekende fouten in deze versie en geeft informatie over hoe u een nieuwe
bug kunt rapporteren.

De opsomming betreft uitsluitend de meest zichtbare fouten. Voor een complete
opsomming zie:

    http://gna.org/bugs/?group=freeciv

BEKENDE FOUTEN:
===============

 - In de Xaw client werkt de -t optie niet altijd. Gebruik in dat geval de
   --tiles optie.
   
 - Het tonen van eenheden in het veld onder de knop 'Beurt klaar' gaat niet
   altijd even foutloos. Een KI kan eenheden verplaatsen zonder dit veld bij
   te werken. U kunt dus 3 of 4 eenheden in dit veld zien, terwijl er maar één
   is. Zie onderwerp PR#2625 voor meer info.

 - Sommige regels met speciale tekens erin verschijnen blanco indien uw locale
   op "C" is ingesteld. Als een noodoplossing kunt u uw locale op iets anders
   zetten, zoals "en_US" of uiteraard "dutch" of "nl_NL" ;-)

 - Wijzigingen in de Burgemeester-instellingen worden pas naar de server
   gezonden als u op de 'Beurt klaar' knop klikt (of Enter geeft). Wijzigingen
   die u maakt in dezelfde beurt als die waarin u het spel opslaat komen als
   gevolg hiervan dus niet in het opgeslagen spel terecht.

 - Als u de burgemeester gebruikt is het resulterende opgeslagen spel niet
   'endian' en 64bit veilig. U kunt deze spelen dus niet overdragen naar een
   computer met een andere architectuur.

 - De 'easy' KI is niet gemakkelijk genoeg voor beginnende spelers. Als de KI
   u al in het beginstadium van het spel op uw donder geeft, probeer dan de
   'generator' serveroptie op 2 of 3 te zetten. Uiteraard voordat u het spel
   begonnen bent. Aan de serverprompt type:
       set generator 2
   of:
       set generator 3

 - De 'hard' KI is niet sterk genoeg voor zeer ervaren spelers en doet nog
   steeds enkele domme dingen. Hij verkiest het bijv. om steden in opstand te
   laten boven het laten uithongeren/krimpen.

 - Soms zijn er zoveel vooruitgangen in het 'doel' menu van het wetenschap-
   pelijk rapport, dat het menu voorbij de onderkant van het scherm komt en
   u niet alles meer kunt selecteren.

 - U kunt some het bericht krijgen
     {ss} speler voor fragment <01> niet gevonden
     {ss} speler voor fragment <01> niet gevonden
   wanneer u de esound driver gebruikt. Dat is niets om u zorgen over te
   maken.

 - Als u op Ctrl-C drukt in de client terwijl u de esd plugin gebruikt dan
   wordt het huidige geluid niet altijd correct onderbroken.

 - Sommige gevolgen van wonderen en onderzoek hebben pas de beurt erop
   effect. Bijv. wanneer u de vuurtoren gebouwd hebt dan zullen de triremen
   pas de beurt erop een bewegingspunt extra krijgen.

 - De XAW client kan slechts 25 burgers tonen in de stadsdialoog.

 - De auto-aanval werkt in het algemeen niet erg goed.

 - Wanneer u een goto/ga naar in de server plant, bijv. een auto-kolonist of
   een vliegtuig, dan kan de server informatie gebruiken waarover de speler
   nog niet kan beschikken.

 - De wetenschapsdialoog wordt niet bijgewerkt wanneer u een vooruitgang
   boekt. U dient hem te sluiten en opnieuw te openen.

 - In de GTK+ client is er soms rommel in het gebied naast de minikaart.

 - Automatische routines zoals auto-onderzoek kunnen niet zo goed overweg
   met triremen.

 - LOG_DEBUG werkt niet met niet-GCC compilers.

 - Bij het instellen van servervariabelen controleert de server de waarden
   niet altijd zo goed als zou moeten.

 - Erge dingen gebeuren wanneer u meerdere algemene werklijsten tegelijk
   bijwerkt.

 - Zelfs in enkelspel zal de KI elke beurt zowel voor als na de menselijke
   speler een kans om te bewegen krijgen. Dit kan soms de indruk wekken dat
   een KI tweemaal verplaatst (dat is dus echt niet zo).

 - De Xaw client werkt niet erg goed in combinatie met de KDE window manager.
   Probeer de GTK+ client te gebruiken of gebruik een andere window manager.

 - Alle opties die aan de client worden gegeven worden ook doorgegeven aan de
   UI. Dat houdt in dat indien er identieke opties zijn, deze een conflict
   kunnen veroorzaken. Dit is met name voor de Xaw client het geval bij het
   gebruik van de -d en soms de -t optie. Een noodoplossing is het gebruik
   van de lange optienaam (--debug en --tiles in dit geval). Zie PR#1752.

RAPPORTEREN VAN FOUTEN:
=======================

(Als het vertalingsfouten betreft in de Nederlandse versie, neem dan contact
op met Pieter J. Kersten <kersten@dia.eur.nl>. Voor vertalingsfouten in andere
talen, neem contact op met de primaire contactpersoon voor die taal. Kijk op 
<http://freeciv.org/wiki/Translations> voor de namen en mailadressen van deze
mensen.)

Dit is wat u moet doen:

 - Controleer of het niet één van de bovenstaande fouten is! :-)

 - Controleer het Freeciv website om er zeker van te zijn dat u de laatste
   versie van Freeciv hebt. (We zouden het al opgelost kunnen hebben.)

   In het bijzonder zou u een ontwikkelaars-versie kunnen proberen uit onze
   CVS database. Deze kunt u FTP'en van:

        http://download.gna.org/freeciv/latest/

 - Controleer de Freeciv FAQ (Frequent Asked Questions) op het Freeciv website
   om te kijken of er een noodoplossing (Engels: workaround) is voor uw fout.

 - Controleer het Freeciv foutenvolgsysteem op:

        http://gna.org/bugs/?group=freeciv

   om te kijken of de fout al eerder gerapporteerd is.

 - Verstuur een foutrapport!

   Of, als u de Freeciv ontwikkelaars enig commentaar wilt sturen maar niet
   direct een foutrapport in wilt vullen, kunt u email sturen naar
   <freeciv-dev@gna.org>, de Freeciv ontwikkelaars mailinglijst.

   Wat moet er in uw rapport staan (Let op: in het Engels!):

   - Beschrijf het probleem, inclusief eventuele berichten die getoond werden.

   - Geef aan welke client(s) u gebruikte (GTK+ of Xaw, ...)

   - Vertel ons de naam en de versie van:

       - Het besturingssysteem dat u gebruikt. U kunt in dit geval de 
         "uname -a" opdracht bruikbaar vinden.

       - Het versienummer van Freeciv

       - Als u de GTK+ client gebruikt, de versienummers (indien bekend) van
         uw GTK+, glib en imlib bibliotheken.

       - Als u de Xaw client gebruikt, de versienummers (indien bekend) van de
         X, Xpm en Xaw-bibliotheken en in het bijzonder of het standaard Xaw
         is of een variant als Xaw3d, Xaw95 of Nextaw.

       - Als u van sourcecode hebt gecompileerd, de naam en het versienummer
         van de compiler.

       - Als u installeerde van een binair pakket, de naam van het pakket, de
         distrubutie waarvoor het bestemd is en waar u het vandaan hebt.

   - Als Freeciv "core dumpt", dan kunnen wij u vragen om een debugger te
     gebruiken om ons van een "stack trace" te voorzien. U hebt daar dan het
     "core" bestand voor nodig, dus bewaar deze alstublieft nog even.


VERDERE INFORMATIE:
====================

Voor meer informatie, als altijd, kom naar het Freeciv website:

        http://www.freeciv.org/
