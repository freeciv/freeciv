==============
DINGEN TE DOEN
==============

Freeciv is behoorlijk volgroeid en we denken dat het een schitterend spel is,
maar er zijn altijd dingen om toe te voegen of te verbeteren. Dit document
somt enige nieuwe voorzieningen op waaraan wordt gewerkt of die gepland zijn
of gewoon gewenst. (Zie ook het bestand BUGS.nl voor enkele bekende
zwakheden).

TEDOEN:
=======

- Server-zijde spelers scripttaal

- Diplomatie:
  * Senaat
  * Een KI die met diplomatie overweg kan

- Betere afbeeldingen voor stadsstijlen.

- Betere internationalisatie (verhelp enige problemen, wijzig die Engelse
  zinnen die niet goed vertaald kunnen worden in de meeste andere talen,
  betere GUI afhandeling voor woorden van verschillende lengte, gemegde taal
  client/server, meer complete vertalingen, meer vertalingen).

- Maak de spelregels voor Stadsverbeteringen en Wonderen flexibeler.

- Maak de code gezichtspunt-agnostisch: verwijder alle aannames over welke
  vlakken geldig zijn. Bijv. de aanname dat de kaart doordraait in de
  x-richting. Dit kan gedaan worden door de code gebruik te laten maken van
  macro's als whole_map_iterate en square_iterate. De functies map_adjust_x()
  en map_adjust_y() zouden verwijderd moeten worden en code die er gebruik van
  maakt moet gebruik maken van normalize_map_pos(), is_real_map_pos() of een
  macro.
  Het zou ook fijn zijn als de aanname dat de x-waarden lopen van
  [0..map.xsize] verwijderd kon worden, aangezien een isometrisch
  nummeringsschema die eigenschap niet heeft.
  Wanneer alle code geconverteerd is, kan de hele freeciv server aangepast
  worden voor het gebruik van een vlakke kaart, een isometrische kaart, een
  cilindrische kaart or misschien zelfs een hexagonale kaart, allemaal door
  slechts een kleine hoeveelheid code op de juiste plaatsen.
  Merk op dat de KI op diverse plaatsen de vlakken niet goed aanpast, zelfs
  niet in het huidige nummeringsschema.
  Merk op dat de conversie terugwaards-uitwisselbaar is, zodat hij in kleine
  stappen gerealiseerd kan worden. Geen mega-patches graag.

- Laat de server stoppen met het versturen van tekst naar de clients. In
  plaats daarvan zou hij een enumeratie moeten sturen en enige waarden, die de
  client omzet in een bericht om aan de gebruiker te tonen. Dit zal de client
  meer informatie verschaffen over het bericht, noodzakelijk voor bijv. een
  KI aan de kant van de client. Het zal ook inhouden dat de berichten
  automatisch in de juiste locale vertaald zullen worden.
  Merk op dat deze nieuwe berichten ook soms stadsnamen zullen moeten
  bevatten.
  De mogelijkheid van het versturen van tekstberichten zou wel moeten blijven
  bestaan, om uitwisselingsproblemen te voorkomen wanneer op deze server wordt
  overgegaan.

- Maak een nieuwe "ocean" variabele in de tile struct, analoog aan de
  "continent" variabele. Hij heeft ondersteunende functies nodig om bijgewerkt
  te blijven in zowel client als server. Hij zou ook een geassocieerde functie
  is_oceans_connected(oceaan1, oceaan2, speler) moeten hebben die teruggeeft
  of er een stad is die de twee oceanen voor deze speler verbindt.

- Documenteren/opschonen KI code. Hernoem variabelen naar meer sprekende name
  dan deze:
  int a, c, d, e, i, a0, b0, f, g, fprime;
  int j, k, l, m, q;
  (uit ai/advmilitary.c:process_attacker_want())
  I zou in het bijzonder graag het gebruik van de amortize() functie in
  servers/settlers.c gedocumenteerd zien.
  Ik zal patches accepteren zelfs als ze slechts een enkele variabele
  hernoemen

- Implementeer goto (ga naar) in common map.

VERDERE INFORMATIE:
===================

Informatie over andere projecten en de Freeciv ontwikkelings "wegenkaart" zijn
beschikbaar op het Freeciv website:

        http://www.freeciv.org/
