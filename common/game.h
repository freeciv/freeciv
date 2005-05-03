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
#ifndef FC__GAME_H
#define FC__GAME_H

#include <time.h>	/* time_t */

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include "shared.h"

#include "connection.h"		/* struct conn_list */
#include "fc_types.h"
#include "improvement.h"	/* Impr_Status */
#include "player.h"

/* Changing these will probably break network compatability. */
#define MAX_LEN_DEMOGRAPHY 16
#define MAX_LEN_ALLOW_TAKE 16
#define MAX_ID_LEN 33
#define MAX_GRANARY_INIS 24
#define MAX_LEN_STARTUNIT (20+1)

enum server_states { 
  PRE_GAME_STATE, 
  SELECT_RACES_STATE, 
  RUN_GAME_STATE,
  GAME_OVER_STATE
};

enum client_states { 
  CLIENT_BOOT_STATE,
  CLIENT_PRE_GAME_STATE,
  CLIENT_SELECT_RACE_STATE,
  CLIENT_WAITING_FOR_GAME_START_STATE,
  CLIENT_GAME_RUNNING_STATE,
  CLIENT_GAME_OVER_STATE
};

#define OVERFLIGHT_NOTHING  1
#define OVERFLIGHT_FRIGHTEN 2

#define CONTAMINATION_POLLUTION 1
#define CONTAMINATION_FALLOUT   2

struct civ_game {
  bool is_new_game;		/* 1 for games never started */
  int version;
  char id[MAX_ID_LEN];		/* server only */
  int civstyle;
  int gold;
  char start_units[MAX_LEN_STARTUNIT];
  int dispersion;
  int tech;
  int skill_level;
  int timeout;
  int timeoutint;     /* increase timeout every N turns... */
  int timeoutinc;     /* ... by this amount ... */
  int timeoutincmult; /* ... and multiply timeoutinc by this amount ... */
  int timeoutintinc;  /* ... and increase timeoutint by this amount */
  int timeoutcounter; /* timeoutcounter - timeoutint = turns to next inc. */
  int timeoutaddenemymove; /* increase to, when enemy move seen */
  int tcptimeout;
  int netwait;
  time_t last_ping;
  int pingtimeout;
  int pingtime;
  time_t turn_start;
  int end_year;
  int year;
  int turn;
  int researchcost; /* Multiplier on cost of new research */
  int diplcost, freecost, conquercost;
  int diplchance;
  int cityfactor;
  int citymindist;
  int civilwarsize;
  int contactturns;
  int rapturedelay;
  int min_players, max_players, nplayers;
  int aifill;
  int notradesize, fulltradesize;
  int barbarianrate;
  int onsetbarbarian;
  int nbarbarians;
  int occupychance;
  int unhappysize;
  bool angrycitizen;
  char *startmessage;
  int player_idx;
  struct player *player_ptr;
  struct player players[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  struct conn_list all_connections;        /* including not yet established */
  struct conn_list est_connections;        /* all established client conns */
  struct conn_list game_connections;       /* involved in game; send map etc */
  int global_advances[A_LAST];             /* a counter */
  int global_wonders[B_LAST];              /* contains city id's */
         /* global_wonders[] may also be (-1), or the id of a city
	    which no longer exists, if the wonder has been destroyed */
  Impr_Status improvements[B_LAST];        /* impr. with equiv_range==World */

  int heating; /* Number of polluted squares. */
  int globalwarming; /* Total damage done. (counts towards a warming event.) */
  int warminglevel; /* If globalwarming is higher than this number there is
		       a chance of a warming event. */

  int cooling; /* Number of irradiated squares. */
  int nuclearwinter; /* Total damage done. (counts towards a cooling event.) */
  int coolinglevel; /* If nuclearwinter is higher than this number there is
		       a chance of a cooling event. */

  char save_name[MAX_LEN_NAME];
  int save_nturns;
  int save_compress_level;
  int foodbox;
  int aqueductloss;
  int killcitizen;
  int techpenalty;
  int razechance;
  bool scorelog;
  int seed;
  int aqueduct_size;
  int add_to_size_limit;
  bool savepalace;
  bool natural_city_names;
  bool spacerace;
  bool turnblock;
  bool fixedlength;
  bool auto_ai_toggle;
  bool fogofwar;
  bool fogofwar_old;	/* as the fog_of_war bit get changed by setting
			   the server we need to remember the old setting */

  int num_unit_types;
  int num_impr_types;
  int num_tech_types;  /* including A_NONE */

  int government_count;
  int default_government;
  int government_when_anarchy;
  int ai_goal_government;	/* kludge */

  int nation_count;
  int playable_nation_count;
  int styles_count;

  int terrain_count;

  int watchtower_extra_vision;
  int watchtower_vision;
  int allowed_city_names;

  int borders;		/* distance of border from city; 0=disabled. */
  bool happyborders;
  int diplomacy;        /* who can do it */
  bool slow_invasions;  /* land units lose all movement landing on shores */

  char rulesetdir[MAX_LEN_NAME];
  int firepower_factor;		/* See README.rulesets */

  Impr_Type_id default_building;
  Impr_Type_id palace_building;
  Impr_Type_id land_defend_building;

  struct {
    int cathedral_plus;		/* eg Theology */
    int cathedral_minus;	/* eg Communism */
    int colosseum_plus;		/* eg Electricity */
    int temple_plus;		/* eg Mysticism */
    int nav;			/* AI convenience: tech_req for first
				   non-trireme ferryboat */
    int u_partisan;		/* convenience: tech_req for first
				   Partisan unit */
    /* Following tech list is A_LAST terminated if shorter than
       max len, and the techs listed are guaranteed to exist;
       this could be better implemented as a new field in the
       units.ruleset
    */
    int partisan_req[MAX_NUM_TECH_LIST];       /* all required for uprisings */
  } rtech;

  /* values from game.ruleset */
  struct {
    struct {
      char name[MAX_LEN_NAME];
      int min_size, bonus;
    } specialists[SP_COUNT];
    bool changable_tax;
    int forced_science; /* only relevant if !changable_tax */
    int forced_luxury;
    int forced_gold;
    int min_city_center_food;
    int min_city_center_shield;
    int min_city_center_trade;
    int min_dist_bw_cities;
    int init_vis_radius_sq;
    int hut_overflight;
    bool pillage_select;
    int nuke_contamination;
    int granary_food_ini[MAX_GRANARY_INIS];
    int granary_num_inis;
    int granary_food_inc;
    int tech_cost_style;
    int tech_leakage;
    int tech_cost_double_year;

    /* Items given to all players at game start.  Server only. */
    int global_init_techs[MAX_NUM_TECH_LIST];
    int global_init_buildings[MAX_NUM_BUILDING_LIST];

    bool killstack;
  } rgame;
  
  struct {
    int improvement_factor;
    int unit_factor;
    int total_factor;
  } incite_cost;

  char demography[MAX_LEN_DEMOGRAPHY];
  char allow_take[MAX_LEN_ALLOW_TAKE];

  /* used by the map editor to control game_save; could be used by the server too */
  struct {
    bool save_random;
    bool save_players;
    bool save_known; /* loading will just reveal the squares around cities and units */
    bool save_starts; /* start positions will be auto generated */
    bool save_private_map; /* FoW map; will be created if not saved */
  } save_options;

  int trireme_loss_chance[MAX_VET_LEVELS];
  int work_veteran_chance[MAX_VET_LEVELS];
  int veteran_chance[MAX_VET_LEVELS];
  int revolution_length; /* 0=> random length, else the fixated length */
};

/* Unused? */
struct lvldat {
  int advspeed;
};

/* Server setting types.  Changing these will break network compatability. */
enum sset_type {
  SSET_BOOL, SSET_INT, SSET_STRING
};

void game_init(void);
void game_free(void);
void ruleset_data_free(void);

int game_next_year(int);
void game_advance_year(void);

int civ_population(struct player *pplayer);
struct city *game_find_city_by_name(const char *name);

struct unit *find_unit_by_id(int id);
struct city *find_city_by_id(int id);

void game_remove_player(struct player *pplayer);
void game_renumber_players(int plrno);

void game_remove_unit(struct unit *punit);
void game_remove_city(struct city *pcity);
void initialize_globals(void);

void translate_data_names(void);

struct player *get_player(int player_id);
bool is_valid_player_id(int player_id);
int get_num_human_and_ai_players(void);

const char *population_to_text(int thousand_citizen);

extern struct civ_game game;
extern bool is_server;

#define GAME_DEFAULT_SEED        0
#define GAME_MIN_SEED            0
#define GAME_MAX_SEED            (MAX_UINT32 >> 1)

#define GAME_DEFAULT_GOLD        50
#define GAME_MIN_GOLD            0
#define GAME_MAX_GOLD            5000

#define GAME_DEFAULT_START_UNITS  "ccx"

#define GAME_DEFAULT_DISPERSION  0
#define GAME_MIN_DISPERSION      0
#define GAME_MAX_DISPERSION      10

#define GAME_DEFAULT_TECHLEVEL   0
#define GAME_MIN_TECHLEVEL       0
#define GAME_MAX_TECHLEVEL       50

#define GAME_DEFAULT_UNHAPPYSIZE 4
#define GAME_MIN_UNHAPPYSIZE     1
#define GAME_MAX_UNHAPPYSIZE     6

#define GAME_DEFAULT_ANGRYCITIZEN TRUE

#define GAME_DEFAULT_END_YEAR    2000
#define GAME_MIN_END_YEAR        GAME_START_YEAR
#define GAME_MAX_END_YEAR        5000

#define GAME_DEFAULT_MIN_PLAYERS     1
#define GAME_MIN_MIN_PLAYERS         1
#define GAME_MAX_MIN_PLAYERS         MAX_NUM_PLAYERS

#define GAME_DEFAULT_MAX_PLAYERS     MAX_NUM_PLAYERS
#define GAME_MIN_MAX_PLAYERS         1
#define GAME_MAX_MAX_PLAYERS         MAX_NUM_PLAYERS

#define GAME_DEFAULT_AIFILL          0
#define GAME_MIN_AIFILL              0
#define GAME_MAX_AIFILL              GAME_MAX_MAX_PLAYERS

#define GAME_DEFAULT_RESEARCHCOST	   20
#define GAME_MIN_RESEARCHCOST	       4
#define GAME_MAX_RESEARCHCOST        100

#define GAME_DEFAULT_DIPLCOST        0
#define GAME_MIN_DIPLCOST            0
#define GAME_MAX_DIPLCOST            100

#define GAME_DEFAULT_FOGOFWAR        TRUE

/* 0 means no national borders.  Performance dropps quickly as the border
 * distance increases (o(n^2) or worse). */
#define GAME_DEFAULT_BORDERS         7
#define GAME_MIN_BORDERS             0
#define GAME_MAX_BORDERS             24

#define GAME_DEFAULT_HAPPYBORDERS    TRUE

#define GAME_DEFAULT_SLOW_INVASIONS  TRUE

#define GAME_DEFAULT_DIPLOMACY       0
#define GAME_MIN_DIPLOMACY           0
#define GAME_MAX_DIPLOMACY           4

#define GAME_DEFAULT_DIPLCHANCE      80
#define GAME_MIN_DIPLCHANCE          1
#define GAME_MAX_DIPLCHANCE          99

#define GAME_DEFAULT_FREECOST        0
#define GAME_MIN_FREECOST            0
#define GAME_MAX_FREECOST            100

#define GAME_DEFAULT_CONQUERCOST     0
#define GAME_MIN_CONQUERCOST         0
#define GAME_MAX_CONQUERCOST         100

#define GAME_DEFAULT_CITYFACTOR      14
#define GAME_MIN_CITYFACTOR          6
#define GAME_MAX_CITYFACTOR          100

#define GAME_DEFAULT_CITYMINDIST     0
#define GAME_MIN_CITYMINDIST         0 /* if 0, ruleset will overwrite this */
#define GAME_MAX_CITYMINDIST         5

#define GAME_DEFAULT_CIVILWARSIZE    10
#define GAME_MIN_CIVILWARSIZE        6
#define GAME_MAX_CIVILWARSIZE        1000

#define GAME_DEFAULT_CONTACTTURNS    20
#define GAME_MIN_CONTACTTURNS        0
#define GAME_MAX_CONTACTTURNS        100

#define GAME_DEFAULT_RAPTUREDELAY    1
#define GAME_MIN_RAPTUREDELAY        1
#define GAME_MAX_RAPTUREDELAY        99 /* 99 practicaly disables rapturing */
 
#define GAME_DEFAULT_SAVEPALACE      TRUE

#define GAME_DEFAULT_NATURALCITYNAMES TRUE

#define GAME_DEFAULT_FOODBOX         10
#define GAME_MIN_FOODBOX             5
#define GAME_MAX_FOODBOX             30

#define GAME_DEFAULT_AQUEDUCTLOSS    0
#define GAME_MIN_AQUEDUCTLOSS        0
#define GAME_MAX_AQUEDUCTLOSS        100

#define GAME_DEFAULT_KILLCITIZEN     1
#define GAME_MIN_KILLCITIZEN         0
#define GAME_MAX_KILLCITIZEN         15

#define GAME_DEFAULT_TECHPENALTY     100
#define GAME_MIN_TECHPENALTY         0
#define GAME_MAX_TECHPENALTY         100

#define GAME_DEFAULT_RAZECHANCE      20
#define GAME_MIN_RAZECHANCE          0
#define GAME_MAX_RAZECHANCE          100

#define GAME_DEFAULT_CIVSTYLE        2
#define GAME_MIN_CIVSTYLE            1
#define GAME_MAX_CIVSTYLE            2

#define GAME_DEFAULT_SCORELOG        FALSE

#define GAME_DEFAULT_SPACERACE       TRUE

#define GAME_DEFAULT_TURNBLOCK       TRUE

#define GAME_DEFAULT_AUTO_AI_TOGGLE  FALSE

#define GAME_DEFAULT_TIMEOUT         0
#define GAME_DEFAULT_TIMEOUTINT      0
#define GAME_DEFAULT_TIMEOUTINTINC   0
#define GAME_DEFAULT_TIMEOUTINC      0
#define GAME_DEFAULT_TIMEOUTINCMULT  1
#define GAME_DEFAULT_TIMEOUTADDEMOVE 0

#ifndef NDEBUG
#define GAME_MIN_TIMEOUT             -1
#else
#define GAME_MIN_TIMEOUT             0
#endif
#define GAME_MAX_TIMEOUT             8639999

#define GAME_DEFAULT_TCPTIMEOUT      10
#define GAME_MIN_TCPTIMEOUT          0
#define GAME_MAX_TCPTIMEOUT          120

#define GAME_DEFAULT_NETWAIT         4
#define GAME_MIN_NETWAIT             0
#define GAME_MAX_NETWAIT             20

#define GAME_DEFAULT_PINGTIME        20
#define GAME_MIN_PINGTIME            1
#define GAME_MAX_PINGTIME            1800

#define GAME_DEFAULT_PINGTIMEOUT     60
#define GAME_MIN_PINGTIMEOUT         60
#define GAME_MAX_PINGTIMEOUT         1800

#define GAME_DEFAULT_NOTRADESIZE     0
#define GAME_MIN_NOTRADESIZE         0
#define GAME_MAX_NOTRADESIZE         49

#define GAME_DEFAULT_FULLTRADESIZE   1
#define GAME_MIN_FULLTRADESIZE       1
#define GAME_MAX_FULLTRADESIZE       50

#define GAME_DEFAULT_BARBARIANRATE   2
#define GAME_MIN_BARBARIANRATE       0
#define GAME_MAX_BARBARIANRATE       4

#define GAME_DEFAULT_ONSETBARBARIAN  (GAME_START_YEAR+ \
				      ((GAME_DEFAULT_END_YEAR-(GAME_START_YEAR))/3))
#define GAME_MIN_ONSETBARBARIAN      GAME_START_YEAR
#define GAME_MAX_ONSETBARBARIAN      GAME_MAX_END_YEAR

#define GAME_DEFAULT_OCCUPYCHANCE    0
#define GAME_MIN_OCCUPYCHANCE        0
#define GAME_MAX_OCCUPYCHANCE        100

#define GAME_DEFAULT_RULESETDIR      "default"

#define GAME_DEFAULT_SAVE_NAME       "civgame"

#define GAME_DEFAULT_SKILL_LEVEL 3      /* easy */
#define GAME_OLD_DEFAULT_SKILL_LEVEL 5  /* normal; for old save games */

#define GAME_DEFAULT_DEMOGRAPHY      "NASRLPEMOqrb"
#define GAME_DEFAULT_ALLOW_TAKE      "HAhadOo"

#define GAME_DEFAULT_COMPRESS_LEVEL 6    /* if we have compression */
#define GAME_MIN_COMPRESS_LEVEL     0
#define GAME_MAX_COMPRESS_LEVEL     9
#define GAME_NO_COMPRESS_LEVEL      0

#define GAME_DEFAULT_REPUTATION 1000
#define GAME_MAX_REPUTATION 1000
#define GAME_REPUTATION_INCR 2

#define GAME_DEFAULT_WATCHTOWER_VISION 2
#define GAME_MIN_WATCHTOWER_VISION 1
#define GAME_MAX_WATCHTOWER_VISION 3

#define GAME_DEFAULT_WATCHTOWER_EXTRA_VISION 0
#define GAME_MIN_WATCHTOWER_EXTRA_VISION 0
#define GAME_MAX_WATCHTOWER_EXTRA_VISION 2

#define GAME_DEFAULT_ALLOWED_CITY_NAMES 1
#define GAME_MIN_ALLOWED_CITY_NAMES 0
#define GAME_MAX_ALLOWED_CITY_NAMES 3

#define GAME_DEFAULT_REVOLUTION_LENGTH 0
#define GAME_MIN_REVOLUTION_LENGTH 0
#define GAME_MAX_REVOLUTION_LENGTH 10

#define GAME_START_YEAR -4000

#define specialist_type_iterate(sp)					    \
{									    \
  int sp;                                                                   \
                                                                            \
  for (sp = 0; sp < SP_COUNT; sp++) {

#define specialist_type_iterate_end                                         \
  }                                                                         \
}

#endif  /* FC__GAME_H */
