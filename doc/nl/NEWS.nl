=======================
Welkom bij Freeciv 1.13
=======================

Dit is versie 1.13.0.

Opnieuw dank aan alle ontwikkelaars, die doorgaan met zo hard te werken.

Deze versie bevat vele wijzigingen. Degenen die geïnteresseerd zijn in de
detail-wijzigingen kunnen in het ChangeLog bestand kijken (Engels).

WAT IS ANDERS SINDS 1.12.0

 - Burgemeester staat u toe om werkers en specialisten in de steden te
   automatiseren.
 - Geluidsondersteuning is toegevoegd.
 - De nieuwe 'isotrident' vlakkenset is tot standaard verheven. De 'hires' and
   'engels' vlakkensets zijn uit de distrubutie gehaald en kunnen apart worden
   gedownload van het website.
 - Nieuwe stadsdialoog voor de Gtk client.
 - Windows versie van de client. Hij heeft een verbeterde verbindingsdialoog
   en ondersteunt laden en opslaan vanuit de client.
 - Gtk 2.0 versie van de client.
 - De client zal proberen stadsnamen aan te dragen die overeenkomen met wat ze
   betekenen.
 - Verbeterde spelersdialoog toont sorteerbare en gekleurde informatie
   inclusief de spelers' vlaggen.
 - De server gebruikt niet langer de --server opdrachtregel optie. I.p.d. kunt
   u de --info optie gebruiken om de metaserver aankondigingstekst te zetten
   op wat u maar wilt. De -a optie slaat - indien opgegeven - de
   verbindingsdialoog volledig over.
 - Een 'wall' server-opdracht is toegevoegd die een boodschap geeft aan alle
   spelers.
 - Een nieuwe flexibeler tijdslimiet instelbaar met de "timeoutinc" server
   optie.
 - Overgebleven onderzoekslampen gaan door naar de volgende vooruitgang.
 - Handelsroutes zijn effectiever.
 - Eenheden die schepen in steden aanvallen verdubbelen hun vuurkracht,
   terwijl verdedigers een vuurkracht van slechts 1 krijgen.
 - Helicopters die verdedigen tegen lucht-eenheden krijgen een handicap van
   50% en zien hun vuurkracht verminderd tot 1 tegen jachtvliegtuigen.
 - U kunt stadsmuren bouwen zelfs als u het Lange Muur wonder hebt.
 - De kosten van voeding onder een communistisch bestuur is in de standaard
   spelregels verminderd tot 1, terwijl het in de Civ2 regels veranderd is in
   2.
 - Stealth jachtvliegtuigen en bommenwerpers zijn nu echt stealthy/slecht
   zichtbaar en zijn gedeeltelijk onzichtbaar analoog aan onderzeeërs. Ook is
   de aanvalskracht van de stealth bommenwerper verhoogd van 14 naar 18.
 - De Civ2 spelregels bevatten nu Fundamentalisme.
 - Verbeterde modpack mogelijkheden: de karavaan-eigenschap is nu gesplitst.
   Nieuwe manieren om de technologiekosten te berekenen. Betere documentatie.
   Spelregels kunnen nu starttechnoligieën aangeven. U kunt meer dan één
   bonustech hebben. Kolonist-eigenschap gesplitst. De spelregelsyntax voor 
   gebouwen is aanzienlijk uitgebreid, maar de effecten werken nog niet alle.
 - Server optie 'tinyisles' staat 1x1 eilanden toe en 'separatepoles' staat
   continenten verbonden met de polen toe.
 - 'citymindist' geeft de minimum afstand tussen steden aan, terwijl
   'notradesize' en 'fulltradesize' de handel reguleren van kleinere steden.
 - U kunt ontevreden burgers aanzetten met de 'angrycitizens' optie.
 - Forten kunnen u verbeterd zicht leveren. Zie de 'watchtower' opties.
 - Als u uw paleis verliest krijgt u gratis een nieuwe in een willekeurig
   gekozen stad. Dit gedrag kan uitgeschakeld worden met de server optie
   'savepalace'.
 - Spelregelbestanden worden nu geladen van binnen de server met de 'ruleset'
   opdracht.
 - De limiet op het aantal naties dat kan worden ingesloten met Freeciv is
   verwijderd.
 - Het formaat van het isometrische vlakkenset spec-bestand is gewijzigd.
 - De kaart en de KI-code is aanmerkelijk opgeschoond.
 - Vertalingen verbeterd. Betere ondersteuning voor meervoudsvormen
   toegevoegd.
 - Diverse bugs(=kevers/fouten) verpletterd en heel veel werk verricht onder
   de motorkap.
 - Voor een overzicht van de overgebleven bugs bekijk alstublieft
   doc/nl/BUGS.nl.

WHAT'S CHANGED SINCE 1.11.4

 - Internationalization extended.  Still needs improvement.
   Current localizations: de en_GB es fr hu it ja nl no pl pt pt_BR
   ro ru sv.
 - Isometric view in gtk and amiga clients using the mostly civ2-
   compatible HiRes tileset. This is on by default, but the old non-
   isometric tiles are still available by giving a --tiles argument to
   the client, fx "civclient --tiles trident".
 - While planning a goto (after hitting "g") a line will be displayed
   showing the route from the selected unit to the mouse pointer.
   Hitting "g" will insert a waypoint at the mouse pointer.
 - The server now has readline completion. This works at all levels,
   fx "cu<TAB>" completes to "cut", and if there exist a player named
   "paulz" "cut pau<TAB>" will complete to "cut paulz".
 - Players can agree to give shared vision, which means that you
   automatically see everything the other player sees.
 - Layer view menu items allow you to only display some map info on the
   main map. Fx you can choose to not show roads.
 - The server will ping all connected clients and cut off those too slow
   to respond.
 - Smarter placing of partisans.
 - The server no longer automatically starts when the maximum number of
   players have been reached.
 - If commandlevels are used and the controlling player disconnects a
   connected player can assume the "first" level with the "/firstlevel"
   command.
 - "Restrictions and Limitations" section added to the README.ruleset
 - Caravans, diplomats and spies can move into allied cities.
 - Elephants, Crusaders and Fanatics activated in civ 2 ruleset.
 - The size of the city foodbox is now controlled by the ruleset
   variables "granary_food_ini" and "granary_food_inc".
 - Limit on number of improvement types in rulesets removed.
 - Capitalization is available from the start of the game in the default
   ruleset. (renamed coinage)
 - Cities can have 0 trade. (used to be at least 1)
 - Settlers can only be added to cities less than size 8, as in civ 2.
 - If you paradrop a unit into unknown terrain and the terrain contains
   an enemy unit the paradropping unit is lost. If you drop into terrain
   you thought was land, but which has changed to water, the unit is
   also lost.
 - diplomats/spies can't take action from a ship.
 - Refueling air units at turn update will refuel units with only 1 fuel
   first. Secundary criteria is unit cost.
 - Trireme loss percentage depends upon known technologies.
 - Leonardo's workshop will upgrade a random unit each turn, and not
   just the next one.
 - Allied cities count as friendly when determining whether a unit is
   being agressive. (gives unhappyness under some govs.)
 - Deserts are created primarily 15 to 35 degrees off the equator.
 - Only arctic tiles generated at poles.
 - The server will report when a new government becomes available.
 - Changed wording of message "famine feared" to "famine occured". New
  "famine feared" message just before food runs out.
 - "wonder soon build" message when another player is about to complete
   a wonder.
 - Players are notified when one of their wonders has become obsolete.
 - In the players dialog it is now reported which nations have an
   embassy with you. Your embassies are also listed.
 - Wonders being built are listed in the "wonders of the world" popup.
 - Server "save" command saves to <auto-save name prefix><year>m.sav[.gz]
   if it is not given any arguments.
 - "quitidle" server commandline option makes server quit if there has
   been no connected players for the specified amount of time.
 - When turning on the autotoggle option existing human nations without
   a connected player will be put on AI.
 - Server doesn't block as long when writing to a slow host, controlled
   by variables "tcptimeout" and "netwait".
 - "savename" server variable controls the prefix of autosaves.
 - "allowconnect" server variable lets you control which types of
   players (new players; human players; AI players; dead players;
   barbarian players) can connect.
 - More nations added.
 - New maps in data/scenario: british-isles-80x76-v2.51.sav,
   iberian-peninsula-136x100-v0.9.sav,
   hagworld-120x60-v1.2.sav (earth map).
 - Amiga internationalization/localization.
 - Amiga client: history added to chatline.
 - Lots of bug fixes and code cleanups.

WHAT'S CHANGED SINCE 1.11.0:

 - Readline support added to the server.
 - May now disperse initial units over specified area.  See "dispersion"
   server option.
 - May now arrange for first client to connect to have a higher cmdlevel
   than the following clients.  See "cmdlevel" server option.
 - Save files now transparently (un)compressed when (loaded) saved.
 - Now requires a minimum number of ocean tiles to be adjacent to a land
   tile wished to be transformed into ocean.  Default is 1.
 - Added Nuclear Fallout.  Industrialization and population still
   generate Pollution.  Dropping a Nuke generates Nuclear Fallout, which
   is distinct from Pollution.  There is a new command to clean Fallout
   vs. cleaning Pollution.  Fallout contributes to Nuclear Winter --
   which also changes terrain, but tends to Desert, Tundra and Glacier.
   Added a new "cooling" icon to the info area to indicate the progress
   towards Nuclear Winter, and also an icon for Fallout on the main map.
   AIs are now more aggressive at cleaning up Pollution, but not Fallout.
 - Ported to OpenVMS.
 - Moved most of the dependencies on the "civstyle" server option to
   separate values in game.ruleset files.
 - Fixed bugs in "turns to build" displays.
 - Fixed bug whereby Diplomat/Spy investigations of cities did not
   reveal the correct supported and present unit lists.
 - Fixed multiple bugs in go-to code.
 - Fixed bug where starting a revolution, saving the game and restarting
   the server would allow switching governments without anarchy.
 - Fix bug that you could paradrop into cities you were at war with even
   if they contained enemy units.

WHAT'S CHANGED SINCE 1.10.0:

 - Internationalization extended.  Still needs improvement.
   Current localizations: de en_GB es fr hu ja nl no pl pt pt_BR ru.
 - Added full Fog of War.  Controlled by "fogofwar" server option.
 - Added explicit Diplomatic States between civilizations: war, neutral,
   no-contact, cease-fire peace and alliance.
 - Allow terrain changes to/from land/ocean.  Default ruleset allows
   Engineers to Transform Swamp to Ocean and Ocean to Swamp.  Also
   allows Forest to be Mined into Swamp.
 - Increased maximum number of players to 30.
 - Fortifying now takes a turn to complete (like Civ1/2).
 - Added correct Civ2 style of Apollo wonder (shows entire map, rather
   than just cities).  Selected by "civstyle" server option.
 - Aggressive sea units no longer cause unhappiness when in a city.
 - Added Civ2 rule that firepower is reduced to 1 for both the defender
   and the attacker when a ship bombards a land unit.
 - When changing current research, if user changes back to what was being
   researched, the penalty is not applied (you keep all your bulbs).
 - Added pop-up of more details when clicking on info box in GTK+ client.
 - Improved the global warming danger indicator.
 - Added warning of incipient city growth.
 - The server "remove <player>" command is no longer available after the
   game has started.
 - Added "fixedlength" server option to make all turns exactly "timeout"
   duration.
 - The "timeout" time may be much longer (up to a day).
 - Added goto for air units.  If destination is beyond range, they will
   stop in cities/airfields/etc. to get there.
 - May now select a unit by clicking on the unit pile display on the left.
 - Diplomats/Spies moving by goto now do pop-up the Diplomat/Spy command
   dialog when they reach a city.
 - Improved goto algorithm and implementation.
 - Help dialog displays which buildings an advance will obsolete.
 - Optionally show city food/shields/trade productions on main map.
 - Added server option "autotoggle", which toggles AI status on and off
   as players connect and disconnect.
 - Allow Hoover Dam to be built anywhere, to conform to Civ2.
 - Show turns per advance in Science Advisor dialog.
 - Improved map and unit movement drawing code.
 - Added "End Turn when done moving" local option.
 - City production penalties now applied more correctly.
 - Added Sentry and Fortify to Present Units' City Dialog pop-up.
 - More nations added.
 - Added a resource file for the GTK+ client.
 - Improved network code for more reliable connections.
 - Split nations.ruleset into individual <nation>.ruleset files.
 - Extended registry file format to allow including files and overriding
   entries.
 - Added --with-xaw and --enable-client=xaw3d options to ./configure
   script.
 - Lots of bug fixes and code cleanups.

WHAT'S CHANGED SINCE 1.9.0:

 - Internationalization extended.  Still needs improvement.
   New ./doc directory for localized versions of README and INSTALL.
   Current localizations: de en_GB es fr hu no pl pt pt_BR ru.
 - Added Civ1/2-like Barbarians.  Controlled by two server options.
 - Many more nations added.
 - Worklists -- Players can now specify a list of things to be built in
   a city.
 - The AI now utilizes diplomats/spies aggressively.
 - Added a variant (1) of Michelangelo's Chapel.
 - Initial rates after Revolution will try to maximize Science.
 - Rapture-triggered city growth will no longer empty the foodbox.
 - Map generator improvements:
   - Gen 1 hills more uniform north/south.
   - Gen 2+ will tend to make fewer length-one rivers.
 - Added unit-death explosion animation.
 - No longer will cities with exactly 0 (zero) production surplus be given
   a "free" shield every turn.
 - Command-line arguments made more consistent between server and client.
 - Caravans now provide a research benefit when initially establishing a
   trade route, equal to the monetary benefit.
 - Diplomat/Spy changes:
   - Changed all actions to more closely conform to Civ2 rules.
   - Changed "diplchance" to be %-chance of success.  Used in many ways.
   - Diplomat/Spy may attempt action with any movement left.
   - Added "At Spy's Discretion" selection to steal and sabotage dialogs.
   - Allow Spies to steal tech from a city more than once (gets harder).
   - May only poison towns of size greater than 1 (one).
   - May only sabotage units that are alone on a square.
   - When a city is subverted, only nearby units change sides.
   - Veteran status improves defense against other Diplomats/Spies.
 - Added production display of number of turns remaining to build.
 - Small, shield-like flags tilesets (trident_shields, engels_shields).
 - Airbase changes (for Civ2 compliance):
   - Ground units can attack Air units when they are parked on an Airbase.
   - Units are defeated only singly when on an Airbase (like a Fortress).
 - Revised and improved the server 'help' command.
 - New intro graphics.
 - Ships may now have their movement reduced after a combat in which they
   are damaged.
 - Added display of production values to main map "city tiles" display.
 - Increased the Add-To City size limit to 9 to conform to Civ2.
 - Settler's "Connect" feature -- Automatically connect two points with
   Road, Railroad, Irrigate or Fortress.
 - Several AI improvements.
 - New ruleset support for CITIES, most notably cities are now drawn in
   different sizes and styles.
 - Allow specifying unambiguous player name prefix, instead of full 
   player name, for server commands taking a player name argument.
 - Added multi-client configuration support.
 - Added 'read' and 'write' server commands.
 - Added "best nation" column to Demographics report.
 - Changed Fighters and Stealth Fighters to not cause unhappiness (Civ2).
 - Cities on mountains will produce an extra food (Civ2).
 - Fixed bug where Lighthouse was not producing veteran sea units.
 - Ported to Amiga.  (This is not included with a "distribution"; get it
   directly from the CVS <http://www.freeciv.org/contribute.html#SetupCVS>,
   or from a CVS Snapshot <http://www.freeciv.org/latest.html>.)
 - Lots of bug fixes, code cleanups, and help-text improvements.

WHAT'S CHANGED SINCE 1.8.1:

 - Internationalization added.  Some aspects still need improvement,
   and in particular currently only works well if client and server
   both use the same language.  Initial (partial) translations
   included are: de es fr hu pl pt pt_BR.
 - Improvements to "trident" tileset, and this set is now the default.
 - New ruleset support for NATIONS, and many new nations added --
   there are now 32 nations in the default nations file.  Also allow
   multiple leaders choices for each nation, allow longer player
   names, and allow specifying/choosing sex of leader.
 - New ruleset support for TERRAIN, and changes to allow Civ2 style
   terrain with more specials, multi-terrain rivers, and new farmland
   infrastructure.  Moved some server options into terrain ruleset,
   and added new rule option regarding movement along rivers.
 - New ruleset support for GOVERNMENTS, and more general unit upkeep.
 - The contents and layout of the graphics files is now described in
   user-editable 'spec' files, instead of being hardwired into the
   code; see README.graphics for details.
 - Changes to city graphics: can now see cities grow.  Also, code
   support for different city styles for different nations, and for
   technology dependent styles, but lacking good graphics except for
   the default style.  Cities show a small flag when occupied by one
   or more units.
 - From Civ2, added Paratroopers unit and Airbase infrastructure.
 - Initial window size of Gtk+ client is smaller, to fit better on
   smaller screens, and can now resize detached chat/output window.
 - City report enhancements: New "Change All" dialog to convert all
   production of a given item to some other item (both clients); can
   select multiple cities and issue commands (Gtk+ client only); can
   sort by different columns (Gtk+ client only).  Can also select
   multiple items in the Trade Report (Gtk+ client only).
 - In Civ2 style, can select which infrastructure item to pillage, and
   multiple units can pillage together.  Can now pillage fortresses,
   as well as new farmland and airbase infrastructure items.
 - More fields added to Demographics Report, and now configurable from
   a server option.
 - Added more information to "Active Units Report", and renamed to
   "Military Report".
 - New cursors added for Goto, Nuke and Paradrop modes; <Escape> key
   cancels Go-To/Paradrop/nuke mode; pollution on city squares is now
   always clearly visible; improvements to refresh method for pixmaps
   in Gtk+ client; various other improvements to both clients.
 - Autosettlers no longer convert Plains/Grasslands to Forests, but
   will now Transform terrain.
 - AI can build spaceships, although does not do so very aggressively
   or intelligently yet.
 - Server and client may specify a different metaserver to use.  There
   is now a metaserver running on www.freeciv.org, and that is now the
   default metaserver (http://www.freeciv.org/metaserver).
 - New server option for "turn blocking" mode, in which players will
   never miss a turn even if sometimes disconnected.
 - More flexible rulesets for units and techs (advances) allowing
   variable number of items in each (up to 200) and no longer any
   techs with hardwired effects depending on position in ruleset file.
 - Appropriate helptext now appears in ruleset files, allowing
   modpacks to give modified help to correspond to modified items.
 - Format of ruleset files is changed, no longer compatible with
   those for 1.8.0 and 1.8.1.
 - Removed science bonus of +1 per city per turn.
 - In Civ2 mode, overflight of a hut causes it to disappear, and can't
   build cities next to each other.
 - Rapture_size now 3, to conform to Civ2.
 - Settlers and Engineers now cost shield upkeep as well as food
   upkeep (depending on government type) as in both Civ1 and Civ2.
 - Helicopters do not lose hitpoints when over an airbase.
 - The amount of food required for a city grow is now ((citysize+1) *
   foodbox), instead of (citysize * foodbox); new behavior matches
   both Civ1 and Civ2.
 - Some re-organization of data directory, in adding data/scenario,
   and graphics now in data/trident, data/engels and data/misc.
 - Preliminary support for compiling the server on Mac (but networking
   code not yet ported).
 - Progressively moving code out of client gui-dependent directories.
 - Lots of bug fixes, code cleanups, and help-text improvements.

WHAT'S CHANGED SINCE 1.8.0:

 - The biggest change is a new client which uses the popular Gtk+
   toolkit.  The old Xaw client is still included as well.
 - A new alternate (30x30) tileset: the "trident" tileset.
   Start the client with "--tiles trident" to try it out.
   (This replaces the "classic/brunus" tileset, which is still
   available separately from the Freeciv ftp site.)
 - In the Gtk+ client, parts of the main window can be detached.
   Detaching them all allows a full-screen map window!
 - The data directory can now be specified as a "path" ($FREECIV_PATH),
   instead of a single directory.  By default, ~/.freeciv is now in
   the path, so if you download new tiles, modpacks, etc, you can now
   simply put them in ~/.freeciv
 - Server console improvements: can abbreviate server command names,
   and server option names.  Better prompt handling - eg, server no
   longer prints an unnecessary prompt each turn.
 - Server commands can now be issued from the client chatline.
   There is a new server command "cmdlevel", to control access to
   this feature on a per-player basis.
 - Some of the "small" graphics have been improved.
 - Added a nice cursor for selecting the destination for "goto".
 - New column "corruption" in the city report.
 - Implemented Marco Polo's Embassy wonder.
 - New command to explode Nuclear units at an empty square.
 - Improved behavior of Caravans and "goto": the Caravan dialog will
   now popup when the Caravan arrives, whether moving by goto or by
   the keyboard.
 - Allow connecting to metaserver (in client) via $http_proxy.
 - Minor "Zone of Control" (ZOC) rules changes to better match Civ2.
 - Improved debug logging.
 - Server will refuse to run as root, as a security measure.
 - As always: lots of bug fixes, code cleanups, and help-text
   improvements.

WHAT'S CHANGED SINCE 1.7.2:

 - Space race!  Be the first player to build a space ship and colonize
   Alpha Centauri.
 - Civil war!  Capturing the enemy capital may split his empire.  Half
   of his towns will join a new AI leader!
 - Two tile sets in this release! The "big" ones are from Ralph Engels.
   I hope you will enjoy them.  The second set is an enhancement of the
   classic ones.  The server option -tiles allows to choose a subdirectory
   of the data directory to use different tiles.  (The data directory can
   be set with the DATADIR environment variable.)  Also, diagonal roads
   and railroads are now drawn.  Changed some of the national flags
   (especially changed Soviet flag to the Russian flag for Russians).
 - Rulesets can be used to customise units, advances, wonders, and
   improvements without recompiling -- see README.rulesets.
 - Units in a town can be put in auto-attack mode.  In this mode, they
   will attack any enemy units which come nearby.
 - New city options for each city, controlling whether new citizens are
   workers or specialists, whether to allow disbanding a size 1 city by
   building a settler, and options for units on auto-attack mode in the
   city.
 - Mapgen 4 (selected with the server command "set generator 4") is a new
   map generator which generates a map with 2 players on each island.
 - Cities can be a traded in diplomatic treaties.  This effectively
   allows you to exchange, sell or buy cities.
 - The AI now builds diplomats to defend against enemy diplomats.
 - Settlers and engineers can do teamwork.  With enough manpower, all
   terrain improvements can be done fast!
 - With engineers, the terrain type can be changed using the new Transform
   command ('O').  For example, hills can be turned into plains.
 - Units only regenerate hit points if they have not moved at all for a
   full turn.
 - Gamelog option.  The whole history of a game can be stored in the
   gamelog file.
 - One can select a unit with 0 moves left from the main map.
 - City workers can be adjusted from the main map, with shift+button1.
 - Changed the order of libraries to fix problems on IRIX and Cygwin.
 - Changed the client-server protocol to use network byte order.
 - Other bug fixes and code and help text cleanups.

WHAT'S CHANGED SINCE 1.7.1:

 - Improvements to the configure script, so it should now work
   properly on all systems.
 - Ability to configure to use Xaw3d instead of the normal Xaw.
 - New "easy" AI mode, which is now the default.
 - Improved "City Report" dialog, with configurable columns.
 - Improved "Message Options" dialog, with customizable filtering and
   selection for the different message types.
 - Improved "Messages" dialog, with a scrollbar, and a "Popup City"
   button.
 - Improved multi-column "Unit Select" dialog when there are lots of
   units.
 - In city dialog, middle-click unit icons to "activate and close".
 - New client command "t" to show tiles used by city under mouse.
 - New metaserver dialog to make connecting to metaserver games easier.
 - Ability to save and re-load client settings.
 - Clearer Yes/No toggle buttons in various places in the client.
 - Reduced color usage (but looks almost identical), so the client should
   now work fine on systems with 8 bit color displays.
 - Ability to use "scenarios", that is, pre-designed maps.  Scenarios
   for Earth and Europe games are included in the data directory.
   (Use them with the server command-line option -f, like savegames.)
 - More city names, so you're less likely to see "City 41" etc.
 - Saving large games is now much faster, and the savefile is smaller.
 - Improvements to the documentation and online help system, including
   a new help section on the "Zones of Control" game concept.
 - Detect repeated log messages and print repeat counts.
 - New server options randseed and aqueductloss.
 - Many other enhancements and bug fixes.

WHAT'S CHANGED SINCE 1.7.0:

 - Removed two C++-style comments, the most reported problem with 1.7.0!
 - Tweaks to the configure script for Solaris, although it's still not
   perfect.
 - Spy and diplomat improvements.
 - Added the "W" command, which Wakes up sentried units.
 - Added the "scorelog" command, which can be read by a script to generate
   power graphs after the game.
 - Loading games is now MUCH faster.
 - AI improvements from John Stonebraker.
 - Visual improvements to dialogs and the map screen.
 - Settlers may not fortify.
 - Numerous bug fixes.

WHAT'S CHANGED SINCE 1.6.3:

 - Massive AI changes. It plays smarter, and the AI uses seagoing vessels.
 - The contents of the help system have been improved substantially.
 - Many bugs fixed.
 - We now recommend you use "configure" instead of Imake.
 - Settlers have a new "e"xplore mode.
 - The makefiles now contain auto-generated dependency information.
 - Units within the radius of the city to which they belong no longer
   cause unhappiness.  This will allow much more realistic defense
   scenarios.
 - The tax/lux/sci rates are now limited by your government type.
   Also, they can modified by clicking on the rate status icons.
 - The behaviour of spies and diplomats has been greatly improved.
 - Freeciv now needs to transmit a lot less data (<25%!) when playing
   over the Internet.  This will greatly speed up games.
 - Preliminary support for compiling Freeciv under Windows and OS/2,
   when used in conjunction with the GNU tools.
 - From now on, it will be possible for different Freeciv versions to
   talk to each other if they're able.
 - Tracy Reed (who provides www.freeciv.org) reports that our site is
   getting over 6,000 hits per month.  He was quoted as saying "Cool!
   Now my computer is doing something!"
 - Lots, and Lots, and Lots of donated patches. Thanks guys, keep 'em
   coming!
