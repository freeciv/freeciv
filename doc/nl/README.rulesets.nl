----------------------------------------------------------------------
                       Freeciv Spelregels
                      (Rulesets in Engels)
----------------------------------------------------------------------
        (Oorspronkelijk door David Pfitzner, dwp@mso.anu.edu.au)
           (Vertaling: Pieter J. Kersten, kersten@dia.eur.nl)

Snelstart:
----------
 Spelregels staan wijzigbare verzamelingen gegevens voor eenheden,
 vooruitgangen, terrein, verbeteringen, wonderen, naties, steden,
 regeringsvormen en diverse andere spelregels toe zonder de noodzaak tot
 hercompileren op een wijze die consistent is over een netwerk en over
 opgeslagen spelen. (In de toekomst kunnen er andere categorieën spelregels
 komen.)

- Om Freeciv normaal te spelen: doe niets speciaals; de nieuwe voorzieningen
  hebben alle standaardwaarden die Freeciv het standaardgedrag geven.

- Om een spel te spelen met regels meer als Civ1, start de server met:
       ./ser -r data/civ1.serv
  (en met de andere argument die u verder normaliter gebruikt; afhankelijk van
  hoe Freeciv geïnstalleerd is kunt u het pad naar de map moeten opgeven in
  plaats van "data").

  Start de client normaal. De client moet netwerk-uitwisselbaar zijn (wat
  doorgaans inhoudt dat u dezelfde versie van client en server moet draaien)
  maar verder zijn er geen bijzondere eisen. (Hoewel er spelregels van derden
  kunnen zijn die gebruik maken van speciale afbeeldingen, in welk geval de
  client hierover de beschikking moet hebben en gestart moet zijn met de
  toepasselijke '--tiles' optie.)

  Zowel als een Civ1 stijl als hierboven heeft Freeciv nu ook een Civ2 stijl,
  hoewel op het ogenblik beide stijlen nagenoeg identiek zijn aan de standaard
  Freeciv regels.

  Merk op dat de Freeciv KI mogelijk niet zo goed overweg kan met andere 
  regels dan standaard Freeciv. (Zie opmerkingen verderop.)

De rest van dit document bevat:

- Meer gedetailleerde informatie over het maken en gebruiken van
  maatwerk/gemengde spelregels.

- Informatie over implementatie en notities voor verdere ontwikkeling.

----------------------------------------------------------------------
Gebruik en wijzigen van spelregels:
-----------------------------------

Spelregels worden opgegeven met nieuwe serveropties. De opdracht hierboven van
"./ser -r data/civ1.serv" leest slechts een bestand die deze opties instelt
(zowel als een paar van de standaard serveropties).
De nieuwe serveropties zijn:

        techs, governments, units, buildings,
        terrain, nations, cities, game.

Het zijn in zoverre speciale serveropties dat ze tekstwaarden accepteren, maar
verder gedragen ze zich als normale serveropties. Probeer bijv. de
serveropdracht "explain techs" maar eens.

Voor elk van deze opties specificeert de waarde van de optie een map onder de
Freeciv data-map met daarin bestanden met de naam techs.ruleset,
units.ruleset, buildings.ruleset, enz.

Bijv. de opdrachten:
   
	set techs default
	set governments default
	set units civ1
	set buildings custom
	set terrain civ2
	set nations default
	set cities default
	set game custom

zouden resp. de volgende bestanden specificeren:

	data/default/techs.ruleset
	data/default/governments.ruleset
	data/civ1/units.ruleset
	data/custom/buildings.ruleset
	data/civ2/terrain.ruleset
	data/default/nations.ruleset
	data/default/cities.ruleset
	data/custom/game.ruleset

(Dit is slechts een voorbeeld en kan niet erg slimme spelregels opleveren; de
map data/custom en het bestand data/custom/buildings.ruleset bestaan niet in
standaard Freeciv.)

De spelregelbestanden in de data map zijn gebruiker-wijzigbaar, dus u kunt ze
wijzigen om andere- en maatwerk spelregels te maken (zonder hiervoor Freeciv
te moeten hercompileren). Het wordt u aangeraden om _niet_ de bestanden in de
bestaande "default", "classic", "civ1" en "civ2" Freeciv-mappen te wijzigen,
maar om ze naar een andere map te kopieëren en ze daar te wijzigen. Dit is
omdat het dan duidelijk is dat u gewijzigde spelregels gebruikt in plaats van
de standaard-.

Het gebruikte formaat in de spelregelbestanden zou voor zichzelf moeten
spreken. Een aantal punten:

- De bestanden zijn niet allemaal onafhankelijk, aangezien bijv. eenheden
  afhankelijk zijn van vooruitgangen in het techs bestand.

- Wonderen en stadsverbeteringen hebben een nieuw veld, "variant". Dit staat
  beperkte wijzigingen toe in het effect van specifieke wonderen en
  stadsverbeteringen waar zulke wijzigingen geïmplementeerd zijn. Zie de
  "TEDOEN Variants" sectie verderop voor waar deze variant tot dusverre
  geïmplementeerd is.

- Eenheden hebben een nieuw veld, "roles" analoog aan "flags", maar die
  bepaalt welke eenheden in de diverse omstandigheden van het spel gebruikt
  worden (anders dan de intrinsieke eigenschappen van de eenheid). Zie ook het
  commentaar in common/unit.h

- De [units_adjust] sectie van het units bestand verdient enige uitleg. Het
  bevat de attributen:

	max_hitpoints, max_firepower, firepower_factor.

  De eerste twee attributen (tenzij nul) overschrijven de hitpoints en
  firepower attributen voor individuele eenheden als een gemaksoptie.
  De waarde voor firepower_factor wordt gebruikt wanneer een gevecht wordt
  berekend: alle gevechts firepower-waarden worden vermenigvuldigd met
  firepower_factor, wat inhoudt dat de effectieve hitpoints van enige eenheid
  in werkelijkheid gelijk is aan (hitpoints/firepower_factor).
  In feite is het deze effectieve waarde voor hitpoints die gerapporteerd
  wordt aan de client voor alle hitpoint waarden.
  (Deze gecompliceerde opzet is noodzakelijk zodat de KI-berekeningen niet de
  mist in gaan bij het gebruik van Civ1-spelregels.)

- De cities sectie in het nations spelregelbestand verdienen enige uitleg. In
  eerste oogopslag bevat het eenvoudigweg een met komma's gescheiden lijst van
  stadsnamen tussen aanhalingstekens. Echter, deze lijst kan ook worden
  gebruikt om steden te voorzien van hun voorkeurs terrein-type. Steden kunnen
  gemarkeerd worden als passend of niet-passend bij een specifiek terreintype,
  wat ze meer (of minder) waarschijnlijk maakt om te verschijnen als
  "standaard" naam. Het exacte formaat van de lijst in deze vorm is

	"<stadsnaam> (<label>, <label>, ...)"

  waar de stadsnaam gewoon de naam van de stad is (let op dat er geen
  aanhalingstekens of haakjes in voor mogen komen), en elk "label" markeert
  (niet hoofdlettergevoelig) een terreintype voor de stad (of "river"), met
  een !, - of ~ er aan voorafgaand in het geval van een ontkenning. De
  terrein-lijst is optioneel uiteraard, dus de regel kan gewoon de stadsnaam
  bevatten indien gewenst. Een stad die gemarkeerd is voor een bepaald
  terreintype zal passend worden geacht voor een kaartcoördinaat indien de
  omringende vlakken of het vlak zelf het terreintype bevat als gemarkeerd.
  In het geval van het "river" label (wat een speciaal geval is) wordt alleen
  het vlak op de coördinaat zélf in ogenschouw genomen. Een complex voorbeeld:

    "Wilmington (ocean, river, swamp, forest, !hills, !mountains, !desert)"

  zal ervoor zorgen dat de stad Wilmington past bij oceaan, rivier, moeras en
  bosvlakken terwijl heuvels, bergen en woestijnen niet passen. Hoewel deze
  mate van detaillering waarschijnlijk onnodig is om het beoogde effect te
  bereiken, is het systeem ontworpen om vloeiend te degraderen, dus het zou
  prima moeten werken.

  (Een opmerking over schaal: het kan verleidelijk zijn om Londen als !oceaan
  te markeren, d.w.z. niet aan de kust. Echter op een Freeciv-wereld van enig
  formaat zal Londen aan de kust horen te liggen. Het markeren met !ocean
  leidt doorgaans tot slechte resultaten. Dit is een beperking van het systeem
  en zou meegenomen moeten worden bij het labelen van steden.)

  Merk ook op dat steden eerder in de lijst een grotere kans hebben om gekozen
  te worden dan latere steden.

Eigenschappen van eenheden en vooruitgangen zijn nu behoorlijk goed
gegeneraliseerd.
Eigenschappen van gebouwen en stadsverbeteringen zijn nog steeds behoorlijk
beperkt.

OPMERKING: Er is veel in de spelregels dat nog niet volledig is
geïmplementeerd. Verzeker u ervan dat u Beperkingen en Limieten leest
verderop.

----------------------------------------------------------------------
De civstyle optie:
------------------

De server optie 'civstyle' bestaat nog steeds, maar wordt op het ogenblik
alleen gebruikt om te bepalen hoeveel van de kaart zichtbaar wordt als het
Apollo-programma is gebouwd.

De civstyle optie zal in de nabije toekomst worden verwijderd.

----------------------------------------------------------------------
De KI:
------

Een belangrijk addertje bij de spelregels is dat waar spelregels behoorlijke
flexibiliteit toestaan, de KI ontworpen is om met de standaard Freeciv regels
(eigenlijk Civ2) om te gaan. Dus de KI zou het wel eens niet zo goed meer
kunnen doen met gewijzigde spelregels.

Er zijn enkele wijzigingen aan de KI aangebracht om enige wijzigingen in
spelregels op te kunnen vangen en de gevechtsberekeningen zijn in zoverre
aangepast dat ze geen core dumps meer geven, maar in het algemeen moet u niet
verwachten dat de KI net zo goed werkt met niet-standaard opties (waarbij Civ1
zeer nadrukkelijk wordt meegenomen in die niet-standaard).

Voorbeelden van problemen die nog niet verholpen zijn:
- Eenheden zonder hitpoints in Civ1 kunnen een groot verschil uitmaken in
  toepasselijke gevechtsstrategieën.
- Stadswallen zijn voldoende verschillend (verhoogde bouw- en onderhoudskosten
  en effect aan de kust) om afwijkende omgang te rechtvaardigen?
- Zonder de Haven en Booreiland verbeteringen zijn steden met veel
  oceaan-vlakken in Civ1 veel meer beperkt.
- Wonderen die slechts effect hebben op een enkel werelddeel krijgen geen
  speciale behandeling van de KI.

Hopelijk verbeterd deze situatie in de toekomst, maar hoe flexibeler de
regels, hoe moeilijker het wordt om een goede KI te schrijven...

----------------------------------------------------------------------
Beperkingen en Limieten:
------------------------

units.ruleset:

  Ongebruikte attributen:

    - uk_gold

  Beperkingen:

    - Tenminste één eenheid met de rol "FirstBuild" moet beschikbaar zijn in
      het begin (dwz., tech_req = "None").

    - Er móeten eenheden zijn voor deze rollen:
      - "Explorer"
      - "FerryBoat"
      - "Hut"
      - "Barbarian"
      - "BarbarianLeader"
      - "BarbarianBuild"
      - "BarbarianBoat"  (move_type moet "Sea" zijn)
      - "BarbarianSea"

    - Er moet tenminste één eenheid zijn met vlag "Cities".

  Limieten:

    - Deze eenheid vlagcombinaties zullen niet werken:
      - "Diplomat" en "Caravan"

buildings.ruleset:

  Ongebruikte attributen:

    - terr_gate
    - spec_gate
    - sabotage
    - effect

nations.ruleset:

  Ongebruikte attributen:

    - attack
    - expand
    - civilized
    - advisors
    - tech_goals
    - wonder
    - government

----------------------------------------------------------------------
Implementatiedetails:
---------------------

Deze sectie en de volgende zijn voornamelijk van belang voor ontwikkelaars die
bekend zijn met de sourcecode van Freeciv.

Spelregels zijn voornamelijk in de server geïmplementeers. De server leest de
bestanden en zend dan de informatie naar de clients. Meestal worden spelregels
gebruikt om de basis data-tabellen voor eenheden e.d. te vullen, maar in
sommige gevallen is enige extra informatie noodzakelijk.

Voor eenheden en vooruitgangen wordt alle informatie betreffende elke eenheid
of vooruitgang gevangen in datatabellen en deze zijn nu "volledig aan te
passen", met de oude enumeraties volledig verwijderd. Voor verbeteringen en
wonderen heeft elk een grotendeels uniek effect, zodat deze effecten nog
grotendeels hard gecodeerd zijn. Het "variant" veld staat nu enige
flexibiliteit toe, maar de effecten zelf moeten nog steeds hard gecodeerd
worden. Er zijn enkele plannen om deze situatie te verbeteren.

----------------------------------------------------------------------
TEDOEN:
-------

- Meer verbeteringen- en wondervarianten voor Civ1 (zie volgende sectie).
- Andere alternatieve (non-Civ1) varianten? (Indien gewenst.)
- Beter: schrap varianten volledig en doe iets meer algemeen...

- Verbeter AI dynamische regeringsvorm-keuze code.
- Werk aan het beter laten omgaan van de KI met niet-standaard gevallen.
- Zou andere informatie beschikbaar kunnen zijn om de KI te helpen?
  Bijv. scores voor eenheden/vooruitgang/gebouwen om de KI beter te laten
  kiezen welke beter is?

- Spelregels voor: tijdsverloop, ...?

----------------------------------------------------------------------
TEDOEN Variants: 
--------------

Eerst de varianten die geïmplementeerd zijn:
(Let op dat Variant=0 altijd "standard Freeciv" effect zou moeten zijn.)

Barrakken:       Variant=0: alleen voor grondeenheden (Civ2)
		 Variant=1: werkt ook voor lucht- en zee-eenheden (Civ1)
Barrakken II, Barrakken III, Sun Tzu:
		 Als Barakken.
Stadswallen:     Variant=0: alleen van toepassing op land- en heli-eenheden
		 Variant=1: ook voor zee-eenheden
Politiebureau:   Als Vrouwen Kiesrecht (zie onder)
Pyramiden:	 Variant=0: telt als Graansilo in elke stad (Civ2)
		 Variant=1: staat alle regeringsvormen toe en er is geen
                            overgangs-anarchie (Civ1)
Verenigde Nat.:  Variant=0: eenheden herstellen extra hp per beurt (Freeciv)
		 Variant=1: staat alle regeringsvormen toe en er is geen
                            overgangs-anarchie (Civ1) (zie opmerking(*))
Hoover Dam:	 Variant=0: werkt voor alle steden in eigendom
		 Variant=1: werkt voor steden op hetzelfde continent als 
			    waar het wonder werd gebouwd.
J.S. Bach:	 Variant=0: werkt op alle steden in eigendom
		 Variant=1: werkt voor steden op hetzelfde continent als 
			    waar het wonder werd gebouwd.
Vrouwen Kiesr.:  Variant=0: -1 eenheid-ontevreden per Stad (-2 onder 
			    Democratie) (Freeciv)
	         Variant=1: -1 eenheid-ontevreden per Eenheid (Civ1)
Magellan's Exp.: Variant=0: geeft zee-eenheden 2 extra bewegingspunten
		 Variant=1: geeft zee-eenheden slechts 1 extra bewegingspunt
Grote Muur:	 Gebruikt zelfde variant en effect als Stadswallen.
Leo's Werkpl.:   Variant=0: Waardeert één verouderde eenheid per beurt op.
		 Variant=1: Waardeert alle verouderde eenheden per beurt op.
Michelangelo's:  Variant=0: zelfde als Kathedraal in elke stad
		 Variant=1: Verdubbelt effect van kathedralen

Andere verschillen tussen Civ1 en Civ2/Freeciv die nog niet als variant
geïmplementeerd zijn:

Copernicus' Obs.: Freeciv: wetenschapswaarde +50% in een stad
		  Civ1:    verdubbelt wetenschapswaarde in stad, na alle
                           andere effecten.
Isaac Newton's:   Freeciv: wetenschapswaarde +100% in een stad
		  Civ1:    "verhoogt effect van Bibliotheken en 
			   Universiteiten" - met hoeveel??

Er zijn andere meer kleine verschillen tussen Civ1/Civ2/Freeciv, die
afgehandeld zouden kunnen worden met varianten, voor:
    Gerechtshof
    Genezing voor Kanker
    Hangende Tuinen
    Vuurtoren

Notities:

Aquaduct en Riolering:
   De maximale grootte van steden zonder deze verbeteringen wordt apart
behandeld zonder gebruik van varianten. De laat varianten beschikbaar als
andere effecten gewenst zijn.

Colosseum, Kathedraal, Tempel:
   Merk op dat de technologische effecten op de effectiviteit van deze
gebouwen niet afgehandeld wordt met varianten maar met speciale attributen van
de vooruitgangen; zie game.rtech velden cathedral_plus, cathedral_minus,
colosseum_plus en temple_plus. (Ook van toepassing voor Michelangelo's.)

(*) Verenigde Naties:
    Ik weet niet zeker wat ik hier moet doen: het diplomatieke effect van
Civ1, Civ2 is niet gepast voor multispeler Freeciv, en het hp effect van
Freeciv is niet gepast voor Civ1 (geen hitpoints). Het govchange effect lijkt
redelijk -- er is geen Vrijheidsbeeld in Civ1 en Pyramidenm die hetzelfde
effect hebben in Civ1, raken verouderd. Alternatief een Kiesrecht-achtig
effect lijkt verstandig: uw bevolking is meer bereid om tot oorlog over te
gaan als het het fiat van de Verenigde Naties heeft!

----------------------------------------------------------------------
