===========================================================================
 Freecivs Stilvägledning
===========================================================================

Om man vill koda Freeciv och få sina patchar antagna så hjälper det
att följa några enkla stilregler. Ja, några av dessa är en aning
petiga, men krig utkämpas över de fånigaste saker...

- Använd K&R-indragsstil med ingrag 2 (vid tveksamhet, använd
  "indent -kr -i2"). Men man ska inte ändra stilen på kodstycken som
  man inte ändrar på annat sätt eller skapar.

- Sätt radlängden till 80 kolumner. Rader ska aldrig någonsin brytas.

- En tom rad ska sättas mellan 2 åtskilda kodstycken.

- Blanksteg ska sättas före och efter operatorer och efter
  kommatecken:

int a,b,c;   /* dåligt */
int i, j, k; /* bra */

if(foo<=bar) {   /* dåligt */
  c=a+b;
}

if(foo <= bar) { /* bra */
  c = a + b;
}

================================
 Kommentarer
================================

- Använd inte kommentarer i C++-stil (det vill säga "//").

- Varje funktion ska ha ett kommentarhuvud. Det ska vara ovanför
  funktionens implementering, inte prototypen:

/*************************************************************************
 funktionsbeskrivningen ska vara här
 all användbar information såsom vad som anropar funktionen med mera
 skapa _inte_ en ny funktion utan någon form av kommentar
*************************************************************************/

int the_function_starts_here(int value) 
{
  ...
}

- Enradskommentarer: Kommentaren ska vara rätt indragen och stå
  ovanför koden som kommenteas:

  int x;

  /* Detta är en enradskommentar */
  x = 3;

- Flerradskommentarer: Stjärnor ska stå framför kommentarraden:

  /* Detta är en
   * flerradskommentar 
   * bla bla bla */

- Kommentarer i deklarationer. Om man behöver kommentera en
  variabeldeklaration så ska det göras så här:

  struct foo {
    int bar;                    /* bar används för ....
                                 * att ..... ska */
    int blah;                   /* blah används för .... */
  };

- Kommentarer i villkorssatser: Om man behöver en kommentar för att
  visa programflödet så ska den stå nedanför if eller else:

  if(is_barbarian(pplayer)) {
    x++;
  } else {
    /* Om inte barbar ... */
    x--;
  }

- Kommentarer till översättare står före strängen som är märkt med
  N_(), _() eller Q_() och har förstavelsen "TRANS:". Dessa
  kommentarer kopieras till översättningsfilerna. De ska användas
  närhelst man tycker att översättarna kan behöva lite mer
  information:

    /* TRANS: Översätt inte "commandname". */
    printf(_("commandname <arg> [-o <optarg>]"));

================================
 Deklarera variabler
================================

- Variabler kan tilldelas ett värde vid initialiseringen:

int foo(struct unit *punit)
{
  int x = punit->x;
  int foo = x;
  char *blah;

  ...
}

- Efter variabeldeklarationerna ska det vara en tom rad före resten av
  funktionskroppen.

- Sammanfoga deklarationer: Variabler behöver inte dekrareras på var
  sin rad. De ska dock endast sammanfogas om de har liknande
  användning.

int foo(struct city *pcity)
{
  int i, j, k;
  int total, cost;
  int build = pcity->shield_stock;
}

================================
 Klamrar
================================

- Funktionsklamrar ska stå i första kolumnen:

int foo()
{
  return 0;
}

 och inte:

int foo() {
  return 0;
}

- Ytterligare klamrar på upprepningar: Lägg märke till att
  *_iterate_end; ska stå på samma rad som slutklammern:

  unit_list_iterate(pcity->units_supported, punit) {
    kill(punit);
  } unit_list_iterate_end;

- I switchsatser ska kramrar bara sättas där de behövs, till exempel
  för att skydda variabler.

- I allmännhet ska (onödiga) klamrar sättas efter villkorssatser:

  if(x == 3) {
    return;
  }

 och 

  if(x == 3) {
    return 1;
  } else {
    return 0;
  }

================================
 Annat
================================

- Om ett tomt block behövs så ska man sätta en förklarnde kommentar i
  det:

  while(*i++) {
    /* nothing */
  }

- Ordna inkluderingsfiler konsekvent: Alla systeminkluderingsfiler ska
  stå först, inom <> och i bokstavsordning. Sedan ska alla
  freecivinkluderingsfiler stå inom "" och ordnade efter katalog
  (common, server, ...) och sedan i bokstavsordning. Det ska vara en
  tom rad mellan styckena. Detta hjälper till att undvika dubbla
  inkluderingar.

- Om man använder systemberoende funktioner så ska man inte lägga till
  #ifdef __CRAY__ eller liknande. Man ska i stället skriva ett prov
	för funktionen i configure.in och använda ett meningsfullt makronamn
	i källkoden.

- Globala funktioner ska alltid ha prototyper i lämpliga
  inkluderingsfiler. Lokala funktioner ska alltid deklareras som
  static.

- Om man skickar patchar så ska man använda "diff -u" (eller "diff -r
  -u"). Se <http://www.freeciv.org/contribute.html> för ytterligare
	upplysningar. Man ska även namnge patcharna beskrivande (till
	exempel "fix-foo-0.diff", men inte "freeciv.diff").

- När man kör "diff" för en patch så ska man se till att utesluta
  onödiga filer genom att använda kommandoradsargumentet "-X" för
  programmet "diff":

    % diff -ruN -Xdiff_ignore freeciv_c freeciv > /tmp/fix-foo-0.diff

  En föreslagen "diff_ignore"-fil följer med Freeciv.

===========================================================================
