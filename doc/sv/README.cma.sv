Stadsförvaltning
================

Stadsförvaltningen är ett ganska nytt verktyg som byggts in i
gtk-klienten i version 1.12.1 av Freeciv. Den är till för att hjälpa
till att hantera städer, det vill säga fördela arbetarna på de olika
arbetsuppgifterna så att städerna blir så lönsamma som möjligt. Man
kan sätta på och stänga av stadsförvaltningen när som helst för vilken
stad som helst, men det uppstår problem om man blandar
stadsförvaltning med eget styre i grupper av städer som överlappar
varandra (se nedan). Stadsförvaltningen kräver serverversion 1.12.1
eller nyare.

Stadsförvaltningen hjärta är en optimerande algoritm som försöker
använda stadens arbetare på ett sätt som uppfyller användarens mål så
mycket som möjligt. Det fanns redan före stadsförvaltningen ett slags
optimering; när man öppnde en stadsdialog och klickade på mittrutan i
kartan (stadskärnan) så sattes arbetarna ut på ett sätt som
huvudsakligen maximerade forskningen, men struntar i ordning/upplopp.

Stadsförvaltningen gör mycket mer än denna enkla optimering. För det
första så utför den beräkningen på nytt varje gång något förändras i
staden, till expempel: staden växer eller krymper, krigsenheter går in
eller ut, rutor får bevattning eller gruva eller besätts av fiender.
För det andra så kan stadsförvaltningen göra alla slags optimeringar,
till exempel tillverkning (sköldar), guld, forskning eller överflöd.
För det tredje så ger den spelaren utförligt inflytande över detta,
med möjlighet att sätta begränsningar för varje slag av utbyte från
staden. Det senare innefattar firande, vilket gör det mycket enkelt
att låta sina städer växa, även i svåra tider. För det fjärde och
antagligen viktigaste i krigstider så håller den stadsborna nöjda så
att det inte blir upplopp.


  Användning
==============

Man kan ställa in stadsförvaltningen för en stad genom att öppna
stadsfönstret och klicka på stadsförvaltningsfliken. På vänster sida
kan man välja en förinställning för ett visst mål. På höger sida kan
man ange mer invecklade mål genom att använda rullningslisterna. Man
kan välja en förinställning först och sedan ändra det. När man har
skapat en ny inställning kan man lägga till det som en förinställning
med ett namn. Detta är inte nödvändigt men mycket användbart eftersom
man kan se och till och med ändra en stads inställning från
stadsredogörelsen om inställningen har ett namn.
Man ska komma i håg att spara inställningarna (i "Spel"-menyn) när man
har skapat en ny förinställning.

Rullningslisterna är av 2 slag: de högra rullningslisterna är
faktorer varmed man kan värdera olika enheter jämfört med varandra,
till exempel hur mycket sköldar är värda jämfört med allt annat. De
vänstra rullningslisterna är begränsningar. Man kan beordra staden att
inte förlora man genom att sätta överskottsbegränsningen för mat till
0, man kan tillåta staden att förlora guld genom att sätta
överskottsbegränsningen för guld till -3, och man kan beordra staden
att tillverka minst 5 sköldar genom att sätta överskottsbegränsningen
för sköldar till 5. Den kraftfullaste begränsningen är dock
firarbegränsningen, vilken gör att staden genast firar (det har
vanligtvis sin verkan omgången efter man ändrar det).

Det är uppenbart att stadsförvaltningen inte kan uppfylla alla dessa
begränsningar i alla lägen. När den inte kan det avgår den med
meddelandet "Kan inte utföra uppdraget i Stockholm. Lämnar uppdraget."
Då kan man välja mellan at sköta staden själv (vilket har vissa
nackdelar, se nedan) eller öppna staden och ändra
överskottsbegränsningarna så att de kan uppfyllas.

När man har gjort inställningarna för en stad måste man klicka på
"Styr stad" för att lämna över staden till stadsförvaltningen. Om
knappen inte är klickbar så styrs staden redan av stadsförvaltningen
eller så är målen ouppnåeliga. I det senare fallet ser man streck i
stället för tal i vinstblocket. Om man någonsin vill stänga av
stadsförvaltningen avsiktligt så trycker på på Släpp stad.


  Fortskridet användande
==========================

Det finns ännu inte mycket erfarenhet av att använda stadsförvaltning,
men några vanliga påpekanden kan vara hjälpsamma. Vanligtvis beror
städernas mål på vilket skede i spelet man befinner sig, om man vill
sprida sig vitt, växa snabbt, forska eller kriga. Man bör sätta en hög
faktor för forskning när man ska forska och en hög faktor för sköldar när
man ska bygga enheter. Den högsta tillgängliga faktorn är 25. Det
innebär att om sköldfaktor är 25 och de andra är 1, så föredrar
stadsförfaltningen en enda sköld över 25 guld (eller handel). Detta är
ganska mycket eftersom man även kan köpa enheter för guld. Det innebär
även att stadsförvaltningen är likgiltig om tillverkning av guld,
forskning, överflöd och mat; men när man krigar föredrar man
vanligtvis guld eller överflöd. Så det är antagligen bra att sätta en
andra (eller till och med tredje) faktor för stadens vinst, alltså
guldfaktor 5. Det gör fortfarande att 1 sköld föredras framför 5 guld
(och 1 guld över 5 mat eller någonting annat).

Begränsningar är inte användbara i alla lägen. Om man vill ha en hög
guldvinst är det antagligen bättre att sätta guldfaktorn till 25 än
att sätta en överskottsgräns på 5 guld eller liknande ty en stor stad
kan ge mer guld än en liten och man skulle därmed behöva sätta olika
överskottsgränser för guld i olika städer.

Om en stads sköldöverskott är under 0 så kan den inte
underhålla alla sina enheter längre. Man förlorar då vissa av
enheterna. Om matöverskottet är under 0 så svälter staden så småningom
och krymper (när sädeslagret är tomt). Detta må vara avsiktligt, men
om staden underhåller bosättare så förloras de innan staden krymper.
Begränsningar kan även varna.

Vilka begränsningar som kan uppfyllas beror mycket på rikets
forsknings-, skatte- och överflödssatser. Ett guldöverskott på >= 0 är
alltså lättare att uppfylla med en högre skattesats än med en lägre.
Man ska alltid överväga att ändra dessa satser när man tänker ändra
stadsförvaltningsdirektiven för de flesta av sina städer.

Råd: För att undivka att oavsiktligt avsätta stadsförvaltningar när
man ändrar satser är det bäst att göra det från skattedialogen i
stället för från satsvisaren i huvudfönstret.


  Nackdelar
============

Stadsförvaltningen är ett kraftfullt verktyg som inte bara avlastar
spelaren från finstyrnignen av städerna utan även ger bättre lönsamhet
än vad de flesta spelarna brukar åstadkomma själva.

Det finns dock några nackdelar. När man har tillsatt
stadsförvaltningen så lägger den beslag på varje bra ruta som den kan
komma åt. Så det blir väldigt svårt att styra överlappande stader
själv. Om man vill ha både städer med stadsförvaltning och städer som
man förvaltar själv så bör de inte överlappa varandra.

Det finns flera lägen då stadsförvaltningen tillfälligt inte kan uppnå
sina mål, till exempel när man förflyttar ett skepp från en stad till
en annan eller när ie fientlig enhet går genom ens land.
Stadsförvaltningen avgår då och man måste återtillsätta den för hand.
Ett allmänt förfarande för att förhindra detta är att sätta
begränsningarna så lågt som möjligt (-20). Naturligtvis måste man vara
försiktig med mat- och sköldöverskotten.

Emedan stadsförvaltningen arbetar utmärkt för enskilda städer så
släpper den aldrig en ruta till förmån för någon annan stad.
Stadsförvaltningarna för de olika städerna kommer på tur i en
slumpmässig ordning. Därför blir den sammanlagda vinsten för en grupp
överlappande städer inte alltid optimal.


  Inställningsfil
===================

Klienten låter användaren ladda och spara förinställningar för
stadsförvaltningen. Välj "Spara inställningar" från "Spel"-menyn för
att spara allmänna inställningar, meddelandeinställningar och även
stadsförvaltningsinställningar.

Formatet för inställningsfilen (vanligtvis ~/.civclientrc) är följande
(i fall att man vill ändra förinställningarna för hand, till exempel
med en textredigerare).

Under överskriften [cma] fins en "number_of_presets". Detta ska sättas
till det antal förinställningar som finns i inställningsfilen. Om man
lägger till eller tar bort förinställningar så måste man ändra detta
tal till rätt värde.

Därefter kommer en tabell med förinställningarna. Här är huvudet:

preset={ "name","minsurp0","factor0","minsurp1","factor1","minsurp2",
"factor2","minsurp3","factor3","minsurp4","factor4","minsurp5",
"factor5","reqhappy","factortarget","happyfactor"

så förinställningarnas ordning ska vara följande:

förinställningens namn, minsta överskott 0, faktor 0, ... , kräv att
stad ska vara lycklig, vad målet ska vara [0, 1], lycklighetsfaktor.

För närvarande finns det 6 överskott och faktorer. De är:
0 = mat, 1 = sköldar, 2 = handel, 3 = guld, 4 = överflöd,
5 = forskning

För tillfället är inte "factortarget" ändringsbar med klienten, se
"client/agents/cma_core.h" för mer information.

Tabellen ska avslutas med '}'. Här är 21 förinställningar om för de
som inte kan komma på sina själva:

"max mat",0,10,0,1,0,1,0,1,0,1,0,1,0,0,1
"max sköldar",0,1,0,10,0,1,0,1,0,1,0,1,0,0,1
"max handel",0,1,0,1,0,10,0,1,0,1,0,1,0,0,1
"max skatt",0,1,0,1,0,1,0,10,0,1,0,1,0,0,1
"max överflöd",0,1,0,1,0,1,0,1,0,10,0,1,0,0,1
"max forskning",0,1,0,1,0,1,0,1,0,1,0,10,0,0,1
"+2 mat",2,1,0,1,0,1,0,1,0,1,0,1,0,0,1
"+2 sköldar",0,1,2,1,0,1,0,1,0,1,0,1,0,0,1
"+2 handel",0,1,0,1,2,1,0,1,0,1,0,1,0,0,1
"+2 guld",0,1,0,1,0,1,2,1,0,1,0,1,0,0,1
"+2 överflöd",0,1,0,1,0,1,0,1,2,1,0,1,0,0,1
"+2 forskning",0,1,0,1,0,1,0,1,0,1,2,1,0,0,1
"max mat ingen guldbegränsning",0,10,0,1,0,1,-20,1,0,1,0,1,0,0,1
"max sköldar ingen guldbegränsning",0,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"max handel ingen guldbegränsning",0,1,0,1,0,10,-20,1,0,1,0,1,0,0,1
"max guld ingen guldbegränsning",0,1,0,1,0,1,-20,10,0,1,0,1,0,0,1
"max överflöd ingen guldbegränsning",0,1,0,1,0,1,-20,1,0,10,0,1,0,0,1
"max science ingen guldbegränsning",0,1,0,1,0,1,-20,1,0,1,0,10,0,0,1
"max mat+sköldar ingen guldbegränsning",0,10,0,10,0,1,-20,1,0,1,0,1,0,0,1
"max mat+sköldar+handel",0,10,0,10,0,10,0,1,0,1,0,1,0,0,1
"max allt",0,1,0,1,0,1,0,1,0,1,0,1,0,0,1

här är 6 till som har lagt till i efterhand:

"+1 mat, max sköldar ingen guldbegränsning",1,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+2 mat, max sköldar ingen guldbegränsning",2,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+3 mat, max sköldar ingen guldbegränsning",3,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+4 mat, max sköldar ingen guldbegränsning",4,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+5 mat, max sköldar ingen guldbegränsning",5,1,0,10,0,1,-20,1,0,1,0,1,0,0,1
"+6 mat, max sköldar ingen guldbegränsning",6,1,0,10,0,1,-20,1,0,1,0,1,0,0,1

and even more, some with multiple goals:

"forskning till varje pris",0,1,0,5,-20,1,-20,1,-20,1,-20,25,0,0,1
"firande och tillväxt",1,1,0,25,-20,1,-20,12,-20,1,-20,1,1,0,1
"tillväxt till varje pris",1,25,0,5,-20,1,-20,1,-20,1,-20,5,0,0,1
"forskning och några sköldar",0,1,0,8,0,1,-3,1,0,1,0,25,0,0,1
"sköldar och lite guld",0,1,0,25,0,1,-3,3,0,1,0,1,0,0,1
"många sköldar och lite guld",0,1,0,25,0,1,0,9,0,1,0,1,0,0,1
"sköldar och lite forskning",0,1,0,25,0,1,-2,1,0,1,0,8,0,0,1
"fira och väx genast",1,1,0,25,-20,1,-20,1,-20,1,-20,8,1,0,1

senast uppdaterad 2002-01-09, översatt 2002-07-04
