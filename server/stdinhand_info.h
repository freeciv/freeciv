/**********************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
                                                                                
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__STDINHAND_C_H
#define FC__STDINHAND_C_H

#define OLEVELS_NUM ARRAY_SIZE(sset_level_names)

static bool valid_notradesize(int value, const char **reject_message);
static bool valid_fulltradesize(int value, const char **reject_message);
static bool autotoggle(bool value, const char **reject_message);
static bool is_valid_allowtake(const char *allow_take,
                               const char **error_string);
static bool is_valid_startunits(const char *start_units,
                               const char **error_string);
static bool valid_max_players(int v, const char **r_m);

bool sset_is_changeable(int idx);

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

/* Categories allow options to be usefully organized when presented to the user
 */
enum sset_category {
  SSET_GEOLOGY,
  SSET_ECOLOGY,
  SSET_SOCIOLOGY,
  SSET_ECONOMICS,
  SSET_MILITARY,
  SSET_SCIENCE,
  SSET_INTERNAL,
  SSET_NETWORK,
  SSET_NUM_CATEGORIES
};

static const char *sset_category_names[]= { N_("Geological"),
				            N_("Ecological"),
				            N_("Sociological"),
				            N_("Economic"),
				            N_("Military"),
				            N_("Scientific"),
				            N_("Internal"),
				            N_("Networking") };

/* Levels allow options to be subdivided and thus easier to navigate */
enum sset_level {
  SSET_NONE,
  SSET_ALL,
  SSET_VITAL,
  SSET_SITUATIONAL,
  SSET_RARE
};

static const char *sset_level_names[]= { N_("None"),
				         N_("All"),
				         N_("Vital"),
				         N_("Situational"),
				         N_("Rare") };

#define SSET_MAX_LEN  16             /* max setting name length (plus nul) */
#define TOKEN_DELIMITERS " \t\n,"

struct settings_s {
  const char *name;
  enum sset_class sclass;
  enum sset_to_client to_client;

  /*
   * Sould be less than 42 chars (?), or shorter if the values may
   * have more than about 4 digits.  Don't put "." on the end.
   */
  const char *short_help;

  /*
   * May be empty string, if short_help is sufficient.  Need not
   * include embedded newlines (but may, for formatting); lines will
   * be wrapped (and indented) automatically.  Should have punctuation
   * etc, and should end with a "."
   */
  const char *extra_help;
  enum sset_type type;
  enum sset_category category;
  enum sset_level level;

  /* 
   * About the *_validate functions: If the function is non-NULL, it
   * is called with the new value, and returns whether the change is
   * legal. The char * is an error message in the case of reject. 
   */

  /*** bool part ***/
  bool *bool_value;
  bool bool_default_value;
  bool (*bool_validate)(bool value, const char **reject_message);

  /*** int part ***/
  int *int_value;
  int int_default_value;
  bool (*int_validate)(int value, const char **reject_message);
  int int_min_value, int_max_value;

  /*** string part ***/
  char *string_value;
  const char *string_default_value;
  bool (*string_validate)(const char * value, const char **reject_message);
  size_t string_value_size;	/* max size we can write into string_value */
};

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

static struct settings_s settings[] = {

  /* These should be grouped by sclass */
  
/* Map size parameters: adjustable if we don't yet have a map */  
  GEN_INT("size", map.size, SSET_MAP_SIZE,
	  SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
          N_("Map size in 1,000 tiles units"),
          N_("This value is used to determine xsize and ysize\n"
             "  size = 4 is a normal map of 4,000 tiles (default)\n"
             "  size = 20 is a huge map of 20,000 tiles"), NULL,
          MAP_MIN_SIZE, MAP_MAX_SIZE, MAP_DEFAULT_SIZE)
  GEN_INT("topology", map.topology_id, SSET_MAP_SIZE,
	  SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("The map topology index"),
	  N_("Two-dimensional maps can wrap at the north-south or \n"
	     "east-west edges, and use a cartesian or isometric \n"
	     "rectangular grid.  See the manual for further explanation.\n"
             "  0 Flat Earth (unwrapped)\n"
             "  1 Earth (wraps E-W)\n"
             "  2 Uranus (wraps N-S)\n"
             "  3 Donut World (wraps N-S, E-W)\n"
	     "  4 Flat Earth (isometric)\n"
	     "  5 Earth (isometric)\n"
	     "  6 Uranus (isometric)\n"
	     "  7 Donut World (isometric)\n"
	     "  8 Flat Earth (hexagonal)\n"
	     "  9 Earth (hexagonal)\n"
	     " 10 Uranus (hexagonal)\n"
	     " 11 Donut World (hexagonal)\n"
	     " 12 Flat Earth (iso-hex)\n"
	     " 13 Earth (iso-hex)\n"
	     " 14 Uranus (iso-hex)\n"
	     " 15 Donut World (iso-hex)"
          ), NULL,
	  MAP_MIN_TOPO, MAP_MAX_TOPO, MAP_DEFAULT_TOPO)

/* Map generation parameters: once we have a map these are of historical
 * interest only, and cannot be changed.
 */
  GEN_INT("generator", map.generator,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_VITAL,  SSET_TO_CLIENT,
	  N_("Method used to generate map"),
	  N_("1 = standard, with random continents;\n\n"
	     "2 = equally sized large islands with one player each, and "
	     "twice that many smaller islands;\n\n"
	     "3 = equally sized large islands with one player each, and "
	     "a number of other islands of similar size;\n\n"
	     "4 = equally sized large islands with two players on every "
	     "island (or one with three players for an odd number of "
	     "players), and additional smaller islands;\n\n"
	     "5 = one or more large earthlike continents with some "
	     "scatter.\n\n"
	     "Note: values 2,3 and 4 generate \"fairer\" (but more boring) "
	     "maps.\n"
	     "(Zero indicates a scenario map.)"), NULL,
	  MAP_MIN_GENERATOR, MAP_MAX_GENERATOR, MAP_DEFAULT_GENERATOR)

  GEN_BOOL("tinyisles", map.tinyisles,
	   SSET_MAP_GEN, SSET_GEOLOGY, SSET_RARE, SSET_TO_CLIENT,
	   N_("Presence of 1x1 islands"),
	   N_("0 = no 1x1 islands; 1 = some 1x1 islands"), NULL,
	   MAP_DEFAULT_TINYISLES)

  GEN_BOOL("separatepoles", map.separatepoles,
	   SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Whether the poles are separate continents"),
	   N_("0 = continents may attach to poles; 1 = poles will "
	      "be separate"), NULL, 
	   MAP_DEFAULT_SEPARATE_POLES)

  GEN_BOOL("alltemperate", map.alltemperate, 
           SSET_MAP_GEN, SSET_GEOLOGY, SSET_RARE, SSET_TO_CLIENT,
	   N_("All the map is temperate"),
	   N_("0 = normal Earth-like planet; 1 = all-temperate planet "),
	   NULL, MAP_DEFAULT_ALLTEMPERATE)

  GEN_INT("landmass", map.landpercent,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of land vs ocean"), "", NULL,
	  MAP_MIN_LANDMASS, MAP_MAX_LANDMASS, MAP_DEFAULT_LANDMASS)

  GEN_INT("mountains", map.mountains,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of hills/mountains"),
	  N_("Small values give flat maps, higher values give more "
	     "hills and mountains."), NULL,
	  MAP_MIN_MOUNTAINS, MAP_MAX_MOUNTAINS, MAP_DEFAULT_MOUNTAINS)

  GEN_INT("rivers", map.riverlength,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of river squares"), "", NULL,
	  MAP_MIN_RIVERS, MAP_MAX_RIVERS, MAP_DEFAULT_RIVERS)

  GEN_INT("grass", map.grasssize,
	  SSET_MAP_GEN, SSET_ECOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of grass squares"), "", NULL,
	  MAP_MIN_GRASS, MAP_MAX_GRASS, MAP_DEFAULT_GRASS)

  GEN_INT("forests", map.forestsize,
	  SSET_MAP_GEN, SSET_ECOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of forest squares"), "", NULL, 
	  MAP_MIN_FORESTS, MAP_MAX_FORESTS, MAP_DEFAULT_FORESTS)

  GEN_INT("swamps", map.swampsize,
	  SSET_MAP_GEN, SSET_ECOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of swamp squares"), "", NULL, 
	  MAP_MIN_SWAMPS, MAP_MAX_SWAMPS, MAP_DEFAULT_SWAMPS)

  GEN_INT("deserts", map.deserts,
	  SSET_MAP_GEN, SSET_ECOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of desert squares"), "", NULL, 
	  MAP_MIN_DESERTS, MAP_MAX_DESERTS, MAP_DEFAULT_DESERTS)

  GEN_INT("seed", map.seed,
	  SSET_MAP_GEN, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Map generation random seed"),
	  N_("The same seed will always produce the same map; "
	     "for zero (the default) a seed will be chosen based on "
	     "the time, to give a random map."), NULL, 
	  MAP_MIN_SEED, MAP_MAX_SEED, MAP_DEFAULT_SEED)

/* Map additional stuff: huts and specials.  randseed also goes here
 * because huts and specials are the first time the randseed gets used (?)
 * These are done when the game starts, so these are historical and
 * fixed after the game has started.
 */
  GEN_INT("randseed", game.randseed,
	  SSET_MAP_ADD, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("General random seed"),
	  N_("For zero (the default) a seed will be chosen based "
	     "on the time."), NULL, 
	  GAME_MIN_RANDSEED, GAME_MAX_RANDSEED, GAME_DEFAULT_RANDSEED)

  GEN_INT("specials", map.riches,
	  SSET_MAP_ADD, SSET_ECOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Amount of \"special\" resource squares"), 
	  N_("Special resources improve the basic terrain type they "
	     "are on.  The server variable's scale is parts per "
	     "thousand."), NULL,
	  MAP_MIN_RICHES, MAP_MAX_RICHES, MAP_DEFAULT_RICHES)

  GEN_INT("huts", map.huts,
	  SSET_MAP_ADD, SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Amount of huts (minor tribe villages)"), "", NULL,
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
	     "players or AI's) before the game can start."), NULL,
	  GAME_MIN_MIN_PLAYERS, GAME_MAX_MIN_PLAYERS, GAME_DEFAULT_MIN_PLAYERS)

  GEN_INT("maxplayers", game.max_players,
	  SSET_PLAYERS, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Maximum number of players"),
          N_("The maximal number of human and AI players who can be in "
             "the game. When this number of players are connected in "
             "the pregame state, any new players who try to connect "
             "will be rejected."), valid_max_players,
	  GAME_MIN_MAX_PLAYERS, GAME_MAX_MAX_PLAYERS, GAME_DEFAULT_MAX_PLAYERS)

  GEN_INT("aifill", game.aifill,
	  SSET_PLAYERS, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Number of players to fill to with AI's"),
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
             N_("List of player's initial units"),
             N_("This should be a string of characters, each of which "
		"specifies a unit role. There must be at least one city "
		"founder in the string. The characters and their "
		"meanings are:\n"
		"    c   = City founder (eg. Settlers)\n"
		"    w   = Terrain worker (eg. Engineers)\n"
		"    x   = Explorer (eg. Explorer)\n"
		"    k   = Gameloss (eg. King)\n"
		"    s   = Diplomat (eg. Diplomat)\n"
		"    f   = Ferryboat (eg. Trireme)\n"
		"    d   = Ok defense unit (eg. Warriors)\n"
		"    D   = Good defense unit (eg. Phalanx)\n"
		"    a   = Fast attack unit (eg. Horsemen)\n"
		"    A   = Strong attack unit (eg. Catapult)\n"),
		is_valid_startunits, GAME_DEFAULT_START_UNITS)

  GEN_INT("dispersion", game.dispersion,
	  SSET_GAME_INIT, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Area where initial units are located"),
	  N_("This is half the length of a side of the square within "
	     "which the initial units are dispersed."), NULL,
	  GAME_MIN_DISPERSION, GAME_MAX_DISPERSION, GAME_DEFAULT_DISPERSION)

  GEN_INT("gold", game.gold,
	  SSET_GAME_INIT, SSET_ECONOMICS, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Starting gold per player"), "", NULL,
	  GAME_MIN_GOLD, GAME_MAX_GOLD, GAME_DEFAULT_GOLD)

  GEN_INT("techlevel", game.tech,
	  SSET_GAME_INIT, SSET_SCIENCE, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Number of initial advances per player"), "", NULL,
	  GAME_MIN_TECHLEVEL, GAME_MAX_TECHLEVEL, GAME_DEFAULT_TECHLEVEL)

  GEN_INT("researchcost", game.researchcost,
	  SSET_RULES, SSET_SCIENCE, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Points required to gain a new advance"),
	  N_("This affects how quickly players can research new "
	     "technology."), NULL,
	  GAME_MIN_RESEARCHCOST, GAME_MAX_RESEARCHCOST, 
	  GAME_DEFAULT_RESEARCHCOST)

  GEN_INT("techpenalty", game.techpenalty,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Percentage penalty when changing tech"),
	  N_("If you change your current research technology, and you have "
	     "positive research points, you lose this percentage of those "
	     "research points.  This does not apply if you have just gained "
	     "tech this turn."), NULL,
	  GAME_MIN_TECHPENALTY, GAME_MAX_TECHPENALTY,
	  GAME_DEFAULT_TECHPENALTY)

  GEN_INT("diplcost", game.diplcost,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Penalty when getting tech from treaty"),
	  N_("For each advance you gain from a diplomatic treaty, you lose "
	     "research points equal to this percentage of the cost to "
	     "research an new advance.  You can end up with negative "
	     "research points if this is non-zero."), NULL, 
	  GAME_MIN_DIPLCOST, GAME_MAX_DIPLCOST, GAME_DEFAULT_DIPLCOST)

  GEN_INT("conquercost", game.conquercost,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Penalty when getting tech from conquering"),
	  N_("For each advance you gain by conquering an enemy city, you "
	     "lose research points equal to this percentage of the cost "
	     "to research an new advance.  You can end up with negative "
	     "research points if this is non-zero."), NULL,
	  GAME_MIN_CONQUERCOST, GAME_MAX_CONQUERCOST,
	  GAME_DEFAULT_CONQUERCOST)

  GEN_INT("freecost", game.freecost,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Penalty when getting a free tech"),
	  N_("For each advance you gain \"for free\" (other than covered by "
	     "diplcost or conquercost: specifically, from huts or from the "
	     "Great Library), you lose research points equal to this "
	     "percentage of the cost to research a new advance.  You can "
	     "end up with negative research points if this is non-zero."), 
	  NULL, 
	  GAME_MIN_FREECOST, GAME_MAX_FREECOST, GAME_DEFAULT_FREECOST)

  GEN_INT("foodbox", game.foodbox,
	  SSET_RULES, SSET_ECONOMICS, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Food required for a city to grow"), "", NULL,
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

  GEN_INT("fulltradesize", game.fulltradesize,
	  SSET_RULES, SSET_ECONOMICS, SSET_RARE, SSET_TO_CLIENT,
	  N_("Minimum city size to get full trade"),
	  N_("There is a trade penalty in all cities smaller than this. "
	     "The penalty is 100% (no trade at all) for sizes up to "
	     "notradesize, and decreases gradually to 0% (no penalty "
	     "except the normal corruption) for size=fulltradesize.  "
	     "See also notradesize."), valid_fulltradesize, 
	  GAME_MIN_FULLTRADESIZE, GAME_MAX_FULLTRADESIZE, 
	  GAME_DEFAULT_FULLTRADESIZE)

  GEN_INT("notradesize", game.notradesize,
	  SSET_RULES, SSET_ECONOMICS, SSET_RARE, SSET_TO_CLIENT,
	  N_("Maximum size of a city without trade"),
	  N_("All the cities of smaller or equal size to this do not "
	     "produce trade at all. The produced trade increases "
	     "gradually for cities larger than notradesize and smaller "
	     "than fulltradesize.  See also fulltradesize."),
	  valid_notradesize,
	  GAME_MIN_NOTRADESIZE, GAME_MAX_NOTRADESIZE,
	  GAME_DEFAULT_NOTRADESIZE)

  GEN_INT("unhappysize", game.unhappysize,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("City size before people become unhappy"),
	  N_("Before other adjustments, the first unhappysize citizens in a "
	     "city are content, and subsequent citizens are unhappy.  "
	     "See also cityfactor."), NULL,
	  GAME_MIN_UNHAPPYSIZE, GAME_MAX_UNHAPPYSIZE,
	  GAME_DEFAULT_UNHAPPYSIZE)

  GEN_BOOL("angrycitizen", game.angrycitizen,
	   SSET_RULES, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Whether angry citizens are enabled"),
	  N_("Introduces angry citizens like civilization II. Angry "
	     "citizens have to become unhappy before any other class "
	     "of citizens may be considered. See also unhappysize, "
	     "cityfactor and governments."), NULL, 
	  GAME_DEFAULT_ANGRYCITIZEN)

  GEN_INT("cityfactor", game.cityfactor,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Number of cities for higher unhappiness"),
	  N_("When the number of cities a player owns is greater than "
	     "cityfactor, one extra citizen is unhappy before other "
	     "adjustments; see also unhappysize.  This assumes a "
	     "Democracy; for other governments the effect occurs at "
	     "smaller numbers of cities."), NULL, 
	  GAME_MIN_CITYFACTOR, GAME_MAX_CITYFACTOR, GAME_DEFAULT_CITYFACTOR)

  GEN_INT("citymindist", game.citymindist,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Minimum distance between cities"),
	  N_("When a player founds a new city, it is checked if there is "
	     "no other city in citymindist distance. For example, if "
	     "citymindist is 3, there have to be at least two empty "
	     "fields between two cities in every direction. If it is set "
	     "to 0 (default), it is overwritten by the current ruleset "
	     "when the game starts."), NULL,
	  GAME_MIN_CITYMINDIST, GAME_MAX_CITYMINDIST,
	  GAME_DEFAULT_CITYMINDIST)

  GEN_INT("rapturedelay", game.rapturedelay,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
          N_("Number of turns between rapture effect"),
          N_("Sets the number of turns between rapture growth of a city. "
             "If set to n a city will grow after rapturing n+1 turns."), NULL,
          GAME_MIN_RAPTUREDELAY, GAME_MAX_RAPTUREDELAY,
          GAME_DEFAULT_RAPTUREDELAY)

  GEN_INT("razechance", game.razechance,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Chance for conquered building destruction"),
	  N_("When a player conquers a city, each City Improvement has this "
	     "percentage chance to be destroyed."), NULL, 
	  GAME_MIN_RAZECHANCE, GAME_MAX_RAZECHANCE, GAME_DEFAULT_RAZECHANCE)

  GEN_INT("civstyle", game.civstyle,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Style of Civ rules"),
	  N_("Sets some basic rules; 1 means style of Civ1, 2 means Civ2.\n"
	     "Currently this option affects the following rules:\n"
	     "  - Apollo shows whole map in Civ2, only cities in Civ1.\n"
	     "See also README.rulesets."), NULL,
	  GAME_MIN_CIVSTYLE, GAME_MAX_CIVSTYLE, GAME_DEFAULT_CIVSTYLE)

  GEN_INT("occupychance", game.occupychance,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Chance of moving into tile after attack"),
	  N_("If set to 0, combat is Civ1/2-style (when you attack, "
	     "you remain in place).  If set to 100, attacking units "
	     "will always move into the tile they attacked if they win "
	     "the combat (and no enemy units remain in the tile).  If "
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
	  N_("watchtower vision range: If set to 1, it has no effect. "
	     "If 2 or larger, the vision range of a unit inside a "
	     "fortress is set to this value, if the necessary invention "
	     "has been made. This invention is determined by the flag "
	     "'Watchtower' in the techs ruleset. Also see wtowerevision."), 
	  NULL, 
	  GAME_MIN_WATCHTOWER_VISION, GAME_MAX_WATCHTOWER_VISION, 
	  GAME_DEFAULT_WATCHTOWER_VISION)

  GEN_INT("wtowerevision", game.watchtower_extra_vision,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Extra vision range for units in a fortress"),
	  N_("watchtower extra vision range: If set to 0, it has no "
	     "effect. If larger than 0, the visionrange of a unit is "
	     "raised by this value, if the unit is inside a fortress "
	     "and the invention determined by the flag 'Watchtower' "
	     "in the techs ruleset has been made. Always the larger "
	     "value of wtowervision and wtowerevision will be used. "
	     "Also see wtowervision."), NULL, 
	  GAME_MIN_WATCHTOWER_EXTRA_VISION, GAME_MAX_WATCHTOWER_EXTRA_VISION, 
	  GAME_DEFAULT_WATCHTOWER_EXTRA_VISION)

  GEN_INT("borders", game.borders,
	  SSET_RULES, SSET_MILITARY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("National border's radius"),
	  N_("If this is set to greater than 0, nations will have territory "
	     "delineated by borders placed on the loci between cities, with "
	     "the maximum distance from any city specified."), NULL,
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
	  N_("If set to 0 (default), diplomacy is enabled for all.\n"
	     "If set to 1, diplomacy is only allowed between human players.\n"
	     "If set to 2, diplomacy is only allowed between AI players.\n"
             "If set to 3, diplomacy is restricted to teams.\n"
             "If set to 4, diplomacy is disabled for all.\n"
             "You can always do diplomacy with players on your team."), NULL,
	  GAME_MIN_DIPLOMACY, GAME_MAX_DIPLOMACY, GAME_DEFAULT_DIPLOMACY)

  GEN_INT("citynames", game.allowed_city_names,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Allowed city names"),
	  N_("If set to 0, there are no restrictions: players can have "
	     "multiple cities with the same names. "
	     "If set to 1, city names have to be unique to a player: "
	     "one player can't have multiple cities with the same name. "
	     "If set to 2 or 3, city names have to be globally unique: "
	     "all cities in a game have to have different names. "
	     "If set to 3, a player isn't allowed to use a default city name "
	     "of another nations."),NULL,
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
	  N_("0 - no barbarians \n"
	     "1 - barbarians only in huts \n"
	     "2 - normal rate of barbarian appearance \n"
	     "3 - frequent barbarian uprising \n"
	     "4 - raging hordes, lots of barbarians"), NULL, 
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
	  N_("When changing government an intermission"
             " period of the specified length, in turns,"
             " occur."
             " Setting this value to 0 will give a random"
             " length of 1-6 turns."), NULL, 
	  GAME_MIN_REVOLUTION_LENGTH, GAME_MAX_REVOLUTION_LENGTH, 
	  GAME_DEFAULT_REVOLUTION_LENGTH)

  GEN_BOOL("fogofwar", game.fogofwar,
	   SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	   N_("Whether to enable fog of war"),
	   N_("If this is set to 1, only those units and cities within "
	      "the sightrange of your own units and cities will be "
	      "revealed to you. You will not see new cities or terrain "
	      "changes in squares not observed."), NULL, 
	   GAME_DEFAULT_FOGOFWAR)

  GEN_INT("diplchance", game.diplchance,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Chance in diplomat/spy contests"),
	  /* xgettext:no-c-format */
	  N_("A Diplomat or Spy acting against a city which has one or "
	     "more defending Diplomats or Spies has a diplchance "
	     "(percent) chance to defeat each such defender.  Also, the "
	     "chance of a Spy returning from a successful mission is "
	     "diplchance percent.  (Diplomats never return.)  Also, a "
	     "basic chance of success for Diplomats and Spies. "
	     "Defending Spys are generally twice as capable as "
	     "Diplomats, veteran units 50% more capable than "
	     "non-veteran ones."), NULL, 
	  GAME_MIN_DIPLCHANCE, GAME_MAX_DIPLCHANCE, GAME_DEFAULT_DIPLCHANCE)

  GEN_BOOL("spacerace", game.spacerace,
	   SSET_RULES_FLEXIBLE, SSET_SCIENCE, SSET_VITAL, SSET_TO_CLIENT,
	   N_("Whether to allow space race"),
	   N_("If this option is 1, players can build spaceships."), NULL, 
	   GAME_DEFAULT_SPACERACE)

  GEN_INT("civilwarsize", game.civilwarsize,
	  SSET_RULES_FLEXIBLE, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Minimum number of cities for civil war"),
	  N_("A civil war is triggered if a player has at least this "
	     "many cities and the player's capital is captured.  If "
	     "this option is set to the maximum value, civil wars are "
	     "turned off altogether."), NULL, 
	  GAME_MIN_CIVILWARSIZE, GAME_MAX_CIVILWARSIZE, 
	  GAME_DEFAULT_CIVILWARSIZE)

  GEN_INT("contactturns", game.contactturns,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Turns until player contact is lost"),
	  N_("Players may meet for diplomacy this number of turns "
	     "after their units have last met. Set this to zero "
	     "to turn this feature off entirely."), NULL,
	  GAME_MIN_CONTACTTURNS, GAME_MAX_CONTACTTURNS, 
	  GAME_DEFAULT_CONTACTTURNS)

  GEN_BOOL("savepalace", game.savepalace,
	   SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	   N_("Rebuild palace if capital is conquered"),
	   N_("If this is set to 1 when the capital is conquered, palace "
	      "is automatically rebuilt for free in another randomly "
	      "choosed city, regardless on the knowledge of Masonry."), NULL, 
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
                "specifies a type or status of a civilization, or "
                "\"player\".  Clients will only be permitted to take "
                "or observe those players which match one of the specified "
                "letters.  This only affects future uses of the take or "
                "observe command.  The characters and their meanings are:\n"
                "    H,h = Human players\n"
                "    A,a = AI players\n"
                "    d   = Dead players\n"
                "    b   = Barbarian players\n"
                "The first description from the _bottom_ which matches a "
                "player is the one which applies.  Thus 'd' does not "
                "include Barbarians, 'a' does not include dead AI "
                "players, and so on.  Upper case letters apply before "
                "the game has started, lower case letters afterwards.\n"
                "Each character above may be followed by one of the "
                "following numbers to allow or restrict the manner "
                "of connection:\n"
                "     (none) = Controller allowed, observers allowed, "
                "can displace connections.*\n"
                "     1 = Controller allowed, observers allowed, "
                "can't displace connections;\n"
                "     2 = Controller allowed, no observers allowed, "
                "can displace connections;\n"
                "     3 = Controller allowed, no observers allowed, "
                "can't displace connections;\n"
                "     4 = No controller allowed, observers allowed;\n\n"
                "* \"Displacing a connection\" means that you may take over "
                "a player that another user already has control of."),
                is_valid_allowtake, GAME_DEFAULT_ALLOW_TAKE)

  GEN_BOOL("autotoggle", game.auto_ai_toggle,
	   SSET_META, SSET_NETWORK, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Whether AI-status toggles with connection"),
	   N_("If this is set to 1, AI status is turned off when a player "
	      "connects, and on when a player disconnects."), autotoggle, 
	   GAME_DEFAULT_AUTO_AI_TOGGLE)

  GEN_INT("endyear", game.end_year,
	  SSET_META, SSET_SOCIOLOGY, SSET_TO_CLIENT, SSET_VITAL,
	  N_("Year the game ends"), "", NULL,
	  GAME_MIN_END_YEAR, GAME_MAX_END_YEAR, GAME_DEFAULT_END_YEAR)

  GEN_INT( "timeout", game.timeout,
	   SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	   N_("Maximum seconds per turn"),
	   N_("If all players have not hit \"Turn Done\" before this "
	      "time is up, then the turn ends automatically. Zero "
	      "means there is no timeout. In DEBUG servers, a timeout "
	      "of -1 sets the autogame test mode. Use this with the command "
              "\"timeoutincrease\" to have a dynamic timer."), NULL, 
	   GAME_MIN_TIMEOUT, GAME_MAX_TIMEOUT, GAME_DEFAULT_TIMEOUT)

  GEN_INT("tcptimeout", game.tcptimeout,
	  SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
	  N_("Seconds to let a client connection block"),
	  N_("If a TCP connection is blocking for a time greater than "
	     "this value, then the TCP connection is closed. Zero "
	     "means there is no timeout beyond that enforced by the "
	     "TCP protocol implementation itself."), NULL, 
	  GAME_MIN_TCPTIMEOUT, GAME_MAX_TCPTIMEOUT, GAME_DEFAULT_TCPTIMEOUT)

  GEN_INT("netwait", game.netwait,
	  SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
	  N_("Max seconds for TCP buffers to drain"),
	  N_("The civserver will wait for up to the value of this "
	     "parameter in seconds, for all client connection TCP "
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
	  N_("If a client doesn't reply to a PONG in this time the "
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
	      "until the timeout has expired, irrespective of players "
	      "clicking on \"Turn Done\"."), NULL,
	   FALSE)

  GEN_STRING("demography", game.demography,
	     SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_TO_CLIENT,
	     N_("What is in the Demographics report"),
	     N_("This should be a string of characters, each of which "
		"specifies the the inclusion of a line of information "
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
		"(The order of these characters is not significant, but their case is.)"),
	     is_valid_demography,
	     GAME_DEFAULT_DEMOGRAPHY)

  GEN_INT("saveturns", game.save_nturns,
	  SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_SERVER_ONLY,
	  N_("Turns per auto-save"),
	  N_("The game will be automatically saved per this number of turns.\n"
	     "Zero means never auto-save."), NULL, 
	  0, 200, 10)

  /* Could undef entire option if !HAVE_LIBZ, but this way users get to see
   * what they're missing out on if they didn't compile with zlib?  --dwp
   */
#ifdef HAVE_LIBZ
  GEN_INT("compress", game.save_compress_level,
	  SSET_META, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Savegame compression level"),
	  N_("If non-zero, saved games will be compressed using zlib "
	     "(gzip format).  Larger values will give better "
	     "compression but take longer.  If the maximum is zero "
	     "this server was not compiled to use zlib."), NULL,

	  GAME_MIN_COMPRESS_LEVEL, GAME_MAX_COMPRESS_LEVEL,
	  GAME_DEFAULT_COMPRESS_LEVEL)
#else
  GEN_INT("compress", game.save_compress_level,
	  SSET_META, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Savegame compression level"),
	  N_("If non-zero, saved games will be compressed using zlib "
	     "(gzip format).  Larger values will give better "
	     "compression but take longer.  If the maximum is zero "
	     "this server was not compiled to use zlib."), NULL, 

	  GAME_NO_COMPRESS_LEVEL, GAME_NO_COMPRESS_LEVEL, 
	  GAME_NO_COMPRESS_LEVEL)
#endif

  GEN_STRING("savename", game.save_name,
	     SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_SERVER_ONLY,
	     N_("Auto-save name prefix"),
	     N_("Automatically saved games will have name "
		"\"<prefix><year>.sav\".\nThis setting sets "
		"the <prefix> part."), NULL,
	     GAME_DEFAULT_SAVE_NAME)

  GEN_BOOL("scorelog", game.scorelog,
	   SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_SERVER_ONLY,
	   N_("Whether to log player statistics"),
	   N_("If this is set to 1, player statistics are appended to "
	      "the file \"civscore.log\" every turn.  These statistics "
	      "can be used to create power graphs after the game."), NULL,
	   GAME_DEFAULT_SCORELOG)

  GEN_INT("gamelog", gamelog_level,
	  SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_SERVER_ONLY,
	  N_("Detail level for logging game events"),
	  N_("Only applies if the game log feature is enabled "
	     "(with the -g command line option).  "
	     "Levels: 0=no logging, 20=standard logging, 30=detailed logging, "
	     "40=debuging logging."), NULL,
	  0, 40, 20)

  GEN_END
};

#define SETTINGS_NUM (ARRAY_SIZE(settings)-1)

/**************************************************************************
  Commands - can be recognised by unique prefix
**************************************************************************/
struct command {
  const char *name;             /* name - will be matched by unique prefix   */
  enum cmdlevel_id level;  	/* access level required to use the command  */
  const char *synopsis;	   	/* one or few-line summary of usage */
  const char *short_help;	/* one line (about 70 chars) description */
  const char *extra_help;	/* extra help information; will be line-wrapped */
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
  CMD_WALL,
  CMD_VOTE,
  
  /* mostly non-harmful: */
  CMD_DEBUG,
  CMD_SET,
  CMD_TEAM,
  CMD_RULESETDIR,
  CMD_METAINFO,
  CMD_METACONN,
  CMD_METASERVER,
  CMD_AITOGGLE,
  CMD_TAKE,
  CMD_OBSERVE,
  CMD_DETACH,
  CMD_CREATE,
  CMD_AWAY,
  CMD_NOVICE,
  CMD_EASY,
  CMD_NORMAL,
  CMD_HARD,
  CMD_EXPERIMENTAL,
  CMD_CMDLEVEL,
  CMD_FIRSTLEVEL,
  CMD_TIMEOUT,

  /* potentially harmful: */
  CMD_END_GAME,
  CMD_REMOVE,
  CMD_SAVE,
  CMD_LOAD,
  CMD_READ_SCRIPT,
  CMD_WRITE_SCRIPT,

  /* undocumented */
  CMD_RFCSTYLE,

  /* pseudo-commands: */
  CMD_NUM,		/* the number of commands - for iterations */
  CMD_UNRECOGNIZED,	/* used as a possible iteration result */
  CMD_AMBIGUOUS		/* used as a possible iteration result */
};


static const struct command commands[] = {
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
   /* TRANS: translate text between <> only */
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
   "list\n"
   "list players\n"
   "list connections",
   N_("Show a list of players or connections."),
   N_("Show a list of players, or a list of connections to the server.  "
      "The argument may be abbreviated, and defaults to 'players' if absent.")
  },
  {"quit",	ALLOW_HACK,
   "quit",
   N_("Quit the game and shutdown the server."), NULL
  },
  {"cut",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("cut <connection-name>"),
   N_("Cut a client's connection to server."),
   N_("Cut specified client's connection to the server, removing that client "
      "from the game.  If the game has not yet started that client's player "
      "is removed from the game, otherwise there is no effect on the player.  "
      "Note that this command now takes connection names, not player names.")
  },
  {"explain",	ALLOW_INFO,
   /* TRANS: translate text between <> only */
   N_("explain\n"
      "explain <option-name>"),
   N_("Explain server options."),
   N_("The 'explain' command gives a subset of the functionality of 'help', "
      "and is included for backward compatibility.  With no arguments it "
      "gives a list of options (like 'help options'), and with an argument "
      "it gives help for a particular option (like 'help <option-name>').")
  },
  {"show",	ALLOW_INFO,
   /* TRANS: translate text between <> only */
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
  {"wall",	ALLOW_HACK,
   N_("wall <message>"),
   N_("Send message to all connections."),
   N_("For each connected client, pops up a window showing the message "
      "entered.")
  },
  {"vote",	ALLOW_INFO,
   N_("vote yes|no [vote number]"),
   N_("Cast a vote."),
      /* xgettext:no-c-format */
   N_("A player with info level access issueing a control level command "
      "starts a new vote for given command.  /vote followed by "
      "\"yes\" or \"no\", and optionally a vote number, "
      "gives your vote.  If you do not add a vote number, your vote applies "
      "to the latest command.  You can only suggest one vote at a time.  "
      "Voting concludes after a full turn has passed or more than 50% of the "
      "votes have been cast for or against.")
  },
  {"debug",	ALLOW_CTRL,
   N_("debug [ player <player> | city <x> <y> | units <x> <y> | unit <id> ]"),
   N_("Turn on or off AI debugging of given entity."),
   N_("Print AI debug information about given entity and turn continous "
      "debugging output for this entity on or off."),
  },
  {"set",	ALLOW_CTRL,
   N_("set <option-name> <value>"),
   N_("Set server option."), NULL
  },
  {"team",	ALLOW_CTRL,
   N_("team <player> [team]"),
   N_("Change, add or remove a player's team affiliation."),
   N_("Sets a player as member of a team. If no team specified, the "
      "player is set teamless. Use \"\" if names contain whitespace. "
      "A team is a group of players that start out allied, with shared "
      "vision and embassies, and fight together to achieve team victory "
      "with averaged individual scores.")
  },
  {"rulesetdir", ALLOW_CTRL,
   N_("rulesetdir <directory>"),
   N_("Choose new ruleset directory or modpack."),
   N_("Choose new ruleset directory or modpack. Calling this\n "
      "without any arguments will show you the currently selected "
      "ruleset.")
  },
  {"metainfo",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("metainfo <meta-line>"),
   N_("Set metaserver info line."), NULL
  },
  {"metaconnection",	ALLOW_HACK,
   "metaconnection u|up\n"
   "metaconnection d|down\n"
   "metaconnection ?",
   N_("Control metaserver connection."),
   N_("'metaconnection ?' reports on the status of the connection to metaserver.\n"
      "'metaconnection down' or 'metac d' brings the metaserver connection down.\n"
      "'metaconnection up' or 'metac u' brings the metaserver connection up.")
  },
  {"metaserver",ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("metaserver <address>"),
   N_("Set address for metaserver to report to."), NULL
  },
  {"aitoggle",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("aitoggle <player-name>"),
   N_("Toggle AI status of player."), NULL
  },
  {"take",    ALLOW_INFO,
   /* TRANS: translate text between [] and <> only */
   N_("take [connection-name] <player-name>"),
   N_("Take over a player's place in the game."),
   N_("Only the console and connections with cmdlevel 'hack' can force "
      "other connections to take over a player. If you're not one of these, "
      "only the <player-name> argument is allowed")
  },
  {"observe",    ALLOW_INFO,
   /* TRANS: translate text between [] and <> only */
   N_("observe [connection-name] <player-name>"),
   N_("Observe a player."),
   N_("Only the console and connections with cmdlevel 'hack' can force "
      "other connections to observe a player. If you're not one of these, "
      "only the <player-name> argument is allowed")
  },
  {"detach",    ALLOW_INFO,
   /* TRANS: translate text between <> only */
   N_("detach <connection-name>"),
   N_("detach from a player."),
   N_("Only the console and connections with cmdlevel 'hack' can force "
      "other connections to detach from a player.")
  },
  {"create",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("create <player-name>"),
   N_("Create an AI player with a given name."),
   N_("The 'create' command is only available before the game has "
      "been started.")
  },
  {"away",	ALLOW_INFO,
   N_("away\n"
      "away"),
   N_("Set yourself in away mode. The AI will watch your back."),
   N_("The AI will govern your nation but do minimal changes."),
  },
  {"novice",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("novice\n"
      "novice <player-name>"),
   N_("Set one or all AI players to 'novice'."),
   N_("With no arguments, sets all AI players to skill level 'novice', and "
      "sets the default level for any new AI players to 'novice'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"easy",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("easy\n"
      "easy <player-name>"),
   N_("Set one or all AI players to 'easy'."),
   N_("With no arguments, sets all AI players to skill level 'easy', and "
      "sets the default level for any new AI players to 'easy'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"normal",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("normal\n"
      "normal <player-name>"),
   N_("Set one or all AI players to 'normal'."),
   N_("With no arguments, sets all AI players to skill level 'normal', and "
      "sets the default level for any new AI players to 'normal'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"hard",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("hard\n"
      "hard <player-name>"),
   N_("Set one or all AI players to 'hard'."),
   N_("With no arguments, sets all AI players to skill level 'hard', and "
      "sets the default level for any new AI players to 'hard'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"experimental",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("experimental\n"
      "experimental <player-name>"),
   N_("Set one or all AI players to 'experimental'."),
   N_("With no arguments, sets all AI players to skill 'experimental', and "
      "sets the default level for any new AI players to this.  With an "
      "argument, sets the skill level for that player only. THIS IS ONLY "
      "FOR TESTING OF NEW AI FEATURES! For ordinary servers, this option "
      "has no effect.")
  },
  {"cmdlevel",	ALLOW_HACK,  /* confusing to leave this at ALLOW_CTRL */
   /* TRANS: translate text between <> only */
   N_("cmdlevel\n"
      "cmdlevel <level>\n"
      "cmdlevel <level> new\n"
      "cmdlevel <level> first\n"
      "cmdlevel <level> <connection-name>"),
   N_("Query or set command access level access."),
   N_("The command access level controls which server commands are available\n"
      "to users via the client chatline.  The available levels are:\n"
      "    none  -  no commands\n"
      "    info  -  informational commands only\n"
      "    ctrl  -  commands that affect the game and users\n"
      "    hack  -  *all* commands - dangerous!\n"
      "With no arguments, the current command access levels are reported.\n"
      "With a single argument, the level is set for all existing "
      "connections,\nand the default is set for future connections.\n"
      "If 'new' is specified, the level is set for newly connecting clients.\n"
      "If 'first come' is specified, the 'first come' level is set; it will be\n"
      "granted to the first client to connect, or if there are connections\n"
      "already, the first client to issue the 'firstlevel' command.\n"
      "If a connection name is specified, the level is set for that "
      "connection only.\n"
      "Command access levels do not persist if a client disconnects, "
      "because some untrusted person could reconnect with the same name.  "
      "Note that this command now takes connection names, not player names."
      )
  },
  {"firstlevel", ALLOW_INFO,  /* Not really "informational", but needs to
				 be ALLOW_INFO to be useful. */
   "firstlevel",
   N_("Grab the 'first come' command access level."),
   N_("If 'cmdlevel first come' has been used to set a special 'first come'\n"
      "command access level, this is the command to grab it with.")
  },
  {"timeoutincrease", ALLOW_CTRL, 
   /* TRANS: translate text between <> only */
   N_("timeoutincrease <turn> <turninc> <value> <valuemult>"), 
   N_("See \"help timeoutincrease\"."),
   N_("Every <turn> turns, add <value> to timeout timer, then add <turninc> "
      "to <turn> and multiply <value> by <valuemult>.  Use this command in "
      "concert with the option \"timeout\". Defaults are 0 0 0 1")
  },
  {"endgame",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("endgame <player1 player2 player3 ...>"),
   N_("End the game.  If players are listed, these win the game."),
   N_("This command ends the game immediately and credits the given players, "
      "if any, with winning it.")
  },
  {"remove",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("remove <player-name>"),
   N_("Fully remove player from game."),
   N_("This *completely* removes a player from the game, including "
      "all cities and units etc.  Use with care!")
  },
  {"save",	ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("save\n"
      "save <file-name>"),
   N_("Save game to file."),
   N_("Save the current game to file <file-name>.  If no file-name "
      "argument is given saves to \"<auto-save name prefix><year>m.sav[.gz]\".\n"
      "To reload a savegame created by 'save', start the server with "
      "the command-line argument:\n"
      "    --file <filename>\n"
      "and use the 'start' command once players have reconnected.")
  },
  {"load",      ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("load\n"
      "load <file-name>"),
   N_("Load game from file."),
   N_("Load a game from <file-name>. Any current data including players, "
      "rulesets and server options are lost.\n")
  },
  {"read",	ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("read <file-name>"),
   N_("Process server commands from file."), NULL
  },
  {"write",	ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("write <file-name>"),
   N_("Write current settings as server commands to file."), NULL
  },
  {"rfcstyle",	ALLOW_HACK,
   "rfcstyle",
   N_("Switch server output between 'RFC-style' and normal style."), NULL
  }
};

#endif
