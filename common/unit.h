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
#ifndef FC__UNIT_H
#define FC__UNIT_H

#include "genlist.h"
#include "unittype.h"

struct player;
struct city;

enum unit_activity {
  ACTIVITY_IDLE, ACTIVITY_POLLUTION, ACTIVITY_ROAD, ACTIVITY_MINE,
  ACTIVITY_IRRIGATE, ACTIVITY_FORTIFIED, ACTIVITY_FORTRESS, ACTIVITY_SENTRY,
  ACTIVITY_RAILROAD, ACTIVITY_PILLAGE, ACTIVITY_GOTO, ACTIVITY_EXPLORE,
  ACTIVITY_TRANSFORM, ACTIVITY_UNKNOWN, ACTIVITY_AIRBASE, ACTIVITY_FORTIFYING,
  ACTIVITY_FALLOUT,
  ACTIVITY_LAST   /* leave this one last */
};

enum unit_focus_status {
  FOCUS_AVAIL, FOCUS_WAIT, FOCUS_DONE  
};

enum diplomat_actions {
  DIPLOMAT_BRIBE, DIPLOMAT_EMBASSY, DIPLOMAT_SABOTAGE,
  DIPLOMAT_STEAL, DIPLOMAT_INCITE, SPY_POISON, 
  DIPLOMAT_INVESTIGATE, SPY_SABOTAGE_UNIT,
  SPY_GET_SABOTAGE_LIST,
  DIPLOMAT_MOVE,	/* move onto city square - only for allied cities */
  DIPLOMAT_ANY_ACTION   /* leave this one last */
};

enum diplomat_client_actions {
  DIPLOMAT_CLIENT_POPUP_DIALOG
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
  Unit_Type_id type;
  int id;
  int owner;
  int x, y;                           
  int veteran;
  int homecity;
  int moves_left;
  int hp;
  int unhappiness;
  int upkeep;
  int upkeep_food;
  int upkeep_gold;
  int foul;
  int fuel;
  int bribe_cost;
  struct unit_ai ai;
  enum unit_activity activity;
  int goto_dest_x, goto_dest_y;
  int activity_count;
  int activity_target;
  enum unit_focus_status focus_status;
  int ord_map, ord_city;
  /* ord_map and ord_city are the order index of this unit in tile.units
     and city.units_supported; they are only used for save/reload */
  int moved;
  int paradropped;
  int connecting;
  int transported_by;
};


/* get 'struct unit_list' and related functions: */
#define SPECLIST_TAG unit
#define SPECLIST_TYPE struct unit
#include "speclist.h"

#define unit_list_iterate(unitlist, punit) \
    TYPED_LIST_ITERATE(struct unit, unitlist, punit)
#define unit_list_iterate_end  LIST_ITERATE_END


struct unit *unit_list_find(struct unit_list *This, int id);

void unit_list_sort_ord_map(struct unit_list *This);
void unit_list_sort_ord_city(struct unit_list *This);

int diplomat_can_do_action(struct unit *pdiplomat,
			   enum diplomat_actions action, 
			   int destx, int desty);
int is_diplomat_action_available(struct unit *pdiplomat,
                                 enum diplomat_actions action,
                                 int destx, int desty);

int unit_move_rate(struct unit *punit);
int unit_can_help_build_wonder(struct unit *punit, struct city *pcity);
int unit_can_help_build_wonder_here(struct unit *punit);
int unit_can_est_traderoute_here(struct unit *punit);
int unit_can_defend_here(struct unit *punit);
int unit_can_airlift_to(struct unit *punit, struct city *pcity);

int can_unit_paradrop(struct unit *punit);
int can_unit_change_homecity(struct unit *punit);
int can_unit_do_connect(struct unit *punit, enum unit_activity activity);
char* get_activity_text (int activity);
int can_unit_do_activity(struct unit *punit, enum unit_activity activity);
int can_unit_do_activity_targeted(struct unit *punit,
				  enum unit_activity activity, int target);
void set_unit_activity(struct unit *punit, enum unit_activity new_activity);
void set_unit_activity_targeted(struct unit *punit,
				enum unit_activity new_activity, int new_target);
int can_unit_do_auto(struct unit *punit); 
int is_unit_activity_on_tile(enum unit_activity activity, int x, int y);
int get_unit_tile_pillage_set(int x, int y);
int is_military_unit(struct unit *punit);           /* !set !dip !cara */
int is_diplomat_unit(struct unit *punit);
int is_ground_threat(struct player *pplayer, struct unit *punit);
int is_square_threatened(struct player *pplayer, int x, int y);
int is_field_unit(struct unit *this_unit);              /* ships+aero */
int is_hiding_unit(struct unit *punit);
int is_sailing_unit(struct unit *punit);
int is_air_unit(struct unit *punit);
int is_heli_unit(struct unit *punit);
int is_ground_unit(struct unit *punit);
int can_unit_build_city(struct unit *punit);
int can_unit_add_to_city(struct unit *punit);
int is_unit_near_a_friendly_city(struct unit *punit);
int kills_citizen_after_attack(struct unit *punit);

char *unit_activity_text(struct unit *punit);
char *unit_description(struct unit *punit);
int ground_unit_transporter_capacity(int x, int y, int playerid);
int get_transporter_capacity(struct unit *punit);
int is_ground_units_transport(struct unit *punit);
int is_air_units_transport(struct unit *punit);
int missile_carrier_capacity(int x, int y, int playerid);
int airunit_carrier_capacity(int x, int y, int playerid);

struct player *unit_owner(struct unit *punit);

#endif  /* FC__UNIT_H */
