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
struct goto_route;
struct tile;

enum unit_activity {
  ACTIVITY_IDLE, ACTIVITY_POLLUTION, ACTIVITY_ROAD, ACTIVITY_MINE,
  ACTIVITY_IRRIGATE, ACTIVITY_FORTIFIED, ACTIVITY_FORTRESS, ACTIVITY_SENTRY,
  ACTIVITY_RAILROAD, ACTIVITY_PILLAGE, ACTIVITY_GOTO, ACTIVITY_EXPLORE,
  ACTIVITY_TRANSFORM, ACTIVITY_UNKNOWN, ACTIVITY_AIRBASE, ACTIVITY_FORTIFYING,
  ACTIVITY_FALLOUT, ACTIVITY_PATROL,
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

enum ai_unit_task { AIUNIT_NONE, AIUNIT_AUTO_SETTLER, AIUNIT_BUILD_CITY,
                    AIUNIT_DEFEND_HOME, AIUNIT_ATTACK, AIUNIT_FORTIFY,
                    AIUNIT_RUNAWAY, AIUNIT_ESCORT, AIUNIT_EXPLORE,
                    AIUNIT_PILLAGE };

enum goto_move_restriction {
  GOTO_MOVE_ANY,
  GOTO_MOVE_CARDINAL_ONLY, /* No diagonal moves.  */
  GOTO_MOVE_STRAIGHTEST
};

enum goto_route_type {
  ROUTE_GOTO, ROUTE_PATROL
};

enum move_reason {
  MR_OTHER, MR_INVALID_TYPE_FOR_CITY_TAKE_OVER, MR_NO_WAR, MR_ZOC
};

struct unit_ai {
  int control; /* 0: not automated    1: automated */
  enum ai_unit_task ai_role;
  /* The following are all unit ids */
  int ferryboat; /* the ferryboat assigned to us */
  int passenger; /* the unit assigned to this ferryboat */
  int bodyguard; /* the unit bodyguarding us */
  int charge; /* the unit this unit is bodyguarding */
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
  struct goto_route *pgr;
};


/* get 'struct unit_list' and related functions: */
#define SPECLIST_TAG unit
#define SPECLIST_TYPE struct unit
#include "speclist.h"

#define unit_list_iterate(unitlist, punit) \
    TYPED_LIST_ITERATE(struct unit, unitlist, punit)
#define unit_list_iterate_end  LIST_ITERATE_END
#define SINGLE_MOVE 3
#define MOVE_COST_RIVER 1
#define MOVE_COST_RAIL 0
#define MOVE_COST_ROAD 1

#define unit_list_iterate_safe(unitlist, punit) \
{ \
  int _size = unit_list_size(&unitlist); \
  if (_size) { \
    int *_ids = fc_malloc(sizeof(int) * _size); \
    int _i = 0; \
    unit_list_iterate(unitlist, punit) { \
      _ids[_i++] = punit->id; \
    } unit_list_iterate_end; \
    for (_i=0; _i<_size; _i++) { \
      struct unit *punit = find_unit_by_id(_ids[_i]); \
      if (punit) { \

#define unit_list_iterate_safe_end \
      } \
    } \
    free(_ids); \
  } \
}

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
int can_unit_continue_current_activity(struct unit *punit);
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
int kills_citizen_after_attack(struct unit *punit);

char *unit_activity_text(struct unit *punit);
char *unit_description(struct unit *punit);
int ground_unit_transporter_capacity(int x, int y, int playerid);
int get_transporter_capacity(struct unit *punit);
int is_ground_units_transport(struct unit *punit);
int is_air_units_transport(struct unit *punit);
int missile_carrier_capacity(int x, int y, int playerid,
			     int count_units_with_extra_fuel);
int airunit_carrier_capacity(int x, int y, int playerid,
			     int count_units_with_extra_fuel);

struct player *unit_owner(struct unit *punit);

struct unit *is_allied_unit_tile(struct tile *ptile, int playerid);
struct unit *is_enemy_unit_tile(struct tile *ptile, int playerid);
struct unit *is_non_allied_unit_tile(struct tile *ptile, int playerid);
struct unit *is_non_attack_unit_tile(struct tile *ptile, int playerid);

int trireme_loss_pct(struct player *pplayer, int x, int y);

int is_my_zoc(int player_id_of_unit_owner, int x0, int y0);
int can_step_taken_wrt_to_zoc(Unit_Type_id type,
			      int player_id_of_unit_owner, int src_x,
			      int src_y, int dest_x, int dest_y);

int can_unit_move_to_tile_with_reason(Unit_Type_id type,
				      int player_id_of_unit_owner,
				      enum unit_activity activity,
				      int connecting, int src_x, int src_y,
				      int dest_x, int dest_y, int igzoc,
				      enum move_reason *reason);
int unit_type_really_ignores_zoc(Unit_Type_id type);
int zoc_ok_move_gen(struct unit *punit, int x1, int y1, int x2, int y2);
int zoc_ok_move(struct unit *punit, int x, int y);

#endif  /* FC__UNIT_H */
