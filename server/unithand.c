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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "combat.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "unit.h"

#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplomats.h"
#include "gamelog.h"
#include "gotohand.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "spacerace.h"
#include "srv_main.h"
#include "unittools.h"

#include "aiexplorer.h"
#include "aitools.h"

#include "unithand.h"

static void city_add_or_build_error(struct player *pplayer,
				    struct unit *punit,
				    enum add_build_city_result res);
static void city_add_unit(struct player *pplayer, struct unit *punit);
static void city_build(struct player *pplayer, struct unit *punit,
		       char *name);
static void handle_unit_activity_request_targeted(struct unit *punit,
						  enum unit_activity
						  new_activity,
						  enum tile_special_type
						  new_target);
static bool base_handle_unit_establish_trade(struct player *pplayer, int unit_id, struct city *pcity_dest);
static void how_to_declare_war(struct player *pplayer);
static bool unit_bombard(struct unit *punit, struct tile *ptile);

/**************************************************************************
...
**************************************************************************/
void handle_unit_goto(struct player *pplayer, int unit_id, int x, int y)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  struct tile *ptile = map_pos_to_tile(x, y);

  if (!ptile || !punit) {
    return;
  }

  free_unit_orders(punit); /* This may reset punit->goto_tile also. */

  punit->goto_tile = ptile;
  set_unit_activity(punit, ACTIVITY_GOTO);

  send_unit_info(NULL, punit);

  (void) do_unit_goto(punit, GOTO_MOVE_ANY, TRUE);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_airlift(struct player *pplayer, int unit_id, int city_id)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  struct city *pcity = find_city_by_id(city_id);

  if (punit && pcity) {
    (void) do_airline(punit, pcity);
  }
}

/**************************************************************************
 Upgrade all units of a given type.
**************************************************************************/
void handle_unit_type_upgrade(struct player *pplayer, Unit_Type_id type)
{
  const Unit_Type_id from_unittype = type;
  const Unit_Type_id to_unittype = can_upgrade_unittype(pplayer,
							from_unittype);
  int number_of_upgraded_units = 0;

  if (to_unittype == -1) {
    notify_player(pplayer,
		  _("Game: Illegal packet, can't upgrade %s (yet)."),
		  unit_types[from_unittype].name);
    return;
  }

  /* 
   * Try to upgrade units. The order we upgrade in is arbitrary (if
   * the player really cared they should have done it manually). 
   */
  conn_list_do_buffer(&pplayer->connections);
  unit_list_iterate(pplayer->units, punit) {
    if (punit->type == from_unittype) {
      enum unit_upgrade_result result = test_unit_upgrade(punit, FALSE);

      if (result == UR_OK) {
	number_of_upgraded_units++;
	upgrade_unit(punit, to_unittype, FALSE);
      } else if (result == UR_NO_MONEY) {
	break;
      }
    }
  } unit_list_iterate_end;
  conn_list_do_unbuffer(&pplayer->connections);

  /* Alert the player about what happened. */
  if (number_of_upgraded_units > 0) {
    const int cost = unit_upgrade_price(pplayer, from_unittype, to_unittype);
    notify_player(pplayer, _("Game: %d %s upgraded to %s for %d gold."),
		  number_of_upgraded_units, unit_types[from_unittype].name,
		  unit_types[to_unittype].name,
		  cost * number_of_upgraded_units);
    send_player_info(pplayer, pplayer);
  } else {
    notify_player(pplayer, _("Game: No units could be upgraded."));
  }
}

/**************************************************************************
 Upgrade a single unit.
**************************************************************************/
void handle_unit_upgrade(struct player *pplayer, int unit_id)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  char buf[512];
  
  if (!punit) {
    return;
  }

  if (get_unit_upgrade_info(buf, sizeof(buf), punit) == UR_OK) {
    Unit_Type_id from_unit = punit->type;
    Unit_Type_id to_unit = can_upgrade_unittype(pplayer, punit->type);
    int cost = unit_upgrade_price(pplayer, punit->type, to_unit);

    upgrade_unit(punit, to_unit, FALSE);
    send_player_info(pplayer, pplayer);
    notify_player(pplayer, _("Game: %s upgraded to %s for %d gold."), 
		  unit_name(from_unit), unit_name(to_unit), cost);
  } else {
    notify_player(pplayer, _("Game: %s"), buf);
  }
}

/***************************************************************
  Tell the client the cost of inciting a revolt or bribing a unit.
  Only send result back to the requesting connection, not all
  connections for that player.
***************************************************************/
void handle_unit_bribe_inq(struct connection *pc, int unit_id)
{
  struct player *pplayer = pc->player;
  struct unit *punit = find_unit_by_id(unit_id);

  if (pplayer && punit) {
    punit->bribe_cost = unit_bribe_cost(punit);
    dsend_packet_unit_bribe_info(pc, unit_id, punit->bribe_cost);
  }
}

/***************************************************************
...
***************************************************************/
void handle_unit_diplomat_action(struct player *pplayer, int diplomat_id,
				 enum diplomat_actions action_type,
				 int target_id, int value)
{
  struct unit *pdiplomat = player_find_unit_by_id(pplayer, diplomat_id);
  struct unit *pvictim = find_unit_by_id(target_id);
  struct city *pcity = find_city_by_id(target_id);

  if (!pdiplomat || !unit_flag(pdiplomat, F_DIPLOMAT)) {
    return;
  }

  if(pdiplomat->moves_left > 0) {
    switch(action_type) {
    case DIPLOMAT_BRIBE:
      if(pvictim && diplomat_can_do_action(pdiplomat, DIPLOMAT_BRIBE,
					   pvictim->tile)) {
	diplomat_bribe(pplayer, pdiplomat, pvictim);
      }
      break;
    case SPY_SABOTAGE_UNIT:
      if(pvictim && diplomat_can_do_action(pdiplomat, SPY_SABOTAGE_UNIT,
					   pvictim->tile)) {
	spy_sabotage_unit(pplayer, pdiplomat, pvictim);
      }
      break;
     case DIPLOMAT_SABOTAGE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_SABOTAGE,
					 pcity->tile)) {
	/* packet value is improvement ID + 1 (or some special codes) */
	diplomat_sabotage(pplayer, pdiplomat, pcity, value - 1);
      }
      break;
    case SPY_POISON:
      if(pcity && diplomat_can_do_action(pdiplomat, SPY_POISON,
					 pcity->tile)) {
	spy_poison(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_INVESTIGATE:
      if(pcity && diplomat_can_do_action(pdiplomat,DIPLOMAT_INVESTIGATE,
					 pcity->tile)) {
	diplomat_investigate(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_EMBASSY:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_EMBASSY,
					 pcity->tile)) {
	diplomat_embassy(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_INCITE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_INCITE,
					 pcity->tile)) {
	diplomat_incite(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_MOVE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_MOVE,
					 pcity->tile)) {
	(void) handle_unit_move_request(pdiplomat, pcity->tile,
					FALSE, TRUE);
      }
      break;
    case DIPLOMAT_STEAL:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_STEAL,
					 pcity->tile)) {
	/* packet value is technology ID (or some special codes) */
	diplomat_get_tech(pplayer, pdiplomat, pcity, value);
      }
      break;
    case SPY_GET_SABOTAGE_LIST:
      if(pcity && diplomat_can_do_action(pdiplomat, SPY_GET_SABOTAGE_LIST,
					 pcity->tile)) {
	spy_get_sabotage_list(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_ANY_ACTION:
      /* Nothing */
      break;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_change_homecity(struct player *pplayer, int unit_id,
				 int city_id)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  struct city *old_pcity, *new_pcity =
      player_find_city_by_id(pplayer, city_id);

  if (!punit || !new_pcity
      || !can_unit_change_homecity_to(punit, new_pcity)) {
    return;
  }

  old_pcity = player_find_city_by_id(pplayer, punit->homecity);

  unit_list_insert(&new_pcity->units_supported, punit);
  if (old_pcity) {
    unit_list_unlink(&old_pcity->units_supported, punit);
  }

  punit->homecity = new_pcity->id;
  send_unit_info(pplayer, punit);

  city_refresh(new_pcity);
  send_city_info(pplayer, new_pcity);

  if (old_pcity) {
    city_refresh(old_pcity);
    send_city_info(pplayer, old_pcity);
  }
}

/**************************************************************************
  Disband a unit.  If its in a city, add 1/2 of the worth of the unit
  to the city's shield stock for the current production.
**************************************************************************/
void handle_unit_disband(struct player *pplayer, int unit_id)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  struct city *pcity;

  if (!punit) {
    return;
  }
  pcity = map_get_city(punit->tile);

  if (!unit_flag(punit, F_UNDISBANDABLE)) { /* refuse to kill ourselves */
    if (pcity) {
      pcity->shield_stock += unit_disband_shields(punit->type);
      /* If we change production later at this turn. No penalty is added. */
      pcity->disbanded_shields += unit_disband_shields(punit->type);

      /* Note: Nowadays it's possible to disband unit in allied city and
       * your ally receives those shields. Should it be like this? Why not?
       * That's why we must use city_owner instead of pplayer -- Zamar */
      send_city_info(city_owner(pcity), pcity);
    }
    wipe_unit(punit);
  } else {
    notify_player_ex(unit_owner(punit), punit->tile, E_NOEVENT,
              _("Game: %s refuses to disband!"), unit_name(punit->type));
    return;
  }
}

/**************************************************************************
 This function assumes that there is a valid city at punit->(x,y) for
 certain values of test_add_build_or_city.  It should only be called
 after a call to unit_add_build_city_result, which does the
 consistency checking.
**************************************************************************/
static void city_add_or_build_error(struct player *pplayer,
				    struct unit *punit,
				    enum add_build_city_result res)
{
  /* Given that res came from test_unit_add_or_build_city, pcity will
     be non-null for all required status values. */
  struct city *pcity = map_get_city(punit->tile);
  const char *unit_name = unit_type(punit)->name;

  switch (res) {
  case AB_NOT_BUILD_LOC:
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: Can't place city here."));
    break;
  case AB_NOT_BUILD_UNIT:
    {
      const char *us = get_units_with_flag_string(F_CITIES);
      if (us) {
	notify_player_ex(pplayer, punit->tile, E_NOEVENT,
			 _("Game: Only %s can build a city."),
			 us);
	free((void *) us);
      } else {
	notify_player_ex(pplayer, punit->tile, E_NOEVENT,
			 _("Game: Can't build a city."));
      }
    }
    break;
  case AB_NOT_ADDABLE_UNIT:
    {
      const char *us = get_units_with_flag_string(F_ADD_TO_CITY);
      if (us) {
	notify_player_ex(pplayer, punit->tile, E_NOEVENT,
			 _("Game: Only %s can add to a city."),
			 us);
	free((void *) us);
      } else {
	notify_player_ex(pplayer, punit->tile, E_NOEVENT,
			 _("Game: Can't add to a city."));
      }
    }
    break;
  case AB_NO_MOVES_ADD:
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: %s unit has no moves left to add to %s."),
		     unit_name, pcity->name);
    break;
  case AB_NO_MOVES_BUILD:
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: %s unit has no moves left to build city."),
		     unit_name);
    break;
  case AB_TOO_BIG:
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: %s is too big to add %s."),
		     pcity->name, unit_name);
    break;
  case AB_NO_SPACE:
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: %s needs an improvement to grow, so "
		       "you cannot add %s."),
		     pcity->name, unit_name);
    break;
  default:
    /* Shouldn't happen */
    freelog(LOG_ERROR, "Cannot add %s to %s for unknown reason",
	    unit_name, pcity->name);
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: Can't add %s to %s."),
		     unit_name, pcity->name);
    break;
  }
}

/**************************************************************************
 This function assumes that there is a valid city at punit->(x,y) It
 should only be called after a call to a function like
 test_unit_add_or_build_city, which does the checking.
**************************************************************************/
static void city_add_unit(struct player *pplayer, struct unit *punit)
{
  struct city *pcity = map_get_city(punit->tile);
  const char *unit_name = unit_type(punit)->name;

  assert(unit_pop_value(punit->type) > 0);
  pcity->size += unit_pop_value(punit->type);
  /* Make the new people something, otherwise city fails the checks */
  pcity->specialists[SP_TAXMAN] += unit_pop_value(punit->type);
  auto_arrange_workers(pcity);
  notify_player_ex(pplayer, pcity->tile, E_NOEVENT,
		   _("Game: %s added to aid %s in growing."),
		   unit_name, pcity->name);
  wipe_unit(punit);
  send_city_info(NULL, pcity);
}

/**************************************************************************
 This function assumes a certain level of consistency checking: There
 is no city under punit->(x,y), and that location is a valid one on
 which to build a city. It should only be called after a call to a
 function like test_unit_add_or_build_city, which does the checking.
**************************************************************************/
static void city_build(struct player *pplayer, struct unit *punit,
		       char *name)
{
  char message[1024];

  if (!is_allowed_city_name(pplayer, name, message, sizeof(message))) {
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: %s"), message);
    return;
  }

  create_city(pplayer, punit->tile, name);
  wipe_unit(punit);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_build_city(struct player *pplayer, int unit_id, char *name)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  enum add_build_city_result res;

  if (!punit) {
    return;
  }

  res = test_unit_add_or_build_city(punit);

  if (res == AB_BUILD_OK)
    city_build(pplayer, punit, name);
  else if (res == AB_ADD_OK)
    city_add_unit(pplayer, punit);
  else
    city_add_or_build_error(pplayer, punit, res);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_change_activity(struct player *pplayer, int unit_id,
				 enum unit_activity activity,
				 enum tile_special_type activity_target)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);

  if (!punit) {
    return;
  }

  if (punit->activity != activity ||
      punit->activity_target != activity_target ||
      punit->ai.control) {
    /* Treat change in ai.control as change in activity, so
       * idle autosettlers behave correctly when selected --dwp
     */
    punit->ai.control = FALSE;
    handle_unit_activity_request_targeted(punit, activity, activity_target);

    /* Exploring is handled here explicitly, since the player expects to
     * see an immediate response from setting a unit to auto-explore.
     * Handling it deeper in the code leads to some tricky recursive loops -
     * see PR#2631. */
    if (punit->moves_left > 0 && activity == ACTIVITY_EXPLORE) {
      int id = punit->id;
      bool more_to_explore = ai_manage_explorer(punit);

      if ((punit = find_unit_by_id(id))) {
	assert(punit->activity == ACTIVITY_EXPLORE);
	if (!more_to_explore) {
	  set_unit_activity(punit, ACTIVITY_IDLE);
	  punit->ai.control = FALSE;
	}
	send_unit_info(NULL, punit);
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_move(struct player *pplayer, int unit_id, int x, int y)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  struct tile *ptile = map_pos_to_tile(x, y);

  if (!ptile || !punit) {
    return;
  }

  if (!is_tiles_adjacent(punit->tile, ptile)) {
    return;
  }
  (void) handle_unit_move_request(punit, ptile, FALSE, FALSE);
}

/**************************************************************************
 Make sure everyone who can see combat does.
**************************************************************************/
static void see_combat(struct unit *pattacker, struct unit *pdefender)
{  
  struct packet_unit_short_info unit_att_short_packet, unit_def_short_packet;

  /* 
   * Special case for attacking/defending:
   * 
   * Normally the player doesn't get the information about the units inside a
   * city. However for attacking/defending the player has to know the unit of
   * the other side.  After the combat a remove_unit packet will be sent
   * to the client to tidy up.
   *
   * Note these packets must be sent out before unit_versus_unit is called,
   * so that the original unit stats (HP) will be sent.
   */
  package_short_unit(pattacker, &unit_att_short_packet, FALSE,
		     UNIT_INFO_IDENTITY, 0);
  package_short_unit(pdefender, &unit_def_short_packet, FALSE,
		     UNIT_INFO_IDENTITY, 0);
  players_iterate(other_player) {
    if (map_is_known_and_seen(pattacker->tile, other_player)
	|| map_is_known_and_seen(pdefender->tile, other_player)) {
      if (!can_player_see_unit(other_player, pattacker)) {
	assert(other_player->player_no != pattacker->owner);
	lsend_packet_unit_short_info(&other_player->connections,
				     &unit_att_short_packet);
      }

      if (!can_player_see_unit(other_player, pdefender)) {
	assert(other_player->player_no != pdefender->owner);
	lsend_packet_unit_short_info(&other_player->connections,
				     &unit_def_short_packet);
      }
    }
  } players_iterate_end;
}

/**************************************************************************
 Send combat info to players.
**************************************************************************/
static void send_combat(struct unit *pattacker, struct unit *pdefender, 
			int veteran, int bombard)
{
  struct packet_unit_combat_info combat;

  combat.attacker_unit_id=pattacker->id;
  combat.defender_unit_id=pdefender->id;
  combat.attacker_hp=pattacker->hp;
  combat.defender_hp=pdefender->hp;
  combat.make_winner_veteran=veteran;

  players_iterate(other_player) {
    if (map_is_known_and_seen(pattacker->tile, other_player)
	|| map_is_known_and_seen(pdefender->tile, other_player)) {
      lsend_packet_unit_combat_info(&other_player->connections, &combat);

      /* 
       * Remove the client knowledge of the units.  This corresponds to the
       * send_packet_unit_short_info calls up above.
       */
      if (!can_player_see_unit(other_player, pattacker)) {
	unit_goes_out_of_sight(other_player, pattacker);
      }
      if (!can_player_see_unit(other_player, pdefender)) {
	unit_goes_out_of_sight(other_player, pdefender);
      }
    }
  } players_iterate_end;

  /* Send combat info to non-player observers as well.  They already know
   * about the unit so no unit_info is needed. */
  conn_list_iterate(game.game_connections, pconn) {
    if (!pconn->player && pconn->observer) {
      send_packet_unit_combat_info(pconn, &combat);
    }
  } conn_list_iterate_end;
}

/**************************************************************************
  This function assumes the bombard is legal. The calling function should
  have already made all necessary checks.
**************************************************************************/
static bool unit_bombard(struct unit *punit, struct tile *ptile)
{
  struct player *pplayer = unit_owner(punit);
  struct city *pcity = map_get_city(ptile);
  int old_unit_vet;

  freelog(LOG_DEBUG, "Start bombard: %s's %s to %d, %d.",
	  pplayer->name, unit_type(punit)->name, TILE_XY(ptile));

  unit_list_iterate_safe(ptile->units, pdefender) {

    /* Sanity checks */
    if (pplayers_non_attack(unit_owner(punit), unit_owner(pdefender))) {
      die("Trying to attack a unit with which you have peace "
	  "or cease-fire at %i, %i", TILE_XY(pdefender->tile));
    }
    if (pplayers_allied(unit_owner(punit), unit_owner(pdefender))
	&& !(unit_flag(punit, F_NUCLEAR) && punit == pdefender)) {
      die("Trying to attack a unit with which you have alliance at %i, %i",
	  TILE_XY(pdefender->tile));
    }

    if (!is_air_unit(pdefender)
	|| (pcity || map_has_special(ptile, S_AIRBASE))) {
      see_combat(punit, pdefender);

      unit_versus_unit(punit, pdefender, TRUE);

      send_combat(punit, pdefender, 0, 1);
  
      send_unit_info(NULL, pdefender);
    }

  } unit_list_iterate_safe_end;

  punit->moves_left = 0;
  
  if (pcity
      && pcity->size > 1
      && get_city_bonus(pcity, EFT_UNIT_NO_LOSE_POP) == 0
      && kills_citizen_after_attack(punit)) {
    city_reduce_size(pcity,1);
    city_refresh(pcity);
    send_city_info(NULL, pcity);
  }

  old_unit_vet = punit->veteran;
  maybe_make_veteran(punit);
  if (punit->veteran != old_unit_vet) {
    notify_player_ex(unit_owner(punit), punit->tile,
		     E_UNIT_WIN_ATT,
		     _("Game: Your bombarding %s%s became more experienced!"),
		     unit_name(punit->type),
		     get_location_str_at(unit_owner(punit),
		     punit->tile));
  }

  send_unit_info(NULL, punit);
  return TRUE;
}

/**************************************************************************
This function assumes the attack is legal. The calling function should have
already made all neccesary checks.
**************************************************************************/
static void handle_unit_attack_request(struct unit *punit, struct unit *pdefender)
{
  struct player *pplayer = unit_owner(punit);
  struct unit *plooser, *pwinner;
  struct city *pcity;
  int moves_used, def_moves_used; 
  struct tile *def_tile = pdefender->tile;
  int old_unit_vet, old_defender_vet, vet;

  freelog(LOG_DEBUG, "Start attack: %s's %s against %s's %s.",
	  pplayer->name, unit_type(punit)->name, 
	  unit_owner(pdefender)->name,
	  unit_type(pdefender)->name);

  /* Sanity checks */
  if (pplayers_non_attack(unit_owner(punit), unit_owner(pdefender))) {
    die("Trying to attack a unit with which you have peace "
	"or cease-fire at %i, %i", TILE_XY(def_tile));
  }
  if (pplayers_allied(unit_owner(punit), unit_owner(pdefender))
      && !(unit_flag(punit, F_NUCLEAR) && punit == pdefender)) {
    die("Trying to attack a unit with which you have alliance at %i, %i",
	TILE_XY(def_tile));
  }

  if (unit_flag(punit, F_NUCLEAR)) {
    if ((pcity = sdi_defense_close(unit_owner(punit), def_tile))) {
      notify_player_ex(pplayer, punit->tile, E_UNIT_LOST_ATT,
		       _("Game: Your Nuclear missile was shot down by"
			 " SDI defences, what a waste."));
      notify_player_ex(city_owner(pcity), def_tile, E_UNIT_WIN,
		       _("Game: The nuclear attack on %s was avoided by"
			 " your SDI defense."), pcity->name);
      wipe_unit(punit);
      return;
    } 

    dlsend_packet_nuke_tile_info(&game.game_connections, def_tile->x,
				 def_tile->y);

    wipe_unit(punit);
    do_nuclear_explosion(pplayer, def_tile);
    return;
  }
  moves_used = unit_move_rate(punit) - punit->moves_left;
  def_moves_used = unit_move_rate(pdefender) - pdefender->moves_left;

  see_combat(punit, pdefender);

  old_unit_vet = punit->veteran;
  old_defender_vet = pdefender->veteran;
  unit_versus_unit(punit, pdefender, FALSE);

  /* Adjust attackers moves_left _after_ unit_versus_unit() so that
   * the movement attack modifier is correct! --dwp
   *
   * For greater Civ2 compatibility (and game balance issues), we recompute 
   * the new total MP based on the HP the unit has left after being damaged, 
   * and subtract the MPs that had been used before the combat (plus the 
   * points used in the attack itself, for the attacker). -GJW, Glip
   */
  punit->moves_left = unit_move_rate(punit) - moves_used - SINGLE_MOVE;
  pdefender->moves_left = unit_move_rate(pdefender) - def_moves_used;
  
  if (punit->moves_left < 0) {
    punit->moves_left = 0;
  }
  if (pdefender->moves_left < 0) {
    pdefender->moves_left = 0;
  }

  if (punit->hp > 0
      && (pcity = map_get_city(def_tile))
      && pcity->size > 1
      && get_city_bonus(pcity, EFT_UNIT_NO_LOSE_POP) == 0
      && kills_citizen_after_attack(punit)) {
    city_reduce_size(pcity,1);
    city_refresh(pcity);
    send_city_info(NULL, pcity);
  }
  if (unit_flag(punit, F_ONEATTACK)) 
    punit->moves_left = 0;
  pwinner = (punit->hp > 0) ? punit : pdefender;
  plooser = (pdefender->hp > 0) ? punit : pdefender;

  vet = (pwinner->veteran == ((punit->hp > 0) ? old_unit_vet :
	old_defender_vet)) ? 0 : 1;

  send_combat(punit, pdefender, vet, 0);
  
  if (punit == plooser) {
    /* The attacker lost */
    freelog(LOG_DEBUG, "Attacker lost: %s's %s against %s's %s.",
	    pplayer->name, unit_type(punit)->name,
	    unit_owner(pdefender)->name, unit_type(pdefender)->name);

    if (vet) {
      notify_player_ex(unit_owner(pwinner),
		       pwinner->tile, E_UNIT_WIN,
		       _("Game: Your %s%s survived the pathetic attack"
		         " from %s's %s and became more experienced!"),
		       unit_name(pwinner->type),
		       get_location_str_in(unit_owner(pwinner),
					   pwinner->tile),
		       unit_owner(plooser)->name, unit_name(plooser->type));
    } else {
      notify_player_ex(unit_owner(pwinner),
		       pwinner->tile, E_UNIT_WIN,
		       _("Game: Your %s%s survived the pathetic attack"
		         " from %s's %s."),
		       unit_name(pwinner->type),
		       get_location_str_in(unit_owner(pwinner),
					   pwinner->tile),
		       unit_owner(plooser)->name, unit_name(plooser->type));
    }
    
    notify_player_ex(unit_owner(plooser),
		     def_tile, E_UNIT_LOST_ATT,
		     _("Game: Your attacking %s failed "
		       "against %s's %s%s!"),
		     unit_name(plooser->type), unit_owner(pwinner)->name,
		     unit_name(pwinner->type),
		     get_location_str_at(unit_owner(plooser),
					 pwinner->tile));
    wipe_unit(plooser);
  } else {
    /* The defender lost, the attacker punit lives! */
    freelog(LOG_DEBUG, "Defender lost: %s's %s against %s's %s.",
	    pplayer->name, unit_type(punit)->name,
	    unit_owner(pdefender)->name, unit_type(pdefender)->name);

    punit->moved = TRUE;	/* We moved */
    if (vet && !unit_flag(punit, F_MISSILE)) {
      notify_player_ex(unit_owner(pwinner), punit->tile,
		       E_UNIT_WIN_ATT,
		       _("Game: Your attacking %s succeeded"
		         " against %s's %s%s and became more experienced!"),
		       unit_name(pwinner->type),
		       unit_owner(plooser)->name, unit_name(plooser->type),
		       get_location_str_at(unit_owner(pwinner),
		       plooser->tile));
    } else {
      notify_player_ex(unit_owner(pwinner), punit->tile,
		       E_UNIT_WIN_ATT,
		       _("Game: Your attacking %s succeeded"
		         " against %s's %s%s!"),
		       unit_name(pwinner->type),
		       unit_owner(plooser)->name, unit_name(plooser->type),
		       get_location_str_at(unit_owner(pwinner),
		       plooser->tile));
    }
    kill_unit(pwinner, plooser);
               /* no longer pplayer - want better msgs -- Syela */
  }
  if (pwinner == punit && unit_flag(punit, F_MISSILE)) {
    wipe_unit(pwinner);
    return;
  }

  /* If attacker wins, and occupychance > 0, it might move in.  Don't move in
   * if there are enemy units in the tile (a fortress, city or air base with
   * multiple defenders and unstacked combat). Note that this could mean 
   * capturing (or destroying) a city. */

  if (pwinner == punit && myrand(100) < game.occupychance &&
      !is_non_allied_unit_tile(def_tile,
			       unit_owner(punit))) {

    /* Hack: make sure the unit has enough moves_left for the move to succeed,
       and adjust moves_left to afterward (if successful). */

    int old_moves = punit->moves_left;
    int full_moves = unit_move_rate(punit);
    punit->moves_left = full_moves;
    if (handle_unit_move_request(punit, def_tile, FALSE, FALSE)) {
      punit->moves_left = old_moves - (full_moves - punit->moves_left);
      if (punit->moves_left < 0) {
	punit->moves_left = 0;
      }
    } else {
      punit->moves_left = old_moves;
    }
  }

  send_unit_info(NULL, pwinner);
}

/**************************************************************************
...
**************************************************************************/
static void how_to_declare_war(struct player *pplayer)
{
  notify_player_ex(pplayer, NULL, E_NOEVENT,
		   _("Game: Cancel treaty in the players dialog first (F3)."));
}

/**************************************************************************
...
**************************************************************************/
static bool can_unit_move_to_tile_with_notify(struct unit *punit,
					      struct tile *dest_tile,
					      bool igzoc)
{
  enum unit_move_result reason;
  struct tile *src_tile = punit->tile;

  reason =
      test_unit_move_to_tile(punit->type, unit_owner(punit),
			     punit->activity,
			     punit->tile, dest_tile, igzoc);
  if (reason == MR_OK)
    return TRUE;

  if (reason == MR_BAD_TYPE_FOR_CITY_TAKE_OVER) {
    const char *units_str = get_units_with_flag_string(F_MARINES);
    if (units_str) {
      notify_player_ex(unit_owner(punit), src_tile,
		       E_NOEVENT, _("Game: Only %s can attack from sea."),
		       units_str);
      free((void *) units_str);
    } else {
      notify_player_ex(unit_owner(punit), src_tile,
		       E_NOEVENT, _("Game: Cannot attack from sea."));
    }
  } else if (reason == MR_NO_WAR) {
    notify_player_ex(unit_owner(punit), src_tile,
		     E_NOEVENT,
		     _("Game: Cannot attack unless you declare war first."));
  } else if (reason == MR_ZOC) {
    notify_player_ex(unit_owner(punit), src_tile, E_NOEVENT,
		     _("Game: %s can only move into your own zone of control."),
		     unit_type(punit)->name);
  }
  return FALSE;
}

/**************************************************************************
  Will try to move to/attack the tile dest_x,dest_y.  Returns true if this
  could be done, false if it couldn't for some reason.
  
  'igzoc' means ignore ZOC rules - not necessary for igzoc units etc, but
  done in some special cases (moving barbarians out of initial hut).
  Should normally be FALSE.

  'move_diplomat_city' is another special case which should normally be
  FALSE.  If TRUE, try to move diplomat (or spy) into city (should be
  allied) instead of telling client to popup diplomat/spy dialog.

  FIXME: This function needs a good cleaning.
**************************************************************************/
bool handle_unit_move_request(struct unit *punit, struct tile *pdesttile,
                              bool igzoc, bool move_diplomat_city)
{
  struct player *pplayer = unit_owner(punit);
  struct city *pcity = pdesttile->city;

  /*** Phase 1: Basic checks ***/

  /* this occurs often during lag, and to the AI due to some quirks -- Syela */
  if (!is_tiles_adjacent(punit->tile, pdesttile)) {
    freelog(LOG_DEBUG, "tiles not adjacent in move request");
    return FALSE;
  }


  if (punit->moves_left<=0)  {
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
                     _("Game: This unit has no moves left."));
    return FALSE;
  }

  /*** Phase 2: Special abilities checks ***/

  /* Caravans.  If city is allied (inc. ours) we would have a popup
   * asking if we are moving on. */
  if (unit_flag(punit, F_TRADE_ROUTE) && pcity
      && !pplayers_allied(city_owner(pcity), pplayer) ) {
    return base_handle_unit_establish_trade(pplayer, punit->id, pcity);
  }

  /* Diplomats. Pop up a diplomat action dialog in the client.  
   * If the AI has used a goto to send a diplomat to a target do not 
   * pop up a dialog in the client.  
   * For allied cities, keep moving if move_diplomat_city tells us to, 
   * or if the unit is on goto and the city is not the final destination. */
  if (is_diplomat_unit(punit)) {
    struct unit *target = is_non_allied_unit_tile(pdesttile, pplayer);

    if (target || is_non_allied_city_tile(pdesttile, pplayer)
        || !move_diplomat_city) {
      if (is_diplomat_action_available(punit, DIPLOMAT_ANY_ACTION,
				       pdesttile)) {
	int target_id = 0;
        
        if (pplayer->ai.control) {
          return FALSE;
        }
        
        /* If we didn't send_unit_info the client would sometimes
         * think that the diplomat didn't have any moves left and so
         * don't pop up the box.  (We are in the middle of the unit
         * restore cycle when doing goto's, and the unit's movepoints
         * have been restored, but we only send the unit info at the
         * end of the function.) */
        send_unit_info(pplayer, punit);
        
        /* if is_diplomat_action_available() then there must be 
         * a city or a unit */
        if (pcity) {
          target_id = pcity->id;
        } else if (target) {
          target_id = target->id;
        } else {
          die("Bug in unithand.c: no diplomat target.");
        }
	dlsend_packet_unit_diplomat_popup_dialog(player_reply_dest(pplayer),
						 punit->id, target_id);
        return FALSE;
      } else if (!can_unit_move_to_tile(punit, pdesttile, igzoc)) {
        notify_player_ex(pplayer, punit->tile, E_NOEVENT,
                         is_ocean(map_get_terrain(punit->tile))
                         ? _("Game: Unit must be on land to "
                             "perform diplomatic action.")
                         : _("Game: No diplomat action possible."));
        return FALSE;
      }
    }
  }

  /*** Phase 3: Is it attack? ***/

  if (is_non_allied_unit_tile(pdesttile, pplayer) 
      || is_non_allied_city_tile(pdesttile, pplayer)) {
    struct unit *victim;

    /* We can attack ONLY in enemy cities */
    if (pcity && !pplayers_at_war(city_owner(pcity), pplayer)) {
      notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		       _("Game: Can't attack %s "
			 "because you are not at war with %s."),
		       pcity->name,
		       city_owner(pcity)->name);
      how_to_declare_war(pplayer);
      return FALSE;
    }

    /* Tile must contain ONLY enemy units. */
    if ((victim = is_non_attack_unit_tile(pdesttile, pplayer))) {
      notify_player_ex(pplayer, punit->tile, E_NOEVENT,
                       _("Game: Can't attack %s's unit "
			 "because you are not at war with %s."),
                       unit_owner(victim)->name,
                       unit_owner(victim)->name);
      how_to_declare_war(pplayer);
      return FALSE;
    }

    /* Are we a bombarder? */
    if (unit_flag(punit, F_BOMBARDER)) {
      /* Only land can be bombarded, if the target is on ocean, fall
       * through to attack. */
      if (!is_ocean(map_get_terrain(pdesttile))) {
	if (can_unit_bombard(punit)) {
	  unit_bombard(punit, pdesttile);
	  return TRUE;
	} else {
	  notify_player_ex(pplayer, punit->tile, E_NOEVENT,
			   _("Game: This unit is being transported, and"
			     " so cannot bombard."));
	  return FALSE;
	}
      }
    }


    /* The attack is legal wrt the alliances */
    victim = get_defender(punit, pdesttile);

    if (victim) {
      /* Must be physically able to attack EVERY unit there */
      if (!can_unit_attack_all_at_tile(punit, pdesttile)) {
        notify_player_ex(pplayer, punit->tile, E_NOEVENT,
                         _("Game: You can't attack there."));
        return FALSE;
      }
      
      handle_unit_attack_request(punit, victim);
      return TRUE;
    } else {
      assert(is_enemy_city_tile(pdesttile, pplayer) != NULL);

      if (unit_flag(punit, F_NUCLEAR)) {
        if (move_unit(punit, pcity->tile, 0)) {
          /* Survived dangers of moving */
          handle_unit_attack_request(punit, punit); /* Boom! */
        }
        return TRUE;
      }

      /* If there is an enemy city it is empty.
       * If not it would have been caught in the attack case. 
       * FIXME: Move this check into test_unit_move_tile */
      if (!COULD_OCCUPY(punit)) {
        notify_player_ex(pplayer, punit->tile, E_NOEVENT,
                         _("Game: This type of troops cannot "
                           "take over a city."));
        return FALSE;
      }
      /* Taking over a city is considered a move, so fall through */
    }
  } 

  /*** Phase 4: OK now move the unit ***/

  /* We cannot move a transport into a tile that holds
   * units or cities not allied with all of our cargo. */
  if (get_transporter_capacity(punit) > 0) {
    unit_list_iterate(punit->tile->units, pcargo) {
      if (pcargo->transported_by == punit->id
          && (is_non_allied_unit_tile(pdesttile, unit_owner(pcargo))
              || is_non_allied_city_tile(pdesttile, unit_owner(pcargo)))) {
         notify_player_ex(pplayer, punit->tile, E_NOEVENT,
                          _("Game: A transported unit is not allied to all "
                            "units or city on target tile."));
         return FALSE;
      }
    } unit_list_iterate_end;
  }


  if (can_unit_move_to_tile_with_notify(punit, pdesttile, igzoc)
      && try_move_unit(punit, pdesttile)) {
    int move_cost = map_move_cost(punit, pdesttile);

    (void) move_unit(punit, pdesttile, move_cost);

    return TRUE;
  } else {
    return FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_help_build_wonder(struct player *pplayer, int unit_id)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  struct city *pcity_dest;
  const char *text;

  if (!punit || !unit_flag(punit, F_HELP_WONDER)) {
    return;
  }
  pcity_dest = map_get_city(punit->tile);
  
  if (!pcity_dest || !unit_can_help_build_wonder(punit, pcity_dest)) {
    return;
  }

  pcity_dest->shield_stock += unit_build_shield_cost(punit->type);
  pcity_dest->caravan_shields += unit_build_shield_cost(punit->type);

  conn_list_do_buffer(&pplayer->connections);

  if (build_points_left(pcity_dest) >= 0) {
    text = _("Game: Your %s helps build the %s in %s (%d remaining).");
  } else {
    text = _("Game: Your %s helps build the %s in %s (%d surplus).");
  }
  notify_player_ex(pplayer, pcity_dest->tile, E_NOEVENT,
		   text, /* Must match arguments below. */
		   unit_name(punit->type),
		   get_improvement_type(pcity_dest->currently_building)->name,
		   pcity_dest->name, 
		   abs(build_points_left(pcity_dest)));

  wipe_unit(punit);
  send_player_info(pplayer, pplayer);
  send_city_info(pplayer, pcity_dest);
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
static bool base_handle_unit_establish_trade(struct player *pplayer, int unit_id, struct city *pcity_dest)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  struct city *pcity_out_of_home = NULL, *pcity_out_of_dest = NULL;
  struct city *pcity_homecity; 
  int revenue, i;
  bool home_full = FALSE, dest_full = FALSE, can_establish;
  
  if (!punit || !unit_flag(punit, F_TRADE_ROUTE)) {
    return FALSE;
  }

  /* if no destination city is passed in,
   *  check whether the unit is already in the city */
  if (!pcity_dest) { 
    pcity_dest = map_get_city(punit->tile);
  }

  if (!pcity_dest) {
    return FALSE;
  }

  pcity_homecity = player_find_city_by_id(pplayer, punit->homecity);

  if (!pcity_homecity) {
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: Sorry, your %s cannot establish"
		       " a trade route because it has no home city"),
		     unit_name(punit->type));
    return FALSE;
   
  }

    
  if (!can_cities_trade(pcity_homecity, pcity_dest)) {
    notify_player_ex(pplayer, pcity_dest->tile, E_NOEVENT,
		     _("Game: Sorry, your %s cannot establish"
		       " a trade route between %s and %s"),
		     unit_name(punit->type),pcity_homecity->name,
		     pcity_dest->name);
    return FALSE;
  }
  
  /* This part of code works like can_establish_trade_route, except
   * that we actually do the action of making the trade route. */

  /* If we can't make a new trade route we can still get the trade bonus. */
  can_establish = !have_cities_trade_route(pcity_homecity, pcity_dest);
    
  if (can_establish) {
    home_full = (city_num_trade_routes(pcity_homecity) == NUM_TRADEROUTES);
    dest_full = (city_num_trade_routes(pcity_dest) == NUM_TRADEROUTES);
  }
  
  if (home_full || dest_full) {
    int slot, trade = trade_between_cities(pcity_homecity, pcity_dest);

    /* See if there's a trade route we can cancel at the home city. */
    if (home_full) {
      if (get_city_min_trade_route(pcity_homecity, &slot) < trade) {
	pcity_out_of_home = find_city_by_id(pcity_homecity->trade[slot]);
	assert(pcity_out_of_home != NULL);
      } else {
	notify_player_ex(pplayer, pcity_dest->tile, E_NOEVENT,
		     _("Game: Sorry, your %s cannot establish"
		       " a trade route here!"), unit_name(punit->type));
        notify_player_ex(pplayer, pcity_dest->tile, E_NOEVENT,
		       _("      The city of %s already has %d "
			 "better trade routes!"), pcity_homecity->name,
		       NUM_TRADEROUTES);
	can_establish = FALSE;
      }
    }
    
    /* See if there's a trade route we can cancel at the dest city. */
    if (can_establish && dest_full) {
      if (get_city_min_trade_route(pcity_dest, &slot) < trade) {
	pcity_out_of_dest = find_city_by_id(pcity_dest->trade[slot]);
	assert(pcity_out_of_dest != NULL);
      } else {
	notify_player_ex(pplayer, pcity_dest->tile, E_NOEVENT,
		     _("Game: Sorry, your %s cannot establish"
		       " a trade route here!"), unit_name(punit->type));
        notify_player_ex(pplayer, pcity_dest->tile, E_NOEVENT,
		       _("      The city of %s already has %d "
			 "better trade routes!"), pcity_dest->name,
		       NUM_TRADEROUTES);
	can_establish = FALSE;
      }
    }

    /* Now cancel the trade route from the home city. */
    if (can_establish && pcity_out_of_home) {
      remove_trade_route(pcity_homecity, pcity_out_of_home);
      notify_player_ex(city_owner(pcity_out_of_home),
		       pcity_out_of_home->tile, E_NOEVENT,
		       _("Game: Sorry, %s has canceled the trade route "
			 "from %s to your city %s."),
		       city_owner(pcity_homecity)->name,
		       pcity_homecity->name, pcity_out_of_home->name);
    }

    /* And the same for the dest city. */
    if (can_establish && pcity_out_of_dest) {
      remove_trade_route(pcity_dest, pcity_out_of_dest);
      notify_player_ex(city_owner(pcity_out_of_dest),
		       pcity_out_of_dest->tile, E_NOEVENT,
		       _("Game: Sorry, %s has canceled the trade route "
			 "from %s to your city %s."),
		       city_owner(pcity_dest)->name,
		       pcity_dest->name, pcity_out_of_dest->name);
    }
  }
  
  revenue = get_caravan_enter_city_trade_bonus(pcity_homecity, pcity_dest);
  if (can_establish) {
    /* establish traderoute */
    for (i = 0; i < NUM_TRADEROUTES; i++) {
      if (pcity_homecity->trade[i] == 0) {
        pcity_homecity->trade[i] = pcity_dest->id;
        break;
      }
    }
    assert(i < NUM_TRADEROUTES);
  
    for (i = 0; i < NUM_TRADEROUTES; i++) {
      if (pcity_dest->trade[i] == 0) {
        pcity_dest->trade[i] = pcity_homecity->id;
        break;
      }
    }
    assert(i < NUM_TRADEROUTES);
  } else {
    /* enter marketplace */
    revenue = (revenue + 2) / 3;
  }
  
  conn_list_do_buffer(&pplayer->connections);
  notify_player_ex(pplayer, pcity_dest->tile, E_NOEVENT,
		   _("Game: Your %s from %s has arrived in %s,"
		     " and revenues amount to %d in gold and research."), 
		   unit_name(punit->type), pcity_homecity->name,
		   pcity_dest->name, revenue);
  wipe_unit(punit);
  pplayer->economic.gold += revenue;
  update_tech(pplayer, revenue);
  
  if (can_establish) {
    /* Refresh the cities. */
    city_refresh(pcity_homecity);
    city_refresh(pcity_dest);
    if (pcity_out_of_home) {
      city_refresh(pcity_out_of_home);
    }
    if (pcity_out_of_dest) {
      city_refresh(pcity_out_of_dest);
    }
  
    /* Notify the owners of the cities. */
    send_city_info(pplayer, pcity_homecity);
    send_city_info(city_owner(pcity_dest), pcity_dest);
    if(pcity_out_of_home) {
      send_city_info(city_owner(pcity_out_of_home), pcity_out_of_home);
    }
    if(pcity_out_of_dest) {
      send_city_info(city_owner(pcity_out_of_dest), pcity_out_of_dest);
    }

    /* Notify each player about the other cities so that they know about
     * the tile_trade value. */
    if (pplayer != city_owner(pcity_dest)) {
      send_city_info(city_owner(pcity_dest), pcity_homecity);
      send_city_info(pplayer, pcity_dest);
    }

    if (pcity_out_of_home) {
      if (city_owner(pcity_dest) != city_owner(pcity_out_of_home)) {
        send_city_info(city_owner(pcity_dest), pcity_out_of_home);
	 send_city_info(city_owner(pcity_out_of_home), pcity_dest);
      }
      if (pplayer != city_owner(pcity_out_of_home)) {
        send_city_info(pplayer, pcity_out_of_home);
	 send_city_info(city_owner(pcity_out_of_home), pcity_homecity);
      }
      if (pcity_out_of_dest && city_owner(pcity_out_of_home) !=
					city_owner(pcity_out_of_dest)) {
	 send_city_info(city_owner(pcity_out_of_home), pcity_out_of_dest);
      }
    }

    if (pcity_out_of_dest) {
      if (city_owner(pcity_dest) != city_owner(pcity_out_of_dest)) {
        send_city_info(city_owner(pcity_dest), pcity_out_of_dest);
	 send_city_info(city_owner(pcity_out_of_dest), pcity_dest);
      }
      if (pplayer != city_owner(pcity_out_of_dest)) {
	 send_city_info(pplayer, pcity_out_of_dest);
	 send_city_info(city_owner(pcity_out_of_dest), pcity_homecity);
      }
      if (pcity_out_of_home && city_owner(pcity_out_of_home) !=
					city_owner(pcity_out_of_dest)) {
	 send_city_info(city_owner(pcity_out_of_dest), pcity_out_of_home);
      }
    }
  }
  
  send_player_info(pplayer, pplayer);
  conn_list_do_unbuffer(&pplayer->connections);
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_establish_trade(struct player *pplayer, int unit_id)
{
  (void) base_handle_unit_establish_trade(pplayer, unit_id, NULL);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_auto(struct player *pplayer, int unit_id)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);

  if (!punit || !can_unit_do_auto(punit))
    return;

  punit->ai.control = TRUE;
  send_unit_info(pplayer, punit);
}

/**************************************************************************
...
**************************************************************************/
static void handle_unit_activity_dependencies(struct unit *punit,
				enum unit_activity old_activity,
				enum tile_special_type old_target)
{
  switch (punit->activity) {
  case ACTIVITY_IDLE:
    switch (old_activity) {
    case ACTIVITY_PILLAGE: 
      {
        enum tile_special_type prereq =
	  map_get_infrastructure_prerequisite(old_target);
        if (prereq != S_NO_SPECIAL) {
          unit_list_iterate (punit->tile->units, punit2)
            if ((punit2->activity == ACTIVITY_PILLAGE) &&
                (punit2->activity_target == prereq)) {
              set_unit_activity(punit2, ACTIVITY_IDLE);
              send_unit_info(NULL, punit2);
            }
          unit_list_iterate_end;
        }
        break;
      }
    case ACTIVITY_EXPLORE:
      /* Restore unit's control status */
      punit->ai.control = FALSE;
      break;
    default: 
      ; /* do nothing */
    }
    break;
  case ACTIVITY_EXPLORE:
    punit->ai.control = TRUE;
    set_unit_activity(punit, ACTIVITY_EXPLORE);
    send_unit_info(NULL, punit);
    break;
  default:
    /* do nothing */
    break;
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_activity_request(struct unit *punit, 
				  enum unit_activity new_activity)
{
  if (can_unit_do_activity(punit, new_activity)) {
    enum unit_activity old_activity = punit->activity;
    enum tile_special_type old_target = punit->activity_target;

    free_unit_orders(punit);
    set_unit_activity(punit, new_activity);
    send_unit_info(NULL, punit);
    handle_unit_activity_dependencies(punit, old_activity, old_target);
  }
}

/**************************************************************************
...
**************************************************************************/
static void handle_unit_activity_request_targeted(struct unit *punit,
						  enum unit_activity
						  new_activity,
						  enum tile_special_type
						  new_target)
{
  if (can_unit_do_activity_targeted(punit, new_activity, new_target)) {
    enum unit_activity old_activity = punit->activity;
    enum tile_special_type old_target = punit->activity_target;

    free_unit_orders(punit);
    set_unit_activity_targeted(punit, new_activity, new_target);
    send_unit_info(NULL, punit);    
    handle_unit_activity_dependencies(punit, old_activity, old_target);
  }
}

/****************************************************************************
  Handle a client request to load the given unit into the given transporter.
****************************************************************************/
void handle_unit_load(struct player *pplayer, int cargo_id, int trans_id)
{
  /* A player may only load their units, but they may be loaded into
   * other players transporters (depending on the rules in
   * can_unit_load). */
  struct unit *pcargo = player_find_unit_by_id(pplayer, cargo_id);
  struct unit *ptrans = find_unit_by_id(trans_id);

  if (!pcargo || !ptrans) {
    return;
  }

  if (!can_unit_load(pcargo, ptrans)) {
    return;
  }

  /* Load the unit and send out info to clients. */
  load_unit_onto_transporter(pcargo, ptrans);
}

/****************************************************************************
  Handle a client request to unload the given unit from the given
  transporter.
****************************************************************************/
void handle_unit_unload(struct player *pplayer, int cargo_id, int trans_id)
{
  struct unit *pcargo = find_unit_by_id(cargo_id);
  struct unit *ptrans = find_unit_by_id(trans_id);

  if (!pcargo || !ptrans) {
    return;
  }

  /* You are allowed to unload a unit if it is yours or if the transporter
   * is yours. */
  if (pcargo->owner != pplayer->player_no
      && ptrans->owner != pplayer->player_no) {
    return;
  }

  if (!can_unit_unload(pcargo, ptrans)) {
    return;
  }

  if (!can_unit_survive_at_tile(pcargo, pcargo->tile)) {
    return;
  }

  /* Unload the unit and send out info to clients. */
  unload_unit_from_transporter(pcargo);
}

/**************************************************************************
Explode nuclear at a tile without enemy units
**************************************************************************/
void handle_unit_nuke(struct player *pplayer, int unit_id)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);

  if (!punit) {
    return;
  }
  handle_unit_attack_request(punit, punit);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_paradrop_to(struct player *pplayer, int unit_id, int x,
			     int y)
{
  struct unit *punit = player_find_unit_by_id(pplayer, unit_id);
  struct tile *ptile = map_pos_to_tile(x, y);
  
  if (!punit || !ptile) {
    return;
  }

  (void) do_paradrop(punit, ptile);
}

/**************************************************************************
Receives route packages.
**************************************************************************/
void handle_unit_orders(struct player *pplayer,
			struct packet_unit_orders *packet)
{
  struct unit *punit = player_find_unit_by_id(pplayer, packet->unit_id);
  int i;

  if (!punit || packet->length < 0 || punit->activity != ACTIVITY_IDLE
      || packet->length > MAX_LEN_ROUTE) {
    return;
  }


  for (i = 0; i < packet->length; i++) {
    switch (packet->orders[i]) {
    case ORDER_MOVE:
      if (!is_valid_dir(packet->dir[i])) {
	return;
      }
      break;
    case ORDER_ACTIVITY:
      switch (packet->activity[i]) {
      case ACTIVITY_POLLUTION:
      case ACTIVITY_ROAD:
      case ACTIVITY_MINE:
      case ACTIVITY_IRRIGATE:
      case ACTIVITY_FORTRESS:
      case ACTIVITY_RAILROAD:
      case ACTIVITY_TRANSFORM:
      case ACTIVITY_AIRBASE:
	/* Simple activities. */
	break;
      case ACTIVITY_SENTRY:
	if (i != packet->length - 1) {
	  /* Only allowed as the last order. */
	  return;
	}
	break;
      default:
	return;
      }
      break;
    case ORDER_FULL_MP:
      break;
    default:
      /* An invalid order.  This is handled in execute_orders. */
      packet->orders[i] = ORDER_LAST;
      break;
    }
  }

  free_unit_orders(punit);

  if (packet->length == 0) {
    assert(!unit_has_orders(punit));
    send_unit_info(NULL, punit);
    return;
  }

  if (punit->ai.ai_role != AIUNIT_NONE) {
    ai_unit_new_role(punit, AIUNIT_NONE, NULL);
  }

  punit->has_orders = TRUE;
  punit->orders.length = packet->length;
  punit->orders.index = 0;
  punit->orders.repeat = packet->repeat;
  punit->orders.vigilant = packet->vigilant;
  punit->orders.list
    = fc_malloc(packet->length * sizeof(*(punit->orders.list)));
  for (i = 0; i < packet->length; i++) {
    punit->orders.list[i].order = packet->orders[i];
    punit->orders.list[i].dir = packet->dir[i];
    punit->orders.list[i].activity = packet->activity[i];
  }

  if (!packet->repeat) {
    if (is_normal_map_pos(packet->dest_x, packet->dest_y)) {
      punit->goto_tile = map_pos_to_tile(packet->dest_x, packet->dest_y);
    }
  }

#ifdef DEBUG
  freelog(LOG_DEBUG, "Orders for unit %d: length:%d",
	  packet->unit_id, packet->length);
  for (i = 0; i < packet->length; i++) {
    freelog(LOG_DEBUG, "  %d,%s", packet->orders[i],
	    dir_get_name(packet->dir[i]));
  }
#endif

  if (execute_orders(punit)) {
    /* Looks like the unit survived. */
    send_unit_info(NULL, punit);
  }
}
