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
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>

#include <game.h>
#include <gamehand.h>
#include <player.h>
#include <civserver.h>
#include <log.h>
#include <sernet.h>
#include <map.h>
#include <mapgen.h>
#include <registry.h>
#include <plrhand.h>
#include <stdinhand.h>
#include <gamelog.h>

extern int gamelog_level;
extern char metaserver_info_line[256];

void cut_player_connection(char *playername);
void quit_game(void);
void show_help(void);
void show_players(void);

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
 * server options.  Eg, not sent: seeds, saveturns, etc.
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

struct settings_s settings[] = {

  /* These should be grouped by sclass */
  
/* Map size parameters: adjustable if we don't yet have a map */  
  { "xsize", &map.xsize,
    SSET_MAP_SIZE, SSET_TO_CLIENT,
    MAP_MIN_WIDTH, MAP_MAX_WIDTH, MAP_DEFAULT_WIDTH,
    "Map width in squares", "" },
  
  { "ysize", &map.ysize,
    SSET_MAP_SIZE, SSET_TO_CLIENT,
    MAP_MIN_HEIGHT, MAP_MAX_HEIGHT, MAP_DEFAULT_HEIGHT,
    "Map height in squares", "" }, 

/* Map generation parameters: once we have a map these are of historical
 * interest only, and cannot be changed.
 */
  { "generator", &map.generator, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_GENERATOR, MAP_MAX_GENERATOR, MAP_DEFAULT_GENERATOR,
    "Method used to generate map",
    "  1 = standard,\n"
    "  2 = equally sized large islands with one player each.\n"
    "  3 = equally sized large islands plus small islands.\n"
    "  4 = equally sized large islands with two players on every island.\n"
    "  Note: values 2,3 and 4 generate \"fairer\" (but more boring) maps.\n"
    "  (Zero indicates a scenario map.)" },

  { "landmass", &map.landpercent,
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_LANDMASS, MAP_MAX_LANDMASS, MAP_DEFAULT_LANDMASS,
    "Amount of land vs ocean", "" },

  { "mountains", &map.mountains,
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_MOUNTAINS, MAP_MAX_MOUNTAINS, MAP_DEFAULT_MOUNTAINS,
    "Amount of hills/mountains",
    "  Small values give flat maps, higher values give more hills and mountains."},

  { "rivers", &map.riverlength, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_RIVERS, MAP_MAX_RIVERS, MAP_DEFAULT_RIVERS,
    "Amount of river squares", "" },

  { "grass", &map.grasssize, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_GRASS, MAP_MAX_GRASS, MAP_DEFAULT_GRASS,
    "Amount of grass squares", "" },

  { "forests", &map.forestsize, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_FORESTS, MAP_MAX_FORESTS, MAP_DEFAULT_FORESTS,
    "Amount of forest squares", "" },

  { "swamps", &map.swampsize, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_SWAMPS, MAP_MAX_SWAMPS, MAP_DEFAULT_SWAMPS,
    "Amount of swamp squares", "" },
    
  { "deserts", &map.deserts, 
    SSET_MAP_GEN, SSET_TO_CLIENT,
    MAP_MIN_DESERTS, MAP_MAX_DESERTS, MAP_DEFAULT_DESERTS,
    "Amount of desert squares", "" },

  { "seed", &map.seed, 
    SSET_MAP_GEN, SSET_SERVER_ONLY,
    MAP_MIN_SEED, MAP_MAX_SEED, MAP_DEFAULT_SEED,
    "Map generation random seed",
    "  The same seed will always produce the same map; for zero (the default)\n"
    "  a seed will be chosen based on the time, to give a random map." },

/* Map additional stuff: huts and specials.  randseed also goes here
 * because huts and specials are the first time the randseed gets used (?)
 * These are done when the game starts, so these are historical and
 * fixed after the game has started.
 */
  { "randseed", &game.randseed, 
    SSET_MAP_ADD, SSET_SERVER_ONLY,
    GAME_MIN_RANDSEED, GAME_MAX_RANDSEED, GAME_DEFAULT_RANDSEED,
    "General random seed",
    "  For zero (the default) a seed will be chosen based on the time." },

  { "specials", &map.riches, 
    SSET_MAP_ADD, SSET_TO_CLIENT,
    MAP_MIN_RICHES, MAP_MAX_RICHES, MAP_DEFAULT_RICHES,
    "Amount of \"special\" resource squares", 
    "Special resources improve the basic terrain type they are on.\n" 
    "The server variable's scale is parts per thousand." },

  { "huts", &map.huts, 
    SSET_MAP_ADD, SSET_TO_CLIENT,
    MAP_MIN_HUTS, MAP_MAX_HUTS, MAP_DEFAULT_HUTS,
    "Amount of huts (minor tribe villages)", "" },

/* Options affecting numbers of players and AI players.  These only
 * affect the start of the game and can not be adjusted after that.
 * (Actually, minplayers does also affect reloads: you can't start a
 * reload game until enough players have connected (or are AI).)
 */
  { "minplayers", &game.min_players,
    SSET_PLAYERS, SSET_TO_CLIENT,
    GAME_MIN_MIN_PLAYERS, GAME_MAX_MIN_PLAYERS, GAME_DEFAULT_MIN_PLAYERS,
    "Minimum number of players",
    "  There must be at least this many players (connected players or AI's)\n"
    "  before the game can start." },
  
  { "maxplayers", &game.max_players,
    SSET_PLAYERS, SSET_TO_CLIENT,
    GAME_MIN_MAX_PLAYERS, GAME_MAX_MAX_PLAYERS, GAME_DEFAULT_MAX_PLAYERS,
    "Maximum number of players",
    "  For new games, the game will start automatically if/when this number of\n"
    "  players are connected or (for AI's) created." },

  { "aifill", &game.aifill, 
    SSET_PLAYERS, SSET_TO_CLIENT,
    GAME_MIN_AIFILL, GAME_MAX_AIFILL, GAME_DEFAULT_AIFILL,
    "Number of players to fill to with AI's",
    "  If there are fewer than this many players when the game starts, extra AI\n"
    "  players will be created to increase the total number of players to the\n"
    "  value of this option." },

/* Game initialization parameters (only affect the first start of the game,
 * and not reloads).  Can not be changed after first start of game.
 */
  { "settlers", &game.settlers,
    SSET_GAME_INIT, SSET_TO_CLIENT,
    GAME_MIN_SETTLERS, GAME_MAX_SETTLERS, GAME_DEFAULT_SETTLERS,
    "Number of initial settlers per player", "" },
    
  { "explorer", &game.explorer,
    SSET_GAME_INIT, SSET_TO_CLIENT,
    GAME_MIN_EXPLORER, GAME_MAX_EXPLORER, GAME_DEFAULT_EXPLORER,
    "Number of initial explorers per player", "" },

  { "gold", &game.gold,
    SSET_GAME_INIT, SSET_TO_CLIENT,
    GAME_MIN_GOLD, GAME_MAX_GOLD, GAME_DEFAULT_GOLD,
    "Starting gold per player", "" },

  { "techlevel", &game.tech, 
    SSET_GAME_INIT, SSET_TO_CLIENT,
    GAME_MIN_TECHLEVEL, GAME_MAX_TECHLEVEL, GAME_DEFAULT_TECHLEVEL,
    "Number of initial advances per player", "" },

/* Various rules: these cannot be changed once the game has started. */
  { "techs", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    "Data subdir containing techs.ruleset",
    "  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"techs.ruleset\".  The advances (technologies) present in\n"
    "  the game will be initialized from this file.  See also README.rulesets.",
    game.ruleset.techs, GAME_DEFAULT_RULESET },

  { "units", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    "Data subdir containing units.ruleset",
    "  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"units.ruleset\".  The unit types present in the game will\n"
    "  be initialized from this file.  See also README.rulesets.",
    game.ruleset.units, GAME_DEFAULT_RULESET },

  { "buildings", NULL,
    SSET_RULES, SSET_TO_CLIENT,
    0, 0, 0,
    "Data subdir containing buildings.ruleset",
    "  This should specify a subdirectory of the data directory, containing a\n"
    "  file called \"buildings.ruleset\".  The building types (City Improvements\n"
    "  and Wonders) in the game will be initialized from this file.\n"
    "  See also README.rulesets.",
    game.ruleset.buildings, GAME_DEFAULT_RULESET },

  { "researchspeed", &game.techlevel,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_RESEARCHLEVEL, GAME_MAX_RESEARCHLEVEL, GAME_DEFAULT_RESEARCHLEVEL,
    "Points required to gain a new advance",
    "  This affects how quickly players can research new technology." },

  { "techpenalty", &game.techpenalty,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_TECHPENALTY, GAME_MAX_TECHPENALTY, GAME_DEFAULT_TECHPENALTY,
    "Percentage penalty when changing tech",
    "  If you change your current research technology, and you have positive\n"
    "  research points, you lose this percentage of those research points.\n"
    "  This does not apply if you have just gained tech this turn." },

  { "diplcost", &game.diplcost,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_DIPLCOST, GAME_MAX_DIPLCOST, GAME_DEFAULT_DIPLCOST,
    "Penalty when getting tech from treaty",
    "  For each advance you gain from a diplomatic treaty, you lose research\n"
    "  points equal to this percentage of the cost to research an new advance.\n"
    "  You can end up with negative research points if this is non-zero." },

  { "conquercost", &game.conquercost,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_CONQUERCOST, GAME_MAX_CONQUERCOST, GAME_DEFAULT_CONQUERCOST,
    "Penalty when getting tech from conquering",
    "  For each advance you gain by conquering an enemy city, you lose research\n"
    "  points equal to this percentage of the cost to research an new advance."
    "  You can end up with negative research points if this is non-zero." },
  
  { "freecost", &game.freecost,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_FREECOST, GAME_MAX_FREECOST, GAME_DEFAULT_FREECOST,
    "Penalty when getting a free tech",
    "  For each advance you gain \"for free\" (other than covered by diplcost\n"
    "  or conquercost: specifically, from huts or from the Great Library), you\n"
    "  lose research points equal to this percentage of the cost to research a\n"
    "  new advance.  You can end up with negative research points if this is\n"
    "  non-zero." },

  { "railprod", &game.rail_prod,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_RAILPROD, GAME_MAX_RAILPROD, GAME_DEFAULT_RAILPROD,
    "Railroad modifier to shield production",
    "  The shield production of a square with a railroad is increased by this\n"
    "  percentage, with the final value rounded down." },
  
  { "railfood", &game.rail_food,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_RAILFOOD, GAME_MAX_RAILFOOD, GAME_DEFAULT_RAILFOOD,
    "Railroad modifier to food production",
    "  The food production of a square with a railroad is increased by this\n"
    "  percentage, with the final value rounded down." },
 
  { "railtrade", &game.rail_trade,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_RAILTRADE, GAME_MAX_RAILTRADE, GAME_DEFAULT_RAILTRADE,
    "Railroad modifier to trade production",
    "  The trade production of a square with a railroad is increased by this\n"
    "  percentage, with the final value rounded down." },

  { "foodbox", &game.foodbox, 
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_FOODBOX, GAME_MAX_FOODBOX, GAME_DEFAULT_FOODBOX,
    "Food required for a city to grow", "" },

  { "aqueductloss", &game.aqueductloss,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_AQUEDUCTLOSS, GAME_MAX_AQUEDUCTLOSS, GAME_DEFAULT_AQUEDUCTLOSS,
    "Percentage food lost when need aqueduct",
    "  If a city would expand, but it can't because it needs an Aqueduct\n"
    "  (or Sewer System), it loses this percentage of its foodbox (or half\n"
    "  that amount if it has a Granary)." },
  
  { "unhappysize", &game.unhappysize,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_UNHAPPYSIZE, GAME_MAX_UNHAPPYSIZE, GAME_DEFAULT_UNHAPPYSIZE,
    "City size before people become unhappy",
    "  Before other adjustments, the first unhappysize citizens in a city are\n"
    "  happy, and subsequent citizens are unhappy. See also cityfactor." },

  { "cityfactor", &game.cityfactor,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_CITYFACTOR, GAME_MAX_CITYFACTOR, GAME_DEFAULT_CITYFACTOR,
    "Number of cities for higher unhappiness",
    "  When the number of cities a player owns is greater than cityfactor, one\n"
    "  extra citizen is unhappy before other adjustments; see also unhappysize.\n"
    "  This assumes a Democracy; for other governments the effect occurs at\n"
    "  smaller numbers of cities." },

  { "razechance", &game.razechance,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_RAZECHANCE, GAME_MAX_RAZECHANCE, GAME_DEFAULT_RAZECHANCE,
    "Chance for conquered building destruction",
    "  When a player conquers a city, each City Improvement has this percentage\n"
    "  chance to be destroyed." },

  { "civstyle", &game.civstyle,
    SSET_RULES, SSET_TO_CLIENT,
    GAME_MIN_CIVSTYLE, GAME_MAX_CIVSTYLE, GAME_DEFAULT_CIVSTYLE,
    "Style of Civ rules",
    "  Sets some basic rules; 1 means style of Civ1, 2 means Civ2.  Currently\n"
    "  this option is mostly unimplemented and only affects a few rules.\n"
    "  See also README.rulesets and the techs, units, and buildings options." },

/* Flexible rules: these can be changed after the game has started.
 * Should such flexible rules exist?  diplchance is included here
 * to duplicate its previous behaviour (and note diplchance is only used
 * in the server, so it is "safe" to change).  --dwp
 */
  { "diplchance", &game.diplchance,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_DIPLCHANCE, GAME_MAX_DIPLCHANCE, GAME_DEFAULT_DIPLCHANCE,
    "Chance (1 in N) for diplomat/spy contests",
    "  A diplomat (or spy) acting against a city which has one or more defending\n"
    "  diplomats (or spies) has a one in diplchance chance to defeat each such\n"
    "  defender.  Also, the chance of a spy returning from a successful mission\n"
    "  is one in diplchance.  (Diplomats never return.)" },

  { "spacerace", &game.spacerace,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_SPACERACE, GAME_MAX_SPACERACE, GAME_DEFAULT_SPACERACE,
    "Whether to allow space race",
    "  If this option is 1, players can build spaceships.  The current AI does not\n"
    "  build spaceships, so this is probably only useful for multiplayer games." },

  { "civilwarsize", &game.civilwarsize,
    SSET_RULES_FLEXIBLE, SSET_TO_CLIENT,
    GAME_MIN_CIVILWARSIZE, GAME_MAX_CIVILWARSIZE, GAME_DEFAULT_CIVILWARSIZE,
    "Minimum number of cities for civil war",
    "  A civil war is triggered if a player has at least this many cities and\n"
    "  the player's capital is captured.  If this option is set to the maximum\n"
    "  value, civil wars are turned off altogether." },

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
    "Year the game ends", "" },

  { "timeout", &game.timeout,
    SSET_META, SSET_TO_CLIENT,
    0, 999, GAME_DEFAULT_TIMEOUT,
    "Maximum seconds per turn",
    "  If all players have not hit \"end turn\" before this time is up, then the\n"
    "  turn ends automatically.  Zero means there is no timeout." },

  { "saveturns", &game.save_nturns,
    SSET_META, SSET_SERVER_ONLY,
    0, 200, 10,
    "Turns per auto-save",
    "  The game will be automatically saved per this number of turns.\n"
    "  Zero means never auto-save." },

  { "scorelog", &game.scorelog,
    SSET_META, SSET_SERVER_ONLY,
    GAME_MIN_SCORELOG, GAME_MAX_SCORELOG, GAME_DEFAULT_SCORELOG,
    "Whether to log player statistics",
    "  If this is set to 1, player statistics are appended to the file\n"
    "  \"civscore.log\" every turn.  These statistics can be used to create\n"
    "  power graphs after the game." },

  { "gamelog", &gamelog_level,
    SSET_META, SSET_SERVER_ONLY,
    0, 40, 20,
    "Detail level for logging game events",
    "  Only applies if the game log feature is enabled (with the -g command line\n"
    "  option).  Levels: 0=no logging, 20=standard logging, 30=detailed logging,\n"
    "  40=debuging logging." },
  
  { NULL, NULL,
    SSET_LAST, SSET_SERVER_ONLY,
    0, 0, 0,
    NULL, NULL }
};

/********************************************************************
Returns whether the specified server setting (option) can currently
be changed.
*********************************************************************/
int sset_is_changeable(int idx)
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
    fprintf(stderr, "Unexpected case %d in %s line %d\n",
	    op->sclass, __FILE__, __LINE__);
    return 0;
  }
}

/********************************************************************
Returns whether the specified server setting (option) should be
sent to the client.
*********************************************************************/
int sset_is_to_client(int idx)
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
PlayerNameStatus test_player_name(char* name)
{
  int len = strlen(name);

  if (!len) {
      return PNameEmpty;
  } else if (len > 9) {
      return PNameTooLong;
  }

  return PNameOk;
}

static char horiz_line[] = "------------------------------------------------------------------------------";

void show_prompt()
{
  static int first=1;

  if (first)
    printf("\nGet a list of the available commands with 'h'.\n");

  printf("> ");
  fflush(stdout);

  first = 0;
}

/**************************************************************************
...
**************************************************************************/
void meta_command(char *arg)
{
  strncpy(metaserver_info_line, arg, 256);
  metaserver_info_line[256-1]='\0';
  if (send_server_info_to_metaserver(1) == 0)
    printf("Not reporting to the metaserver in this game\n");
}

/***************************************************************
 This could be in common/player if the client ever gets
 told the ai player skill levels.
***************************************************************/
const char *name_of_skill_level(int level)
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
int handicap_of_skill_level(int level)
{
  int h[11] = { -1, 0, 0, H_RATES+H_TARGETS+H_HUTS,
		0, H_RATES+H_TARGETS+H_HUTS, 0, 0,
		0, 0, 0 };
  
  assert(level>0 && level<=10);
  return h[level];
}

/**************************************************************************
Return the AI fuzziness (0 to 1000) corresponding to a given skill
level (1 to 10).  See ai_fuzzy() in common/player.c
**************************************************************************/
int fuzzy_of_skill_level(int level)
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
int expansionism_of_skill_level(int level)
{
  int x[11] = { -1, 100, 100, 30/*easy*/, 100, 100, 100, 100, 100, 100, 100 };
  
  assert(level>0 && level<=10);
  return x[level];
}

/**************************************************************************
...
**************************************************************************/
void save_command(char *arg)
{
  struct section_file file;
  
  if (server_state==SELECT_RACES_STATE)
    {
      printf("Please wait until the game has started to save.\n");
      return;
    }

  section_file_init(&file);
  
  game_save(&file);

  if(!section_file_save(&file, arg))
    printf("Failed saving game as %s\n", arg);
  else
    printf("Game saved as %s\n", arg);

  section_file_free(&file);
}

/**************************************************************************
...
**************************************************************************/
void toggle_ai_player(char *arg)
{
  struct player *pplayer;

  if (test_player_name(arg) != PNameOk) {
      puts("Name is either empty or too long, so it cannot be an AI.");
      return;
  }

  pplayer=find_player_by_name(arg);
  if (!pplayer) {
    puts("No player by that name.");
    return;
  }
  pplayer->ai.control = !pplayer->ai.control;
  if (pplayer->ai.control) {
    notify_player(0, "Game: %s is now AI-controlled.", pplayer->name);
    printf("%s is now under AI control.\n",pplayer->name);
    if (pplayer->ai.skill_level==0) {
      pplayer->ai.skill_level = game.skill_level;
    }
    /* Set the skill level explicitly, because eg: the player skill
       level could have been set as AI, then toggled, then saved,
       then reloaded. */ 
    set_ai_level(arg, pplayer->ai.skill_level);
  } else {
    notify_player(0, "Game: %s is now human.", pplayer->name);
    printf("%s is now under human control.\n",pplayer->name);
  }
  send_player_info(pplayer,0);
}

/**************************************************************************
...
**************************************************************************/
void create_ai_player(char *arg)
{
  struct player *pplayer;
  PlayerNameStatus PNameStatus;
   
  if (server_state!=PRE_GAME_STATE)
  {
    puts ("Can't add AI players once the game has begun.");
    return;
  }

  if (game.nplayers==game.max_players) 
  {
    puts ("Can't add more players, server is full.");
    return;
  }

  if ((PNameStatus = test_player_name(arg)) == PNameEmpty)
  {
    puts("Can't use an empty name.");
    return;
  }

  if (PNameStatus == PNameTooLong)
  {
    puts("The name exceeds the maximum of 9 chars.");
    return;
  }

  if ((pplayer=find_player_by_name(arg)))
  {
    puts("A player already exists by that name.");
    return;
  }

  accept_new_player(arg, NULL);
  pplayer = find_player_by_name(arg);
  if (!pplayer)
  {
    printf ("Error creating new ai player: %s\n", arg);
    return;
  }

  pplayer->ai.control = !pplayer->ai.control;
  pplayer->ai.skill_level = game.skill_level;
  pplayer->is_connected=0;
  notify_player(0, "Game: %s has been added as an AI-controlled player.",
		pplayer->name);
}


/**************************************************************************
...
**************************************************************************/
void remove_player(char *arg)
{
  struct player *pplayer;

  if (test_player_name(arg) != PNameOk) {
      puts("Name is either empty or too long, so it cannot be a player.");
      return;
  }

  pplayer=find_player_by_name(arg);
  
  if(!pplayer) {
    puts("No player by that name.");
    return;
  }

  server_remove_player(pplayer);
}

int lookup_cmd(char *find)
{
  int i;
  for (i=0;settings[i].name!=NULL;i++) 
    if (!strcmp(find, settings[i].name)) return i;
  
  return -1;
}

void explain_option(char *str)
{
  char command[512], *cptr_s, *cptr_d;
  int cmd,i;

  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);
  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  if (*command) {
    cmd=lookup_cmd(command);
    if (cmd==-1) {
      puts("No explanation for that yet.");
      return;
    }
    else {
      struct settings_s *op = &settings[cmd];

      printf("Option: %s\n", op->name);
      printf("Description: %s.\n", op->short_help);
      if(op->extra_help && strcmp(op->extra_help,"")!=0) {
	puts(op->extra_help);
      }
      printf("Status: %s\n", (sset_is_changeable(cmd)
			      ? "changeable" : "fixed"));
      if (SETTING_IS_INT(op)) {
	printf("Value: %d, Minimum: %d, Default: %d, Maximum: %d\n",
	       *(op->value), op->min_value, op->default_value, op->max_value);
      } else {
	printf("Value: \"%s\", Default: \"%s\"\n",
	       op->svalue, op->default_svalue);
      }
    }
  } else {
    puts(horiz_line);
    puts("Explanations are available for the following server options:");
    puts(horiz_line);
    for (i=0;settings[i].name;i++) {
      printf("%-19s%c",settings[i].name, ((i+1)%4) ? ' ' : '\n'); 
    }
    if ((i)%4!=0) puts("");
    puts(horiz_line);
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
  buffer[0]=0;
  sprintf(title, "%-20svalue  (min , max)", "Option");

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
  if(0) freelog(LOG_DEBUG, "report_server_options buffer len %d", i);
  page_player(pplayer, title, buffer);
}

void set_ai_level_direct(struct player *pplayer, int level)
{
  pplayer->ai.handicap = handicap_of_skill_level(level);
  pplayer->ai.fuzzy = fuzzy_of_skill_level(level);
  pplayer->ai.expand = expansionism_of_skill_level(level);
  pplayer->ai.skill_level = level;
  printf("%s is now %s.\n", pplayer->name, name_of_skill_level(level));
}

void set_ai_level(char *name, int level)
{
  struct player *pplayer;
  int i;

  if (test_player_name(name) == PNameTooLong) {
    puts("Name is too long, so it cannot be a player.");
    return;
  }

  assert(level > 0 && level < 11);

  pplayer=find_player_by_name(name);

  if (pplayer) {
    if (pplayer->ai.control) {
      set_ai_level_direct(pplayer, level);
    } else {
      printf("%s is not controlled by the AI.\n", pplayer->name);
    }
  } else if(test_player_name(name) == PNameEmpty) {
    for (i = 0; i < game.nplayers; i++) {
      pplayer = get_player(i);
      if (pplayer->ai.control) {
	set_ai_level_direct(pplayer, level);
      }
    }
    printf("Setting game.skill_level to %d.\n", level);
    game.skill_level = level;
  } else {
    printf("%s is not the name of any player.\n", name);
  }
}

void crash_and_burn(void)
{
  printf("Crashing and burning.\n");
  assert(0);
}

/******************************************************************
Print a summary of the settings and their values.
Note that most values are at most 4 digits, except seeds,
which we let overflow their columns.  (And endyear may have '-'.)
******************************************************************/
void show_command(char *str)
{
  int i, len1;
  puts(horiz_line);
  len1 = printf("%-*s  value (min , max)  ", SSET_MAX_LEN, "Option");
  puts("description");
  puts(horiz_line);
  for (i=0;settings[i].name;i++) {
    struct settings_s *op = &settings[i];
    int len;
    if (SETTING_IS_INT(op)) {
      len = printf("%-*s %c%c%-4d (%d,%d)", SSET_MAX_LEN, op->name,
		   (sset_is_changeable(i) ? '#' : ' '),
		   ((*op->value==op->default_value) ? '*' : ' '),
		   *op->value, op->min_value, op->max_value);
    } else {
      len = printf("%-*s %c%c\"%s\"", SSET_MAX_LEN, op->name,
		   (sset_is_changeable(i) ? '#' : ' '),
		   ((strcmp(op->svalue, op->default_svalue)==0) ? '*' : ' '),
		   op->svalue);
    }
    /* Line up the descriptions: */
    if(len < len1) {
      printf("%*s", (len1-len), " ");
    } else {
      printf(" ");
    }
    puts(op->short_help);
  }
  puts(horiz_line);
  puts("* means that it's the default for that option");
  puts("# means the option may be changed");
  puts(horiz_line);
}

void set_command(char *str) 
{
  char command[512], arg[512], *cptr_s, *cptr_d;
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

  cmd=lookup_cmd(command);
  if (cmd==-1) {
    puts("Undefined argument.  Usage: set <option> <value>.");
    return;
  }
  if (!sset_is_changeable(cmd)) {
    puts("This setting can't be modified after game has started");
    return;
  }

  op = &settings[cmd];
  
  if (SETTING_IS_INT(op)) {
    val = atoi(arg);
    if (val >= op->min_value && val <= op->max_value) {
      *(op->value) = val;
      if (sset_is_to_client(cmd)) {
	notify_player(0, "Option: %s has been set to %d.", command, val);
	/* canonify map generator settings( all of which are int ) */
	adjust_terrain_param();
      }
    } else {
      puts("Value out of range. Usage: set <option> <value>.");
    }
  } else {
    if (strlen(arg)<MAX_LENGTH_NAME) {
      strcpy(op->svalue, arg);
      if (sset_is_to_client(cmd)) {
	notify_player(0, "Option: %s has been set to \"%s\".", command, arg);
      }
    } else {
      puts("String value too long. Usage: set <option> <value>.");
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_stdin_input(char *str)
{
  char command[512], arg[512], *cptr_s, *cptr_d;
  int i;

  /* Is it a comment or a blank line? */
  /* line is comment if the first non-whitespace character is '#': */
  for(cptr_s=str; *cptr_s && isspace(*cptr_s); cptr_s++);
  if(*cptr_s == 0 || *cptr_s == '#')
    return;
  
  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);

  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  for(; *cptr_s && isspace(*cptr_s); cptr_s++);
  strncpy(arg, cptr_s, 511);
  arg[511]='\0';

  i=strlen(arg)-1;
  while(i>0 && isspace(arg[i]))
    arg[i--]='\0';
  
  if(!strcmp("remove", command))
    remove_player(arg);
  else if(!strcmp("save", command))
    save_command(arg);
  else if(!strcmp("meta", command))
    meta_command(arg);
  else if(!strcmp("h", command))
    show_help();
  else if(!strcmp("help", command))
    show_help();
  else if(!strcmp("l", command))
    show_players();
  else if (!strcmp("ai", command))
    toggle_ai_player(arg);
  else if (!strcmp("create", command))
    create_ai_player(arg);
  else if (!strcmp("crash", command))
    crash_and_burn();
  else if (!strcmp("log", command)) /* undocumented */
    freelog(LOG_NORMAL, "%s", arg);
  else if (!strcmp("easy", command))
    set_ai_level(arg, 3);
  else if (!strcmp("normal", command))
    set_ai_level(arg, 5);
  else if (!strcmp("hard", command))
    set_ai_level(arg, 7);
  else if(!strcmp("q", command))
    quit_game();
  else if(!strcmp("c", command))
    cut_player_connection(arg);
  else if (!strcmp("show",command)) 
    show_command(arg);
  else if (!strcmp("explain",command)) 
    explain_option(arg);
  else if (!strcmp("set", command)) 
    set_command(arg);
  else if(!strcmp("score", command)) {
    if(server_state==RUN_GAME_STATE)
      show_ending();
    else
      printf("The game must be running, before you can see the score.\n");
  }
  else if(server_state==PRE_GAME_STATE) {
    int plrs=0;
    int i;
    if(!strcmp("s", command)) {
      for (i=0;i<game.nplayers;i++) {
	if (game.players[i].conn || game.players[i].ai.control) plrs++ ;
      }
      if (plrs<game.min_players) 
	printf("Not enough players, game will not start.\n");
      else 
	start_game();
    }
    else
      printf("Unknown command.  Try 'h' for help.\n");
  }
  else
    printf("Unknown command.  Try 'h' for help.\n");  
}

/**************************************************************************
...
**************************************************************************/
void cut_player_connection(char *playername)
{
  struct player *pplayer;

  if (test_player_name(playername) != PNameOk) {
      puts("Name is either empty or too long, so it cannot be a player.");
      return;
  }

  pplayer=find_player_by_name(playername);

  if(pplayer && pplayer->conn) {
    freelog(LOG_NORMAL, "cutting connection to %s", playername);
    close_connection(pplayer->conn);
    pplayer->conn=NULL;
  }
  else
    puts("uh, no such player connected");
}

/**************************************************************************
...
**************************************************************************/
void quit_game(void)
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
void show_help(void)
{
  puts("Available commands: (P=player, M=message, F=file, T=topic)");
  puts(horiz_line);
  puts("h         - this help");
  puts("explain   - help on server options");
  puts("explain T - help on a particular server option");
  puts("l         - list players");
  puts("q         - quit game and shutdown server");
  puts("c P       - cut connection to player");
  puts("remove P  - fully remove player from game");
  puts("score     - show current score");
  puts("save F    - save game as file F");
  puts("show      - list server options");
  puts("meta M    - Set meta-server infoline to M");
  puts("ai P      - toggles AI on player");
  puts("create P  - creates an AI player");
  puts("easy P    - AI player will be easy");
  puts("easy      - All AI players will be easy");
  puts("normal P  - AI player will be normal");
  puts("normal    - All AI players will be normal");
  puts("hard P    - AI player will be hard");
  puts("hard      - All AI players will be hard");
  puts("set       - set options");
  if(server_state==PRE_GAME_STATE) {
    puts("s         - start game");
  }
}

/**************************************************************************
...
**************************************************************************/
void show_players(void)
{
  int i;
  
  puts("List of players:");
  puts(horiz_line);

  if (game.nplayers == 0)
    printf("<no players>\n");
  else
  {
    for(i=0; i<game.nplayers; i++) {
      printf("%s", game.players[i].name);
      if (game.players[i].ai.control) {
	printf(" (AI, %s)",
	       name_of_skill_level(game.players[i].ai.skill_level));
	if (game.players[i].conn)
	  printf(" observer connected from %s\n", game.players[i].addr);
	else
	  printf("\n");
      }
      else {
	if (game.players[i].conn)
	  printf(" is connected from %s\n", game.players[i].addr); 
	else
	  printf(" is not connected\n");
      }
    }
  }
  puts(horiz_line);
}
