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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "rand.h"
#include "support.h"

/* common */
#include "city.h"
#include "events.h"
#include "game.h"
#include "idex.h"
#include "map.h"
#include "player.h"
#include "specialist.h"
#include "unit.h"
#include "worklist.h"

/* server */
#include "citytools.h"
#include "cityturn.h"
#include "notify.h"
#include "plrhand.h"
#include "sanitycheck.h"
#include "unithand.h"

#include "cityhand.h"

/**************************************************************************
  Send city_name_suggestion packet back to requesting conn, with
  suggested name and with same id which was passed in (either unit id
  for city builder or existing city id for rename, we don't care here).
**************************************************************************/
void handle_city_name_suggestion_req(struct player *pplayer, int unit_id)
{
  struct unit *punit = player_unit_by_number(pplayer, unit_id);
  enum unit_add_build_city_result res;

  if (NULL == punit) {
    /* Probably died or bribed. */
    log_verbose("handle_city_name_suggestion_req() invalid unit %d",
                unit_id);
    return;
  }

  res = unit_add_or_build_city_test(punit);

  switch (res) {
  case UAB_BUILD_OK:
    log_verbose("handle_city_name_suggest_req(unit_pos (%d, %d))",
                TILE_XY(unit_tile(punit)));
    dlsend_packet_city_name_suggestion_info(pplayer->connections, unit_id,
        city_name_suggestion(pplayer, unit_tile(punit)));
    break;

  case UAB_BAD_CITY_TERRAIN:
  case UAB_BAD_UNIT_TERRAIN:
  case UAB_BAD_BORDERS:
  case UAB_NO_MIN_DIST:
  case UAB_NOT_BUILD_UNIT:
  case UAB_NO_MOVES_BUILD:
    log_verbose("handle_city_name_suggest_req(unit_pos (%d, %d)): "
                "cannot build there.", TILE_XY(unit_tile(punit)));
    city_add_or_build_error(pplayer, punit, res);       /* Message. */
    break;

  case UAB_ADD_OK:
  case UAB_NOT_ADDABLE_UNIT:
  case UAB_NO_MOVES_ADD:
  case UAB_NOT_OWNER:
  case UAB_TOO_BIG:
  case UAB_NO_SPACE:
    log_verbose("handle_city_name_suggest_req(unit_pos (%d, %d)): "
                "there is already a city there.", TILE_XY(unit_tile(punit)));
    /* Ignoring. */
    break;
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_change_specialist(struct player *pplayer, int city_id,
				   Specialist_type_id from,
				   Specialist_type_id to)
{
  struct city *pcity = player_city_by_number(pplayer, city_id);

  if (!pcity) {
    return;
  }

  if (to < 0 || to >= specialist_count()
      || from < 0 || from >= specialist_count()
      || !city_can_use_specialist(pcity, to)
      || pcity->specialists[from] == 0) {
    /* This could easily just be due to clicking faster on the specialist
     * than the server can cope with. */
    log_verbose("Error in specialist change request from client.");
    return;
  }

  pcity->specialists[from]--;
  pcity->specialists[to]++;

  sanity_check_city(pcity);
  city_refresh(pcity);
  send_city_info(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_make_specialist(struct player *pplayer, int city_id,
                                 int worker_x, int worker_y)
{
  struct tile *ptile;
  struct tile *pcenter;
  struct city *pcity = player_city_by_number(pplayer, city_id);
  int city_radius_sq;

  if (NULL == pcity) {
    /* Probably lost. */
    log_verbose("handle_city_make_specialist() bad city number %d.",
                city_id);
    return;
  }

  city_radius_sq = city_map_radius_sq_get(pcity);
  if (!is_valid_city_coords(city_radius_sq, worker_x, worker_y)) {
    log_error("handle_city_make_specialist() invalid city map {%d,%d} "
              "\"%s\".", worker_x, worker_y, city_name(pcity));
    return;
  }
  pcenter = city_tile(pcity);

  if (NULL == (ptile = city_map_to_tile(pcenter, city_radius_sq, worker_x,
                                        worker_y))) {
    log_error("handle_city_make_specialist() unavailable city map {%d,%d} "
              "\"%s\".", worker_x, worker_y, city_name(pcity));
    return;
  }

  if (is_free_worked(pcity, ptile)) {
    auto_arrange_workers(pcity);
    sync_cities();
    return;
  }

  if (tile_worked(ptile) == pcity) {
    city_map_update_empty(pcity, ptile);
    pcity->specialists[DEFAULT_SPECIALIST]++;
    city_refresh(pcity);
    sync_cities();
  } else {
    log_verbose("handle_city_make_specialist() not working {%d,%d} \"%s\".",
                worker_x, worker_y, city_name(pcity));
  }

  sanity_check_city(pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_make_worker(struct player *pplayer, int city_id,
			     int worker_x, int worker_y)
{
  struct tile *ptile;
  struct tile *pcenter;
  struct city *pcity = player_city_by_number(pplayer, city_id);
  int city_radius_sq = city_map_radius_sq_get(pcity);

  if (NULL == pcity) {
    /* Probably lost. */
    log_verbose("handle_city_make_worker() bad city number %d.",city_id);
    return;
  }

  if (!is_valid_city_coords(city_radius_sq, worker_x, worker_y)) {
    log_error("handle_city_make_worker() invalid city map {%d,%d} "
              "\"%s\".", worker_x, worker_y, city_name(pcity));
    return;
  }
  pcenter = city_tile(pcity);

  if (NULL == (ptile = city_map_to_tile(pcenter, city_radius_sq, worker_x,
                                        worker_y))) {
    log_error("handle_city_make_worker() unavailable city map {%d,%d} "
              "\"%s\".", worker_x, worker_y, city_name(pcity));
    return;
  }

  if (is_free_worked(pcity, ptile)) {
    auto_arrange_workers(pcity);
    sync_cities();
    return;
  }

  if (tile_worked(ptile) == pcity) {
    log_verbose("handle_city_make_worker() already working {%d,%d} \"%s\".",
                worker_x, worker_y, city_name(pcity));
    return;
  }

  if (0 == city_specialists(pcity)) {
    log_verbose("handle_city_make_worker() no specialists {%d,%d} \"%s\".",
                worker_x, worker_y, city_name(pcity));
    return;
  }

  if (!city_can_work_tile(pcity, ptile)) {
    log_verbose("handle_city_make_worker() cannot work here {%d,%d} \"%s\".",
                worker_x, worker_y, city_name(pcity));
    return;
  }

  city_map_update_worker(pcity, ptile);

  specialist_type_iterate(i) {
    if (pcity->specialists[i] > 0) {
      pcity->specialists[i]--;
      break;
    }
  } specialist_type_iterate_end;

  sanity_check_city(pcity);
  city_refresh(pcity);
  sync_cities();
}

/**************************************************************************
...
**************************************************************************/
void really_handle_city_sell(struct player *pplayer, struct city *pcity,
			     struct impr_type *pimprove)
{
  int price;
  if (pcity->did_sell) {
    notify_player(pplayer, pcity->tile, E_BAD_COMMAND, ftc_server,
		  _("You have already sold something here this turn."));
    return;
  }

  if (!can_city_sell_building(pcity, pimprove))
    return;

  pcity->did_sell=TRUE;
  price = impr_sell_gold(pimprove);
  notify_player(pplayer, pcity->tile, E_IMP_SOLD, ftc_server,
                PL_("You sell %s in %s for %d gold.",
                    "You sell %s in %s for %d gold.", price),
                improvement_name_translation(pimprove),
                city_link(pcity), price);
  do_sell_building(pplayer, pcity, pimprove);

  city_refresh(pcity);

  /* If we sold the walls the other players should see it */
  send_city_info(NULL, pcity);
  send_player_info_c(pplayer, pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_sell(struct player *pplayer, int city_id, int build_id)
{
  struct city *pcity = player_city_by_number(pplayer, city_id);
  struct impr_type *pimprove = improvement_by_number(build_id);

  if (!pcity || !pimprove) {
    return;
  }
  really_handle_city_sell(pplayer, pcity, pimprove);
}

/**************************************************************************
...
**************************************************************************/
void really_handle_city_buy(struct player *pplayer, struct city *pcity)
{
  int cost, total;

  /* This function corresponds to city_can_buy() in the client. */

  fc_assert_ret(pcity && player_owns_city(pplayer, pcity));
 
  if (pcity->turn_founded == game.info.turn) {
    notify_player(pplayer, pcity->tile, E_BAD_COMMAND, ftc_server,
                  _("Cannot buy in city created this turn."));
    return;
  }

  if (pcity->did_buy) {
    notify_player(pplayer, pcity->tile, E_BAD_COMMAND, ftc_server,
		  _("You have already bought this turn."));
    return;
  }

  if (city_production_has_flag(pcity, IF_GOLD)) {
    notify_player(pplayer, pcity->tile, E_BAD_COMMAND, ftc_server,
                  _("You don't buy %s!"),
                  improvement_name_translation(pcity->production.value.building));
    return;
  }

  if (VUT_UTYPE == pcity->production.kind && pcity->anarchy != 0) {
    notify_player(pplayer, pcity->tile, E_BAD_COMMAND, ftc_server,
                  _("Can't buy units when city is in disorder."));
    return;
  }

  total = city_production_build_shield_cost(pcity);
  cost = city_production_buy_gold_cost(pcity);
  if (cost <= 0) {
    return; /* sanity */
  }
  if (cost > pplayer->economic.gold) {
    /* In case something changed while player tried to buy, or player 
     * tried to cheat! */
    /* Split into two to allow localization of two pluralisations. */
    char buf[MAX_LEN_MSG];
    /* TRANS: This whole string is only ever used when included in one
     * other string (search for this string to find it). */
    fc_snprintf(buf, ARRAY_SIZE(buf), PL_("%d gold required.",
                                          "%d gold required.",
                                          cost), cost);
    notify_player(pplayer, pcity->tile, E_BAD_COMMAND, ftc_server,
                  /* TRANS: %s is a pre-pluralised string:
                   * "%d gold required." */
                  PL_("%s You only have %d gold.",
                      "%s You only have %d gold.", pplayer->economic.gold),
                  buf, pplayer->economic.gold);
    return;
  }

  pplayer->economic.gold-=cost;
  if (pcity->shield_stock < total){
    /* As we never put penalty on disbanded_shields, we can
     * fully well add the missing shields there. */
    pcity->disbanded_shields += total - pcity->shield_stock;
    pcity->shield_stock=total; /* AI wants this -- Syela */
    pcity->did_buy = TRUE;	/* !PS: no need to set buy flag otherwise */
  }
  city_refresh(pcity);

  if (VUT_UTYPE == pcity->production.kind) {
    notify_player(pplayer, pcity->tile, E_UNIT_BUY, ftc_server,
                  /* TRANS: bought an unit. */
                  _("You bought %s in %s."),
                  utype_rule_name(pcity->production.value.utype),
                  city_name(pcity));
  } else if (VUT_IMPROVEMENT == pcity->production.kind) {
    notify_player(pplayer, pcity->tile, E_IMP_BUY, ftc_server,
                  /* TRANS: bought an improvement .*/
                  _("You bought %s in %s."),
                  improvement_name_translation(
                    pcity->production.value.building), city_name(pcity));
  }

  conn_list_do_buffer(pplayer->connections);
  send_city_info(pplayer, pcity);
  send_player_info_c(pplayer, pplayer->connections);
  conn_list_do_unbuffer(pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_worklist(struct player *pplayer, int city_id,
                          const struct worklist *worklist)
{
  struct city *pcity = player_city_by_number(pplayer, city_id);

  if (!pcity) {
    return;
  }

  worklist_copy(&pcity->worklist, worklist);

  send_city_info(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_buy(struct player *pplayer, int city_id)
{
  struct city *pcity = player_city_by_number(pplayer, city_id);

  if (!pcity) {
    return;
  }

  really_handle_city_buy(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_refresh(struct player *pplayer, int city_id)
{
  if (city_id != 0) {
    struct city *pcity = player_city_by_number(pplayer, city_id);

    if (!pcity) {
      return;
    }

    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  } else {
    city_refresh_for_player(pplayer);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_change(struct player *pplayer, int city_id,
			int production_kind, int production_value)
{
  struct universal prod;
  struct city *pcity = player_city_by_number(pplayer, city_id);

  if (!universals_n_is_valid(production_kind)) {
    log_error("[%s] bad production_kind %d.", __FUNCTION__,
              production_kind);
    prod.kind = VUT_NONE;
    return;
  } else {
    prod = universal_by_number(production_kind, production_value);
    if (!universals_n_is_valid(prod.kind)) {
      log_error("[%s] production_kind %d with bad production_value %d.",
                __FUNCTION__, production_kind, production_value);
      prod.kind = VUT_NONE;
      return;
    }
  }

  if (!pcity) {
    return;
  }

  if (are_universals_equal(&pcity->production, &prod)) {
    /* The client probably shouldn't send such a packet. */
    return;
  }

  if (!can_city_build_now(pcity, prod)) {
    return;
  }
  if (!city_can_change_build(pcity)) {
    notify_player(pplayer, pcity->tile, E_BAD_COMMAND, ftc_server,
                  _("You have bought this turn, can't change."));
    return;
  }

  change_build_target(pplayer, pcity, prod, E_CITY_PRODUCTION_CHANGED);

  sanity_check_city(pcity);
  city_refresh(pcity);
  send_city_info(pplayer, pcity);
}

/****************************************************************************
  'struct packet_city_rename' handler.
****************************************************************************/
void handle_city_rename(struct player *pplayer, int city_id,
                        const char *name)
{
  struct city *pcity = player_city_by_number(pplayer, city_id);
  char message[1024];

  if (!pcity) {
    return;
  }

  if (!is_allowed_city_name(pplayer, name, message, sizeof(message))) {
    notify_player(pplayer, pcity->tile, E_BAD_COMMAND,
                  ftc_server, "%s", message);
    return;
  }

  sz_strlcpy(pcity->name, name);
  city_refresh(pcity);
  send_city_info(NULL, pcity);
}

/****************************************************************************
  Handles a packet from the client that requests the city options for the
  given city be changed.
****************************************************************************/
void handle_city_options_req(struct player *pplayer, int city_id,
			     bv_city_options options)
{
  struct city *pcity = player_city_by_number(pplayer, city_id);

  if (!pcity) {
    return;
  }

  pcity->city_options = options;

  send_city_info(pplayer, pcity);
}
