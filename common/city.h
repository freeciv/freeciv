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

#include "genlist.h"
#include "improvement.h"
#include "unit.h"		/* struct unit_list */
#include "worklist.h"


struct player;
struct government;
struct tile;


enum specialist_type {
  SP_ELVIS, SP_SCIENTIST, SP_TAXMAN
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

#define CITY_MAP_SIZE 5


/* Iterate a city map */

#define city_map_iterate(x, y) \
  for (y=0;y<CITY_MAP_SIZE;y++) \
    for (x=0;x<CITY_MAP_SIZE;x++) \
      if (! ((x == 0 || x == (CITY_MAP_SIZE-1)) && \
	     (y == 0 || y == (CITY_MAP_SIZE-1))) )

/* Iterate a city map, from the center (the city) outwards */

extern int city_map_iterate_outwards_indices[(CITY_MAP_SIZE*CITY_MAP_SIZE)-4][2];

#define city_map_iterate_outwards(x, y) { \
  int city_map_iterate_outwards_index; \
  for \
  ( \
    city_map_iterate_outwards_index = 0; \
    city_map_iterate_outwards_index < (CITY_MAP_SIZE*CITY_MAP_SIZE)-4; \
    city_map_iterate_outwards_index++ \
  ) \
  { \
    x = city_map_iterate_outwards_indices[city_map_iterate_outwards_index][0]; \
    y = city_map_iterate_outwards_indices[city_map_iterate_outwards_index][1];

#define city_map_iterate_outwards_end } }

/* Iterate a city radius: (dx,dy) centered on (0,0) */

#define city_radius_iterate(dx, dy) \
  for (dy = -(int)(CITY_MAP_SIZE/2); dy<(int)(CITY_MAP_SIZE/2); dy++) \
    for (dx = -(int)(CITY_MAP_SIZE/2); dx<(int)(CITY_MAP_SIZE/2); dx++) \
      if (! ((dx == -(int)(CITY_MAP_SIZE/2) || dx == (int)(CITY_MAP_SIZE/2)) && \
	     (dy == -(int)(CITY_MAP_SIZE/2) || dy == (int)(CITY_MAP_SIZE/2))) )

/* Iterate a city radius in map coordinates; skip non-existant squares */

#define map_city_radius_iterate(city_x, city_y, x_itr, y_itr) \
{ \
  int x_itr, y_itr; \
  int MCMI_x, MCMI_y; \
  for (MCMI_x=0; MCMI_x<CITY_MAP_SIZE; MCMI_x++) { \
    for (MCMI_y=0; MCMI_y<CITY_MAP_SIZE; MCMI_y++) { \
      if (! ((MCMI_x == 0 || MCMI_x == (CITY_MAP_SIZE-1)) \
	     && (MCMI_y == 0 || MCMI_y == (CITY_MAP_SIZE-1))) ) { \
        y_itr = city_y + MCMI_y - CITY_MAP_SIZE/2; \
        if (y_itr < 0 || y_itr >= map.ysize) \
	  continue; \
	x_itr = map_adjust_x(city_x + MCMI_x - CITY_MAP_SIZE/2);

#define map_city_radius_iterate_end \
      } \
    } \
  } \
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
};

struct ai_city {
  int workremain;
  int ai_role;
  /* building desirabilities - easiest to handle them here -- Syela */
  int building_want[B_LAST]; /* not sure these will always be < 256 */
  int danger; /* danger to be compared to assess_defense */
  int diplomat_threat; /* an enemy diplomat or spy is near the city,
			  and this city has no diplomat or spy defender */
  int urgency; /* how close the danger is; if zero, bodyguards can leave */
  int grave_danger; /* danger that is upon us, should show positive feedback */
  int wallvalue; /* how much it helps for defenders to be ground units */
  int trade_want; /* saves a zillion calculations */
  struct ai_choice choice; /* to spend gold in the right place only */
  int downtown; /* distance from neighbours, for locating wonders wisely */
  int distance_to_wonder_city; /* wondercity will set this for us, avoiding paradox */
  /* caching these so that CPU usage is O(cities) instead of O(cities^2) -- Syela */
  signed short int detox[5][5];
  signed short int derad[5][5];
  signed short int mine[5][5];
  signed short int irrigate[5][5];
  signed short int road[5][5];
  signed short int railroad[5][5];
  signed short int transform[5][5];
  signed short int tile_value[5][5]; /* caching these will help too. */
  /* so we can contemplate with warmap fresh and decide later */
  int settler_want, founder_want; /* for builder (F_SETTLERS) and founder (F_CITIES) */
  int a, f, invasion; /* who's coming to kill us, for attack co-ordination */
};

struct city {
  int id;
  int owner;
  int x, y;
  char name[MAX_LEN_NAME];

  /* the people */
  int size;

  int ppl_happy[5], ppl_content[5], ppl_unhappy[5];
  int ppl_elvis, ppl_scientist, ppl_taxman;

  /* trade routes */
  int trade[4],trade_value[4];

  /* the productions */
  int food_prod, food_surplus;
  int shield_prod, shield_surplus;
  int trade_prod, corruption, tile_trade;
  int shield_bonus, tax_bonus, science_bonus; /* more CPU savings! */

  /* the totals */
  int luxury_total, tax_total, science_total;
  
  /* the physics */
  int food_stock;
  int shield_stock;
  int pollution;
  int incite_revolt_cost;
   
  int is_building_unit;    /* boolean unit/improvement */
  int currently_building;
  
  unsigned char improvements[B_LAST];
  
  struct worklist *worklist;

  enum city_tile_type city_map[CITY_MAP_SIZE][CITY_MAP_SIZE];

  struct unit_list units_supported;
  int steal;		      /* diplomats steal once; for spies, gets harder */
  /* turn states */
  int did_buy, did_sell, is_updated;
  int turn_last_built;	      /* The last year in which something was built */
  int turn_changed_target;    /* Suffer shield loss at most once per turn */
  int changed_from_id;	      /* If changed this turn, what changed from (id) */
  int changed_from_is_unit;   /* If changed this turn, what changed from (unit?) */
  int before_change_shields;  /* If changed this turn, shields before penalty */
  int anarchy;		      /* anarchy rounds count */ 
  int rapture;                /* rapture rounds count */ 
  int was_happy;
  int airlift;
  int original;			/* original owner */
  int city_options;		/* bitfield; positions as enum city_options */

  /* info for dipl/spy investigation -- used only in client */
  struct unit_list info_units_supported;
  struct unit_list info_units_present;

  struct ai_city ai;
};

/* city drawing styles */

#define MAX_CITY_TILES 8

struct citystyle {
  char name[MAX_LEN_NAME];
  char name_orig[MAX_LEN_NAME];	      /* untranslated */
  char graphic[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
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

/* properties */

struct player *city_owner(struct city *pcity);
int city_population(struct city *pcity);
int city_gold_surplus(struct city *pcity);
int city_buy_cost(struct city *pcity);
int city_happy(struct city *pcity);  /* generally use celebrating instead */
int city_unhappy(struct city *pcity);                /* anarchy??? */
int city_celebrating(struct city *pcity);            /* love the king ??? */
int city_rapture_grow(struct city *pcity);

/* city related improvement and unit functions */

int improvement_upkeep(struct city *pcity, int i); 
int could_build_improvement(struct city *pcity, Impr_Type_id id);
int can_build_improvement(struct city *pcity, Impr_Type_id id);
int can_eventually_build_improvement(struct city *pcity, Impr_Type_id id);
int can_build_unit(struct city *pcity, Unit_Type_id id);
int can_build_unit_direct(struct city *pcity, Unit_Type_id id);
int can_eventually_build_unit(struct city *pcity, Unit_Type_id id);
int city_got_building(struct city *pcity,  Impr_Type_id id); 
int city_affected_by_wonder(struct city *pcity, Impr_Type_id id);
int city_got_effect(struct city *pcity, Impr_Type_id id);
int city_got_citywalls(struct city *pcity);
int wonder_replacement(struct city *pcity, Impr_Type_id id);
int city_change_production_penalty(struct city *pcity,
				   int target, int is_unit, int apply_it);
int city_turns_to_build(struct city *pcity, int id, int id_is_unit);

/* textual representation of buildings */

char *get_impr_name_ex(struct city *pcity, Impr_Type_id id);

/* tile production functions */

int get_shields_tile(int x, int y); /* shield on spot */
int get_trade_tile(int x, int y);   /* trade  on spot */
int get_food_tile(int x, int y);    /* food   on spot */

/* city map functions */

int city_get_shields_tile(int x, int y, struct city *pcity); /* shield on spot */
int city_get_trade_tile(int x, int y, struct city *pcity);   /* trade  on spot */
int city_get_food_tile(int x, int y, struct city *pcity);    /* food   on spot */
void set_worker_city(struct city *pcity, int x, int y, enum city_tile_type type); 
enum city_tile_type get_worker_city(struct city *pcity, int x, int y);
int is_worker_here(struct city *pcity, int x, int y);
int map_to_city_x(struct city *pcity, int x);
int map_to_city_y(struct city *pcity, int y);

int city_can_be_built_here(int x, int y);

/* trade functions */
int can_establish_trade_route(struct city *pc1, struct city *pc2);
int trade_between_cities(struct city *pc1, struct city *pc2);
int city_num_trade_routes(struct city *pcity);

/* list functions */
struct city *city_list_find_id(struct city_list *This, int id);
struct city *city_list_find_name(struct city_list *This, char *name);

int city_name_compare(const void *p1, const void *p2);

/* city free cost values depending on government: */
int citygov_free_shield(struct city *pcity, struct government *gov);
int citygov_free_happy(struct city *pcity, struct government *gov);
int citygov_free_food(struct city *pcity, struct government *gov);
int citygov_free_gold(struct city *pcity, struct government *gov);

/* city style functions */
int get_city_style(struct city *pcity);
int get_player_city_style(struct player *plr);
int get_style_by_name(char *);

struct city *is_enemy_city_tile(struct tile *ptile, int playerid);
struct city *is_allied_city_tile(struct tile *ptile, int playerid);
struct city *is_non_attack_city_tile(struct tile *ptile, int playerid);
struct city *is_non_allied_city_tile(struct tile *ptile, int playerid);

int is_unit_near_a_friendly_city(struct unit *punit);
int is_friendly_city_near(int owner, int x, int y);

/* granary size as a function of city size */
int city_granary_size(int city_size);

#endif  /* FC__CITY_H */
