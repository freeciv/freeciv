/***********************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "log.h"

/* common */
#include "game.h"
#include "movement.h"
#include "unit.h"
#include "unitlist.h"

/* aicore */
#include "path_finding.h"
#include "pf_tools.h"

/* server/advisors */
#include "advgoto.h"
#include "autoexplorer.h"
#include "autosettlers.h"

/* server */
#include "hand_gen.h"
#include "srv_log.h"
#include "unithand.h"
#include "unittools.h"

/* server/advisors */
#include "advdata.h"

/* ai */
#include "handicaps.h"

/* ai/default */
#include "aidata.h"
#include "aiguard.h"
#include "ailog.h"
#include "aiplayer.h"
#include "aitools.h"
#include "aiunit.h"

#include "aiferry.h"

 
/* =================== constants with special meaning =================== */

/* 
 * This one is used only by ferryboats in ai.passenger field 
 */
#define FERRY_AVAILABLE     (-1)  /* Boat is looking for a passenger */
#define FERRY_ABANDON_BOSS  (-2)  /* Passenger is assigned for boat, but boat
                                   * might take another passenger. Probably
                                   * passenger already left the boat*/

/* 
 * The below is used only by passengers in ai.ferryboat field 
 */ 
#define FERRY_WANTED       (-1)   /* Needs a boat */
#define FERRY_NONE         0      /* Has no boat and doesn't need one */


/* =================== group log levels, debug options ================= */

/* Logging in ferry management functions */
#define LOGLEVEL_FERRY LOG_DEBUG
/* Logging in go_by_boat functions */
#define LOGLEVEL_GOBYBOAT LOG_DEBUG
/* Logging in find_ferry functions */
#define LOGLEVEL_FINDFERRY LOG_DEBUG
/* Extra consistency checks */
#ifdef FREECIV_DEBUG
#define LOGLEVEL_FERRY_STATS LOG_NORMAL
#endif


/* ========= managing statistics and boat/passanger assignments ======== */

/**********************************************************************//**
  Call to initialize the ferryboat statistics
**************************************************************************/
void aiferry_init_stats(struct ai_type *ait, struct player *pplayer)
{
  /* def_ai_player_data() instead of dai_plr_data_get() is deliberate.
     We are only initializing player data structures and dai_plr_data_get()
     would try to use it uninitialized. We are only setting values to
     data structure, not reading them, so we have no need for extra
     arrangements dai_plr_data_get() would do compared to def_ai_player_data()
  */
  struct ai_plr *ai = def_ai_player_data(pplayer, ait);

  ai->stats.passengers = 0;
  ai->stats.boats = 0;
  ai->stats.available_boats = 0;
 
  unit_list_iterate(pplayer->units, punit) {
    struct unit_ai *unit_data = def_ai_unit_data(punit, ait);

    if (dai_is_ferry(punit, ait)) {
      ai->stats.boats++;
      if (unit_data->passenger == FERRY_AVAILABLE) {
        ai->stats.available_boats++;
      }
    }
    if (unit_data->ferryboat == FERRY_WANTED) {
      UNIT_LOG(LOG_DEBUG, punit, "wants a boat.");
      ai->stats.passengers++;
    }
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Print the list of boats of pplayer.
**************************************************************************/
#ifdef LOGLEVEL_FERRY_STATS
static void aiferry_print_stats(struct ai_type *ait, struct player *pplayer)
{
  struct ai_plr *ai = dai_plr_data_get(ait, pplayer, NULL);
  int n = 1;

  log_base(LOGLEVEL_FERRY_STATS, "Boat stats for %s[%d]",
           player_name(pplayer), player_number(pplayer));
  log_base(LOGLEVEL_FERRY_STATS, "Registered: %d free out of total %d",
           ai->stats.available_boats, ai->stats.boats);
  unit_list_iterate(pplayer->units, punit) {
    if (dai_is_ferry(punit, ait)) {
      log_base(LOGLEVEL_FERRY_STATS, "#%d. %s[%d], psngr=%d", n,
               unit_rule_name(punit), punit->id,
               def_ai_unit_data(punit, ait)->passenger);
      n++;
    }
  } unit_list_iterate_end;
}
#endif /* LOGLEVEL_FERRY_STATS */

/**********************************************************************//**
  Should unit type be considered a ferry?
**************************************************************************/
bool dai_is_ferry_type(struct unit_type *pferry, struct ai_type *ait)
{
  struct unit_type_ai *utai = utype_ai_data(pferry, ait);

  return utai->ferry;
}

/**********************************************************************//**
  Should unit be considered a ferry?
**************************************************************************/
bool dai_is_ferry(struct unit *pferry, struct ai_type *ait)
{
  return dai_is_ferry_type(unit_type_get(pferry), ait);
}

/**********************************************************************//**
  Initialize new ferry when player gets it
**************************************************************************/
void dai_ferry_init_ferry(struct ai_type *ait, struct unit *ferry)
{
  if (dai_is_ferry(ferry, ait)) {
    bool caller_closes;
    struct player *pplayer = unit_owner(ferry);
    struct unit_ai *unit_data = def_ai_unit_data(ferry, ait);
    struct ai_plr *ai = dai_plr_data_get(ait, pplayer,
                                         &caller_closes);

    unit_data->passenger = FERRY_AVAILABLE;
    ai->stats.boats++;
    ai->stats.available_boats++;

    if (caller_closes) {
      dai_data_phase_finished(ait, pplayer);
    }
  }
}

/**********************************************************************//**
  Update ferry system when unit is transformed.
**************************************************************************/
void dai_ferry_transformed(struct ai_type *ait, struct unit *ferry,
                           struct unit_type *old)
{
  bool old_f = dai_is_ferry_type(old, ait);
  bool new_f = dai_is_ferry(ferry, ait);

  if (old_f && !new_f) {
    struct ai_plr *ai = dai_plr_data_get(ait, unit_owner(ferry), NULL);
    struct unit_ai *unit_data = def_ai_unit_data(ferry, ait);

    ai->stats.boats--;

    if (unit_data->passenger == FERRY_AVAILABLE) {
      ai->stats.available_boats--;
    } else if (unit_data->passenger > 0) {
      struct unit *passenger = game_unit_by_number(unit_data->passenger);

      if (passenger != NULL) {
        aiferry_clear_boat(ait, passenger);
      }
    }
  } else if (!old_f && new_f) {
    struct ai_plr *ai = dai_plr_data_get(ait, unit_owner(ferry), NULL);

    ai->stats.boats++;
    ai->stats.available_boats++;
  }
}

/**********************************************************************//**
  Close ferry when player loses it
**************************************************************************/
void dai_ferry_lost(struct ai_type *ait, struct unit *punit)
{
  /* Ignore virtual units. */
  if (punit->id != 0 && is_ai_data_phase_open(ait, unit_owner(punit))) {
    bool close_here;
    struct unit_ai *unit_data = def_ai_unit_data(punit, ait);
    struct player *pplayer = unit_owner(punit);
    struct ai_plr *ai = dai_plr_data_get(ait, pplayer, &close_here);

    if (dai_is_ferry(punit, ait)) {
      ai->stats.boats--;
      if (unit_data->passenger == FERRY_AVAILABLE) {
        ai->stats.available_boats--;
      }
    } else {
      /* Not a ferry */
      if (unit_data->ferryboat > 0) {
        aiferry_clear_boat(ait, punit);
      }
    }

    if (close_here) {
      dai_data_phase_finished(ait, pplayer);
    }
  }
}

/**********************************************************************//**
  Use on a unit which no longer needs a boat.
**************************************************************************/
void aiferry_clear_boat(struct ai_type *ait, struct unit *punit)
{
  struct unit_ai *unit_data = def_ai_unit_data(punit, ait);

  if (unit_data->ferryboat == FERRY_WANTED) {
    struct player *pplayer = unit_owner(punit);

    if (is_ai_data_phase_open(ait, pplayer)) {
      struct ai_plr *ai = dai_plr_data_get(ait, pplayer, NULL);

      ai->stats.passengers--;
    }
  } else {
    struct unit *ferry = game_unit_by_number(unit_data->ferryboat);

    if (ferry) {
      struct unit_ai *ferry_data = def_ai_unit_data(ferry, ait);

      if (ferry_data->passenger == punit->id) {
        /* punit doesn't want us anymore */
        struct player *pplayer = unit_owner(ferry);

        if (is_ai_data_phase_open(ait, pplayer)) {
          dai_plr_data_get(ait, pplayer, NULL)->stats.available_boats++;
        }
        ferry_data->passenger = FERRY_AVAILABLE;
      }
    }
  }

  unit_data->ferryboat = FERRY_NONE;
}

/**********************************************************************//**
  Request a boat for the unit.  Should only be used if the unit is on the
  coast, otherwise ferries will not see it.
**************************************************************************/
static void aiferry_request_boat(struct ai_type *ait, struct unit *punit)
{
  struct ai_plr *ai = dai_plr_data_get(ait, unit_owner(punit), NULL);
  struct unit_ai *unit_data = def_ai_unit_data(punit, ait);

  /* First clear the previous assignments (just in case there are). 
   * Substract virtual units or already counted */
  if ((punit->id == 0) || 
      ((ai->stats.passengers > 0) && 
       (unit_data->ferryboat == FERRY_WANTED))) {
    aiferry_clear_boat(ait, punit);
  }

  /* Now add ourselves to the list of potential passengers */
  ai->stats.passengers++;
  UNIT_LOG(LOG_DEBUG, punit, "requests a boat (total passengers=%d).",
           ai->stats.passengers);
  unit_data->ferryboat = FERRY_WANTED;

  /* Lastly, wait for ferry. */
  unit_data->done = TRUE;
}

/**********************************************************************//**
  Assign the passenger to the boat and vice versa.
**************************************************************************/
static void aiferry_psngr_meet_boat(struct ai_type *ait,
                                    struct unit *punit, struct unit *pferry)
{
  struct unit_ai *ferry_data = def_ai_unit_data(pferry, ait);
  struct player *ferry_owner = unit_owner(pferry);

  fc_assert_ret(unit_owner(punit) == ferry_owner);

  /* First delete the unit from the list of passengers and 
   * release its previous ferry */
  aiferry_clear_boat(ait, punit);

  /* If ferry was available, update the stats */
  if (ferry_data->passenger == FERRY_AVAILABLE) {
    dai_plr_data_get(ait, ferry_owner, NULL)->stats.available_boats--;
  }

  /* Exchange the phone numbers */
  def_ai_unit_data(punit, ait)->ferryboat = pferry->id;
  ferry_data->passenger = punit->id;
}

/**********************************************************************//**
  Mark the ferry as available and update the statistics.
**************************************************************************/
static void aiferry_make_available(struct ai_type *ait, struct unit *pferry)
{
  struct unit_ai *ferry_data = def_ai_unit_data(pferry, ait);

  if (ferry_data->passenger != FERRY_AVAILABLE) {
    dai_plr_data_get(ait, unit_owner(pferry), NULL)->stats.available_boats++;
    ferry_data->passenger = FERRY_AVAILABLE;
  }
}

/**********************************************************************//**
  Returns the number of available boats.  A simple accessor made to perform 
  debug checks.
**************************************************************************/
int aiferry_avail_boats(struct ai_type *ait, struct player *pplayer)
{
  struct ai_plr *ai = dai_plr_data_get(ait, pplayer, NULL);

  /* To developer: Switch this checking on when testing some new 
   * ferry code. */
#ifdef LOGLEVEL_FERRY_STATS
  int boats = 0;

  unit_list_iterate(pplayer->units, punit) {
    if (dai_is_ferry(punit, ait)
        && def_ai_unit_data(punit, ait)->passenger == FERRY_AVAILABLE) {
      boats++;
    }
  } unit_list_iterate_end;

  if (boats != ai->stats.available_boats) {
    log_error("Player[%d] in turn %d: boats miscounted.",
              player_number(pplayer), game.info.turn);
    aiferry_print_stats(ait, pplayer);
  }
#endif /* LOGLEVEL_FERRY_STATS */

  return ai->stats.available_boats;
}

/* ================== functions to find a boat ========================= */

/**********************************************************************//**
  Combined cost function for a land unit looking for a ferry.  The path
  finding first goes over the continent and then into the ocean where we
  actually look for ferry.  Thus moves land-to-sea are allowed and moves
  sea-to-land are not.  A consequence is that we don't get into the cities
  on other continent, which might station boats.  This defficiency seems to
  be impossible to fix with the current PF structure, so it has to be
  accounted for in the actual ferry search function.

  For movements sea-to-sea the cost is collected via the extra cost
  call-back.  Doesn't care for enemy/neutral tiles, these should be
  excluded using a TB call-back.
**************************************************************************/
static int combined_land_sea_move(const struct tile *src_tile,
                                  enum pf_move_scope src_scope,
                                  const struct tile *tgt_tile,
                                  enum pf_move_scope dst_scope,
                                  const struct pf_parameter *param)
{
  int move_cost;

  if (!((PF_MS_NATIVE | PF_MS_CITY) & dst_scope)) {
    /* Any-to-Sea */
    move_cost = 0;
  } else if (!((PF_MS_NATIVE | PF_MS_CITY) & src_scope)) {
    /* Sea-to-Land */
    move_cost = PF_IMPOSSIBLE_MC;
  } else {
    /* Land-to-Land */
    move_cost = map_move_cost(&(wld.map), param->owner, param->utype,
                              src_tile, tgt_tile);
  }

  return move_cost;
}

/**********************************************************************//**
  EC callback to account for the cost of sea moves by a ferry hurrying to 
  pick our unit up.
**************************************************************************/
static int sea_move(const struct tile *ptile, enum known_type known,
                    const struct pf_parameter *param)
{
  if (is_ocean_tile(ptile)) {
    /* Approximately TURN_FACTOR / average ferry move rate 
     * we can pass a better guess of the move rate through param->data
     * but we don't know which boat we will find out there */
    return SINGLE_MOVE * PF_TURN_FACTOR / 12;
  } else {
    return 0;
  }
}

/**********************************************************************//**
  Runs a few checks to determine if "boat" is a free boat that can carry
  "cap" units of the same type as "punit" over sea.
**************************************************************************/
bool is_boat_free(struct ai_type *ait, struct unit *boat,
                  struct unit *punit, int cap)
{
  /* - Only transporters capable of transporting this unit are eligible.
   * - Units with orders are skipped (the AI doesn't control units with
   *   orders).
   * - Only boats that we own are eligible.
   * - Only available boats or boats that are already dedicated to this unit
   *   are eligible.
   * - Only boats with enough remaining capacity are eligible.
   * - Only units that can travel at sea are eligible.
   * - Units that require fuel, except "Coast" ones, or lose hitpoints are
   *   not eligible.
   */
  struct unit_class *ferry_class = unit_class_get(boat);
  struct unit_type *ferry_type = unit_type_get(boat);
  struct unit_ai *boat_data = def_ai_unit_data(boat, ait);

  return (can_unit_transport(boat, punit)
          && !unit_has_orders(boat)
          && unit_owner(boat) == unit_owner(punit)
          && (boat_data->passenger == FERRY_AVAILABLE
              || boat_data->passenger == punit->id)
          && (get_transporter_capacity(boat)
              - get_transporter_occupancy(boat) >= cap)
          && ferry_class->adv.sea_move != MOVE_NONE
          && !is_losing_hp(boat)
          && (ferry_type->fuel == 0 || utype_has_flag(ferry_type, UTYF_COAST)));
}

/**********************************************************************//**
  Check if unit is boss in ferry
**************************************************************************/
bool is_boss_of_boat(struct ai_type *ait, struct unit *punit)
{
  if (!unit_transported(punit)) {
    /* Not even in boat */
    return FALSE;
  }

  if (unit_transported(punit)
      && def_ai_unit_data(unit_transport_get(punit), ait)->passenger
         == punit->id) {
    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Proper and real PF function for finding a boat.  If you don't require
  the path to the ferry, pass path=NULL.
  Return the unit ID of the boat; punit is the passenger.

  WARNING: Due to the nature of this function and PF (see the comment of
  combined_land_sea_move), the path won't lead onto the boat itself.
**************************************************************************/
int aiferry_find_boat(struct ai_type *ait, struct unit *punit,
                      int cap, struct pf_path **path)
{
  int best_turns = FC_INFINITY;
  int best_id = 0;
  struct pf_parameter param;
  struct pf_map *search_map;
  struct player *pplayer = unit_owner(punit);

  /* currently assigned ferry */
  int ferryboat = def_ai_unit_data(punit, ait)->ferryboat;

  /* We may end calling pf_destroy_path for *path if it's not NULL.
   * Most likely you are passing garbage or path you don't want
   * destroyed if this assertion fails.
   * Don't try to be clever and pass 'fallback' path that will be returned
   * if no path is found. Instead check for NULL return value and then
   * use fallback path in calling function. */
  fc_assert_ret_val(path == NULL || *path == NULL, 0);

  fc_assert_ret_val(0 < ferryboat
                    || FERRY_NONE == ferryboat
                    || FERRY_WANTED == ferryboat, 0);
  UNIT_LOG(LOGLEVEL_FINDFERRY, punit, "asked aiferry_find_boat for a boat");

  if (aiferry_avail_boats(ait, pplayer) <= 0 
      && ferryboat <= 0) {
    /* No boats to be found (the second check is to ensure that we are not 
     * the ones keeping the last boat busy) */
    return 0;
  }

  pft_fill_unit_parameter(&param, punit);
  param.omniscience = !has_handicap(pplayer, H_MAP);
  param.get_TB = no_fights_or_unknown;
  param.get_EC = sea_move;
  param.get_MC = combined_land_sea_move;
  param.ignore_none_scopes = FALSE;

  search_map = pf_map_new(&param);

  pf_map_positions_iterate(search_map, pos, TRUE) {
   /* Should this be !can_unit_exist_at_tile() instead of is_ocean() some day?
    * That would allow special units to wade in shallow coast waters to meet
    * ferry where deep sea starts. */
    int radius = (is_ocean_tile(pos.tile) ? 1 : 0);

    if (pos.turn + pos.total_EC/PF_TURN_FACTOR > best_turns) {
      /* Won't find anything better */
      /* FIXME: This condition is somewhat dodgy */
      break;
    }
    
    square_iterate(&(wld.map), pos.tile, radius, ptile) {
      unit_list_iterate(ptile->units, aunit) {
        if (is_boat_free(ait, aunit, punit, cap)) {
          /* Turns for the unit to get to rendezvous pnt */
          int u_turns = pos.turn;
          /* Turns for the boat to get to the rendezvous pnt */
          int f_turns = ((pos.total_EC / PF_TURN_FACTOR * 16 
                          - aunit->moves_left) 
                         / unit_type_get(aunit)->move_rate);
          int turns = MAX(u_turns, f_turns);

          if (turns < best_turns) {
            UNIT_LOG(LOGLEVEL_FINDFERRY, punit, 
                     "Found a potential boat %s[%d](%d,%d)(moves left: %d)",
                     unit_rule_name(aunit), aunit->id,
                     TILE_XY(unit_tile(aunit)), aunit->moves_left);
            if (path) {
             if (*path) {
                pf_path_destroy(*path);
              }
	      *path = pf_map_iter_path(search_map);
	    }
            best_turns = turns;
            best_id = aunit->id;
          }
        }
      } unit_list_iterate_end;
    } square_iterate_end;
  } pf_map_positions_iterate_end;
  pf_map_destroy(search_map);

  return best_id;
}

/**********************************************************************//**
  Find a boat within one move from us (i.e. a one we can board).
**************************************************************************/
static int aiferry_find_boat_nearby(struct ai_type *ait, struct unit *punit,
                                    int cap)
{
  UNIT_LOG(LOGLEVEL_FINDFERRY, punit, "asked find_ferry_nearby for a boat");

  square_iterate(&(wld.map), unit_tile(punit), 1, ptile) {
    unit_list_iterate(ptile->units, aunit) {
      if (is_boat_free(ait, aunit, punit, cap)) {
        return aunit->id;
      }
    } unit_list_iterate_end;
  } square_iterate_end;

  return 0;
}


/* ============================= go by boat ============================== */

/**********************************************************************//**
  Manage the passengers on a ferry, even if they are asleep.
  This is suitable for when the commander of a ferry has left;
  it gives a chance for another passenger to take control.
**************************************************************************/
static void dai_activate_passengers(struct ai_type *ait, struct unit *ferry)
{
  struct player *ferry_owner = unit_owner(ferry);
  
  unit_list_iterate_safe(unit_tile(ferry)->units, aunit) {
    if (unit_transport_get(aunit) == ferry) {
      unit_activity_handling(aunit, ACTIVITY_IDLE);
      def_ai_unit_data(aunit, ait)->done = FALSE;

      if (unit_owner(aunit) == ferry_owner) {
        /* Move it only if it's our own. */
        dai_manage_unit(ait, ferry_owner, aunit);
      }
    }
  } unit_list_iterate_safe_end;
}

/**********************************************************************//**
  Move a passenger on a ferry to a specified destination.
  The passenger is assumed to be on the given ferry.
  The destination may be inland, in which case the passenger will ride
  the ferry to a beach head, disembark, then continue on land.
  Return FALSE iff we died.
**************************************************************************/
bool dai_amphibious_goto_constrained(struct ai_type *ait,
                                     struct unit *ferry,
                                     struct unit *passenger,
                                     struct tile *ptile,
                                     struct pft_amphibious *parameter)
{
  bool alive = TRUE;
  struct player *pplayer = unit_owner(passenger);
  struct pf_map *pfm;
  struct pf_path *path;
  int pass_id = passenger->id;

  fc_assert_ret_val(is_ai(pplayer), TRUE);
  fc_assert_ret_val(!unit_has_orders(passenger), TRUE);
  fc_assert_ret_val(unit_tile(ferry) == unit_tile(passenger), TRUE);

  ptile = immediate_destination(passenger, ptile);

  if (same_pos(unit_tile(passenger), ptile)) {
    /* Not an error; sometimes immediate_destination instructs the unit
     * to stay here. For example, to refuel.*/
    send_unit_info(NULL, passenger);
    return TRUE;
  } else if (passenger->moves_left == 0 && ferry->moves_left == 0) {
    send_unit_info(NULL, passenger);
    return TRUE;
  }

  pfm = pf_map_new(&parameter->combined);
  path = pf_map_path(pfm, ptile);

  if (path) {
    dai_log_path(passenger, path, &parameter->combined);
    /* Sea leg */
    alive = adv_follow_path(ferry, path, ptile);
    if (alive && unit_tile(passenger) != ptile) {
      /* Ferry has stopped; it is at the landing beach or
       * has run out of movement points */
      struct tile *next_tile;

      if (!pf_path_advance(path, unit_tile(passenger))) {
        /* Somehow we got thrown away from our route.
         * This can happen if our movement caused alliance breakup. */
        return unit_is_alive(pass_id);
      }
      next_tile = path->positions[1].tile;
      if (!is_ocean_tile(next_tile)) {
        int ferry_id = ferry->id;

	UNIT_LOG(LOG_DEBUG, passenger, "Our boat has arrived "
		 "[%d](moves left: %d)", ferry->id, ferry->moves_left);
	UNIT_LOG(LOG_DEBUG, passenger, "Disembarking to (%d,%d)",
		 TILE_XY(next_tile));
	/* Land leg */
        alive = adv_follow_path(passenger, path, ptile);
        /* Movement of the passenger outside the ferry can cause also
         * ferry to die. That has happened at least when passenger
         * destroyed city cutting the civ1-style channel (cities in
         * a chain) ferry was in. */
	if (unit_is_alive(ferry_id) && 0 < ferry->moves_left
            && (!alive || unit_tile(ferry) != unit_tile(passenger))) {
	  /* The passenger is no longer on the ferry,
	   * and the ferry can still act.
	   * Give a chance for another passenger to take command
	   * of the ferry.
	   */
	  UNIT_LOG(LOG_DEBUG, ferry, "Activating passengers");
          dai_activate_passengers(ait, ferry);

          /* It is theoretically possible passenger died here due to
           * autoattack against another passing unit at its location. */
          alive = unit_is_alive(pass_id);
        }
      }
      /* else at sea */
    }
    /* else dead or arrived */
  } else {
    /* Not always an error; enemy units might block all paths. */
    UNIT_LOG(LOG_DEBUG, passenger, "no path to destination");
  }

  pf_path_destroy(path);
  pf_map_destroy(pfm);

  return alive;
}

/**********************************************************************//**
  Move a passenger on a ferry to a specified destination.
  Return FALSE iff we died.
**************************************************************************/
bool aiferry_goto_amphibious(struct ai_type *ait, struct unit *ferry,
			     struct unit *passenger, struct tile *ptile)
{
  struct pft_amphibious parameter;
  struct adv_risk_cost land_risk_cost;
  struct adv_risk_cost sea_risk_cost;

  dai_fill_unit_param(ait, &parameter.land, &land_risk_cost, passenger, ptile);
  if (parameter.land.get_TB != no_fights) {
    /* Use the ferry to go around danger areas: */
    parameter.land.get_TB = no_intermediate_fights;
  }
  dai_fill_unit_param(ait, &parameter.sea, &sea_risk_cost, ferry, ptile);
  pft_fill_amphibious_parameter(&parameter);

  /* Move as far along the path to the destination as we can;
   * that is, ignore the presence of enemy units when computing the
   * path */
  parameter.combined.get_zoc = NULL;

  return dai_amphibious_goto_constrained(ait, ferry, passenger, ptile,
                                         &parameter);
}

/**********************************************************************//**
  This function is to be called if punit needs to use a boat to get to the 
  destination.

  Return values: TRUE if got to or next to our destination, FALSE otherwise. 

  TODO: A big one is rendezvous points between units and boats.  When this is 
  implemented, we won't have to be at the coast to ask for a boat to come 
  to us.
**************************************************************************/
bool aiferry_gobyboat(struct ai_type *ait, struct player *pplayer,
                      struct unit *punit, struct tile *dest_tile, bool with_bodyguard)
{
  if (!unit_transported(punit)) {
    /* We are not on a boat and we cannot walk */
    int boatid;
    struct unit *ferryboat = NULL;
    int cap = with_bodyguard ? 2 : 1;

    UNIT_LOG(LOGLEVEL_GOBYBOAT, punit, "will have to go to (%d,%d) by boat",
             TILE_XY(dest_tile));

    if (!is_terrain_class_near_tile(unit_tile(punit), TC_OCEAN)) {
      struct pf_path *path_to_ferry = NULL;

      boatid = aiferry_find_boat(ait, punit, cap, &path_to_ferry);
      if (boatid <= 0) {
        UNIT_LOG(LOGLEVEL_GOBYBOAT, punit,
                 "in ai_gothere cannot find any boats.");
        def_ai_unit_data(punit, ait)->done = TRUE; /* Nothing to do */
        return FALSE;
      }

      ferryboat = game_unit_by_number(boatid);
      UNIT_LOG(LOGLEVEL_GOBYBOAT, punit, "found boat[%d](%d,%d), going there", 
	       boatid, TILE_XY(unit_tile(ferryboat)));
      /* The path can be amphibious so we will stop at the coast.  
       * It might not lead _onto_ the boat. */
      if (!adv_unit_execute_path(punit, path_to_ferry)) { 
        /* Died. */
	pf_path_destroy(path_to_ferry);
        return FALSE;
      }
      pf_path_destroy(path_to_ferry);
    }

    if (!is_terrain_class_near_tile(unit_tile(punit), TC_OCEAN)) {
      /* Still haven't reached the coast */
      return FALSE;
    }

    /* We are on the coast, look around for a boat */
    boatid = aiferry_find_boat_nearby(ait, punit, cap);
    if (boatid <= 0) {
      UNIT_LOG(LOGLEVEL_GOBYBOAT, punit, "requesting a boat.");
      aiferry_request_boat(ait, punit);
      return FALSE;
    }

    /* Ok, a boat found, try boarding it */
    ferryboat = game_unit_by_number(boatid);
    UNIT_LOG(LOGLEVEL_GOBYBOAT, punit, "found a nearby boat[%d](%d,%d)",
	     ferryboat->id, TILE_XY(unit_tile(ferryboat)));
    /* Setting ferry now in hope it won't run away even 
     * if we can't board it right now */
    aiferry_psngr_meet_boat(ait, punit, ferryboat);

    if (is_tiles_adjacent(unit_tile(punit), unit_tile(ferryboat))) {
      (void) dai_unit_move(ait, punit, unit_tile(ferryboat));
    }

    if (!can_unit_load(punit, ferryboat)) {
      /* Something prevented us from boarding */
      /* FIXME: this is probably a serious bug, but we just skip past
       * it and continue. */
      UNIT_LOG(LOGLEVEL_GOBYBOAT, punit, "couldn't board boat[%d](%d,%d)",
	       ferryboat->id, TILE_XY(unit_tile(ferryboat)));
      return FALSE;
    }

    handle_unit_load(pplayer, punit->id, ferryboat->id, ferryboat->tile->index);
    fc_assert(unit_transported(punit));
  }

  if (unit_transported(punit)) {
    /* We are on a boat, ride it! */
    struct unit *ferryboat = unit_transport_get(punit);

    /* Check if we are the passenger-in-charge */
    if (is_boat_free(ait, ferryboat, punit, 0)) {
      struct unit *bodyguard = aiguard_guard_of(ait, punit);

      UNIT_LOG(LOGLEVEL_GOBYBOAT, punit, 
	       "got boat[%d](moves left: %d), going (%d,%d)",
               ferryboat->id, ferryboat->moves_left, TILE_XY(dest_tile));
      aiferry_psngr_meet_boat(ait, punit, ferryboat);

      punit->goto_tile = dest_tile;
      /* Grab bodyguard */
      if (bodyguard
          && !same_pos(unit_tile(punit), unit_tile(bodyguard))) {
        if (!goto_is_sane(bodyguard, unit_tile(punit))
            || !dai_unit_goto(ait, bodyguard, unit_tile(punit))) {
          /* Bodyguard can't get there or died en route */
          aiguard_request_guard(ait, punit);
          bodyguard = NULL;
        } else if (bodyguard->moves_left <= 0) {
          /* Wait for me, I'm cooooming!! */
          UNIT_LOG(LOGLEVEL_GOBYBOAT, punit, "waiting for bodyguard");
          def_ai_unit_data(punit, ait)->done = TRUE;
          return FALSE;
        } else {
          /* Crap bodyguard. Got stuck somewhere. Ditch it! */
          UNIT_LOG(LOGLEVEL_GOBYBOAT, punit, "ditching useless bodyguard");
          aiguard_request_guard(ait, punit);
          dai_unit_new_task(ait, bodyguard, AIUNIT_NONE, NULL);
          bodyguard = NULL;
        }
      }
      if (bodyguard) {
        fc_assert(same_pos(unit_tile(punit), unit_tile(bodyguard)));
        handle_unit_load(pplayer, bodyguard->id, ferryboat->id,
                         ferryboat->tile->index);
      }
      if (!aiferry_goto_amphibious(ait, ferryboat, punit, dest_tile)) {
        /* died */
        return FALSE;
      }
      if (same_pos(unit_tile(punit), dest_tile)) {
        /* Arrived */
        unit_activity_handling(punit, ACTIVITY_IDLE);
      } else {
        /* We are in still transit */
        def_ai_unit_data(punit, ait)->done = TRUE;
        return FALSE;
      }
    } else {
      /* Waiting for the boss to load and move us */
      UNIT_LOG(LOGLEVEL_GOBYBOAT, punit, "Cannot command boat [%d],"
               " its boss is [%d]", 
               ferryboat->id, def_ai_unit_data(ferryboat, ait)->passenger);
      return FALSE;
    }
  }

  return TRUE;
}


/* ===================== boat management ================================= */

/**********************************************************************//**
  A helper for ai_manage_ferryboat.  Finds a passenger for the ferry.
  Potential passengers signal the boats by setting their ai.ferry field to
  FERRY_WANTED.

  TODO: lift the path off the map
**************************************************************************/
static bool aiferry_findcargo(struct ai_type *ait, struct unit *pferry)
{
  /* Path-finding stuff */
  struct pf_map *pfm;
  struct pf_parameter parameter;
  int passengers = dai_plr_data_get(ait, unit_owner(pferry), NULL)->stats.passengers;
  struct player *pplayer;

  if (passengers <= 0) {
    /* No passangers anywhere */
    return FALSE;
  }

  UNIT_LOG(LOGLEVEL_FERRY, pferry, "Ferryboat is looking for cargo.");

  pplayer = unit_owner(pferry);
  pft_fill_unit_overlap_param(&parameter, pferry);
  parameter.omniscience = !has_handicap(pplayer, H_MAP);
  /* If we have omniscience, we use it, since paths to some places
   * might be "blocked" by unknown.  We don't want to fight though */
  parameter.get_TB = no_fights;
  
  pfm = pf_map_new(&parameter);
  pf_map_tiles_iterate(pfm, ptile, TRUE) {
    unit_list_iterate(ptile->units, aunit) {
      struct unit_ai *unit_data = def_ai_unit_data(aunit, ait);

      if (unit_owner(pferry) == unit_owner(aunit) 
          && (unit_data->ferryboat == FERRY_WANTED
              || unit_data->ferryboat == pferry->id)) {
        UNIT_LOG(LOGLEVEL_FERRY, pferry, 
                 "Found a potential cargo %s[%d](%d,%d), going there",
                 unit_rule_name(aunit),
                 aunit->id,
                 TILE_XY(unit_tile(aunit)));
	pferry->goto_tile = unit_tile(aunit);
        /* Exchange phone numbers */
        aiferry_psngr_meet_boat(ait, aunit, pferry);
        pf_map_destroy(pfm);
        return TRUE;
      }
    } unit_list_iterate_end;
  } pf_map_tiles_iterate_end;

  /* False positive can happen if we cannot find a route to the passenger
   * because of an internal sea or enemy blocking the route */
  UNIT_LOG(LOGLEVEL_FERRY, pferry,
           "AI Passengers counting reported false positive %d", passengers);
  pf_map_destroy(pfm);
  return FALSE;
}

/**********************************************************************//**
  A helper for ai_manage_ferryboat.  Finds a city that wants a ferry.  It
  might signal for the ferry using pcity->server.ai.choice.need_boat field or
  it might simply be building a ferry of it's own.

  The city found will be set as the goto destination.

  TODO: lift the path off the map
  TODO (possible): put this and ai_ferry_findcargo into one PF-loop.  This 
  will save some code lines but will be faster in the rare cases when there
  passengers that can not be reached ("false positive").
**************************************************************************/
static bool aiferry_find_interested_city(struct ai_type *ait,
                                         struct unit *pferry)
{
  /* Path-finding stuff */
  struct pf_map *pfm;
  struct pf_parameter parameter;
  /* Early termination condition */
  int turns_horizon = FC_INFINITY;
  /* Future return value */
  bool needed = FALSE;

  UNIT_LOG(LOGLEVEL_FERRY, pferry, "Ferry looking for a city that needs it");

  pft_fill_unit_parameter(&parameter, pferry);
  /* We are looking for our own cities, no need to look into the unknown */
  parameter.get_TB = no_fights_or_unknown;
  parameter.omniscience = FALSE;
  pfm = pf_map_new(&parameter);

  pf_map_positions_iterate(pfm, pos, TRUE) {
    struct city *pcity;

    if (pos.turn >= turns_horizon) {
      /* Won't be able to find anything better than what we have */
      break;
    }

    pcity = tile_city(pos.tile);
    
    if (pcity && city_owner(pcity) == unit_owner(pferry)
        && (def_ai_city_data(pcity, ait)->choice.need_boat 
            || (VUT_UTYPE == pcity->production.kind
		&& utype_has_role(pcity->production.value.utype,
				  L_FERRYBOAT)))) {
      bool really_needed = TRUE;
      int turns = city_production_turns_to_build(pcity, TRUE);

      UNIT_LOG(LOGLEVEL_FERRY, pferry, "%s (%d, %d) looks promising...", 
               city_name_get(pcity), TILE_XY(pcity->tile));

      if (pos.turn > turns
          && VUT_UTYPE == pcity->production.kind
          && utype_has_role(pcity->production.value.utype, L_FERRYBOAT)) {
        UNIT_LOG(LOGLEVEL_FERRY, pferry, "%s is NOT suitable: "
                 "will finish building its own ferry too soon",
                 city_name_get(pcity));
        continue;
      }

      if (turns >= turns_horizon) {
        UNIT_LOG(LOGLEVEL_FERRY, pferry, "%s is NOT suitable: "
                 "has just started building",
                 city_name_get(pcity));
        continue;
      }

      unit_list_iterate(pos.tile->units, aunit) {
	if (aunit != pferry && unit_owner(aunit) == unit_owner(pferry)
            && unit_has_type_role(aunit, L_FERRYBOAT)) {

          UNIT_LOG(LOGLEVEL_FERRY, pferry, "%s is NOT suitable: "
                   "has another ferry",
                   city_name_get(pcity));
	  really_needed = FALSE;
	  break;
	}
      } unit_list_iterate_end;

      if (really_needed) {
        UNIT_LOG(LOGLEVEL_FERRY, pferry, "will go to %s unless we "
                 "find something better",
                 city_name_get(pcity));
	pferry->goto_tile = pos.tile;
        turns_horizon = turns;
        needed = TRUE;
      }
    }
  } pf_map_positions_iterate_end;

  pf_map_destroy(pfm);
  return needed;
}

/**********************************************************************//**
  It's about 12 feet square and has a capacity of almost 1000 pounds.
  It is well constructed of teak, and looks seaworthy.

  Manage ferryboat.  If there is a passenger-in-charge, we let it drive the
  boat.  If there isn't, appoint one from those we have on board.

  If there is no one aboard, look for potential cargo.  If none found,
  explore and then go to the nearest port.
**************************************************************************/
void dai_manage_ferryboat(struct ai_type *ait, struct player *pplayer,
                          struct unit *punit)
{
  struct city *pcity;
  int sanity = punit->id;
  struct unit_ai *unit_data;
  int bossid;
  struct unit_type *ptype;

  CHECK_UNIT(punit);

  /* Try to recover hitpoints if we are in a city, before we do anything */
  if (punit->hp < unit_type_get(punit)->hp 
      && (pcity = tile_city(unit_tile(punit)))) {
    UNIT_LOG(LOGLEVEL_FERRY, punit, "waiting in %s to recover hitpoints", 
             city_name_get(pcity));
    def_ai_unit_data(punit, ait)->done = TRUE;
    return;
  }

  /* Check if we are an empty barbarian boat and so not needed */
  if (is_barbarian(pplayer) && get_transporter_occupancy(punit) == 0) {
    wipe_unit(punit, ULR_RETIRED, NULL);
    return;
  }

  unit_data = def_ai_unit_data(punit, ait);

  bossid = unit_data->passenger; /* Old boss */

  do {
    /* Do we have the passenger-in-charge on board? */
    if (unit_data->passenger > 0) {
      struct unit *psngr = game_unit_by_number(unit_data->passenger);

      /* If the passenger-in-charge is adjacent, we should wait for it to 
       * board.  We will pass control to it later. */
      if (!psngr
          || real_map_distance(unit_tile(punit), unit_tile(psngr)) > 1) {
        UNIT_LOG(LOGLEVEL_FERRY, punit, 
                 "recorded passenger[%d] is not on board, checking for "
                 "others", unit_data->passenger);
        unit_data->passenger = FERRY_ABANDON_BOSS;
      }
    }

    if (unit_data->passenger == FERRY_AVAILABLE
        || unit_data->passenger == FERRY_ABANDON_BOSS) {
      struct unit *candidate = NULL;

      /* Try to select passanger-in-charge from among our passengers */
      unit_list_iterate(punit->transporting, aunit) {
        if (unit_owner(aunit) != pplayer) {
          /* We used to check if ferryboat was set to us or to
           * FERRY_WANTED too, but this was a bit strict. Especially
           * when we don't save these values in a savegame. */
          continue;
        }

        if (def_ai_unit_data(aunit, ait)->task != AIUNIT_ESCORT) {
          candidate = aunit;
          break;
        } else {
          /* Bodyguards shouldn't be in charge of boats so continue looking */
          candidate = aunit;
        }
      } unit_list_iterate_end;
      
      if (candidate) {
        UNIT_LOG(LOGLEVEL_FERRY, punit, 
                 "appointed %s[%d] our passenger-in-charge",
                 unit_rule_name(candidate),
                 candidate->id);
        bossid = candidate->id;
        aiferry_psngr_meet_boat(ait, candidate, punit);
      }
    }

    if (unit_data->passenger > 0) {
      struct unit *boss = game_unit_by_number(bossid);

      fc_assert_ret(NULL != boss);

      if (unit_has_type_flag(boss, UTYF_SETTLERS)
          || unit_is_cityfounder(boss)) {
        /* Temporary hack: settlers all go in the end, forcing them 
         * earlier might mean uninitialised cache, so just wait for them */
        return;
      }

      UNIT_LOG(LOGLEVEL_FERRY, punit, "passing control to %s[%d]",
		unit_rule_name(boss),
		boss->id);
      dai_manage_unit(ait, pplayer, boss);
    
      if (!game_unit_by_number(sanity) || punit->moves_left <= 0) {
        return;
      }
      if (game_unit_by_number(bossid)) {
	if (same_pos(unit_tile(punit), unit_tile(boss))) {
	  /* The boss decided to stay put on the ferry. We aren't moving. */
          UNIT_LOG(LOG_DEBUG, boss, "drove ferry - done for now");
          def_ai_unit_data(boss, ait)->done = TRUE;
          return;
        } else if (unit_data->passenger == bossid
                   && get_transporter_occupancy(punit) != 0) {
          /* The boss isn't on the ferry, has not passed control away,
           * and we have other passengers?
           * Forget about him. */
          unit_data->passenger = FERRY_ABANDON_BOSS;
        }
      }
      if (unit_data->passenger != bossid
          && unit_data->passenger != FERRY_ABANDON_BOSS) {
        /* Boss handling resulted in boss change. Run next round with new boss. */
        bossid = unit_data->passenger;
      }
    } else {
      /* Cannot select a passenger-in-charge */
      break;
    }
  } while (get_transporter_occupancy(punit) != 0);

  if (unit_data->passenger == FERRY_ABANDON_BOSS) {
    /* As nobody else wanted the control, restore it to old boss. */
    unit_data->passenger = bossid;
  }

  /* Not carrying anyone, even the ferryman */

  ptype = unit_type_get(punit);

  if (IS_ATTACKER(ptype) && punit->moves_left > 0) {
     /* AI used to build frigates to attack and then use them as ferries 
      * -- Syela */
    dai_unit_new_task(ait, punit, AIUNIT_NONE, NULL);
    UNIT_LOG(LOGLEVEL_FERRY, punit, "passing ferry over to attack code");
    dai_manage_military(ait, pplayer, punit);
    return;
  }

  UNIT_LOG(LOGLEVEL_FERRY, punit, "Ferryboat is not carrying anyone "
	   "(moves left: %d).", punit->moves_left);
  aiferry_make_available(ait, punit);
  unit_activity_handling(punit, ACTIVITY_IDLE);
  dai_unit_new_task(ait, punit, AIUNIT_NONE, NULL);
  CHECK_UNIT(punit);

  /* Try to find passengers */
  if (aiferry_findcargo(ait, punit)) {
    UNIT_LOG(LOGLEVEL_FERRY, punit, "picking up cargo (moves left: %d)",
	     punit->moves_left);
    if (dai_unit_goto(ait, punit, punit->goto_tile)) {
      if (is_tiles_adjacent(unit_tile(punit), punit->goto_tile)
          || same_pos(unit_tile(punit), punit->goto_tile)) {
        struct unit *cargo = game_unit_by_number(unit_data->passenger);

        /* See if passenger can jump on board! */
        fc_assert_ret(cargo != punit);
        dai_manage_unit(ait, pplayer, cargo);
      }
    }
    return;
  }

  /* Try to find a city that needs a ferry */
  if (aiferry_find_interested_city(ait, punit)) {
    if (same_pos(unit_tile(punit), punit->goto_tile)) {
      UNIT_LOG(LOGLEVEL_FERRY, punit, "staying in city that needs us");
      unit_data->done = TRUE;
      return;
    } else {
      UNIT_LOG(LOGLEVEL_FERRY, punit, "going to city that needs us");
      if (dai_unit_goto(ait, punit, punit->goto_tile)
          && same_pos(unit_tile(punit), punit->goto_tile)) {
        unit_data->done = TRUE; /* save some CPU */
      }
      return;
    }
  }

  UNIT_LOG(LOGLEVEL_FERRY, punit, "Passing control of ferry to explorer code");
  switch (manage_auto_explorer(punit)) {
  case MR_DEATH:
    /* don't use punit! */
    return;
  case MR_OK:
    /* FIXME: continue moving? */
    break;
  default:
    unit_data->done = TRUE;
    break;
  };

  if (punit->moves_left > 0) {
    struct city *safe_city = find_nearest_safe_city(punit);

    if (safe_city != NULL) {
      punit->goto_tile = safe_city->tile;
      UNIT_LOG(LOGLEVEL_FERRY, punit, "No work, going home");
      unit_data->done = TRUE;
      dai_unit_new_task(ait, punit, AIUNIT_NONE, NULL);
      (void) dai_unit_goto(ait, punit, safe_city->tile);
    }
  }
 
  return;
}
