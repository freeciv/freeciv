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
#include "shared.h"		/* MAX_LEN_NAME */
#include "unit.h"		/* struct unit_list */
#include "worklist.h"

struct player;
struct government;

enum improvement_type_id {
  B_AIRPORT=0, B_AQUEDUCT, B_BANK, B_BARRACKS, B_BARRACKS2, B_BARRACKS3, 
  B_CATHEDRAL, B_CITY, B_COASTAL, B_COLOSSEUM, B_COURTHOUSE,  B_FACTORY, 
  B_GRANARY, B_HARBOUR, B_HYDRO, B_LIBRARY, B_MARKETPLACE, B_MASS, B_MFG, 
  B_NUCLEAR, B_OFFSHORE, B_PALACE, B_POLICE, B_PORT, B_POWER,
  B_RECYCLING, B_RESEARCH, B_SAM, B_SDI, B_SEWER, B_SOLAR, B_SCOMP, 
  B_SMODULE, B_SSTRUCTURAL, B_STOCK, B_SUPERHIGHWAYS, B_SUPERMARKET, B_TEMPLE,
  B_UNIVERSITY,  
  
  B_APOLLO, B_ASMITHS, B_COLLOSSUS, B_COPERNICUS, B_CURE, B_DARWIN, B_EIFFEL,
  B_GREAT, B_WALL, B_HANGING, B_HOOVER, B_ISAAC, B_BACH, B_RICHARDS, 
  B_LEONARDO, B_LIGHTHOUSE, B_MAGELLAN, B_MANHATTEN, B_MARCO, B_MICHELANGELO, 
  B_ORACLE, B_PYRAMIDS, B_SETI, B_SHAKESPEARE, B_LIBERTY, B_SUNTZU, 
  B_UNITED, B_WOMENS,
  B_CAPITAL, B_LAST
};

typedef enum improvement_type_id Impr_Type_id;
/* This will later not be an enum */

struct improvement_type {
  char name[MAX_LEN_NAME];
  char name_orig[MAX_LEN_NAME];	      /* untranslated */
  int is_wonder;
  int tech_requirement;
  int build_cost;
  int shield_upkeep;
  int obsolete_by;
  int variant;
  char *helptext;
};

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
  for (dy=-(int)(CITY_MAP_SIZE/2);dy<(int)(CITY_MAP_SIZE/2);dy++) \
    for (dx=-(int)(CITY_MAP_SIZE/2);dx<(int)(CITY_MAP_SIZE/2);dx++) \
      if (! ((dx == -(int)(CITY_MAP_SIZE/2) || dx == (int)(CITY_MAP_SIZE/2)) && \
	     (dy == -(int)(CITY_MAP_SIZE/2) || dy == (int)(CITY_MAP_SIZE/2))) )



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
  signed short int detox[5][5], mine[5][5], irrigate[5][5], road[5][5], railroad[5][5], transform[5][5];
/* caching these so that CPU usage is O(cities) instead of O(cities^2) -- Syela */
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
  int anarchy;		      /* anarchy rounds count */ 
  int rapture;                /* rapture rounds count */ 
  int was_happy;
  int airlift;
  int original;			/* original owner */
  int city_options;		/* bitfield; positions as enum city_options */
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


extern struct improvement_type improvement_types[B_LAST];
extern char **misc_city_names;

/* properties */

struct player *city_owner(struct city *pcity);
int city_population(struct city *pcity);
int city_gold_surplus(struct city *pcity);
int city_buy_cost(struct city *pcity);
int city_happy(struct city *pcity);  /* generally use celebrating instead */
int city_unhappy(struct city *pcity);                /* anarchy??? */
int city_celebrating(struct city *pcity);            /* love the king ??? */
int city_rapture_grow(struct city *pcity);

/* improvement functions */

int improvement_value(Impr_Type_id id);
int improvement_obsolete(struct player *pplayer, Impr_Type_id id);
struct improvement_type *get_improvement_type(Impr_Type_id id);
int wonder_obsolete(Impr_Type_id id);
int is_wonder_usefull(Impr_Type_id id);
int is_wonder(Impr_Type_id id);
int improvement_exists(Impr_Type_id id);
Impr_Type_id find_improvement_by_name(char *s);
int improvement_variant(Impr_Type_id id);

/* player related improvement and unit functions */

int could_player_eventually_build_improvement(struct player *p, Impr_Type_id id);
int could_player_build_improvement(struct player *p, Impr_Type_id id);
int can_player_build_improvement(struct player *p, Impr_Type_id id);
int can_player_build_unit_direct(struct player *p, Unit_Type_id id);
int can_player_build_unit(struct player *p, Unit_Type_id id);
int can_player_eventually_build_unit(struct player *p, Unit_Type_id id);


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
int city_turns_to_build(struct city *pcity, int id, int id_is_unit);

/* textual representation of buildings */

char *get_improvement_name(Impr_Type_id id);
char *get_impr_name_ex(struct city *pcity, Impr_Type_id id);

/* city map functions */

int get_shields_tile(int x, int y, struct city *pcity); /* shield on spot */
int get_trade_tile(int x, int y, struct city *pcity);   /* trade  on spot */
int get_food_tile(int x, int y, struct city *pcity);    /* food   on spot */
void set_worker_city(struct city *pcity, int x, int y, enum city_tile_type type); 
enum city_tile_type get_worker_city(struct city *pcity, int x, int y);
int is_worker_here(struct city *pcity, int x, int y);
int map_to_city_x(struct city *pcity, int x);
int map_to_city_y(struct city *pcity, int y);

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
int get_style_by_name(char *);

#endif  /* FC__CITY_H */
