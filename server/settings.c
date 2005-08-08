/********************************************************************** 
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Project
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

#include "fcintl.h"

#include "map.h"

#include "gamelog.h"
#include "report.h"
#include "settings.h"
#include "stdinhand.h"

/* Category names must match the values in enum sset_category. */
const char *sset_category_names[] = {N_("Geological"),
				     N_("Ecological"),
				     N_("Sociological"),
				     N_("Economic"),
				     N_("Military"),
				     N_("Scientific"),
				     N_("Internal"),
				     N_("Networking")};

/* Level names must match the values in enum sset_level. */
const char *sset_level_names[] = {N_("None"),
				  N_("All"),
				  N_("Vital"),
				  N_("Situational"),
				  N_("Rare")};
const int OLEVELS_NUM = ARRAY_SIZE(sset_level_names);


/**************************************************************************
  A callback invoked when autotoggle is set.
**************************************************************************/
static bool autotoggle_callback(bool value, const char **reject_message)
{
  reject_message = NULL;
  if (!value) {
    return TRUE;
  }

  players_iterate(pplayer) {
    if (!pplayer->ai.control && !pplayer->is_connected) {
      toggle_ai_player_direct(NULL, pplayer);
    }
  } players_iterate_end;

  return TRUE;
}

/*************************************************************************
  Verify that a given allowtake string is valid.  See
  game.allow_take.
*************************************************************************/
static bool allowtake_callback(const char *value, const char **error_string)
{
  int len = strlen(value), i;
  bool havecharacter_state = FALSE;

  /* We check each character individually to see if it's valid.  This
   * does not check for duplicate entries.
   *
   * We also track the state of the machine.  havecharacter_state is
   * true if the preceeding character was a primary label, e.g.
   * NHhAadb.  It is false if the preceeding character was a modifier
   * or if this is the first character. */

  for (i = 0; i < len; i++) {
    /* Check to see if the character is a primary label. */
    if (strchr("HhAadbOo", value[i])) {
      havecharacter_state = TRUE;
      continue;
    }

    /* If we've already passed a primary label, check to see if the
     * character is a modifier. */
    if (havecharacter_state && strchr("1234", value[i])) {
      havecharacter_state = FALSE;
      continue;
    }

    /* Looks like the character was invalid. */
    *error_string = _("Allowed take string contains invalid\n"
		      "characters.  Try \"help allowtake\".");
    return FALSE;
  }

  /* All characters were valid. */
  *error_string = NULL;
  return TRUE;
}

/*************************************************************************
  Verify that a given startunits string is valid.  See
  game.start_units.
*************************************************************************/
static bool startunits_callback(const char *value, const char **error_string)
{
  int len = strlen(value), i;
  bool have_founder = FALSE;

  /* We check each character individually to see if it's valid, and
   * also make sure there is at least one city founder. */

  for (i = 0; i < len; i++) {
    /* Check for a city founder */
    if (value[i] == 'c') {
      have_founder = TRUE;
      continue;
    }
    if (strchr("cwxksdDaA", value[i])) {
      continue;
    }

    /* Looks like the character was invalid. */
    *error_string = _("Starting units string contains invalid\n"
		      "characters.  Try \"help startunits\".");
    return FALSE;
  }

  if (!have_founder) {
    *error_string = _("Starting units string does not contain\n"
		      "at least one city founder.  Try \n"
		      "\"help startunits\".");
    return FALSE;
  }
  /* All characters were valid. */
  *error_string = NULL;
  return TRUE;
}

/*************************************************************************
  Verify that a given maxplayers string is valid.
*************************************************************************/
static bool maxplayers_callback(int value, const char **error_string)
{
  if (value < game.nplayers) {
    *error_string =_("Number of players is higher than requested value; "
		     "Keeping old value.");
    return FALSE;
  }

  error_string = NULL;
  return TRUE;
}

#define GEN_BOOL(name, value, sclass, scateg, slevel, to_client,	\
		 short_help, extra_help, func, default)			\
  {name, sclass, to_client, short_help, extra_help, SSET_BOOL,		\
      scateg, slevel, &value, default, func,				\
      NULL, 0, NULL, 0, 0,						\
      NULL, NULL, NULL, 0},

#define GEN_INT(name, value, sclass, scateg, slevel, to_client,		\
		short_help, extra_help, func, min, max, default)	\
  {name, sclass, to_client, short_help, extra_help, SSET_INT,		\
      scateg, slevel,							\
      NULL, FALSE, NULL,						\
      &value, default, func, min, max,					\
      NULL, NULL, NULL, 0},

#define GEN_STRING(name, value, sclass, scateg, slevel, to_client,	\
		   short_help, extra_help, func, default)		\
  {name, sclass, to_client, short_help, extra_help, SSET_STRING,	\
      scateg, slevel,							\
      NULL, FALSE, NULL,						\
      NULL, 0, NULL, 0, 0,						\
      value, default, func, sizeof(value)},

#define GEN_END							\
  {NULL, SSET_LAST, SSET_SERVER_ONLY, NULL, NULL, SSET_INT,	\
      SSET_NUM_CATEGORIES, SSET_NONE,				\
      NULL, FALSE, NULL,					\
      NULL, 0, NULL, 0, 0,					\
      NULL, NULL, NULL},

struct settings_s settings[] = {

  /* These should be grouped by sclass */
  
  /* Map size parameters: adjustable if we don't yet have a map */  
  GEN_INT("size", map.size, SSET_MAP_SIZE,
	  SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
          N_("Map size (in thousands of tiles)"),
          N_("This value is used to determine the map dimensions.\n"
             "  size = 4 is a normal map of 4,000 tiles (default)\n"
             "  size = 20 is a huge map of 20,000 tiles"), NULL,
          MAP_MIN_SIZE, MAP_MAX_SIZE, MAP_DEFAULT_SIZE)
  GEN_INT("topology", map.topology_id, SSET_MAP_SIZE,
	  SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Map topology index"),
	  /* TRANS: do not edit the ugly ASCII art */
	  N_("Freeciv maps are always two-dimensional. They may wrap at "
	     "the north-south and east-west directions to form a flat map, "
	     "a cylinder, or a torus (donut). Individual tiles may be "
	     "rectangular or hexagonal, with either a classic or isometric "
	     "alignment - this should be set based on the tileset being "
	     "used.\n"
             "   0 Flat Earth (unwrapped)\n"
             "   1 Earth (wraps E-W)\n"
             "   2 Uranus (wraps N-S)\n"
             "   3 Donut World (wraps N-S, E-W)\n"
	     "   4 Flat Earth (isometric)\n"
	     "   5 Earth (isometric)\n"
	     "   6 Uranus (isometric)\n"
	     "   7 Donut World (isometric)\n"
	     "   8 Flat Earth (hexagonal)\n"
	     "   9 Earth (hexagonal)\n"
	     "  10 Uranus (hexagonal)\n"
	     "  11 Donut World (hexagonal)\n"
	     "  12 Flat Earth (iso-hex)\n"
	     "  13 Earth (iso-hex)\n"
	     "  14 Uranus (iso-hex)\n"
	     "  15 Donut World (iso-hex)\n"
	     "Classic rectangular:       Isometric rectangular:\n"
	     "      _________               /\\/\\/\\/\\/\\ \n"
	     "     |_|_|_|_|_|             /\\/\\/\\/\\/\\/ \n"
	     "     |_|_|_|_|_|             \\/\\/\\/\\/\\/\\\n"
	     "     |_|_|_|_|_|             /\\/\\/\\/\\/\\/ \n"
	     "                             \\/\\/\\/\\/\\/  \n"
	     "Hex tiles:                 Iso-hex:\n"
	     "  /\\/\\/\\/\\/\\/\\               _   _   _   _   _       \n"
	     "  | | | | | | |             / \\_/ \\_/ \\_/ \\_/ \\      \n"
	     "  \\/\\/\\/\\/\\/\\/\\             \\_/ \\_/ \\_/ \\_/ \\_/  \n"
	     "   | | | | | | |            / \\_/ \\_/ \\_/ \\_/ \\      \n"
	     "   \\/\\/\\/\\/\\/\\/             \\_/ \\_/ \\_/ \\_/ \\_/    \n"
          ), NULL,
	  MAP_MIN_TOPO, MAP_MAX_TOPO, MAP_DEFAULT_TOPO)

  /* Map generation parameters: once we have a map these are of historical
   * interest only, and cannot be changed.
   */
  GEN_INT("generator", map.generator,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_VITAL,  SSET_TO_CLIENT,
	  N_("Method used to generate map"),
	  N_("0 = Scenario map - no generator;\n"
	     "1 = Fully random height generator;              [4]\n"
	     "2 = Pseudo-fractal height generator;             [3]\n"
	     "3 = Island-based generator (fairer but boring)   [1]\n"
	     "\n"
	     "Numbers in [] give the default values for placement of "
	     "starting positions.  If the default value of startpos is "
	     "used then a startpos setting will be chosen based on the "
	     "generator.  See the \"startpos\" setting."), NULL,
	  MAP_MIN_GENERATOR, MAP_MAX_GENERATOR, MAP_DEFAULT_GENERATOR)

  GEN_INT("startpos", map.startpos,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_VITAL,  SSET_TO_CLIENT,
	  N_("Method used to choose start positions"),
	  N_("0 = Generator's choice.  Selecting this setting means\n"
	     "    the default value will be picked based on the generator\n"
	     "    chosen.  See the \"generator\" setting.\n"
	     "1 = Try to place one player per continent.\n"
	     "2 = Try to place two players per continent.\n"
	     "3 = Try to place all players on a single continent.\n"
	     "4 = Place players depending on size of continents.\n"
	     "Note: generators try to create the right number of continents "
	     "for the choice of start pos and to the number of players"),
	  NULL, MAP_MIN_STARTPOS, MAP_MAX_STARTPOS, MAP_DEFAULT_STARTPOS)

  GEN_BOOL("tinyisles", map.tinyisles,
	   SSET_MAP_GEN, SSET_GEOLOGY, SSET_RARE, SSET_TO_CLIENT,
	   N_("Presence of 1x1 islands"),
	   N_("0 = no 1x1 islands; 1 = some 1x1 islands"), NULL,
	   MAP_DEFAULT_TINYISLES)

  GEN_BOOL("separatepoles", map.separatepoles,
	   SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Whether the poles are separate continents"),
	   N_("0 = continents may attach to poles; 1 = poles will "
	      "usually be separate"), NULL, 
	   MAP_DEFAULT_SEPARATE_POLES)

  GEN_BOOL("alltemperate", map.alltemperate, 
           SSET_MAP_GEN, SSET_GEOLOGY, SSET_RARE, SSET_TO_CLIENT,
	   N_("All the map is temperate"),
	   N_("0 = normal Earth-like planet; 1 = all-temperate planet "),
	   NULL, MAP_DEFAULT_ALLTEMPERATE)

  GEN_INT("temperature", map.temperature,
 	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
 	  N_("Average temperature of the planet"),
 	  N_("Small values will give a cold map, while larger values will "
             "give a hotter map.\n"
	     "\n"
	     "100 means a very dry and hot planet with no polar arctic "
	     "zones, only tropical and dry zones.\n\n"
	     "70 means a hot planet with little polar ice.\n\n"
             "50 means a temperate planet with normal polar, cold, "
	     "temperate, and tropical zones; a desert zone overlaps "
	     "tropical and temperate zones.\n\n"
	     "30 means a cold planet with small tropical zones.\n\n"
	     "0 means a very cold planet with large polar zones and no "
	     "tropics"), 
          NULL,
  	  MAP_MIN_TEMPERATURE, MAP_MAX_TEMPERATURE, MAP_DEFAULT_TEMPERATURE)
 
  GEN_INT("landmass", map.landpercent,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Percentage of the map that is land"),
	  N_("This setting gives the approximate percentage of the map "
	     "that will be made into land."), NULL,
	  MAP_MIN_LANDMASS, MAP_MAX_LANDMASS, MAP_DEFAULT_LANDMASS)

  GEN_INT("steepness", map.steepness,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of hills/mountains"),
	  N_("Small values give flat maps, while higher values give a "
	     "steeper map with more hills and mountains."), NULL,
	  MAP_MIN_STEEPNESS, MAP_MAX_STEEPNESS, MAP_DEFAULT_STEEPNESS)

  GEN_INT("wetness", map.wetness,
 	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
 	  N_("Amount of water on lands"), 
	  N_("Small values mean lots of dry, desert-like land; "
	     "higher values give a wetter map with more swamps, "
	     "jungles, and rivers."), NULL, 
 	  MAP_MIN_WETNESS, MAP_MAX_WETNESS, MAP_DEFAULT_WETNESS)

  GEN_INT("mapseed", map.seed,
	  SSET_MAP_GEN, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Map generation random seed"),
	  N_("The same seed will always produce the same map; "
	     "for zero (the default) a seed will be chosen based on "
	     "the time to give a random map. This setting is usually "
	     "only of interest while debugging the game."), NULL, 
	  MAP_MIN_SEED, MAP_MAX_SEED, MAP_DEFAULT_SEED)

  /* Map additional stuff: huts and specials.  gameseed also goes here
   * because huts and specials are the first time the gameseed gets used (?)
   * These are done when the game starts, so these are historical and
   * fixed after the game has started.
   */
  GEN_INT("gameseed", game.seed,
	  SSET_MAP_ADD, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Game random seed"),
	  N_("For zero (the default) a seed will be chosen based "
	     "on the time. This setting is usually "
	     "only of interest while debugging the game"), NULL, 
	  GAME_MIN_SEED, GAME_MAX_SEED, GAME_DEFAULT_SEED)

  GEN_INT("specials", map.riches,
	  SSET_MAP_ADD, SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Amount of \"special\" resource squares"), 
	  N_("Special resources improve the basic terrain type they "
	     "are on. The server variable's scale is parts per "
	     "thousand."), NULL,
	  MAP_MIN_RICHES, MAP_MAX_RICHES, MAP_DEFAULT_RICHES)

  GEN_INT("huts", map.huts,
	  SSET_MAP_ADD, SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Amount of huts (minor tribe villages)"),
	  N_("This setting gives the exact number of huts that will be "
	     "placed on the entire map. Huts are small tribal villages "
	     "that may be investiged by units."), NULL,
	  MAP_MIN_HUTS, MAP_MAX_HUTS, MAP_DEFAULT_HUTS)

  /* Options affecting numbers of players and AI players.  These only
   * affect the start of the game and can not be adjusted after that.
   * (Actually, minplayers does also affect reloads: you can't start a
   * reload game until enough players have connected (or are AI).)
   */
  GEN_INT("minplayers", game.min_players,
	  SSET_PLAYERS, SSET_INTERNAL, SSET_VITAL,
          SSET_TO_CLIENT,
	  N_("Minimum number of players"),
	  N_("There must be at least this many players (connected "
	     "human players or AI players) before the game can start."),
	  NULL,
	  GAME_MIN_MIN_PLAYERS, GAME_MAX_MIN_PLAYERS, GAME_DEFAULT_MIN_PLAYERS)

  GEN_INT("maxplayers", game.max_players,
	  SSET_PLAYERS, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Maximum number of players"),
          N_("The maximal number of human and AI players who can be in "
             "the game. When this number of players are connected in "
             "the pregame state, any new players who try to connect "
             "will be rejected."), maxplayers_callback,
	  GAME_MIN_MAX_PLAYERS, GAME_MAX_MAX_PLAYERS, GAME_DEFAULT_MAX_PLAYERS)

  GEN_INT("aifill", game.aifill,
	  SSET_PLAYERS, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Total number of players (including AI players)"),
	  N_("If there are fewer than this many players when the "
	     "game starts, extra AI players will be created to "
	     "increase the total number of players to the value of "
	     "this option."), NULL, 
	  GAME_MIN_AIFILL, GAME_MAX_AIFILL, GAME_DEFAULT_AIFILL)

  /* Game initialization parameters (only affect the first start of the game,
   * and not reloads).  Can not be changed after first start of game.
   */
  GEN_STRING("startunits", game.start_units,
	     SSET_GAME_INIT, SSET_SOCIOLOGY, SSET_VITAL, SSET_TO_CLIENT,
             N_("List of players' initial units"),
             N_("This should be a string of characters, each of which "
		"specifies a unit role. There must be at least one city "
		"founder in the string. The characters and their "
		"meanings are:\n"
		"    c   = City founder (eg., Settlers)\n"
		"    w   = Terrain worker (eg., Engineers)\n"
		"    x   = Explorer (eg., Explorer)\n"
		"    k   = Gameloss (eg., King)\n"
		"    s   = Diplomat (eg., Diplomat)\n"
		"    d   = Ok defense unit (eg., Warriors)\n"
		"    D   = Good defense unit (eg., Phalanx)\n"
		"    a   = Fast attack unit (eg., Horsemen)\n"
		"    A   = Strong attack unit (eg., Catapult)\n"),
		startunits_callback, GAME_DEFAULT_START_UNITS)

  GEN_INT("dispersion", game.dispersion,
	  SSET_GAME_INIT, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Area where initial units are located"),
	  N_("This is the radius within "
	     "which the initial units are dispersed."), NULL,
	  GAME_MIN_DISPERSION, GAME_MAX_DISPERSION, GAME_DEFAULT_DISPERSION)

  GEN_INT("gold", game.gold,
	  SSET_GAME_INIT, SSET_ECONOMICS, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Starting gold per player"), 
	  N_("At the beginning of the game, each player is given this "
	     "much gold."), NULL,
	  GAME_MIN_GOLD, GAME_MAX_GOLD, GAME_DEFAULT_GOLD)

  GEN_INT("techlevel", game.tech,
	  SSET_GAME_INIT, SSET_SCIENCE, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Number of initial techs per player"), 
	  N_("At the beginning of the game, each player is given this "
	     "many technologies. The technologies chosen are random for "
	     "each player."), NULL,
	  GAME_MIN_TECHLEVEL, GAME_MAX_TECHLEVEL, GAME_DEFAULT_TECHLEVEL)

  GEN_INT("researchcost", game.researchcost,
	  SSET_RULES, SSET_SCIENCE, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Points required to gain a new tech"),
	  N_("This affects how quickly players can research new "
	     "technology. Doubling its value will make all technologies "
	     "take twice as long to research."), NULL,
	  GAME_MIN_RESEARCHCOST, GAME_MAX_RESEARCHCOST, 
	  GAME_DEFAULT_RESEARCHCOST)

  GEN_INT("techpenalty", game.techpenalty,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Percentage penalty when changing tech"),
	  N_("If you change your current research technology, and you have "
	     "positive research points, you lose this percentage of those "
	     "research points. This does not apply if you have just gained "
	     "a technology this turn."), NULL,
	  GAME_MIN_TECHPENALTY, GAME_MAX_TECHPENALTY,
	  GAME_DEFAULT_TECHPENALTY)

  GEN_INT("diplcost", game.diplcost,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Penalty when getting tech from treaty"),
	  N_("For each technology you gain from a diplomatic treaty, you "
	     "lost research points equal to this percentage of the cost to "
	     "research a new technology. You can end up with negative "
	     "research points if this is non-zero."), NULL, 
	  GAME_MIN_DIPLCOST, GAME_MAX_DIPLCOST, GAME_DEFAULT_DIPLCOST)

  GEN_INT("conquercost", game.conquercost,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Penalty when getting tech from conquering"),
	  N_("For each technology you gain by conquering an enemy city, you "
	     "lose research points equal to this percentage of the cost "
	     "to research a new technology. You can end up with negative "
	     "research points if this is non-zero."), NULL,
	  GAME_MIN_CONQUERCOST, GAME_MAX_CONQUERCOST,
	  GAME_DEFAULT_CONQUERCOST)

  GEN_INT("freecost", game.freecost,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Penalty when getting a free tech"),
	  N_("For each technology you gain \"for free\" (other than "
	     "covered by diplcost or conquercost: specifically, from huts "
	     "or from Great Library effects), you lose research points "
	     "equal to this percentage of the cost to research a new "
	     "technology. You can end up with negative research points if "
	     "this is non-zero."), 
	  NULL, 
	  GAME_MIN_FREECOST, GAME_MAX_FREECOST, GAME_DEFAULT_FREECOST)

  GEN_INT("foodbox", game.foodbox,
	  SSET_RULES, SSET_ECONOMICS, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Food required for a city to grow"),
	  N_("This is the base amount of food required to grow a city. "
	     "This value is multiplied by another factor that comes from "
	     "the ruleset and is dependent on the size of the city."),
	  NULL,
	  GAME_MIN_FOODBOX, GAME_MAX_FOODBOX, GAME_DEFAULT_FOODBOX)

  GEN_INT("aqueductloss", game.aqueductloss,
	  SSET_RULES, SSET_ECONOMICS, SSET_RARE, SSET_TO_CLIENT,
	  N_("Percentage food lost when need aqueduct"),
	  N_("If a city would expand, but it can't because it needs "
	     "an Aqueduct (or Sewer System), it loses this percentage "
	     "of its foodbox (or half that amount if it has a "
	     "Granary)."), NULL, 
	  GAME_MIN_AQUEDUCTLOSS, GAME_MAX_AQUEDUCTLOSS, 
	  GAME_DEFAULT_AQUEDUCTLOSS)

  /* Notradesize and fulltradesize used to have callbacks to prevent them
   * from being set illegally (notradesize > fulltradesize).  However this
   * provided a problem when setting them both through the client's settings
   * dialog, since they cannot both be set atomically.  So the callbacks were
   * removed and instead the game now knows how to deal with invalid
   * settings. */
  GEN_INT("fulltradesize", game.fulltradesize,
	  SSET_RULES, SSET_ECONOMICS, SSET_RARE, SSET_TO_CLIENT,
	  N_("Minimum city size to get full trade"),
	  N_("There is a trade penalty in all cities smaller than this. "
	     "The penalty is 100% (no trade at all) for sizes up to "
	     "notradesize, and decreases gradually to 0% (no penalty "
	     "except the normal corruption) for size=fulltradesize. "
	     "See also notradesize."), NULL, 
	  GAME_MIN_FULLTRADESIZE, GAME_MAX_FULLTRADESIZE, 
	  GAME_DEFAULT_FULLTRADESIZE)

  GEN_INT("notradesize", game.notradesize,
	  SSET_RULES, SSET_ECONOMICS, SSET_RARE, SSET_TO_CLIENT,
	  N_("Maximum size of a city without trade"),
	  N_("Cities do not produce any trade at all unless their size "
	     "is larger than this amount. The produced trade increases "
	     "gradually for cities larger than notradesize and smaller "
	     "than fulltradesize. See also fulltradesize."), NULL,
	  GAME_MIN_NOTRADESIZE, GAME_MAX_NOTRADESIZE,
	  GAME_DEFAULT_NOTRADESIZE)

  GEN_INT("unhappysize", game.unhappysize,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("City size before people become unhappy"),
	  N_("Before other adjustments, the first unhappysize citizens in a "
	     "city are content, and subsequent citizens are unhappy. "
	     "See also cityfactor."), NULL,
	  GAME_MIN_UNHAPPYSIZE, GAME_MAX_UNHAPPYSIZE,
	  GAME_DEFAULT_UNHAPPYSIZE)

  GEN_BOOL("angrycitizen", game.angrycitizen,
	   SSET_RULES, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Whether angry citizens are enabled"),
	  N_("Introduces angry citizens like in civilization II. Angry "
	     "citizens have to become unhappy before any other class "
	     "of citizens may be considered. See also unhappysize, "
	     "cityfactor and governments."), NULL, 
	  GAME_DEFAULT_ANGRYCITIZEN)

  GEN_INT("cityfactor", game.cityfactor,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Number of cities for higher unhappiness"),
	  N_("When the number of cities a player owns is greater than "
	     "cityfactor, one extra citizen is unhappy before other "
	     "adjustments; see also unhappysize. This assumes a "
	     "Democracy; for other governments the effect occurs at "
	     "smaller numbers of cities."), NULL, 
	  GAME_MIN_CITYFACTOR, GAME_MAX_CITYFACTOR, GAME_DEFAULT_CITYFACTOR)

  GEN_INT("citymindist", game.citymindist,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Minimum distance between cities"),
	  N_("When a player founds a new city, it is checked if there is "
	     "no other city in citymindist distance. For example, if "
	     "citymindist is 3, there have to be at least two empty "
	     "fields between two cities in every direction. If set "
	     "to 0 (default), the ruleset value will be used."),
	  NULL,
	  GAME_MIN_CITYMINDIST, GAME_MAX_CITYMINDIST,
	  GAME_DEFAULT_CITYMINDIST)

  GEN_INT("rapturedelay", game.rapturedelay,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
          N_("Number of turns between rapture effect"),
          N_("Sets the number of turns between rapture growth of a city. "
             "If set to n a city will grow after celebrating for n+1 "
	     "turns."),
	  NULL,
          GAME_MIN_RAPTUREDELAY, GAME_MAX_RAPTUREDELAY,
          GAME_DEFAULT_RAPTUREDELAY)

  GEN_INT("razechance", game.razechance,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Chance for conquered building destruction"),
	  N_("When a player conquers a city, each city improvement has this "
	     "percentage chance to be destroyed."), NULL, 
	  GAME_MIN_RAZECHANCE, GAME_MAX_RAZECHANCE, GAME_DEFAULT_RAZECHANCE)

  GEN_INT("civstyle", game.civstyle,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
          N_("civstyle is an obsolete setting"),
          N_("This setting is obsolete; it does nothing in the current "
	     "version. It will be removed from future versions."), NULL,
	  GAME_MIN_CIVSTYLE, GAME_MAX_CIVSTYLE, GAME_DEFAULT_CIVSTYLE)

  GEN_INT("occupychance", game.occupychance,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Chance of moving into tile after attack"),
	  N_("If set to 0, combat is Civ1/2-style (when you attack, "
	     "you remain in place). If set to 100, attacking units "
	     "will always move into the tile they attacked if they win "
	     "the combat (and no enemy units remain in the tile). If "
	     "set to a value between 0 and 100, this will be used as "
	     "the percent chance of \"occupying\" territory."), NULL, 
	  GAME_MIN_OCCUPYCHANCE, GAME_MAX_OCCUPYCHANCE, 
	  GAME_DEFAULT_OCCUPYCHANCE)

  GEN_INT("killcitizen", game.killcitizen,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Reduce city population after attack"),
	  N_("This flag indicates if city population is reduced "
	     "after successful attack of enemy unit, depending on "
	     "its movement type (OR-ed):\n"
	     "  1 = land\n"
	     "  2 = sea\n"
	     "  4 = heli\n"
	     "  8 = air"), NULL,
	  GAME_MIN_KILLCITIZEN, GAME_MAX_KILLCITIZEN,
	  GAME_DEFAULT_KILLCITIZEN)

  GEN_INT("wtowervision", game.watchtower_vision,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Range of vision for units in a fortress"),
	  N_("If set to 1, it has no effect. "
	     "If 2 or larger, the vision range of a unit inside a "
	     "fortress is set to this value, if the necessary invention "
	     "has been made. This invention is determined by the flag "
	     "'Watchtower' in the techs ruleset. See also wtowerevision."), 
	  NULL, 
	  GAME_MIN_WATCHTOWER_VISION, GAME_MAX_WATCHTOWER_VISION, 
	  GAME_DEFAULT_WATCHTOWER_VISION)

  GEN_INT("wtowerevision", game.watchtower_extra_vision,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Extra vision range for units in a fortress"),
	  N_("If set to 0, it has no "
	     "effect. If larger than 0, the vision range of a unit is "
	     "raised by this value, if the unit is inside a fortress "
	     "and the invention determined by the flag 'Watchtower' "
	     "in the techs ruleset has been made. Always the larger "
	     "value of wtowervision and wtowerevision will be used. "
	     "Also see wtowervision."), NULL, 
	  GAME_MIN_WATCHTOWER_EXTRA_VISION, GAME_MAX_WATCHTOWER_EXTRA_VISION, 
	  GAME_DEFAULT_WATCHTOWER_EXTRA_VISION)

  GEN_INT("borders", game.borders,
	  SSET_RULES, SSET_MILITARY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("National borders radius"),
	  N_("If this is set to greater than 0, then any land tiles "
	     "within the given radius of a city will be owned by that "
	     "nation. Special rules apply for ocean tiles or tiles within "
	     "range of more than one nation's cities."),
	  NULL,
	  GAME_MIN_BORDERS, GAME_MAX_BORDERS, GAME_DEFAULT_BORDERS)

  GEN_BOOL("happyborders", game.happyborders,
	   SSET_RULES, SSET_MILITARY, SSET_SITUATIONAL,
	   SSET_TO_CLIENT,
	   N_("Units inside borders cause no unhappiness"),
	   N_("If this is set, units will not cause unhappiness when "
	      "inside your own borders."), NULL,
	   GAME_DEFAULT_HAPPYBORDERS)

  GEN_INT("diplomacy", game.diplomacy,
	  SSET_RULES, SSET_MILITARY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Ability to do diplomacy with other players"),
	  N_("0 = default; diplomacy is enabled for everyone.\n\n"
	     "1 = diplomacy is only allowed between human players.\n\n"
	     "2 = diplomacy is only allowed between AI players.\n\n"
             "3 = diplomacy is restricted to teams.\n\n"
             "4 = diplomacy is disabled for everyone.\n\n"
             "You can always do diplomacy with players on your team."), NULL,
	  GAME_MIN_DIPLOMACY, GAME_MAX_DIPLOMACY, GAME_DEFAULT_DIPLOMACY)

  GEN_INT("citynames", game.allowed_city_names,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Allowed city names"),
	  N_("0 = There are no restrictions: players can have "
	     "multiple cities with the same names.\n\n"
	     "1 = City names have to be unique to a player: "
	     "one player can't have multiple cities with the same name.\n\n"
	     "2 = City names have to be globally unique: "
	     "all cities in a game have to have different names.\n\n"
	     "3 = Like setting 2, but a player isn't allowed to use a "
	     "default city name of another nations unless it is a default "
	     "for their nation also."),
	  NULL,
	  GAME_MIN_ALLOWED_CITY_NAMES, GAME_MAX_ALLOWED_CITY_NAMES, 
	  GAME_DEFAULT_ALLOWED_CITY_NAMES)
  
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
  GEN_INT("barbarians", game.barbarianrate,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Barbarian appearance frequency"),
	  N_("0 = no barbarians \n"
	     "1 = barbarians only in huts \n"
	     "2 = normal rate of barbarian appearance \n"
	     "3 = frequent barbarian uprising \n"
	     "4 = raging hordes, lots of barbarians"), NULL, 
	  GAME_MIN_BARBARIANRATE, GAME_MAX_BARBARIANRATE, 
	  GAME_DEFAULT_BARBARIANRATE)

  GEN_INT("onsetbarbs", game.onsetbarbarian,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Barbarian onset year"),
	  N_("Barbarians will not appear before this year."), NULL,
	  GAME_MIN_ONSETBARBARIAN, GAME_MAX_ONSETBARBARIAN, 
	  GAME_DEFAULT_ONSETBARBARIAN)

  GEN_INT("revolen", game.revolution_length,
	  SSET_RULES_FLEXIBLE, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Length in turns of revolution"),
	  N_("When changing governments, a period of anarchy lasting this "
	     "many turns will occur. "
             "Setting this value to 0 will give a random "
             "length of 1-6 turns."), NULL, 
	  GAME_MIN_REVOLUTION_LENGTH, GAME_MAX_REVOLUTION_LENGTH, 
	  GAME_DEFAULT_REVOLUTION_LENGTH)

  GEN_BOOL("fogofwar", game.fogofwar,
	   SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	   N_("Whether to enable fog of war"),
	   N_("If this is set to 1, only those units and cities within "
	      "the vision range of your own units and cities will be "
	      "revealed to you. You will not see new cities or terrain "
	      "changes in tiles not observed."), NULL, 
	   GAME_DEFAULT_FOGOFWAR)

  GEN_INT("diplchance", game.diplchance,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Chance in diplomat/spy contests"),
	  /* xgettext:no-c-format */
	  N_("A diplomatic unit acting against a city which has one or "
	     "more defending diplomatic units has a diplchance "
	     "(percent) chance to defeat each such defender. Also, the "
	     "chance of a spy returning from a successful mission is "
	     "diplchance percent (diplomats never return).  This value is "
	     "also the basic chance of success for diplomats and spies. "
	     "Defending spies are generally twice as capable as "
	     "diplomats; veteran units are 50% more capable than "
	     "non-veteran ones."), NULL, 
	  GAME_MIN_DIPLCHANCE, GAME_MAX_DIPLCHANCE, GAME_DEFAULT_DIPLCHANCE)

  GEN_BOOL("spacerace", game.spacerace,
	   SSET_RULES_FLEXIBLE, SSET_SCIENCE, SSET_VITAL, SSET_TO_CLIENT,
	   N_("Whether to allow space race"),
	   N_("If this option is set to 1, players can build spaceships."),
	   NULL, 
	   GAME_DEFAULT_SPACERACE)

  GEN_INT("civilwarsize", game.civilwarsize,
	  SSET_RULES_FLEXIBLE, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Minimum number of cities for civil war"),
	  N_("A civil war is triggered if a player has at least this "
	     "many cities and the player's capital is captured. If "
	     "this option is set to the maximum value, civil wars are "
	     "turned off altogether."), NULL, 
	  GAME_MIN_CIVILWARSIZE, GAME_MAX_CIVILWARSIZE, 
	  GAME_DEFAULT_CIVILWARSIZE)

  GEN_INT("contactturns", game.contactturns,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Turns until player contact is lost"),
	  N_("Players may meet for diplomacy this number of turns "
	     "after their units have last met, even if they do not have "
	     "an embassy. If set to zero then players cannot meet unless "
	     "they have an embassy."),
	  NULL,
	  GAME_MIN_CONTACTTURNS, GAME_MAX_CONTACTTURNS, 
	  GAME_DEFAULT_CONTACTTURNS)

  GEN_BOOL("savepalace", game.savepalace,
	   SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	   N_("Rebuild palace if capital is conquered"),
	   N_("If this is set to 1, then when the capital is conquered the "
	      "palace "
	      "is automatically rebuilt for free in another randomly "
	      "choosen city. This is significant because the technology "
	      "requirement for building a palace will be ignored."),
	   NULL,
	   GAME_DEFAULT_SAVEPALACE)

  GEN_BOOL("naturalcitynames", game.natural_city_names,
           SSET_RULES_FLEXIBLE, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
           N_("Whether to use natural city names"),
           N_("If enabled, the default city names will be determined based "
              "on the surrounding terrain."),
           NULL, GAME_DEFAULT_NATURALCITYNAMES)

  /* Meta options: these don't affect the internal rules of the game, but
   * do affect players.  Also options which only produce extra server
   * "output" and don't affect the actual game.
   * ("endyear" is here, and not RULES_FLEXIBLE, because it doesn't
   * affect what happens in the game, it just determines when the
   * players stop playing and look at the score.)
   */
  GEN_STRING("allowtake", game.allow_take,
	     SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
             N_("Players that users are allowed to take"),
             N_("This should be a string of characters, each of which "
                "specifies a type or status of a civilization (player). "
                "Clients will only be permitted to take "
                "or observe those players which match one of the specified "
                "letters. This only affects future uses of the take or "
                "observe command; it is not retroactive. The characters "
		"and their meanings are:\n"
                "    o,O = Global observer\n"
                "    b   = Barbarian players\n"
                "    d   = Dead players\n"
                "    a,A = AI players\n"
                "    h,H = Human players\n"
                "The first description on this list which matches a "
                "player is the one which applies. Thus 'd' does not "
                "include dead barbarians, 'a' does not include dead AI "
                "players, and so on. Upper case letters apply before "
                "the game has started, lower case letters afterwards.\n\n"
                "Each character above may be followed by one of the "
                "following numbers to allow or restrict the manner "
                "of connection:\n\n"
                "(none) = Controller allowed, observers allowed, "
                "can displace connections. (Displacing a connection means "
		"that you may take over a player, even if another user "
		"already controls that player.)\n\n"
                "1 = Controller allowed, observers allowed, "
                "can't displace connections;\n\n"
                "2 = Controller allowed, no observers allowed, "
                "can displace connections;\n\n"
                "3 = Controller allowed, no observers allowed, "
                "can't displace connections;\n\n"
                "4 = No controller allowed, observers allowed;\n\n"),
                allowtake_callback, GAME_DEFAULT_ALLOW_TAKE)

  GEN_BOOL("autotoggle", game.auto_ai_toggle,
	   SSET_META, SSET_NETWORK, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Whether AI-status toggles with connection"),
	   N_("If this is set to 1, AI status is turned off when a player "
	      "connects, and on when a player disconnects."),
	   autotoggle_callback, GAME_DEFAULT_AUTO_AI_TOGGLE)

  GEN_INT("endyear", game.end_year,
	  SSET_META, SSET_SOCIOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Year the game ends"), 
	  N_("The game will end at the end of the given year."), NULL,
	  GAME_MIN_END_YEAR, GAME_MAX_END_YEAR, GAME_DEFAULT_END_YEAR)

  GEN_INT("timeout", game.timeout,
	  SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Maximum seconds per turn"),
	  N_("If all players have not hit \"Turn Done\" before this "
	     "time is up, then the turn ends automatically. Zero "
	     "means there is no timeout. In servers compiled with "
	     "debugging, a timeout "
	     "of -1 sets the autogame test mode. Use this with the command "
	     "\"timeoutincrease\" to have a dynamic timer."), NULL, 
	   GAME_MIN_TIMEOUT, GAME_MAX_TIMEOUT, GAME_DEFAULT_TIMEOUT)

  GEN_INT("timeaddenemymove", game.timeoutaddenemymove,
        SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Timeout at least n seconds when enemy moved"),
	  N_("Any time a unit moves when in sight of an enemy player, "
	     "the remaining timeout is set to this value if it was lower."),
	  NULL, 0, GAME_MAX_TIMEOUT, GAME_DEFAULT_TIMEOUTADDEMOVE)
  
  GEN_INT("nettimeout", game.tcptimeout,
	  SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
	  N_("Seconds to let a client's network connection block"),
	  N_("If a network connection is blocking for a time greater than "
	     "this value, then the connection is closed. Zero "
	     "means there is no timeout (although connections will be "
	     "automatically disconnected eventually)."),
	  NULL,
	  GAME_MIN_TCPTIMEOUT, GAME_MAX_TCPTIMEOUT, GAME_DEFAULT_TCPTIMEOUT)

  GEN_INT("netwait", game.netwait,
	  SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
	  N_("Max seconds for network buffers to drain"),
	  N_("The server will wait for up to the value of this "
	     "parameter in seconds, for all client connection network "
	     "buffers to unblock. Zero means the server will not "
	     "wait at all."), NULL, 
	  GAME_MIN_NETWAIT, GAME_MAX_NETWAIT, GAME_DEFAULT_NETWAIT)

  GEN_INT("pingtime", game.pingtime,
	  SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
	  N_("Seconds between PINGs"),
	  N_("The civserver will poll the clients with a PING request "
	     "each time this period elapses."), NULL, 
	  GAME_MIN_PINGTIME, GAME_MAX_PINGTIME, GAME_DEFAULT_PINGTIME)

  GEN_INT("pingtimeout", game.pingtimeout,
	  SSET_META, SSET_NETWORK, SSET_RARE,
          SSET_TO_CLIENT,
	  N_("Time to cut a client"),
	  N_("If a client doesn't reply to a PING in this time the "
	     "client is disconnected."), NULL, 
	  GAME_MIN_PINGTIMEOUT, GAME_MAX_PINGTIMEOUT, GAME_DEFAULT_PINGTIMEOUT)

  GEN_BOOL("turnblock", game.turnblock,
	   SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Turn-blocking game play mode"),
	   N_("If this is set to 1 the game turn is not advanced "
	      "until all players have finished their turn, including "
	      "disconnected players."), NULL, 
	   GAME_DEFAULT_TURNBLOCK)

  GEN_BOOL("fixedlength", game.fixedlength,
	   SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Fixed-length turns play mode"),
	   N_("If this is set to 1 the game turn will not advance "
	      "until the timeout has expired, even if all players "
	      "have clicked on \"Turn Done\"."), NULL,
	   FALSE)

  GEN_STRING("demography", game.demography,
	     SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_TO_CLIENT,
	     N_("What is in the Demographics report"),
	     N_("This should be a string of characters, each of which "
		"specifies the inclusion of a line of information "
		"in the Demographics report.\n"
		"The characters and their meanings are:\n"
		"    N = include Population\n"
		"    P = include Production\n"
		"    A = include Land Area\n"
		"    L = include Literacy\n"
		"    R = include Research Speed\n"
		"    S = include Settled Area\n"
		"    E = include Economics\n"
		"    M = include Military Service\n"
		"    O = include Pollution\n"
		"Additionally, the following characters control whether "
		"or not certain columns are displayed in the report:\n"
		"    q = display \"quantity\" column\n"
		"    r = display \"rank\" column\n"
		"    b = display \"best nation\" column\n"
		"The order of characters is not significant, but "
		"their capitalization is."),
	     is_valid_demography, GAME_DEFAULT_DEMOGRAPHY)

  GEN_INT("saveturns", game.save_nturns,
	  SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_SERVER_ONLY,
	  N_("Turns per auto-save"),
	  N_("The game will be automatically saved per this number of "
	     "turns. Zero means never auto-save."), NULL, 
	  0, 200, 10)

  /* Could undef entire option if !HAVE_LIBZ, but this way users get to see
   * what they're missing out on if they didn't compile with zlib?  --dwp
   */
#ifdef HAVE_LIBZ
  GEN_INT("compress", game.save_compress_level,
	  SSET_META, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Savegame compression level"),
	  N_("If non-zero, saved games will be compressed using zlib "
	     "(gzip format). Larger values will give better "
	     "compression but take longer. If the maximum is zero "
	     "this server was not compiled to use zlib."), NULL,

	  GAME_MIN_COMPRESS_LEVEL, GAME_MAX_COMPRESS_LEVEL,
	  GAME_DEFAULT_COMPRESS_LEVEL)
#else
  GEN_INT("compress", game.save_compress_level,
	  SSET_META, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Savegame compression level"),
	  N_("If non-zero, saved games will be compressed using zlib "
	     "(gzip format). Larger values will give better "
	     "compression but take longer. If the maximum is zero "
	     "this server was not compiled to use zlib."), NULL, 

	  GAME_NO_COMPRESS_LEVEL, GAME_NO_COMPRESS_LEVEL, 
	  GAME_NO_COMPRESS_LEVEL)
#endif

  GEN_STRING("savename", game.save_name,
	     SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_SERVER_ONLY,
	     N_("Auto-save name prefix"),
	     N_("Automatically saved games will have name "
		"\"<prefix><year>.sav\". This setting sets "
		"the <prefix> part."), NULL,
	     GAME_DEFAULT_SAVE_NAME)

  GEN_BOOL("scorelog", game.scorelog,
	   SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_SERVER_ONLY,
	   N_("Whether to log player statistics"),
	   N_("If this is set to 1, player statistics are appended to "
	      "the file \"civscore.log\" every turn. These statistics "
	      "can be used to create power graphs after the game."), NULL,
	   GAME_DEFAULT_SCORELOG)

  GEN_INT("gamelog", gamelog_level,
	  SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_SERVER_ONLY,
	  N_("Detail level for logging game events"),
	  N_("Only applies if the game log feature is enabled "
	     "(with the -g command line option). "
	     "Levels: 0=no logging, 20=standard logging, 30=detailed "
	     "logging, 40=debuging logging."), NULL,
	  0, 40, 20)

  GEN_END
};

/* The number of settings, not including the END. */
const int SETTINGS_NUM = ARRAY_SIZE(settings) - 1;
