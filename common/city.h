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
#include "unitlist.h"
#include "vision.h"
#include "worklist.h"

enum production_class_type {
  PCT_UNIT,
  PCT_NORMAL_IMPROVEMENT,
  PCT_WONDER,
  PCT_LAST
};

enum city_tile_type {
  C_TILE_EMPTY,
  C_TILE_WORKER,
  C_TILE_UNAVAILABLE
};

/* Various city options.  These are stored by the server and can be
 * toggled by the user.  Each one defaults to off.  Adding new ones
 * will break network compatibility.  Reordering them will break savegame
 * compatibility.  If you want to remove one you should replace it with
 * a CITYO_UNUSED entry; new options can just be added at the end.*/
enum city_options {
  CITYO_DISBAND,      /* If building a settler at size 1 disbands the city */
  CITYO_NEW_EINSTEIN, /* If new citizens are science specialists */
  CITYO_NEW_TAXMAN,   /* If new citizens are gold specialists */
  CITYO_LAST
};
BV_DEFINE(bv_city_options, CITYO_LAST);

/* Changing this requires updating CITY_TILES and network capabilities. */
#define CITY_MAP_RADIUS 2

/* The city includes all tiles dx^2 + dy^2 <= CITY_MAP_RADIUS_SQ */
#define CITY_MAP_RADIUS_SQ (CITY_MAP_RADIUS * CITY_MAP_RADIUS + 1)

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
#define city_map_iterate(x, y) city_map_iterate_outwards(x, y)
#define city_map_iterate_end city_map_iterate_outwards_end

/* Iterate a city map, from the center (the city) outwards */
extern struct iter_index {
  int dx, dy, dist;
} *city_map_iterate_outwards_indices;
extern int city_tiles;

/* Iterate a city map, from the center (the city) outwards.
 * (_x, _y) will be the city coordinates. */
#define city_map_iterate_outwards(_x, _y)				\
{									\
  int _x, _y;								\
  int _x##_y##_index = 0;						\
  for (;								\
       _x##_y##_index < CITY_TILES;					\
       _x##_y##_index++) {						\
    _x = CITY_MAP_RADIUS						\
       + city_map_iterate_outwards_indices[_x##_y##_index].dx;		\
    _y = CITY_MAP_RADIUS						\
       + city_map_iterate_outwards_indices[_x##_y##_index].dy;

#define city_map_iterate_outwards_end					\
  }									\
}

/*
 * Iterate a city map in checked real map coordinates. The center of
 * the city is given as a map position (x0,y0). cx and cy will be
 * elements of [0,CITY_MAP_SIZE). mx and my will form the map position
 * (mx,my).
 */
#define city_map_checked_iterate(city_tile, cx, cy, _tile) {		\
  city_map_iterate_outwards(cx, cy) {					\
    struct tile *_tile = base_city_map_to_map(city_tile, cx, cy);	\
    if (NULL != _tile) {

#define city_map_checked_iterate_end					\
    }									\
  } city_map_iterate_outwards_end					\
}

/* Does the same thing as city_map_checked_iterate, but keeps the city
 * coordinates hidden. */
#define map_city_radius_iterate(city_tile, _tile)			\
{									\
  city_map_checked_iterate(city_tile, _tile##_x, _tile##_y, _tile) { 

#define map_city_radius_iterate_end					\
  } city_map_checked_iterate_end;					\
}

/* Improvement status (for cities' lists of improvements)
 * (replaced Impr_Status) */

struct built_status {
  int turn;			/* turn built, negative for old state */
#define I_NEVER		(-1)	/* Improvement never built */
#define I_DESTROYED	(-2)	/* Improvement built and destroyed */
};

/* How much this output type is penalized for unhappy cities: not at all,
 * surplus knocked down to 0, or all production removed. */
enum output_unhappy_penalty {
  UNHAPPY_PENALTY_NONE,
  UNHAPPY_PENALTY_SURPLUS,
  UNHAPPY_PENALTY_ALL_PRODUCTION
};

struct output_type {
  int index;
  const char *name; /* Untranslated name */
  const char *id;   /* Identifier string (for rulesets, etc.) */
  bool harvested;   /* Is this output type gathered by city workers? */
  enum output_unhappy_penalty unhappy_penalty;
};

enum choice_type {
  CT_NONE = 0,
  CT_BUILDING = 1,
  CT_CIVILIAN,
  CT_ATTACKER,
  CT_DEFENDER,
  CT_LAST
};

#define ASSERT_CHOICE(c)                                                 \
  do {                                                                   \
    if ((c).want > 0) {                                                  \
      assert((c).type > CT_NONE && (c).type < CT_LAST);                  \
      if ((c).type == CT_BUILDING) {                                     \
        int _iindex = improvement_index((c).value.building);             \
        assert(_iindex >= 0 && _iindex < improvement_count());           \
      } else {                                                           \
        int _uindex = utype_index((c).value.utype);                      \
        assert(_uindex >= 0 && _uindex < utype_count());                 \
      }                                                                  \
    }                                                                    \
  } while(0);

struct ai_choice {
  enum choice_type type;
  universals_u value; /* what the advisor wants */
  int want;              /* how much it wants it (0-100) */
  bool need_boat;        /* unit being built wants a boat */
};

struct ai_city {
  /* building desirabilities - easiest to handle them here -- Syela */
  /* The units of building_want are output
   * (shields/gold/luxuries) multiplied by a priority
   * (SHIELD_WEIGHTING, etc or ai->shields_priority, etc)
   */
  int building_want[B_LAST];

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

  /* Used for caching change in value from a worker performing
   * a particular activity on a particular tile. */
  int act_value[ACTIVITY_LAST][CITY_MAP_SIZE][CITY_MAP_SIZE];

  /* so we can contemplate with warmap fresh and decide later */
  /* These values are for builder (F_SETTLERS) and founder (F_CITIES) units.
   * Negative values indicate that the city needs a boat first;
   * -value is the degree of want in that case. */
  int settler_want, founder_want;
  int next_founder_want_recalc; /* do not recalc founder_want every turn */
  bool founder_boat; /* if the city founder will need a boat */
  int invasion; /* who's coming to kill us, for attack co-ordination */
  int attack, bcost; /* This is also for invasion - total power and value of
                      * all units coming to kill us. */

  int worth; /* Cache city worth here, sum of all weighted incomes */
  /* Only recalc every Nth turn: */
  int recalc_interval; /* Use for weighting values calculated every Nth turn */
  int next_recalc;
};

enum citizen_category {
  CITIZEN_HAPPY,
  CITIZEN_CONTENT,
  CITIZEN_UNHAPPY,
  CITIZEN_ANGRY,
  CITIZEN_LAST,
  CITIZEN_SPECIALIST = CITIZEN_LAST,
};

/* changing this order will break network compatibility,
 * and clients that don't use the symbols. */
enum citizen_feeling {
  FEELING_BASE,		/* before any of the modifiers below */
  FEELING_LUXURY,	/* after luxury */
  FEELING_EFFECT,	/* after building effects */
  FEELING_MARTIAL,	/* after units enforce martial order */
  FEELING_FINAL,	/* after wonders (final result) */
  FEELING_LAST
};

struct city {
  int id;
  struct player *owner; /* Cannot be NULL. */
  struct tile *tile;
  char name[MAX_LEN_NAME];

  /* the people */
  int size;

  int feel[CITIZEN_LAST][FEELING_LAST];

  /* Specialists */
  int specialists[SP_MAX];

  /* trade routes */
  int trade[NUM_TRADEROUTES], trade_value[NUM_TRADEROUTES];

  /* Tile output, regardless of if the tile is actually worked. */
  unsigned char tile_output[CITY_MAP_SIZE][CITY_MAP_SIZE][O_MAX];

  /* the productions */
  int surplus[O_MAX]; /* Final surplus in each category. */
  int waste[O_MAX]; /* Waste/corruption in each category. */
  int unhappy_penalty[O_MAX]; /* Penalty from unhappy cities. */
  int prod[O_MAX]; /* Production is total minus waste and penalty. */
  int citizen_base[O_MAX]; /* Base production from citizens. */
  int usage[O_MAX]; /* Amount of each resource being used. */

  /* Cached values for CPU savings. */
  int bonus[O_MAX];

  int martial_law; /* Number of citizens pacified by martial law. */
  int unit_happy_upkeep; /* Number of citizens angered by military action. */

  /* the physics */
  int food_stock;
  int shield_stock;
  int pollution;

  struct universal production;

  struct built_status built[B_LAST];

  struct worklist worklist;

  enum city_tile_type city_map[CITY_MAP_SIZE][CITY_MAP_SIZE];

  struct unit_list *units_supported;

  struct {
    /* Only used at the client (the server is omniscient). */
    bool occupied;
    bool happy, unhappy;

    /* The color is an index into the city_colors array in mapview_common */
    bool colored;
    int color_index;

    bool walls;
  } client;

  int steal;		      /* diplomats steal once; for spies, gets harder */
  /* turn states */
  bool did_buy;
  bool did_sell, is_updated;
  int turn_last_built;	      /* The last year in which something was built */

  /* If changed this turn, what we changed from */
  struct universal changed_from;

  int disbanded_shields;      /* If you disband unit in a city. Count them */
  int caravan_shields;        /* If caravan has helped city to build wonder. */
  int before_change_shields;  /* If changed this turn, shields before penalty */
  int last_turns_shield_surplus; /* The surplus we had last turn. */
  int anarchy;		      /* anarchy rounds count */ 
  int rapture;                /* rapture rounds count */ 
  bool was_happy;
  bool airlift;
  struct player *original;	/* original owner - cannot be NULL */
  bv_city_options city_options;

  /* server variable. indicates if the city map is synced with the client. */
  bool synced;
  struct {
    /* If > 0, workers will not be rearranged until they are unfrozen. */
    int workers_frozen;

    /* If set, workers need to be arranged when the city is unfrozen.  Only
     * set inside auto_arrange_workers. */
    bool needs_arrange;

    struct vision *vision;
  } server;

  int turn_founded;		/* In which turn was the city founded? */

  /* info for dipl/spy investigation -- used only in client */
  struct unit_list *info_units_supported;
  struct unit_list *info_units_present;

  struct ai_city ai;
  bool debug;
};

struct citystyle {
  struct name_translation name;
  char graphic[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  char citizens_graphic[MAX_LEN_NAME];
  char citizens_graphic_alt[MAX_LEN_NAME];
  struct requirement_vector reqs;
  int replaced_by;              /* index to replacing style          */
};                              /* not incl. wall and occupied tiles */

extern struct citystyle *city_styles;
extern const Output_type_id num_output_types;
extern struct output_type output_types[];

/* get 'struct city_list' and related functions: */
#define SPECLIST_TAG city
#define SPECLIST_TYPE struct city
#include "speclist.h"

#define city_list_iterate(citylist, pcity) \
    TYPED_LIST_ITERATE(struct city, citylist, pcity)
#define city_list_iterate_end  LIST_ITERATE_END

#define cities_iterate(pcity)                                               \
{                                                                           \
  players_iterate(pcity##_player) {                                         \
    city_list_iterate(pcity##_player->cities, pcity) {

#define cities_iterate_end                                                  \
    } city_list_iterate_end;                                                \
  } players_iterate_end;                                                    \
}


static inline bool is_city_center(int city_x, int city_y);
static inline bool is_free_worked_tile(int city_x, int city_y);

/* output type functions */

const char *get_output_identifier(Output_type_id output);
const char *get_output_name(Output_type_id output);
struct output_type *get_output_type(Output_type_id output);
Output_type_id find_output_type_by_identifier(const char *id);
void add_specialist_output(const struct city *pcity, int *output);

/* properties */

struct player *city_owner(const struct city *pcity);
int city_population(const struct city *pcity);
int city_unit_unhappiness(struct unit *punit, int *free_happy);
void city_unit_upkeep(struct unit *punit, int *outputs, int *free_upkeep);
bool city_happy(const struct city *pcity);  /* generally use celebrating instead */
bool city_unhappy(const struct city *pcity);                /* anarchy??? */
bool base_city_celebrating(const struct city *pcity);
bool city_celebrating(const struct city *pcity);            /* love the king ??? */
bool city_rapture_grow(const struct city *pcity);

/* city related improvement and unit functions */

int city_improvement_upkeep(const struct city *pcity,
			    const struct impr_type *pimprove);

bool can_city_build_improvement_direct(const struct city *pcity,
				       struct impr_type *pimprove);
bool can_city_build_improvement_later(const struct city *pcity,
				      struct impr_type *pimprove);
bool can_city_build_improvement_now(const struct city *pcity,
				    struct impr_type *pimprove);

bool can_city_build_unit_direct(const struct city *pcity,
				const struct unit_type *punittype);
bool can_city_build_unit_later(const struct city *pcity,
			       const struct unit_type *punittype);
bool can_city_build_unit_now(const struct city *pcity,
			     const struct unit_type *punittype);

bool can_city_build_direct(const struct city *pcity,
			   struct universal target);
bool can_city_build_later(const struct city *pcity,
			  struct universal target);
bool can_city_build_now(const struct city *pcity,
			struct universal target);

bool city_can_use_specialist(const struct city *pcity,
			     Specialist_type_id type);
bool city_has_building(const struct city *pcity,
		       const struct impr_type *pimprove);
bool is_capital(const struct city *pcity);
bool city_got_citywalls(const struct city *pcity);
bool city_got_defense_effect(const struct city *pcity,
                             const struct unit_type *attacker);

int city_production_build_shield_cost(const struct city *pcity);
int city_production_buy_gold_cost(const struct city *pcity);

bool city_production_has_flag(const struct city *pcity,
			      enum impr_flag_id flag);
int city_production_turns_to_build(const struct city *pcity,
				   bool include_shield_stock);

int city_change_production_penalty(const struct city *pcity,
				   struct universal target);
int city_turns_to_build(const struct city *pcity,
			struct universal target,
                        bool include_shield_stock);
int city_turns_to_grow(const struct city *pcity);
bool city_can_grow_to(const struct city *pcity, int pop_size);
bool city_can_change_build(const struct city *pcity);

/* textual representation of buildings */

const char *city_improvement_name_translation(const struct city *pcity,
					      struct impr_type *pimprove);
const char *city_production_name_translation(const struct city *pcity);

/* city map functions */

bool is_valid_city_coords(const int city_x, const int city_y);
bool map_to_city_map(int *city_map_x, int *city_map_y,
		     const struct city *const pcity,
		     const struct tile *ptile);
bool base_map_to_city_map(int *city_map_x, int *city_map_y,
			  const struct tile *city_tile,
			  const struct tile *map_tile);

struct tile *base_city_map_to_map(const struct tile *city_center_tile,
				  int city_map_x, int city_map_y);
struct tile *city_map_to_map(const struct city *const pcity,
			     int city_map_x, int city_map_y);

/* Initialization functions */
int compare_iter_index(const void *a, const void *b);
void generate_city_map_indices(void);

/* output on spot */
int get_output_tile(const struct tile *ptile, Output_type_id otype);
int city_get_output_tile(int city_x, int city_y, const struct city *pcity,
			 Output_type_id otype);
int base_city_get_output_tile(int city_x, int city_y,
			      const struct city *pcity, bool is_celebrating,
			      Output_type_id otype);

void set_worker_city(struct city *pcity, int city_x, int city_y,
		     enum city_tile_type type); 
enum city_tile_type get_worker_city(const struct city *pcity, int city_x,
				    int city_y);
void get_worker_on_map_position(const struct tile *ptile,
				enum city_tile_type *result_city_tile_type,
				struct city **result_pcity);
bool is_worker_here(const struct city *pcity, int city_x, int city_y);

bool city_can_be_built_here(const struct tile *ptile,
			    const struct unit *punit);

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

/* city style functions */
const char *city_style_rule_name(const int style);
const char *city_style_name_translation(const int style);

int find_city_style_by_rule_name(const char *s);
int find_city_style_by_translated_name(const char *s);

bool city_style_has_requirements(const struct citystyle *style);
int city_style_of_player(const struct player *plr);
int style_of_city(const struct city *pcity);

struct city *is_enemy_city_tile(const struct tile *ptile,
				const struct player *pplayer);
struct city *is_allied_city_tile(const struct tile *ptile,
				 const struct player *pplayer);
struct city *is_non_attack_city_tile(const struct tile *ptile,
				     const struct player *pplayer);
struct city *is_non_allied_city_tile(const struct tile *ptile,
				     const struct player *pplayer);

bool is_unit_near_a_friendly_city(const struct unit *punit);
bool is_friendly_city_near(const struct player *owner,
			   const struct tile *ptile);
bool city_exists_within_city_radius(const struct tile *ptile,
				    bool may_be_on_center);

/* granary size as a function of city size */
int city_granary_size(int city_size);

void city_add_improvement(struct city *pcity,
			  const struct impr_type *pimprove);
void city_remove_improvement(struct city *pcity,
			     const struct impr_type *pimprove);

/* city update functions */
void generic_city_refresh(struct city *pcity, bool full_refresh);

int city_waste(const struct city *pcity, Output_type_id otype, int total);
int city_specialists(const struct city *pcity);                 /* elv+tax+scie */
Specialist_type_id best_specialist(Output_type_id otype,
				   const struct city *pcity);
int get_final_city_output_bonus(const struct city *pcity, Output_type_id otype);
bool city_built_last_turn(const struct city *pcity);

/* city creation / destruction */
struct city *create_city_virtual(struct player *pplayer,
				 struct tile *ptile, const char *name);
void destroy_city_virtual(struct city *pcity);

/* misc */
bool is_city_option_set(const struct city *pcity, enum city_options option);
void city_styles_alloc(int num);
void city_styles_free(void);

void add_tax_income(const struct player *pplayer, int trade, int *output);
int get_city_tithes_bonus(const struct city *pcity);
int city_pollution_types(const struct city *pcity, int shield_total,
			 int *pollu_prod, int *pollu_pop, int *pollu_mod);
int city_pollution(const struct city *pcity, int shield_total);

/*
 * Iterates over all improvements, skipping those not yet built in the
 * given city.
 */
#define city_built_iterate(_pcity, _p)				\
  improvement_iterate(_p) {						\
    if ((_pcity)->built[improvement_index(_p)].turn <= I_NEVER) {	\
      continue;								\
    }

#define city_built_iterate_end					\
  } improvement_iterate_end;


/* Iterates over all output types in the game. */
#define output_type_iterate(output)					    \
{									    \
  Output_type_id output;						    \
									    \
  for (output = 0; output < O_COUNT; output++) {

#define output_type_iterate_end						    \
  }									    \
}

/**************************************************************************
  Return TRUE iff the given city coordinate pair is the center tile of
  the citymap.
**************************************************************************/
static inline bool is_city_center(int city_x, int city_y)
{
  return CITY_MAP_RADIUS == city_x && CITY_MAP_RADIUS == city_y;
}

/**************************************************************************
  Return TRUE iff the given city coordinate pair can be worked for free by
  a city.
**************************************************************************/
static inline bool is_free_worked_tile(int city_x, int city_y)
{
  return CITY_MAP_RADIUS == city_x && CITY_MAP_RADIUS == city_y;
}

#endif  /* FC__CITY_H */
