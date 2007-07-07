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
#ifndef FC__PLAYER_H
#define FC__PLAYER_H

#include "city.h"
#include "connection.h"
#include "fc_types.h"
#include "improvement.h"	/* Impr_Status */
#include "nation.h"
#include "shared.h"
#include "spaceship.h"
#include "tech.h"
#include "unit.h"

#define PLAYER_DEFAULT_TAX_RATE 0
#define PLAYER_DEFAULT_SCIENCE_RATE 100
#define PLAYER_DEFAULT_LUXURY_RATE 0

#define ANON_PLAYER_NAME "noname"
#define ANON_USER_NAME  "Unassigned"
#define OBSERVER_NAME	"Observer"

/*
 * pplayer->ai.barbarian_type uses this enum. Note that the values
 * have to stay since they are used in savegames.
 */
enum barbarian_type {
  NOT_A_BARBARIAN = 0,
  LAND_BARBARIAN = 1,
  SEA_BARBARIAN = 2
};

enum handicap_type {
  H_NONE = 0,         /* No handicaps */
  H_DIPLOMAT = 1,     /* Can't build offensive diplomats */
  H_AWAY = 2,         /* Away mode */
  H_LIMITEDHUTS = 4,  /* Can get only 25 gold and barbs from huts */
  H_DEFENSIVE = 8,    /* Build defensive buildings without calculating need */
  H_EXPERIMENTAL = 16,/* Enable experimental AI features (for testing) */
  H_RATES = 32,       /* Can't set its rates beyond government limits */
  H_TARGETS = 64,     /* Can't target anything it doesn't know exists */
  H_HUTS = 128,       /* Doesn't know which unseen tiles have huts on them */
  H_FOG = 256,        /* Can't see through fog of war */
  H_NOPLANES = 512,   /* Doesn't build air units */
  H_MAP = 1024,       /* Only knows map_is_known tiles */
  H_DIPLOMACY = 2048, /* Not very good at diplomacy */
  H_REVOLUTION = 4096 /* Cannot skip anarchy */
};

struct player_economic {
  int gold;
  int tax;
  int science;
  int luxury;
};

struct player_research {
  int bulbs_last_turn;    /* # bulbs researched last turn only */
  int bulbs_researched;   /* # bulbs reseached for the current tech */    
  int techs_researched;   /* # techs the player has researched/acquired */
  /* 
   * Invention being researched in. Valid values for researching are:
   *  - any existing tech but not A_NONE or
   *  - A_FUTURE.
   * In addition A_NOINFO is allowed at the client for enemies.
   */
  int researching;        
  int changed_from;       /* if the player changed techs, which one
			     changed from */
  int bulbs_researched_before;  /* if the player changed techs, how
				   many points they had before the
				   change */
  struct {
    /* One of TECH_UNKNOWN, TECH_KNOWN or TECH_REACHABLE. */
    enum tech_state state;

    /* 
     * required_techs, num_required_techs and bulbs_required are
     * cached values. Updated from build_required_techs (which is
     * called by update_research).
     */
    tech_vector required_techs;
    int num_required_techs, bulbs_required;
  } inventions[A_LAST];

  /*
   * Cached values. Updated by update_research.
   */
  int num_known_tech_with_flag[TF_LAST];
};

struct player_score {
  int happy;
  int content;
  int unhappy;
  int angry;
  int taxmen;
  int scientists;
  int elvis;
  int wonders;
  int techs;
  int techout;
  int landarea;
  int settledarea;
  int population; 	/* in thousand of citizen */
  int cities;
  int units;
  int pollution;
  int literacy;
  int bnp;
  int mfg;
  int spaceship;
};

struct player_ai {
  bool control;

  /* 
   * Valid values for tech_goal are:
   *  - any existing tech but not A_NONE or
   *  - A_UNSET.
   */
  int tech_goal;
  int prev_gold;
  int maxbuycost;
  int est_upkeep; /* estimated upkeep of buildings in cities */
  int tech_want[A_LAST+1];
  int handicap;			/* sum of enum handicap_type */
  int skill_level;		/* 0-10 value for save/load/display */
  int fuzzy;			/* chance in 1000 to mis-decide */
  int expand;			/* percentage factor to value new cities */
  int science_cost;             /* Cost in bulbs to get new tech, relative
                                   to non-AI players (100: Equal cost) */
  int warmth; /* threat of global warming */
  enum barbarian_type barbarian_type;

  int love[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
};

/* Diplomatic states (how one player views another).
 * (Some diplomatic states are "pacts" (mutual agreements), others aren't.)
 *
 * Adding to or reordering this array will break many things.
 */
enum diplstate_type {
  DS_NEUTRAL = 0,
  DS_WAR,
  DS_CEASEFIRE,
  DS_PEACE,
  DS_ALLIANCE,
  DS_NO_CONTACT,
  DS_TEAM,
  DS_LAST	/* leave this last */
};

struct player_diplstate {
  enum diplstate_type type;	/* this player's disposition towards other */
  /* the following are for "pacts" */
  int turns_left;		/* until pact (e.g., cease-fire) ends */
  int has_reason_to_cancel;	/* 0: no, 1: this turn, 2: this or next turn */
  int contact_turns_left;	/* until contact ends */
};

/***************************************************************************
  On the distinction between nations(formerly races), players, and users,
  see doc/HACKING
***************************************************************************/

struct attribute_block_s {
  void *data;
  int length;
#define MAX_ATTRIBUTE_BLOCK     (256*1024)	/* largest attribute block */
};

struct player {
  int player_no;
  char name[MAX_LEN_NAME];
  char username[MAX_LEN_NAME];
  bool is_male;
  int government;
  int target_government;
  Nation_Type_id nation;
  Team_Type_id team;
  bool is_started; /* Did the player click "start" yet? */
  bool turn_done;
  int nturns_idle;
  bool is_alive;
  bool is_observer; /* is the player a global observer */ 
  bool is_dying; /* set once the player is in the process of dying */
  bool got_tech; /* set once the player is fully dead */

  /* Turn in which the player's revolution is over; see update_revolution. */
  int revolution_finishes;

  bool capital; /* used to give player init_buildings in first city. */
  int embassy;
  int reputation;
  struct player_diplstate diplstates[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  int city_style;
  struct unit_list units;
  struct city_list cities;
  struct player_score score;
  struct player_economic economic;
  struct player_research research;
  struct player_spaceship spaceship;
  int future_tech;
  struct player_ai ai;
  bool was_created;                    /* if the player was /created */
  bool is_connected;		       /* observers don't count */
  struct connection *current_conn;     /* non-null while handling packet */
  struct conn_list connections;	       /* will replace conn */
  struct worklist worklists[MAX_NUM_WORKLISTS];
  struct player_tile *private_map;
  unsigned int gives_shared_vision; /* bitvector those that give you shared vision */
  unsigned int really_gives_vision; /* takes into account that p3 may see what p1 has via p2 */
  Impr_Status improvements[B_LAST]; /* improvements with equiv_range==Player */
  Impr_Status *island_improv; /* improvements with equiv_range==Island, dimensioned to
			 	 [map.num_continents][game.num_impr_types] */

  struct attribute_block_s attribute_block;
  struct attribute_block_s attribute_block_buffer;
  bool debug;
};

void player_init(struct player *plr);
struct player *find_player_by_name(const char *name);
struct player *find_player_by_name_prefix(const char *name,
					  enum m_pre_result *result);
struct player *find_player_by_user(const char *name);
void player_set_unit_focus_status(struct player *pplayer);
bool player_has_embassy(struct player *pplayer, struct player *pplayer2);

bool can_player_see_unit(struct player *pplayer, struct unit *punit);
bool can_player_see_unit_at(struct player *pplayer, struct unit *punit,
			    struct tile *ptile);

bool can_player_see_units_in_city(struct player *pplayer,
				  struct city *pcity);
bool can_player_see_city_internals(struct player *pplayer,
				   struct city *pcity);

bool player_owns_city(struct player *pplayer, struct city *pcity);

struct city *player_find_city_by_id(const struct player *pplayer,
				    int city_id);
struct unit *player_find_unit_by_id(const struct player *pplayer,
				    int unit_id);

bool player_in_city_radius(struct player *pplayer, struct tile *ptile);
bool player_knows_improvement_tech(struct player *pplayer,
				   Impr_Type_id id);
bool player_knows_techs_with_flag(struct player *pplayer,
				 enum tech_flag_id flag);
int num_known_tech_with_flag(struct player *pplayer, enum tech_flag_id flag);
int player_get_expected_income(struct player *pplayer);

void player_limit_to_government_rates(struct player *pplayer);

struct city *find_palace(struct player *pplayer);

bool ai_handicap(struct player *pplayer, enum handicap_type htype);
bool ai_fuzzy(struct player *pplayer, bool normal_decision);

const char *reputation_text(const int rep);
const char *diplstate_text(const enum diplstate_type type);
const char *love_text(const int love);

const struct player_diplstate *pplayer_get_diplstate(const struct player
						     *pplayer,
						     const struct player
						     *pplayer2);
bool are_diplstates_equal(const struct player_diplstate *pds1,
			  const struct player_diplstate *pds2);
bool pplayer_can_ally(struct player *p1, struct player *p2);
bool pplayers_at_war(const struct player *pplayer,
		    const struct player *pplayer2);
bool pplayers_allied(const struct player *pplayer,
		    const struct player *pplayer2);
bool pplayers_in_peace(const struct player *pplayer,
                    const struct player *pplayer2);
bool pplayers_non_attack(const struct player *pplayer,
			const struct player *pplayer2);
bool players_on_same_team(const struct player *pplayer1,
                          const struct player *pplayer2);
int player_in_territory(struct player *pplayer, struct player *pplayer2);

bool is_barbarian(const struct player *pplayer);

bool gives_shared_vision(struct player *me, struct player *them);

#define players_iterate(PI_player)                                            \
{                                                                             \
  struct player *PI_player;                                                   \
  int PI_p_itr;                                                               \
  for (PI_p_itr = 0; PI_p_itr < game.nplayers; PI_p_itr++) {                  \
    PI_player = get_player(PI_p_itr);

#define players_iterate_end                                                   \
  }                                                                           \
}

/* ai love values should be in range [-MAX_AI_LOVE..MAX_AI_LOVE] */
#define MAX_AI_LOVE 1000


/* User functions. */
bool is_valid_username(const char *name);


#endif  /* FC__PLAYER_H */
