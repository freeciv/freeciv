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

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "registry.h"

#include "civserver.h"
#include "console.h"
#include "gamehand.h"
#include "gamelog.h"
#include "mapgen.h"
#include "plrhand.h"
#include "sernet.h"
#include "meta.h"

#include "stdinhand.h"

#define MAX_LEN_CMD MAX_LEN_PACKET
  /* to be used more widely - rp */

extern int gamelog_level;
extern char metaserver_info_line[256];
extern char metaserver_addr[256];

enum cmdlevel_id default_access_level = ALLOW_INFO;

void cut_player_connection(struct player *caller, char *playername);
void quit_game(struct player *caller);
void show_help(struct player *caller);
void show_players(struct player *caller);
void set_ai_level(struct player *caller, char *name, int level);

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
       If longer than 80 squares should include embedded newlines at
       less than 80 char intervals.  Each line should start with 2
       spaces for indentation.  Should have punctuation etc, and
       should end with a "."
  */
  /* The following apply if the setting is string valued; note these
     default to 0 (NULL) if not explicitly mentioned in initialization
     array.  The setting is integer valued if svalue is NULL.
     (Yes, we could use a union here.  But we don't.)
  */
  char *svalue;	
  char *default_svalue;
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
    N_("  1 = standard, with random continents;\n"
    "  2 = equally sized large islands with one player each, and twice\n"
    "      that many smaller islands;\n"
    "  3 = equally sized large islands with one player each, and a number\n"
    "      of other islands of similar size;\n"
    "  4 = equally sized large islands with two players on every island\n"
    "      (or one with three players for an odd number of players), and\n"
    "      additional smaller islands.\n"
    "  Note: values 2,3 and 4 generate \"fairer\" (but more boring) maps.\n"
    "  (Zero indicates a scenario map.)") },

  { "landmass", &map.landpercent,
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_LANDMASS, MAP_MAX_LANDMASS, MAP_DEFAULT_LANDMASS,
    N_("Amount of land vs ocean"), "" },

  { "mountains", &map.mountains,
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_MOUNTAINS, MAP_MAX_MOUNTAINS, MAP_DEFAULT_MOUNTAINS,
    N_("Amount of hills/mountains"),
    N_("  Small values give flat maps, higher values give more hills and mountains.")},

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
    N_("  The same seed will always produce the same map; for zero (the default)\n"
    "  a seed will be chosen based on the time, to give a random map.") },

/* Map additional stuff: huts and specials.  randseed also goes here
 * because huts and specials are the first time the randseed gets used (?)
 * These are done when the game starts, so these are historical and
 * fixed after the game has started.
 */
  { "randseed", &game.randseed, 
    SSET_MAP_ADD, SSET_SERVER_ONLY,
    GAME_MIN_RANDSEED, GAME_MAX_RANDSEED, GAME_DEFAULT_RANDSEED,
    N_("General random seed"),
    N_("  For zero (the default) a seed will be chosen based on the time.") },

  { "specials", &map.riches, 
    SSET_MAP_ADD, SSET_TO_CLIENT,
    MAP_MIN_RICHES, MAP_MAX_RICHES, MAP_DEFAULT_RICHES,
    N_("Amount of \"special\" resource squares"), 
    N_("  Special resources improve the basic terrain type they are on.\n" 
    "  The server variable's scale is parts per thousand.") },

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
    N_("  There must be at least this many players (connected players or AI's)\n"
    "  before the game can start.") },
  
  { "maxplayers", &game.max_players,
    SSET_PLAYERS, SSET_TO_CLIENT,
    GAME_MIN_MAX_PLAYERS, GAME_MAX_MAX_PLAYERS, GAME_DEFAULT_MAX_PLAYERS,
    N_("Maximum number of players"),
    N_("  For new games, the game will start automatically if/when this number of\n"
    "  players are connected or (for AI's) created.") },

  { "aifill", &game.aifill, 
    SSET_PLAYERS, SSET_TO_CLIENT,
    GAME_MIN_AIFILL, GAME_MAX_AIFILL, GAME_DEFAULT_AIFILL,
    N_("Number of players to fill to with AI's"),
    N_("  If there are fewer than this many players when the game starts, extra AI\n"
    "  players will be created to increase the total number of players to the\n"
    "  value of this option.") },

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
    N_("  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"techs.ruleset\".  The advances (technologies) present in\n"
    "  the game will be initialized from this file.  See also README.rulesets."),
    game.ruleset.techs, GAME_DEFAULT_RULESET },

  { "governments", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing governments.ruleset"),
    N_("  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"governments.ruleset\".  The government types available in\n"
    "  the game will be initialized from this file.  See also README.rulesets."),
    game.ruleset.governments, GAME_DEFAULT_RULESET },

  { "units", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing units.ruleset"),
    N_("  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"units.ruleset\".  The unit types present in the game will\n"
    "  be initialized from this file.  See also README.rulesets."),
    game.ruleset.units, GAME_DEFAULT_RULESET },

  { "buildings", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing buildings.ruleset"),
    N_("  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"buildings.ruleset\".  The building types (City Improvements\n"
    "  and Wonders) in the game will be initialized from this file.\n"
    "  See also README.rulesets."),
    game.ruleset.buildings, GAME_DEFAULT_RULESET },

  { "terrain", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing terrain.ruleset"),
    N_("  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"terrain.ruleset\".  The terrain types present in the game\n"
    "  will be initialized from this file.  See also README.rulesets."),
    game.ruleset.terrain, GAME_DEFAULT_RULESET },

  { "nations", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing nations.ruleset"),
    N_("  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"nations.ruleset\".  The nations present in the game\n"
    "  will be initialized from this file.  See also README.rulesets."),
    game.ruleset.nations, GAME_DEFAULT_RULESET },

  { "cities", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    N_("Data subdir containing cities.ruleset"),
    N_("  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"cities.ruleset\".  The file is used to initialize\n"
    "  city data (such as city style).  See also README.rulesets."),
    game.ruleset.cities, GAME_DEFAULT_RULESET },

  { "researchspeed", &game.techlevel,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_RESEARCHLEVEL, GAME_MAX_RESEARCHLEVEL, GAME_DEFAULT_RESEARCHLEVEL,
    N_("Points required to gain a new advance"),
    N_("  This affects how quickly players can research new technology.") },

  { "techpenalty", &game.techpenalty,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_TECHPENALTY, GAME_MAX_TECHPENALTY, GAME_DEFAULT_TECHPENALTY,
    N_("Percentage penalty when changing tech"),
    N_("  If you change your current research technology, and you have positive\n"
    "  research points, you lose this percentage of those research points.\n"
    "  This does not apply if you have just gained tech this turn.") },

  { "diplcost", &game.diplcost,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_DIPLCOST, GAME_MAX_DIPLCOST, GAME_DEFAULT_DIPLCOST,
    N_("Penalty when getting tech from treaty"),
    N_("  For each advance you gain from a diplomatic treaty, you lose research\n"
    "  points equal to this percentage of the cost to research an new advance.\n"
    "  You can end up with negative research points if this is non-zero.") },

  { "conquercost", &game.conquercost,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_CONQUERCOST, GAME_MAX_CONQUERCOST, GAME_DEFAULT_CONQUERCOST,
    N_("Penalty when getting tech from conquering"),
    N_("  For each advance you gain by conquering an enemy city, you lose research\n"
    "  points equal to this percentage of the cost to research an new advance."
    "  You can end up with negative research points if this is non-zero.") },
  
  { "freecost", &game.freecost,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_FREECOST, GAME_MAX_FREECOST, GAME_DEFAULT_FREECOST,
    N_("Penalty when getting a free tech"),
    N_("  For each advance you gain \"for free\" (other than covered by diplcost\n"
    "  or conquercost: specifically, from huts or from the Great Library), you\n"
    "  lose research points equal to this percentage of the cost to research a\n"
    "  new advance.  You can end up with negative research points if this is\n"
    "  non-zero.") },

  { "foodbox", &game.foodbox, 
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_FOODBOX, GAME_MAX_FOODBOX, GAME_DEFAULT_FOODBOX,
    N_("Food required for a city to grow"), "" },

  { "aqueductloss", &game.aqueductloss,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_AQUEDUCTLOSS, GAME_MAX_AQUEDUCTLOSS, GAME_DEFAULT_AQUEDUCTLOSS,
    N_("Percentage food lost when need aqueduct"),
    N_("  If a city would expand, but it can't because it needs an Aqueduct\n"
    "  (or Sewer System), it loses this percentage of its foodbox (or half\n"
    "  that amount if it has a Granary).") },
  
  { "unhappysize", &game.unhappysize,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_UNHAPPYSIZE, GAME_MAX_UNHAPPYSIZE, GAME_DEFAULT_UNHAPPYSIZE,
    N_("City size before people become unhappy"),
    N_("  Before other adjustments, the first unhappysize citizens in a city are\n"
    "  happy, and subsequent citizens are unhappy. See also cityfactor.") },

  { "cityfactor", &game.cityfactor,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_CITYFACTOR, GAME_MAX_CITYFACTOR, GAME_DEFAULT_CITYFACTOR,
    N_("Number of cities for higher unhappiness"),
    N_("  When the number of cities a player owns is greater than cityfactor, one\n"
    "  extra citizen is unhappy before other adjustments; see also unhappysize.\n"
    "  This assumes a Democracy; for other governments the effect occurs at\n"
    "  smaller numbers of cities.") },

  { "razechance", &game.razechance,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_RAZECHANCE, GAME_MAX_RAZECHANCE, GAME_DEFAULT_RAZECHANCE,
    N_("Chance for conquered building destruction"),
    N_("  When a player conquers a city, each City Improvement has this percentage\n"
    "  chance to be destroyed.") },

  { "civstyle", &game.civstyle,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_CIVSTYLE, GAME_MAX_CIVSTYLE, GAME_DEFAULT_CIVSTYLE,
    N_("Style of Civ rules"),
    N_("  Sets some basic rules; 1 means style of Civ1, 2 means Civ2.\n"
    "  Currently this option affects the following rules:\n"
    "    - Civ2 exposes more area at the very start of a new game.\n"
    "    - Civ2 allows the player to pick which improvement to pillage.\n"
    "    - In Civ2, cities cannot be built next to each other.\n"
    "    - In Civ2, overflight of a hut causes it to disappear.\n"
    "    - In Civ2, cities on mountains produce some food.\n"
    "  See also README.rulesets and the techs, units, buildings and terrain\n"
    "  options.") },

/* Flexible rules: these can be changed after the game has started.
 * Should such flexible rules exist?  diplchance is included here
 * to duplicate its previous behaviour (and note diplchance is only used
 * in the server, so it is "safe" to change).  --dwp
 */
  { "diplchance", &game.diplchance,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_DIPLCHANCE, GAME_MAX_DIPLCHANCE, GAME_DEFAULT_DIPLCHANCE,
    N_("Chance (1 in N) for diplomat/spy contests"),
    N_("  A diplomat (or spy) acting against a city which has one or more defending\n"
    "  diplomats (or spies) has a one in diplchance chance to defeat each such\n"
    "  defender.  Also, the chance of a spy returning from a successful mission\n"
    "  is one in diplchance.  (Diplomats never return.)") },

  { "spacerace", &game.spacerace,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_SPACERACE, GAME_MAX_SPACERACE, GAME_DEFAULT_SPACERACE,
    N_("Whether to allow space race"),
    N_("  If this option is 1, players can build spaceships.  The current AI does not\n"
    "  build spaceships, so this is probably only useful for multiplayer games.") },

  { "civilwarsize", &game.civilwarsize,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_CIVILWARSIZE, GAME_MAX_CIVILWARSIZE, GAME_DEFAULT_CIVILWARSIZE,
    N_("Minimum number of cities for civil war"),
    N_("  A civil war is triggered if a player has at least this many cities and\n"
    "  the player's capital is captured.  If this option is set to the maximum\n"
    "  value, civil wars are turned off altogether.") },

/* Meta options: these don't affect the internal rules of the game, but
 * do affect players.  Also options which only produce extra server
 * "output" and don't affect the actual game.
 * ("endyear" is here, and not RULES_FLEXIBLE, because it doesn't
 * affect what happens in the game, it just determines when the
 * players stop playing and look at the score.)
 */
  { "endyear", &game.end_year,
    SSET_META, SSET_TO_CLIENT,
    GAME_MIN_END_YEAR, GAME_MAX_END_YEAR, GAME_DEFAULT_END_YEAR,
    N_("Year the game ends"), "" },

  { "timeout", &game.timeout,
    SSET_META, SSET_TO_CLIENT,
    GAME_MIN_TIMEOUT, GAME_MAX_TIMEOUT, GAME_DEFAULT_TIMEOUT,
    N_("Maximum seconds per turn"),
    N_("  If all players have not hit \"end turn\" before this time is up, then the\n"
    "  turn ends automatically.  Zero means there is no timeout.") },

  { "turnblock", &game.turnblock,
    SSET_META, SSET_TO_CLIENT,
    0, 1, 0,
    N_("Turn-blocking game play mode"),
    N_("  If this is set to 1 the game turn is not advanced until all players have\n"
    "  finished their turn, including disconnected players.") },
  
  { "demography", NULL,
    SSET_META, SSET_TO_CLIENT,
    0, 0, 0,
    N_("What is in the Demographics report"),
    N_("  This should be a string of characters, each of which specifies the\n"
    "  the inclusion of a line of information in the Demographics report.\n"
    "  The characters and their meanings are:\n"
    "      N = include Population           P = include Production\n"
    "      A = include Land Area            E = include Economics\n"
    "      S = include Settled Area         M = include Military Service\n"
    "      R = include Research Speed       O = include Pollution\n"
    "      L = include Literacy\n"
    "  Additionally, the following characters control whether or not certain\n"
    "  columns are displayed in the report:\n"
    "      q = display \"quantity\" column    r = display \"rank\" column\n"
    "  (The order of these characters is not significant, but their case is.)"),
    game.demography, GAME_DEFAULT_DEMOGRAPHY },

  { "saveturns", &game.save_nturns,
    SSET_META, SSET_SERVER_ONLY,
    0, 200, 10,
    N_("Turns per auto-save"),
    N_("  The game will be automatically saved per this number of turns.\n"
    "  Zero means never auto-save.") },

  { "scorelog", &game.scorelog,
    SSET_META, SSET_SERVER_ONLY,
    GAME_MIN_SCORELOG, GAME_MAX_SCORELOG, GAME_DEFAULT_SCORELOG,
    N_("Whether to log player statistics"),
    N_("  If this is set to 1, player statistics are appended to the file\n"
    "  \"civscore.log\" every turn.  These statistics can be used to create\n"
    "  power graphs after the game.") },

  { "gamelog", &gamelog_level,
    SSET_META, SSET_SERVER_ONLY,
    0, 40, 20,
    N_("Detail level for logging game events"),
    N_("  Only applies if the game log feature is enabled (with the -g command line\n"
    "  option).  Levels: 0=no logging, 20=standard logging, 30=detailed logging,\n"
    "  40=debuging logging.") },

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
     * or if we do have a map but its a scenario one.  Once a scenario is
     * actually started, game.scenario will be set to 0.
     */
    return (map_is_empty() || (game.scenario!=0));
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
};

/* Order here is important: for ambiguous abbreviations the first
   match is used.  Arrange order to:
   - allow old commands 's', 'h', 'l', 'q', 'c' to work.
   - reduce harm for ambiguous cases, where "harm" includes inconvenience,
     eg accidently removing a player in a running game.
*/
enum command_id {
  /* old one-letter commands: */
  CMD_START = 0,
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
  CMD_METASERVER,
  CMD_NOMETA,
  CMD_AITOGGLE,
  CMD_CREATE,
  CMD_EASY,
  CMD_NORMAL,
  CMD_HARD,
  CMD_CMDLEVEL,

  /* potentially harmful: */
  CMD_REMOVE,
  CMD_SAVE,
  CMD_READ,
  CMD_WRITE,

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
  {"start",	ALLOW_CTRL}, 
  {"help",	ALLOW_INFO},
  {"list",	ALLOW_INFO},
  {"quit",	ALLOW_HACK},
  {"cut",	ALLOW_CTRL},
  {"explain",	ALLOW_INFO},
  {"show",	ALLOW_INFO},
  {"score",	ALLOW_CTRL},
  {"set",	ALLOW_CTRL},
  {"rename",	ALLOW_CTRL},
  {"meta",	ALLOW_CTRL},
  {"metaserver",ALLOW_HACK},
  {"nometa",    ALLOW_HACK},
  {"aitoggle",	ALLOW_CTRL},
  {"create",	ALLOW_CTRL},
  {"easy",	ALLOW_CTRL},
  {"normal",	ALLOW_CTRL},
  {"hard",	ALLOW_CTRL},
  {"cmdlevel",	ALLOW_HACK},  /* confusing to leave this at ALLOW_CTRL */
  {"remove",	ALLOW_CTRL},
  {"save",	ALLOW_HACK},
  {"read",	ALLOW_HACK},
  {"write",	ALLOW_HACK},
  {"log",	ALLOW_HACK},
  {"rfcstyle",	ALLOW_HACK},
  {"freestyle",	ALLOW_HACK},
  {"crash",	ALLOW_HACK}
};

/**************************************************************************
  Convert a named command into an id.
  If accept_ambiguity is true, return the first command in the
  enum list which matches, else return CMD_AMBIGOUS on ambiguity.
  (This is a trick to allow ambiguity to be handled in a flexible way
  without importing notify_player() messages inside this routine - rp)
**************************************************************************/
static enum command_id command_named(char *token, int accept_ambiguity)
{
  enum command_id i;
  enum command_id found = CMD_UNRECOGNIZED;
  int len = strlen(token);

  for (i = 0; i < CMD_NUM; ++i) {
    if (strncmp(commands[i].name, token, len) == 0) {
      if (accept_ambiguity) {
	return i;
      } else if (found != CMD_UNRECOGNIZED) {
        return CMD_AMBIGUOUS;
      } else if (strlen(commands[i].name) == len) {
	return i;
      } else {
        found = i;
      }
    }
  }
  return found;
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
  feedback related to server commands
  caller == NULL means console.
  No longer duplicate all output to console.
  'console_id' should be one of the C_ identifiers in console.h
**************************************************************************/
static void cmd_reply(enum command_id cmd, struct player *caller,
		      int console_id, char *format, ...)
{
  char line[MAX_LEN_CMD];
  va_list ap;
  char *cmdname = cmd < CMD_NUM ? commands[cmd].name :
                  cmd == CMD_AMBIGUOUS ? _("(ambiguous)") :
                  cmd == CMD_UNRECOGNIZED ? _("(unknown)") :
			"(?!?)";  /* this case is a bug! */

  va_start(ap,format);
  /* (void)vsnprintf(line,MAX_LEN_CMD-1,format,ap); */
    /*
     * no snprintf() in ANSI C and I don't know how to do it otherwise;
     * then again, there are plenty of other places in which this overflow
     * exists so I won't solve it here until we have myvsnprintf() - rp
     */
  vsprintf(line,format,ap);
  va_end(ap);

  if (caller) {
    notify_player(caller, "/%s: %s", cmdname, line);
    /* cc: to the console - testing has proved it's too verbose - rp
    con_write(console_id, "%s/%s: %s", caller->name, cmdname, line);
    */
  } else {
    con_write(console_id, "%s", line);
  }
}

/**************************************************************************
...
**************************************************************************/
static void meta_command(struct player *caller, char *arg)
{
  strncpy(metaserver_info_line, arg, 256);
  metaserver_info_line[256-1]='\0';
  if (send_server_info_to_metaserver(1,0) == 0) {
    cmd_reply(CMD_META, caller, C_METAERROR,
	      _("Not reporting to the metaserver."));
  } else {
    notify_player(0, _("Metaserver infostring set to '%s'."),
		  metaserver_info_line);
    cmd_reply(CMD_META, caller, C_OK,
	      _("Metaserver info string set."), arg);
  }
}

/**************************************************************************
Is this function really usefull and safe ? --nb
**************************************************************************/

static void close_udp_safe(struct player *caller)
{
  if (send_server_info_to_metaserver(1,1))
  {
    server_close_udp();
    notify_player(0, _("Close metaserver connection to '%s'."), metaserver_addr);
    cmd_reply(CMD_META, caller, C_OK,
	      _("Metaserver connection closed."));
  }
}

/**************************************************************************
...
**************************************************************************/
static void set_metaserver(struct player *caller, char *arg)
{
  close_udp_safe(caller);

  strncpy(metaserver_addr, arg, 256);
  metaserver_addr[256-1]='\0';
  server_open_udp(); 
  if (send_server_info_to_metaserver(1,0))
  { 
    notify_player(0, _("Metaserver is now '%s'."), metaserver_addr);
    cmd_reply(CMD_META, caller, C_OK,
	      _("Metaserver connection opened."));
  }
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
static void toggle_ai_player(struct player *caller, char *arg)
{
  struct player *pplayer;

  if (test_player_name(arg) != PNameOk) {
      cmd_reply(CMD_AITOGGLE, caller, C_SYNTAX,
	     _("Name '%s' is either empty or too long, so it cannot be an AI."),
		arg);
      return;
  }

  pplayer=find_player_by_name(arg);
  if (!pplayer) {
    cmd_reply(CMD_AITOGGLE, caller, C_FAIL,
	      _("No player by the name of '%s'."), arg);
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
    set_ai_level(caller, arg, pplayer->ai.skill_level);
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

  pplayer->ai.control = !pplayer->ai.control;
  pplayer->ai.skill_level = game.skill_level;
  cmd_reply(CMD_CREATE, caller, C_OK,
	    _("Created new AI player: %s."), pplayer->name);
}


/**************************************************************************
...
**************************************************************************/
static void remove_player(struct player *caller, char *arg)
{
  struct player *pplayer;

  if (test_player_name(arg) != PNameOk) {
      cmd_reply(CMD_REMOVE, caller, C_SYNTAX,
	    _("Name is either empty or too long, so it cannot be a player."));
      return;
  }

  pplayer=find_player_by_name(arg);
  
  if(!pplayer) {
    cmd_reply(CMD_REMOVE, caller, C_FAIL, _("No player by that name."));
    return;
  }

  server_remove_player(pplayer);
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
static void read_command(struct player *caller, char *arg)
{
  generic_not_implemented(caller, "read");
}

/**************************************************************************
...
**************************************************************************/
static void write_command(struct player *caller, char *arg)
{
  generic_not_implemented(caller, "write");
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
	      _("Default command level for new connections: %s"),
	      cmdlevel_name(default_access_level));
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
	      _("Default command access level set to '%s'."),
	      cmdlevel_name(level));
    notify_player(0, _("Game: All players now have access level '%s'."),
		  cmdlevel_name(level));
  }
  else if (strcmp(arg_name,"new") == 0) {
    default_access_level = level;
    cmd_reply(CMD_CMDLEVEL, caller, C_OK,
	      _("default command access level set to '%s'"),
	      cmdlevel_name(level));
    notify_player(0, _("Game: New connections will have access level '%s'."),
		  cmdlevel_name(level));
  }
  else if (test_player_name(arg_name) == PNameOk &&
                (pplayer=find_player_by_name(arg_name))) {
    if (!pplayer->conn) {
      cmd_reply(CMD_CMDLEVEL, caller, C_FAIL,
		_("Cannot change command access for unconnected player '%s'."),
		arg_name);
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
    cmd_reply(CMD_CMDLEVEL, caller, C_FAIL,
	      _("Cannot change command access for unknown/invalid"
		" player name '%s'."),
	      arg_name);
  }
}

/**************************************************************************
Find option index by name. Return index (>=0) on success, -1 if no
suitable options were found, -2 if several matches were found.
**************************************************************************/
static int lookup_option(char *find)
{
  int i, lastmatch = -1;
    
  for (i=0;settings[i].name!=NULL;i++) 
    if (!strncmp(find, settings[i].name, strlen(find))) 
    {
	/* If name is a perfect match, not partial, assume
	 * that this is the option the user wanted. */
	if (!strcmp(find, settings[i].name))
	  return i;
	if (lastmatch != -1)
	  return -2;
	lastmatch = i;
    }
    
  return lastmatch;
}

/**************************************************************************
 ...
**************************************************************************/
static void explain_option(struct player *caller, char *str)
{
  char command[MAX_LEN_CMD+1], *cptr_s, *cptr_d;
  int cmd,i;

  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);
  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  if (*command) {
    cmd=lookup_option(command);
    if (cmd==-1) {
      cmd_reply(CMD_EXPLAIN, caller, C_FAIL, _("No explanation for that yet."));
      return;
    }
    else if (cmd==-2) {
      cmd_reply(CMD_EXPLAIN, caller, C_FAIL, _("Ambiguous option name."));
      return;
    }
    else {
      struct settings_s *op = &settings[cmd];

      cmd_reply(CMD_EXPLAIN, caller, C_COMMENT,
		_("Option: %s"), op->name);
      cmd_reply(CMD_EXPLAIN, caller, C_COMMENT,
		_("Description: %s."), _(op->short_help));
      if(op->extra_help && strcmp(op->extra_help,"")!=0) {
	char *line_by_line = mystrdup(_(op->extra_help));
	char *line = line_by_line;
	char *line_end;
	while ((line_end = strchr(line,'\n'))) {
	  *line_end = '\0';
	  cmd_reply(CMD_EXPLAIN, caller, C_COMMENT, line);
	  line = line_end+1;
	}
	cmd_reply(CMD_EXPLAIN, caller, C_COMMENT, line);
	free(line_by_line);
      }
      cmd_reply(CMD_EXPLAIN, caller, C_COMMENT,
		_("Status: %s"), (sset_is_changeable(cmd)
				  ? _("changeable") : _("fixed")));
      if (SETTING_IS_INT(op)) {
 	cmd_reply(CMD_EXPLAIN, caller, C_COMMENT,
		  _("Value: %d, Minimum: %d, Default: %d, Maximum: %d"),
		  *(op->value), op->min_value, op->default_value, op->max_value);
      } else {
 	cmd_reply(CMD_EXPLAIN, caller, C_COMMENT,
		  _("Value: \"%s\", Default: \"%s\""),
		  op->svalue, op->default_svalue);
      }
    }
  } else {
    cmd_reply(CMD_EXPLAIN, caller, C_COMMENT, horiz_line);
    cmd_reply(CMD_EXPLAIN, caller, C_COMMENT,
	_("Explanations are available for the following server options:"));
    cmd_reply(CMD_EXPLAIN, caller, C_COMMENT, horiz_line);
    if(caller == NULL && con_get_style()) {
      for (i=0;settings[i].name;i++) {
	cmd_reply(CMD_EXPLAIN, caller, C_COMMENT, "%s", settings[i].name);
      }
    } else {
      char buf[MAX_LEN_CMD+1];
      buf[0] = '\0';
      for (i=0; settings[i].name; i++) {
	sprintf(&buf[strlen(buf)], "%-19s", settings[i].name);
	if(((i+1)%4) == 0) {
	  cmd_reply(CMD_EXPLAIN, caller, C_COMMENT, buf);
	  buf[0] = '\0';
	}
      }
      if (((i+1)%4) != 0)
	cmd_reply(CMD_EXPLAIN, caller, C_COMMENT, buf);
    }
    cmd_reply(CMD_EXPLAIN, caller, C_COMMENT, horiz_line);
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
  char buf2[4096];
  char title[128];
  char *caption;
  buffer[0]=0;
  sprintf(title, _("%-20svalue  (min , max)"), _("Option"));
  caption = (which == 1) ?
    _("Server Options (initial)") :
    _("Server Options (ongoing)");

  for (i=0;settings[i].name;i++) {
    struct settings_s *op = &settings[i];
    if (!sset_is_to_client(i)) continue;
    if (which==1 && op->sclass > SSET_GAME_INIT) continue;
    if (which==2 && op->sclass <= SSET_GAME_INIT) continue;
    if (SETTING_IS_INT(op)) {
      sprintf(buf2, "%-20s%c%-6d (%d,%d)\n", op->name,
	      (*op->value==op->default_value) ? '*' : ' ',
	      *op->value, op->min_value, op->max_value);
    } else {
      sprintf(buf2, "%-20s%c\"%s\"\n", op->name,
	      (strcmp(op->svalue, op->default_svalue)==0) ? '*' : ' ',
	      op->svalue);
    }
    strcat(buffer, buf2);
  }
  i = strlen(buffer);
  assert(i<sizeof(buffer));
  freelog(LOG_DEBUG, "report_server_options buffer len %d", i);
  page_player(pplayer, caption, title, buffer);
}

/******************************************************************
  Set an AI level and related quantities, with no feedback.
******************************************************************/
static void set_ai_level_directer(struct player *pplayer, int level)
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
void set_ai_level(struct player *caller, char *name, int level)
{
  struct player *pplayer;
  int i;
  enum command_id cmd = (level == 3) ? CMD_EASY :
			(level == 5) ? CMD_NORMAL :
			CMD_HARD;
    /* kludge - these commands ought to be 'set' options really - rp */

  if (test_player_name(name) == PNameTooLong) {
    cmd_reply(cmd, caller, C_SYNTAX,
	      _("Name is too long, so it cannot be a player."));
    return;
  }

  assert(level > 0 && level < 11);

  pplayer=find_player_by_name(name);

  if (pplayer) {
    if (pplayer->ai.control) {
      set_ai_level_directer(pplayer, level);
      cmd_reply(cmd, caller, C_OK,
		_("%s is now %s."), pplayer->name, name_of_skill_level(level));
    } else {
      cmd_reply(cmd, caller, C_FAIL,
		_("%s is not controlled by the AI."), pplayer->name);
    }
  } else if(test_player_name(name) == PNameEmpty) {
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
    cmd_reply(cmd, caller, C_FAIL,
	      _("%s is not the name of any player."), name);
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
which we let overflow their columns.  (And endyear may have '-'.)
******************************************************************/
static void show_command(struct player *caller, char *str)
{
  char buf[MAX_LEN_CMD+1];  /* length is not checked ... - rp */
  char command[MAX_LEN_CMD+1], *cptr_s, *cptr_d;
  int cmd,i,len1;

  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);
  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  if (*command) {
    cmd=lookup_option(command);
    if (cmd==-1) {
      cmd_reply(CMD_SHOW, caller, C_FAIL, _("Unknown option '%s'."), command);
      return;
    }
    else if (cmd==-2) {
      cmd_reply(CMD_SHOW, caller, C_FAIL,
		_("Ambiguous option name '%s'."), command);
      return;
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
  len1 = sprintf(buf,
	_("%-*s value  (min,max)       "), OPTION_NAME_SPACE, _("Option"));
  sprintf(&buf[len1], _("description"));
  cmd_reply_show(buf);
  cmd_reply_show(horiz_line);

  buf[0] = '\0';

  for (i=0;settings[i].name;i++) {
    if (cmd==-1 || cmd==i) {
      /* in the latter case, this loop is inefficient. never mind - rp */
      struct settings_s *op = &settings[i];
      int len;

      if (SETTING_IS_INT(op)) {
        len = sprintf(&buf[strlen(buf)],
		      "%-*s %c%c%-4d (%d,%d)", OPTION_NAME_SPACE, op->name,
		      may_set_option_now(caller,i) ? '+' : ' ',
		      ((*op->value==op->default_value) ? '=' : ' '),
		      *op->value, op->min_value, op->max_value);
      } else {
        len = sprintf(&buf[strlen(buf)],
		      "%-*s %c%c     \"%s\"", OPTION_NAME_SPACE, op->name,
		      may_set_option_now(caller,i) ? '+' : ' ',
		      ((strcmp(op->svalue, op->default_svalue)==0) ? '=' : ' '),
		      op->svalue);
      }
      /* Line up the descriptions: */
      if(len < len1) {
        sprintf(&buf[strlen(buf)], "%*s", (len1-len), " ");
      } else {
        sprintf(&buf[strlen(buf)], " ");
      }
      sprintf(&buf[strlen(buf)], _(op->short_help));
      cmd_reply_show(buf);
      buf[0] = '\0';
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
      }
    } else {
      cmd_reply(CMD_SET, caller, C_SYNTAX,
	_("Value out of range.  Usage: set <option> <value>."));
    }
  } else {
    if (strlen(arg)<MAX_LEN_NAME) {
      strcpy(op->svalue, arg);
      cmd_reply(CMD_SET, caller, C_OK,
		_("Option: %s has been set to \"%s\"."),
		settings[cmd].name, arg);
      if (sset_is_to_client(cmd)) {
	notify_player(0, _("Option: %s has been set to \"%s\"."), 
		      settings[cmd].name, arg);
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
  strncpy(arg, cptr_s, MAX_LEN_CMD);
  arg[MAX_LEN_CMD]='\0';

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
  case CMD_NOMETA:
    close_udp_safe(caller);
    break;
  case CMD_META:
    meta_command(caller,arg);
    break;
  case CMD_METASERVER:
    set_metaserver(caller,arg);
    break;
  case CMD_HELP:
    show_help(caller);
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
  case CMD_READ:
    read_command(caller,arg);
    break;
  case CMD_WRITE:
    write_command(caller,arg);
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
  case CMD_START:
    if(server_state==PRE_GAME_STATE) {
      int plrs=0;
      int i;

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
void cut_player_connection(struct player *caller, char *playername)
{
  struct player *pplayer;

  if (test_player_name(playername) != PNameOk) {
    cmd_reply(CMD_CUT, caller, C_SYNTAX,
	      _("Name is either empty or too long,"
		" so it cannot be a player."));
    return;
  }

  pplayer=find_player_by_name(playername);

  if(pplayer && pplayer->conn) {
    cmd_reply(CMD_CUT, caller, C_DISCONNECTED,
	       _("Cutting connection to %s."), playername);
    close_connection(pplayer->conn);
    pplayer->conn=NULL;
  }
  else {
    cmd_reply(CMD_CUT, caller, C_FAIL, _("Sorry, no such player connected."));
  }
}

/**************************************************************************
...
**************************************************************************/
void quit_game(struct player *caller)
{
  int i;
  struct packet_generic_message gen_packet;
  gen_packet.message[0]='\0';
  
  for(i=0; i<game.nplayers; i++)
    send_packet_generic_message(game.players[i].conn, PACKET_SERVER_SHUTDOWN,
				&gen_packet);
  close_connections_and_socket();
  
  exit(1);
}

/**************************************************************************
...
**************************************************************************/
void show_help(struct player *caller)
{
  assert(!may_use_nothing(caller));
    /* no commands means no help, either */

#define cmd_reply_help(cmd,string) \
  if (may_use(caller,cmd)) \
    cmd_reply(CMD_HELP, caller, C_COMMENT, string)

  cmd_reply_help(CMD_HELP,
		 _("Available commands: (P=player, M=message, F=file,"
		   " L=level, T=topic, O=option)"));
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
	_("easy            - All AI players will be easy"));
  cmd_reply_help(CMD_EASY,
	_("easy P          - AI player will be easy"));
  cmd_reply_help(CMD_EXPLAIN,
	_("explain         - help on server options"));
  cmd_reply_help(CMD_EXPLAIN,
	_("explain T       - help on a particular server option"));
  cmd_reply_help(CMD_HARD,
	_("hard            - All AI players will be hard"));
  cmd_reply_help(CMD_HARD,
	_("hard P          - AI player will be hard"));
  cmd_reply_help(CMD_HELP,
	_("help            - this help text"));
  cmd_reply_help(CMD_LIST,
	_("list            - list players"));
  cmd_reply_help(CMD_META,
	_("meta M          - Set meta-server infoline to M"));
  cmd_reply_help(CMD_METASERVER,
	_("metaserver A    - Game are reported to address A"));
  cmd_reply_help(CMD_NOMETA,
	_("nometa          - Close connection to the metaserver"));
  cmd_reply_help(CMD_NORMAL,
	_("normal          - All AI players will be normal"));
  cmd_reply_help(CMD_NORMAL,
	_("normal P        - AI player will be normal"));
  cmd_reply_help(CMD_QUIT,
	_("quit            - quit game and shutdown server"));
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
    cmd_reply_help(CMD_START,
	_("start           - start game"));
  } else {
    cmd_reply_help(CMD_START,
	_("start           - start game (unavailable: already running)"));
  }
  cmd_reply_help(CMD_HELP,
	_("Abbreviations are allowed."));
#undef cmd_reply_help
}

/**************************************************************************
...
**************************************************************************/
void show_players(struct player *caller)
{
  int i;
  
  cmd_reply(CMD_LIST, caller, C_COMMENT, _("List of players:"));
  cmd_reply(CMD_LIST, caller, C_COMMENT, horiz_line);

  if (game.nplayers == 0)
    cmd_reply(CMD_LIST, caller, C_WARNING, _("<no players>"));
  else
  {
    for(i=0; i<game.nplayers; i++) {
      if (game.players[i].ai.control) {
	if (game.players[i].conn) {
	  cmd_reply(CMD_LIST, caller, C_COMMENT,
		_("%s (AI, %s) is being observed from %s"),
		game.players[i].name,
		name_of_skill_level(game.players[i].ai.skill_level),
		game.players[i].addr);
	} else {
	  cmd_reply(CMD_LIST, caller, C_COMMENT,
		_("%s (AI, %s) is not being observed"),
		game.players[i].name,
		name_of_skill_level(game.players[i].ai.skill_level));
	}
      }
      else {
	if (game.players[i].conn) {
	  cmd_reply(CMD_LIST, caller, C_COMMENT,
		_("%s (human, %s) is connected from %s"),
		game.players[i].name,
                game.players[i].username,
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
