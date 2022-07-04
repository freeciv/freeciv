===================
Freeciv Version 3.1
===================

Bienvenue à Freeciv !

Cette archive contient Freeciv, un clone libre de Civilization pour X,
principalement sous Unix. Il permet les parties multijoueurs locales ou
à travers un réseau, et inclut une IA qui donne du fil à retordre à la
plupart des gens.

Le but de Freeciv est d'avoir des règles compatibles avec Civilization
II [tm], publié par Sid Meier et Microprose [tm]. Quelques règles
diffèrent, lorsque nous pensons que c'est plus logique, et il y a
beaucoup de paramètres réglables pour permettre la personnalisation des
parties.

Freeciv a été implémenté totalement indépendamment de Civilization ;
vous n'avez pas besoin de posséder Civilization pour jouer à Freeciv.

Bien que les graphismes ne soient pas aussi soignés que ceux de
Civilization II, les règles sont très complètes et notre code réseau et
multijoueurs est excellent.

Traductions :
=============

Vous pouvez trouver des versions traduites de ce fichier, ainsi que
d'autres parties de la documentation de Freeciv, aux emplacements
suivants :

        Catalan                 ./doc/ca
        Hollandais              ./doc/nl
        Anglais                 ./doc
        Allemand                ./doc/de
        Italien                 ./doc/it
        Japonais                ./doc/ja
        Suédois                 ./doc/sv

Même s'il n'y a pas de traduction pour votre langue, le jeu lui-même
la supporte peut-être. Prière de consulter la partie "Localisation"
ci-dessous.

Site web :
==========

Le site web de Freeciv se trouve à cette adresse :

  https://www.freeciv.org/

Nous vous invitons à le visiter. Vous pourrez y connaître les dernières
nouvelles concernant Freeciv, télécharger les dernières versions et
patches, trouver des informations à propos des listes de diffusion et
consulter le métaserveur Freeciv, qui répertorie des parties du monde
entier.


Licence :
=========

Freeciv est distribué sous la GNU General Public License (version 2
ou, à votre discrétion, tout version plus récente). En bref, vous
pouvez copier ce programme (y compris ses sources) librement, mais
consultez le fichier COPYING.fr pour les détails complets.

Certains des éléments utilisés pour préparer les graphismes du jeu
(dans le sous-répertoire 'data'), comme les modèles 3D utilisés pour
créer les images bitmap, ne sont pas distribués avec le code source
principal à cause de leur taille ; ils ne sont pas nécessaires pour
construire Freeciv à partir de ses sources, puisque les graphismes
(manuellement) dérivés sont aussi inclus dans la distribution. Ces
éléments sont disponibles dans la distribution séparée
'graphics-material' (par exemple,
freeciv-2.4.0-graphics-materials.tar.bz2), ou depuis le gestionnaire
de versions (Git).


Compilation et installation :
=============================

Merci de lire attentivement le fichier INSTALL.fr pour savoir comment
compiler et installer Freeciv sur votre machine.


Freeciv est modulaire :
=======================

Freeciv est en fait plusieurs programmes, un serveur et un ou
plusieurs clients. Quand le jeu sera en cours, il y aura un programme
serveur en fonctionnement, et autant de programmes clients qu'il y a
de joueurs humains. Le serveur n'utilise pas d'IHM, mais les clients,
si. Il y a de nombreuses variétés de clients :

freeciv-gtk3 : Il utilise les librairies GTK+ 3. Pour l'installer,
  voir la section 1a du document INSTALL.fr.

freeciv-gtk3.22 : Il utilise les librairies GTK+ 3. La version 3.22 ou
  3.24 est nécessaire. Pour l'installer, voir la version 1b du
  document INSTALL.fr. Ce client est mieux supporté et plus développé
  que les autres ; il est de ce fait considéré comme étant l'interface
  par défaut dans le reste de ce document.

freeciv-qt : Il utilise la librairie QT. Le développement de ce client
  a récemment atteint l'état où il est généralement considéré comme
  utilisable, mais il lui manque encore un certain nombre de
  fonctionnalités dont disposent les clients GTK.

freeciv-sdl2 : Il utilise les librairies Simple DirectMedia Layer version 2.

freeciv-gtk4 : Il utilise les librairies GTK+ 3.90+. C'est une
  version de développement d'un client qui évoluera pour utiliser GTK4
  une fois qu'il sera diffusé.

Commencer une partie:
=====================

  NOTE :
  Les exemples suivants partent du principe que Freeciv a été installé
  sur votre système, et que le répertoire contenant les programmes
  "freeciv-gtk3" et "freeciv-server" est dans votre PATH. Si Freeciv
  n'est pas installé, vous pouvez utiliser les programmes "fcgui" et
  "fcser", qui se trouvent dans le répertoire racine de Freeciv. Ils
  s'utilisent exactement de la même façon que "freeciv-gtk3" et
  "freeciv-server".


Jouer à Freeciv implique de lancer le serveur, puis le(s) client(s) et
de créer la ou les IA. Ensuite, vous devez dire au serveir de lancer
la partie. Voici les étapes :


Serveur :

  Pour lancer le serveur :

  |  % freeciv-server

  Ou pour la liste des options en ligne de commande :

  |  % freeciv-server --help

  Une fois que le serveur est lancé, une invite doit apparaître :

  |  Pour obtenir une aide sommaire, tapez 'help'.
  |  >

  [ Si ce message n'est pas en Français, et que vous voulez absolument
  jouer dans notre langue, positionnez la variable d'environnement
  LANG à "fr". Pour plus de détails, consultez le fichier
  INSTALL.fr. ]

  Et vous pourrez alors voir l'information suivante en tapant la
  commande help :

  |  > help
  |  Bienvenue - ceci est le texte d'aide introductif du serveur Freeciv.
  |
  |  Les commandes et les options sont deux concepts importants pour le
  |  serveur. Les commandes, comme 'help', sont utilisées pour interagir
  |  avec le serveur. Certaines commandes prennent un ou plusieurs
  |  arguments, séparés par des espaces. Dans la plupart des cas, les
  |  commandes et les arguments des commandes peuvent être abrégés. Les
  |  options sont des paramètres qui contrôlent la façon dont fonctionne
  |  le serveur.
  |
  |  Pour savoir comment obtenir plus d'information sur les commandes et
  |  les options, utilisez 'help help'.
  |
  |  Pour les impatients, les commandes principales pour commencer sont :
  |    show  - pour voir les options actuelles
  |    set   - pour régler des options
  |    start - pour lancer la partie une fois que les joueurs sont
  |  connectés
  |    save  - pour sauver la partie en cours
  |    quit  - pour quitter
  |  >

  Si vous le désirez, vous pouvez utiliser la commande 'set' pour régler
  certaines options de la partie. Vous pouvez obtenir une liste des
  options grâce à la commande 'show', et des descriptions détaillées de
  chacune avec 'help <nom-de-l'option>'.

  Par exemple :

  |  > help size
  |  Option : size  -  Taille de la carte (en milliers de cases)
  |  Description :
  |    Cette valeur est utilisée pour déterminer les dimensions de la
  |    carte.
  |      size = 4 est une carte normale de 4000 cases (défaut)
  |      size = 20 est une carte énorme de 20000 cases
  |    Pour que cette option soit prise en compte, l'option "Définition de
  |    la taille de la carte" ('Mapsize') doit être positionnée à "Nombre
  |    de cases" (FULLSIZE).
  |  État : modifiable
  |  Valeur : 4, Minimum : 0, Défaut : 4, Maximum : 2048
  |  >

  Et :

  |  > set size 8

  Ceci rendra la carte deux fois plus grande que celle par défaut.

Client :

  À présent, tous les joueurs humains devraient se connecter en
  lançant le client Freeciv :

  |  % freeciv-gtk3

  Ceci part du principe que le serveur tourne sur la même
  machine. Sinon, vous pouvez le spécifier en ligne de commande à
  l'aide de l'option '--server' ou le saisir dans la première boîte de
  dialogue lorsque le client démarre.

  Par exemple, supposons que le serveur tourne sur une machine
  différente appelée 'neptune'. Dans ce cas, les joueurs rejoignent la
  partie avec une commande de ce style :

  |  % freeciv-gtk3 --server neptune

  Si vous êtes le seul joueur humain, un seul client a besoin d'être
  lancé. Vous pouvez lancer le client "en tâche de fond" de la manière
  standard sous Unix en ajoutant une esperluette (ou "et commercial" -
  '&') :

  |  % freeciv-gtk3 &

  Une autre option du client que vous pourriez vouloir essayer est
  l'option '--tiles', qui peut être utilisée pour choisir des "jeux de
  tuiles" différents (c'est-à-dire des graphismes différents pour le
  terrain, les unités, etc.). La distribution fournit neuf tilesets
  principaux :

  - amplio2 : un jeu de tuiles isométriques, avec des tuiles plus
    grandes et plus détaillées.
  - isotrident : un jeu de tuiles isométriques, de forme similaire à
    celui de Civ 2.
  - cimpletoon : amplio2 avec des unités alternatives qui affichent
    leur direction.
  - trident : un jeu de tuiles de style Civ 1 ('vue de dessus'), avec
    des tuiles de 30×30.
  - hexemplio : un jeu de tuiles iso-hexagonal avec des tuiles plus
    grandes, dérivées d'amplio2.
  - toonhex : un jeu de tuiles qui combine les tuiles hexemplio avec
    les unités cimpletoon.
  - isophex : un jeu de tuiles iso-hexagonal
  - hex2t : un jeu de tuiles hexagonal
  - alio : un jeu de tuiles iso-hexagonal avec des graphiques adaptés
    pour les règles du jeu alien.

  Dans cette distribution, chaque topologie de carte a son propre jeu
  de tuiles par défaut ; avec les options par défaut, amplio2 est le
  jeu de tuiles par défaut.
  Pour en essayer un autre, par exemple le jeu de tuiles trident,
  lancez le client ainsi :

  |  % freeciv-gtk3 --tiles trident

  D'autres jeux de tuiles sont disponibles sur
  https://www.freeciv.org/wiki/Tilesets

  Les clients peuvent être autorisés à utiliser des commandes du
  serveur. Pour les autoriser à n'utiliser que des commandes
  d'information, tapez ce qui suit à l'invite du serveur :

  |  > cmdlevel info

  Les clients peuvent à présent utiliser '/help', '/list', '/show
  settlers', etc.


Joueurs IA :

  Il y a deux façons de créer des joueurs IA. La première est de
  régler le nombre total de joueurs (humains et IA) à l'aide de
  l'option 'aifill' du serveur. Par exemple :

  |  > set aifill 7

  Après avoir utilisé la commande 'start' du serveur pour commencer la
  partie, tous les joueurs qui ne sont pas contrôlés par des humains
  seront des joueurs IA. Dans l'exemple ci-dessus, si 2 joueurs
  humains ont rejoint la partie, 5 joueurs IA seront créés.

  La deuxième façon est de créer explicitement une IA avec la commande
  'create' du serveur. Par exemple :

  |  > create TueurdHumains

  Ceci créera un joueur IA appelé TueurdHumains.

  Des nations sont assignées aux joueurs IA après que tous les joueurs
  humains aient choisi les leurs, mais vous pouvez choisir une nation
  particulière pour un joueur IA en utilisant le nom normal pour le
  chef de cette nation. Par exemple, pour jouer contre des Romains
  contrôlés par l'IA, utilisez la commande du serveur suivante :

  |  > create César

  Note : ceci n'est qu'un préférence. Si aucun joueur humain ne choisit
  de jouer les Romains, alors cette IA les prendra.

Serveur :

  Une fois que tout le monde a rejoint la partie (utilisez la commande
  "list" pour savoir qui est là), lancez la partie avec la commande
  "start" :

  |  > start

Et la partie est lancée !

  NOTE : Les clients ont la capacité de lancer une session
  freeciv-server automagiquement quand l'utilisateur sélectionne
  "Démarrer une nouvelle partie" à partir du menu principal. Ceci
  réduit les étapes nécessaires pour commencer à jouer un jeu de
  Freeciv. D'un autre côté, cela veut aussi dire que si le client
  tombe en panne pour une raison quelconque ou devient inaccessible,
  alors la session freeciv-server sera également perdue. De ce fait,
  lancer une session freeciv-server séparée, et s'y connecter ensuite
  avec le client est généralement la méthode recommandée.

Annoncer la partie :
====================

Si vous ne voulez pas limiter vos opposants à des amis locaux ou aux
joueurs IA, visitez le métaserveur Freeciv :

  https://meta.freeciv.org/

C'est une liste de serveurs Freeciv. Pour que votre propre serveur s'y
annonce lui-même, lancez freeciv-server avec l'option '--meta', ou
'-m'.

Avertissements :

 1) Étant donnée l'inclusion de nouvelles fonctionnalités, des
    versions différentes du client et du serveur sont souvent
    incompatibles. La version 2.5.0, par exemple, est incompatible
    avec toutes les versions 2.4.x ou plus anciennes.

 2) Si le bouton Métaserveur dans la boîte de dialogue de connexion ne
    fonctionne pas, vérifiez si votre fournisser d'accès utilise un
    proxy web et signalez-le au client à l'aide de la variable
    d'environnement $http_proxy. Par exemple, si le proxy est
    proxy.monfournisseur.com, sur le port 8888, positionnez
    $http_proxy à http://proxy.monfournisseur.com:8888/ avant de
    lancer le client.

 3) Parfois, il n'y a pas de partie sur le métaserveur. Cela
    arrive. Le nombre de joueurs varie en fonction de l'heure de la
    journée. Essayez d'en lancer une vous-même !


Jouer :
=======

La partie peut être sauvée à n'importe quel moment en utilisant la
commande 'save' du serveur, ainsi :

  |  > save mapartie.sav

(Si votre serveur est compilé avec le support de la compression et que
l'option serveur 'compresstype' du serveur a une autre valeur que
PLAIN, alors le fichier écrit peut être compressé et appelé
'mapartie.sav.gz', 'mapartie.sav.bz2' ou 'mapartie.sav.xz', en
fonction de l'option.)

Le client Freeciv fonctionne d'une façon assez proche de ce à quoi on
peut s'attendre pour une partie de Civilization multijoueurs -
c'est-à-dire que les joueurs humains se déplacent tous en même temps,
puis tous les joueurs IA se déplacent lorsque tous les joueurs humains
ont terminé leur tour. Il y a une valeur de timeout pour les tours,
qui est par défaut de 0 seconde (pas de timeout). L'administrateur du
serveur peut modifier cette valeur à n'importe quel moment grâce à la
commande 'set'.

Jetez un oeil au système d'aide en ligne. Les trois boutons de la
souris sont utilisés, et documentés dans l'aide.

Les joueurs peuvent maintenir 'Maj' pressée et appuyer sur la touche
'Entrée' pour annoncer la fin de leur tour, ou simplement cliquer sur
le bouton 'Fin du tour'.

Utilisez la boîte de dialogue 'Joueurs' pour savoir qui a annoncé la
fin de son tour, et qui vous attendez ("Hé, gars ! Tu t'es endormi ou
quoi ?" ;-) ).

Utilisez la ligne de saisie en bas de la fenêtre pour diffuser des
messages aux autres joueurs.

Vous pouvez envoyer un message à un seul joueur (par exemple 'pierre') :

  |  pierre: enlève ce tank *TOUT DE SUITE* !

Le serveur est suffisamment intelligent pour faire de la "complétion
de nom" ; ainsi, si vous aviez tapé "pie:", il aurait trouvé un nom de
joueur qui corresponde à la partie du nom que vous avec tapée.

Vous pouvez envoyer un message à tous vos alliés en le préfixant avec
la lettre '.' (oui, c'est un point.)

Vous pouvez exécuter des commandes du serveur depuis la ligne de
saisie du client :

  |  /list
  |  /set settlers 4
  |  /save mapartie.sav

L'administrateur du serveur ne vous laissera probablement lancer que
des commandes d'information. Ceci est en partie dû au fait que laisser
des clients utiliser toutes les commandes du serveur a des
répercussions sur la sécurité ; imaginez qu'un joueur essaye :

  |  /save /etc/passwd

Bien sûr, le serveur ne devrait en aucun cas être lancé avec les
privilèges de super-utilisateur pour réduire ce genre de risques.

Si vous débutez juste et que désirez avoir une idée d'une stratégie
possible, consultez le "Freeciv playing HOWTO", contenu dans le
fichier HOWTOPLAY.fr.

Pour avoir beaucoup plus de renseignements à propos du client, du
serveur et des concepts et des règles du jeu, consultez le manuel de
Freeciv disponible sur le site wiki :

  https://www.freeciv.org/wiki-fr/Manuel
  ou
  https://www.freeciv.org/wiki/Manual (en anglais)

Fin de la partie :
==================

Il y a six façons dont une partie peut se terminer :

1) Il ne reste que des gagnants dans le jeu
  1a) Si la clause de victoire 'ALLIED' est activée dans l'option
    serveur 'victories' : toutes les nations restantes, sauf celles
    qui ont capitulé (/surrender), sont alliées ou dans la même
    équipe.
  1b) Si la clause de victoire 'ALLIED' est desactivée dans l'option
    serveur 'victories' : il ne reste qu'une seule nation ou qu'une
    seule équipe, ou tous les autres joueurs de toutes les autres
    équipes ont capitulé (/surrender).
2) L'année de fin est atteinte (/show endturn).
3) Si la clause de victoire 'SPACERACE' est activée dans l'option
  serveur 'victories' et qu'un joueur construit et lance un vaisseau
  spatial qui atteint Alpha du Centaure le premier.
4) Si la clause de victoire 'CULTURE' est activée dans l'option
  serveur 'victories' et qu'un joueur a atteint à la fois le nombre de
  points de culture minimal défini dans les règles du jeu, et le
  nombre minimal de points de culture d'avance défini par les règles
  du jeu.
5) Si le modpack a des conditions de victoire particulières, et qu'un
  joueur a atteint l'une d'entre elles.
6) L'opérateur du serveur utilise la commande /endgame.

Un tableau des scores sera montré dans tous les cas. Truc :
l'administrateur du serveur peut changer l'année de fin pendant que la
partie est en cours en modifiant l'option 'endturn'. C'est agréable
lorsque le vainqueur est évident mais que l'on veut pas avoir à jouer
toute la phase ennuyeuse de "nettoyage".


Restauration des parties :
==========================

Vous pouvez restaurer une partie sauvegardée en utilisant l'option
'-f' du serveur, par exemple :

  |  % freeciv-server -f notresauvegarde2001.sav

ou, si le fichier de sauvegarde a été créé par un serveur qui l'a
compressé :

  |  % freeciv-server -f notresauvegarde2001.sav.gz

À présent, les joueurs peuvent rejoindre la partie :

  |  % freeciv-gtk3 -n Alexandre

Remarquez la façon dont le nom du joueur est spécifié avec l'option
-n. Il est primordial que le joueur utilise le même nom qu'il avait
lorsque la partie était en cours, s'ils veulent être autorisés à la
rejoindre.

La partie peut ensuite être redémarrée avec la commande 'start', comme
d'habitude.


Localisation :
==============

Freeciv supporte un certain nombre de langues (dont le Français ^^).

Vous pouvez choisir quel langue locale utiliser, en précisant une
"locale". Chaque locale a un nom standard (par exemple, 'fr' pour le
Français). Si vous avez installé Freeciv, vous pouvez choisir une
locale en positionnant la variable d'environnement LANG au nom
standard de cette locale, avant de lancer freeciv-server et
freeciv-gtk3.

Par exemple, pour utiliser la localisation française, vous feriez :

  export LANG; LANG=fr   (avec un shell Bourne (sh)),
ou
  setenv LANG fr         (avec le shell C (csh)).

(Vous pourriez faire ceci dans votre .profile ou votre .login)

Parfois, il y a un conflit entre l'implémentation de la librairie
locale et la résolution interne de la locale. Il est souvent possible
de contourner les problèmes avec un descripteur plus détaillé :

  LANG=fr_FR.UTF-8

Nous aimerions être au courant de ce genre de problèmes. Merci de les
rapporter en tant que bugs (voir BUGS).

Journal :
=========

Le client et le serveur affichent tous les deux des messages de log
("journal"). Il y a cinq ou six catégories de messages de log :
"fatal", "erreur" (error), "avertissement" (warning), "normal",
"verbeux" (verbose) et, dans les versions de mise au point, "débogage"
(debug).

Par défaut, les messages fatals, d'erreur, d'avertissement et normaux
sont affichés sur la sortie standard à l'endroit où le client et le
serveur ont été lancés. Vous pouvez rediriger les messages de log vers
un fichier au lieu de l'écran avec les options en ligne de commande
"--log fichier" ou "-l fichier".

Vous pouvez modifier le niveau des messages affichés avec "--debug
niveau" ou "-d niveau", où "niveau" est le nom du niveau de log, ou de
la valeur numérique qui lui correspond (l'un de 0, 1, 2, 3 ou 4). Tous
les messages du niveau sélectionné, et inférieur, sont affichés. Par
exemple, au niveau par défaut "normal", vous aurez tous les messages
de niveau "fatal", "erreur", "avertissement" et "normal".

Si vous avez compilé en définissant DEBUG (vous pouvez facilement le
faire en lançant la configuration avec --enable-debug), vous pouvez
avoir les messages de niveau "débogage" en réglant le niveau à "debug"
(ou 5). De plus, il est possible de contrôler les messages de niveau
débogage (mais pas les autres) par fichier et par ligne. Pour ce
faire, utilisez "--debug debug:chaîne1:chaîne2" (autant de chaînes que
vous voulez, séparées par des deux-points) et tous les noms de
fichiers source qui contiennent ces sous-chaînes auront les messages
log de débogage activés, tandis que tous les autres messages de
déboguage seront supprimés. Pour contrôler les lignes, utilisez :
"--debug debug:chaîne1,min,max" et, pour les fichiers qui
correspondent à chaîne1, seuls les messages de débogage entre les
lignes min et max seront affichés. Un seul couple (min,max) peut être
appliqué à chaque fichier.

Exemple :

  |  % freeciv-server -l mon.log -d verbose

Ceci met tous les messages de log du serveur dans le fichier
"mon.log", y compris les messages de niveau verbeux.

Exemple:

  |  % freeciv-gtk3 --debug fatal

Ceci supprime tous les messages de log non fatals du client.

Exemple:

  |  % freeciv-server -d debug:log:freeciv-server,120,500:autoattack

Ceci active tous les messages fatals, d'erreur, d'avertissement,
normaux et verbeux pour le serveur, et les messages de déboguage pour
certains modules. Notez que "log" s'applique aussi bien à "gamelog.c"
qu'à "log.c". Pour "freeciv-server.c", seuls les messages de déboguage
entre les lignes 120 et 500 seront affichés. Cet exemple ne fonctionne
que si le serveur à été compilé avec DEBUG.


Bugs :
======

Vous avez trouvé un bug ? Nous voudrions vraiment que vous nous
préveniez, afin que nous puissions le corriger. Consultez le fichier
BUGS pour avoir une liste des bugs connus dans cette distribution,
ainsi que des renseignements pour signaler de nouveaux bugs.


Listes de diffusion :
=====================

Nous maintenons 4 listes de diffusion :

  freeciv-announce@freelists.org
    Annonces d'intérêt général.
    C'est une liste en "lecture seule", avec des messages peu
    fréquents. En d'autres termes, vous ne pouvez pas écrire à cette
    liste, seulement la lire.
    Pour s'inscrire : https://www.freelists.org/list/freeciv-announce
  freeciv-i18n@freelists.org
    Traduction de Freeciv.
    Toutes les discussions en rapport avec la traduction du code, de
    la documentation et du site Web de Freeciv vers d'autres langues
    que l'anglais.
    Pour s'inscrire : https://www.freelists.org/list/freeciv-i18n
  freeciv-dev@freelists.org
    Développement de Freeciv.
    Pour s'inscrire : https://www.freelists.org/list/freeciv-dev
  freeciv-commits@gna.org
    Notifications de changements dans le dépôt de code.
    C'est une liste en "lecture seule", qui porte des messages
    automatisés. En d'autres mots, vous ne pouvez pas écrire à cette
    liste, uniquement la lire. (Au moment de la rédaction, il est
    prévu que gna.org ferme prochainement ; nous ne sommes pas encore
    sûrs des adaptations qui seront faites pour avoir une liste de
    remplacement pour la diffusion des changements.)

Toutes ces listes sont publiques et chacun est le bienvenu pour s'y
inscrire. Seuls les mainteneurs peuvent poster dans les listes
-announce et -commits.

Internet Relay Chat (IRC)
=========================

Plusieurs joueurs et développeurs traînent sur les canaux #freeciv and
#freeciv-dev sur le réseau Libera.Chat network. Essayez de vous connecter
au serveur

        irc.libera.chat


Et pour conclure :
==================

Amusez-vous bien et envoyez-les en enfer !

				   -- L'équipe de Freeciv
