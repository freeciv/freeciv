/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

#include "astring.h"
#include "attribute.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "registry.h"
#include "shared.h"
#include "support.h"

#include "civserver.h"
#include "console.h"
#include "gamehand.h"
#include "gamelog.h"
#include "mapgen.h"
#include "meta.h"
#include "plrhand.h"
#include "rulesout.h"
#include "sernet.h"

#include "advmilitary.h"	/* assess_danger_player() */

#include "stdinhand.h"

#define MAX_LEN_CMD MAX_LEN_PACKET
  /* to be used more widely - rp */

extern int gamelog_level;
extern char metaserver_info_line[256];
extern char metaserver_addr[256];
extern int metaserver_port;

enum cmdlevel_id default_access_level = ALLOW_INFO;
enum cmdlevel_id   first_access_level = ALLOW_INFO;

static void cut_player_connection(struct player *caller, char *playername);
static void quit_game(struct player *caller);
static void show_help(struct player *caller, char *arg);
static void show_players(struct player *caller);
static void set_ai_level(struct player *caller, char *name, int level);

static char horiz_line[] =
"------------------------------------------------------------------------------";

/* The following classes determine what can be changed when.
 * Actually, some of them have the same "changeability", but
 * different types are separated here in case they have
 * other uses.
 * Also, SSET_GAME_INIT/SSET_RULES separate the two sections
 * of server settings sent to the client.
 * See the settings[] array for what these correspond to and
 * explanations.
 */
enum sset_class {
  SSET_MAP_SIZE,
  SSET_MAP_GEN,
  SSET_MAP_ADD,
  SSET_PLAYERS,
  SSET_GAME_INIT,
  SSET_RULES,
  SSET_RULES_FLEXIBLE,
  SSET_META,
  SSET_LAST
};

/* Whether settings are sent to the client when the client lists
 * server options; also determines whether clients can set them in principle.
 * Eg, not sent: seeds, saveturns, etc.
 */
enum sset_to_client {
  SSET_TO_CLIENT, SSET_SERVER_ONLY
};

#define SSET_MAX_LEN  16	/* max setting name length (plus nul) */

struct settings_s {
  char *name;
  int *value;
  enum sset_class sclass;
  enum sset_to_client to_client;
  int min_value, max_value, default_value;
  char *short_help;
  char *extra_help;
  /* short_help:
       Sould be less than 42 chars (?), or shorter if the values may
       have more than about 4 digits.   Don't put "." on the end.
     extra_help:
       May be empty string, if short_help is sufficient.
       Need not include embedded newlines (but may, for formatting);
       lines will be wrapped (and indented) automatically.
       Should have punctuation etc, and should end with a "."
  */
  /* The following apply if the setting is string valued; note these
     default to 0 (NULL) if not explicitly mentioned in initialization
     array.  The setting is integer valued if svalue is NULL.
     (Yes, we could use a union here.  But we don't.)
  */
  char *svalue;	
  char *default_svalue;
  size_t sz_svalue;		/* max size we can write into svalue */
};

#define SETTING_IS_INT(s) ((s)->value!=NULL)
#define SETTING_IS_STRING(s) ((s)->value==NULL)

static struct settings_s settings[] = {

  /* These should be grouped by sclass */
  
/* Map size parameters: adjustable if we don't yet have a map */  
  { "xsize", &map.xsize,
    SSET_MAP_SIZE, SSET_TO_CLIENT,
    MAP_MIN_WIDTH, MAP_MAX_WIDTH, MAP_DEFAULT_WIDTH,
    N_("Map width in squares"), "" },
  
  { "ysize", &map.ysize,
    SSET_MAP_SIZE, SSET_TO_CLIENT,
    MAP_MIN_HEIGHT, MAP_MAX_HEIGHT, MAP_DEFAULT_HEIGHT,
    N_("Map height in squares"), "" }, 

/* Map generation parameters: once we have a map these are of historical
 * interest only, and cannot be changed.
 */
  { "generator", &map.generator, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_GENERATOR, MAP_MAX_GENERATOR, MAP_DEFAULT_GENERATOR,
    N_("Method used to generate map"),
    N_("1 = standard, with random continents;\n"
       "2 = equally sized large islands with one player each, and twice that many\n"
       "    smaller islands;\n"
       "3 = equally sized large islands with one player each, and a number of other\n"
       "    islands of similar size;\n"
       "4 = equally sized large islands with two players on every island (or one\n"
       "    with three players for an odd number of players), and additional\n"
       "    smaller islands.\n"
       "Note: values 2,3 and 4 generate \"fairer\" (but more boring) maps.\n"
       "(Zero indicates a scenario map.)") },

  { "landmass", &map.landpercent,
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_LANDMASS, MAP_MAX_LANDMASS, MAP_DEFAULT_LANDMASS,
    N_("Amount of land vs ocean"), "" },

  { "mountains", &map.mountains,
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_MOUNTAINS, MAP_MAX_MOUNTAINS, MAP_DEFAULT_MOUNTAINS,
    N_("Amount of hills/mountains"),
    N_("Small values give flat maps, higher values give more "
       "hills and mountains.")},

  { "rivers", &map.riverlength, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_RIVERS, MAP_MAX_RIVERS, MAP_DEFAULT_RIVERS,
    N_("Amount of river squares"), "" },

  { "grass", &map.grasssize, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_GRASS, MAP_MAX_GRASS, MAP_DEFAULT_GRASS,
    N_("Amount of grass squares"), "" },

  { "forests", &map.forestsize, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_FORESTS, MAP_MAX_FORESTS, MAP_DEFAULT_FORESTS,
    N_("Amount of forest squares"), "" },

  { "swamps", &map.swampsize, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_SWAMPS, MAP_MAX_SWAMPS, MAP_DEFAULT_SWAMPS,
    N_("Amount of swamp squares"), "" },
    
  { "deserts", &map.deserts, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_DESERTS, MAP_MAX_DESERTS, MAP_DEFAULT_DESERTS,
    N_("Amount of desert squares"), "" },

  { "seed", &map.seed, 
    SSET_MAP_GEN, SSET_SERVER_ONLY,
    MAP_MIN_SEED, MAP_MAX_SEED, MAP_DEFAULT_SEED,
    N_("Map generation random seed"),
    N_("The same seed will always produce the same map; "
       "for zero (the default) a seed will be chosen based on the time, "
       "to give a random map.") },

/* Map additional stuff: huts and specials.  randseed also goes here
 * because huts and specials are the first time the randseed gets used (?)
 * These are done when the game starts, so these are historical and
 * fixed after the game has started.
 */
  { "randseed", &game.randseed, 
    SSET_MAP_ADD, SSET_SERVER_ONLY,
    GAME_MIN_RANDSEED, GAME_MAX_RANDSEED, GAME_DEFAULT_RANDSEED,
    N_("General random seed"),
    N_("For zero (the default) a seed will be chosen based on the time.") },

  { "specials", &map.riches, 
    SSET_MAP_ADD, SSET_TO_CLIENT,
    MAP_MIN_RICHES, MAP_MAX_RICHES, MAP_DEFAULT_RICHES,
    N_("Amount of \"special\" resource squares"), 
    N_("Special resources improve the basic terrain type they are on.  " 
       "The server variable's scale is parts per thousand.") },

  { "huts", &map.huts, 
    SSET_MAP_ADD, SSET_TO_CLIENT,
    MAP_MIN_HUTS, MAP_MAX_HUTS, MAP_DEFAULT_HUTS,
    N_("Amount of huts (minor tribe villages)"), "" },

/* Options affecting numbers of players and AI players.  These only
 * affect the start of the game and can not be adjusted after that.
 * (Actually, minplayers does also affect reloads: you can't start a
 * reload game until enough players have connected (or are AI).)
 */
  { "minplayers", &game.min_players,
    SSET_PLAYERS, SSET_TO_CLIENT,
    GAME_MIN_MIN_PLAYERS, GAME_MAX_MIN_PLAYERS, GAME_DEFAULT_MIN_PLAYERS,
    N_("Minimum number of players"),
    N_("There must be at least this many players (connected players or AI's) "
       "before the game can start.") },
  
  { "maxplayers", &game.max_players,
    SSET_PLAYERS, SSET_TO_CLIENT,
    GAME_MIN_MAX_PLAYERS, GAME_MAX_MAX_PLAYERS, GAME_DEFAULT_MAX_PLAYERS,
    N_("Maximum number of players"),
    N_("For new games, the game will start automatically if/when this "
       "number of players are connected or (for AI's) created.") },

  { "aifill", &game.aifill, 
    SSET_PLAYERS, SSET_TO_CLIENT,
    GAME_MIN_AIFILL, GAME_MAX_AIFILL, GAME_DEFAULT_AIFILL,
    N_("Number of players to fill to with AI's"),
    N_("If there are fewer than this many players when the game starts, "
       "extra AI players will be created to increase the total number "
       "of players to the value of this option.") },

/* Game initialization parameters (only affect the first start of the game,
 * and not reloads).  Can not be changed after first start of game.
 */
  { "settlers", &game.settlers,
    SSET_GAME_INIT, SSET_TO_CLIENT,
    GAME_MIN_SETTLERS, GAME_MAX_SETTLERS, GAME_DEFAULT_SETTLERS,
    N_("Number of initial settlers per player"), "" },
    
  { "explorer", &game.explorer,
    SSET_GAME_INIT, SSET_TO_CLIENT,
    GAME_MIN_EXPLORER, GAME_MAX_EXPLORER, GAME_DEFAULT_EXPLORER,
    N_("Number of initial explorers per player"), "" },

  { "gold", &game.gold,
    SSET_GAME_INIT, SSET_TO_CLIENT,
    GAME_MIN_GOLD, GAME_MAX_GOLD, GAME_DEFAULT_GOLD,
    N_("Starting gold per player"), "" },

  { "techlevel", &game.tech, 
    SSET_GAME_INIT, SSET_TO_CLIENT,
    GAME_MIN_TECHLEVEL, GAME_MAX_TECHLEVEL, GAME_DEFAULT_TECHLEVEL,
    N_("Number of initial advances per player"), "" },

/* Various rules: these cannot be changed once the game has started. */
  { "techs", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing techs.ruleset"),
    N_("This should specify a subdirectory of the data directory, "
       "containing a file called \"techs.ruleset\".  "
       "The advances (technologies) present in the game will be "
       "initialized from this file.  "
       "See also README.rulesets."),
    game.ruleset.techs, GAME_DEFAULT_RULESET,
    sizeof(game.ruleset.techs) },

  { "governments", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing governments.ruleset"),
    N_("This should specify a subdirectory of the data directory, "
       "containing a file called \"governments.ruleset\".  "
       "The government types available in the game will be "
       "initialized from this file.  "
       "See also README.rulesets."),
    game.ruleset.governments, GAME_DEFAULT_RULESET,
    sizeof(game.ruleset.governments) },

  { "units", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing units.ruleset"),
    N_("This should specify a subdirectory of the data directory, "
       "containing a file called \"units.ruleset\".  "
       "The unit types present in the game will be "
       "initialized from this file.  "
       "See also README.rulesets."),
    game.ruleset.units, GAME_DEFAULT_RULESET,
    sizeof(game.ruleset.units) },

  { "buildings", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing buildings.ruleset"),
    N_("This should specify a subdirectory of the data directory, "
       "containing a file called \"buildings.ruleset\".  "
       "The building types (City Improvements and Wonders) "
       "in the game will be initialized from this file.  "
       "See also README.rulesets."),
    game.ruleset.buildings, GAME_DEFAULT_RULESET,
    sizeof(game.ruleset.buildings) },

  { "terrain", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing terrain.ruleset"),
    N_("This should specify a subdirectory of the data directory, "
       "containing a file called \"terrain.ruleset\".  "
       "The terrain types present in the game will be "
       "initialized from this file.  "
       "See also README.rulesets."),
    game.ruleset.terrain, GAME_DEFAULT_RULESET,
    sizeof(game.ruleset.terrain) },

  { "nations", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing nations.ruleset"),
    N_("This should specify a subdirectory of the data directory, "
       "containing a file called \"nations.ruleset\".  "
       "The nations present in the game will be "
       "initialized from this file.  "
       "See also README.rulesets."),
    game.ruleset.nations, GAME_DEFAULT_RULESET,
    sizeof(game.ruleset.nations) },

  { "cities", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing cities.ruleset"),
    N_("This should specify a subdirectory of the data directory, "
       "containing a file called \"cities.ruleset\".  "
       "The file is used to initialize city data (such as city style).  "
       "See also README.rulesets."),
    game.ruleset.cities, GAME_DEFAULT_RULESET,
    sizeof(game.ruleset.cities) },

  { "game", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing game.ruleset"),
    N_("This should specify a subdirectory of the data directory, "
       "containing a file called \"game.ruleset\".  "
       "The file is used to initialize some miscellanous game rules.  "
       "See also README.rulesets."),
    game.ruleset.game, GAME_DEFAULT_RULESET,
    sizeof(game.ruleset.game) },

  { "researchspeed", &game.techlevel,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_RESEARCHLEVEL, GAME_MAX_RESEARCHLEVEL, GAME_DEFAULT_RESEARCHLEVEL,
    N_("Points required to gain a new advance"),
    N_("This affects how quickly players can research new technology.") },

  { "techpenalty", &game.techpenalty,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_TECHPENALTY, GAME_MAX_TECHPENALTY, GAME_DEFAULT_TECHPENALTY,
    N_("Percentage penalty when changing tech"),
    N_("If you change your current research technology, and you have "
       "positive research points, you lose this percentage of those "
       "research points.  This does not apply if you have just gained "
       "tech this turn.") },

  { "diplcost", &game.diplcost,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_DIPLCOST, GAME_MAX_DIPLCOST, GAME_DEFAULT_DIPLCOST,
    N_("Penalty when getting tech from treaty"),
    N_("For each advance you gain from a diplomatic treaty, you lose "
       "research points equal to this percentage of the cost to "
       "research an new advance.  "
       "You can end up with negative research points if this is non-zero.") },

  { "conquercost", &game.conquercost,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_CONQUERCOST, GAME_MAX_CONQUERCOST, GAME_DEFAULT_CONQUERCOST,
    N_("Penalty when getting tech from conquering"),
    N_("For each advance you gain by conquering an enemy city, you "
       "lose research points equal to this percentage of the cost "
       "to research an new advance.  "
       "You can end up with negative research points if this is non-zero.") },
  
  { "freecost", &game.freecost,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_FREECOST, GAME_MAX_FREECOST, GAME_DEFAULT_FREECOST,
    N_("Penalty when getting a free tech"),
    N_("For each advance you gain \"for free\" (other than covered by "
       "diplcost or conquercost: specifically, from huts or from the "
       "Great Library), you lose research points equal to this "
       "percentage of the cost to research a new advance.  "
       "You can end up with negative research points if this is non-zero.") },

  { "foodbox", &game.foodbox, 
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_FOODBOX, GAME_MAX_FOODBOX, GAME_DEFAULT_FOODBOX,
    N_("Food required for a city to grow"), "" },

  { "aqueductloss", &game.aqueductloss,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_AQUEDUCTLOSS, GAME_MAX_AQUEDUCTLOSS, GAME_DEFAULT_AQUEDUCTLOSS,
    N_("Percentage food lost when need aqueduct"),
    N_("If a city would expand, but it can't because it needs an Aqueduct "
       "(or Sewer System), it loses this percentage of its foodbox "
       "(or half that amount if it has a Granary).") },
  
  { "unhappysize", &game.unhappysize,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_UNHAPPYSIZE, GAME_MAX_UNHAPPYSIZE, GAME_DEFAULT_UNHAPPYSIZE,
    N_("City size before people become unhappy"),
    N_("Before other adjustments, the first unhappysize citizens in a "
       "city are happy, and subsequent citizens are unhappy.  "
       "See also cityfactor.") },

  { "cityfactor", &game.cityfactor,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_CITYFACTOR, GAME_MAX_CITYFACTOR, GAME_DEFAULT_CITYFACTOR,
    N_("Number of cities for higher unhappiness"),
    N_("When the number of cities a player owns is greater than "
       "cityfactor, one extra citizen is unhappy before other "
       "adjustments; see also unhappysize.  This assumes a "
       "Democracy; for other governments the effect occurs at "
       "smaller numbers of cities.") },

  { "razechance", &game.razechance,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_RAZECHANCE, GAME_MAX_RAZECHANCE, GAME_DEFAULT_RAZECHANCE,
    N_("Chance for conquered building destruction"),
    N_("When a player conquers a city, each City Improvement has this "
       "percentage chance to be destroyed.") },

  { "civstyle", &game.civstyle,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_CIVSTYLE, GAME_MAX_CIVSTYLE, GAME_DEFAULT_CIVSTYLE,
    N_("Style of Civ rules"),
    N_("Sets some basic rules; 1 means style of Civ1, 2 means Civ2.\n"
       "Currently this option affects the following rules:\n"
       "  - Apollo shows whole map in Civ2, only cities in Civ1.\n"
       "See also README.rulesets.") },

  { "occupychance", &game.occupychance,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_OCCUPYCHANCE, GAME_MAX_OCCUPYCHANCE, GAME_DEFAULT_OCCUPYCHANCE,
    N_("Chance of moving into tile after attack"),
    N_("If set to 0, combat is Civ1/2-style (when you attack, you remain in "
       "place).  If set to 100, attacking units will always move into the "
       "tile they attacked if they win the combat (and no enemy units remain "
       "in the tile).  If set to a value between 0 and 100, this will be used "
       "as the percent chance of \"occupying\" territory.") },

  { "killcitizen", &game.killcitizen,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_KILLCITIZEN, GAME_MAX_KILLCITIZEN, GAME_DEFAULT_KILLCITIZEN,
    N_("Reduce city population after attack"),
    N_("This flag indicates if city population is reduced after successful "
       "attack of enemy unit, depending on its movement type (OR-ed):\n"
       "  1 = land\n"
       "  2 = sea\n"
       "  4 = heli\n"
       "  8 = air") },

/* Flexible rules: these can be changed after the game has started.
 *
 * The distinction between "rules" and "flexible rules" is not always
 * clearcut, and some existing cases may be largely historical or
 * accidental.  However some generalizations can be made:
 *
 *   -- Low-level game mechanics should not be flexible (eg, rulesets).
 *   -- Options which would affect the game "state" (city production etc)
 *      should not be flexible (eg, foodbox).
 *   -- Options which are explicitly sent to the client (eg, in
 *      packet_game_info) should probably not be flexible, or at
 *      least need extra care to be flexible.
 */
  { "barbarians", &game.barbarianrate,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_BARBARIANRATE, GAME_MAX_BARBARIANRATE, GAME_DEFAULT_BARBARIANRATE,
    N_("Barbarian appearance frequency"),
    N_("0 - no barbarians \n"
    "1 - barbarians only in huts \n"
    "2 - normal rate of barbarian appearance \n"
    "3 - frequent barbarian uprising \n"
    "4 - raging hordes, lots of barbarians") },

  { "onsetbarbs", &game.onsetbarbarian,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_ONSETBARBARIAN, GAME_MAX_ONSETBARBARIAN, GAME_DEFAULT_ONSETBARBARIAN,
    N_("Barbarian onset year"),
    N_("Barbarians will not appear before this year.") },

  { "fogofwar", &game.fogofwar,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_FOGOFWAR, GAME_MAX_FOGOFWAR, GAME_DEFAULT_FOGOFWAR,
    N_("Whether to enable fog of war"),
    N_("If this is set to 1, only those units and cities within the sightrange "
       "of your own units and cities will be revealed to you. You will not see "
       "new cities or terrain changes in squares not observed.") },

  { "diplchance", &game.diplchance,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_DIPLCHANCE, GAME_MAX_DIPLCHANCE, GAME_DEFAULT_DIPLCHANCE,
    N_("Chance in diplomat/spy contests"),
    N_("A Diplomat or Spy acting against a city which has one or more "
       "defending Diplomats or Spies has a diplchance (percent) chance to "
       "defeat each such defender.  Also, the chance of a Spy returning "
       "from a successful mission is diplchance percent.  (Diplomats never "
       "return.)  Also, a basic chance of success for Diplomats and Spies.  "
       "Defending Spys are generally twice as capable as Diplomats, "
       "veteran units 50% more capable than non-veteran ones.") },

  { "spacerace", &game.spacerace,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_SPACERACE, GAME_MAX_SPACERACE, GAME_DEFAULT_SPACERACE,
    N_("Whether to allow space race"),
    N_("If this option is 1, players can build spaceships.") },

  { "civilwarsize", &game.civilwarsize,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_CIVILWARSIZE, GAME_MAX_CIVILWARSIZE, GAME_DEFAULT_CIVILWARSIZE,
    N_("Minimum number of cities for civil war"),
    N_("A civil war is triggered if a player has at least this many cities "
       "and the player's capital is captured.  If this option is set to "
       "the maximum value, civil wars are turned off altogether.") },

/* Meta options: these don't affect the internal rules of the game, but
 * do affect players.  Also options which only produce extra server
 * "output" and don't affect the actual game.
 * ("endyear" is here, and not RULES_FLEXIBLE, because it doesn't
 * affect what happens in the game, it just determines when the
 * players stop playing and look at the score.)
 */
  { "autotoggle", &game.auto_ai_toggle,
    SSET_META, SSET_TO_CLIENT,
    GAME_MIN_AUTO_AI_TOGGLE, GAME_MAX_AUTO_AI_TOGGLE,
    GAME_DEFAULT_AUTO_AI_TOGGLE,
    N_("Whether AI-status toggles with connection"),
    N_("If this is set to 1, AI status is turned off when a player "
       "connects, and on when a player disconnects.") },

  { "endyear", &game.end_year,
    SSET_META, SSET_TO_CLIENT,
    GAME_MIN_END_YEAR, GAME_MAX_END_YEAR, GAME_DEFAULT_END_YEAR,
    N_("Year the game ends"), "" },

  { "timeout", &game.timeout,
    SSET_META, SSET_TO_CLIENT,
    GAME_MIN_TIMEOUT, GAME_MAX_TIMEOUT, GAME_DEFAULT_TIMEOUT,
    N_("Maximum seconds per turn"),
    N_("If all players have not hit \"Turn Done\" before this time is up, "
       "then the turn ends automatically.  Zero means there is no timeout.") },

  { "turnblock", &game.turnblock,
    SSET_META, SSET_TO_CLIENT,
    0, 1, 0,
    N_("Turn-blocking game play mode"),
    N_("If this is set to 1 the game turn is not advanced until all players "
       "have finished their turn, including disconnected players.") },

  { "fixedlength", &game.fixedlength,
    SSET_META, SSET_TO_CLIENT,
    0, 1, 0,
    N_("Fixed-length turns play mode"),
    N_("If this is set to 1 the game turn will not advance until the timeout "
       "has expired, irrespective of players clicking on \"Turn Done\".") },

  { "demography", NULL,
    SSET_META, SSET_TO_CLIENT,
    0, 0, 0,
    N_("What is in the Demographics report"),
    N_("This should be a string of characters, each of which specifies the "
       "the inclusion of a line of information in the Demographics report.\n"
       "The characters and their meanings are:\n"
       "    N = include Population           P = include Production\n"
       "    A = include Land Area            E = include Economics\n"
       "    S = include Settled Area         M = include Military Service\n"
       "    R = include Research Speed       O = include Pollution\n"
       "    L = include Literacy\n"
       "Additionally, the following characters control whether or not certain "
       "columns are displayed in the report:\n"
       "    q = display \"quantity\" column    r = display \"rank\" column\n"
       "    b = display \"best nation\" column\n"
       "(The order of these characters is not significant, but their case is.)"),
    game.demography, GAME_DEFAULT_DEMOGRAPHY,
    sizeof(game.demography) },

  { "saveturns", &game.save_nturns,
    SSET_META, SSET_SERVER_ONLY,
    0, 200, 10,
    N_("Turns per auto-save"),
    N_("The game will be automatically saved per this number of turns.\n"
       "Zero means never auto-save.") },

  /* Could undef entire option if !HAVE_LIBZ, but this way users get to see
   * what they're missing out on if they didn't compile with zlib?  --dwp
   */
  { "compress", &game.save_compress_level,
    SSET_META, SSET_SERVER_ONLY,
#ifdef HAVE_LIBZ
    GAME_MIN_COMPRESS_LEVEL, GAME_MAX_COMPRESS_LEVEL,
    GAME_DEFAULT_COMPRESS_LEVEL,
#else
    GAME_NO_COMPRESS_LEVEL, GAME_NO_COMPRESS_LEVEL, GAME_NO_COMPRESS_LEVEL,
#endif
    N_("Savegame compression level"),
    N_("If non-zero, saved games will be compressed using zlib (gzip format).  "
       "Larger values will give better compression but take longer.  If the "
       "maximum is zero this server was not compiled to use zlib.") },

  { "scorelog", &game.scorelog,
    SSET_META, SSET_SERVER_ONLY,
    GAME_MIN_SCORELOG, GAME_MAX_SCORELOG, GAME_DEFAULT_SCORELOG,
    N_("Whether to log player statistics"),
    N_("If this is set to 1, player statistics are appended to the file "
       "\"civscore.log\" every turn.  These statistics can be used to "
       "create power graphs after the game.") },

  { "gamelog", &gamelog_level,
    SSET_META, SSET_SERVER_ONLY,
    0, 40, 20,
    N_("Detail level for logging game events"),
    N_("Only applies if the game log feature is enabled "
       "(with the -g command line option).  "
       "Levels: 0=no logging, 20=standard logging, 30=detailed logging, "
       "40=debuging logging.") },

  { NULL, NULL,
    SSET_LAST, SSET_SERVER_ONLY,
    0, 0, 0,
    NULL, NULL }
};

/********************************************************************
Returns whether the specified server setting (option) can currently
be changed.  Does not indicate whether it can be changed by clients or not.
*********************************************************************/
static int sset_is_changeable(int idx)
{
  struct settings_s *op = &settings[idx];

  switch(op->sclass) {
  case SSET_MAP_SIZE:
  case SSET_MAP_GEN:
    /* Only change map options if we don't yet have a map: */
    return map_is_empty();	
  case SSET_MAP_ADD:
  case SSET_PLAYERS:
  case SSET_GAME_INIT:
  case SSET_RULES:
    /* Only change start params and most rules if we don't yet have a map,
     * or if we do have a map but its a scenario one (ie, the game has
     * never actually been started).
     */
    return (map_is_empty() || game.is_new_game);
  case SSET_RULES_FLEXIBLE:
  case SSET_META:
    /* These can always be changed: */
    return 1;
  default:
    freelog(LOG_NORMAL, "Unexpected case %d in %s line %d",
	    op->sclass, __FILE__, __LINE__);
    return 0;
  }
}

/********************************************************************
Returns whether the specified server setting (option) should be
sent to the client.
*********************************************************************/
static int sset_is_to_client(int idx)
{
  return (settings[idx].to_client == SSET_TO_CLIENT);
}

typedef enum {
    PNameOk,
    PNameEmpty,
    PNameTooLong
} PlayerNameStatus;

/**************************************************************************
...
**************************************************************************/
static PlayerNameStatus test_player_name(char* name)
{
  int len = strlen(name);

  if (!len) {
      return PNameEmpty;
  } else if (len > MAX_LEN_NAME-1) {
      return PNameTooLong;
  }

  return PNameOk;
}

/**************************************************************************
  Commands - can be recognised by unique prefix
**************************************************************************/
struct command {
  char *name;              /* name - will be matched by unique prefix   */
  enum cmdlevel_id level;  /* access level required to use the command  */
  char *synopsis;	   /* one or few-line summary of usage */
  char *short_help;	   /* one line (about 70 chars) description */
  char *extra_help;	   /* extra help information; will be line-wrapped */
};

/* Order here is important: for ambiguous abbreviations the first
   match is used.  Arrange order to:
   - allow old commands 's', 'h', 'l', 'q', 'c' to work.
   - reduce harm for ambiguous cases, where "harm" includes inconvenience,
     eg accidently removing a player in a running game.
*/
enum command_id {
  /* old one-letter commands: */
  CMD_START_GAME = 0,
  CMD_HELP,
  CMD_LIST,
  CMD_QUIT,
  CMD_CUT,

  /* completely non-harmful: */
  CMD_EXPLAIN,
  CMD_SHOW,
  CMD_SCORE,
  
  /* mostly non-harmful: */
  CMD_SET,
  CMD_RENAME,
  CMD_META,
  CMD_METAINFO,
  CMD_METASERVER,
  CMD_AITOGGLE,
  CMD_CREATE,
  CMD_EASY,
  CMD_NORMAL,
  CMD_HARD,
  CMD_CMDLEVEL,

  /* potentially harmful: */
  CMD_REMOVE,
  CMD_SAVE,
  CMD_READ_SCRIPT,
  CMD_WRITE_SCRIPT,
  CMD_RULESOUT,

  /* undocumented */
  CMD_LOG,
  CMD_RFCSTYLE,
  CMD_FREESTYLE,
  CMD_CRASH,

  /* pseudo-commands: */
  CMD_NUM,		/* the number of commands - for iterations */
  CMD_UNRECOGNIZED,	/* used as a possible iteration result */
  CMD_AMBIGUOUS		/* used as a possible iteration result */
};

static struct command commands[] = {
  {"start",	ALLOW_CTRL,
   "start",
   N_("Start the game, or restart after loading a savegame."),
   N_("This command starts the game.  When starting a new game, "
      "it should be used after all human players have connected, and "
      "AI players have been created (if required), and any desired "
      "changes to initial server options have been made.  "
      "After 'start', each human player will be able to "
      "choose their nation, and then the game will begin.  "
      "This command is also required after loading a savegame "
      "for the game to recommence.  Once the game is running this command "
      "is no longer available, since it would have no effect.")
  },
  
  {"help",	ALLOW_INFO,
   /* translate <> only */
   N_("help\n"
      "help commands\n"
      "help options\n"
      "help <command-name>\n"
      "help <option-name>"),
   N_("Show help about server commands and server options."),
   N_("With no arguments gives some introductory help.  "
      "With argument \"commands\" or \"options\" gives respectively "
      "a list of all commands or all options.  "
      "Otherwise the argument is taken as a command name or option name, "
      "and help is given for that command or option.  For options, the help "
      "information includes the current and default values for that option.  "
      "The argument may be abbreviated where unambiguous.")
  },
   
  {"list",	ALLOW_INFO,
   "list",
   N_("Show a list of players.")
  },
  {"quit",	ALLOW_HACK,
   "quit",
   N_("Quit the game and shutdown the server.")
  },
  {"cut",	ALLOW_CTRL,
   /* translate <> only */
   N_("cut <player-name>"),
   N_("Cut connection to player.")
  },
  {"explain",	ALLOW_INFO,
   /* translate <> only */
   N_("explain\n"
      "explain <option-name>"),
   N_("Explain server options."),
   N_("The 'explain' command gives a subset of the functionality of 'help', "
      "and is included for backward compatibility.  With no arguments it "
      "gives a list of options (like 'help options'), and with an argument "
      "it gives help for a particular option (like 'help <option-name>').")
  },
  {"show",	ALLOW_INFO,
   /* translate <> only */
   N_("show\n"
      "show <option-name>\n"
      "show <option-prefix>"),
   N_("Show server options."),
   N_("With no arguments, shows all server options (or available options, when "
      "used by clients).  With an argument, show only the named option, "
      "or options with that prefix.")
  },
  {"score",	ALLOW_CTRL,
   "score",
   N_("Show current scores."),
   N_("For each connected client, pops up a window showing the current "
      "player scores.")
  },
  {"set",	ALLOW_CTRL,
   N_("set <option-name> <value>"),
   N_("Set server options.")
  },
  {"rename",	ALLOW_CTRL,
   NULL,
   N_("This command is not currently implemented."),
  },
  {"meta",	ALLOW_HACK,
   "meta u|up\n"
   "meta d|down\n"
   "meta ?",
   N_("Control metaserver connection."),
   N_("'meta ?' reports on the status of the connection to metaserver..\n"
      "'meta d' or 'meta down' brings the metaserver connection down.\n"
      "'meta u' or 'meta up' brings the metaserver connection up.")
  },
  {"metainfo",	ALLOW_CTRL,
   /* translate <> only */
   N_("metainfo <meta-line>"),
   N_("Set metaserver info line.")
  },
  {"metaserver",ALLOW_HACK,
   /* translate <> only */
   N_("metaserver <address>"),
   N_("Set address for metaserver to report to.")
  },
  {"aitoggle",	ALLOW_CTRL,
   /* translate <> only */
   N_("aitoggle <player-name>"),
   N_("Toggle AI status of player.")
  },
  {"create",	ALLOW_CTRL,
   /* translate <> only */
   N_("create <player-name>"),
   N_("Create an AI player with a given name."),
   N_("The 'create' command is only available before the game has "
      "been started.")
  },
  {"easy",	ALLOW_CTRL,
   /* translate <> only */
   N_("easy\n"
      "easy <player-name>"),
   N_("Set one or all AI players to 'easy'."),
   N_("With no arguments, sets all AI players to skill level 'easy', and "
      "sets the default level for any new AI players to 'easy'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"normal",	ALLOW_CTRL,
   /* translate <> only */
   N_("normal\n"
      "normal <player-name>"),
   N_("Set one or all AI players to 'normal'."),
   N_("With no arguments, sets all AI players to skill level 'normal', and "
      "sets the default level for any new AI players to 'normal'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"hard",	ALLOW_CTRL,
   /* translate <> only */
   N_("hard\n"
      "hard <player-name>"),
   N_("Set one or all AI players to 'hard'."),
   N_("With no arguments, sets all AI players to skill level 'hard', and "
      "sets the default level for any new AI players to 'hard'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"cmdlevel",	ALLOW_HACK,  /* confusing to leave this at ALLOW_CTRL */
   /* translate <> only */
   N_("cmdlevel\n"
      "cmdlevel <level>\n"
      "cmdlevel <level> new\n"
      "cmdlevel <level> first\n"
      "cmdlevel <level> <player-name>"),
   N_("Query or set command-level access."),
   N_("The command-level controls which server commands are available to "
      "players via the client chatline.  The available levels are:\n"
      "    none  -  no commands\n"
      "    info  -  informational commands only\n"
      "    ctrl  -  commands that affect the game and users\n"
      "    hack  -  *all* commands - dangerous!\n"
      "With no arguments, the current command-levels are reported.\n"
      "With a single argument, the level is set for all existing "
      "connections, and the default is set for future connections.\n"
      "If 'new' is specified, the level is set for newly connecting players.\n"
      "If 'first' is specified, the level is set for the first player connected.\n"
      "If a player name is specified, the level is set for that player only.\n"
      "Command-levels do not persist if a player disconnects, "
      "because some untrusted person could reconnect as that player.\n"
      "If the first player to connect disconnects, then the next player "
      "to connect receives 'first' status.")
  },
  {"remove",	ALLOW_CTRL,
   /* translate <> only */
   N_("remove <player-name>"),
   N_("Fully remove player from game."),
   N_("This *completely* removes a player from the game, including "
      "all cities and units etc.  Use with care!")
  },
  {"save",	ALLOW_HACK,
   /* translate <> only */
   N_("save <file-name>"),
   N_("Save game to file."),
   N_("Save the current game to a file.  Currently there may be some problems "
      "with saving the game before it has been started.  "
      "To reload a savegame created by 'save', "
      "start the server with the command-line argument:\n"
      "    --file <filename>\n"
      "and use the 'start' command once players have reconnected.")
  },
  {"read",	ALLOW_HACK,
   /* translate <> only */
   N_("read <file-name>"),
   N_("Process server commands from file.")
  },
  {"write",	ALLOW_HACK,
   /* translate <> only */
   N_("write <file-name>"),
   N_("Write current settings as server commands to file.")
  },
  {"rulesout",	ALLOW_HACK,
   /* translate <> only */
   N_("rulesout <rules-type> <file-name>"),
   N_("Write selected rules information to file."),
   N_("Write a file with information from currently loaded ruleset data.  "
      "Requires that the ruleset data has been loaded (e.g., after 'start').  "
      "Currently the only option for <rules-type> is 'techs', which writes "
      "information about all the advances (technologies).  This can be used "
      "by the 'techtree' utility program to produce a graph of the advances.")
  },
  {"log",	ALLOW_HACK,
   /* translate <> only */
   N_("log <message>"),
   N_("Generate a log message."),
   N_("Generates a 'log message' with level 1.  "
      "This is mostly useful for debugging the logging system.")
  },
  {"rfcstyle",	ALLOW_HACK,
   "rfcstyle",
   N_("Change server output style to 'RFC-style'.")
  },
  {"freestyle",	ALLOW_HACK,
   "freestyle",
   N_("Change server output style to normal style.")
  },
  {"crash",	ALLOW_HACK,
   "crash",
   N_("Abort the server and generate a 'core' file."),
   N_("This is mostly useful for debugging purposes.")
  }
};

static const char *cmdname_accessor(int i) {
  return commands[i].name;
}
/**************************************************************************
  Convert a named command into an id.
  If accept_ambiguity is true, return the first command in the
  enum list which matches, else return CMD_AMBIGOUS on ambiguity.
  (This is a trick to allow ambiguity to be handled in a flexible way
  without importing notify_player() messages inside this routine - rp)
**************************************************************************/
static enum command_id command_named(const char *token, int accept_ambiguity)
{
  enum m_pre_result result;
  int ind;

  result = match_prefix(cmdname_accessor, CMD_NUM, 0,
			mystrncasecmp, token, &ind);

  if (result < M_PRE_AMBIGUOUS) {
    return ind;
  } else if (result == M_PRE_AMBIGUOUS) {
    return accept_ambiguity ? ind : CMD_AMBIGUOUS;
  } else {
    return CMD_UNRECOGNIZED;
  }
}

/**************************************************************************
  Return current command access level for this player, or default if the
  player is not connected.
**************************************************************************/
static enum cmdlevel_id access_level(struct player *pplayer)
{
  if (pplayer->conn) {
    return pplayer->conn->access_level;
  } else {
    return default_access_level;
  }
}

/**************************************************************************
  Whether the player can use the specified command.
  pplayer == NULL means console.
**************************************************************************/
static int may_use(struct player *pplayer, enum command_id cmd)
{
  if (pplayer == NULL) {
    return 1;  /* on the console, everything is allowed */
  }
  return (access_level(pplayer) >= commands[cmd].level);
}

/**************************************************************************
  Whether the player cannot use any commands at all.
  pplayer == NULL means console.
**************************************************************************/
static int may_use_nothing(struct player *pplayer)
{
  if (pplayer == NULL) {
    return 0;  /* on the console, everything is allowed */
  }
  return (access_level(pplayer) == ALLOW_NONE);
}

/**************************************************************************
  Whether the player can set the specified option (assuming that
  the state of the game would allow changing the option at all).
  pplayer == NULL means console.
**************************************************************************/
static int may_set_option(struct player *pplayer, int option_idx)
{
  if (pplayer == NULL) {
    return 1;  /* on the console, everything is allowed */
  } else {
    int level = access_level(pplayer);
    return ((level == ALLOW_HACK)
	    || (level == ALLOW_CTRL && sset_is_to_client(option_idx)));
  }
}

/**************************************************************************
  Whether the player can set the specified option, taking into account
  both access and the game state.  pplayer == NULL means console.
**************************************************************************/
static int may_set_option_now(struct player *pplayer, int option_idx)
{
  return (may_set_option(pplayer, option_idx)
	  && sset_is_changeable(option_idx));
}

/**************************************************************************
  Whether the player can SEE the specified option.
  pplayer == NULL means console, which can see all.
  client players can see "to client" options, or if player
  has command level to change option.
**************************************************************************/
static int may_view_option(struct player *pplayer, int option_idx)
{
  if (pplayer == NULL) {
    return 1;  /* on the console, everything is allowed */
  } else {
    return sset_is_to_client(option_idx)
      || may_set_option(pplayer, option_idx);
  }
}

/**************************************************************************
  feedback related to server commands
  caller == NULL means console.
  No longer duplicate all output to console.
  'console_id' should be one of the C_ identifiers in console.h

  This lowlevel function takes a single line; prefix is prepended to line.
**************************************************************************/
static void cmd_reply_line(enum command_id cmd, const struct player *caller,
			   int console_id, const char *prefix, const char *line)
{
  char *cmdname = cmd < CMD_NUM ? commands[cmd].name :
                  cmd == CMD_AMBIGUOUS ? _("(ambiguous)") :
                  cmd == CMD_UNRECOGNIZED ? _("(unknown)") :
			"(?!?)";  /* this case is a bug! */

  if (caller) {
    notify_player(caller, "/%s: %s%s", cmdname, prefix, line);
    /* cc: to the console - testing has proved it's too verbose - rp
    con_write(console_id, "%s/%s: %s%s", caller->name, cmdname, prefix, line);
    */
  } else {
    con_write(console_id, "%s%s", prefix, line);
  }
}
/**************************************************************************
  va_list version which allow embedded newlines, and each line is sent
  separately. 'prefix' is prepended to every line _after_ the first line.
**************************************************************************/
static void vcmd_reply_prefix(enum command_id cmd, const struct player *caller,
			      int console_id, const char *prefix,
			      const char *format, va_list ap)
{
  char buf[4096];
  char *c0, *c1;

  my_vsnprintf(buf, sizeof(buf), format, ap);

  c0 = buf;
  while ((c1=strstr(c0, "\n"))) {
    *c1 = '\0';
    cmd_reply_line(cmd, caller, console_id, (c0==buf?"":prefix), c0);
    c0 = c1+1;
  }
  cmd_reply_line(cmd, caller, console_id, (c0==buf?"":prefix), c0);
}
/**************************************************************************
  var-args version of above
  duplicate declaration required for attribute to work...
**************************************************************************/
static void cmd_reply_prefix(enum command_id cmd, const struct player *caller,
			     int console_id, const char *prefix,
			     const char *format, ...)
     fc__attribute((format (printf, 5, 6)));
static void cmd_reply_prefix(enum command_id cmd, const struct player *caller,
			     int console_id, const char *prefix,
			     const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  vcmd_reply_prefix(cmd, caller, console_id, prefix, format, ap);
  va_end(ap);
}
/**************************************************************************
  var-args version as above, no prefix
**************************************************************************/
static void cmd_reply(enum command_id cmd, const struct player *caller,
		      int console_id, const char *format, ...)
     fc__attribute((format (printf, 4, 5)));
static void cmd_reply(enum command_id cmd, const struct player *caller,
		      int console_id, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  vcmd_reply_prefix(cmd, caller, console_id, "", format, ap);
  va_end(ap);
}

/**************************************************************************
...
**************************************************************************/
static void cmd_reply_no_such_player(enum command_id cmd,
				     struct player *caller,
				     char *name,
				     enum m_pre_result match_result)
{
  switch(match_result) {
  case M_PRE_EMPTY:
    cmd_reply(cmd, caller, C_SYNTAX,
	      _("Name is empty, so cannot be a player."));
    break;
  case M_PRE_LONG:
    cmd_reply(cmd, caller, C_SYNTAX,
	      _("Name is too long, so cannot be a player."));
    break;
  case M_PRE_AMBIGUOUS:
    cmd_reply(cmd, caller, C_FAIL,
	      _("Player name prefix '%s' is ambiguous."), name);
    break;
  case M_PRE_FAIL:
    cmd_reply(cmd, caller, C_FAIL,
	      _("No player by the name of '%s'."), name);
    break;
  default:
    cmd_reply(cmd, caller, C_FAIL,
	      _("Unexpected match_result %d (%s) for '%s'."),
	      match_result, _(m_pre_description(match_result)), name);
    freelog(LOG_NORMAL,
	    "Unexpected match_result %d (%s) for '%s'.",
	    match_result, m_pre_description(match_result), name);
  }
}

/**************************************************************************
...
**************************************************************************/
static void open_metaserver_connection(struct player *caller)
{
  server_open_udp();
  if (send_server_info_to_metaserver(1,0)) {
    notify_player(0, _("Open metaserver connection to [%s]."),
		  meta_addr_port());
    cmd_reply(CMD_META, caller, C_OK,
	      _("Metaserver connection opened."));
  }
}

/**************************************************************************
...
**************************************************************************/
static void close_metaserver_connection(struct player *caller)
{
  if (send_server_info_to_metaserver(1,1)) {
    server_close_udp();
    notify_player(0, _("Close metaserver connection to [%s]."),
		  meta_addr_port());
    cmd_reply(CMD_META, caller, C_OK,
	      _("Metaserver connection closed."));
  }
}

/**************************************************************************
...
**************************************************************************/
static void meta_command(struct player *caller, char *arg)
{
  if ((*arg == '\0') ||
      (0 == strcmp (arg, "?"))) {
    if (server_is_open) {
      cmd_reply(CMD_META, caller, C_OK,
		_("Metaserver connection is open."));
    } else {
      cmd_reply(CMD_META, caller, C_OK,
		_("Metaserver connection is closed."));
    }
  } else if ((0 == mystrcasecmp(arg, "u")) ||
	     (0 == mystrcasecmp(arg, "up"))) {
    if (!server_is_open) {
      open_metaserver_connection(caller);
    } else {
      cmd_reply(CMD_META, caller, C_METAERROR,
		_("Metaserver connection is already open."));
    }
  } else if ((0 == mystrcasecmp(arg, "d")) ||
	     (0 == mystrcasecmp(arg, "down"))) {
    if (server_is_open) {
      close_metaserver_connection(caller);
    } else {
      cmd_reply(CMD_META, caller, C_METAERROR,
		_("Metaserver connection is already closed."));
    }
  } else {
    cmd_reply(CMD_META, caller, C_METAERROR,
	      _("Argument must be 'u', 'up', 'd', 'down', or '?'."));
  }
}

/**************************************************************************
...
**************************************************************************/
static void metainfo_command(struct player *caller, char *arg)
{
  mystrlcpy(metaserver_info_line, arg, 256);
  if (send_server_info_to_metaserver(1,0) == 0) {
    cmd_reply(CMD_META, caller, C_METAERROR,
	      _("Not reporting to the metaserver."));
  } else {
    notify_player(0, _("Metaserver infostring set to '%s'."),
		  metaserver_info_line);
    cmd_reply(CMD_META, caller, C_OK,
	      _("Metaserver info string set."));
  }
}

/**************************************************************************
...
**************************************************************************/
static void metaserver_command(struct player *caller, char *arg)
{
  close_metaserver_connection(caller);

  mystrlcpy(metaserver_addr, arg, 256);
  meta_addr_split();

  notify_player(0, _("Metaserver is now [%s]."),
		meta_addr_port());
  cmd_reply(CMD_META, caller, C_OK,
	    _("Metaserver address set."));
}

/***************************************************************
 This could be in common/player if the client ever gets
 told the ai player skill levels.
***************************************************************/
static const char *name_of_skill_level(int level)
{
  const char *nm[11] = { "UNUSED", "UNKNOWN", "UNKNOWN", "easy",
			 "UNKNOWN", "normal", "UNKNOWN", "hard",
			 "UNKNOWN", "UNKNOWN", "UNKNOWN" };
  
  assert(level>0 && level<=10);
  return nm[level];
}

/***************************************************************
...
***************************************************************/
static int handicap_of_skill_level(int level)
{
  int h[11] = { -1, 
		H_NONE, 
		H_NONE, 
		H_RATES | H_TARGETS | H_HUTS,
		H_NONE, 
		H_RATES | H_TARGETS | H_HUTS, 
		H_NONE, 
		H_NONE, 
		H_NONE, 
		H_NONE, 
		H_NONE, 
		};
  
  assert(level>0 && level<=10);
  return h[level];
}

/**************************************************************************
Return the AI fuzziness (0 to 1000) corresponding to a given skill
level (1 to 10).  See ai_fuzzy() in common/player.c
**************************************************************************/
static int fuzzy_of_skill_level(int level)
{
  int f[11] = { -1, 0, 0, 300/*easy*/, 0, 0, 0, 0, 0, 0, 0 };
  
  assert(level>0 && level<=10);
  return f[level];
}

/**************************************************************************
Return the AI expansion tendency, a percentage factor to value new cities,
compared to defaults.  0 means _never_ build new cities, > 100 means to
(over?)value them even more than the default (already expansionistic) AI.
**************************************************************************/
static int expansionism_of_skill_level(int level)
{
  int x[11] = { -1, 100, 100, 30/*easy*/, 100, 100, 100, 100, 100, 100, 100 };
  
  assert(level>0 && level<=10);
  return x[level];
}

/**************************************************************************
For command "save foo";
Save the game, with filename=arg, provided server state is ok.
**************************************************************************/
static void save_command(struct player *caller, char *arg)
{
  if (server_state==SELECT_RACES_STATE) {
    cmd_reply(CMD_SAVE, caller, C_SYNTAX,
	      _("The game cannot be saved before it is started."));
    return;
  }
  save_game(arg);
}

/**************************************************************************
...
**************************************************************************/
void toggle_ai_player_direct(struct player *caller, struct player *pplayer)
{
  assert(pplayer);
  if (is_barbarian(pplayer)) {
    cmd_reply(CMD_AITOGGLE, caller, C_FAIL,
	      _("Cannot toggle a barbarian player."));
    return;
  }

  pplayer->ai.control = !pplayer->ai.control;
  if (pplayer->ai.control) {
    notify_player(0, _("Game: %s is now AI-controlled."), pplayer->name);
    cmd_reply(CMD_AITOGGLE, caller, C_OK,
	      _("%s is now under AI control."), pplayer->name);
    if (pplayer->ai.skill_level==0) {
      pplayer->ai.skill_level = game.skill_level;
    }
    /* Set the skill level explicitly, because eg: the player skill
       level could have been set as AI, then toggled, then saved,
       then reloaded. */ 
    set_ai_level(caller, pplayer->name, pplayer->ai.skill_level);
    /* The following is sometimes necessary to avoid using
       uninitialized data... */
    assess_danger_player(pplayer);
    neutralize_ai_player(pplayer);
  } else {
    notify_player(0, _("Game: %s is now human."), pplayer->name);
    cmd_reply(CMD_AITOGGLE, caller, C_OK,
	      _("%s is now under human control."), pplayer->name);

    /* because the hard AI `cheats' with government rates but humans shouldn't */
    check_player_government_rates(pplayer);
  }
  send_player_info(pplayer,0);
}

/**************************************************************************
...
**************************************************************************/
static void toggle_ai_player(struct player *caller, char *arg)
{
  enum m_pre_result match_result;
  struct player *pplayer;

  pplayer = find_player_by_name_prefix(arg, &match_result);

  if (!pplayer) {
    cmd_reply_no_such_player(CMD_AITOGGLE, caller, arg, match_result);
    return;
  }
  toggle_ai_player_direct(caller, pplayer);
}

/**************************************************************************
...
**************************************************************************/
static void create_ai_player(struct player *caller, char *arg)
{
  struct player *pplayer;
  PlayerNameStatus PNameStatus;
   
  if (server_state!=PRE_GAME_STATE)
  {
    cmd_reply(CMD_CREATE, caller, C_SYNTAX,
	      _("Can't add AI players once the game has begun."));
    return;
  }

  if (game.nplayers==game.max_players) 
  {
    cmd_reply(CMD_CREATE, caller, C_FAIL,
	      _("Can't add more players, server is full."));
    return;
  }

  if ((PNameStatus = test_player_name(arg)) == PNameEmpty)
  {
    cmd_reply(CMD_CREATE, caller, C_SYNTAX, _("Can't use an empty name."));
    return;
  }

  if (PNameStatus == PNameTooLong)
  {
    cmd_reply(CMD_CREATE, caller, C_SYNTAX,
	      _("That name exceeds the maximum of %d chars."), MAX_LEN_NAME-1);
    return;
  }

  if ((pplayer=find_player_by_name(arg)))
  {
    cmd_reply(CMD_CREATE, caller, C_BOUNCE,
	      _("A player already exists by that name."));
    return;
  }

  accept_new_player(arg, NULL);
  pplayer = find_player_by_name(arg);
  if (!pplayer)
  {
    cmd_reply(CMD_CREATE, caller, C_FAIL,
	      _("Error creating new ai player: %s."), arg);
    return;
  }

  pplayer->ai.control = 1;
  set_ai_level_directer(pplayer, game.skill_level);
  cmd_reply(CMD_CREATE, caller, C_OK,
	    _("Created new AI player: %s."), pplayer->name);
}


/**************************************************************************
...
**************************************************************************/
static void remove_player(struct player *caller, char *arg)
{
  enum m_pre_result match_result;
  struct player *pplayer;
  char name[MAX_LEN_NAME];

  pplayer=find_player_by_name_prefix(arg, &match_result);
  
  if(!pplayer) {
    cmd_reply_no_such_player(CMD_REMOVE, caller, arg, match_result);
    return;
  }

  if (!(game.is_new_game && (server_state==PRE_GAME_STATE ||
			     server_state==SELECT_RACES_STATE))) {
    cmd_reply(CMD_REMOVE, caller, C_FAIL,
	      _("Players cannot be removed once the game has started"));
    return;
  }

  sz_strlcpy(name, pplayer->name);
  server_remove_player(pplayer);
  cmd_reply(CMD_REMOVE, caller, C_OK,
	    _("Removed player %s from the game."), name);
}

/**************************************************************************
...
**************************************************************************/
static void generic_not_implemented(struct player *caller, char *cmd)
{
  cmd_reply(CMD_RENAME, caller, C_FAIL,
	    _("Sorry, the '%s' command is not implemented yet."), cmd);
}

/**************************************************************************
...
**************************************************************************/
static void rename_player(struct player *caller, char *arg)
{
  generic_not_implemented(caller, "rename");
}

/**************************************************************************
...
**************************************************************************/
void read_init_script(char *script_filename)
{
  FILE *script_file;
  char buffer[512];

  script_file = fopen(script_filename,"r");

  if (script_file) {

    /* the size 511 is set as to not overflow buffer in handle_stdin_input */
    while(fgets(buffer,511,script_file))
      handle_stdin_input((struct player *)NULL, buffer);
    fclose(script_file);

  } else {
    freelog(LOG_NORMAL,
	_("Could not read script file '%s'."), script_filename);
  }
}

/**************************************************************************
...
**************************************************************************/
static void read_command(struct player *caller, char *arg)
{
  /* warning: there is no recursion check! */
  read_init_script(arg);
}

/**************************************************************************
...
**************************************************************************/
static void write_init_script(char *script_filename)
{
  FILE *script_file;

  script_file = fopen(script_filename,"w");

  if (script_file) {

    int i;

    fprintf(script_file,
	"#FREECIV SERVER COMMAND FILE, version %s\n", VERSION_STRING);
    fputs("# These are server options saved from a running civserver.\n",
	script_file);

    /* first, some state info from commands (we can't save everything) */

    fprintf(script_file, "cmdlevel %s new\n",
	cmdlevel_name(default_access_level));

    fprintf(script_file, "cmdlevel %s first\n",
	cmdlevel_name(first_access_level));

    fprintf(script_file, "%s\n",
	(game.skill_level <= 3) ?	"easy" :
	(game.skill_level >= 6) ?	"hard" :
					"normal");

    if (*metaserver_addr &&
	((0 != strcmp(metaserver_addr, DEFAULT_META_SERVER_ADDR)) ||
	 (metaserver_port != DEFAULT_META_SERVER_PORT))) {
      fprintf(script_file, "metaserver %s\n", meta_addr_port());
    }

    if (*metaserver_info_line &&
	(0 != strcmp(metaserver_info_line,
		     DEFAULT_META_SERVER_INFO_STRING))) {
      fprintf(script_file, "metainfo %s\n", metaserver_info_line);
    }

    /* then, the 'set' option settings */

    for (i=0;settings[i].name;i++) {
      struct settings_s *op = &settings[i];

      if (SETTING_IS_INT(op)) {
        fprintf(script_file, "set %s %i\n", op->name, *op->value);
      } else {
        fprintf(script_file, "set %s %s\n", op->name, op->svalue);
      }
    }

    fclose(script_file);

  } else {
    freelog(LOG_NORMAL,
	_("Could not write script file '%s'."), script_filename);
  }
}

/**************************************************************************
...
**************************************************************************/
static void write_command(struct player *caller, char *arg)
{
  write_init_script(arg);
}

enum rulesout_type { RULESOUT_TECHS, RULESOUT_NUM };
static const char * const rulesout_names[] = { "techs" };
static const char *rulesout_accessor(int i) {
  return rulesout_names[i];
}

/**************************************************************************
  Output rules information.  arg must be form "rules_type filename"
  Only rules currently available is "techs".
  Modifies string pointed to by arg.
**************************************************************************/
static void rulesout_command(struct player *caller, char *arg)
{
  char *rules, *filename, *s;	/* all point into arg string */
  enum m_pre_result result;
  int ind;
  
  if (game.num_tech_types==0) {
    cmd_reply(CMD_RULESOUT, caller, C_FAIL,
	      _("Cannot output rules: ruleset data not loaded "
		"(e.g., use '%sstart')."), (caller?"/":""));
    return;
  }

  /* Find space-delimited rules type and filename: */
  s = skip_leading_spaces(arg);
  if (*s == '\0') goto usage;
  rules = s;

  while (*s && !isspace(*s)) s++;
  if (*s == '\0') goto usage;
  *s = '\0';			/* terminate rules */
  
  s = skip_leading_spaces(s+1);
  if (*s == '\0') goto usage;
  filename = s;
  remove_trailing_spaces(filename);

  result = match_prefix(rulesout_accessor, RULESOUT_NUM, 0,
			mystrncasecmp, rules, &ind);

  if (result > M_PRE_ONLY) {
    cmd_reply(CMD_RULESOUT, caller, C_FAIL,
	      _("Bad rulesout type: '%s' (%s).  Try '%shelp rulesout'."),
	      rules, _(m_pre_description(result)), (caller?"/":""));
    return;
  }

  switch(ind) {
  case RULESOUT_TECHS:
    if (rulesout_techs(filename)) {
      cmd_reply(CMD_RULESOUT, caller, C_OK,
		_("Saved techs rules data to '%s'."), filename);
    } else {
      cmd_reply(CMD_RULESOUT, caller, C_FAIL,
		_("Failed saving techs rules data to '%s'."), filename);
    }
    return;
  default:
    cmd_reply(CMD_RULESOUT, caller, C_FAIL,
	      "Internal error: ind %d in rulesout_command", ind);
    freelog(LOG_NORMAL, "Internal error: ind %d in rulesout_command", ind);
    return;
  }
  
 usage:
  cmd_reply(CMD_RULESOUT, caller, C_FAIL,
	    _("Usage: rulesout <ruletype> <filename>.  Try '%shelp rulesout'."),
	      (caller?"/":""));
}

/**************************************************************************
 set pplayer's cmdlevel to level if caller is allowed to do so 
**************************************************************************/
static int set_cmdlevel(struct player *caller, struct player *pplayer,
			enum cmdlevel_id level)
{
  assert(pplayer && pplayer->conn);
    /* only ever call me for specific, connected players */

  if (caller && access_level(pplayer) > access_level(caller)) {
    /* Can this happen?  Caller must already have ALLOW_HACK
       to even use the cmdlevel command.  Hmm, someone with
       ALLOW_HACK can take away ALLOW_HACK from others... --dwp
    */
    cmd_reply(CMD_CMDLEVEL, caller, C_FAIL,
	      _("Cannot decrease command level '%s' for player '%s';"
		" you only have '%s'."),
	      cmdlevel_name(access_level(pplayer)),
	      pplayer->name,
	      cmdlevel_name(access_level(caller)));
    return 0;
  } else {
    pplayer->conn->access_level = level;
    notify_player(pplayer, _("Game: You now have access level '%s'."),
		  cmdlevel_name(level));
    return 1;
  }
}

/**************************************************************************
 Change command level for individual player, or all, or new.
**************************************************************************/
static void cmdlevel_command(struct player *caller, char *str)
{
  char arg_level[MAX_LEN_CMD+1]; /* info, ctrl etc */
  char arg_name[MAX_LEN_CMD+1];	 /* a player name, or "new" */
  char *cptr_s, *cptr_d;	 /* used for string ops */

  enum m_pre_result match_result;
  enum cmdlevel_id level;
  struct player *pplayer;

  /* find the start of the level: */
  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);

  /* copy the level into arg_level[] */
  for(cptr_d=arg_level; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++) {
    *cptr_d=*cptr_s;
  }
  *cptr_d='\0';
  
  if (!arg_level[0]) {
    /* no level name supplied; list the levels */
    int i;

    cmd_reply(CMD_CMDLEVEL, caller, C_COMMENT, _("Command levels in effect:"));

    for (i = 0; i < game.nplayers; ++i) {
      struct player *pplayer = &game.players[i];
      if (pplayer->conn) {
	cmd_reply(CMD_CMDLEVEL, caller, C_COMMENT,
		  "cmdlevel %s %s",
		  cmdlevel_name(access_level(pplayer)), pplayer->name);
      } else {
	cmd_reply(CMD_CMDLEVEL, caller, C_COMMENT,
		  "cmdlevel %s %s (not connected)",
		  cmdlevel_name(ALLOW_NONE), pplayer->name);
      }
    }
    cmd_reply(CMD_CMDLEVEL, caller, C_COMMENT,
	      _("Command level for new connections: %s"),
	      cmdlevel_name(default_access_level));
    cmd_reply(CMD_CMDLEVEL, caller, C_COMMENT,
	      _("Command level for first connections: %s"),
	      cmdlevel_name(first_access_level));
    return;
  }

  /* a level name was supplied; set the level */

  if ((level = cmdlevel_named(arg_level)) == ALLOW_UNRECOGNIZED) {
    cmd_reply(CMD_CMDLEVEL, caller, C_SYNTAX,
	      _("Error: command level must be one of"
		" 'none', 'info', 'ctrl', or 'hack'."));
    return;
  } else if (caller && level > access_level(caller)) {
    cmd_reply(CMD_CMDLEVEL, caller, C_FAIL,
	      _("Cannot increase command level to '%s';"
		" you only have '%s' yourself."),
	      arg_level, cmdlevel_name(access_level(caller)));
    return;
  }

  /* find the start of the name: */
  for(; *cptr_s && !isalnum(*cptr_s); cptr_s++);

  /* copy the name into arg_name[] */
  for(cptr_d=arg_name;
      *cptr_s && (*cptr_s == '-' || *cptr_s == ' ' || isalnum(*cptr_s));
      cptr_s++ , cptr_d++) {
    *cptr_d=*cptr_s;
  }
  *cptr_d='\0';
 
  if (!arg_name[0]) {
    /* no playername supplied: set for all connected players,
     * and set the default
     */
    int i;
    for (i = 0; i < game.nplayers; ++i) {
      struct player *pplayer = &game.players[i];
      if (pplayer->conn) {
	if (set_cmdlevel(caller, pplayer, level)) {
	  cmd_reply(CMD_CMDLEVEL, caller, C_OK,
	    _("Command access level set to '%s' for player %s."),
	    cmdlevel_name(level),
	    pplayer->name);
	} else {
	  cmd_reply(CMD_CMDLEVEL, caller, C_OK,
	    _("Command access level could not be set to '%s' for player %s."),
	    cmdlevel_name(level),
	    pplayer->name);
	}
      }
    }
    default_access_level = level;
    cmd_reply(CMD_CMDLEVEL, caller, C_OK,
	      _("default command access level set to '%s'"),
	      cmdlevel_name(level));
    notify_player(0, _("Game: All players now have access level '%s'."),
		  cmdlevel_name(level));
    if (level > first_access_level) {
      first_access_level = level;
      cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("first connection command access level also raised to '%s'"),
		cmdlevel_name(level));
    }
  }
  else if (strcmp(arg_name,"new") == 0) {
    default_access_level = level;
    cmd_reply(CMD_CMDLEVEL, caller, C_OK,
	      _("default command access level set to '%s'"),
	      cmdlevel_name(level));
    notify_player(0, _("Game: New connections will have access level '%s'."),
		  cmdlevel_name(level));
    if (level > first_access_level) {
      first_access_level = level;
      cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("first connection command access level also raised to '%s'"),
		cmdlevel_name(level));
    }
  }
  else if (strcmp(arg_name,"first") == 0) {
    first_access_level = level;
    cmd_reply(CMD_CMDLEVEL, caller, C_OK,
	      _("first connection command access level set to '%s'"),
	      cmdlevel_name(level));
    notify_player(0, _("Game: First connections will have access level '%s'."),
		  cmdlevel_name(level));
    if (level < default_access_level) {
      default_access_level = level;
      cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("default command access level also lowered to '%s'"),
		cmdlevel_name(level));
    }
  }
  else if ((pplayer=find_player_by_name_prefix(arg_name,&match_result))) {
    if (!pplayer->conn) {
      cmd_reply(CMD_CMDLEVEL, caller, C_FAIL,
		_("Cannot change command access for unconnected player '%s'."),
		pplayer->name);
      return;
    }
    if (set_cmdlevel(caller,pplayer,level)) {
      cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("Command access level set to '%s' for player %s."),
		cmdlevel_name(level),
		pplayer->name);
    } else {
      cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("Command access level could not be set to '%s'"
		  " for player %s."),
		cmdlevel_name(level),
		pplayer->name);
    }
  } else {
    cmd_reply_no_such_player(CMD_CMDLEVEL, caller, arg_name, match_result);
  }
}

static const char *optname_accessor(int i) {
  return settings[i].name;
}
/**************************************************************************
Find option index by name. Return index (>=0) on success, -1 if no
suitable options were found, -2 if several matches were found.
**************************************************************************/
static int lookup_option(const char *name)
{
  enum m_pre_result result;
  int ind;
  static int num = 0;		/* number of options */

  while (settings[num].name!=NULL) num++;

  result = match_prefix(optname_accessor, num, 0, mystrncasecmp, name, &ind);

  return ((result < M_PRE_AMBIGUOUS) ? ind :
	  (result == M_PRE_AMBIGUOUS) ? -2 : -1);
}

/**************************************************************************
 Show the caller detailed help for the single OPTION given by id.
 help_cmd is the command the player used.
 Only show option values for options which the caller can SEE.
**************************************************************************/
static void show_help_option(struct player *caller,
			     enum command_id help_cmd,
			     int id)
{
  struct settings_s *op = &settings[id];

  if (op->short_help) {
    cmd_reply(help_cmd, caller, C_COMMENT,
	      "%s %s  -  %s", _("Option:"), op->name, _(op->short_help));
  } else {
    cmd_reply(help_cmd, caller, C_COMMENT,
	      "%s %s", _("Option:"), op->name);
  }

  if(op->extra_help && strcmp(op->extra_help,"")!=0) {
    static struct astring abuf = ASTRING_INIT;
    const char *help = _(op->extra_help);

    astr_minsize(&abuf, strlen(help)+1);
    strcpy(abuf.str, help);
    wordwrap_string(abuf.str, 76);
    cmd_reply(help_cmd, caller, C_COMMENT, _("Description:"));
    cmd_reply_prefix(help_cmd, caller, C_COMMENT,
		     "  ", "  %s", abuf.str);
  }
  cmd_reply(help_cmd, caller, C_COMMENT,
	    _("Status: %s"), (sset_is_changeable(id)
				  ? _("changeable") : _("fixed")));
  
  if (may_view_option(caller, id)) {
    if (SETTING_IS_INT(op)) {
      cmd_reply(help_cmd, caller, C_COMMENT,
		_("Value: %d, Minimum: %d, Default: %d, Maximum: %d"),
		*(op->value), op->min_value, op->default_value, op->max_value);
    } else {
      cmd_reply(help_cmd, caller, C_COMMENT,
		_("Value: \"%s\", Default: \"%s\""),
		op->svalue, op->default_svalue);
    }
  }
}

/**************************************************************************
 Show the caller list of OPTIONS.
 help_cmd is the command the player used.
 Only show options which the caller can SEE.
**************************************************************************/
static void show_help_option_list(struct player *caller,
				  enum command_id help_cmd)
{
  int i, j;
  
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
  cmd_reply(help_cmd, caller, C_COMMENT,
	    _("Explanations are available for the following server options:"));
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
  if(caller == NULL && con_get_style()) {
    for (i=0; settings[i].name; i++) {
      cmd_reply(help_cmd, caller, C_COMMENT, "%s", settings[i].name);
    }
  } else {
    char buf[MAX_LEN_CMD+1];
    buf[0] = '\0';
    for (i=0, j=0; settings[i].name; i++) {
      if (may_view_option(caller, i)) {
	cat_snprintf(buf, sizeof(buf), "%-19s", settings[i].name);
	if ((++j % 4) == 0) {
	  cmd_reply(help_cmd, caller, C_COMMENT, buf);
	  buf[0] = '\0';
	}
      }
    }
    if (buf[0])
      cmd_reply(help_cmd, caller, C_COMMENT, buf);
  }
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
}

/**************************************************************************
 ...
**************************************************************************/
static void explain_option(struct player *caller, char *str)
{
  char command[MAX_LEN_CMD+1], *cptr_s, *cptr_d;
  int cmd;

  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);
  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  if (*command) {
    cmd=lookup_option(command);
    if (cmd==-1) {
      cmd_reply(CMD_EXPLAIN, caller, C_FAIL, _("No explanation for that yet."));
      return;
    } else if (cmd==-2) {
      cmd_reply(CMD_EXPLAIN, caller, C_FAIL, _("Ambiguous option name."));
      return;
    } else {
      show_help_option(caller, CMD_EXPLAIN, cmd);
    }
  } else {
    show_help_option_list(caller, CMD_EXPLAIN);
  }
}
  
/******************************************************************
Send a report with server options to the client;
"which" should be one of:
1: initial options only
2: ongoing options only 
(which=0 means all options; this is now obsolete and no longer used.)
******************************************************************/
void report_server_options(struct player *pplayer, int which)
{
  int i;
  char buffer[4096];
  char title[128];
  char *caption;
  buffer[0]=0;
  my_snprintf(title, sizeof(title), _("%-20svalue  (min , max)"), _("Option"));
  caption = (which == 1) ?
    _("Server Options (initial)") :
    _("Server Options (ongoing)");

  for (i=0;settings[i].name;i++) {
    struct settings_s *op = &settings[i];
    if (!sset_is_to_client(i)) continue;
    if (which==1 && op->sclass > SSET_GAME_INIT) continue;
    if (which==2 && op->sclass <= SSET_GAME_INIT) continue;
    if (SETTING_IS_INT(op)) {
      cat_snprintf(buffer, sizeof(buffer), "%-20s%c%-6d (%d,%d)\n", op->name,
		   (*op->value==op->default_value) ? '*' : ' ',
		   *op->value, op->min_value, op->max_value);
    } else {
      cat_snprintf(buffer, sizeof(buffer), "%-20s%c\"%s\"\n", op->name,
		   (strcmp(op->svalue, op->default_svalue)==0) ? '*' : ' ',
		   op->svalue);
    }
  }
  freelog(LOG_DEBUG, "report_server_options buffer len %d", i);
  page_player(pplayer, caption, title, buffer);
}

/******************************************************************
  Set an AI level and related quantities, with no feedback.
******************************************************************/
void set_ai_level_directer(struct player *pplayer, int level)
{
  pplayer->ai.handicap = handicap_of_skill_level(level);
  pplayer->ai.fuzzy = fuzzy_of_skill_level(level);
  pplayer->ai.expand = expansionism_of_skill_level(level);
  pplayer->ai.skill_level = level;
}

/******************************************************************
  Set an AI level, with feedback to console only.
******************************************************************/
void set_ai_level_direct(struct player *pplayer, int level)
{
  set_ai_level_directer(pplayer,level);
  con_write(C_OK, _("%s is now %s."),
	pplayer->name, name_of_skill_level(level));
}

/******************************************************************
  Handle a user command to set an AI level.
******************************************************************/
static void set_ai_level(struct player *caller, char *name, int level)
{
  enum m_pre_result match_result;
  struct player *pplayer;
  int i;
  enum command_id cmd = (level <= 3) ?	CMD_EASY :
			(level >= 6) ?	CMD_HARD :
					CMD_NORMAL;
    /* kludge - these commands ought to be 'set' options really - rp */

  assert(level > 0 && level < 11);

  pplayer=find_player_by_name_prefix(name, &match_result);

  if (pplayer) {
    if (pplayer->ai.control) {
      set_ai_level_directer(pplayer, level);
      cmd_reply(cmd, caller, C_OK,
		_("%s is now %s."), pplayer->name, name_of_skill_level(level));
    } else {
      cmd_reply(cmd, caller, C_FAIL,
		_("%s is not controlled by the AI."), pplayer->name);
    }
  } else if(match_result == M_PRE_EMPTY) {
    for (i = 0; i < game.nplayers; i++) {
      pplayer = get_player(i);
      if (pplayer->ai.control) {
	set_ai_level_directer(pplayer, level);
	cmd_reply(cmd, caller, C_OK,
		  _("%s is now %s."), pplayer->name, name_of_skill_level(level));
      }
    }
    cmd_reply(cmd, caller, C_OK,
	      _("Setting game.skill_level to %d."), level);
    game.skill_level = level;
  } else {
    cmd_reply_no_such_player(cmd, caller, name, match_result);
  }
}

static void crash_and_burn(struct player *caller)
{
  cmd_reply(CMD_CRASH, caller, C_GENFAIL, _("Crashing and burning."));
  /* Who is General Failure and why is he crashing and
     burning my computer? :) -- Per */
   assert(0);
}

/******************************************************************
Print a summary of the settings and their values.
Note that most values are at most 4 digits, except seeds,
which we let overflow their columns, plus a sign character.
Only show options which the caller can SEE.
******************************************************************/
static void show_command(struct player *caller, char *str)
{
  char buf[MAX_LEN_CMD+1];
  char command[MAX_LEN_CMD+1], *cptr_s, *cptr_d;
  int cmd,i,len1;
  int clen = 0;

  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);
  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  if (*command) {
    cmd=lookup_option(command);
    if (cmd>=0 && !may_view_option(caller, cmd)) {
      cmd_reply(CMD_SHOW, caller, C_FAIL,
		_("Sorry, you do not have access to view option '%s'."),
		command);
      return;
    }
    if (cmd==-1) {
      cmd_reply(CMD_SHOW, caller, C_FAIL, _("Unknown option '%s'."), command);
      return;
    }
    if (cmd==-2) {
      /* allow ambiguous: show all matching */
      clen = strlen(command);
    }
  } else {
   cmd = -1;  /* to indicate that no comannd was specified */
  }

#define cmd_reply_show(string)  cmd_reply(CMD_SHOW, caller, C_COMMENT, string)

#define OPTION_NAME_SPACE 13
  /* under SSET_MAX_LEN, so it fits into 80 cols more easily - rp */

  cmd_reply_show(horiz_line);
  cmd_reply_show(_("+ means you may change the option"));
  cmd_reply_show(_("= means the option is on its default value"));
  cmd_reply_show(horiz_line);
  len1 = my_snprintf(buf, sizeof(buf),
	_("%-*s value   (min,max)      "), OPTION_NAME_SPACE, _("Option"));
  sz_strlcat(buf, _("description"));
  cmd_reply_show(buf);
  cmd_reply_show(horiz_line);

  buf[0] = '\0';

  for (i=0;settings[i].name;i++) {
    if (may_view_option(caller, i)
	&& (cmd==-1 || cmd==i
	    || (cmd==-2 && mystrncasecmp(settings[i].name, command, clen)==0))) {
      /* in the cmd==i case, this loop is inefficient. never mind - rp */
      struct settings_s *op = &settings[i];
      int len;

      if (SETTING_IS_INT(op)) {
        len = my_snprintf(buf, sizeof(buf),
		      "%-*s %c%c%-5d (%d,%d)", OPTION_NAME_SPACE, op->name,
		      may_set_option_now(caller,i) ? '+' : ' ',
		      ((*op->value==op->default_value) ? '=' : ' '),
		      *op->value, op->min_value, op->max_value);
      } else {
        len = my_snprintf(buf, sizeof(buf),
		      "%-*s %c%c\"%s\"", OPTION_NAME_SPACE, op->name,
		      may_set_option_now(caller,i) ? '+' : ' ',
		      ((strcmp(op->svalue, op->default_svalue)==0) ? '=' : ' '),
		      op->svalue);
      }
      /* Line up the descriptions: */
      if(len < len1) {
        cat_snprintf(buf, sizeof(buf), "%*s", (len1-len), " ");
      } else {
        sz_strlcat(buf, " ");
      }
      sz_strlcat(buf, _(op->short_help));
      cmd_reply_show(buf);
    }
  }
  cmd_reply_show(horiz_line);
#undef cmd_reply_show
#undef OPTION_NAME_SPACE
}

static void set_command(struct player *caller, char *str) 
{
  char command[MAX_LEN_CMD+1], arg[MAX_LEN_CMD+1], *cptr_s, *cptr_d;
  int val, cmd;
  struct settings_s *op;

  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);

  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';
  
  for(; *cptr_s && (*cptr_s != '-' && !isalnum(*cptr_s)); cptr_s++);

  for(cptr_d=arg; *cptr_s && (*cptr_s == '-' || isalnum(*cptr_s)); cptr_s++ , cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  cmd=lookup_option(command);
  if (cmd==-1) {
    cmd_reply(CMD_SET, caller, C_SYNTAX,
	      _("Undefined argument.  Usage: set <option> <value>."));
    return;
  }
  else if (cmd==-2) {
    cmd_reply(CMD_SET, caller, C_SYNTAX,
	      _("Ambiguous option name."));
    return;
  }
  if (!may_set_option(caller,cmd)) {
     cmd_reply(CMD_SET, caller, C_FAIL,
	       _("You are not allowed to set this option."));
    return;
  }
  if (!sset_is_changeable(cmd)) {
    cmd_reply(CMD_SET, caller, C_BOUNCE,
	      _("This setting can't be modified after the game has started."));
    return;
  }

  op = &settings[cmd];
  
  if (SETTING_IS_INT(op)) {
    val = atoi(arg);
    if (val >= op->min_value && val <= op->max_value) {
      *(op->value) = val;
      cmd_reply(CMD_SET, caller, C_OK, _("Option: %s has been set to %d."), 
		settings[cmd].name, val);
      if (sset_is_to_client(cmd)) {
	notify_player(0, _("Option: %s has been set to %d."), 
		      settings[cmd].name, val);
	/* canonify map generator settings( all of which are int ) */
	adjust_terrain_param();
	/* send any modified game parameters to the clients */
	send_game_info(0);
      }
    } else {
      cmd_reply(CMD_SET, caller, C_SYNTAX,
		_("Value out of range (minimum: %d, maximum: %d)."),
		op->min_value, op->max_value);
    }
  } else {
    if (strlen(arg)<op->sz_svalue) {
      strcpy(op->svalue, arg);
      cmd_reply(CMD_SET, caller, C_OK,
		_("Option: %s has been set to \"%s\"."),
		op->name, op->svalue);
      if (sset_is_to_client(cmd)) {
	notify_player(0, _("Option: %s has been set to \"%s\"."), 
		      op->name, op->svalue);
      }
    } else {
      cmd_reply(CMD_SET, caller, C_SYNTAX,
		_("String value too long.  Usage: set <option> <value>."));
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_stdin_input(struct player *caller, char *str)
{
  char command[MAX_LEN_CMD+1], arg[MAX_LEN_CMD+1], *cptr_s, *cptr_d;
  int i;
  enum command_id cmd;

  /* notify to the server console */
  if (caller) {
    con_write(C_COMMENT, "%s: '%s'", caller->name, str);
  }

  /* if the caller may not use any commands at all, don't waste any time */
  if (may_use_nothing(caller)) {
    cmd_reply(CMD_HELP, caller, C_FAIL,
	_("Sorry, you are not allowed to use server commands."));
     return;
  }

  /* Is it a comment or a blank line? */
  /* line is comment if the first non-whitespace character is '#': */
  for(cptr_s=str; *cptr_s && isspace(*cptr_s); cptr_s++);
  if(*cptr_s == 0 || *cptr_s == '#') {
    return;
  }

  /* commands may be prefixed with SERVER_COMMAND_PREFIX,
     even when given on the server command line - rp */

  if (*cptr_s == SERVER_COMMAND_PREFIX) cptr_s++;

  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);

  for(cptr_d=command; *cptr_s && isalnum(*cptr_s) &&
      cptr_d < command+sizeof(command)-1; cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  cmd = command_named(command,0);
  if (cmd == CMD_AMBIGUOUS) {
    cmd = command_named(command,1);
    cmd_reply(cmd, caller, C_SYNTAX,
	_("Warning: '%s' interpreted as '%s', but it is ambiguous."
	  "  Try '%shelp'."),
	command, commands[cmd].name, caller?"/":"");
  } else if (cmd == CMD_UNRECOGNIZED) {
    cmd_reply(cmd, caller, C_SYNTAX,
	_("Unknown command.  Try '%shelp'."), caller?"/":"");
    return;
  }
  if (!may_use(caller, cmd)) {
    assert(caller);
    cmd_reply(cmd, caller, C_FAIL,
	      _("You are not allowed to use this command."));
    return;
  }

  for(; *cptr_s && isspace(*cptr_s); cptr_s++);
  sz_strlcpy(arg, cptr_s);

  i=strlen(arg)-1;
  while(i>0 && isspace(arg[i]))
    arg[i--]='\0';

  switch(cmd) {
  case CMD_REMOVE:
    remove_player(caller,arg);
    break;
  case CMD_RENAME:
    rename_player(caller,arg);
    break;
  case CMD_SAVE:
    save_command(caller,arg);
    break;
  case CMD_META:
    meta_command(caller,arg);
    break;
  case CMD_METAINFO:
    metainfo_command(caller,arg);
    break;
  case CMD_METASERVER:
    metaserver_command(caller,arg);
    break;
  case CMD_HELP:
    show_help(caller, arg);
    break;
  case CMD_LIST:
    show_players(caller);
    break;
  case CMD_AITOGGLE:
    toggle_ai_player(caller,arg);
    break;
  case CMD_CREATE:
    create_ai_player(caller,arg);
    break;
  case CMD_CRASH:
    crash_and_burn(caller);
  case CMD_LOG:		/* undocumented */
    freelog(LOG_NORMAL, "%s", arg);
    break;
  case CMD_EASY:
    set_ai_level(caller, arg, 3);
    break;
  case CMD_NORMAL:
    set_ai_level(caller, arg, 5);
    break;
  case CMD_HARD:
    set_ai_level(caller, arg, 7);
    break;
  case CMD_QUIT:
    quit_game(caller);
    break;
  case CMD_CUT:
    cut_player_connection(caller,arg);
    break;
  case CMD_SHOW:
    show_command(caller,arg);
    break;
  case CMD_EXPLAIN:
    explain_option(caller,arg);
    break;
  case CMD_SET:
    set_command(caller,arg);
    break;
  case CMD_SCORE:
    if(server_state==RUN_GAME_STATE) {
      show_ending();
    } else {
      cmd_reply(cmd, caller, C_SYNTAX,
		_("The game must be running before you can see the score."));
    }
    break;
  case CMD_READ_SCRIPT:
    read_command(caller,arg);
    break;
  case CMD_WRITE_SCRIPT:
    write_command(caller,arg);
    break;
  case CMD_RULESOUT:
    rulesout_command(caller, arg);
    break;
  case CMD_RFCSTYLE:	/* undocumented */
    con_set_style(1);
    break;
  case CMD_FREESTYLE:	/* undocumented */
    con_set_style(0);
    break;
  case CMD_CMDLEVEL:
    cmdlevel_command(caller,arg);
    break;
  case CMD_START_GAME:
    if(server_state==PRE_GAME_STATE) {
      int plrs=0;
      int i;

      /* Sanity check scenario */
      if (map.fixed_start_positions && game.max_players > map.num_start_positions) {
	freelog(LOG_VERBOSE, _("Reduced maxplayers from %i to %i to fit"
			       "to the number of start positions."),
		game.max_players, map.num_start_positions);
	game.max_players = map.num_start_positions;
      }

      if (game.nplayers > game.max_players) {
	/* Because of the way player ids are renumbered during
	   server_remove_player() this is correct */
	while (game.nplayers > game.max_players)
	  server_remove_player(get_player(game.max_players));

	freelog(LOG_VERBOSE, _("Had to cut down the number of players to the "
			       "number of map start positions, there must be "
			       "something wrong with the savegame or you "
			       "adjusted the maxplayers value."));
      }

      for (i=0;i<game.nplayers;i++) {
        if (game.players[i].conn || game.players[i].ai.control) plrs++ ;
      }

      if (plrs<game.min_players) {
        cmd_reply(cmd,caller, C_FAIL,
		  _("Not enough players, game will not start."));
      } else {
        start_game();
      }
    }
    else {
      cmd_reply(cmd,caller, C_FAIL,
		_("Cannot start the game: it is already running."));
    }
    break;
  case CMD_NUM:
  case CMD_UNRECOGNIZED:
  case CMD_AMBIGUOUS:
  default:
    freelog(LOG_FATAL, "bug in civserver: impossible command recognized; bye!");
    assert(0);
  }
}

/**************************************************************************
...
**************************************************************************/
static void cut_player_connection(struct player *caller, char *playername)
{
  enum m_pre_result match_result;
  struct player *pplayer;
  struct connection *pc;

  pplayer=find_player_by_name_prefix(playername, &match_result);

  if (!pplayer) {
    cmd_reply_no_such_player(CMD_CUT, caller, playername, match_result);
    return;
  }

  if((pc=pplayer->conn)) {
    cmd_reply(CMD_CUT, caller, C_DISCONNECTED,
	       _("Cutting connection to %s."), pplayer->name);
    lost_connection_to_player(pc);
    close_connection(pc);
  }
  else {
    cmd_reply(CMD_CUT, caller, C_FAIL, _("Sorry, %s is not connected."),
	      pplayer->name);
  }
}

/**************************************************************************
...
**************************************************************************/
static void quit_game(struct player *caller)
{
  int i;
  struct packet_generic_message gen_packet;
  gen_packet.message[0]='\0';
  
  for(i=0; i<game.nplayers; i++)
    send_packet_generic_message(game.players[i].conn, PACKET_SERVER_SHUTDOWN,
				&gen_packet);
  close_connections_and_socket();

  cmd_reply(CMD_QUIT, caller, C_OK, _("Goodbye."));
  exit(0);
}

/**************************************************************************
 Show caller introductory help about the server.
 help_cmd is the command the player used.
**************************************************************************/
static void show_help_intro(struct player *caller, enum command_id help_cmd)
{
  /* This is formated like extra_help entries for settings and commands: */
  const char *help =
    _("Welcome - this is the introductory help text for the Freeciv server.\n\n"
      "Two important server concepts are Commands and Options.\n"
      "Commands, such as 'help', are used to interact with the server.\n"
      "Some commands take one or more parameters, separated by spaces.\n"
      "In many cases commands and command arguments may be abbreviated.\n"
      "Options are settings which control the server as it is running.\n\n"
      "To find out how to get more information about commands and options,\n"
      "use 'help help'.\n\n"
      "For the impatient, the main commands to get going are:\n"
      "  show   -  to see current options\n"
      "  set    -  to set options\n"
      "  start  -  to start the game once players have connected\n"
      "  save   -  to save the current game\n"
      "  quit   -  to exit");

  static struct astring abuf = ASTRING_INIT;
      
  astr_minsize(&abuf, strlen(help)+1);
  strcpy(abuf.str, help);
  wordwrap_string(abuf.str, 78);
  cmd_reply(help_cmd, caller, C_COMMENT, abuf.str);
}

/**************************************************************************
 Show the caller detailed help for the single COMMAND given by id.
 help_cmd is the command the player used.
**************************************************************************/
static void show_help_command(struct player *caller,
			      enum command_id help_cmd,
			      enum command_id id)
{
  struct command *cmd = &commands[id];
  
  if (cmd->short_help) {
    cmd_reply(help_cmd, caller, C_COMMENT,
	      "%s %s  -  %s", _("Command:"), cmd->name, _(cmd->short_help));
  } else {
    cmd_reply(help_cmd, caller, C_COMMENT,
	      "%s %s", _("Command:"), cmd->name);
  }
  if (cmd->synopsis) {
    /* line up the synopsis lines: */
    const char *syn = _("Synopsis: ");
    int synlen = strlen(syn);
    char prefix[40];

    my_snprintf(prefix, sizeof(prefix), "%*s", synlen, " ");
    cmd_reply_prefix(help_cmd, caller, C_COMMENT, prefix,
		     "%s%s", syn, _(cmd->synopsis));
  }
  cmd_reply(help_cmd, caller, C_COMMENT,
	    _("Level: %s"), cmdlevel_name(cmd->level));
  if (cmd->extra_help) {
    static struct astring abuf = ASTRING_INIT;
    const char *help = _(cmd->extra_help);
      
    astr_minsize(&abuf, strlen(help)+1);
    strcpy(abuf.str, help);
    wordwrap_string(abuf.str, 76);
    cmd_reply(help_cmd, caller, C_COMMENT, _("Description:"));
    cmd_reply_prefix(help_cmd, caller, C_COMMENT, "  ",
		     "  %s", abuf.str);
  }
}

/**************************************************************************
 Show the caller list of COMMANDS.
 help_cmd is the command the player used.
**************************************************************************/
static void show_help_command_list(struct player *caller,
				  enum command_id help_cmd)
{
  enum command_id i;
  
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
  cmd_reply(help_cmd, caller, C_COMMENT,
	    _("The following server commands are available:"));
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
  if(caller == NULL && con_get_style()) {
    for (i=0; i<CMD_NUM; i++) {
      cmd_reply(help_cmd, caller, C_COMMENT, "%s", commands[i].name);
    }
  } else {
    char buf[MAX_LEN_CMD+1];
    int j;
    
    buf[0] = '\0';
    for (i=0, j=0; i<CMD_NUM; i++) {
      if (may_use(caller, i)) {
	cat_snprintf(buf, sizeof(buf), "%-19s", commands[i].name);
	if((++j % 4) == 0) {
	  cmd_reply(help_cmd, caller, C_COMMENT, buf);
	  buf[0] = '\0';
	}
      }
    }
    if (buf[0])
      cmd_reply(help_cmd, caller, C_COMMENT, buf);
  }
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
}

#define H_ARG_COMMANDS  (CMD_NUM)
#define H_ARG_OPTIONS   ((CMD_NUM)+1)
#define H_ARG_OPT_START ((CMD_NUM)+2)

/**************************************************************************
 0 to CMD_NUM-1 are commands, then "commands", "options", then options.
**************************************************************************/
static const char *helparg_accessor(int i) {
  if (i<CMD_NUM)         return cmdname_accessor(i);
  if (i==H_ARG_COMMANDS) return "commands";
  if (i==H_ARG_OPTIONS)  return "options";
  return optname_accessor(i-H_ARG_OPT_START);
}
/**************************************************************************
...
**************************************************************************/
static void show_help(struct player *caller, char *arg)
{
  static int num_opt = 0;	/* number of options */
  int num_args;			/* number of valid args */
  int ind;
  enum m_pre_result match_result;

  while (settings[num_opt].name!=NULL) num_opt++;
  num_args = num_opt + CMD_NUM + 2;
  
  assert(!may_use_nothing(caller));
    /* no commands means no help, either */

  match_result = match_prefix(helparg_accessor, num_args, 0,
			      mystrncasecmp, arg, &ind);

  if (match_result==M_PRE_EMPTY) {
    show_help_intro(caller, CMD_HELP);
    return;
  }
  if (match_result==M_PRE_AMBIGUOUS) {
    cmd_reply(CMD_HELP, caller, C_FAIL,
	      _("Help argument '%s' is ambiguous."), arg);
    return;
  }
  if (match_result==M_PRE_FAIL) {
    cmd_reply(CMD_HELP, caller, C_FAIL,
	      _("No match for help argument '%s'."), arg);
    return;
  }

  /* other cases should be above */
  assert(match_result < M_PRE_AMBIGUOUS);
  
  if (ind == H_ARG_OPTIONS) {
    show_help_option_list(caller, CMD_HELP);
    return;
  }
  if (ind == H_ARG_COMMANDS) {
    show_help_command_list(caller, CMD_HELP);
    return;
  }
  if (ind >= H_ARG_OPT_START) {
    show_help_option(caller, CMD_HELP, ind-H_ARG_OPT_START);
    return;
  }
  if (ind < CMD_NUM) {
    show_help_command(caller, CMD_HELP, ind);
    return;
  }
  
  /* should have finished by now */
  freelog(LOG_NORMAL, "Bug in show_help!");

#ifdef UNUSED
#define cmd_reply_help(cmd,string) \
  if (may_use(caller,cmd)) \
    cmd_reply(CMD_HELP, caller, C_COMMENT, string)

  cmd_reply_help(CMD_HELP, _("Available commands (abbreviations are allowed):"));
  cmd_reply_help(CMD_HELP, horiz_line);
  cmd_reply_help(CMD_AITOGGLE,
	_("ai P            - toggles AI on player"));
  cmd_reply_help(CMD_CMDLEVEL,
	_("cmdlevel        - see current command levels"));
  cmd_reply_help(CMD_CMDLEVEL,
	_("cmdlevel L      - sets command access level to L for all players"));
  cmd_reply_help(CMD_CMDLEVEL,
	_("cmdlevel L new  - sets command access level to L for new connections"));
  cmd_reply_help(CMD_CMDLEVEL,
	_("cmdlevel L P    - sets command access level to L for player P"));
  cmd_reply_help(CMD_CREATE,
	_("create P        - creates an AI player"));
  cmd_reply_help(CMD_CUT,
	_("cut P           - cut connection to player"));
  cmd_reply_help(CMD_EASY,
	_("easy            - all AI players will be easy"));
  cmd_reply_help(CMD_EASY,
	_("easy P          - AI player will be easy"));
  cmd_reply_help(CMD_EXPLAIN,
	_("explain         - help on server options"));
  cmd_reply_help(CMD_EXPLAIN,
	_("explain T       - help on a particular server option"));
  cmd_reply_help(CMD_HARD,
	_("hard            - all AI players will be hard"));
  cmd_reply_help(CMD_HARD,
	_("hard P          - AI player will be hard"));
  cmd_reply_help(CMD_HELP,
	_("help            - this help text"));
  cmd_reply_help(CMD_LIST,
	_("list            - list players"));
  cmd_reply_help(CMD_META,
	_("meta u|d|?      - metaserver connection control: up, down, report"));
  cmd_reply_help(CMD_METAINFO,
	_("metainfo M      - set metaserver infoline to M"));
  cmd_reply_help(CMD_METASERVER,
	_("metaserver A    - game reported to metaserver at address A"));
  cmd_reply_help(CMD_NORMAL,
	_("normal          - all AI players will be normal"));
  cmd_reply_help(CMD_NORMAL,
	_("normal P        - AI player will be normal"));
  cmd_reply_help(CMD_QUIT,
	_("quit            - quit game and shutdown server"));
  cmd_reply_help(CMD_READ_SCRIPT,
	_("read F          - process the server commands from file F"));
  cmd_reply_help(CMD_REMOVE,
	_("remove P        - fully remove player from game"));
  cmd_reply_help(CMD_SAVE,
	_("save F          - save game as file F"));
  cmd_reply_help(CMD_SCORE,
	_("score           - show current score"));
  cmd_reply_help(CMD_SET,
	_("set             - set options"));
  cmd_reply_help(CMD_SHOW,
	_("show            - list current server options"));
  cmd_reply_help(CMD_SHOW,
	_("show O          - list current value of server option O"));
  if(server_state==PRE_GAME_STATE) {
    cmd_reply_help(CMD_START_GAME,
	_("start           - start game"));
  } else {
    cmd_reply_help(CMD_START_GAME,
	_("start           - start game (unavailable: already running)"));
  }
  cmd_reply_help(CMD_WRITE_SCRIPT,
	_("write F         - write current settings as server commands to F"));
  cmd_reply_help(CMD_HELP, horiz_line);
  cmd_reply_help(CMD_HELP,
	_("(A=address[:port], F=file, L=level, M=message, O=option, P=player, T=topic)"));
#undef cmd_reply_help
#endif
}

/**************************************************************************
...
**************************************************************************/
static void show_players(struct player *caller)
{
  int i;
  
  cmd_reply(CMD_LIST, caller, C_COMMENT, _("List of players:"));
  cmd_reply(CMD_LIST, caller, C_COMMENT, horiz_line);

  if (game.nplayers == 0)
    cmd_reply(CMD_LIST, caller, C_WARNING, _("<no players>"));
  else
  {
    for(i=0; i<game.nplayers; i++) {
      if (is_barbarian(&game.players[i])) {
	if (caller==NULL || access_level(caller) >= ALLOW_CTRL) {
	  cmd_reply(CMD_LIST, caller, C_COMMENT,
		_("%s (Barbarian, %s)"),
		game.players[i].name,
		name_of_skill_level(game.players[i].ai.skill_level));
	}
      }
      else if (game.players[i].ai.control) {
	if (game.players[i].conn) {
	  cmd_reply(CMD_LIST, caller, C_COMMENT,
	       _("%s (AI, difficulty level %s) is being observed from %s"),
		game.players[i].name,
		name_of_skill_level(game.players[i].ai.skill_level),
		game.players[i].addr);
	} else {
	  cmd_reply(CMD_LIST, caller, C_COMMENT,
		_("%s (AI, difficulty level %s) is not being observed"),
		game.players[i].name,
		name_of_skill_level(game.players[i].ai.skill_level));
	}
      }
      else {
	if (game.players[i].conn) {
	  cmd_reply(CMD_LIST, caller, C_COMMENT,
		_("%s (human %s, command level %s) is connected from %s"),
		game.players[i].name,
		game.players[i].username,
		cmdlevel_name(access_level(&game.players[i])),
		game.players[i].addr);
	} else {
	  cmd_reply(CMD_LIST, caller, C_COMMENT,
		_("%s is not connected"),
		game.players[i].name);
	}
      }
    }
  }
  cmd_reply(CMD_LIST, caller, C_COMMENT, horiz_line);
}
