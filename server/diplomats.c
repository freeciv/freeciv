/***********************************************************************
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
#include <fc_config.h>
#endif

#include <stdio.h>

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "rand.h"

/* common */
#include "base.h"
#include "combat.h"
#include "events.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "player.h"
#include "research.h"
#include "unitlist.h"

/* server */
#include "actiontools.h"
#include "aiiface.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplhand.h"
#include "diplomats.h"
#include "maphand.h"
#include "notify.h"
#include "plrhand.h"
#include "techtools.h"
#include "unithand.h"
#include "unittools.h"

/* server/scripting */
#include "script_server.h"

/****************************************************************************/

static void diplomat_charge_movement (struct unit *pdiplomat,
                                      struct tile *ptile);
static bool diplomat_success_vs_defender(struct unit *patt, struct unit *pdef,
                                         struct tile *pdefender_tile,
                                         int *att_vet, int *def_vet);
static bool diplomat_may_lose_gold(struct player *dec_player,
                                   struct player *inc_player,
                                   int revolt_gold);
static bool diplomat_infiltrate_tile(struct player *pplayer,
                                     struct player *cplayer,
                                     const struct action *paction,
                                     struct unit *pdiplomat,
                                     struct unit *pvictim,
                                     struct tile *ptile,
                                     struct player **defender_owner);
static void diplomat_escape(struct player *pplayer, struct unit *pdiplomat,
                            const struct city *pcity,
                            const struct action *paction);
static void diplomat_escape_full(struct player *pplayer,
                                 struct unit *pdiplomat,
                                 bool city_related,
                                 struct tile *ptile,
                                 const char *vlink,
                                 const struct action *paction);

/************************************************************************//**
  Poison a city's water supply.

  - Check for infiltration success.  Our poisoner may not survive this.
  - Check for basic success.  Again, our poisonner may not survive this.
  - If successful, reduces population by one point.

  - The poisoner may be captured and executed, or escape to its home town.

  'pplayer' is the player who tries to poison 'pcity' with its unit
  'pdiplomat'.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool spy_poison(struct player *pplayer, struct unit *pdiplomat,
                struct city *pcity, const struct action *paction)
{
  const struct unit_type *act_utype;
  struct player *cplayer;
  struct tile *ctile;
  const char *clink;

  /* Fetch target city's player.  Sanity checks. */
  fc_assert_ret_val(pcity, FALSE);
  cplayer = city_owner(pcity);
  fc_assert_ret_val(cplayer, FALSE);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(pdiplomat, FALSE);

  act_utype = unit_type_get(pdiplomat);

  ctile = city_tile(pcity);
  clink = city_link(pcity);

  log_debug("poison: unit: %d", pdiplomat->id);

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_tile(pplayer, cplayer, paction,
                                pdiplomat, NULL, ctile, NULL)) {
    return FALSE;
  }

  log_debug("poison: infiltrated");

  /* The Spy may get caught while trying to poison the city. */
  if (action_failed_dice_roll(pplayer, pdiplomat, pcity, cplayer,
                              paction)) {
    notify_player(pplayer, ctile, E_MY_DIPLOMAT_FAILED, ftc_server,
                  /* TRANS: unit, action */
                  _("Your %s was caught attempting to do %s!"),
                  unit_tile_link(pdiplomat),
                  action_name_translation(paction));
    notify_player(cplayer, ctile, E_ENEMY_DIPLOMAT_FAILED,
                  ftc_server,
                  /* TRANS: nation, unit, action, city */
                  _("You caught %s %s attempting to do %s in %s!"),
                  nation_adjective_for_player(pplayer),
                  unit_tile_link(pdiplomat),
                  action_name_translation(paction),
                  clink);

    /* This may cause a diplomatic incident */
    action_consequence_caught(paction, pplayer, act_utype,
                              cplayer, ctile, clink);

    /* Execute the caught Spy. */
    wipe_unit(pdiplomat, ULR_CAUGHT, cplayer);

    return FALSE;
  }

  log_debug("poison: succeeded");

  /* Poison people! */
  if (city_reduce_size(pcity, 1, pplayer, "poison")) {
    /* Notify everybody involved. */
    notify_player(pplayer, ctile, E_MY_DIPLOMAT_POISON, ftc_server,
                  _("Your %s poisoned the water supply of %s."),
                  unit_link(pdiplomat), clink);
    notify_player(cplayer, ctile,
                  E_ENEMY_DIPLOMAT_POISON, ftc_server,
                  _("%s is suspected of poisoning the water supply of %s."),
                  player_name(pplayer), clink);

    if (game.info.poison_empties_food_stock) {
      /* The food was poisoned too. */
      city_empty_food_stock(pcity);
    }

    /* Update clients. */
    city_refresh (pcity);
    send_city_info(NULL, pcity);
  } else {
    /* Notify everybody involved. */
    notify_player(pplayer, ctile, E_MY_DIPLOMAT_POISON, ftc_server,
                  _("Your %s destroyed %s by poisoning its water supply."),
                  unit_link(pdiplomat), clink);
    notify_player(cplayer, ctile,
                  E_ENEMY_DIPLOMAT_POISON, ftc_server,
                  _("%s is suspected of destroying %s by poisoning its"
                    " water supply."),
                  player_name(pplayer), clink);
  }

  /* this may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype,
                             cplayer, ctile, clink);

  /* Now lets see if the spy survives. */
  diplomat_escape_full(pplayer, pdiplomat, TRUE, ctile, clink, paction);

  return TRUE;
}

/************************************************************************//**
  Spread a plague to the target city.

  - Check for infiltration success.  Our infector may not survive this.
  - Check for basic success.  Again, our infector may not survive this.
  - If successful, strikes the city with illness.

  - The infector may be captured and executed, or escape to a nearby city.

  'act_player' is the player who tries to infect 'tgt_city' with its unit
  'act_unit'.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool spy_spread_plague(struct player *act_player, struct unit *act_unit,
                       struct city *tgt_city, const struct action *paction)
{
  const struct unit_type *act_utype;
  struct player *tgt_player;
  struct tile *tgt_tile;

  const char *tgt_city_link;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);
  act_utype = unit_type_get(act_unit);

  /* Sanity check: The target city still exists. */
  fc_assert_ret_val(tgt_city, FALSE);

  /* Who to infect. */
  tgt_player = city_owner(tgt_city);

  /* Sanity check: The target player still exists. */
  fc_assert_ret_val(tgt_player, FALSE);

  tgt_tile = city_tile(tgt_city);
  tgt_city_link = city_link(tgt_city);

  log_debug("spread plague: unit: %d", act_unit->id);

  /* Battle all units capable of diplomatic defense. */
  if (!diplomat_infiltrate_tile(act_player, tgt_player,
                                paction,
                                act_unit, NULL, tgt_tile, NULL)) {
    return FALSE;
  }

  log_debug("spread plague: infiltrated");

  /* The infector may get caught while trying to spread a plague in the
   * city. */
  if (action_failed_dice_roll(act_player, act_unit, tgt_city, tgt_player,
                              paction)) {
    notify_player(act_player, tgt_tile, E_MY_DIPLOMAT_FAILED, ftc_server,
                  /* TRANS: unit, action */
                  _("Your %s was caught attempting to do %s!"),
                  unit_tile_link(act_unit),
                  action_name_translation(paction));
    notify_player(tgt_player, tgt_tile, E_ENEMY_DIPLOMAT_FAILED,
                  ftc_server,
                  /* TRANS: nation, unit, action, city */
                  _("You caught %s %s attempting to do %s in %s!"),
                  nation_adjective_for_player(act_player),
                  unit_tile_link(act_unit),
                  action_name_translation(paction),
                  tgt_city_link);

    /* This may cause a diplomatic incident */
    action_consequence_caught(paction, act_player, act_utype,
                              tgt_player, tgt_tile, tgt_city_link);

    /* Execute the caught infector. */
    wipe_unit(act_unit, ULR_CAUGHT, tgt_player);

    return FALSE;
  }

  log_debug("spread plague: succeeded");

  /* Commit bio-terrorism. */
  if (city_illness_strike(tgt_city)) {
    /* Update the clients. */
    city_refresh(tgt_city);
    send_city_info(NULL, tgt_city);
  }

  /* Notify everyone involved. */
  notify_player(act_player, tgt_tile, E_UNIT_ACTION_ACTOR_SUCCESS,
                ftc_server,
                /* TRANS: unit, action, city */
                _("Your %s did %s to %s."),
                unit_link(act_unit), action_name_translation(paction),
                tgt_city_link);
  notify_player(tgt_player, tgt_tile, E_UNIT_ACTION_TARGET_HOSTILE,
                ftc_server,
                /* TRANS: action, city, nation */
                _("%s done to %s, %s suspected."),
                action_name_translation(paction), tgt_city_link,
                nation_plural_for_player(act_player));

  /* This may cause a diplomatic incident. */
  action_consequence_success(paction, act_player, act_utype,
                             tgt_player, tgt_tile, tgt_city_link);

  /* Try to escape. */
  diplomat_escape_full(act_player, act_unit, TRUE,
                       tgt_tile, tgt_city_link, paction);

  return TRUE;
}

/************************************************************************//**
  Investigate a city.

  - It costs some minimal movement to investigate a city.

  - The actor unit always survives the investigation unless the action
    being performed is configured to always consume the actor unit.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool diplomat_investigate(struct player *pplayer, struct unit *pdiplomat,
                          struct city *pcity,
                          const struct action *paction)
{
  struct player *cplayer;
  struct packet_unit_short_info unit_packet;
  struct packet_city_info city_packet;
  struct packet_city_nationalities nat_packet;
  struct packet_city_rally_point rally_packet;
  struct packet_web_city_info_addition web_packet;
  struct trade_route_packet_list *routes;
  const struct unit_type *act_utype;
  struct packet_web_city_info_addition *webp_ptr;

  /* Fetch target city's player.  Sanity checks. */
  fc_assert_ret_val(pcity, FALSE);
  cplayer = city_owner(pcity);
  fc_assert_ret_val(cplayer, FALSE);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(pdiplomat, FALSE);

  act_utype = unit_type_get(pdiplomat);

  /* Sanity check: The target is foreign. */
  if (cplayer == pplayer) {
    /* Nothing to do to a domestic target. */
    return FALSE;
  }

  log_debug("investigate: unit: %d", pdiplomat->id);

  dlsend_packet_investigate_started(pplayer->connections, pcity->id);

  /* Do It... */
  update_dumb_city(pplayer, pcity);
  /* Special case for a diplomat/spy investigating a city:
     The investigator needs to know the supported and present
     units of a city, whether or not they are fogged. So, we
     send a list of them all before sending the city info.
     As this is a special case we bypass send_unit_info. */
  unit_list_iterate(pcity->units_supported, punit) {
    package_short_unit(punit, &unit_packet,
                       UNIT_INFO_CITY_SUPPORTED, pcity->id);
    /* We need to force to send the packet to ensure the client will receive
     * something (e.g. investigating twice). */
    lsend_packet_unit_short_info(pplayer->connections, &unit_packet, TRUE);
  } unit_list_iterate_end;
  unit_list_iterate((pcity->tile)->units, punit) {
    package_short_unit(punit, &unit_packet,
                       UNIT_INFO_CITY_PRESENT, pcity->id);
    /* We need to force to send the packet to ensure the client will receive
     * something (e.g. investigating twice). */
    lsend_packet_unit_short_info(pplayer->connections, &unit_packet, TRUE);
  } unit_list_iterate_end;
  /* Send city info to investigator's player.
     As this is a special case we bypass send_city_info(). */

  if (any_web_conns()) {
    webp_ptr = &web_packet;
  } else {
    webp_ptr = NULL;
  }

  routes = trade_route_packet_list_new();
  package_city(pcity, &city_packet, &nat_packet, &rally_packet,
               webp_ptr, routes, TRUE);
  /* We need to force to send the packet to ensure the client will receive
   * something and popup the city dialog. */
  city_packet.original = city_original_owner(pcity, pplayer);
  lsend_packet_city_info(pplayer->connections, &city_packet, TRUE);
  lsend_packet_city_nationalities(pplayer->connections, &nat_packet, TRUE);
  lsend_packet_city_rally_point(pplayer->connections, &rally_packet, TRUE);
  web_lsend_packet(city_info_addition, pplayer->connections, webp_ptr, TRUE);
  trade_route_packet_list_iterate(routes, route_packet) {
    lsend_packet_trade_route_info(pplayer->connections, route_packet);
    FC_FREE(route_packet);
  } trade_route_packet_list_iterate_end;
  trade_route_packet_list_destroy(routes);

  /* This may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype, cplayer,
                             city_tile(pcity), city_link(pcity));

  /* The actor unit always survive unless the action it self has determined
   * to always consume it. */
  if (!utype_is_consumed_by_action(paction, unit_type_get(pdiplomat))) {
    /* This unit isn't about to be consumed. Send updated unit information
     * to the clients. */
    send_unit_info(NULL, pdiplomat);
  }

  dlsend_packet_investigate_finished(pplayer->connections, pcity->id);

  return TRUE;
}

/************************************************************************//**
  Get list of improvements from city (for purposes of sabotage).

  - Always successful; returns list.

  Only send back to the originating connection, if there is one. (?)
****************************************************************************/
void spy_send_sabotage_list(struct connection *pc, struct unit *pdiplomat,
                            struct city *pcity,
                            const struct action *paction,
                            int request_kind)
{
  struct packet_city_sabotage_list packet;

  /* Send city improvements info to player. */
  BV_CLR_ALL(packet.improvements);

  if (action_has_result(paction, ACTRES_SPY_TARGETED_SABOTAGE_CITY)) {
    /* Can see hidden buildings. */
    improvement_iterate(ptarget) {
      if (city_has_building(pcity, ptarget)) {
        BV_SET(packet.improvements, improvement_index(ptarget));
      }
    } improvement_iterate_end;
  } else {
    /* Can't see hidden buildings. */
    struct vision_site *plrcity;

    plrcity = map_get_player_city(city_tile(pcity), unit_owner(pdiplomat));

    if (plrcity == nullptr) {
      /* Must know city to remember visible buildings. */
      return;
    }

    improvement_iterate(ptarget) {
      if (BV_ISSET(plrcity->improvements, improvement_index(ptarget))
          && !improvement_has_flag(ptarget, IF_INDESTRUCTIBLE)) {
        BV_SET(packet.improvements, improvement_index(ptarget));
      }
    } improvement_iterate_end;
  }

  packet.actor_id = pdiplomat->id;
  packet.city_id = pcity->id;
  packet.act_id = paction->id;
  packet.request_kind = request_kind;
  send_packet_city_sabotage_list(pc, &packet);
}

/************************************************************************//**
  Establish an embassy.

  - Always success; the embassy is created.
  - It costs some minimal movement to establish an embassy.

  - The actor unit always survives establishing the embassy unless the action
    being performed is configured to always consume the actor unit.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool diplomat_embassy(struct player *pplayer, struct unit *pdiplomat,
                      struct city *pcity, const struct action *paction)
{
  struct player *cplayer;
  const struct unit_type *act_utype;

  /* Fetch target city's player.  Sanity checks. */
  fc_assert_ret_val(pcity, FALSE);
  cplayer = city_owner(pcity);
  fc_assert_ret_val(cplayer, FALSE);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(pdiplomat, FALSE);

  act_utype = unit_type_get(pdiplomat);

  /* Sanity check: The target is foreign. */
  if (cplayer == pplayer) {
    /* Nothing to do to a domestic target. */
    return FALSE;
  }

  log_debug("embassy: unit: %d", pdiplomat->id);

  log_debug("embassy: succeeded");

  establish_embassy(pplayer, cplayer);

  /* Notify everybody involved. */
  notify_player(pplayer, city_tile(pcity),
                E_MY_DIPLOMAT_EMBASSY, ftc_server,
                _("You have established an embassy in %s."),
                city_link(pcity));
  notify_player(cplayer, city_tile(pcity),
                E_ENEMY_DIPLOMAT_EMBASSY, ftc_server,
                _("The %s have established an embassy in %s."),
                nation_plural_for_player(pplayer),
                city_link(pcity));

  /* this may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype, cplayer,
                             city_tile(pcity), city_link(pcity));

  /* The actor unit always survive unless the action it self has determined
   * to always consume it. */
  if (!utype_is_consumed_by_action(paction, unit_type_get(pdiplomat))) {
    /* This unit isn't about to be consumed. Send updated unit information
     * to the clients. */
    send_unit_info(NULL, pdiplomat);
  }

  return TRUE;
}

/************************************************************************//**
  Sabotage an enemy unit.

  - If successful, reduces hit points by half of those remaining.

  - The saboteur may be captured and executed, or escape to its home town.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool spy_sabotage_unit(struct player *pplayer, struct unit *pdiplomat,
                       struct unit *pvictim,
                       const struct action *paction)
{
  char victim_link[MAX_LEN_LINK];
  struct player *uplayer;
  const struct unit_type *act_utype;

  /* Fetch target unit's player.  Sanity checks. */
  fc_assert_ret_val(pvictim, FALSE);
  uplayer = unit_owner(pvictim);
  fc_assert_ret_val(uplayer, FALSE);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(pdiplomat, FALSE);

  act_utype = unit_type_get(pdiplomat);

  log_debug("sabotage-unit: unit: %d", pdiplomat->id);

  /* N.B: unit_link() always returns the same pointer. */
  sz_strlcpy(victim_link, unit_link(pvictim));

  /* Diplomatic battle against any diplomatic defender except the intended
   * victim of the sabotage. */
  if (!diplomat_infiltrate_tile(pplayer, uplayer,
                                paction,
                                pdiplomat, pvictim,
                                unit_tile(pvictim),
                                NULL)) {
    return FALSE;
  }

  log_debug("sabotage-unit: succeeded");

  if (pvictim->hp < 2) {
    /* Not possible to halve the hit points. Kill it. */
    wipe_unit(pvictim, ULR_KILLED, pplayer);

    /* Notify everybody involved. */
    notify_player(pplayer, unit_tile(pvictim),
                  E_MY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("Your %s's successful sabotage killed the %s %s."),
                  unit_link(pdiplomat),
                  nation_adjective_for_player(uplayer),
                  victim_link);
    notify_player(uplayer, unit_tile(pvictim),
                  E_ENEMY_DIPLOMAT_SABOTAGE, ftc_server,
                  /* TRANS: ... the Poles! */
                  _("Your %s was killed by %s sabotage!"),
                  victim_link,
                  nation_plural_for_player(pplayer));
  } else {
    /* Sabotage the unit by removing half its remaining hit points. */
    pvictim->hp /= 2;
    send_unit_info(NULL, pvictim);

    /* Notify everybody involved. */
    notify_player(pplayer, unit_tile(pvictim),
                  E_MY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("Your %s succeeded in sabotaging the %s %s."),
                  unit_link(pdiplomat),
                  nation_adjective_for_player(uplayer),
                  victim_link);
    notify_player(uplayer, unit_tile(pvictim),
                  E_ENEMY_DIPLOMAT_SABOTAGE, ftc_server,
                  /* TRANS: ... the Poles! */
                  _("Your %s was sabotaged by the %s!"),
                  victim_link,
                  nation_plural_for_player(pplayer));
  }

  /* this may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype, uplayer,
                             unit_tile(pvictim), victim_link);

  /* Now lets see if the spy survives. */
  diplomat_escape(pplayer, pdiplomat, NULL, paction);

  return TRUE;
}

/************************************************************************//**
  Bribe an enemy unit.

  - Can't bribe a unit if:
    - Player doesn't have enough gold.
  - Otherwise, the unit will be bribed.

  - A successful briber will try to move onto the victim's square.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool diplomat_bribe_unit(struct player *pplayer, struct unit *pdiplomat,
                         struct unit *pvictim, const struct action *paction)
{
  char victim_link[MAX_LEN_LINK];
  struct player *uplayer;
  struct tile *victim_tile;
  int bribe_cost;
  int diplomat_id;
  const struct unit_type *act_utype, *victim_type;
  struct city *pcity;
  bool bounce;

  /* Fetch target unit's player.  Sanity checks. */
  fc_assert_ret_val(pvictim, FALSE);
  uplayer = unit_owner(pvictim);
  victim_type = unit_type_get(pvictim);
  fc_assert_ret_val(uplayer, FALSE);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(pdiplomat, FALSE);
  diplomat_id = pdiplomat->id;

  act_utype = unit_type_get(pdiplomat);

  /* Sanity check: The target is foreign. */
  if (uplayer == pplayer) {
    /* Nothing to do to a domestic target. */
    return FALSE;
  }

  /* Sanity check: The victim isn't a unique unit the actor player already
   * has. */
  fc_assert_ret_val_msg(!utype_player_already_has_this_unique(pplayer,
                             unit_type_get(pvictim)),
                        FALSE,
                        "bribe-unit: already got unique unit");

  log_debug("bribe-unit: unit: %d", pdiplomat->id);

  /* Get bribe cost, ignoring any previously saved value. */
  bribe_cost = unit_bribe_cost(pvictim, pplayer, pdiplomat);

  /* If player doesn't have enough gold, can't bribe. */
  if (pplayer->economic.gold < bribe_cost) {
    notify_player(pplayer, unit_tile(pdiplomat),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("You don't have enough gold to bribe the %s %s."),
                  nation_adjective_for_player(uplayer),
                  unit_link(pvictim));
    log_debug("bribe-unit: not enough gold");
    return FALSE;
  }

  log_debug("bribe-unit: enough gold");

  /* Diplomatic battle against any diplomatic defender except the one that
   * will get the bribe. */
  if (!diplomat_infiltrate_tile(pplayer, uplayer,
                                paction,
                                pdiplomat, pvictim,
                                pvictim->tile,
                                NULL)) {
    return FALSE;
  }

  log_debug("bribe-unit: succeeded");

  victim_tile = unit_tile(pvictim);
  pvictim = unit_change_owner(pvictim, pplayer, pdiplomat->homecity,
                              ULR_BRIBED);

  if (pvictim) {
    /* N.B.: unit_link always returns the same pointer. As unit_change_owner()
     * currently remove the old unit and replace by a new one (with a new id),
     * we want to make link to the new unit. */
    sz_strlcpy(victim_link, unit_link(pvictim));
  } else {
    sz_strlcpy(victim_link, utype_name_translation(victim_type));
  }

  if (!unit_is_alive(diplomat_id)) {
    /* Destroyed by a script */
    pdiplomat = NULL;
  }

  /* Notify everybody involved. */
  notify_player(pplayer, victim_tile, E_MY_DIPLOMAT_BRIBE, ftc_server,
                /* TRANS: <diplomat> ... <unit> */
                _("Your %s succeeded in bribing the %s."),
                pdiplomat ? unit_link(pdiplomat)
                : utype_name_translation(act_utype), victim_link);
  if (pdiplomat && maybe_make_veteran(pdiplomat, 100)) {
    notify_unit_experience(pdiplomat);
  }
  notify_player(uplayer, victim_tile, E_ENEMY_DIPLOMAT_BRIBE, ftc_server,
                /* TRANS: <unit> ... <Poles> */
                _("Your %s was bribed by the %s."),
                victim_link, nation_plural_for_player(pplayer));

  if (pvictim) {
    /* The unit may have been on a tile shared with a city or a unit
     * it no longer can share a tile with. */
    pcity = tile_city(unit_tile(pvictim));
    bounce = ((NULL != pcity
               && !pplayers_allied(city_owner(pcity), unit_owner(pvictim)))
              /* Keep the old behavior (insto check is_non_allied_unit_tile) */
              || 1 < unit_list_size(unit_tile(pvictim)->units));
    if (bounce) {
      bounce_unit(pvictim, TRUE);
    }
  } else {
    bounce = FALSE;
  }

  /* This costs! */
  pplayer->economic.gold -= bribe_cost;
  if (pplayer->economic.gold < 0) {
    /* Scripts have deprived us of too much gold before we paid */
    log_normal("%s has bribed %s but has not %d gold at payment time, "
               "%d is the discount", player_name(pplayer),
               utype_rule_name(victim_type), bribe_cost,
               -pplayer->economic.gold);
    pplayer->economic.gold = 0;
  }

  /* This may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype, uplayer,
                             victim_tile, victim_link);

  if (!pdiplomat || !unit_is_alive(diplomat_id)) {
    return TRUE;
  }

  /* Try to move the briber onto the victim's square unless the victim has
   * been bounced because it couldn't share tile with a unit or city. */
  if (!bounce
      /* Try to perform post move forced actions. */
      && (NULL == action_auto_perf_unit_do(AAPC_POST_ACTION, pdiplomat,
                                           uplayer, NULL, paction,
                                           victim_tile, tile_city(victim_tile),
                                           pvictim, NULL))
      /* May have died while trying to do forced actions. */
      && unit_is_alive(diplomat_id)) {
    pdiplomat->moves_left = 0;
  }
  if (NULL != player_unit_by_number(pplayer, diplomat_id)) {
    send_unit_info(NULL, pdiplomat);
  }

  /* Update clients. */
  send_player_all_c(pplayer, NULL);

  return TRUE;
}

/************************************************************************//**
  Bribe an enemy unit stack.

  - Can't bribe a unit if:
    - Player doesn't have enough gold.
  - Otherwise, the unit will be bribed.

  - A successful briber will try to move onto the victim's square.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool diplomat_bribe_stack(struct player *pplayer, struct unit *pdiplomat,
                          struct tile *pvictim, const struct action *paction)
{
  int bribe_cost = 0;
  int bribe_count = 0;
  struct city *pcity;
  bool bounce = FALSE;
  int diplomat_id = pdiplomat->id;
  const struct unit_type *act_utype;

  unit_list_iterate(pvictim->units, pbribed) {
    struct player *owner = unit_owner(pbribed);

    if (!pplayers_at_war(pplayer, owner)) {
      notify_player(pplayer, unit_tile(pdiplomat),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("You are not in war with all the units in the stack."));
      return FALSE;
    }
  } unit_list_iterate_end;

  bribe_cost = stack_bribe_cost(pvictim, pplayer, pdiplomat);

  /* If player doesn't have enough gold, can't bribe. */
  if (pplayer->economic.gold < bribe_cost) {
    notify_player(pplayer, unit_tile(pdiplomat),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("You don't have enough gold to bribe the unit stack."));
    log_debug("bribe-stack: not enough gold");
    return FALSE;
  }

  pcity = tile_city(pvictim);
  if (pcity != NULL && !pplayers_allied(city_owner(pcity), pplayer)) {
    bounce = TRUE;
  }

  unit_list_iterate_safe(pvictim->units, pbribed) {
    struct player *owner = unit_owner(pbribed);
    struct unit *nunit = unit_change_owner(pbribed, pplayer,
                                           pdiplomat->homecity, ULR_BRIBED);

    notify_player(owner, pvictim, E_ENEMY_DIPLOMAT_BRIBE, ftc_server,
                  /* TRANS: <unit> ... <Poles> */
                  _("Your %s was bribed by the %s."),
                  unit_link(nunit), nation_plural_for_player(pplayer));
    bribe_count++;

    if (bounce) {
      bounce_unit(pbribed, TRUE);
    }
  } unit_list_iterate_safe_end;

  if (!unit_is_alive(diplomat_id)) {
    /* Destroyed by a script */
    pdiplomat = NULL;
  }

  act_utype = unit_type_get(pdiplomat);

  /* Notify everybody involved. */
  notify_player(pplayer, pvictim, E_MY_DIPLOMAT_BRIBE, ftc_server,
                /* TRANS: <diplomat> ... */
                _("Your %s succeeded in bribing %d units."),
                pdiplomat ? unit_link(pdiplomat)
                : utype_name_translation(act_utype), bribe_count);
  if (pdiplomat && maybe_make_veteran(pdiplomat, 100)) {
    notify_unit_experience(pdiplomat);
  }

  /* This costs! */
  pplayer->economic.gold -= bribe_cost;
  if (pplayer->economic.gold < 0) {
    /* Scripts have deprived us of too much gold before we paid */
    log_normal("%s has bribed %d units but has not %d gold at payment time, "
               "%d is the discount", player_name(pplayer),
               bribe_count, bribe_cost,
               -pplayer->economic.gold);
    pplayer->economic.gold = 0;
  }

  if (!pdiplomat || !unit_is_alive(diplomat_id)) {
    return TRUE;
  }

  /* Try to move the briber onto the victim's square unless the victim has
   * been bounced because it couldn't share tile with a unit or city. */
  if (!bounce
      /* Try to perform post move forced actions. */
      && (NULL == action_auto_perf_unit_do(AAPC_POST_ACTION, pdiplomat,
                                           NULL, NULL, paction,
                                           pvictim, pcity,
                                           NULL, NULL))
      /* May have died while trying to do forced actions. */
      && unit_is_alive(diplomat_id)) {
    pdiplomat->moves_left = 0;
  }
  if (NULL != player_unit_by_number(pplayer, diplomat_id)) {
    send_unit_info(NULL, pdiplomat);
  }

  /* Update clients. */
  send_player_all_c(pplayer, NULL);

  return TRUE;
}

/************************************************************************//**
  Diplomatic battle.

  - Check for infiltration success. The entire point of this action.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool spy_attack(struct player *act_player, struct unit *act_unit,
                struct tile *tgt_tile, const struct action *paction)
{
  int act_unit_id;
  struct player *tgt_player = NULL;
  const struct unit_type *act_utype;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);

  act_utype = unit_type_get(act_unit);
  act_unit_id = act_unit->id;

  /* Do the diplomatic battle against a diplomatic defender. */
  diplomat_infiltrate_tile(act_player, NULL,
                           paction,
                           act_unit, NULL, tgt_tile,
                           &tgt_player);

  /* Sanity check: the defender had an owner. */
  fc_assert_ret_val(tgt_player != NULL, TRUE);

  if (!unit_is_alive(act_unit_id)) {
    /* action_consequence_caught() is handled in
     * diplomat_infiltrate_tile() */

    /* The action was to start a diplomatic battle. */
    return TRUE;
  }

  /* This may cause a diplomatic incident. */
  action_consequence_success(paction, act_player, act_utype,
                             tgt_player, tgt_tile, tile_link(tgt_tile));

  /* The action was to start a diplomatic battle. */
  return TRUE;
}

/************************************************************************//**
  Returns the amount of tech thefts from a city not ignored by the
  EFT_STEALINGS_IGNORE effect.
****************************************************************************/
int diplomats_unignored_tech_stealings(struct unit *pdiplomat,
                                       struct city *pcity)
{
  int times;
  int bonus = get_unit_bonus(pdiplomat, EFT_STEALINGS_IGNORE);

  if (bonus < 0) {
    /* Negative effect value means infinite bonus */
    times = 0;
  } else {
    times = pcity->steal - bonus;
    if (times < 0) {
      times = 0;
    }
  }

  return times;
}

/************************************************************************//**
  Try to steal a technology from an enemy city.
  If paction results in ACTION_SPY_STEAL_TECH or ACTION_SPY_STEAL_TECH_ESC,
  steal a random technology.
  Otherwise, steal the technology whose ID is "technology".
  (Note: Only Spies can select what to steal.)

  - Check for infiltration success.  Our thief may not survive this.
  - Check for basic success.  Again, our thief may not survive this.
  - If a technology has already been stolen from this city at any time:
    - Diplomats will be caught and executed.
    - Spies will have an increasing chance of being caught and executed.
  - Steal technology!

  - The thief may be captured and executed, or escape to its home town.

  FIXME: It should give a loss of reputation to steal from a player you are
  not at war with

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool diplomat_get_tech(struct player *pplayer, struct unit *pdiplomat,
                       struct city  *pcity, Tech_type_id technology,
                       const struct action *paction)
{
  struct research *presearch, *cresearch;
  struct player *cplayer;
  const struct unit_type *act_utype;
  int count;
  int times;
  Tech_type_id tech_stolen;
  bool expected_kills;

  /* We have to check arguments. They are sent to us by a client,
     so we cannot trust them */
  fc_assert_ret_val(pcity, FALSE);
  cplayer = city_owner(pcity);
  fc_assert_ret_val(cplayer, FALSE);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(pdiplomat, FALSE);

  act_utype = unit_type_get(pdiplomat);

  /* Sanity check: The target is foreign. */
  if (cplayer == pplayer) {
    /* Nothing to do to a domestic target. */
    return FALSE;
  }

  /* Currently based on if unit is consumed or not. */
  expected_kills = utype_is_consumed_by_action(paction,
                                               unit_type_get(pdiplomat));

  if (action_has_result(paction, ACTRES_SPY_STEAL_TECH)) {
    /* Can't choose target. Will steal a random tech. */
    technology = A_UNSET;
  }

  /* Targeted technology should be a ruleset defined tech,
   * "At Spy's Discretion" (A_UNSET) or a future tech (A_FUTURE). */
  if (technology == A_NONE
      || (technology != A_FUTURE
          && !(technology == A_UNSET
               && action_has_result(paction, ACTRES_SPY_STEAL_TECH))
          && !valid_advance_by_number(technology))) {
    return FALSE;
  }

  presearch = research_get(pplayer);
  cresearch = research_get(cplayer);

  if (technology == A_FUTURE) {
    if (presearch->future_tech >= cresearch->future_tech) {
      return FALSE;
    }
  } else if (technology != A_UNSET) {
    if (research_invention_state(presearch, technology) == TECH_KNOWN) {
      return FALSE;
    }
    if (research_invention_state(cresearch, technology) != TECH_KNOWN) {
      return FALSE;
    }
    if (!research_invention_gettable(presearch, technology,
                                     game.info.tech_steal_allow_holes)) {
      return FALSE;
    }
  }

  log_debug("steal-tech: unit: %d", pdiplomat->id);

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_tile(pplayer, cplayer,
                                paction,
                                pdiplomat, NULL,
                                pcity->tile,
                                NULL)) {
    return FALSE;
  }

  log_debug("steal-tech: infiltrated");

  times = diplomats_unignored_tech_stealings(pdiplomat, pcity);

  /* Check if the Diplomat/Spy succeeds with their task. */
  /* (Twice as difficult if target is specified.) */
  /* (If already stolen from, impossible for Diplomats and harder for Spies.) */
  if (times > 0 && expected_kills) {
    /* Already stolen from: Diplomat always fails! */
    count = 1;
    log_debug("steal-tech: difficulty: impossible");
  } else {
    /* Determine difficulty. */
    count = 1;
    if (action_has_result(paction, ACTRES_SPY_TARGETED_STEAL_TECH)) {
      /* Targeted steal tech is more difficult. */
      count++;
    }
    count += times;
    log_debug("steal-tech: difficulty: %d", count);
    /* Determine success or failure. */
    while (count > 0) {
      if (action_failed_dice_roll(pplayer, pdiplomat, pcity, cplayer,
                                  paction)) {
        break;
      }
      count--;
    }
  }

  if (count > 0) {
    /* Failed to steal a tech. */
    if (times > 0 && expected_kills) {
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    /* TRANS: Paris was expecting ... Your Spy was caught */
                    _("%s was expecting your attempt to steal technology "
                      "again. Your %s was caught and executed."),
                    city_link(pcity),
                    unit_tile_link(pdiplomat));
      notify_player(cplayer, city_tile(pcity),
                    E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                    /* TRANS: The Belgian Spy ... from Paris */
                    _("The %s %s failed to steal technology again from %s. "
                      "We were prepared for the attempt."),
                    nation_adjective_for_player(pplayer),
                    unit_tile_link(pdiplomat),
                    city_link(pcity));
    } else {
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    /* TRANS: Your Spy was caught ... from %s. */
                    _("Your %s was caught in the attempt of"
                      " stealing technology from %s."),
                    unit_tile_link(pdiplomat),
                    city_link(pcity));
      notify_player(cplayer, city_tile(pcity),
                    E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                    /* TRANS: The Belgian Spy ... from Paris */
                    _("The %s %s failed to steal technology from %s."),
                    nation_adjective_for_player(pplayer),
                    unit_tile_link(pdiplomat),
                    city_link(pcity));
    }

    /* This may cause a diplomatic incident */
    action_consequence_caught(paction, pplayer, act_utype, cplayer,
                              city_tile(pcity), city_link(pcity));
    wipe_unit(pdiplomat, ULR_CAUGHT, cplayer);
    return FALSE;
  }

  tech_stolen = steal_a_tech(pplayer, cplayer, technology);

  if (tech_stolen == A_NONE) {
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("No new technology found in %s."),
                  city_link(pcity));
    diplomat_charge_movement (pdiplomat, pcity->tile);
    send_unit_info(NULL, pdiplomat);
    return FALSE;
  }

  /* Update stealing player's science progress and research fields */
  send_player_all_c(pplayer, NULL);

  /* Record the theft. */
  (pcity->steal)++;

  /* this may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype, cplayer,
                             city_tile(pcity), city_link(pcity));

  /* Check if a spy survives her mission. */
  diplomat_escape(pplayer, pdiplomat, pcity, paction);

  return TRUE;
}

/************************************************************************//**
  Gold for inciting might get lost.

  - If the provocateur is captured and executed, there is probability
    that they were carrying bag with some gold, which will be lost.
  - There is chance, that this gold will be transferred to nation
    which successfully defended against inciting revolt.

  Returns TRUE if money is lost, FALSE if not.
****************************************************************************/
bool diplomat_may_lose_gold(struct player *dec_player, struct player *inc_player,
                            int revolt_gold)
{
  if (game.server.incite_gold_loss_chance <= 0) {
    return FALSE;
  }

  /* Roll the dice. */
  if (fc_rand(100) < game.server.incite_gold_loss_chance) {
    notify_player(dec_player, NULL, E_MY_DIPLOMAT_FAILED, ftc_server,
                  PL_("Your %d gold prepared to incite the revolt was lost!",
                      "Your %d gold prepared to incite the revolt was lost!",
                      revolt_gold), revolt_gold);
    dec_player->economic.gold -= revolt_gold;
    /* Lost money was pocketed by fraudulent executioners?
     * Or returned to local authority?
     * Roll the dice twice. */
    if (fc_rand(100) < game.server.incite_gold_capt_chance) {
      inc_player->economic.gold += revolt_gold;
      notify_player(inc_player, NULL, E_ENEMY_DIPLOMAT_FAILED,
                    ftc_server,
                    PL_("Your security service captured %d gold prepared "
                        "to incite your town!",
                        "Your security service captured %d gold prepared "
                        "to incite your town!", revolt_gold), revolt_gold);
    }
    /* Update clients. */
    send_player_all_c(dec_player, NULL);
    send_player_all_c(inc_player, NULL);

    return TRUE;
  } else {
    return FALSE;
  }
}

/************************************************************************//**
  Incite a city to disaffect.

  - Can't incite a city to disaffect if:
    - Player doesn't have enough gold.
  - Check for infiltration success.  Our provocateur may not survive this.
  - Check for basic success.  Again, our provocateur may not survive this.
  - Otherwise, the city will disaffect:
    - Player gets the city.
    - Player gets certain of the city's present/supported units.
    - Player gets a technology advance, if any were unknown.

  - The provocateur may be captured and executed, or escape to its home
    town.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool diplomat_incite(struct player *pplayer, struct unit *pdiplomat,
                     struct city *pcity, const struct action *paction)
{
  struct player *cplayer;
  struct tile *ctile;
  const char *clink;
  int revolt_cost;
  const struct unit_type *act_utype;

  /* Fetch target civilization's player.  Sanity checks. */
  fc_assert_ret_val(pcity, FALSE);
  cplayer = city_owner(pcity);
  fc_assert_ret_val(cplayer, FALSE);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(pdiplomat, FALSE);

  /* Sanity check: The target is foreign. */
  if (cplayer == pplayer) {
    /* Nothing to do to a domestic target. */
    return FALSE;
  }

  act_utype = unit_type_get(pdiplomat);

  ctile = city_tile(pcity);
  clink = city_link(pcity);

  log_debug("incite: unit: %d", pdiplomat->id);

  /* Get incite cost, ignoring any previously saved value. */
  revolt_cost = city_incite_cost(pplayer, pcity);

  /* If player doesn't have enough gold, can't incite a revolt. */
  if (pplayer->economic.gold < revolt_cost) {
    notify_player(pplayer, ctile, E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("You don't have enough gold to subvert %s."),
                  clink);
    log_debug("incite: not enough gold");
    return FALSE;
  }

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_tile(pplayer, cplayer,
                                paction,
                                pdiplomat, NULL,
                                pcity->tile,
                                NULL)) {
    diplomat_may_lose_gold(pplayer, cplayer, revolt_cost / 2 );
    return FALSE;
  }

  log_debug("incite: infiltrated");

  /* Check if the Diplomat/Spy succeeds with their task. */
  if (action_failed_dice_roll(pplayer, pdiplomat, pcity, cplayer,
                              paction)) {
    notify_player(pplayer, ctile, E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s was caught in the attempt"
                    " of inciting a revolt!"),
                  unit_tile_link(pdiplomat));
    notify_player(cplayer, ctile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                  _("You caught %s %s attempting"
                    " to incite a revolt in %s!"),
                  nation_adjective_for_player(pplayer),
                  unit_tile_link(pdiplomat),
                  clink);

    diplomat_may_lose_gold(pplayer, cplayer, revolt_cost / 4);

    /* This may cause a diplomatic incident */
    action_consequence_caught(paction, pplayer, act_utype,
                              cplayer, ctile, clink);

    wipe_unit(pdiplomat, ULR_CAUGHT, cplayer);
    return FALSE;
  }

  log_debug("incite: succeeded");

  /* Subvert the city to your cause... */

  /* City loses some population. */
  if (city_size_get(pcity) > 1) {
    city_reduce_size(pcity, 1, pplayer, "incited");
  }

  /* This costs! */
  pplayer->economic.gold -= revolt_cost;

  /* Notify everybody involved. */
  notify_player(pplayer, ctile, E_MY_DIPLOMAT_INCITE, ftc_server,
                _("Revolt incited in %s, you now rule the city!"),
                clink);
  notify_player(cplayer, ctile, E_ENEMY_DIPLOMAT_INCITE, ftc_server,
                _("%s has revolted, %s influence suspected."),
                clink,
                nation_adjective_for_player(pplayer));

  pcity->shield_stock = 0;
  nullify_prechange_production(pcity);

  /* You get a technology advance, too! */
  steal_a_tech(pplayer, cplayer, A_UNSET);

  /* this may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype,
                             cplayer, ctile, clink);

  /* Transfer city and units supported by this city (that
     are within one square of the city) to the new owner. */
  if (transfer_city(pplayer, pcity, 1, TRUE, TRUE, FALSE,
                    !is_barbarian(pplayer))) {
    script_server_signal_emit("city_transferred", pcity, cplayer, pplayer,
                              "incited");
  }

  /* Check if a spy survives her mission.
   * _After_ transferring the city, or the city area is first fogged
   * when the diplomat is removed, and then unfogged when the city
   * is transferred. */
  diplomat_escape_full(pplayer, pdiplomat, TRUE, ctile, clink, paction);

  /* Update the players gold in the client */
  send_player_info_c(pplayer, pplayer->connections);

  return TRUE;
}

/************************************************************************//**
  Sabotage enemy city's improvement or production.
  If this is untargeted sabotage city a random improvement or production is
  targeted.
  Targeted sabotage city lets the value of "improvement" decide the target.
  If "improvement" is -1, sabotage current production.
  Otherwise, sabotage the city improvement whose ID is "improvement".

  - Check for infiltration success.  Our saboteur may not survive this.
  - Check for basic success.  Again, our saboteur may not survive this.
  - Determine target, given arguments and constraints.
  - If specified, city walls and anything in a capital are 50% likely to fail.
  - Do sabotage!

  - The saboteur may be captured and executed, or escape to its home town.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool diplomat_sabotage(struct player *pplayer, struct unit *pdiplomat,
                       struct city *pcity, Impr_type_id improvement,
                       const struct action *paction)
{
  struct player *cplayer;
  struct impr_type *ptarget;
  int count, which;
  const struct unit_type *act_utype;

  /* Fetch target city's player.  Sanity checks. */
  fc_assert_ret_val(pcity, FALSE);
  cplayer = city_owner(pcity);
  fc_assert_ret_val(cplayer, FALSE);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(pdiplomat, FALSE);

  act_utype = unit_type_get(pdiplomat);

  log_debug("sabotage: unit: %d", pdiplomat->id);

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_tile(pplayer, cplayer,
                                paction,
                                pdiplomat, NULL,
                                pcity->tile,
                                NULL)) {
    return FALSE;
  }

  log_debug("sabotage: infiltrated");

  /* Check if the Diplomat/Spy succeeds with their task. */
  if (action_failed_dice_roll(pplayer, pdiplomat, pcity, cplayer,
                              paction)) {
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s was caught in the attempt"
                    " of industrial sabotage!"),
                  unit_tile_link(pdiplomat));
    notify_player(cplayer, city_tile(pcity),
                  E_ENEMY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("You caught %s %s attempting sabotage in %s!"),
                  nation_adjective_for_player(pplayer),
                  unit_tile_link(pdiplomat),
                  city_link(pcity));

    /* This may cause a diplomatic incident */
    action_consequence_caught(paction, pplayer, act_utype, cplayer,
                              city_tile(pcity), city_link(pcity));

    wipe_unit(pdiplomat, ULR_CAUGHT, cplayer);
    return FALSE;
  }

  log_debug("sabotage: succeeded");

  /* Examine the city for improvements to sabotage. */
  count = 0;
  city_built_iterate(pcity, pimprove) {
    if (pimprove->sabotage > 0) {
      count++;
    }
  } city_built_iterate_end;

  log_debug("sabotage: count of improvements: %d", count);

  /* Determine the target (-1 is production). */
  if (action_has_result(paction, ACTRES_SPY_SABOTAGE_CITY)) {
    /*
     * Pick random:
     * 50/50 chance to pick production or some improvement.
     * Won't pick something that doesn't exit.
     * If nothing to do, say so, deduct movement cost and return.
     */
    if (count == 0 && pcity->shield_stock == 0) {
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s could not find anything to sabotage in %s."),
                    unit_link(pdiplomat),
                    city_link(pcity));
      diplomat_charge_movement(pdiplomat, pcity->tile);
      send_unit_info(NULL, pdiplomat);
      log_debug("sabotage: random: nothing to do");
      return FALSE;
    }
    if (count == 0 || fc_rand (2) == 1) {
      ptarget = NULL;
      log_debug("sabotage: random: targeted production");
    } else {
      ptarget = NULL;
      which = fc_rand (count);

      city_built_iterate(pcity, pimprove) {
	if (pimprove->sabotage > 0) {
	  if (which > 0) {
	    which--;
	  } else {
	    ptarget = pimprove;
	    break;
	  }
	}
      } city_built_iterate_end;

      if (NULL != ptarget) {
        log_debug("sabotage: random: targeted improvement: %d (%s)",
                  improvement_number(ptarget),
                  improvement_rule_name(ptarget));
      } else {
        log_error("sabotage: random: targeted improvement error!");
      }
    }
  } else if (improvement < 0) {
    /* If told to sabotage production, do so. */
    ptarget = NULL;
    log_debug("sabotage: specified target production");
  } else {
    struct impr_type *pimprove = improvement_by_number(improvement);
    if (pimprove == NULL) {
      log_error("sabotage: requested for invalid improvement %d", improvement);
      return FALSE;
    }
    /*
     * Told which improvement to pick:
     * If try for wonder or palace, complain, deduct movement cost and return.
     * If not available, say so, deduct movement cost and return.
     */
    if (city_has_building(pcity, pimprove)) {
      if (pimprove->sabotage > 0) {
	ptarget = pimprove;
        log_debug("sabotage: specified target improvement: %d (%s)",
                  improvement, improvement_rule_name(pimprove));
      } else {
        notify_player(pplayer, city_tile(pcity),
                      E_MY_DIPLOMAT_FAILED, ftc_server,
                      _("You cannot sabotage a %s!"),
                      improvement_name_translation(pimprove));
        diplomat_charge_movement(pdiplomat, pcity->tile);
        send_unit_info(NULL, pdiplomat);
        log_debug("sabotage: disallowed target improvement: %d (%s)",
                  improvement, improvement_rule_name(pimprove));
        return FALSE;
      }
    } else {
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s could not find the %s to sabotage in %s."),
                    unit_name_translation(pdiplomat),
                    improvement_name_translation(pimprove),
                    city_link(pcity));
      diplomat_charge_movement(pdiplomat, pcity->tile);
      send_unit_info(NULL, pdiplomat);
      log_debug("sabotage: target improvement not found: %d (%s)",
                improvement, improvement_rule_name(pimprove));
      return FALSE;
    }
  }

  /* Now, the fun stuff!  Do the sabotage! */
  if (NULL == ptarget) {
     char prod[256];

    /* Do it. */
    pcity->shield_stock = 0;
    nullify_prechange_production(pcity); /* Make it impossible to recover */

    /* Report it. */
    universal_name_translation(&pcity->production, prod, sizeof(prod));

    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("Your %s succeeded in destroying"
                    " the production of %s in %s."),
                  unit_link(pdiplomat),
                  prod,
                  city_name_get(pcity));
    notify_player(cplayer, city_tile(pcity),
                  E_ENEMY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("The production of %s was destroyed in %s,"
                    " %s are suspected."),
                  prod,
                  city_link(pcity),
                  nation_plural_for_player(pplayer));
    log_debug("sabotage: sabotaged production");
  } else {
    int vulnerability = ptarget->sabotage;

    /* Sabotage a city improvement. */

    /*
     * One last chance to get caught:
     * If target was specified, and it is in the capital or are
     * City Walls, then there is a 50% chance of getting caught.
     */
    vulnerability -= (vulnerability
                      * get_city_bonus(pcity, EFT_SABOTEUR_RESISTANT)
                      / 100);

    if (fc_rand(100) >= vulnerability) {
      /* Caught! */
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s was caught in the attempt of sabotage!"),
                    unit_tile_link(pdiplomat));
      notify_player(cplayer, city_tile(pcity),
                    E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                    _("You caught %s %s attempting"
                      " to sabotage the %s in %s!"),
                    nation_adjective_for_player(pplayer),
                    unit_tile_link(pdiplomat),
                    improvement_name_translation(ptarget),
                    city_link(pcity));

      /* This may cause a diplomatic incident */
      action_consequence_caught(paction, pplayer, act_utype, cplayer,
                                city_tile(pcity), city_link(pcity));

      wipe_unit(pdiplomat, ULR_CAUGHT, cplayer);
      log_debug("sabotage: caught in capital or on city walls");
      return FALSE;
    }

    /* Report it. */
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("Your %s destroyed the %s in %s."),
                  unit_link(pdiplomat),
                  improvement_name_translation(ptarget),
                  city_link(pcity));
    notify_player(cplayer, city_tile(pcity),
                  E_ENEMY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("The %s destroyed the %s in %s."),
                  nation_plural_for_player(pplayer),
                  improvement_name_translation(ptarget),
                  city_link(pcity));
    log_debug("sabotage: sabotaged improvement: %d (%s)",
              improvement_number(ptarget),
              improvement_rule_name(ptarget));

    /* Do it. */
    building_lost(pcity, ptarget, "sabotaged", pdiplomat);

    /* FIXME: Lua script might have destroyed the diplomat, the city, or even the player.
     *        Avoid dangling pointers below in that case. */
  }

  /* Update clients. */
  send_city_info(NULL, pcity);

  /* this may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype, cplayer,
                             city_tile(pcity), city_link(pcity));

  /* Check if a spy survives her mission. */
  diplomat_escape(pplayer, pdiplomat, pcity, paction);

  return TRUE;
}

/************************************************************************//**
  Steal gold from another player.
  The amount stolen is decided randomly.
  Not everything stolen reaches the player that ordered it stolen.

  - Check for infiltration success.  Our thief may not survive this.
  - Check for basic success.  Again, our thief may not survive this.
  - Can't steal if there is no money to take.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool spy_steal_gold(struct player *act_player, struct unit *act_unit,
                    struct city *tgt_city, const struct action *paction)
{
  struct player *tgt_player;
  struct tile *tgt_tile;

  const char *tgt_city_link;

  int gold_take;
  int gold_give;
  const struct unit_type *act_utype;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);

  act_utype = unit_type_get(act_unit);

  /* Sanity check: The target city still exists. */
  fc_assert_ret_val(tgt_city, FALSE);

  /* Who to steal from. */
  tgt_player = city_owner(tgt_city);

  /* Sanity check: The target player still exists. */
  fc_assert_ret_val(tgt_player, FALSE);

  /* Sanity check: The target is foreign. */
  if (tgt_player == act_player) {
    /* Nothing to do to a domestic target. */
    return FALSE;
  }

  /* Sanity check: There is something to steal. */
  if (tgt_player->economic.gold <= 0) {
    return FALSE;
  }

  tgt_tile = city_tile(tgt_city);
  tgt_city_link = city_link(tgt_city);

  log_debug("steal gold: unit: %d", act_unit->id);

  /* Battle all units capable of diplomatic defense. */
  if (!diplomat_infiltrate_tile(act_player, tgt_player,
                                paction,
                                act_unit, NULL, tgt_tile,
                                NULL)) {
    return FALSE;
  }

  log_debug("steal gold: infiltrated");

  /* The thief may get caught while trying to steal the gold. */
  if (action_failed_dice_roll(act_player, act_unit, tgt_city, tgt_player,
                              paction)) {
    notify_player(act_player, tgt_tile, E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s was caught attempting to steal gold!"),
                  unit_tile_link(act_unit));
    notify_player(tgt_player, tgt_tile, E_ENEMY_DIPLOMAT_FAILED,
                  ftc_server,
                  /* TRANS: nation, unit, city */
                  _("You caught %s %s attempting"
                    " to steal your gold in %s!"),
                  nation_adjective_for_player(act_player),
                  unit_tile_link(act_unit),
                  tgt_city_link);

    /* This may cause a diplomatic incident */
    action_consequence_caught(paction, act_player, act_utype,
                              tgt_player, tgt_tile, tgt_city_link);

    /* Execute the caught thief. */
    wipe_unit(act_unit, ULR_CAUGHT, tgt_player);

    return FALSE;
  }

  log_debug("steal gold: succeeded");

  /* The upper limit on how much gold the thief can steal. */
  gold_take = (tgt_player->economic.gold
               * get_city_bonus(tgt_city, EFT_MAX_STOLEN_GOLD_PM))
              / 1000;

  /* How much to actually take. 1 gold is the smallest amount that can be
   * stolen. The victim player has at least 1 gold. If they didn't, the
   * something to steal sanity check would have aborted the theft. */
  gold_take = fc_rand(gold_take) + 1;

  log_debug("steal gold: will take %d gold", gold_take);

  /* Steal the gold. */
  tgt_player->economic.gold -= gold_take;

  /* Some gold may be lost during transfer. */
  gold_give = gold_take
            - (gold_take * get_unit_bonus(act_unit, EFT_THIEFS_SHARE_PM))
              / 1000;

  log_debug("steal gold: will give %d gold", gold_give);

  /* Pocket the stolen money. */
  act_player->economic.gold += gold_give;

  /* Notify everyone involved. */
  notify_player(act_player, tgt_tile, E_MY_SPY_STEAL_GOLD, ftc_server,
                /* TRANS: unit, gold, city */
                PL_("Your %s stole %d gold from %s.",
                    "Your %s stole %d gold from %s.", gold_give),
                unit_link(act_unit), gold_give, tgt_city_link);
  notify_player(tgt_player, tgt_tile, E_ENEMY_SPY_STEAL_GOLD, ftc_server,
                /* TRANS: gold, city, nation */
                PL_("%d gold stolen from %s, %s suspected.",
                    "%d gold stolen from %s, %s suspected.", gold_take),
                gold_take, tgt_city_link,
                nation_plural_for_player(act_player));

  /* This may cause a diplomatic incident. */
  action_consequence_success(paction, act_player, act_utype,
                             tgt_player, tgt_tile, tgt_city_link);

  /* Try to escape. */
  diplomat_escape_full(act_player, act_unit, TRUE,
                       tgt_tile, tgt_city_link, paction);

  /* Update the players' gold in the client */
  send_player_info_c(act_player, act_player->connections);
  send_player_info_c(tgt_player, tgt_player->connections);

  return TRUE;
}

/************************************************************************//**
  Steal part of another player's map.

  - Check for infiltration success. Our thief may not survive this.
  - Check for basic success.  Again, our thief may not survive this.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool spy_steal_some_maps(struct player *act_player, struct unit *act_unit,
                         struct city *tgt_city,
                         const struct action *paction)
{
  struct player *tgt_player;
  struct tile *tgt_tile;
  const struct unit_type *act_utype;

  int normal_tile_prob;

  const char *tgt_city_link;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);

  act_utype = unit_type_get(act_unit);

  /* Sanity check: The target city still exists. */
  fc_assert_ret_val(tgt_city, FALSE);

  /* Who to steal from. */
  tgt_player = city_owner(tgt_city);

  /* Sanity check: The target player still exists. */
  fc_assert_ret_val(tgt_player, FALSE);

  /* Sanity check: The target is foreign. */
  if (tgt_player == act_player) {
    /* Nothing to do to a domestic target. */
    return FALSE;
  }

  tgt_tile = city_tile(tgt_city);
  tgt_city_link = city_link(tgt_city);

  log_debug("steal some maps: unit: %d", act_unit->id);

  /* Battle all units capable of diplomatic defense. */
  if (!diplomat_infiltrate_tile(act_player, tgt_player,
                                paction,
                                act_unit, NULL, tgt_tile,
                                NULL)) {
    return FALSE;
  }

  log_debug("steal some maps: infiltrated");

  /* Try to steal the map. */
  if (action_failed_dice_roll(act_player, act_unit, tgt_city, tgt_player,
                              paction)) {
    notify_player(act_player, tgt_tile, E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s was caught in an attempt of"
                    " stealing parts of the %s world map!"),
                  unit_tile_link(act_unit),
                  nation_adjective_for_player(tgt_player));
    notify_player(tgt_player, tgt_tile, E_ENEMY_DIPLOMAT_FAILED,
                  ftc_server,
                  _("You caught %s %s attempting to steal"
                    " parts of your world map in %s!"),
                  nation_adjective_for_player(act_player),
                  unit_tile_link(act_unit),
                  tgt_city_link);

    /* This may cause a diplomatic incident. */
    action_consequence_caught(paction, act_player, act_utype,
                              tgt_player, tgt_tile, tgt_city_link);

    /* Execute the caught thief. */
    wipe_unit(act_unit, ULR_CAUGHT, tgt_player);

    return FALSE;
  }

  log_debug("steal some maps: succeeded");

  /* Steal it. */
  normal_tile_prob = 100
      + get_target_bonus_effects(NULL,
                                 &(const struct req_context) {
                                   .player = act_player,
                                 /* City: Decide once requests from ruleset
                                  * authors arrive. Could be target city or
                                  * - with a refactoring - the city at the
                                  * tile that may be transferred. */
                                 /* Tile: Decide once requests from ruleset
                                  * authors arrive. Could be actor unit
                                  * tile, target city tile or even - with a
                                  * refactoring - the tile that may be
                                  * transferred. */
                                   .unit = act_unit,
                                   .unittype = unit_type_get(act_unit),
                                   .action = paction,
                                 },
                                 &(const struct req_context) {
                                   .player = tgt_player,
                                 },
                                 EFT_MAPS_STOLEN_PCT);
  give_distorted_map(tgt_player, act_player,
                     normal_tile_prob,
                     /* Could - with a refactoring where EFT_MAPS_STOLEN_PCT
                      * is evaulated for each tile and the city sent to it
                      * is the tile to transfer's city - be moved into
                      * EFT_MAPS_STOLEN_PCT. */
                     game.info.steal_maps_reveals_all_cities);

  /* Notify everyone involved. */
  notify_player(act_player, tgt_tile, E_MY_SPY_STEAL_MAP, ftc_server,
                _("Your %s stole parts of the %s world map in %s."),
                unit_link(act_unit),
                nation_adjective_for_player(tgt_player),
                tgt_city_link);
  notify_player(tgt_player, tgt_tile, E_ENEMY_SPY_STEAL_MAP, ftc_server,
                _("The %s are suspected of stealing"
                  " parts of your world map in %s."),
                nation_plural_for_player(act_player),
                tgt_city_link);

  /* This may cause a diplomatic incident. */
  action_consequence_success(paction, act_player, act_utype,
                             tgt_player, tgt_tile, tgt_city_link);

  /* Try to escape. */
  diplomat_escape_full(act_player, act_unit, TRUE,
                       tgt_tile, tgt_city_link, paction);

  return TRUE;
}

/************************************************************************//**
  Hide a suitcase nuke in a city and detonate it.

  - Check for infiltration success. Our thief may not survive this.
  - Check for basic success.  Again, our thief may not survive this.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool spy_nuke_city(struct player *act_player, struct unit *act_unit,
                   struct city *tgt_city, const struct action *paction)
{
  struct player *tgt_player;
  struct tile *tgt_tile;

  const char *tgt_city_link;
  const struct unit_type *act_utype;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);

  act_utype = unit_type_get(act_unit);

  /* Sanity check: The target city still exists. */
  fc_assert_ret_val(tgt_city, FALSE);

  /* The victim. */
  tgt_player = city_owner(tgt_city);

  /* Sanity check: The target player still exists. */
  fc_assert_ret_val(tgt_player, FALSE);

  tgt_tile = city_tile(tgt_city);
  tgt_city_link = city_link(tgt_city);

  log_debug("suitcase nuke: unit: %d", act_unit->id);

  /* Battle all units capable of diplomatic defense. */
  if (!diplomat_infiltrate_tile(act_player, tgt_player,
                                paction,
                                act_unit, NULL, tgt_tile,
                                NULL)) {
    return FALSE;
  }

  log_debug("suitcase nuke: infiltrated");

  /* Try to hide the nuke. */
  if (action_failed_dice_roll(act_player, act_unit, tgt_city, tgt_player,
                              paction)) {
    notify_player(act_player, tgt_tile, E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s was caught in an attempt of"
                    " hiding a nuke in %s!"),
                  unit_tile_link(act_unit),
                  tgt_city_link);
    notify_player(tgt_player, tgt_tile, E_ENEMY_DIPLOMAT_FAILED,
                  ftc_server,
                  _("You caught %s %s attempting to hide a nuke in %s!"),
                  nation_adjective_for_player(act_player),
                  unit_tile_link(act_unit),
                  tgt_city_link);

    /* This may cause a diplomatic incident. */
    action_consequence_caught(paction, act_player, act_utype,
                              tgt_player, tgt_tile, tgt_city_link);

    /* Execute the caught terrorist. */
    wipe_unit(act_unit, ULR_CAUGHT, tgt_player);

    return FALSE;
  }

  log_debug("suitcase nuke: succeeded");

  /* Notify everyone involved. */
  notify_player(act_player, tgt_tile, E_MY_SPY_NUKE, ftc_server,
                _("Your %s hid a nuke in %s."),
                unit_link(act_unit),
                tgt_city_link);
  notify_player(tgt_player, tgt_tile, E_ENEMY_SPY_NUKE, ftc_server,
                _("The %s are suspected of hiding a nuke in %s."),
                nation_plural_for_player(act_player),
                tgt_city_link);

  /* Try to escape before the blast. */
  diplomat_escape_full(act_player, act_unit, TRUE,
                       tgt_tile, tgt_city_link, paction);

  if (utype_is_consumed_by_action(paction, unit_type_get(act_unit))) {
    /* The unit must be wiped here so it won't be seen as a victim of the
     * detonation of its own nuke. */
    wipe_unit(act_unit, ULR_USED, NULL);
  }

  /* Detonate the nuke. */
  dlsend_packet_nuke_tile_info(game.est_connections, tile_index(tgt_tile));
  do_nuclear_explosion(paction, act_utype, act_player, tgt_tile);

  /* This may cause a diplomatic incident. */
  action_consequence_success(paction, act_player, act_utype,
                             tgt_player, tgt_tile, tgt_city_link);

  return TRUE;
}

/************************************************************************//**
  This subtracts the destination movement cost from a diplomat/spy.
****************************************************************************/
static void diplomat_charge_movement(struct unit *pdiplomat, struct tile *ptile)
{
  pdiplomat->moves_left -=
    map_move_cost_unit(&(wld.map), pdiplomat, ptile);
  if (pdiplomat->moves_left < 0) {
    pdiplomat->moves_left = 0;
  }
}

/************************************************************************//**
  This determines if a diplomat/spy succeeds against some defender,
  who is also a diplomat or spy. Note: a superspy defender always
  succeeds, otherwise a superspy attacker always wins.

  Return TRUE if the "attacker" succeeds.
****************************************************************************/
static bool diplomat_success_vs_defender(struct unit *pattacker,
                                         struct unit *pdefender,
                                         struct tile *pdefender_tile,
                                         int *att_vet, int *def_vet)
{
  int chance = 50; /* Base 50% chance */

  /* There's no challenge for the SuperSpy to gain veterancy from,
   * i.e. no veterancy if we exit early in next couple of checks. */
  *att_vet = 0;
  *def_vet = 0;

  if (unit_has_type_flag(pdefender, UTYF_SUPERSPY)) {
    /* A defending UTYF_SUPERSPY will defeat every possible attacker. */
    return FALSE;
  }
  if (unit_has_type_flag(pattacker, UTYF_SUPERSPY)) {
    /* An attacking UTYF_SUPERSPY will defeat every possible defender
     * except another UTYF_SUPERSPY. */
    return TRUE;
  }

  /* Add or remove 25% if spy flag. */
  if (unit_has_type_flag(pattacker, UTYF_SPY)) {
    chance += 25;
  }
  if (unit_has_type_flag(pdefender, UTYF_SPY)) {
    chance -= 25;
  }

  /* Use power_fact from veteran level to modify chance in a linear way.
   * Equal veteran levels cancel out.
   * It's probably not good for rulesets to allow this to have more than
   * 20% effect. */
  {
    const struct veteran_level
      *vatt = utype_veteran_level(unit_type_get(pattacker), pattacker->veteran);
    const struct veteran_level
      *vdef = utype_veteran_level(unit_type_get(pdefender), pdefender->veteran);

    fc_assert_ret_val(vatt != NULL && vdef != NULL, FALSE);

    chance += vatt->power_fact - vdef->power_fact;
  }

  /* Reduce the chance of an attack by EFT_SPY_RESISTANT percent. */
  chance -= chance * get_target_bonus_effects(
                         NULL,
                         &(const struct req_context) {
                           .player = tile_owner(pdefender_tile),
                           .city = tile_city(pdefender_tile),
                           .tile = pdefender_tile,
                         },
                         NULL,
                         EFT_SPY_RESISTANT
                     ) / 100;

  chance = CLIP(0, chance, 100);

  /* In a combat between equal strength units the values are 50% / 50%.
   * -> scaling that to 100% by doubling, to match scale of chances
   *    in existing rulesets, and in !combat_odds_scaled_veterancy case. */
  *att_vet = (100 - chance) * 2;
  *def_vet = chance * 2;

  return (int)fc_rand(100) < chance;
}

/************************************************************************//**
  This determines if a diplomat/spy succeeds in infiltrating a tile.

  - The infiltrator must go up against each defender.
  - The victim unit won't defend. (NULL if everyone should defend)
  - One or the other is eliminated in each contest.

  - Return TRUE if the infiltrator succeeds.

  'pplayer' is the player who tries to do a spy/diplomat action on 'ptile'
  with the unit 'pdiplomat' against 'cplayer'. If 'cplayer' is NULL the
  owner of the chosen defender, if a defender can be chosen, gets its
  role.
  'defender_owner' is, if non NULL, set to the owner of the unit that
  defended.
****************************************************************************/
static bool diplomat_infiltrate_tile(struct player *pplayer,
                                     struct player *cplayer,
                                     const struct action *paction,
                                     struct unit *pdiplomat,
                                     struct unit *pvictim,
                                     struct tile *ptile,
                                     struct player **defender_owner)
{
  struct unit *punit;
  char link_city[MAX_LEN_LINK] = "";
  char link_diplomat[MAX_LEN_LINK];
  char link_unit[MAX_LEN_LINK];
  struct city *pcity = tile_city(ptile);
  const struct unit_type *act_utype = unit_type_get(pdiplomat);
  int att_vet;
  int def_vet;

  if (pcity) {
    /* N.B.: *_link() always returns the same pointer. */
    sz_strlcpy(link_city, city_link(pcity));
  }

  if ((punit = get_diplomatic_defender(pdiplomat, pvictim, ptile,
                                       paction))) {
    struct player *uplayer = unit_owner(punit);

    if (defender_owner != NULL) {
      /* Some action performers may want to know defender player. */
      *defender_owner = uplayer;
    }

    if (diplomat_success_vs_defender(pdiplomat, punit, ptile,
                                     &att_vet, &def_vet)) {
      /* Defending Spy/Diplomat dies. */

      /* N.B.: *_link() always returns the same pointer. */
      sz_strlcpy(link_unit, unit_tile_link(punit));
      sz_strlcpy(link_diplomat, unit_link(pdiplomat));

      notify_player(pplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                    /* TRANS: <unit> ... <diplomat> */
                    _("An enemy %s has been eliminated by your %s."),
                    link_unit, link_diplomat);

      if (pcity) {
        if (uplayer == cplayer || cplayer == NULL) {
          notify_player(uplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                        /* TRANS: <unit> ... <city> ... <diplomat> */
                        _("Your %s has been eliminated defending %s"
                          " against a %s."), link_unit, link_city,
                        link_diplomat);
        } else {
          notify_player(cplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                        /* TRANS: <nation adj> <unit> ... <city>
                                         * TRANS: ... <diplomat> */
                        _("A %s %s has been eliminated defending %s "
                          "against a %s."),
                        nation_adjective_for_player(uplayer),
                        link_unit, link_city, link_diplomat);
          notify_player(uplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                        /* TRANS: ... <unit> ... <nation adj> <city>
                                         * TRANS: ... <diplomat> */
                        _("Your %s has been eliminated defending %s %s "
                          "against a %s."), link_unit,
                        nation_adjective_for_player(cplayer),
                        link_city, link_diplomat);
        }
      } else {
        if (uplayer == cplayer || cplayer == NULL) {
          notify_player(uplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                        /* TRANS: <unit> ... <diplomat> */
                        _("Your %s has been eliminated defending "
                          "against a %s."), link_unit, link_diplomat);
        } else {
          notify_player(cplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                        /* TRANS: <nation adj> <unit> ... <diplomat> */
                        _("A %s %s has been eliminated defending "
                          "against a %s."),
                        nation_adjective_for_player(uplayer),
                        link_unit, link_diplomat);
          notify_player(uplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                        /* TRANS: ... <unit> ... <diplomat> */
                        _("Your %s has been eliminated defending "
                          "against a %s."), link_unit, link_diplomat);
        }
      }

      pdiplomat->moves_left = MAX(0, pdiplomat->moves_left - SINGLE_MOVE);

      /* Attacking unit became more experienced? */
      if (maybe_make_veteran(pdiplomat,
                             game.info.combat_odds_scaled_veterancy ? att_vet : 100)) {
        notify_unit_experience(pdiplomat);
      }
      send_unit_info(NULL, pdiplomat);
      wipe_unit(punit, ULR_ELIMINATED, pplayer);
      return FALSE;
    } else {
      /* Attacking Spy/Diplomat dies. */

      const char *victim_link;

      /* N.B.: *_link() always returns the same pointer. */
      sz_strlcpy(link_unit, unit_link(punit));
      sz_strlcpy(link_diplomat, unit_tile_link(pdiplomat));

      notify_player(pplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s was eliminated by a defending %s."),
                    link_diplomat, link_unit);

      if (pcity) {
        if (uplayer == cplayer || cplayer == NULL) {
          notify_player(uplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                        _("Eliminated a %s %s while infiltrating %s."),
                        nation_adjective_for_player(pplayer),
                        link_diplomat, link_city);
        } else {
          notify_player(cplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                        _("A %s %s eliminated a %s %s while infiltrating "
                          "%s."), nation_adjective_for_player(uplayer),
                        link_unit, nation_adjective_for_player(pplayer),
                        link_diplomat, link_city);
          notify_player(uplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                        _("Your %s eliminated a %s %s while infiltrating "
                          "%s."), link_unit,
                        nation_adjective_for_player(pplayer),
                        link_diplomat, link_city);
        }
      } else {
        if (uplayer == cplayer || cplayer == NULL) {
          notify_player(uplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                        _("Eliminated a %s %s while infiltrating our troops."),
                        nation_adjective_for_player(pplayer),
                        link_diplomat);
        } else {
          notify_player(cplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                        _("A %s %s eliminated a %s %s while infiltrating our "
                          "troops."), nation_adjective_for_player(uplayer),
                        link_unit, nation_adjective_for_player(pplayer),
                        link_diplomat);
          notify_player(uplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                        /* TRANS: ... <unit> ... <diplomat> */
                        _("Your %s eliminated a %s %s while infiltrating our "
                          "troops."), link_unit,
                        nation_adjective_for_player(pplayer),
                        link_diplomat);
        }
      }

      /* Defending unit became more experienced? */
      if (maybe_make_veteran(punit,
                             game.info.combat_odds_scaled_veterancy ? def_vet : 100)) {
        notify_unit_experience(punit);
      }

      victim_link = NULL;

      switch (action_get_target_kind(paction)) {
      case ATK_CITY:
        victim_link = city_link(pcity);
        break;
      case ATK_UNIT:
      case ATK_STACK:
        victim_link = pvictim ? unit_tile_link(pvictim)
                              : tile_link(ptile);
        break;
      case ATK_TILE:
      case ATK_EXTRAS:
        victim_link = tile_link(ptile);
        break;
      case ATK_SELF:
        /* How did a self targeted action end up here? */
        fc_assert(action_get_target_kind(paction) != ATK_SELF);
        break;
      case ATK_COUNT:
        break;
      }

      fc_assert(victim_link != NULL);

      action_consequence_caught(paction, pplayer, act_utype, uplayer,
                                ptile, victim_link);

      wipe_unit(pdiplomat, ULR_ELIMINATED, uplayer);
      return FALSE;
    }
  }

  return TRUE;
}

/************************************************************************//**
  This determines if a diplomat/spy survives and escapes.
  If "pcity" is NULL, assume action was in the field.

  Spies have a game.server.diplchance specified chance of survival (better
  if veteran):
    - Diplomats always die.
    - Escapes to home city.
    - Escapee may become a veteran.
****************************************************************************/
void diplomat_escape(struct player *pplayer, struct unit *pdiplomat,
                     const struct city *pcity,
                     const struct action *paction)
{
  struct tile *ptile;
  const char *vlink;

  if (pcity) {
    ptile = city_tile(pcity);
    vlink = city_link(pcity);
  } else {
    ptile = unit_tile(pdiplomat);
    vlink = NULL;
  }

  return diplomat_escape_full(pplayer, pdiplomat, pcity != NULL,
                              ptile, vlink, paction);
}

/************************************************************************//**
  This determines if a diplomat/spy survives and escapes.

  Spies have a game.server.diplchance specified chance of survival (better
  if veteran):
    - Diplomats always die.
    - Escapes to home city.
    - Escapee may become a veteran.
****************************************************************************/
static void diplomat_escape_full(struct player *pplayer,
                                 struct unit *pdiplomat,
                                 bool city_related,
                                 struct tile *ptile,
                                 const char *vlink,
                                 const struct action *paction)
{
  int escapechance;
  struct city *spyhome;
  const struct unit_type *dipltype = unit_type_get(pdiplomat);

  fc_assert(paction->actor.is_unit.moves_actor == MAK_ESCAPE);

  /* Veteran level's power factor's effect on escape chance is relative to
   * unpromoted unit's power factor */
  {
    const struct veteran_level
      *vunit = utype_veteran_level(dipltype, pdiplomat->veteran);
    const struct veteran_level
      *vbase = utype_veteran_level(dipltype, 0);

    escapechance = game.server.diplchance
      + (vunit->power_fact - vbase->power_fact);
  }

  /* find closest city for escape target */
  spyhome = find_closest_city(ptile, NULL, unit_owner(pdiplomat), FALSE,
                              FALSE, FALSE, TRUE, FALSE, NULL);

  if (spyhome
      && !utype_is_consumed_by_action(paction, dipltype)
      && (unit_has_type_flag(pdiplomat, UTYF_SUPERSPY)
          || fc_rand (100) < escapechance)) {
    /* Attacking Spy/Diplomat survives. */
    notify_player(pplayer, ptile, E_MY_DIPLOMAT_ESCAPE, ftc_server,
                  _("Your %s has successfully completed"
                    " the mission and returned unharmed to %s."),
                  unit_link(pdiplomat),
                  city_link(spyhome));
    if (maybe_make_veteran(pdiplomat, 100)) {
      notify_unit_experience(pdiplomat);
    }

    if (!teleport_unit_to_city (pdiplomat, spyhome,
                                /* Handled by the ruleset. */
                                0,
                                FALSE)) {
      send_unit_info(NULL, pdiplomat);
      log_error("Bug in diplomat_escape: Spy can't teleport.");
      return;
    }

    return;
  } else {
    if (city_related) {
      notify_player(pplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s was captured after completing"
                      " the mission in %s."),
                    unit_tile_link(pdiplomat),
                    vlink);
    } else {
      notify_player(pplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s was captured after completing"
                      " the mission."),
                    unit_tile_link(pdiplomat));
    }
  }

  if (!utype_is_consumed_by_action(paction, dipltype)) {
    /* The unit was caught, not spent. It must therefore be deleted by
     * hand. */
    wipe_unit(pdiplomat, ULR_CAUGHT, NULL);
  }
}

/************************************************************************//**
  Attempt to escape without doing anything else first.

  May be captured and executed, or escape to the nearest domestic city.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
****************************************************************************/
bool spy_escape(struct player *pplayer,
                struct unit *actor_unit,
                struct city *target_city,
                struct tile *target_tile,
                const struct action *paction)
{
  const char *vlink;
  const struct unit_type *act_utype = unit_type_get(actor_unit);

  if (target_city != NULL) {
    fc_assert(city_tile(target_city) == target_tile);
    vlink = city_link(target_city);
  } else {
    vlink = tile_link(target_tile);
  }

  /* this may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype,
                             tile_owner(target_tile),
                             target_tile, vlink);

  /* Try to escape. */
  diplomat_escape_full(pplayer, actor_unit, target_city != NULL,
                       target_tile, vlink, paction);

  return TRUE;
}

/************************************************************************//**
  Return number of diplomats on this square.  AJS 20000130
****************************************************************************/
int count_diplomats_on_tile(struct tile *ptile)
{
  int count = 0;

  unit_list_iterate((ptile)->units, punit) {
    if (unit_has_type_flag(punit, UTYF_DIPLOMAT)) {
      count++;
    }
  } unit_list_iterate_end;

  return count;
}
