
  Burgemeester (Burg)
=======================

De Burg is een betrekkelijk nieuw gereedschap in de GTK+ client van FreeCiv
versie 1.12.1. Hij is ontworpen om u te helpen uw steden te besturen, d.w.z.
hij zet de stedelingen zodanig aan het werk op de beschikbare vlakken rondom
(of maakt ze wetenschapper, belastingsbeambte of zelfs artiest), dat het
maximaal haalbare uit de stad wordt gehaald. U kunt de burgemeester aan of uit
zetten naar behoefte voor willekeurig welke stad, maar er zijn enkele
handicaps (verderop uitgelegd) als u bestuurde en niet-bestuurde steden naast
elkaar heeft. In elk geval dient u een server versie 1.12.1 of hoger te
hebben; de burgemeester draait niet op 1.12.0 of lager.

Het hart van de burgemeester is een optimalisatiealgoritme dat probeert de
werkers van een stad zodanig in te zetten dat een door de gebruiker gesteld
doel zoveel mogelijk gehaald wordt. Zoals u waarschijnlijk weet is er al een
soort optimalisatiealgoritme in FreeCiv beschikbaar als u de stadsdialoog
opent en u klikt op het stads-plaatje in de mini-kaart. Deze optimalisatie
probeert voornamelijk de wetenschap te optimaliseren zonder daarbij rekening
te houden met onlusten e.d.

De nieuwe burgemeester gaat ver voorbij deze oude vorm van optimalisatie. Ten
eerste voert het deze taak telkens uit wanneer de stad verandert. Als de stad
groeit of inkrimpt, troepen in- en uitgaan, vlakken irrigatie of mijnen
krijgen of ingenomen worden door vijanden wordt de burgemeester aktief. Ten
tweede ondersteunt hij alle vormen van optimalisatie zoals produktie
(schilden), goud, wetenschap of showbizz. Ten derde geeft hij de speler een
vergaande beheersing over dit, met de mogelijkheid om beperkingen te leggen op
willekerig welke vorm van stadsproduktie. Dit laatste is inclusief de
beperking van vieringen, wat het makkelijk maakt om uw steden te laten
groeien, ook in slechtere tijden. Ten vierde en waarschijnlijk meest
waardevolle in oorlogstijden, is dat hij uw steden tevreden houdt, daarmee
onlusten voorkomend.


  Gebruik
===========

U kunt de burgemeester instellen voor een stad door de stadsdialoog te openen
en op het Burgemeester-tab te klikken. Aan de linkerkant kunt u een snelkeuze
selecteren voor een specifiek doel, aan de rechterkant kunt u meer complexe
doelen stellen door de schuifknoppen te verplaatsen. U kunt eerst een snelkeuze
kiezen en daarna wijzigen. Zodra u een instelling heeft, kunt u er een
snelkeuzenaam aan toekennen. Dit is niet noodzakelijk, maar heel bruikbaar,
aangezien u de instellingen kunt bekijken en zelfs veranderen in de
stadsdialoog wanneer u er een naam aan gegeven heeft. Vergeet niet om de
instellingen op te slaan (in het Spel-menu) wanneer u nieuwe snelkeuzen
vervaardigd heeft.

De schuifknoppen hebben twee 'smaken': de rechter knoppen zijn factoren, die
aangeven hoeveel een produkt waard is in vergelijking met de andere- (bijv.
hoeveel produktie waard is in vergelijking met de rest). De linker knoppen
zijn beperkingen: u kunt de stad opdracht geven om geen voeding te verliezen,
bijv. door de overcapaciteit-beperking op nul te zetten; en u kunt de stad
toestaan om goud te verliezen door de overcapaciteits-beperking op goud bijv.
op -3 te zetten en de stad aan te sporen om tenminste 5 produktie per beurt te
maken door de produktie overcapaciteits-beperking op 5 te zetten. De
krachtigste beperking is echter de Viering-beperking, die uw stad onmiddelijk
een festival laat organiseren (wat meestal effect heeft in de eerst volgende 
beurt na de verandering).

Het is duidelijk dat de burgemeester niet aan alle beperkingen in elke
situatie kan voldoen. Wanneer de beperkingen niet gevolgd kunnen worden
vertrekt de burgemeester uit die stad met de melding 'De burgemeester kan niet
voldoen aan de beperkingen voor Berlijn. Besturing wordt teruggegeven.' U
heeft dan de keuze om de stad zelf te besturen of om de stadsdialoog te openen
en de beperkingen te wijzigen zodanig dat ze wel gevolgd kunnen worden.

Wanneer u een opzet voor een stad heeft gemaakt, dient u op de knop 'Bestuur
stad' te klikken om de burgemeester 'aan' te zetten. Als deze knop grijs
is, dan is of de burgemeester al aktief, of de taak is onuitvoerbaar. In het
laatste geval ziet u in het resultaatgedeelte streepjes in plaats van
getallen. Als u ooit de burgemeester opzettelijk uit wilt schakelen, klik dan
op 'Verlos stad'.


  Geavanceerd gebruik
=======================

Er nog niet veel ervaring met de burgemeester, maar sommige kanttekeningen 
kunnen helpen. De doelen voor uw stad hangen meestal af van de spelfase; of u
zich snel wilt verspreiden, snel wilt groeien, geavanceerde wetenschap wilt
onderzoeken of oorlog wilt voeren. U zou een hoge factor voor wetenschap
willen instellen of een hoge produktiefactor om eenheden te produceren. De
hoogst beschikbare factor is 25, wat wil zeggen: als de produktiefactor op 25
staat en de rest op 1, dat de burgemeester 1 enkele produktie-eenheid even
belangrijk vindt als 25 goud (of handel, of ...). Dat is nogal wat, aangezien
je met goud ook eenheden kunt kopen. Het houdt ook in dat het de burgemeester
niet uitmaakt of het goud of wetenschap of luxe of voeding moet produceren;
maar als u oorlog voert wilt u meestal liever goud of luxe. Het is dus
waarschijnlijk een goed idee om een tweede (of zelfs derde) doel voor de
stadsproduktie in te stellen, bijv. een goudfactor van 5. Dat levert nog
steeds een voorkeur op van 1 produktie tegen 5 goud (en 1 goud over 5 voeding
of iets anders).

Beperkingen zijn niet altijd even handig. Als u een hoog inkomen wilt, is het
waarschijnlijk beter om de goudfactor op 25 te zetten dan op een minimale
overproduktie van 5 of zoiets. Omdat een grote stad meer goud kan maken dan
een kleine-, eindigt u met een afwijkende instelling voor elke stad.

Echter, als de produktie-overcapaciteit van een stad onder nul komt, kan het
niet alle eenheden meer ondersteunen. U zult enkele van de door de stad
ondersteunde eenheden verliezen. Als de voedings-overcapaciteit negatief is,
zal de stad verhongeren en uiteindelijk (wanneer de graansilo leeg is)
inkrimpen. Dat kan de bedoeling zijn, maar als de stad kolonisten ondersteund,
zult u deze verliezen lang voordat de stad inkrimpt. Beperkingen kunnen ook
een waarschuwings-effect hebben.

Welke beperkingen vervuld kunnen worden hangt grotendeels af van de algemene
instellingen van wetenschap, belasting en luxe af. Een goud-overcapaciteit >=0
is bijvoorbeeld gemakkelijker te vervullen met een hogere belastingswaarde dan
met een lagere-. U zult altijd moeten overwegen deze instellingen te
veranderen wanneer u de burgemeester-instellingen voor het merendeel van uw 
steden gaat veranderen.

Aanwijzing: Om te voorkomen dat de burgemeester per ongeluk vertrekt uit
steden wanneer u de waarden wijzigt is het het beste om deze instellingen te
veranderen in de belastings-dialoog in plaats van op het hoofd-scherm.


  Nadelen
===========
De burgemeester is een krachtig hulpmiddel dat u niet alleen verlost van het
micro-bestuur van uw steden, maar u ook betere opbrengsten levert dan ooit
tevoren (voor de meeste spelers dan).

Er zijn echter wat nadelen. Als u eenmaal de burgemeester aan hebt gezet, pakt
hij ieder goed vlak dat hij kan vinden. U gaat dus zeer moeilijke tijden
tegemoet als u probeert een nabijgelegen stad handmatig te besturen. Dit is
waar voor de stadsdialoog en voor het hoofdscherm. Als u
burgemeester-bestuurde en handbestuurde steden wilt hebben, bent u
waarschijnlijk het beste uit als u deze op verschillende eilanden zet.

Er zijn verschillende situaties waarbij de burgemeester de doelstellingen
slechts tijdelijk kan halen, bijv. wanneer u een schip verplaatst van de ene
stad naar de andere, of wanneer een vijand langs uw stad loopt. De
burgemeester geeft dan de besturing aan u terug en u moet dan de burgemeester 
handmatig weer aanzetten. Een algemene benadering om dit te voorkomen kan zijn
door de minimale opbrengsten zo laag mogelijk te zetten (-20). Uiteraard moet
u voorzichtig zijn met de voedings- en produktie-overcapaciteit.

Hoewel de burgemeester heel goed werk levert voor een enkele stad, zal geen
vlak ooit vrijgegeven worden voor het welzijn van een andere stad. Ook worden
de 'geburgemeesterde' steden in een willekeurige volgorde berekend; de
resultaten kunnen daarvan afhangen en veranderen wanneer een herberekening
wordt gedaan (wanneer bijv. de belastingswaarde verandert). Er is dus geen
garantie dat de totale resultaten altijd optimaal zullen zijn.


  Instellingenbestand
=======================

De client staat de gebruiker toe om snelkeuzen voor de burgemeester op te
slaan en te laden. Als u 'Bewaar instellingen' van het 'Spel' menu zal niet
alleen uw algemene- en berichtopties opslaan, maar zal ook alle wijzigingen
opslaan die u gemaakt heeft in uw burgemeester's snelkeuzen.

Het formaat voor het opties-bestand (meestal ~/.civclientrc) is als volgt (in
het geval dat u deze handmatig wilt veranderen, bijv met een editor):

Onder het kopje '[cma]' is een 'number_of_presets'. Deze hoort op het aantal
snelkeuzen gezet te worden die aanwezig zijn in het opties-bestand. Als u
handmatig een snelkeuze toevoegt of verwijdert dient u deze waarde
overeenkomstig aan te passen.

Hierna is een tabel die de snelkeuzen bevat. Hier is de kop:

preset={ "naam","minover0","factor0",'minover1","factor1","minover2",
"factor2","minover3","factor3","minover4","factor4","minover5","factor5",
"ben.gelukkig","factordoel","vieringfactor"

dus de volgorde van de snelkeuze moet als volgt zijn:

naam van de snelkeuze, minimale overcapaciteit 0, factor 0, ...
benodigt of de stad gelukkig moet zijn, wat het doel moet zijn [0,1],
de vieringsfactor

Op dit moment zijn er 6 overcapaciteiten en factoren. Het zijn:
0 = voeding, 1 = produktie, 2 = handel, 3 = goud, 4 = luxe, 5 = wetenschap

Ook is op dit moment "factordoel" niet instelbaar met de client, zie
"client/agents/citizen_management.h" voor meer informatie.

De tabel dient afgesloten te worden met een '}'

Hier volgen 21 snelkeuzen die u gebruiken kunt als u zelf niet iets zinnigs
kunt verzinnen:

"Max voeding",0,10,0,1,0,1,0,1,0,1,0,1,0,0,1
"Max produktie",0,1,0,10,0,1,0,1,0,1,0,1,0,0,1
"Max handel",0,1,0,1,0,10,0,1,0,1,0,1,0,0,1
"Max belasting",0,1,0,1,0,1,0,10,0,1,0,1,0,0,1
"Max luxe",0,1,0,1,0,1,0,1,0,10,0,1,0,0,1
"Max wetenschap",0,1,0,1,0,1,0,1,0,1,0,10,0,0,1
"+2 voeding",2,1,0,1,0,1,0,1,0,1,0,1,0,0,1
"+2 produktie",0,1,2,1,0,1,0,1,0,1,0,1,0,0,1
"+2 handel",0,1,0,1,2,1,0,1,0,1,0,1,0,0,1
"+2 goud",0,1,0,1,0,1,2,1,0,1,0,1,0,0,1
"+2 luxe",0,1,0,1,0,1,0,1,2,1,0,1,0,0,1
"+2 wetenschap",0,1,0,1,0,1,0,1,0,1,2,1,0,0,1
"Max voeding geen goudlimiet",0,10,0,1,0,1,-20,1,0,1,0,1,0,0,1
"Max produktie geen goudlimiet",0,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"Max handel geen goudlimiet",0,1,0,1,0,10,-20,1,0,1,0,1,0,0,1
"Max goud geen goudlimiet",0,1,0,1,0,1,-20,10,0,1,0,1,0,0,1
"Max luxe geen goudlimiet",0,1,0,1,0,1,-20,1,0,10,0,1,0,0,1
"Max wetenschap geen goudlimiet",0,1,0,1,0,1,-20,1,0,1,0,10,0,0,1
"Max voeding+prod. geen goudlimiet",0,10,0,10,0,1,-20,1,0,1,0,1,0,0,1
"Max voeding+prod.+handel",0,10,0,10,0,10,0,1,0,1,0,1,0,0,1
"Max alles",0,1,0,1,0,1,0,1,0,1,0,1,0,0,1

Hier zijn nog 6 die zijn toegevoegd na wat overdenken:

"+1 voeding, max prod. geen goudlimiet",1,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+2 voeding, max prod. geen goudlimiet",2,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+3 voeding, max prod. geen goudlimiet",3,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+4 voeding, max prod. geen goudlimiet",4,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+5 voeding, max prod. geen goudlimiet",5,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+6 voeding, max prod. geen goudlimiet",6,1,0,10,0,1,-20,1,0,1,0,1,0,0,1

en zelfs nog meer, sommige met meervoudige doelen:

"onderzoek tegen elke prijs",0,1,0,5,-20,1,-20,1,-20,1,-20,25,0,0,1
"viering en groei",1,1,0,25,-20,1,-20,12,-20,1,-20,1,1,0,1
"groei tegen elke prijs",1,25,0,5,-20,1,-20,1,-20,1,-20,5,0,0,1
"onderzoek en een wat produktie",0,1,0,8,0,1,-3,1,0,1,0,25,0,0,1
"produktie en een beetje goud",0,1,0,25,0,1,-3,3,0,1,0,1,0,0,1
"veel produktie en wat geld",0,1,0,25,0,1,0,9,0,1,0,1,0,0,1
"produktie en wat onderzoek",0,1,0,25,0,1,-2,1,0,1,0,8,0,0,1
"viering en groei tegelijk",1,1,0,25,-20,1,-20,1,-20,1,-20,8,1,0,1

Laatste wijziging 4 febr. 2002


