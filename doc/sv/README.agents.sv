Översikt
========

Ett ombud är ett kodstycke som ansvarar för ett visst område. Ett
ombud får ett uppdrag av sin användare, samt en uppsättning objekt som
ombudet kan styra over (till exempel tillverkningen i en stad, en stad
i helhet, en enhet, en uppsättning enheter eller alla rikets enheter).
Användare kan vara en mänskig spelare eller en annan del av
programkoden, till exempel ett annat ombud. Det sker inget ytterligare
samspel mellan användare och ombud efter att ombudet fått sin
uppdragsbeskrivning.

Exempel på ombud:
 - ett ombud som ansvarar för att förflytta en enhet från A till B
 - ett ombud som ansvarar för att ge största möjliga mattillverkning i
   en stad
 - ett ombud som ansvarar för tillverkningen i en stad
 - ett ombud som ansvarar för försvaret av en stad
 - ett ombud som ansvarar för en stad
 - ett ombud som ansvarar för alla städer

Ett ombud kan använda andra ombud för att uppnå sina mål. Sådana
beroenden bildar en rangordning av ombud. Ombud har en rang i denna
rangordning. Ombud med högre rang är mer invecklade. Ett ombud av rang
n kan endast använda ombud av rang (n - 1) eller lägre. Rang 0
innebär handlingar som utförs av servern och är odelbara (inte kan
simuleras av klienten).

Genom en sådan definition behöver ett ombut inte vara implementerad i C
och behöver inte heller använda client/agents/agents.[ch].

Kärnan i ett ombud utgörs av 2 delar: en del som fattar beslut och en
del som verkställer beslut. Den första delens beslut ska göras
tillgängliga. Ett ombud som saknar den verkställande delen kallas
rådgivare.

Ett ombud ska förutom kärnan tillhandahålla ett grafiskt gränssnitt.

Implementering
==============

Den mottagna uppdragsbeskrivningen och alla beslut som har fattats kan
sparas i attribut. Ett ombud ska inte göra några antaganden. Detta
innebär: INGA MAGISKA TAL. Allt ska vara inställbart av användaren.

Använd clients/agents/agents.[ch] för att informeras om vissa
händelser. Tveka inte att lägga till fler återrop. Använd
client/agents/agents:wait_for_requests i stället för
client/civclient:wait_till_request_got_processed.
