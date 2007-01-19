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
#ifndef FC__CITY_H
#define FC__CITY_H

#include "fc_types.h"
#include "improvement.h"
#include "unit.h"		/* struct unit_list */
#include "worklist.h"

enum production_class_type {
  TYPE_UNIT, TYPE_NORMAL_IMPROVEMENT, TYPE_WONDER
};

enum specialist_type {
  SP_ELVIS, SP_SCIENTIST, SP_TAXMAN, SP_COUNT
};

enum city_tile_type {
  C_TILE_EMPTY, C_TILE_WORKER, C_TILE_UNAVAILABLE
};

enum city_options {
  /* The first 4 are whether to auto-attack versus each unit move_type
   * from with auto-attack units within this city.  Note that these
   * should stay the first four, and must stay in the same order as
   * enum unit_move_type.  
   *
   * The next is whether building a settler at size 1 disbands a city.
   *
   * The following 2 are what to do of new citizens when the city grows:
   * make them workers, scientists, or taxmen. Should have only one set,
   * or if neither is set, that means make workers.
   *
   * Any more than 8 options requires a protocol extension, since
   * we only send 8 bits.
   */
  CITYO_ATT_LAND=0, CITYO_ATT_SEA, CITYO_ATT_HELI, CITYO_ATT_AIR,
  CITYO_DISBAND, CITYO_NEW_EINSTEIN, CITYO_NEW_TAXMAN
};

/* first four bits are for auto-attack: */
#define CITYOPT_AUTOATTACK_BITS 0xF

/* for new city: default auto-attack options all on, others off: */
#define CITYOPT_DEFAULT (CITYOPT_AUTOATTACK_BITS)


/* Changing this requires updating CITY_TILES and network capabilities. */
#define CITY_MAP_RADIUS 2

/* Diameter of the workable city area.  Some places harcdode this number. */
#define CITY_MAP_SIZE (CITY_MAP_RADIUS * 2 + 1) 

/* Number of tiles a city can use */
#define CITY_TILES city_tiles

#define INCITE_IMPOSSIBLE_COST (1000 * 1000 * 1000)

/*
 * Number of traderoutes a city can have.
 */
#define NUM_TRADEROUTES		4

/*
 * Size of the biggest possible city.
 *
 * The constant may be changed since it isn't externally visible.
 */
#define MAX_CITY_SIZE					100

/*
 * Iterate a city map.  This iterates over all city positions in the
 * city map (i.e., positions that are workable by the city) in unspecified
 * order.
 */
#define city_map_iterate(x, y)						    \
{									    \
  int _itr;								    \
  									    \
  for (_itr = 0; _itr < CITY_MAP_SIZE * CITY_MAP_SIZE; _itr++) {	    \
    const int x = _itr % CITY_MAP_SIZE, y = _itr / CITY_MAP_SIZE;	    \
    									    \
    if (is_valid_city_coords(x, y)) {

#define city_map_iterate_end			                            \
    }									    \
  }									    \
}

/* Iterate a city map, from the center (the city) outwards */
extern struct iter_index {
  int dx, dy, dist;
} *city_map_iterate_outwards_indices;
extern int city_tiles;

/* Iterate a city map, from the center (the city) outwards.
 * (city_x, city_y) will be the city coordinates. */
#define city_map_iterate_outwards(city_x, city_y)			    \
{									    \
  int city_x, city_y, _index;						    \
									    \
  for (_index = 0; _index < CITY_TILES; _index++) {			    \
    city_x = city_map_iterate_outwards_indices[_index].dx + CITY_MAP_RADIUS;\
    city_y = city_map_iterate_outwards_indices[_index].dy + CITY_MAP_RADIUS;

#define city_map_iterate_outwards_end                                       \
  }                                                                         \
}

/*
 * Iterate a city map in checked real map coordinates. The center of
 * the city is given as a map position (x0,y0). cx and cy will be
 * elements of [0,CITY_MAP_SIZE). mx and my will form the map position
 * (mx,my).
 */
#define city_map_checked_iterate(city_tile, cx, cy, itr_tile) {     \
  city_map_iterate_outwards(cx, cy) {                          \
    struct tile *itr_tile;				       \
    if ((itr_tile = base_city_map_to_map(city_tile, cx, cy))) {

#define city_map_checked_iterate_end \
    }                                \
  } city_map_iterate_outwards_end    \
}

/* Does the same thing as city_map_checked_iterate, but keeps the city
 * coordinates hidden. */
#define map_city_radius_iterate(city_tile, itr_tile)     \
{                                                                 \
  city_map_checked_iterate(city_tile, _cx, _cy, itr_tile) { 

#define map_city_radius_iterate_end                               \
  } city_map_checked_iterate_end;                                 \
}


enum choice_type { CT_NONE = 0, CT_BUILDING = 0, CT_NONMIL, CT_ATTACKER,
                   CT_DEFENDER, CT_LAST };

/* FIXME:

   This should detect also cases where type is just initialized with
   CT_NONE (probably in order to silence compiler warnings), but no real value
   is given. You have to change value of CT_BUILDING into 1 before you
   can add this check. It's left this way for now, is case hardcoded
   value 0 is still used somewhere instead of CT_BUILDING.

   -- Caz
*/
#define ASSERT_REAL_CHOICE_TYPE(type)                                    \
        assert(type >= 0 && type < CT_LAST /* && type != CT_NONE */ );


struct ai_choice {
  int choice;            /* what the advisor wants */
  int want;              /* how bad it wants it (0-100) */
  int type;              /* unit/building or other depending on question */
  bool need_boat;        /* unit being built wants a boat */
};

struct ai_city {
  /* building desirabilities - easiest to handle them here -- Syela */
  int building_want[B_LAST];    /* not sure these will always be < 256 */

  unsigned int danger;          /* danger to be compared to assess_defense */
  bool diplomat_threat;         /* enemy diplomat or spy is near the city */
  bool has_diplomat;            /* this city has diplomat or spy defender */
  unsigned int urgency;         /* how close the danger is; if zero, 
                                   bodyguards can leave */
  unsigned int grave_danger;    /* danger, should show positive feedback */
  int wallvalue;                /* how much it helps for defenders to be 
                                   ground units */
  int trade_want;               /* saves a zillion calculations */
  struct ai_choice choice;      /* to spend gold in the right place only */
  int downtown;                 /* distance from neighbours, for locating 
                                   wonders wisely */
  int distance_to_wonder_city;  /* wondercity will set this for us, 
                                   avoiding paradox */
  bool celebrate;               /* try to celebrate in this city */

  /* Used for caching when settlers evalueate which tile to improve,
     and when we place workers. */
  signed short int detox[CITY_MAP_SIZE][CITY_MAP_SIZE];
  signed short int derad[CITY_MAP_SIZE][CITY_MAP_SIZE];
  signed short int mine[CITY_MAP_SIZE][CITY_MAP_SIZE];
  signed short int irrigate[CITY_MAP_SIZE][CITY_MAP_SIZE];
  signed short int road[CITY_MAP_SIZE][CITY_MAP_SIZE];
  signed short int railroad[CITY_MAP_SIZE][CITY_MAP_SIZE];
  signed short int transform[CITY_MAP_SIZE][CITY_MAP_SIZE];
  signed short int tile_value[CITY_MAP_SIZE][CITY_MAP_SIZE];

  /* so we can contemplate with warmap fresh and decide later */
  int settler_want, founder_want; /* for builder (F_SETTLERS) and founder (F_CITIES) */
  int next_founder_want_recalc; /* do not recalc founder_want every turn */
  bool founder_boat; /* if the city founder will need a boat */
  int invasion; /* who's coming to kill us, for attack co-ordination */
  int attack, bcost; /* This is also for invasion - total power and value of
                      * all units coming to kill us. */

  int worth; /* Cache city worth here, sum of all weighted incomes */
  int next_recalc; /* Only recalc every Nth turn */
};

struct city {
  int id;
  int owner;
  struct tile *tile;
  char name[MAX_LEN_NAME];

  /* the people */
  int size;

  /* How the citizens feel:
     ppl_*[0] is distribution before any of the modifiers below.
     ppl_*[1] is distribution after luxury.
     ppl_*[2] is distribution after after building effects.
     ppl_*[3] is distribution after units enfored martial order.
     ppl_*[4] is distribution after wonders. (final result.) */
  int ppl_happy[5], ppl_content[5], ppl_unhappy[5], ppl_angry[5];

  /* Specialists */
  int specialists[SP_COUNT];

  /* trade routes */
  int trade[NUM_TRADEROUTES], trade_value[NUM_TRADEROUTES];

  /* the productions */
  int food_prod, food_surplus;
  /* Shield production is shields produced minus shield_waste*/
  int shield_prod, shield_surplus, shield_waste; 
  int trade_prod, corruption, tile_trade;

  /* Cached values for CPU savings. */
  int shield_bonus, luxury_bonus, tax_bonus, science_bonus;

  /* the totals */
  int luxury_total, tax_total, science_total;
  
  /* the physics */
  int food_stock;
  int shield_stock;
  int pollution;
  /* city can't be incited if INCITE_IMPOSSIBLE_COST */
  int incite_revolt_cost;      
   
  bool is_building_unit;    /* boolean unit/improvement */
  int currently_building;
  
  Impr_Status improvements[B_LAST];

  struct worklist worklist;

  enum city_tile_type city_map[CITY_MAP_SIZE][CITY_MAP_SIZE];

  struct unit_list units_supported;

  struct {
    /* Only used at the client (the serer is omniscient). */
    bool occupied;
    bool happy, unhappy;

    /* The color is an index into the city_colors array in mapview_common */
    bool colored;
    int color_index;
  } client;

  int steal;		      /* diplomats steal once; for spies, gets harder */
  /* turn states */
  bool did_buy;
  bool did_sell, is_updated;
  int turn_last_built;	      /* The last year in which something was built */
  int changed_from_id;	      /* If changed this turn, what changed from (id) */
  bool changed_from_is_unit;   /* If changed this turn, what changed from (unit?) */
  int disbanded_shields;      /* If you disband unit in a city. Count them */
  int caravan_shields;        /* If caravan has helped city to build wonder. */
  int before_change_shields;  /* If changed this turn, shields before penalty */
  int last_turns_shield_surplus; /* The surplus we had last turn. */
  int anarchy;		      /* anarchy rounds count */ 
  int rapture;                /* rapture rounds count */ 
  bool was_happy;
  bool airlift;
  int original;			/* original owner */
  int city_options;		/* bitfield; positions as enum city_options */

  /* server variable. indicates if the city map is synced with the client. */
  bool synced;
  struct {
    /* If > 0, workers will not be rearranged until they are unfrozen. */
    int workers_frozen;

    /* If set, workers need to be arranged when the city is unfrozen.  Only
     * set inside auto_arrange_workers. */
    bool needs_arrange;
  } server;

  int turn_founded;		/* In which turn was the city founded? */

  /* info for dipl/spy investigation -- used only in client */
  struct unit_list info_units_supported;
  struct unit_list info_units_present;

  struct ai_city ai;
  bool debug;
};

/* city drawing styles */

#define MAX_CITY_TILES 8

struct citystyle {
  const char *name; /* Translated string - doesn't need freeing. */
  char name_orig[MAX_LEN_NAME];	      /* untranslated */
  char graphic[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  char citizens_graphic[MAX_LEN_NAME];
  char citizens_graphic_alt[MAX_LEN_NAME];
  int techreq;                  /* tech required to use a style      */
  int replaced_by;              /* index to replacing style          */
                                /* client side-only:                 */
  int tresh[MAX_CITY_TILES];    /* treshholds - what city size to use a tile */
  int tiles_num;                /* number of "normal" city tiles,    */
};                              /* not incl. wall and occupied tiles */

extern struct citystyle *city_styles;

/* get 'struct city_list' and related functions: */
#define SPECLIST_TAG city
#define SPECLIST_TYPE struct city
#include "speclist.h"

#define city_list_iterate(citylist, pcity) \
    TYPED_LIST_ITERATE(struct city, citylist, pcity)
#define city_list_iterate_end  LIST_ITERATE_END

#define cities_iterate(pcity)                                               \
{                                                                           \
  players_iterate(CI_player) {                                              \
    city_list_iterate(CI_player->cities, pcity) {

#define cities_iterate_end                                                  \
    } city_list_iterate_end;                                                \
  } players_iterate_end;                                                    \
}


/* properties */

struct player *city_owner(const struct city *pcity);
int city_population(const struct city *pcity);
int city_gold_surplus(const struct city *pcity, int tax_total);
int city_buy_cost(const struct city *pcity);
bool city_happy(const struct city *pcity);  /* generally use celebrating instead */
bool city_unhappy(const struct city *pcity);                /* anarchy??? */
bool base_city_celebrating(const struct city *pcity);
bool city_celebrating(const struct city *pcity);            /* love the king ??? */
bool city_rapture_grow(const struct city *pcity);

/* city related improvement and unit functions */

bool city_has_terr_spec_gate(const struct city *pcity, Impr_Type_id id); 
int improvement_upkeep(const struct city *pcity, Impr_Type_id i); 
bool can_build_improvement_direct(const struct city *pcity, Impr_Type_id id);
bool can_build_improvement(const struct city *pcity, Impr_Type_id id);
bool can_eventually_build_improvement(const struct city *pcity,
				      Impr_Type_id id);
bool can_build_unit(const struct city *pcity, Unit_Type_id id);
bool can_build_unit_direct(const struct city *pcity, Unit_Type_id id);
bool can_eventually_build_unit(const struct city *pcity, Unit_Type_id id);
bool city_can_use_specialist(const struct city *pcity,
			     Specialist_type_id type);
bool city_got_building(const struct city *pcity,  Impr_Type_id id); 
bool is_capital(const struct city *pcity);
bool city_got_citywalls(const struct city *pcity);
bool building_replaced(const struct city *pcity, Impr_Type_id id);
int city_change_production_penalty(const struct city *pcity,
				   int target, bool is_unit);
int city_turns_to_build(const struct city *pcity, int id, bool id_is_unit,
                        bool include_shield_stock );
int city_turns_to_grow(const struct city *pcity);
bool city_can_grow_to(const struct city *pcity, int pop_size);
bool city_can_change_build(const struct city *pcity);

/* textual representation of buildings */

const char *get_impr_name_ex(const struct city *pcity, Impr_Type_id id);

/* tile production functions */

int get_shields_tile(const struct tile *ptile); /* shield on spot */
int get_trade_tile(const struct tile *ptile); /* trade on spot */
int get_food_tile(const struct tile *ptile); /* food on spot */

/* city map functions */

bool is_valid_city_coords(const int city_x, const int city_y);
bool map_to_city_map(int *city_map_x, int *city_map_y,
		     const struct city *const pcity,
		     const struct tile *ptile);

struct tile *base_city_map_to_map(const struct tile *city_center_tile,
				  int city_map_x, int city_map_y);
struct tile *city_map_to_map(const struct city *const pcity,
			     int city_map_x, int city_map_y);

/* Initialization functions */
int compare_iter_index(const void *a, const void *b);
void generate_city_map_indices(void);

/* shield on spot */
int city_get_shields_tile(int city_x, int city_y, const struct city *pcity);
int base_city_get_shields_tile(int city_x, int city_y,
			       const struct city *pcity, bool is_celebrating);
/* trade  on spot */
int city_get_trade_tile(int city_x, int city_y, const struct city *pcity);
int base_city_get_trade_tile(int city_x, int city_y,
			     const struct city *pcity, bool is_celebrating);
/* food   on spot */
int city_get_food_tile(int city_x, int city_y, const struct city *pcity);
int base_city_get_food_tile(int city_x, int city_y,
			    const struct city *pcity, bool is_celebrating);

void set_worker_city(struct city *pcity, int city_x, int city_y,
		     enum city_tile_type type); 
enum city_tile_type get_worker_city(const struct city *pcity, int city_x,
				    int city_y);
void get_worker_on_map_position(const struct tile *ptile,
				enum city_tile_type *result_city_tile_type,
				struct city **result_pcity);
bool is_worker_here(const struct city *pcity, int city_x, int city_y);

bool city_can_be_built_here(const struct tile *ptile, struct unit *punit);

/* trade functions */
bool can_cities_trade(const struct city *pc1, const struct city *pc2);
bool can_establish_trade_route(const struct city *pc1, const struct city *pc2);
bool have_cities_trade_route(const struct city *pc1, const struct city *pc2);
int trade_between_cities(const struct city *pc1, const struct city *pc2);
int city_num_trade_routes(const struct city *pcity);
int get_caravan_enter_city_trade_bonus(const struct city *pc1, 
                                       const struct city *pc2);
int get_city_min_trade_route(const struct city *pcity, int *slot);
  
/* list functions */
struct city *city_list_find_id(struct city_list *This, int id);
struct city *city_list_find_name(struct city_list *This, const char *name);

int city_name_compare(const void *p1, const void *p2);

/* city free cost values depending on government: */
int citygov_free_shield(const struct city *pcity, struct government *gov);
int citygov_free_happy(const struct city *pcity, struct government *gov);
int citygov_free_food(const struct city *pcity, struct government *gov);

/* city style functions */
int get_city_style(const struct city *pcity);
int get_player_city_style(struct player *plr);
int get_style_by_name(const char *);
int get_style_by_name_orig(const char *);
const char *get_city_style_name(int style);
char* get_city_style_name_orig(int style);

struct city *is_enemy_city_tile(const struct tile *ptile,
				struct player *pplayer);
struct city *is_allied_city_tile(const struct tile *ptile,
				 struct player *pplayer);
struct city *is_non_attack_city_tile(const struct tile *ptile,
				     struct player *pplayer);
struct city *is_non_allied_city_tile(const struct tile *ptile,
				     struct player *pplayer);

bool is_unit_near_a_friendly_city(struct unit *punit);
bool is_friendly_city_near(struct player *owner, const struct tile *ptile);
bool city_exists_within_city_radius(const struct tile *ptile,
				    bool may_be_on_center);

/* granary size as a function of city size */
int city_granary_size(int city_size);

void city_add_improvement(struct city *pcity,Impr_Type_id impr);
void city_remove_improvement(struct city *pcity,Impr_Type_id impr);

/* city update functions */
void generic_city_refresh(struct city *pcity,
			  bool refresh_trade_route_cities,
			  void (*send_unit_info) (struct player * pplayer,
						  struct unit * punit));
void adjust_city_free_cost(int *num_free, int *this_cost);
int city_corruption(const struct city *pcity, int trade);
int city_waste(const struct city *pcity, int shields);
int city_specialists(const struct city *pcity);                 /* elv+tax+scie */
const char *specialists_string(const int *specialists);
int get_city_tax_bonus(const struct city *pcity);
int get_city_luxury_bonus(const struct city *pcity);
int get_city_shield_bonus(const struct city *pcity);
int get_city_science_bonus(const struct city *pcity);
bool city_built_last_turn(const struct city *pcity);

/* city creation / destruction */
struct city *create_city_virtual(struct player *pplayer, struct tile *ptile,
				 const char *name);
void remove_city_virtual(struct city *pcity);

/* misc */
bool is_city_option_set(const struct city *pcity, enum city_options option);
void city_styles_alloc(int num);
void city_styles_free(void);

void get_food_trade_shields(const struct city *pcity, int *food, int *trade,
                            int *shields);
void get_tax_income(struct player *pplayer, int trade, int *sci,
                    int *lux, int *tax);
int get_city_tithes_bonus(const struct city *pcity);
int city_pollution(struct city *pcity, int shield_total);

/*
 * Iterates over all improvements which are built in the given city.
 */
#define built_impr_iterate(m_pcity, m_i)                                      \
  impr_type_iterate(m_i) {                                                    \
    if((m_pcity)->improvements[m_i] == I_NONE) {                              \
      continue;                                                               \
    }

#define built_impr_iterate_end                                                \
  } impr_type_iterate_end;

/**************************************************************************
  Return TRUE iff the given city coordinate pair is the center tile of
  the citymap.
**************************************************************************/
static inline bool is_city_center(int city_x, int city_y)
{
  return CITY_MAP_RADIUS == city_x && CITY_MAP_RADIUS == city_y;
}

#endif  /* FC__CITY_H */
