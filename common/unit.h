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
#ifndef __UNIT_H
#define __UNIT_H

#include "genlist.h"
#include "shared.h"

struct player;
struct city;

enum unit_type_id { 
  U_SETTLERS, 
  U_ENGINEERS, 
  U_WARRIORS,
  U_PHALANX, 
  U_ARCHERS,
  U_LEGION, 
  U_PIKEMEN, 
  U_MUSKETEERS, 
  U_FANATICS, 
  U_PARTISAN,
  U_ALPINE, 
  U_RIFLEMEN,  
  U_MARINES, 
  U_PARATROOPERS, 
  U_MECH, 
  U_HORSEMEN, 
  U_CHARIOT, 
  U_ELEPHANT,
  U_CRUSADERS, 
  U_KNIGHTS, 
  U_DRAGOONS, 
  U_CAVALRY, 
  U_ARMOR, 
  U_CATAPULT,
  U_CANNON, 
  U_ARTILLERY, 
  U_HOWITZER, 
  U_FIGHTER, 
  U_BOMBER, 
  U_HELICOPTER, 
  U_SFIGHTER, 
  U_SBOMBER,
  U_TRIREME, 
  U_CARAVEL, 
  U_GALLEON, 
  U_FRIGATE, 
  U_IRONCLAD, 
  U_DESTROYER, 
  U_CRUISER, 
  U_AEGIS, 
  U_BATTLESHIP, 
  U_SUBMARINE, 
  U_CARRIER, 
  U_TRANSPORT, 
  U_CRUISERMIS, 
  U_NUCLEAR, 
  U_DIPLOMAT, 
  U_SPY,
  U_CARAVAN, 
  U_FREIGHT, 
  U_EXPLORER, 
  U_LAST
};

enum unit_activity {
  ACTIVITY_IDLE, ACTIVITY_POLLUTION, ACTIVITY_ROAD, ACTIVITY_MINE, 
  ACTIVITY_IRRIGATE, ACTIVITY_FORTIFY, ACTIVITY_FORTRESS, ACTIVITY_SENTRY,
  ACTIVITY_RAILROAD, ACTIVITY_PILLAGE, ACTIVITY_GOTO, ACTIVITY_EXPLORE,
  ACTIVITY_UNKNOWN
};

enum unit_move_type {
  LAND_MOVING = 1, SEA_MOVING, HELI_MOVING, AIR_MOVING
};

enum unit_focus_status {
  FOCUS_AVAIL, FOCUS_WAIT, FOCUS_DONE  
};

enum diplomat_actions {
  DIPLOMAT_BRIBE, DIPLOMAT_EMBASSY, DIPLOMAT_SABOTAGE,
  DIPLOMAT_STEAL, DIPLOMAT_INCITE, SPY_POISON, 
  DIPLOMAT_INVESTIGATE, SPY_SABOTAGE_UNIT,
  DIPLOMAT_ANY_ACTION
};

struct unit_ai {
  int control;
  int ai_role;
  int ferryboat;
  int passenger;
  int bodyguard;
  int charge; /* couldn't find a better synonym -- Syela */
};

struct unit {
  enum unit_type_id type;
  int id;
  int owner;
  int x, y;                           
  int veteran;
  int homecity;
  int moves_left;
  int hp;
  int unhappiness;
  int upkeep;
  int foul;
  int fuel;
  int bribe_cost;
  struct unit_ai ai;
  enum unit_activity activity;
  int goto_dest_x, goto_dest_y;
  int activity_count;
  enum unit_focus_status focus_status;
  int ord_map, ord_city;
  /* ord_map and ord_city are the order index of this unit in tile.units
     and city.units_supported; they are only used for save/reload */
};

#define F_CARAVAN      (1<<0)
#define F_MISSILE      (1<<1)
#define F_IGZOC        (1<<2)
#define F_NONMIL       (1<<3)
#define F_IGTER        (1<<4)
#define F_CARRIER      (1<<5)
#define F_ONEATTACK    (1<<6)
#define F_PIKEMEN      (1<<7)
#define F_HORSE        (1<<8)
#define F_IGWALL       (1<<9)
#define F_FIELDUNIT    (1<<10)
#define F_AEGIS        (1<<11)
#define F_FIGHTER      (1<<12)
#define F_MARINES      (1<<13)
#define F_SUBMARINE    (1<<14)
#define F_SETTLERS     (1<<15)
#define F_DIPLOMAT     (1<<16)
#define F_TRIREME      (1<<17)     /* Trireme sinking effect */
#define F_NUCLEAR      (1<<18)     /* Nuclear attack effect */
#define F_SPY          (1<<19)     /* Enhanced spy abilities */
struct unit_type {
  char name[MAX_LENGTH_NAME];
  int graphics;
  enum unit_move_type move_type;
  int build_cost;
  int attack_strength;
  int defense_strength;
  int move_rate;
  int tech_requirement;
  int vision_range;
  int transport_capacity;
  int hp;
  int firepower;
  int obsoleted_by;
  int fuel;
  int flags;
};


struct unit_list {
  struct genlist list;
};

#define unit_list_iterate(unitlist, punit) { \
  struct genlist_iterator myiter; \
  struct unit *punit; \
  genlist_iterator_init(&myiter, &unitlist.list, 0); \
  for(; ITERATOR_PTR(myiter);) { \
    punit=(struct unit *)ITERATOR_PTR(myiter); \
    ITERATOR_NEXT(myiter); 
#define unit_list_iterate_end }}

extern struct unit_type unit_types[U_LAST];

void unit_list_init(struct unit_list *This);
struct unit *unit_list_get(struct unit_list *This, int index);
struct unit *unit_list_find(struct unit_list *This, int id);
void unit_list_insert(struct unit_list *This, struct unit *punit);
void unit_list_insert_back(struct unit_list *This, struct unit *punit);
int unit_list_size(struct unit_list *This);
void unit_list_unlink(struct unit_list *This, struct unit *punit);
void unit_list_unlink_all(struct unit_list *This);
char *unit_name(enum unit_type_id id);

void unit_list_sort_ord_map(struct unit_list *This);
void unit_list_sort_ord_city(struct unit_list *This);

int diplomat_can_do_action(struct unit *pdiplomat,
			   enum diplomat_actions action, 
			   int destx, int desty);
int is_diplomat_action_available(struct unit *pdiplomat,
                                 enum diplomat_actions action,
                                 int destx, int desty);

struct unit *find_unit_by_id(int id);

char *get_unit_name(enum unit_type_id id);

int unit_move_rate(struct unit *punit);
int unit_can_help_build_wonder(struct unit *punit, struct city *pcity);
int unit_can_defend_here(struct unit *punit);
int unit_can_airlift_to(struct unit *punit, struct city *pcity);

int can_unit_change_homecity(struct unit *punit);
int can_unit_move_to_tile(struct unit *punit, int x, int y);
int can_unit_do_activity(struct unit *punit, enum unit_activity activity);
int is_unit_activity_on_tile(enum unit_activity activity, int x, int y);
int unit_value(enum unit_type_id id);
int is_military_unit(struct unit *this_unit);           /* !set !dip !cara */
int is_field_unit(struct unit *this_unit);              /* ships+aero */
void raise_unit_top(struct unit *punit);
int is_water_unit(enum unit_type_id id);
int is_sailing_unit(struct unit *punit);
int is_air_unit(struct unit *punit);
int is_air_unittype(enum unit_type_id id);
int is_heli_unit(struct unit *punit);
int is_ground_unit(struct unit *punit);
int is_ground_unittype(enum unit_type_id id);
int can_unit_build_city(struct unit *punit);

struct unit_type *get_unit_type(enum unit_type_id id);
char *unit_activity_text(struct unit *punit);
char *unit_description(struct unit *punit);
int is_transporter_with_free_space(struct player *pplayer, int x, int y);
int get_transporter_capacity(struct unit *punit);

void move_unit_list_to_tile(struct unit_list *units, int x, int y);
void transporter_cargo_to_unitlist(struct unit *ptran, struct unit_list *list);
void transporter_min_cargo_to_unitlist(struct unit *ptran,
				       struct unit_list *list);
int unit_flag(enum unit_type_id id, int flag);
int can_upgrade_unittype(struct player *pplayer, enum unit_type_id id);
int unit_upgrade_price(struct player *pplayer, enum unit_type_id from, enum unit_type_id to);

int unit_type_exists(enum unit_type_id id);
enum unit_type_id find_unit_type_by_name(char *s);

#endif
