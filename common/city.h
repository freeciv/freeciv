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
#ifndef __CITY_H
#define __CITY_H

#include "genlist.h"

struct player;

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

struct improvement_type {
  char name[MAX_LENGTH_NAME];
  int is_wonder;
  int tech_requirement;
  int build_cost;
  int shield_upkeep;
  int obsolete_by;
};

enum specialist_type {
  SP_ELVIS, SP_SCIENTIST, SP_TAXMAN
};

enum city_tile_type {
  C_TILE_EMPTY, C_TILE_WORKER, C_TILE_UNAVAILABLE
};

#define min(X, Y) ((X)>(Y) ? (Y) : (X))
#define max(X, Y) ((X)<(Y) ? (Y) : (X))
#define get_government(X) (game.players[X].government)

#define CITY_MAP_SIZE 5



#define city_map_iterate(x, y) \
  for (y=0;y<CITY_MAP_SIZE;y++) \
    for (x=0;x<CITY_MAP_SIZE;x++) \
      if (! ((x == 0 || x == 4) && (y == 0 || y == 4))) 				      
#define city_list_iterate(citylist, pcity) { \
  struct genlist_iterator myiter; \
  struct city *pcity; \
  genlist_iterator_init(&myiter, &citylist.list, 0); \
  for(; ITERATOR_PTR(myiter);) { \
    pcity=(struct city *)ITERATOR_PTR(myiter); \
    ITERATOR_NEXT(myiter); 
#define city_list_iterate_end }}


struct ai_city {
  int workremain;
  int ai_role;
  /* building desirabilities - easiest to handle them here -- Syela */
  int building_want[B_LAST]; /* not sure these will always be < 256 */
  int danger; /* eight miles better than calculating it twice */
  int trade_want; /* saves a zillion calculations */
};

struct city {
  int id;
  int owner;
  int x, y;
  char name[MAX_LENGTH_NAME];

  /* the people */
  int size;

  int ppl_happy[5], ppl_content[5], ppl_unhappy[5];
  int ppl_elvis, ppl_scientist, ppl_taxman;

  /* trade routes */
  int trade[4];

  /* the productions */
  int food_prod, food_surplus;
  int shield_prod, shield_surplus;
  int trade_prod, corruption;
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
  
  enum city_tile_type city_map[CITY_MAP_SIZE][CITY_MAP_SIZE];

  struct unit_list units_supported;
  int steal;                  /* diplomats can only steal once */
  /* turn states */
  int did_buy, did_sell, is_updated;
  int anarchy;                /* anarchy rounds count */ 
  int was_happy;
  int airlift;
  int original;  /* original owner */
  struct ai_city ai;
};

struct city_list {
  struct genlist list;
};

extern struct improvement_type improvement_types[B_LAST];


/* initial functions */

char *city_name_suggestion(struct player *pplayer);

/* properties */

struct player *city_owner(struct city *pcity);
int city_got_citywalls(struct city *pcity);
int city_population(struct city *pcity);
int city_gold_surplus(struct city *pcity);
int city_buy_cost(struct city *pcity);
int city_happy(struct city *pcity);  /* generally use celebrating instead */
int city_unhappy(struct city *pcity);                /* anarchy??? */
int city_celebrating(struct city *pcity);            /* love the king ??? */

/* improvement functions */

int improvement_value(enum improvement_type_id id);
int improvement_obsolete(struct player *pplayer, enum improvement_type_id id);
struct improvement_type *get_improvement_type(enum improvement_type_id id);
int wonder_obsolete(enum improvement_type_id id);
int is_wonder(enum improvement_type_id id);

/* city related improvement and unit functions */

int improvement_upkeep(struct city *pcity, int i); 
int can_build_improvement(struct city *pcity, enum improvement_type_id id);
int can_build_unit(struct city *pcity, enum unit_type_id id);
int can_build_unit_direct(struct city *pcity, enum unit_type_id id);
int city_got_building(struct city *pcity,  enum improvement_type_id id); 
int city_affected_by_wonder(struct city *pcity, enum improvement_type_id id);
int wonder_replacement(struct city *pcity, enum improvement_type_id id);

/* textual representation of buildings */

char *get_improvement_name(enum improvement_type_id id);
char *get_imp_name_ex(struct city *pcity, enum improvement_type_id id);

/* city map functions */

int get_shields_tile(int x, int y, struct city *pcity); /* shield on spot */
int get_trade_tile(int x, int y, struct city *pcity);   /* trade  on spot */
int get_food_tile(int x, int y, struct city *pcity);    /* food   on spot */

/* trade functions */
int can_establish_trade_route(struct city *pc1, struct city *pc2);
int trade_between_cities(struct city *pc1, struct city *pc2);

/* list functions */
struct city *find_city_by_id(int id);
struct city *find_city_by_name(int id);
void city_list_init(struct city_list *This);
struct city *city_list_get(struct city_list *This, int index);
struct city *city_list_find_id(struct city_list *This, int id);
struct city *city_list_find_coor(struct city_list *This, int x, int y);
struct city *city_list_find_name(struct city_list *This, char *name);

void city_list_insert(struct city_list *This, struct city *pcity);
int city_list_size(struct city_list *This);
void city_list_unlink(struct city_list *This, struct city *pcity);

#endif




