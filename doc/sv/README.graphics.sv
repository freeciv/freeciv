----------------------------------------------------------------------
Freecivs bilder och bildbeskrivningsfiler
----------------------------------------------------------------------

Att använda bilder:
-------------------

För att använda andra bilder än standardbilderna med Freeciv ger man
"--tiles" som kommandoradsargument till freecivklienten. För att till
exempel andänva "Engels"-bilderna kör man ageten med:

  civclient --tiles engels

Vad Freeciv gör då är att leta efter en fil med namnet
"engels.tilespec" någonstans i Freecivs datasökväg. (Se filen INSTALL
för information om Freecivs datasökväg.) Denna beskrivningsfil
innehåller information om vilka bildfiler som ska användas och vad de
innehåller.

Det är allt som man behöver veta för att använda andra
bilduppsättningar som följer med Freeciv eller sprids för sig av
tredje part. Resten av denna fil beskriver (dock inte fullständigt)
innehållet i beskrivningsfilen och andra filer i sammanhanget. Detta
är tänkt som utvecklarhandledning och för folk som vill
skapa/sammanställa alternativa bilduppsättningar och
ändringsuppsättningar för Freeciv.

----------------------------------------------------------------------
Översikt:
---------

Syftet med beskrivningsfilen "tilespec" och andra beskrivningsfiler i
sammanhanget är att beskriva hur de olika bilderna är lagrade i
bildfilerna så att denna information inte behöver vara hårdkodad i
Freeciv. Därför är det enkelt att erbjuda ytterligare bilder som
tillägg.

Det är två lager i beskrivningsfilerna:

Filen med det övre lagret har till exempel namnet "trident.tilespec".
Filens grundnamn, i detta fall "trident", motsvarar det
kommandoradsargument som skrivs efter "--tiles" på freecivklientens
kommandorad, såsom beskrivet ovan.

Den filen innehåller allmän information om hela bilduppsättningen och
en lista över filer som ger information om de ensilda bildfilerna.
Dessa filer måste finnas någonstans i datasökvägen, men inte
nödvändigtvis på samma ställe som det övre lagrets beskrivningsfil.
Lägg märke till att de hänvisade filernas antal och innehåll är helt
anpasningsbart.

Ett undantag är att inledningsbilderna måste vara i enskilda filer,
såsom det beskrivs i beskrivningsfilen, ty Freeciv särbehandlar dessa:
de tas bort ur arbetsminnet när spelet börjar och läses in igen om det
behövs.

----------------------------------------------------------------------
Enskilda beskrivningsfiler:
---------------------------

Varje beskrivningsfil beskriver en bildfil (för närvarande endast i
xpm-format) såsom det anges i beskrivningsfilen. Bildfilen måste
finnas i Freecivs datasökväg, men inte nödvändigtvis nära
beskrivningsfilen. (Därför man man ha flera beskrivningsfiler som
använder samma bildfil på olika sätt.)

Huvudinformationen som beskrivs i beskrivningsfilen står i stycken som
heter [grid_*], där * är något godtyckligt märke (men entydigt inom
varje fil). Ett rutnät motsvarar en vanlig tabell av rutor. I
allmänhet kan man ha flera rutnät i varje fil, men
standardbilduppsättningarna har vanligtvis bara ett i varje fil.
(Flera rutnät i samma fil vore användbart för att ha olika
rutstorlekar i samma fil.) Varje rutnät anger ett utgångsläge (övre
vänstra) och rutavstånd, båda i bildpunkter, och hänvisar sedan till
ensilda rutor i rutnätet med hjälp av rad och kolumn. Rader och
kolumner räknas med (0, 0) som övre vänstra.

Enskilda rutor ges ett märke som är en sträng som det hänvisas
till i koden eller från regeluppsättningsfilerna. Ett rutnät kan vara
glest, med tomma rutor (deras koordinater nämns helt enkelt inte), och
en enda ruta kan ha flera märken (för att använda samma bild för flera
syften i spelet), ange helt enkelt en lista med kommateckenskilda
strängar.

Om ett givet märke förekommer flera gånger i en beskrivningsfil så
används den sista förekomsten. (Alltså i den ordning som filerna är
nämnda i det övre lagrets beskrivningsfil och ordningen i varje
ensild fil.) Detta tillåter att valda bilder ersätts genom att ange en
ersättningsbeskrivningsfil nära slutet av fillistan i det övre lagrets
beskrivningsfil lagret utan att ändra tidigare filer i listan.

----------------------------------------------------------------------
Märkesförstavelser:
-------------------

För att hålla ordning på märkena finns det ett grovt förstavelsesystem
som används för vanliga märken:

  f.	        nationsflaggor
  r.	        väg/järnväg
  s.	        allmänt liten
  u.	        enhetsbilder
  t.	        grundlandskapsslag (med _n0s0e0w0 to _n1s1e1w1)
  ts.	        särskilda tillgångar i landskapet
  tx.	        ytterligare landskapsegenskaper
  gov.	      regeringsformer
  unit.	      ytterligare enhetsinformation: träffpunkter, stack,
              sysslor (gå till, befäst med mera)
  upkeep.     enhetsunderhåll och lycklighet
  city.	      stadsinformation (stad, storlek, tillverkning i ruta,
              upplopp, upptagen)
  cd.	        standardvärden för stad
  citizen.    stadsinvånare, även fackmän
  explode.    explosionsbilder (kärnvapen, enheter)
  spaceship.  rymdskeppsdelar
  treaty.     avtalstummar
  user.	      hårkors (i allmänhet: användargränssnitt?)

I allmänhet måste bildmärken som är hårdkodade i Freeciv
tillhandahållas i beskrivningsfilerna, annars vägrar klienten att gå
igång. Bildmärken som ges av regeluppsättningarna (åtminstone
standardregeluppsättningarna) ska också tillhandahållas, men i
allmänhet går klienten igång ändå, men då kan klientens bildvisning
bli alltför bristfällig för användaren. För att fungera ordentligt ska
märkena hänvisa till bilder med lämplig storlek. (Grundstorleken kan
variera, såsom anges i det övre lagrets beskrivningsfil, men de
ensilda rutbilderna ska stämma överens med dessa storlekar eller
användningen av dessa bilder.)

----------------------------------------------------------------------
