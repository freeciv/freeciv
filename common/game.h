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
#include "player.h"

#define MAX_LEN_DEMOGRAPHY  16

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
  CLIENT_GAME_RUNNING_STATE
};

struct unit;
struct city;

#define OVERFLIGHT_NOTHING  1
#define OVERFLIGHT_FRIGHTEN 2

#define CONTAMINATION_POLLUTION 1
#define CONTAMINATION_FALLOUT   2

struct civ_game {
  int is_new_game;		/* 1 for games never started */
  int version;
  int civstyle;
  int gold;
  int settlers, explorer;
  int dispersion;
  int tech;
  int skill_level;
  int timeout;
  time_t turn_start;
  int end_year;
  int year;
  int techlevel;
  int diplcost, freecost, conquercost;
  int diplchance;
  int cityfactor;
  int civilwarsize;
  int min_players, max_players, nplayers;
  int aifill;
  int barbarianrate;
  int onsetbarbarian;
  int nbarbarians;
  int occupychance;
  int unhappysize;
  char *startmessage;
  int player_idx;
  struct player *player_ptr;
  struct player players[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  int global_advances[A_LAST];             /* a counter */
  int global_wonders[B_LAST];              /* contains city id's */
         /* global_wonders[] may also be (-1), or the id of a city
	    which no longer exists, if the wonder has been destroyed */
  int globalwarming;                       /* counter of how disturbed 
					      mother nature is */
  int heating;
  int warminglevel;
  int nuclearwinter;
  int cooling;
  int coolinglevel;
  char save_name[MAX_LEN_NAME];
  int save_nturns;
  int save_compress_level;
  int foodbox;
  int aqueductloss;
  int killcitizen;
  int techpenalty;
  int razechance;
  int scorelog;
  int randseed;
  int aqueduct_size;
  int sewer_size;
  int add_to_size_limit;
  int spacerace;
  int turnblock;
  int fixedlength;
  int auto_ai_toggle;
  int fogofwar;
  int fogofwar_old;	/* as the fog_of_war bit get changed by setting
			   the server we need to remember the old setting */

  int num_unit_types;
  int num_tech_types;  /* including A_NONE */

  int government_count;
  int default_government;
  int government_when_anarchy;
  int ai_goal_government;	/* kludge */

  int nation_count;
  int playable_nation_count;
  int styles_count;

  struct {
    char techs[MAX_LEN_NAME];
    char units[MAX_LEN_NAME];
    char buildings[MAX_LEN_NAME];
    char terrain[MAX_LEN_NAME];
    char governments[MAX_LEN_NAME];
    char nations[MAX_LEN_NAME];
    char cities[MAX_LEN_NAME];
    char game[MAX_LEN_NAME];
  } ruleset;
  int firepower_factor;		/* See README.rulesets */
  struct {
    int get_bonus_tech;		/* eg Philosophy */
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
    int min_city_center_food;
    int min_city_center_shield;
    int min_city_center_trade;
    int min_dist_bw_cities;
    int init_vis_radius_sq;
    int hut_overflight;
    int pillage_select;
    int nuke_contamination;
  } rgame;

  char demography[MAX_LEN_DEMOGRAPHY];
};

struct lvldat {
  int advspeed;
};

void game_init(void);
int game_next_year(int);
void game_advance_year(void);

int civ_population(struct player *pplayer);
struct city *game_find_city_by_name(char *name);

struct unit *find_unit_by_id(int id);
struct city *find_city_by_id(int id);

void game_remove_player(int plrno);
void game_remove_all_players(void);
void game_renumber_players(int plrno);

void game_remove_unit(int unit_id);
void game_remove_city(struct city *pcity);
int research_time(struct player *pplayer);
int total_player_citizens(struct player *pplayer);
int civ_score(struct player *pplayer);
void initialize_globals(void);

void translate_data_names(void);

struct player *get_player(int player_id);
int get_num_human_and_ai_players(void);

extern struct civ_game game;

#define GAME_DEFAULT_RANDSEED        0
#define GAME_MIN_RANDSEED            0
#define GAME_MAX_RANDSEED            (MAX_UINT32 >> 1)

#define GAME_DEFAULT_GOLD        50
#define GAME_MIN_GOLD            0
#define GAME_MAX_GOLD            5000

#define GAME_DEFAULT_SETTLERS    2
#define GAME_MIN_SETTLERS        1
#define GAME_MAX_SETTLERS        10

#define GAME_DEFAULT_EXPLORER    1
#define GAME_MIN_EXPLORER        0
#define GAME_MAX_EXPLORER        10

#define GAME_DEFAULT_DISPERSION  0
#define GAME_MIN_DISPERSION      0
#define GAME_MAX_DISPERSION      10

#define GAME_DEFAULT_TECHLEVEL   3
#define GAME_MIN_TECHLEVEL       0
#define GAME_MAX_TECHLEVEL       50

#define GAME_DEFAULT_UNHAPPYSIZE 4
#define GAME_MIN_UNHAPPYSIZE     1
#define GAME_MAX_UNHAPPYSIZE     6

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

#define GAME_DEFAULT_RESEARCHLEVEL   10
#define GAME_MIN_RESEARCHLEVEL       4
#define GAME_MAX_RESEARCHLEVEL       100

#define GAME_DEFAULT_DIPLCOST        0
#define GAME_MIN_DIPLCOST            0
#define GAME_MAX_DIPLCOST            100

#define GAME_DEFAULT_FOGOFWAR        1
#define GAME_MIN_FOGOFWAR            0
#define GAME_MAX_FOGOFWAR            1

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

#define GAME_DEFAULT_CIVILWARSIZE    10
#define GAME_MIN_CIVILWARSIZE        6
#define GAME_MAX_CIVILWARSIZE        1000

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

#define GAME_DEFAULT_SCORELOG        0
#define GAME_MIN_SCORELOG            0
#define GAME_MAX_SCORELOG            1

#define GAME_DEFAULT_SPACERACE       1
#define GAME_MIN_SPACERACE           0
#define GAME_MAX_SPACERACE           1

#define GAME_DEFAULT_AUTO_AI_TOGGLE  0
#define GAME_MIN_AUTO_AI_TOGGLE      0
#define GAME_MAX_AUTO_AI_TOGGLE      1

#define GAME_DEFAULT_TIMEOUT         0
#define GAME_MIN_TIMEOUT             0
#define GAME_MAX_TIMEOUT             8639999

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

#define GAME_DEFAULT_RULESET         "default"

#define GAME_DEFAULT_SKILL_LEVEL 3      /* easy */
#define GAME_OLD_DEFAULT_SKILL_LEVEL 5  /* normal; for old save games */

#define GAME_DEFAULT_DEMOGRAPHY      "NASRLPEMOqrb"

#define GAME_DEFAULT_COMPRESS_LEVEL 6    /* if we have compression */
#define GAME_MIN_COMPRESS_LEVEL     0
#define GAME_MAX_COMPRESS_LEVEL     9
#define GAME_NO_COMPRESS_LEVEL      0

#define GAME_DEFAULT_REPUTATION 1000
#define GAME_MAX_REPUTATION 1000
#define GAME_REPUTATION_INCR 2

#define GAME_START_YEAR -4000

#endif  /* FC__GAME_H */
