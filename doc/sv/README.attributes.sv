Klient/servermodell
===================

Varje klientspelare har ett attributblock och servern tillhandahåller
även ett sådant attributblock för varje spelare. Alla attributblock
som servern tillhandahåller skrivs till sparningsfilen. Klienten och
servern samordnar sina block. Servern skickar sitt block till klienten
vid spelets början eller återladdning. Klienten skickar ett uppdaterat
block till servern vid slutet av varje omgång. Eftersom den största
paketstorleken är 4k och attributblock kan ha godtycklig storlek
(dock begränsat till 64k i denna inledande version) så kan inte
attributblocken skickas i ett enda paket. Så attributblocken delas upp
i attributsjok som åter sammanfogas hos mottagaren. Ingen del av
servern känner till någon inre ordning hos attributblocket. För
servern är ett attributblock endast ett block av betydelselösa data.

Användargränssnitt
==================

Eftersom ett attributblock inte är ett bra användargränssnitt så kan
användaren komma åt attributen genom
kartläggnings-/ordliste-/haskarte-/hashtabellsgränssnitt.
Denna hashtabell serialiseras till attributblocket och omvänt.
Hashtabellens nyckel består av: (den verkliga) nyckeln, x, y och id.
(Den verkliga) nyckeln är ett heltal som definierar användningen och
formatet för detta attribut. Hashtabellens värden kan ha godtycklig
längd. Den inere ordningen hos ett värde är okänt för
attributhanteringen.

För enklare åtkomst finns det omslagsfunktioner för de vanliga typerna
enhet, stad, spelare och ruta. Så det finns enkla sätt att koppla
godtyckliga data till en enhet, stad, spelare (själv eller annan)
eller en ruta.
