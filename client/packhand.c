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
#include <fc_config.h>
#endif

#include <string.h>

/* utility */
#include "bitvector.h"
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "string_vector.h"
#include "support.h"

/* common */
#include "capstr.h"
#include "citizens.h"
#include "events.h"
#include "game.h"
#include "government.h"
#include "idex.h"
#include "map.h"
#include "name_translation.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "research.h"
#include "rgbcolor.h"
#include "road.h"
#include "spaceship.h"
#include "specialist.h"
#include "unit.h"
#include "unitlist.h"
#include "worklist.h"

/* client/include */
#include "chatline_g.h"
#include "citydlg_g.h"
#include "cityrep_g.h"
#include "connectdlg_g.h"
#include "dialogs_g.h"
#include "editgui_g.h"
#include "gui_main_g.h"
#include "inteldlg_g.h"
#include "mapctrl_g.h"          /* popup_newcity_dialog() */
#include "mapview_g.h"
#include "menu_g.h"
#include "messagewin_g.h"
#include "pages_g.h"
#include "plrdlg_g.h"
#include "repodlgs_g.h"
#include "spaceshipdlg_g.h"
#include "voteinfo_bar_g.h"
#include "wldlg_g.h"

/* client */
#include "agents.h"
#include "attribute.h"
#include "audio.h"
#include "client_main.h"
#include "climap.h"
#include "climisc.h"
#include "connectdlg_common.h"
#include "control.h"
#include "editor.h"
#include "ggzclient.h"
#include "goto.h"               /* client_goto_init() */
#include "helpdata.h"           /* boot_help_texts() */
#include "mapview_common.h"
#include "options.h"
#include "overview_common.h"
#include "tilespec.h"
#include "update_queue.h"
#include "voteinfo.h"

#include "packhand.h"

/* Define this macro to get additional debug output about the transport
 * status of the units. */
#undef DEBUG_TRANSPORT

static void city_packet_common(struct city *pcity, struct tile *pcenter,
                               struct player *powner,
                               struct tile_list *worked_tiles,
                               bool is_new, bool popup, bool investigate);
static bool handle_unit_packet_common(struct unit *packet_unit);


/* The dumbest of cities, placeholders for unknown and unseen cities. */
static struct {
  struct city_list *cities;
  struct player *placeholder;
} invisible = {
  .cities = NULL,
  .placeholder = NULL
};

/****************************************************************************
  Called below, and by client/client_main.c client_game_free()
****************************************************************************/
void packhand_free(void)
{
  if (NULL != invisible.cities) {
    city_list_iterate(invisible.cities, pcity) {
      idex_unregister_city(pcity);
      destroy_city_virtual(pcity);
    } city_list_iterate_end;

    city_list_destroy(invisible.cities);
    invisible.cities = NULL;
  }

  if (NULL != invisible.placeholder) {
    free(invisible.placeholder);
    invisible.placeholder = NULL;
  }
}

/****************************************************************************
  Called only by handle_map_info() below.
****************************************************************************/
static void packhand_init(void)
{
  packhand_free();

  invisible.cities = city_list_new();

  /* Can't use player_new() here, as it will register the player. */
  invisible.placeholder = fc_calloc(1, sizeof(*invisible.placeholder));
  memset(invisible.placeholder, 0, sizeof(*invisible.placeholder));
  /* Set some values to prevent bugs ... */
  sz_strlcpy(invisible.placeholder->name, ANON_PLAYER_NAME);
  sz_strlcpy(invisible.placeholder->username, ANON_USER_NAME);
  sz_strlcpy(invisible.placeholder->ranked_username, ANON_USER_NAME);
}

/****************************************************************************
  Unpackage the unit information into a newly allocated unit structure.

  Information for the client must also be processed in
  handle_unit_packet_common()! Especially notice that unit structure filled
  here is just temporary one. Values must be copied to real unit in
  handle_unit_packet_common().
****************************************************************************/
static struct unit *unpackage_unit(const struct packet_unit_info *packet)
{
  struct unit *punit = unit_virtual_create(player_by_number(packet->owner),
					   NULL,
					   utype_by_number(packet->type),
					   packet->veteran);

  /* Owner, veteran, and type fields are already filled in by
   * unit_virtual_create. */
  punit->id = packet->id;
  unit_tile_set(punit, index_to_tile(packet->tile));
  punit->facing = packet->facing;
  punit->homecity = packet->homecity;
  output_type_iterate(o) {
    punit->upkeep[o] = packet->upkeep[o];
  } output_type_iterate_end;
  punit->moves_left = packet->movesleft;
  punit->hp = packet->hp;
  punit->activity = packet->activity;
  punit->activity_count = packet->activity_count;

  punit->activity_target.type = ATT_SPECIAL;
  punit->activity_target.obj.spe = packet->activity_tgt_spe;
  if (packet->activity_tgt_base != BASE_NONE) {
    punit->activity_target.type = ATT_BASE;
    punit->activity_target.obj.base = packet->activity_tgt_base;
  } else if (packet->activity_tgt_road != ROAD_NONE) {
    punit->activity_target.type = ATT_ROAD;
    punit->activity_target.obj.road = packet->activity_tgt_road;
  }

  punit->changed_from = packet->changed_from;
  punit->changed_from_count = packet->changed_from_count;

  punit->changed_from_target.type = ATT_SPECIAL;
  punit->changed_from_target.obj.spe = packet->changed_from_tgt_spe;
  if (packet->changed_from_tgt_base != BASE_NONE) {
    punit->changed_from_target.type = ATT_BASE;
    punit->changed_from_target.obj.base = packet->changed_from_tgt_base;
  } else if (packet->changed_from_tgt_road != ROAD_NONE) {
    punit->changed_from_target.type = ATT_ROAD;
    punit->changed_from_target.obj.road = packet->changed_from_tgt_road;
  }

  punit->ai_controlled = packet->ai;
  punit->fuel = packet->fuel;
  punit->goto_tile = index_to_tile(packet->goto_tile);
  punit->paradropped = packet->paradropped;
  punit->done_moving = packet->done_moving;

  /* Transporter / transporting information. */
  punit->client.occupied = packet->occupied;
  if (packet->transported) {
    punit->client.transported_by = packet->transported_by;
  } else {
    punit->client.transported_by = -1;
  }

  punit->battlegroup = packet->battlegroup;
  punit->has_orders = packet->has_orders;
  punit->orders.length = packet->orders_length;
  punit->orders.index = packet->orders_index;
  punit->orders.repeat = packet->orders_repeat;
  punit->orders.vigilant = packet->orders_vigilant;
  if (punit->has_orders) {
    int i;

    punit->orders.list
      = fc_malloc(punit->orders.length * sizeof(*punit->orders.list));
    for (i = 0; i < punit->orders.length; i++) {
      punit->orders.list[i].order = packet->orders[i];
      punit->orders.list[i].dir = packet->orders_dirs[i];
      punit->orders.list[i].activity = packet->orders_activities[i];
      punit->orders.list[i].base = packet->orders_bases[i];
      punit->orders.list[i].road = packet->orders_roads[i];
    }
  }

  punit->client.asking_city_name = FALSE;

  return punit;
}

/****************************************************************************
  Unpackage a short_unit_info packet.  This extracts a limited amount of
  information about the unit, and is sent for units we shouldn't know
  everything about (like our enemies' units).

  Information for the client must also be processed in
  handle_unit_packet_common()! Especially notice that unit structure filled
  here is just temporary one. Values must be copied to real unit in
  handle_unit_packet_common().
****************************************************************************/
static struct unit *
unpackage_short_unit(const struct packet_unit_short_info *packet)
{
  struct unit *punit = unit_virtual_create(player_by_number(packet->owner),
					   NULL,
					   utype_by_number(packet->type),
					   FALSE);

  /* Owner and type fields are already filled in by unit_virtual_create. */
  punit->id = packet->id;
  unit_tile_set(punit, index_to_tile(packet->tile));
  punit->facing = packet->facing;
  punit->veteran = packet->veteran;
  punit->hp = packet->hp;
  punit->activity = packet->activity;

  punit->activity_target.type = ATT_SPECIAL;
  punit->activity_target.obj.spe = S_LAST;
  if (packet->activity_tgt_base != BASE_NONE) {
    punit->activity_target.type = ATT_BASE;
    punit->activity_target.obj.base = packet->activity_tgt_base;
  } else if (packet->activity_tgt_road != ROAD_NONE) {
    punit->activity_target.type = ATT_ROAD;
    punit->activity_target.obj.road = packet->activity_tgt_road;
  }

  /* Transporter / transporting information. */
  punit->client.occupied = packet->occupied;
  if (packet->transported) {
    punit->client.transported_by = packet->transported_by;
  } else {
    punit->client.transported_by = -1;
  }

  return punit;
}

/****************************************************************************
  After we send a join packet to the server we receive a reply.  This
  function handles the reply.
****************************************************************************/
void handle_server_join_reply(bool you_can_join, const char *message,
                              const char *capability,
                              const char *challenge_file, int conn_id)
{
  const char *s_capability = client.conn.capability;

  sz_strlcpy(client.conn.capability, capability);
  close_connection_dialog();

  if (you_can_join) {
    struct packet_client_info client_info;

    log_verbose("join game accept:%s", message);
    client.conn.established = TRUE;
    client.conn.id = conn_id;

    agents_game_joined();
    set_server_busy(FALSE);

    if (get_client_page() == PAGE_MAIN
        || get_client_page() == PAGE_NETWORK
        || get_client_page() == PAGE_GGZ) {
      set_client_page(PAGE_START);
    }

    client_info.gui = get_gui_type();
    strncpy(client_info.distribution, FREECIV_DISTRIBUTOR,
            sizeof(client_info.distribution));
    send_packet_client_info(&client.conn, &client_info);

    /* we could always use hack, verify we're local */ 
    send_client_wants_hack(challenge_file);

    set_client_state(C_S_PREPARING);
  } else {
    output_window_printf(ftc_client,
                         _("You were rejected from the game: %s"), message);
    client.conn.id = -1; /* not in range of conn_info id */

    if (auto_connect) {
      log_normal(_("You were rejected from the game: %s"), message);
    }
    gui_server_connect();

    if (!with_ggz) {
      set_client_page(in_ggz ? PAGE_MAIN : PAGE_GGZ);
    }
  }
  if (strcmp(s_capability, our_capability) == 0) {
    return;
  }
  output_window_printf(ftc_client, _("Client capability string: %s"),
                       our_capability);
  output_window_printf(ftc_client, _("Server capability string: %s"),
                       s_capability);
}

/****************************************************************************
  Handles a remove-city packet, used by the server to tell us any time a
  city is no longer there.
****************************************************************************/
void handle_city_remove(int city_id)
{
  struct city *pcity = game_city_by_number(city_id);
  bool need_menus_update;

  fc_assert_ret_msg(NULL != pcity, "Bad city %d.", city_id);

  need_menus_update = (NULL != get_focus_unit_on_tile(city_tile(pcity)));

  agents_city_remove(pcity);
  editgui_notify_object_changed(OBJTYPE_CITY, pcity->id, TRUE);
  client_remove_city(pcity);

  /* Update menus if the focus unit is on the tile. */
  if (need_menus_update) {
    menus_update();
  }
}

/**************************************************************************
  Handle a remove-unit packet, sent by the server to tell us any time a
  unit is no longer there.
**************************************************************************/
void handle_unit_remove(int unit_id)
{
  struct unit *punit = game_unit_by_number(unit_id);
  struct unit_list *cargos;
  struct player *powner;
  bool need_economy_report_update;

  if (!punit) {
    return;
  }

  /* Close diplomat dialog if the diplomat is lost */
  if (diplomat_handled_in_diplomat_dialog() == punit->id) {
    close_diplomat_dialog();
    /* Open another diplomat dialog if there are other diplomats waiting */
    process_diplomat_arrival(NULL, 0);
  }

  need_economy_report_update = (0 < punit->upkeep[O_GOLD]);
  powner = unit_owner(punit);

  /* Unload cargo if this is a transporter. */
  cargos = unit_transport_cargo(punit);
  if (unit_list_size(cargos) > 0) {
    unit_list_iterate(cargos, pcargo) {
      unit_transport_unload(pcargo);
    } unit_list_iterate_end;
  }

  /* Unload unit if it is transported. */
  if (unit_transport_get(punit)) {
    unit_transport_unload(punit);
  }

  agents_unit_remove(punit);
  editgui_notify_object_changed(OBJTYPE_UNIT, punit->id, TRUE);
  client_remove_unit(punit);

  if (!client_has_player() || powner == client_player()) {
    if (need_economy_report_update) {
      economy_report_dialog_update();
    }
    units_report_dialog_update();
  }
}

/****************************************************************************
  The tile (x,y) has been nuked!
****************************************************************************/
void handle_nuke_tile_info(int tile)
{
  put_nuke_mushroom_pixmaps(index_to_tile(tile));
}

/****************************************************************************
  The name of team 'team_id'
****************************************************************************/
void handle_team_name_info(int team_id, const char *team_name)
{
  struct team_slot *tslot = team_slot_by_number(team_id);

  fc_assert_ret(NULL != tslot);
  team_slot_set_defined_name(tslot, team_name);
  conn_list_dialog_update();
}

/****************************************************************************
  A combat packet.  The server tells us the attacker and defender as well
  as both of their hitpoints after the combat is over (in most combat, one
  unit always dies and their HP drops to zero).  If make_winner_veteran is
  set then the surviving unit becomes veteran.
****************************************************************************/
void handle_unit_combat_info(int attacker_unit_id, int defender_unit_id,
			     int attacker_hp, int defender_hp,
			     bool make_winner_veteran)
{
  bool show_combat = FALSE;
  struct unit *punit0 = game_unit_by_number(attacker_unit_id);
  struct unit *punit1 = game_unit_by_number(defender_unit_id);

  if (punit0 && punit1) {
    if (tile_visible_mapcanvas(unit_tile(punit0)) &&
	tile_visible_mapcanvas(unit_tile(punit1))) {
      show_combat = TRUE;
    } else if (auto_center_on_combat) {
      if (unit_owner(punit0) == client.conn.playing)
	center_tile_mapcanvas(unit_tile(punit0));
      else
	center_tile_mapcanvas(unit_tile(punit1));
      show_combat = TRUE;
    }

    if (show_combat) {
      int hp0 = attacker_hp, hp1 = defender_hp;

      audio_play_sound(unit_type(punit0)->sound_fight,
		       unit_type(punit0)->sound_fight_alt);
      audio_play_sound(unit_type(punit1)->sound_fight,
		       unit_type(punit1)->sound_fight_alt);

      if (smooth_combat_step_msec > 0) {
	decrease_unit_hp_smooth(punit0, hp0, punit1, hp1);
      } else {
	punit0->hp = hp0;
	punit1->hp = hp1;

	set_units_in_combat(NULL, NULL);
	refresh_unit_mapcanvas(punit0, unit_tile(punit0), TRUE, FALSE);
	refresh_unit_mapcanvas(punit1, unit_tile(punit1), TRUE, FALSE);
      }
    }
    if (make_winner_veteran) {
      struct unit *pwinner = (defender_hp == 0 ? punit0 : punit1);

      pwinner->veteran++;
      refresh_unit_mapcanvas(pwinner, unit_tile(pwinner), TRUE, FALSE);
    }
  }
}

/**************************************************************************
  Updates a city's list of improvements from packet data.
  "have_impr" specifies whether the improvement should be added (TRUE)
  or removed (FALSE). Returns TRUE if the improvement has been actually
  added or removed.
**************************************************************************/
static bool update_improvement_from_packet(struct city *pcity,
                                           struct impr_type *pimprove,
                                           bool have_impr)
{
  if (have_impr) {
    if (pcity->built[improvement_index(pimprove)].turn <= I_NEVER) {
      city_add_improvement(pcity, pimprove);
      return TRUE;
    }
  } else {
    if (pcity->built[improvement_index(pimprove)].turn > I_NEVER) {
      city_remove_improvement(pcity, pimprove);
      return TRUE;
    }
  }
  return FALSE;
}

/****************************************************************************
  A city-info packet contains all information about a city.  If we receive
  this packet then we know everything about the city internals.
****************************************************************************/
void handle_city_info(const struct packet_city_info *packet)
{
  struct universal product;
  int caravan_city_id;
  int i;
  bool popup;
  bool city_is_new = FALSE;
  bool city_has_changed_owner = FALSE;
  bool need_science_dialog_update = FALSE;
  bool need_units_dialog_update = FALSE;
  bool need_economy_dialog_update = FALSE;
  bool name_changed = FALSE;
  bool update_descriptions = FALSE;
  bool shield_stock_changed = FALSE;
  bool production_changed = FALSE;
  bool trade_routes_changed = FALSE;
  struct unit_list *pfocus_units = get_units_in_focus();
  struct city *pcity = game_city_by_number(packet->id);
  struct tile_list *worked_tiles = NULL;
  struct tile *pcenter = index_to_tile(packet->tile);
  struct tile *ptile = NULL;
  struct player *powner = player_by_number(packet->owner);

  fc_assert_ret_msg(NULL != powner, "Bad player number %d.", packet->owner);
  fc_assert_ret_msg(NULL != pcenter, "Invalid tile index %d.", packet->tile);

  if (!universals_n_is_valid(packet->production_kind)) {
    log_error("handle_city_info() bad production_kind %d.",
              packet->production_kind);
    product.kind = VUT_NONE;
  } else {
    product = universal_by_number(packet->production_kind,
                                  packet->production_value);
    if (!universals_n_is_valid(product.kind)) {
      log_error("handle_city_info() "
                "production_kind %d with bad production_value %d.",
                packet->production_kind, packet->production_value);
      product.kind = VUT_NONE;
    }
  }

  if (NULL != pcity) {
    ptile = city_tile(pcity);

    if (NULL == ptile) {
      /* invisible worked city */
      city_list_remove(invisible.cities, pcity);
      city_is_new = TRUE;

      pcity->tile = pcenter;
      ptile = pcenter;
      pcity->owner = powner;
      pcity->original = powner;
    } else if (city_owner(pcity) != powner) {
      /* Remember what were the worked tiles.  The server won't
       * send to us again. */
      city_tile_iterate_skip_free_worked(city_map_radius_sq_get(pcity),
                                         ptile, pworked, _index, _x, _y) {
        if (pcity == tile_worked(pworked)) {
          if (NULL == worked_tiles) {
            worked_tiles = tile_list_new();
          }
          tile_list_append(worked_tiles, pworked);
        }
      } city_tile_iterate_skip_free_worked_end;
      client_remove_city(pcity);
      pcity = NULL;
      city_has_changed_owner = TRUE;
    }
  }

  if (NULL == pcity) {
    city_is_new = TRUE;
    pcity = create_city_virtual(powner, pcenter, packet->name);
    pcity->id = packet->id;
    idex_register_city(pcity);
    update_descriptions = TRUE;
  } else if (pcity->id != packet->id) {
    log_error("handle_city_info() city id %d != id %d.",
              pcity->id, packet->id);
    return;
  } else if (ptile != pcenter) {
    log_error("handle_city_info() city tile (%d, %d) != (%d, %d).",
              TILE_XY(ptile), TILE_XY(pcenter));
    return;
  } else {
    name_changed = (0 != strncmp(packet->name, pcity->name,
                                 sizeof(pcity->name)));
    /* pcity->trade_value doesn't change the city description, neither the
     * trade routes lines. */
    trade_routes_changed = (draw_city_trade_routes
                            && 0 != memcmp(pcity->trade, packet->trade,
                                           sizeof(pcity->trade)));

    /* Descriptions should probably be updated if the
     * city name, production or time-to-grow changes.
     * Note that if either the food stock or surplus
     * have changed, the time-to-grow is likely to
     * have changed as well. */
    update_descriptions = (draw_city_names && name_changed)
      || (draw_city_productions
          && (!are_universals_equal(&pcity->production, &product)
              || pcity->surplus[O_SHIELD] != packet->surplus[O_SHIELD]
              || pcity->shield_stock != packet->shield_stock))
      || (draw_city_growth
          && (pcity->food_stock != packet->food_stock
              || pcity->surplus[O_FOOD] != packet->surplus[O_FOOD]))
      || (draw_city_trade_routes && trade_routes_changed);
  }
  
  sz_strlcpy(pcity->name, packet->name);
  
  /* check data */
  city_size_set(pcity, 0);
  for (i = 0; i < FEELING_LAST; i++) {
    pcity->feel[CITIZEN_HAPPY][i] = packet->ppl_happy[i];
    pcity->feel[CITIZEN_CONTENT][i] = packet->ppl_content[i];
    pcity->feel[CITIZEN_UNHAPPY][i] = packet->ppl_unhappy[i];
    pcity->feel[CITIZEN_ANGRY][i] = packet->ppl_angry[i];
  }
  for (i = 0; i < CITIZEN_LAST; i++) {
    city_size_add(pcity, pcity->feel[i][FEELING_FINAL]);
  }
  specialist_type_iterate(sp) {
    pcity->specialists[sp] = packet->specialists[sp];
    city_size_add(pcity, pcity->specialists[sp]);
  } specialist_type_iterate_end;

  if (city_size_get(pcity) != packet->size) {
    log_error("handle_city_info() "
              "%d citizens not equal %d city size in \"%s\".",
              city_size_get(pcity), packet->size, city_name(pcity));
    city_size_set(pcity, packet->size);
  }

  /* The nationality of the citizens. */
  if (game.info.citizen_nationality == TRUE) {
    citizens_init(pcity);
    for (i = 0; i < packet->nationalities_count; i++) {
      citizens_nation_set(pcity, player_slot_by_number(packet->nation_id[i]),
                          packet->nation_citizens[i]);
    }
    fc_assert(citizens_count(pcity) == city_size_get(pcity));
  }

  pcity->city_radius_sq = packet->city_radius_sq;

  pcity->city_options = packet->city_options;

  for (i = 0; i < MAX_TRADE_ROUTES; i++) {
    pcity->trade[i] = packet->trade[i];
    pcity->trade_value[i] = packet->trade_value[i];
  }

  if (pcity->surplus[O_SCIENCE] != packet->surplus[O_SCIENCE]
      || pcity->surplus[O_SCIENCE] != packet->surplus[O_SCIENCE]
      || pcity->waste[O_SCIENCE] != packet->waste[O_SCIENCE]
      || (pcity->unhappy_penalty[O_SCIENCE]
          != packet->unhappy_penalty[O_SCIENCE])
      || pcity->prod[O_SCIENCE] != packet->prod[O_SCIENCE]
      || pcity->citizen_base[O_SCIENCE] != packet->citizen_base[O_SCIENCE]
      || pcity->usage[O_SCIENCE] != packet->usage[O_SCIENCE]) {
    need_science_dialog_update = TRUE;
  }

  pcity->food_stock=packet->food_stock;
  if (pcity->shield_stock != packet->shield_stock) {
    shield_stock_changed = TRUE;
    pcity->shield_stock = packet->shield_stock;
  }
  pcity->pollution = packet->pollution;
  pcity->illness_trade = packet->illness_trade;

  if (!are_universals_equal(&pcity->production, &product)) {
    production_changed = TRUE;
  }
  /* Need to consider shield stock/surplus for unit dialog as used build
   * slots may change, affecting number of "in-progress" units. */
  if ((city_is_new && VUT_UTYPE == product.kind)
      || (production_changed && (VUT_UTYPE == pcity->production.kind
                                 || VUT_UTYPE == product.kind))
      || pcity->surplus[O_SHIELD] != packet->surplus[O_SHIELD]
      || shield_stock_changed) {
    need_units_dialog_update = TRUE;
  }
  pcity->production = product;

  output_type_iterate(o) {
    pcity->surplus[o] = packet->surplus[o];
    pcity->waste[o] = packet->waste[o];
    pcity->unhappy_penalty[o] = packet->unhappy_penalty[o];
    pcity->prod[o] = packet->prod[o];
    pcity->citizen_base[o] = packet->citizen_base[o];
    pcity->usage[o] = packet->usage[o];
  } output_type_iterate_end;

#ifdef DONE_BY_create_city_virtual
  if (city_is_new) {
    worklist_init(&pcity->worklist);

    for (i = 0; i < ARRAY_SIZE(pcity->built); i++) {
      pcity->built[i].turn = I_NEVER;
    }
  }
#endif /* DONE_BY_create_city_virtual */

  worklist_copy(&pcity->worklist, &packet->worklist);

  pcity->airlift = packet->airlift;
  pcity->did_buy=packet->did_buy;
  pcity->did_sell=packet->did_sell;
  pcity->was_happy=packet->was_happy;

  pcity->turn_founded = packet->turn_founded;
  pcity->turn_last_built = packet->turn_last_built;

  if (!universals_n_is_valid(packet->changed_from_kind)) {
    log_error("handle_city_info() bad changed_from_kind %d.",
              packet->changed_from_kind);
    product.kind = VUT_NONE;
  } else {
    product = universal_by_number(packet->changed_from_kind,
                                     packet->changed_from_value);
    if (!universals_n_is_valid(product.kind)) {
      log_error("handle_city_info() bad changed_from_value %d.",
                packet->changed_from_value);
      product.kind = VUT_NONE;
    }
  }
  pcity->changed_from = product;

  pcity->before_change_shields=packet->before_change_shields;
  pcity->disbanded_shields=packet->disbanded_shields;
  pcity->caravan_shields=packet->caravan_shields;
  pcity->last_turns_shield_surplus = packet->last_turns_shield_surplus;

  improvement_iterate(pimprove) {
    bool have = BV_ISSET(packet->improvements, improvement_index(pimprove));

    if (have && !city_is_new
        && pcity->built[improvement_index(pimprove)].turn <= I_NEVER) {
      audio_play_sound(pimprove->soundtag, pimprove->soundtag_alt);
    }
    need_economy_dialog_update |=
        update_improvement_from_packet(pcity, pimprove, have);
  } improvement_iterate_end;

  /* We should be able to see units in the city.  But for a diplomat
   * investigating an enemy city we can't.  In that case we don't update
   * the occupied flag at all: it's already been set earlier and we'll
   * get an update if it changes. */
  if (can_player_see_units_in_city(client.conn.playing, pcity)) {
    pcity->client.occupied
      = (unit_list_size(pcity->tile->units) > 0);
  }

  pcity->client.walls = packet->walls;

  pcity->client.happy = city_happy(pcity);
  pcity->client.unhappy = city_unhappy(pcity);

  popup = (city_is_new && can_client_change_view()
           && powner == client.conn.playing
           && popup_new_cities)
          || packet->diplomat_investigate;

  city_packet_common(pcity, pcenter, powner, worked_tiles,
                     city_is_new, popup, packet->diplomat_investigate);

  if (city_is_new && !city_has_changed_owner) {
    agents_city_new(pcity);
  } else {
    agents_city_changed(pcity);
  }

  /* Update the description if necessary. */
  if (update_descriptions) {
    update_city_description(pcity);
  }

  /* Update focus unit info label if necessary. */
  if (name_changed) {
    unit_list_iterate(pfocus_units, pfocus_unit) {
      if (pfocus_unit->homecity == pcity->id) {
	update_unit_info_label(pfocus_units);
	break;
      }
    } unit_list_iterate_end;
  }

  /* Update the science dialog if necessary. */
  if (need_science_dialog_update) {
    science_report_dialog_update();
  }

  /* Update the units dialog if necessary. */
  if (need_units_dialog_update) {
    units_report_dialog_update();
  }

  /* Update the economy dialog if necessary. */
  if (need_economy_dialog_update) {
    economy_report_dialog_update();
  }

  /* Update the panel text (including civ population). */
  update_info_label();
  
  /* update caravan dialog */
  if ((production_changed || shield_stock_changed)
      && caravan_dialog_is_open(NULL, &caravan_city_id)
      && caravan_city_id == pcity->id) {
    caravan_dialog_update();
  }

  if (draw_city_trade_routes
      && (trade_routes_changed
          || (city_is_new && 0 < city_num_trade_routes(pcity)))) {
    update_map_canvas_visible();
  }
}

/****************************************************************************
  A helper function for handling city-info and city-short-info packets.
  Naturally, both require many of the same operations to be done on the
  data.
****************************************************************************/
static void city_packet_common(struct city *pcity, struct tile *pcenter,
                               struct player *powner,
                               struct tile_list *worked_tiles,
                               bool is_new, bool popup, bool investigate)
{
  if (NULL != worked_tiles) {
    /* We need to transfer the worked infos because the server will assume
     * those infos are kept in our side and won't send to us again. */
    tile_list_iterate(worked_tiles, pwork) {
      tile_set_worked(pwork, pcity);
    } tile_list_iterate_end;
    tile_list_destroy(worked_tiles);
  }

  if (is_new) {
    tile_set_worked(pcenter, pcity); /* is_free_worked() */
    city_list_prepend(powner->cities, pcity);

    if (client_is_global_observer() || powner == client_player()) {
      city_report_dialog_update();
    }

    players_iterate(pp) {
      unit_list_iterate(pp->units, punit) 
	if(punit->homecity==pcity->id)
	  unit_list_prepend(pcity->units_supported, punit);
      unit_list_iterate_end;
    } players_iterate_end;
  } else {
    if (client_is_global_observer() || powner == client_player()) {
      city_report_dialog_update_city(pcity);
    }
  }

  if (can_client_change_view()) {
    refresh_city_mapcanvas(pcity, pcenter, FALSE, FALSE);
  }

  if (city_workers_display==pcity)  {
    city_workers_display=NULL;
  }

  if (popup
      && NULL != client.conn.playing
      && !client.conn.playing->ai_controlled
      && can_client_issue_orders()) {
    menus_update();
    if (!city_dialog_is_open(pcity)) {
      popup_city_dialog(pcity);
    }
  }

  if (!is_new
      && (popup || can_player_see_city_internals(client.conn.playing, pcity))) {
    refresh_city_dialog(pcity);
  }

  /* update menus if the focus unit is on the tile. */
  if (get_focus_unit_on_tile(pcenter)) {
    menus_update();
  }

  if (is_new) {
    log_debug("(%d,%d) creating city %d, %s %s", TILE_XY(pcenter),
              pcity->id, nation_rule_name(nation_of_city(pcity)),
              city_name(pcity));
  }

  editgui_notify_object_changed(OBJTYPE_CITY, pcity->id, FALSE);
}

/****************************************************************************
  A city-short-info packet is sent to tell us about any cities we can't see
  the internals of.  Most of the time this includes any cities owned by
  someone else.
****************************************************************************/
void handle_city_short_info(const struct packet_city_short_info *packet)
{
  bool city_has_changed_owner = FALSE;
  bool city_is_new = FALSE;
  bool name_changed = FALSE;
  bool update_descriptions = FALSE;
  struct city *pcity = game_city_by_number(packet->id);
  struct tile *pcenter = index_to_tile(packet->tile);
  struct tile *ptile = NULL;
  struct tile_list *worked_tiles = NULL;
  struct player *powner = player_by_number(packet->owner);
  int radius_sq = game.info.init_city_radius_sq;

  fc_assert_ret_msg(NULL != powner, "Bad player number %d.", packet->owner);
  fc_assert_ret_msg(NULL != pcenter, "Invalid tile index %d.", packet->tile);

  if (NULL != pcity) {
    ptile = city_tile(pcity);

    if (NULL == ptile) {
      /* invisible worked city */
      city_list_remove(invisible.cities, pcity);
      city_is_new = TRUE;

      pcity->tile = pcenter;
      ptile = pcenter;
      pcity->owner = powner;
      pcity->original = powner;

      whole_map_iterate(ptile) {
        if (ptile->worked == pcity) {
          int dist_sq = sq_map_distance(pcenter, ptile);
          if (dist_sq > city_map_radius_sq_get(pcity)) {
            city_map_radius_sq_set(pcity, dist_sq);
          }
        }
      } whole_map_iterate_end;
    } else if (city_owner(pcity) != powner) {
      /* Remember what were the worked tiles.  The server won't
       * send to us again. */
      city_tile_iterate_skip_free_worked(city_map_radius_sq_get(pcity), ptile,
                                         pworked, _index, _x, _y) {
        if (pcity == tile_worked(pworked)) {
          if (NULL == worked_tiles) {
            worked_tiles = tile_list_new();
          }
          tile_list_append(worked_tiles, pworked);
        }
      } city_tile_iterate_skip_free_worked_end;
      radius_sq = city_map_radius_sq_get(pcity);
      client_remove_city(pcity);
      pcity = NULL;
      city_has_changed_owner = TRUE;
    }
  }

  if (NULL == pcity) {
    city_is_new = TRUE;
    pcity = create_city_virtual(powner, pcenter, packet->name);
    pcity->id = packet->id;
    city_map_radius_sq_set(pcity, radius_sq);
    idex_register_city(pcity);
  } else if (pcity->id != packet->id) {
    log_error("handle_city_short_info() city id %d != id %d.",
              pcity->id, packet->id);
    return;
  } else if (city_tile(pcity) != pcenter) {
    log_error("handle_city_short_info() city tile (%d, %d) != (%d, %d).",
              TILE_XY(city_tile(pcity)), TILE_XY(pcenter));
    return;
  } else {
    name_changed = (0 != strncmp(packet->name, pcity->name,
                                 sizeof(pcity->name)));

    /* Check if city descriptions should be updated */
    if (draw_city_names && name_changed) {
      update_descriptions = TRUE;
    }

    sz_strlcpy(pcity->name, packet->name);
    
    memset(pcity->feel, 0, sizeof(pcity->feel));
    memset(pcity->specialists, 0, sizeof(pcity->specialists));
  }

  pcity->specialists[DEFAULT_SPECIALIST] = packet->size;
  city_size_set(pcity, packet->size);

  /* We can't actually see the internals of the city, but the server tells
   * us this much. */
  if (pcity->client.occupied != packet->occupied) {
    pcity->client.occupied = packet->occupied;
    if (draw_full_citybar) {
      update_descriptions = TRUE;
    }
  }
  pcity->client.walls = packet->walls;

  pcity->client.happy = packet->happy;
  pcity->client.unhappy = packet->unhappy;

  improvement_iterate(pimprove) {
    /* Don't update the non-visible improvements, they could hide the
     * previously seen informations about the city (diplomat investigation).
     */
    if (is_improvement_visible(pimprove)) {
      bool have = BV_ISSET(packet->improvements,
                           improvement_index(pimprove));
      update_improvement_from_packet(pcity, pimprove, have);
    }
  } improvement_iterate_end;

  city_packet_common(pcity, pcenter, powner, worked_tiles,
                     city_is_new, FALSE, FALSE);

  if (city_is_new && !city_has_changed_owner) {
    agents_city_new(pcity);
  } else {
    agents_city_changed(pcity);
  }

  /* Update the description if necessary. */
  if (update_descriptions) {
    update_city_description(pcity);
  }
}

/**************************************************************************
  Handle turn and year advancement.
**************************************************************************/
void handle_new_year(int year, int turn)
{
  game.info.year = year;
  /*
   * The turn was increased in handle_before_new_year()
   */
  fc_assert(game.info.turn == turn);
  update_info_label();

  unit_focus_update();
  auto_center_on_focus_unit();

  update_unit_info_label(get_units_in_focus());
  menus_update();

  set_seconds_to_turndone(game.info.timeout);

#if 0
  /* This information shouldn't be needed, but if it is this is the only
   * way we can get it. */
  if (NULL != client.conn.playing) {
    turn_gold_difference =
      client.conn.playing->economic.gold - last_turn_gold_amount;
    last_turn_gold_amount = client.conn.playing->economic.gold;
  }
#endif

  update_city_descriptions();
  link_marks_decrease_turn_counters();

  if (sound_bell_at_new_turn) {
    create_event(NULL, E_TURN_BELL, ftc_client,
                 _("Start of turn %d"), game.info.turn);
  }

  agents_new_turn();
}

/**************************************************************************
  Called by the network code when an end-phase packet is received.  This
  signifies the end of our phase (it's not sent for other player's
  phases).
**************************************************************************/
void handle_end_phase(void)
{
  meswin_clear();
  /*
   * The local idea of the game.info.turn is increased here since the
   * client will get unit updates (reset of move points for example)
   * between handle_before_new_year() and handle_new_year(). These
   * unit updates will look like they did take place in the old turn
   * which is incorrect. If we get the authoritative information about
   * the game.info.turn in handle_new_year() we will check it.
   */
  game.info.turn++;
  agents_before_new_turn();
}

/**************************************************************************
  Called by the network code when an start-phase packet is received.  This
  may be the start of our phase or someone else's phase.
**************************************************************************/
void handle_start_phase(int phase)
{
  if (!client_has_player() && !client_is_observer()) {
    /* We are on detached state, let ignore this packet. */
    return;
  }

  if (phase < 0
      || (game.info.phase_mode == PMT_PLAYERS_ALTERNATE
          && phase >= player_count())
      || (game.info.phase_mode == PMT_TEAMS_ALTERNATE
          && phase >= team_count())) {
    log_error("handle_start_phase() illegal phase %d.", phase);
    return;
  }

  set_client_state(C_S_RUNNING);

  game.info.phase = phase;

  if (NULL != client.conn.playing
      && is_player_phase(client.conn.playing, phase)) {
    agents_start_turn();
    non_ai_unit_focus = FALSE;

    update_turn_done_button_state();

    if (client.conn.playing->ai_controlled && !ai_manual_turn_done) {
      user_ended_turn();
    }

    unit_focus_set_status(client.conn.playing);

    city_list_iterate(client.conn.playing->cities, pcity) {
      pcity->client.colored = FALSE;
    } city_list_iterate_end;

    unit_list_iterate(client.conn.playing->units, punit) {
      punit->client.colored = FALSE;
    } unit_list_iterate_end;

    update_map_canvas_visible();
  }

  update_info_label();
}

/**************************************************************************
  Called when begin-turn packet is received. Server has finished processing
  turn change.
**************************************************************************/
void handle_begin_turn(void)
{
  log_debug("handle_begin_turn()");

  /* Possibly replace wait cursor with something else */
  set_server_busy(FALSE);
}

/**************************************************************************
  Called when end-turn packet is received. Server starts processing turn
  change.
**************************************************************************/
void handle_end_turn(void)
{
  log_debug("handle_end_turn()");

  /* Make sure wait cursor is in use */
  set_server_busy(TRUE);
}

/**************************************************************************
  Plays sound associated with event
**************************************************************************/
void play_sound_for_event(enum event_type type)
{
  const char *sound_tag = get_event_sound_tag(type);

  if (sound_tag) {
    audio_play_sound(sound_tag, NULL);
  }
}  
  
/**************************************************************************
  Handle a message packet.  This includes all messages - both
  in-game messages and chats from other players.
**************************************************************************/
void handle_chat_msg(const char *message, int tile,
                     enum event_type event, int conn_id)
{
  handle_event(message, index_to_tile(tile), event, conn_id);
}

/**************************************************************************
  Handle a connect message packet. Server sends connect message to
  client immediately when client connects.
**************************************************************************/
void handle_connect_msg(const char *message)
{
  popup_connect_msg(_("Welcome"), message);
}

/****************************************************************************
  Packet page_msg handler.
****************************************************************************/
void handle_page_msg(const char *caption, const char *headline,
                     const char *lines, enum event_type event)
{
  if (!client_has_player()
      || !client_player()->ai_controlled
      || event != E_BROADCAST_REPORT) {
    popup_notify_dialog(caption, headline, lines);
    play_sound_for_event(event);
  }
}

/****************************************************************************
  Packet unit_info.
****************************************************************************/
void handle_unit_info(const struct packet_unit_info *packet)
{
  struct unit *punit;

  punit = unpackage_unit(packet);
  if (handle_unit_packet_common(punit)) {
    free(punit);
  }
}

/**************************************************************************
  Called to do basic handling for a unit_info or short_unit_info packet.

  Both owned and foreign units are handled; you may need to check unit
  owner, or if unit equals focus unit, depending on what you are doing.

  Note: Normally the server informs client about a new "activity" here.
  For owned units, the new activity can be a result of:
  - The player issued a command (a request) with the client.
  - The server side AI did something.
  - An enemy encounter caused a sentry to idle. (See "Wakeup Focus").

  Depending on what caused the change, different actions may be taken.
  Therefore, this function is a bit of a jungle, and it is advisable
  to read thoroughly before changing.

  Exception: When the client puts a unit in focus, it's status is set to
  idle immediately, before informing the server about the new status. This
  is because the server can never deny a request for idle, and should not
  be concerned about which unit the client is focusing on.
**************************************************************************/
static bool handle_unit_packet_common(struct unit *packet_unit)
{
  struct city *pcity;
  struct unit *punit;
  bool need_menus_update = FALSE;
  bool need_economy_report_update = FALSE;
  bool need_units_report_update = FALSE;
  bool repaint_unit = FALSE;
  bool repaint_city = FALSE;	/* regards unit's homecity */
  struct tile *old_tile = NULL;
  bool check_focus = FALSE;     /* conservative focus change */
  bool moved = FALSE;
  bool ret = FALSE;

  punit = player_unit_by_number(unit_owner(packet_unit), packet_unit->id);
  if (!punit && game_unit_by_number(packet_unit->id)) {
    /* This means unit has changed owner. We deal with this here
     * by simply deleting the old one and creating a new one. */
    handle_unit_remove(packet_unit->id);
  }

  if (punit) {
    ret = TRUE;
    punit->activity_count = packet_unit->activity_count;
    unit_change_battlegroup(punit, packet_unit->battlegroup);
    if (punit->ai_controlled != packet_unit->ai_controlled) {
      punit->ai_controlled = packet_unit->ai_controlled;
      repaint_unit = TRUE;
      /* AI is set:     may change focus */
      /* AI is cleared: keep focus */
      if (packet_unit->ai_controlled && unit_is_in_focus(punit)) {
        check_focus = TRUE;
      }
    }

    if (punit->facing != packet_unit->facing) {
      punit->facing = packet_unit->facing;
      repaint_unit = TRUE;
    }

    if (punit->activity != packet_unit->activity
        || cmp_act_tgt(&punit->activity_target, &packet_unit->activity_target)
        || punit->client.transported_by != packet_unit->client.transported_by
        || punit->client.occupied != packet_unit->client.occupied
	|| punit->has_orders != packet_unit->has_orders
	|| punit->orders.repeat != packet_unit->orders.repeat
	|| punit->orders.vigilant != packet_unit->orders.vigilant
	|| punit->orders.index != packet_unit->orders.index) {

      /*** Change in activity or activity's target. ***/

      /* May change focus if focus unit gets a new activity.
       * But if new activity is Idle, it means user specifically selected
       * the unit */
      if (unit_is_in_focus(punit)
	  && (packet_unit->activity != ACTIVITY_IDLE
	      || packet_unit->has_orders)) {
        check_focus = TRUE;
      }

      repaint_unit = TRUE;

      /* Wakeup Focus */
      if (wakeup_focus 
          && NULL != client.conn.playing
          && !client.conn.playing->ai_controlled
          && unit_owner(punit) == client.conn.playing
          && punit->activity == ACTIVITY_SENTRY
          && packet_unit->activity == ACTIVITY_IDLE
          && !unit_is_in_focus(punit)
          && is_player_phase(client.conn.playing, game.info.phase)) {
        /* many wakeup units per tile are handled */
        unit_focus_urgent(punit);
        check_focus = FALSE; /* and keep it */
      }

      punit->activity = packet_unit->activity;
      punit->activity_target = packet_unit->activity_target;

      if (punit->client.transported_by
          != packet_unit->client.transported_by) {
        if (packet_unit->client.transported_by == -1) {
          /* The unit was unloaded from its transport. The check for a new
           * transport is done below. */
          unit_transport_unload(punit);
        }

        punit->client.transported_by = packet_unit->client.transported_by;
      }

      if (punit->client.occupied != packet_unit->client.occupied) {
        if (get_focus_unit_on_tile(unit_tile(packet_unit))) {
          /* Special case: (un)loading a unit in a transporter on the same
           *tile as the focus unit may (dis)allow the focus unit to be
           * loaded.  Thus the orders->(un)load menu item needs updating. */
          need_menus_update = TRUE;
        }
        punit->client.occupied = packet_unit->client.occupied;
      }

      punit->has_orders = packet_unit->has_orders;
      punit->orders.length = packet_unit->orders.length;
      punit->orders.index = packet_unit->orders.index;
      punit->orders.repeat = packet_unit->orders.repeat;
      punit->orders.vigilant = packet_unit->orders.vigilant;

      /* We cheat by just stealing the packet unit's list. */
      if (punit->orders.list) {
	free(punit->orders.list);
      }
      punit->orders.list = packet_unit->orders.list;
      packet_unit->orders.list = NULL;

      if (NULL == client.conn.playing
          || unit_owner(punit) == client.conn.playing) {
        refresh_unit_city_dialogs(punit);
      }
    } /*** End of Change in activity or activity's target. ***/

    /* These two lines force the menus to be updated as appropriate when
     * the focus unit changes. */
    if (unit_is_in_focus(punit)) {
      need_menus_update = TRUE;
    }

    if (punit->homecity != packet_unit->homecity) {
      /* change homecity */
      struct city *pcity;
      if ((pcity = game_city_by_number(punit->homecity))) {
	unit_list_remove(pcity->units_supported, punit);
	refresh_city_dialog(pcity);
      }
      
      punit->homecity = packet_unit->homecity;
      if ((pcity = game_city_by_number(punit->homecity))) {
	unit_list_prepend(pcity->units_supported, punit);
	repaint_city = TRUE;
      }

      /* This can change total upkeep figures */
      need_units_report_update = TRUE;
    }

    if (punit->hp != packet_unit->hp) {
      /* hp changed */
      punit->hp = packet_unit->hp;
      repaint_unit = TRUE;
    }

    if (punit->utype != unit_type(packet_unit)) {
      /* Unit type has changed (been upgraded) */
      struct city *pcity = tile_city(unit_tile(punit));
      
      punit->utype = unit_type(packet_unit);
      repaint_unit = TRUE;
      repaint_city = TRUE;
      if (pcity && (pcity->id != punit->homecity)) {
	refresh_city_dialog(pcity);
      }
      if (unit_is_in_focus(punit)) {
        /* Update the orders menu -- the unit might have new abilities */
        need_menus_update = TRUE;
      }
      need_units_report_update = TRUE;
    }

    /* May change focus if an attempted move or attack exhausted unit */
    if (punit->moves_left != packet_unit->moves_left
        && unit_is_in_focus(punit)) {
      check_focus = TRUE;
    }

    if (!same_pos(unit_tile(punit), unit_tile(packet_unit))) {
      /*** Change position ***/
      struct city *pcity = tile_city(unit_tile(punit));

      old_tile = unit_tile(punit);
      moved = TRUE;

      /* Show where the unit is going. */
      do_move_unit(punit, packet_unit);

      if(pcity)  {
	if (can_player_see_units_in_city(client.conn.playing, pcity)) {
	  /* Unit moved out of a city - update the occupied status. */
	  bool new_occupied =
	    (unit_list_size(pcity->tile->units) > 0);

          if (pcity->client.occupied != new_occupied) {
            pcity->client.occupied = new_occupied;
            refresh_city_mapcanvas(pcity, pcity->tile, FALSE, FALSE);
            if (draw_full_citybar) {
              update_city_description(pcity);
            }
          }
        }

        if(pcity->id==punit->homecity)
	  repaint_city = TRUE;
	else
	  refresh_city_dialog(pcity);
      }
      
      if ((pcity = tile_city(unit_tile(punit)))) {
        if (can_player_see_units_in_city(client.conn.playing, pcity)) {
          /* Unit moved into a city - obviously it's occupied. */
          if (!pcity->client.occupied) {
            pcity->client.occupied = TRUE;
            refresh_city_mapcanvas(pcity, pcity->tile, FALSE, FALSE);
            if (draw_full_citybar) {
              update_city_description(pcity);
            }
          }
        }

        if(pcity->id == punit->homecity)
	  repaint_city = TRUE;
	else
	  refresh_city_dialog(pcity);

        if (popup_caravan_arrival
            && client_has_player() 
            && !client_player()->ai_controlled
            && can_client_issue_orders()
            && !unit_has_orders(punit)) {
          if (!unit_transported(punit)
              && client_player() == unit_owner(punit)
              && (unit_can_help_build_wonder_here(punit)
                  || unit_can_est_trade_route_here(punit))) {
            process_caravan_arrival(punit);
          }
          /* Check for transported units. */
          unit_list_iterate(unit_transport_cargo(punit), pcargo) {
            if (client_player() == unit_owner(pcargo)
                && !unit_has_orders(pcargo)
                && (unit_can_help_build_wonder_here(pcargo)
                    || unit_can_est_trade_route_here(pcargo))) {
              process_caravan_arrival(pcargo);
            }
          } unit_list_iterate_end;
        }
      }

    }  /*** End of Change position. ***/

    if (repaint_city || repaint_unit) {
      /* We repaint the city if the unit itself needs repainting or if
       * there is a special city-only redrawing to be done. */
      if ((pcity = game_city_by_number(punit->homecity))) {
	refresh_city_dialog(pcity);
      }
      if (repaint_unit && tile_city(unit_tile(punit))
          && tile_city(unit_tile(punit)) != pcity) {
        /* Refresh the city we're occupying too. */
        refresh_city_dialog(tile_city(unit_tile(punit)));
      }
    }

    need_economy_report_update = (punit->upkeep[O_GOLD]
                                  != packet_unit->upkeep[O_GOLD]);
    /* unit upkeep information */
    output_type_iterate(o) {
      punit->upkeep[o] = packet_unit->upkeep[o];
    } output_type_iterate_end;

    punit->veteran = packet_unit->veteran;
    punit->moves_left = packet_unit->moves_left;
    punit->fuel = packet_unit->fuel;
    punit->goto_tile = packet_unit->goto_tile;
    punit->paradropped = packet_unit->paradropped;
    if (punit->done_moving != packet_unit->done_moving) {
      punit->done_moving = packet_unit->done_moving;
      check_focus = TRUE;
    }

    /* This won't change punit; it enqueues the call for later handling. */
    agents_unit_changed(punit);
    editgui_notify_object_changed(OBJTYPE_UNIT, punit->id, FALSE);
  } else {
    /*** Create new unit ***/
    punit = packet_unit;
    idex_register_unit(punit);

    unit_list_prepend(unit_owner(punit)->units, punit);
    unit_list_prepend(unit_tile(punit)->units, punit);

    unit_register_battlegroup(punit);

    if ((pcity = game_city_by_number(punit->homecity))) {
      unit_list_prepend(pcity->units_supported, punit);
    }

    log_debug("New %s %s id %d (%d %d) hc %d %s",
              nation_rule_name(nation_of_unit(punit)),
              unit_rule_name(punit), TILE_XY(unit_tile(punit)),
              punit->id, punit->homecity,
              (pcity ? city_name(pcity) : "(unknown)"));

    repaint_unit = !unit_transported(punit);
    agents_unit_new(punit);

    if ((pcity = tile_city(unit_tile(punit)))) {
      /* The unit is in a city - obviously it's occupied. */
      pcity->client.occupied = TRUE;
    }

    if (punit->client.transported_by != -1) {
      punit->client.transported_by = packet_unit->client.transported_by;
    }

    need_units_report_update = TRUE;
  } /*** End of Create new unit ***/

  /* Check if we have to load the unit on a transporter. */
  if (punit->client.transported_by != -1) {
    struct unit *ptrans
      = game_unit_by_number(packet_unit->client.transported_by);

    /* Load unit only if transporter is known by the client. For full
     * unit info the transporter should be known. See recursive sending
     * of transporter information in send_unit_info_to_onlookers(). */
    if (ptrans && ptrans != unit_transport_get(punit)) {
      /* First, we have to unload the unit from its old transporter. */
      unit_transport_unload(punit);
      unit_transport_load(punit, ptrans, TRUE);
#ifdef DEBUG_TRANSPORT
      log_debug("load %s (ID: %d) onto %s (ID: %d)",
                unit_name_translation(punit), punit->id,
                unit_name_translation(ptrans), ptrans->id);
    } else if (ptrans && ptrans == unit_transport_get(punit)) {
      log_debug("%s (ID: %d) is loaded onto %s (ID: %d)",
                unit_name_translation(punit), punit->id,
                unit_name_translation(ptrans), ptrans->id);
    } else {
      log_debug("%s (ID: %d) is not loaded", unit_name_translation(punit),
                punit->id);
#endif /* DEBUG_TRANSPORT */
    }
  }

  fc_assert_ret_val(punit != NULL, ret);

  if (unit_is_in_focus(punit)
      || get_focus_unit_on_tile(unit_tile(punit))
      || (moved && get_focus_unit_on_tile(old_tile))) {
    update_unit_info_label(get_units_in_focus());
    /* Update (an possible active) unit select dialog. */
    unit_select_dialog_update();
  }

  if (repaint_unit) {
    refresh_unit_mapcanvas(punit, unit_tile(punit), TRUE, FALSE);
  }

  if ((check_focus || get_num_units_in_focus() == 0)
      && NULL != client.conn.playing
      && !client.conn.playing->ai_controlled
      && is_player_phase(client.conn.playing, game.info.phase)) {
    unit_focus_update();
  }

  if (need_menus_update) {
    menus_update();
  }

  if (!client_has_player() || unit_owner(punit) == client_player()) {
    if (need_economy_report_update) {
      economy_report_dialog_update();
    }
    if (need_units_report_update) {
      units_report_dialog_update();
    }
  }

  return ret;
}

/****************************************************************************
  Receive a short_unit info packet.
****************************************************************************/
void handle_unit_short_info(const struct packet_unit_short_info *packet)
{
  struct city *pcity;
  struct unit *punit;

  if (packet->goes_out_of_sight) {
    punit = game_unit_by_number(packet->id);
    if (punit) {
      client_remove_unit(punit);
    }
    return;
  }

  /* Special case for a diplomat/spy investigating a city: The investigator
   * needs to know the supported and present units of a city, whether or not
   * they are fogged. So, we send a list of them all before sending the city
   * info. */
  if (packet->packet_use == UNIT_INFO_CITY_SUPPORTED
      || packet->packet_use == UNIT_INFO_CITY_PRESENT) {
    static int last_serial_num = 0;

    /* fetch city -- abort if not found */
    pcity = game_city_by_number(packet->info_city_id);
    if (!pcity) {
      return;
    }

    /* New serial number -- clear (free) everything */
    if (last_serial_num != packet->serial_num) {
      last_serial_num = packet->serial_num;
      unit_list_clear(pcity->client.info_units_supported);
      unit_list_clear(pcity->client.info_units_present);
    }

    /* Okay, append a unit struct to the proper list. */
    punit = unpackage_short_unit(packet);
    if (packet->packet_use == UNIT_INFO_CITY_SUPPORTED) {
      unit_list_prepend(pcity->client.info_units_supported, punit);
    } else {
      fc_assert(packet->packet_use == UNIT_INFO_CITY_PRESENT);
      unit_list_prepend(pcity->client.info_units_present, punit);
    }

    /* Done with special case. */
    return;
  }

  if (player_by_number(packet->owner) == client.conn.playing) {
    log_error("handle_unit_short_info() for own unit.");
  }

  punit = unpackage_short_unit(packet);
  if (handle_unit_packet_common(punit)) {
    free(punit);
  }
}

/****************************************************************************
  Receive information about the map size and topology from the server.  We
  initialize some global variables at the same time.
****************************************************************************/
void handle_map_info(int xsize, int ysize, int topology_id)
{
  if (!map_is_empty()) {
    map_free();
  }

  map.xsize = xsize;
  map.ysize = ysize;
  map.topology_id = topology_id;

  /* Parameter is FALSE so that sizes are kept unchanged. */
  map_init_topology(FALSE);

  map_allocate();
  client_player_maps_reset();
  init_client_goto();
  mapdeco_init();

  generate_citydlg_dimensions();

  calculate_overview_dimensions();

  packhand_init();
}

/****************************************************************************
  Packet game_info handler.
****************************************************************************/
void handle_game_info(const struct packet_game_info *pinfo)
{
  bool boot_help;
  bool update_aifill_button = FALSE;


  if (game.info.aifill != pinfo->aifill) {
    update_aifill_button = TRUE;
  }
  
  if (game.info.is_edit_mode != pinfo->is_edit_mode) {
    popdown_all_city_dialogs();
    /* Clears the current goto command. */
    set_hover_state(NULL, HOVER_NONE, ACTIVITY_LAST, NULL, ORDER_LAST);
  }

  game.info = *pinfo;

  /* check the values! */
#define VALIDATE(_count, _maximum, _string)                                 \
  if (game.info._count > _maximum) {                                        \
    log_error("handle_game_info(): Too many " _string "; using %d of %d",   \
              _maximum, game.info._count);                                  \
    game.info._count = _maximum;                                            \
  }

  VALIDATE(granary_num_inis,	MAX_GRANARY_INIS,	"granary entries");
#undef VALIDATE

  game.government_during_revolution =
    government_by_number(game.info.government_during_revolution_id);

  boot_help = (can_client_change_view()
	       && game.info.spacerace != pinfo->spacerace);
  if (game.info.timeout != 0 && pinfo->seconds_to_phasedone >= 0) {
    /* If this packet is received in the middle of a turn, this value
     * represents the number of seconds from now to the end of the turn
     * (not from the start of the turn). So we need to restart our
     * timer. */
    set_seconds_to_turndone(pinfo->seconds_to_phasedone);
  }
  if (boot_help) {
    boot_help_texts(client.conn.playing); /* reboot, after setting game.spacerace */
  }
  unit_focus_update();
  menus_update();
  players_dialog_update();
  if (update_aifill_button) {
    update_start_page();
  }
  
  if (can_client_change_view()) {
    update_info_label();
  }

  editgui_notify_object_changed(OBJTYPE_GAME, 1, FALSE);
}

/**************************************************************************
  Sets player inventions to values specified in inventions array
**************************************************************************/
static bool read_player_info_techs(struct player *pplayer,
                                   const char *inventions)
{
  bool need_effect_update = FALSE;

#ifdef DEBUG
  log_verbose("Player%d inventions:%s", player_number(pplayer), inventions);
#endif

  advance_index_iterate(A_NONE, i) {
    enum tech_state newstate = inventions[i] - '0';
    enum tech_state oldstate = player_invention_set(pplayer, i, newstate);

    if (newstate != oldstate
	&& (newstate == TECH_KNOWN || oldstate == TECH_KNOWN)) {
      need_effect_update = TRUE;
    }
  } advance_index_iterate_end;

  if (need_effect_update) {
    menus_update();
  }
  player_research_update(pplayer);
  return need_effect_update;
}

/**************************************************************************
  Sets the target government.  This will automatically start a revolution
  if the target government differs from the current one.
**************************************************************************/
void set_government_choice(struct government *government)
{
  if (NULL != client.conn.playing
      && can_client_issue_orders()
      && government != government_of_player(client.conn.playing)) {
    dsend_packet_player_change_government(&client.conn, government_number(government));
  }
}

/**************************************************************************
  Begin a revolution by telling the server to start it.  This also clears
  the current government choice.
**************************************************************************/
void start_revolution(void)
{
  dsend_packet_player_change_government(&client.conn,
				    game.info.government_during_revolution_id);
}

/**************************************************************************
  Handle a notification that the player slot identified by 'playerno' has
  become unused. If the slot is already unused, then just ignore. Otherwise
  update the total player count and the GUI.
**************************************************************************/
void handle_player_remove(int playerno)
{
  struct player_slot *pslot;
  struct player *pplayer;
  int plr_nbr;

  pslot = player_slot_by_number(playerno);

  if (NULL == pslot || !player_slot_is_used(pslot)) {
    /* Ok, just ignore. */
    return;
  }

  pplayer = player_slot_get_player(pslot);

  if (can_client_change_view()) {
    close_intel_dialog(pplayer);
  }

  /* Update the connection informations. */
  if (client_player() == pplayer) {
    client.conn.playing = NULL;
  }
  conn_list_iterate(pplayer->connections, pconn) {
    pconn->playing = NULL;
  } conn_list_iterate_end;
  conn_list_clear(pplayer->connections);

  /* Free the memory allocated for the player color. */
  tileset_player_free(tileset, pplayer);

  /* Save player number before player is freed */
  plr_nbr = player_number(pplayer);

  client_player_destroy(pplayer);
  player_destroy(pplayer);

  players_dialog_update();
  conn_list_dialog_update();

  editgui_refresh();
  editgui_notify_object_changed(OBJTYPE_PLAYER, plr_nbr, TRUE);
}

/****************************************************************************
  Handle information about a player. If the packet refers to a player slot
  that is not currently used, then this function will set that slot to
  used and update the total player count.
****************************************************************************/
void handle_player_info(const struct packet_player_info *pinfo)
{
  bool is_new_nation = FALSE;
  bool new_tech = FALSE;
  bool poptechup = FALSE;
  bool turn_done_changed = FALSE;
  bool new_player = FALSE;
  int i;
  struct player_research *research;
  struct player *pplayer, *my_player;
  struct nation_type *pnation;
  struct government *pgov, *ptarget_gov;
  struct player_slot *pslot;
  struct team_slot *tslot;

  /* Player. */
  pslot = player_slot_by_number(pinfo->playerno);
  fc_assert(NULL != pslot);
  new_player = !player_slot_is_used(pslot);
  pplayer = player_new(pslot);

  if (pplayer->rgb == NULL || (pplayer->rgb->r != pinfo->color_red
                               || pplayer->rgb->g != pinfo->color_green
                               || pplayer->rgb->b != pinfo->color_blue)) {
    struct rgbcolor *prgbcolor = rgbcolor_new(pinfo->color_red,
                                              pinfo->color_green,
                                              pinfo->color_blue);
    fc_assert_ret(prgbcolor != NULL);

    player_set_color(pplayer, prgbcolor);
    tileset_player_init(tileset, pplayer);

    rgbcolor_destroy(prgbcolor);
  }

  if (new_player) {
    /* Initialise client side player data (tile vision). At the moment
     * redundant as the values are initialised with 0 due to fc_calloc(). */
    client_player_init(pplayer);
  }

  /* Team. */
  tslot = team_slot_by_number(pinfo->team);
  fc_assert(NULL != tslot);
  team_add_player(pplayer, team_new(tslot));

  pnation = nation_by_number(pinfo->nation);
  pgov = government_by_number(pinfo->government);
  ptarget_gov = government_by_number(pinfo->target_government);

  /* Now update the player information. */
  sz_strlcpy(pplayer->name, pinfo->name);
  sz_strlcpy(pplayer->username, pinfo->username);


  is_new_nation = player_set_nation(pplayer, pnation);
  pplayer->is_male = pinfo->is_male;
  pplayer->score.game = pinfo->score;
  pplayer->was_created = pinfo->was_created;

  pplayer->economic.gold = pinfo->gold;
  pplayer->economic.tax = pinfo->tax;
  pplayer->economic.science = pinfo->science;
  pplayer->economic.luxury = pinfo->luxury;
  pplayer->government = pgov;
  pplayer->target_government = ptarget_gov;
  /* Don't use player_iterate here, because we ignore the real number
   * of players and we want to read all the datas. */
  BV_CLR_ALL(pplayer->real_embassy);
  fc_assert(8 * sizeof(pplayer->real_embassy)
            >= ARRAY_SIZE(pinfo->real_embassy));
  for (i = 0; i < ARRAY_SIZE(pinfo->real_embassy); i++) {
    if (pinfo->real_embassy[i]) {
      BV_SET(pplayer->real_embassy, i);
    }
  }
  pplayer->gives_shared_vision = pinfo->gives_shared_vision;
  pplayer->city_style = pinfo->city_style;

  /* Don't use player_iterate or player_slot_count here, because we ignore
   * the real number of players and we want to read all the datas. */
  fc_assert(ARRAY_SIZE(pplayer->ai_common.love) >= ARRAY_SIZE(pinfo->love));
  for (i = 0; i < ARRAY_SIZE(pinfo->love); i++) {
    pplayer->ai_common.love[i] = pinfo->love[i];
  }

  my_player = client_player();

  pplayer->is_connected = pinfo->is_connected;

  for (i = 0; i < B_LAST; i++) {
    pplayer->wonders[i] = pinfo->wonders[i];
  }

  /* We need to set ai.control before read_player_info_techs */
  if (pplayer->ai_controlled != pinfo->ai)  {
    pplayer->ai_controlled = pinfo->ai;
    if (pplayer == my_player)  {
      if (my_player->ai_controlled) {
        output_window_append(ftc_client, _("AI mode is now ON."));
      } else {
        output_window_append(ftc_client, _("AI mode is now OFF."));
      }
    }
  }

  pplayer->ai_common.science_cost = pinfo->science_cost;

  /* If the server sends out player information at the wrong time, it is
   * likely to give us inconsistent player tech information, causing a
   * sanity-check failure within this function.  Fixing this at the client
   * end is very tricky; it's hard to figure out when to read the techs
   * and when to ignore them.  The current solution is that the server should
   * only send the player info out at appropriate times - e.g., while the
   * game is running. */
  new_tech = read_player_info_techs(pplayer, pinfo->inventions);
  
  research = player_research_get(pplayer);

  poptechup = (research->researching != pinfo->researching
               || research->tech_goal != pinfo->tech_goal);
  pplayer->bulbs_last_turn = pinfo->bulbs_last_turn;
  research->bulbs_researched = pinfo->bulbs_researched;
  research->techs_researched = pinfo->techs_researched;

  /* check for bad values, complicated by discontinuous range */
  if (NULL == advance_by_number(pinfo->researching)
      && A_UNKNOWN != pinfo->researching
      && A_FUTURE != pinfo->researching
      && A_UNSET != pinfo->researching) {
    research->researching = A_NONE; /* should never happen */
  } else {
    research->researching = pinfo->researching;
  }
  research->future_tech = pinfo->future_tech;
  research->tech_goal = pinfo->tech_goal;
  
  turn_done_changed = (pplayer->phase_done != pinfo->phase_done
                       || pplayer->ai_controlled != pinfo->ai);
  pplayer->phase_done = pinfo->phase_done;

  pplayer->is_ready = pinfo->is_ready;
  pplayer->nturns_idle = pinfo->nturns_idle;
  pplayer->is_alive = pinfo->is_alive;
  pplayer->ai_common.barbarian_type = pinfo->barbarian_type;
  pplayer->revolution_finishes = pinfo->revolution_finishes;
  pplayer->ai_common.skill_level = pinfo->ai_skill_level;

  /* if the server requests that the client reset, then information about
   * connections to this player are lost. If this is the case, insert the
   * correct conn back into the player->connections list */
  if (conn_list_size(pplayer->connections) == 0) {
    conn_list_iterate(game.est_connections, pconn) {
      if (pplayer == pconn->playing) {
        /* insert the controller into first position */
        if (pconn->observer) {
          conn_list_append(pplayer->connections, pconn);
        } else {
          conn_list_prepend(pplayer->connections, pconn);
        }
      }
    } conn_list_iterate_end;
  }


  /* The player information is now fully set. Update the GUI. */

  if (pplayer == my_player && can_client_change_view()) {
    if (poptechup) {
      if (client_has_player() && !my_player->ai_controlled) {
        science_report_dialog_popup(FALSE);
      }
    }
    if (new_tech) {
      /* If we just learned bridge building and focus is on a settler
         on a river the road menu item will remain disabled unless we
         do this. (applys in other cases as well.) */
      if (get_num_units_in_focus() > 0) {
        menus_update();
      }
    }
    if (turn_done_changed) {
      update_turn_done_button_state();
    }
    science_report_dialog_update();
    if (new_tech) {
      /* If we got a new tech the tech tree news an update. */
      science_report_dialog_redraw();
    }
    economy_report_dialog_update();
    units_report_dialog_update();
    city_report_dialog_update();
    update_info_label();
  }

  upgrade_canvas_clipboard();

  players_dialog_update();
  conn_list_dialog_update();

  if (is_new_nation) {
    races_toggles_set_sensitive();

    /* When changing nation during a running game, some refreshing is needed.
     * This may not be the only one! */
    update_map_canvas_visible();
  }

  if (can_client_change_view()) {
    /* Just about any changes above require an update to the intelligence
     * dialog. */
    update_intel_dialog(pplayer);
  }

  editgui_refresh();
  editgui_notify_object_changed(OBJTYPE_PLAYER, player_number(pplayer),
                                FALSE);
}

/****************************************************************************
  Packet player_diplstate handler.
****************************************************************************/
void handle_player_diplstate(const struct packet_player_diplstate *packet)
{
  struct player *plr1 = player_by_number(packet->plr1);
  struct player *plr2 = player_by_number(packet->plr2);
  struct player *my_player = client_player();
  struct player_diplstate *ds = player_diplstate_get(plr1, plr2);
  bool need_players_dialog_update = FALSE;

  fc_assert_ret(ds != NULL);

  if (client_has_player() && my_player == plr2) {
    if (ds->type != packet->type) {
      need_players_dialog_update = TRUE;
    }

    /* Check if we detect change to armistice with us. If so,
     * ready all units for movement out of the territory in
     * question; otherwise they will be disbanded. */
    if (DS_ARMISTICE != player_diplstate_get(plr1, my_player)->type
        && DS_ARMISTICE == packet->type) {
      unit_list_iterate(my_player->units, punit) {
        if (!tile_owner(unit_tile(punit))
            || tile_owner(unit_tile(punit)) != plr1) {
          continue;
        }
        if (punit->client.focus_status == FOCUS_WAIT) {
          punit->client.focus_status = FOCUS_AVAIL;
        }
        if (punit->activity != ACTIVITY_IDLE) {
          request_new_unit_activity(punit, ACTIVITY_IDLE);
        }
      } unit_list_iterate_end;
    }
  }

  ds->type = packet->type;
  ds->turns_left = packet->turns_left;
  ds->has_reason_to_cancel = packet->has_reason_to_cancel;
  ds->contact_turns_left = packet->contact_turns_left;

  if (need_players_dialog_update) {
    players_dialog_update();
  }
}

/****************************************************************************
  Remove, add, or update dummy connection struct representing some
  connection to the server, with info from packet_conn_info.
  Updates player and game connection lists.
  Calls players_dialog_update() in case info for that has changed.
****************************************************************************/
void handle_conn_info(const struct packet_conn_info *pinfo)
{
  struct connection *pconn = conn_by_number(pinfo->id);
  bool preparing_client_state = FALSE;

  log_debug("conn_info id%d used%d est%d plr%d obs%d acc%d",
            pinfo->id, pinfo->used, pinfo->established, pinfo->player_num,
            pinfo->observer, (int) pinfo->access_level);
  log_debug("conn_info \"%s\" \"%s\" \"%s\"",
            pinfo->username, pinfo->addr, pinfo->capability);
  
  if (!pinfo->used) {
    /* Forget the connection */
    if (!pconn) {
      log_verbose("Server removed unknown connection %d", pinfo->id);
      return;
    }
    client_remove_cli_conn(pconn);
    pconn = NULL;
  } else {
    struct player_slot *pslot = player_slot_by_number(pinfo->player_num);
    struct player *pplayer = NULL;

    if (NULL != pslot) {
      pplayer = player_slot_get_player(pslot);
    }

    if (!pconn) {
      log_verbose("Server reports new connection %d %s",
                  pinfo->id, pinfo->username);

      pconn = fc_calloc(1, sizeof(struct connection));
      pconn->buffer = NULL;
      pconn->send_buffer = NULL;
      pconn->ping_time = -1.0;
      if (pplayer) {
        conn_list_append(pplayer->connections, pconn);
      }
      conn_list_append(game.all_connections, pconn);
      conn_list_append(game.est_connections, pconn);
    } else {
      log_packet("Server reports updated connection %d %s",
                 pinfo->id, pinfo->username);
      if (pplayer != pconn->playing) {
	if (NULL != pconn->playing) {
	  conn_list_remove(pconn->playing->connections, pconn);
	}
	if (pplayer) {
	  conn_list_append(pplayer->connections, pconn);
	}
      }
    }

    pconn->id = pinfo->id;
    pconn->established = pinfo->established;
    pconn->observer = pinfo->observer;
    pconn->access_level = pinfo->access_level;
    pconn->playing = pplayer;

    sz_strlcpy(pconn->username, pinfo->username);
    sz_strlcpy(pconn->addr, pinfo->addr);
    sz_strlcpy(pconn->capability, pinfo->capability);

    if (pinfo->id == client.conn.id) {
      /* NB: In this case, pconn is not a duplication of client.conn.
       *
       * pconn->addr is our address that the server knows whereas
       * client.conn.addr is the address to the server. Also,
       * pconn->capability stores our capabilites known at server side
       * whereas client.conn.capability represents the capabilities of the
       * server. */
      if (client.conn.playing != pplayer
          || client.conn.observer != pinfo->observer) {
        /* Our connection state changed, let prepare the changes and reset
         * the game. */
        preparing_client_state = TRUE;
      }

      /* Copy our current state into the static structure (our connection
       * to the server). */
      client.conn.established = pinfo->established;
      client.conn.observer = pinfo->observer;
      client.conn.access_level = pinfo->access_level;
      client.conn.playing = pplayer;
      sz_strlcpy(client.conn.username, pinfo->username);
    }
  }

  players_dialog_update();
  conn_list_dialog_update();

  if (pinfo->used && pinfo->id == client.conn.id) {
    /* For updating the sensitivity of the "Edit Mode" menu item,
     * among other things. */
    menus_update();
  }

  if (preparing_client_state) {
    set_client_state(C_S_PREPARING);
  }
}

/*************************************************************************
  Handles a conn_ping_info packet from the server.  This packet contains
  ping times for each connection.
**************************************************************************/
void handle_conn_ping_info(int connections, const int *conn_id,
                           const float *ping_time)
{
  int i;

  for (i = 0; i < connections; i++) {
    struct connection *pconn = conn_by_number(conn_id[i]);

    if (!pconn) {
      continue;
    }

    pconn->ping_time = ping_time[i];
    log_debug("conn-id=%d, ping=%fs", pconn->id, pconn->ping_time);
  }
  /* The old_ping_time data is ignored. */

  players_dialog_update();
}

/**************************************************************************
Ideally the client should let the player choose which type of
modules and components to build, and (possibly) where to extend
structurals.  The protocol now makes this possible, but the
client is not yet that good (would require GUI improvements)
so currently the client choices stuff automatically if there
is anything unplaced.

This function makes a choice (sends spaceship_action) and
returns 1 if we placed something, else 0.

Do things one at a time; the server will send us an updated
spaceship_info packet, and we'll be back here to do anything
which is left.
**************************************************************************/
static bool spaceship_autoplace(struct player *pplayer,
			       struct player_spaceship *ship)
{
  int i, num;
  enum spaceship_place_type type;
  
  if (ship->modules > (ship->habitation + ship->life_support
		       + ship->solar_panels)) {
    /* "nice" governments prefer to keep success 100%;
     * others build habitation first (for score?)  (Thanks Massimo.)
     */
    type =
      (ship->habitation==0)   ? SSHIP_PLACE_HABITATION :
      (ship->life_support==0) ? SSHIP_PLACE_LIFE_SUPPORT :
      (ship->solar_panels==0) ? SSHIP_PLACE_SOLAR_PANELS :
      ((ship->habitation < ship->life_support)
       && (ship->solar_panels*2 >= ship->habitation + ship->life_support + 1))
                              ? SSHIP_PLACE_HABITATION :
      (ship->solar_panels*2 < ship->habitation + ship->life_support)
                              ? SSHIP_PLACE_SOLAR_PANELS :
      (ship->life_support<ship->habitation)
                              ? SSHIP_PLACE_LIFE_SUPPORT :
      ((ship->life_support <= ship->habitation)
       && (ship->solar_panels*2 >= ship->habitation + ship->life_support + 1))
                              ? SSHIP_PLACE_LIFE_SUPPORT :
                                SSHIP_PLACE_SOLAR_PANELS;

    if (type == SSHIP_PLACE_HABITATION) {
      num = ship->habitation + 1;
    } else if(type == SSHIP_PLACE_LIFE_SUPPORT) {
      num = ship->life_support + 1;
    } else {
      num = ship->solar_panels + 1;
    }
    fc_assert(num <= NUM_SS_MODULES / 3);

    dsend_packet_spaceship_place(&client.conn, type, num);
    return TRUE;
  }
  if (ship->components > ship->fuel + ship->propulsion) {
    if (ship->fuel <= ship->propulsion) {
      type = SSHIP_PLACE_FUEL;
      num = ship->fuel + 1;
    } else {
      type = SSHIP_PLACE_PROPULSION;
      num = ship->propulsion + 1;
    }
    dsend_packet_spaceship_place(&client.conn, type, num);
    return TRUE;
  }
  if (ship->structurals > num_spaceship_structurals_placed(ship)) {
    /* Want to choose which structurals are most important.
       Else we first want to connect one of each type of module,
       then all placed components, pairwise, then any remaining
       modules, or else finally in numerical order.
    */
    int req = -1;
    
    if (!ship->structure[0]) {
      /* if we don't have the first structural, place that! */
      type = SSHIP_PLACE_STRUCTURAL;
      num = 0;
      dsend_packet_spaceship_place(&client.conn, type, num);
      return TRUE;
    }
    
    if (ship->habitation >= 1
	&& !ship->structure[modules_info[0].required]) {
      req = modules_info[0].required;
    } else if (ship->life_support >= 1
	       && !ship->structure[modules_info[1].required]) {
      req = modules_info[1].required;
    } else if (ship->solar_panels >= 1
	       && !ship->structure[modules_info[2].required]) {
      req = modules_info[2].required;
    } else {
      int i;
      for(i=0; i<NUM_SS_COMPONENTS; i++) {
	if ((i%2==0 && ship->fuel > (i/2))
	    || (i%2==1 && ship->propulsion > (i/2))) {
	  if (!ship->structure[components_info[i].required]) {
	    req = components_info[i].required;
	    break;
	  }
	}
      }
    }
    if (req == -1) {
      for(i=0; i<NUM_SS_MODULES; i++) {
	if ((i%3==0 && ship->habitation > (i/3))
	    || (i%3==1 && ship->life_support > (i/3))
	    || (i%3==2 && ship->solar_panels > (i/3))) {
	  if (!ship->structure[modules_info[i].required]) {
	    req = modules_info[i].required;
	    break;
	  }
	}
      }
    }
    if (req == -1) {
      for(i=0; i<NUM_SS_STRUCTURALS; i++) {
	if (!ship->structure[i]) {
	  req = i;
	  break;
	}
      }
    }
    /* sanity: */
    fc_assert(req != -1);
    fc_assert(!ship->structure[req]);
    
    /* Now we want to find a structural we can build which leads to req.
       This loop should bottom out, because everything leads back to s0,
       and we made sure above that we do s0 first.
     */
    while(!ship->structure[structurals_info[req].required]) {
      req = structurals_info[req].required;
    }
    type = SSHIP_PLACE_STRUCTURAL;
    num = req;
    dsend_packet_spaceship_place(&client.conn, type, num);
    return TRUE;
  }
  return FALSE;
}

/****************************************************************************
  Packet spaceship_info handler.
****************************************************************************/
void handle_spaceship_info(const struct packet_spaceship_info *p)
{
  int i;
  struct player_spaceship *ship;
  struct player *pplayer = player_by_number(p->player_num);

  fc_assert_ret_msg(NULL != pplayer, "Invalid player number %d.",
                    p->player_num);

  ship = &pplayer->spaceship;
  ship->state        = p->sship_state;
  ship->structurals  = p->structurals;
  ship->components   = p->components;
  ship->modules      = p->modules;
  ship->fuel         = p->fuel;
  ship->fuel         = p->fuel;
  ship->propulsion   = p->propulsion;
  ship->habitation   = p->habitation;
  ship->life_support = p->life_support;
  ship->solar_panels = p->solar_panels;
  ship->launch_year  = p->launch_year;
  ship->population   = p->population;
  ship->mass         = p->mass;
  ship->support_rate = p->support_rate;
  ship->energy_rate  = p->energy_rate;
  ship->success_rate = p->success_rate;
  ship->travel_time  = p->travel_time;
  
  for(i=0; i<NUM_SS_STRUCTURALS; i++) {
    if (p->structure[i] == '0') {
      ship->structure[i] = FALSE;
    } else if (p->structure[i] == '1') {
      ship->structure[i] = TRUE;
    } else {
      log_error("handle_spaceship_info() "
                "invalid spaceship structure '%c' (%d).",
                p->structure[i], p->structure[i]);
      ship->structure[i] = FALSE;
    }
  }

  if (pplayer != client_player()) {
    refresh_spaceship_dialog(pplayer);
    menus_update();
    return;
  }

  if (!spaceship_autoplace(pplayer, ship)) {
    refresh_spaceship_dialog(pplayer);
  }
}

/****************************************************************************
  Packet tile_info handler.
****************************************************************************/
void handle_tile_info(const struct packet_tile_info *packet)
{
  enum known_type new_known;
  enum known_type old_known;
  bool known_changed = FALSE;
  bool tile_changed = FALSE;
  struct player *powner = player_by_number(packet->owner);
  struct resource *presource = resource_by_number(packet->resource);
  struct terrain *pterrain = terrain_by_number(packet->terrain);
  struct tile *ptile = index_to_tile(packet->tile);

  fc_assert_ret_msg(NULL != ptile, "Invalid tile index %d.", packet->tile);
  old_known = client_tile_get_known(ptile);

  if (NULL == tile_terrain(ptile) || pterrain != tile_terrain(ptile)) {
    tile_changed = TRUE;
    switch (old_known) {
    case TILE_UNKNOWN:
      tile_set_terrain(ptile, pterrain);
      break;
    case TILE_KNOWN_UNSEEN:
    case TILE_KNOWN_SEEN:
      if (NULL != pterrain || TILE_UNKNOWN == packet->known) {
        tile_set_terrain(ptile, pterrain);
      } else {
        tile_changed = FALSE;
        log_error("handle_tile_info() unknown terrain (%d, %d).",
                  TILE_XY(ptile));
      }
      break;
    };
  }

  tile_special_type_iterate(spe) {
    if (packet->special[spe]) {
      if (!tile_has_special(ptile, spe)) {
	tile_set_special(ptile, spe);
	tile_changed = TRUE;
      }
    } else {
      if (tile_has_special(ptile, spe)) {
	tile_clear_special(ptile, spe);
	tile_changed = TRUE;
      }
    }
  } tile_special_type_iterate_end;
  if (!BV_ARE_EQUAL(ptile->bases, packet->bases)) {
    ptile->bases = packet->bases;
    tile_changed = TRUE;
  }
  if (!BV_ARE_EQUAL(ptile->roads, packet->roads)) {
    ptile->roads = packet->roads;
    tile_changed = TRUE;
  }

  tile_changed = tile_changed || (tile_resource(ptile) != presource);

  /* always called after setting terrain */
  tile_set_resource(ptile, presource);

  if (tile_owner(ptile) != powner) {
    tile_set_owner(ptile, powner, NULL);
    tile_changed = TRUE;
  }

  if (NULL == tile_worked(ptile)
   || tile_worked(ptile)->id != packet->worked) {
    if (IDENTITY_NUMBER_ZERO != packet->worked) {
      struct city *pwork = game_city_by_number(packet->worked);

      if (NULL == pwork) {
        char named[MAX_LEN_NAME];

        /* new unseen city, or before city_info */
        fc_snprintf(named, sizeof(named), "%06u", packet->worked);

        pwork = create_city_virtual(invisible.placeholder, NULL, named);
        pwork->id = packet->worked;
        idex_register_city(pwork);

        city_list_prepend(invisible.cities, pwork);

        log_debug("(%d,%d) invisible city %d, %s",
                  TILE_XY(ptile), pwork->id, city_name(pwork));
      } else if (NULL == city_tile(pwork)) {
        /* old unseen city, or before city_info */
        if (NULL != powner && city_owner(pwork) != powner) {
          /* update placeholder with current owner */
          pwork->owner = powner;
          pwork->original = powner;
        }
      } else {
        int dist_sq = sq_map_distance(city_tile(pwork), ptile);
        if (dist_sq > city_map_radius_sq_get(pwork)) {
          /* This is probably enemy city which has grown in diameter since we
           * last saw it. We need city_radius_sq to be at least big enough so
           * that all workers fit in, so set it so. */
          city_map_radius_sq_set(pwork, dist_sq);
        }
      }

      /* This marks tile worked by invisible city. Other
       * parts of the code have to handle invisible cities correctly
       * (ptile->worked->tile == NULL) */
      tile_set_worked(ptile, pwork);
    } else {
      tile_set_worked(ptile, NULL);
    }

    tile_changed = TRUE;
  }

  if (old_known != packet->known) {
    known_changed = TRUE;
  }

  if (NULL != client.conn.playing) {
    dbv_clr(&client.conn.playing->tile_known, tile_index(ptile));
    vision_layer_iterate(v) {
      dbv_clr(&client.conn.playing->client.tile_vision[v], tile_index(ptile));
    } vision_layer_iterate_end;

    switch (packet->known) {
    case TILE_KNOWN_SEEN:
      dbv_set(&client.conn.playing->tile_known, tile_index(ptile));
      vision_layer_iterate(v) {
        dbv_set(&client.conn.playing->client.tile_vision[v], tile_index(ptile));
      } vision_layer_iterate_end;
      break;
    case TILE_KNOWN_UNSEEN:
      dbv_set(&client.conn.playing->tile_known, tile_index(ptile));
      break;
    case TILE_UNKNOWN:
      break;
    default:
      log_error("handle_tile_info() invalid known (%d).", packet->known);
      break;
    };
  }
  new_known = client_tile_get_known(ptile);

  if (packet->spec_sprite[0] != '\0') {
    if (!ptile->spec_sprite
	|| strcmp(ptile->spec_sprite, packet->spec_sprite) != 0) {
      if (ptile->spec_sprite) {
	free(ptile->spec_sprite);
      }
      ptile->spec_sprite = fc_strdup(packet->spec_sprite);
      tile_changed = TRUE;
    }
  } else {
    if (ptile->spec_sprite) {
      free(ptile->spec_sprite);
      ptile->spec_sprite = NULL;
      tile_changed = TRUE;
    }
  }

  if (TILE_KNOWN_SEEN == old_known && TILE_KNOWN_SEEN != new_known) {
    /* This is an error. So first we log the error,
     * then make an assertion. */
    unit_list_iterate(ptile->units, punit) {
      log_error("%p %d %s at (%d,%d) %s", punit, punit->id,
                unit_rule_name(punit), TILE_XY(unit_tile(punit)),
                player_name(unit_owner(punit)));
    } unit_list_iterate_end;
    fc_assert(0 == unit_list_size(ptile->units));
    /* Repairing... */
    unit_list_clear(ptile->units);
  }

  ptile->continent = packet->continent;
  map.num_continents = MAX(ptile->continent, map.num_continents);

  if (packet->label[0] == '\0') {
    if (ptile->label != NULL) {
      FC_FREE(ptile->label);
      ptile->label = NULL;
      tile_changed = TRUE;
    }
  } else if (ptile->label == NULL || strcmp(packet->label, ptile->label)) {
      tile_set_label(ptile, packet->label);
      tile_changed = TRUE;
  }

  if (known_changed || tile_changed) {
    /* 
     * A tile can only change if it was known before and is still
     * known. In the other cases the tile is new or removed.
     */
    if (known_changed && TILE_KNOWN_SEEN == new_known) {
      agents_tile_new(ptile);
    } else if (known_changed && TILE_KNOWN_UNSEEN == new_known) {
      agents_tile_remove(ptile);
    } else {
      agents_tile_changed(ptile);
    }
    editgui_notify_object_changed(OBJTYPE_TILE, tile_index(ptile), FALSE);
  }

  /* refresh tiles */
  if (can_client_change_view()) {
    /* the tile itself (including the necessary parts of adjacent tiles) */
    if (tile_changed || old_known != new_known) {
      refresh_tile_mapcanvas(ptile, TRUE, FALSE);
    }
  }

  /* update menus if the focus unit is on the tile. */
  if (tile_changed) {
    if (get_focus_unit_on_tile(ptile)) {
      menus_update();
    }
  }
}

/****************************************************************************
  Received packet containing info about current scenario
****************************************************************************/
void handle_scenario_info(const struct packet_scenario_info *packet)
{
  game.scenario.is_scenario = packet->is_scenario;
  sz_strlcpy(game.scenario.name, packet->name);
  sz_strlcpy(game.scenario.description, packet->description);
  game.scenario.players = packet->players;

  editgui_notify_object_changed(OBJTYPE_GAME, 1, FALSE);
}

/****************************************************************************
  Take arrival of ruleset control packet to indicate that
  current allocated governments should be free'd, and new
  memory allocated for new size. The same for nations.
****************************************************************************/
void handle_ruleset_control(const struct packet_ruleset_control *packet)
{
  /* The ruleset is going to load new nations. So close
   * the nation selection dialog if it is open. */
  popdown_races_dialog();

  game_ruleset_free();
  game_ruleset_init();
  game.control = *packet;

  /* check the values! */
#define VALIDATE(_count, _maximum, _string)                                 \
  if (game.control._count > _maximum) {                                     \
    log_error("handle_ruleset_control(): Too many " _string                 \
              "; using %d of %d", _maximum, game.control._count);           \
    game.control._count = _maximum;                                         \
  }

  VALIDATE(num_unit_classes,	UCL_LAST,		"unit classes");
  VALIDATE(num_unit_types,	U_LAST,			"unit types");
  VALIDATE(num_impr_types,	B_LAST,			"improvements");
  VALIDATE(num_tech_types,	A_LAST_REAL,		"advances");
  VALIDATE(num_base_types,	MAX_BASE_TYPES,		"bases");
  VALIDATE(num_road_types,      MAX_ROAD_TYPES,         "roads");
  VALIDATE(num_disaster_types,  MAX_DISASTER_TYPES,     "disasters");

  /* game.control.government_count, game.control.nation_count and
   * game.control.styles_count are allocated dynamically, and does
   * not need a size check.  See the allocation bellow. */

  VALIDATE(terrain_count,	MAX_NUM_TERRAINS,	"terrains");
  VALIDATE(resource_count,	MAX_NUM_RESOURCES,	"resources");

  VALIDATE(num_specialist_types, SP_MAX,		"specialists");
#undef VALIDATE

  governments_alloc(game.control.government_count);
  nations_alloc(game.control.nation_count);
  city_styles_alloc(game.control.styles_count);

  /* After nation ruleset free/alloc, nation->player pointers are NULL.
   * We have to initialize player->nation too, to keep state consistent. */
  players_iterate(pplayer) {
    pplayer->nation = NULL;
  } players_iterate_end;

  if (packet->prefered_tileset[0] != '\0') {
    /* There is tileset suggestion */
    if (strcmp(packet->prefered_tileset, tileset_get_name(tileset))) {
      /* It's not currently in use */
      popup_tileset_suggestion_dialog();
    }
  }
}

/****************************************************************************
  Received packet indicating that all rulesets have now been received.
****************************************************************************/
void handle_rulesets_ready(void)
{
  /* We are not going to crop any more sprites from big sprites, free them. */
  finish_loading_sprites(tileset);
}

/****************************************************************************
  Packet ruleset_unit_class handler.
****************************************************************************/
void handle_ruleset_unit_class(const struct packet_ruleset_unit_class *p)
{
  struct unit_class *c = uclass_by_number(p->id);

  fc_assert_ret_msg(NULL != c, "Bad unit_class %d.", p->id);

  names_set(&c->name, p->name, p->rule_name);
  c->move_type   = p->move_type;
  c->min_speed   = p->min_speed;
  c->hp_loss_pct = p->hp_loss_pct;
  c->hut_behavior = p->hut_behavior;
  c->flags       = p->flags;
}

/****************************************************************************
  Packet ruleset_unit handler.
****************************************************************************/
void handle_ruleset_unit(const struct packet_ruleset_unit *p)
{
  int i;
  struct unit_type *u = utype_by_number(p->id);

  fc_assert_ret_msg(NULL != u, "Bad unit_type %d.", p->id);

  names_set(&u->name, p->name, p->rule_name);
  sz_strlcpy(u->graphic_str, p->graphic_str);
  sz_strlcpy(u->graphic_alt, p->graphic_alt);
  sz_strlcpy(u->sound_move, p->sound_move);
  sz_strlcpy(u->sound_move_alt, p->sound_move_alt);
  sz_strlcpy(u->sound_fight, p->sound_fight);
  sz_strlcpy(u->sound_fight_alt, p->sound_fight_alt);

  u->uclass             = uclass_by_number(p->unit_class_id);
  u->build_cost         = p->build_cost;
  u->pop_cost           = p->pop_cost;
  u->attack_strength    = p->attack_strength;
  u->defense_strength   = p->defense_strength;
  u->move_rate          = p->move_rate;
  u->require_advance    = advance_by_number(p->tech_requirement);
  u->need_improvement   = improvement_by_number(p->impr_requirement);
  u->need_government    = government_by_number(p->gov_requirement);
  u->vision_radius_sq = p->vision_radius_sq;
  u->transport_capacity = p->transport_capacity;
  u->hp                 = p->hp;
  u->firepower          = p->firepower;
  u->obsoleted_by       = utype_by_number(p->obsoleted_by);
  u->converted_to       = utype_by_number(p->converted_to);
  u->convert_time       = p->convert_time;
  u->fuel               = p->fuel;
  u->flags              = p->flags;
  u->roles              = p->roles;
  u->happy_cost         = p->happy_cost;
  output_type_iterate(o) {
    u->upkeep[o] = p->upkeep[o];
  } output_type_iterate_end;
  u->paratroopers_range = p->paratroopers_range;
  u->paratroopers_mr_req = p->paratroopers_mr_req;
  u->paratroopers_mr_sub = p->paratroopers_mr_sub;
  u->bombard_rate       = p->bombard_rate;
  u->city_size          = p->city_size;
  u->cargo              = p->cargo;
  u->targets            = p->targets;

  if (p->veteran_levels == 0) {
    u->veteran = NULL;
  } else {
    u->veteran = veteran_system_new(p->veteran_levels);

    for (i = 0; i < p->veteran_levels; i++) {
      veteran_system_definition(u->veteran, i, p->veteran_name[i],
                                p->power_fact[i], 0, 0, p->move_bonus[i]);
    }
  }

  PACKET_STRVEC_EXTRACT(u->helptext, p->helptext);

  tileset_setup_unit_type(tileset, u);
}

/****************************************************************************
  Packet ruleset_tech handler.
****************************************************************************/
void handle_ruleset_tech(const struct packet_ruleset_tech *p)
{
  struct advance *a = advance_by_number(p->id);

  fc_assert_ret_msg(NULL != a, "Bad advance %d.", p->id);

  names_set(&a->name, p->name, p->rule_name);
  sz_strlcpy(a->graphic_str, p->graphic_str);
  sz_strlcpy(a->graphic_alt, p->graphic_alt);
  a->require[AR_ONE] = advance_by_number(p->req[AR_ONE]);
  a->require[AR_TWO] = advance_by_number(p->req[AR_TWO]);
  a->require[AR_ROOT] = advance_by_number(p->root_req);
  a->flags = p->flags;
  a->preset_cost = p->preset_cost;
  a->num_reqs = p->num_reqs;
  PACKET_STRVEC_EXTRACT(a->helptext, p->helptext);

  tileset_setup_tech_type(tileset, a);
}

/****************************************************************************
  Packet ruleset_building handler.
****************************************************************************/
void handle_ruleset_building(const struct packet_ruleset_building *p)
{
  int i;
  struct impr_type *b = improvement_by_number(p->id);

  fc_assert_ret_msg(NULL != b, "Bad improvement %d.", p->id);

  b->genus = p->genus;
  names_set(&b->name, p->name, p->rule_name);
  sz_strlcpy(b->graphic_str, p->graphic_str);
  sz_strlcpy(b->graphic_alt, p->graphic_alt);
  for (i = 0; i < p->reqs_count; i++) {
    requirement_vector_append(&b->reqs, p->reqs[i]);
  }
  fc_assert(b->reqs.size == p->reqs_count);
  b->obsolete_by = advance_by_number(p->obsolete_by);
  b->replaced_by = improvement_by_number(p->replaced_by);
  b->build_cost = p->build_cost;
  b->upkeep = p->upkeep;
  b->sabotage = p->sabotage;
  b->flags = p->flags;
  PACKET_STRVEC_EXTRACT(b->helptext, p->helptext);
  sz_strlcpy(b->soundtag, p->soundtag);
  sz_strlcpy(b->soundtag_alt, p->soundtag_alt);

#ifdef DEBUG
  if (p->id == improvement_count() - 1) {
    improvement_iterate(b) {
      log_debug("Improvement: %s...", improvement_rule_name(b));
      if (A_NEVER != b->obsolete_by) {
        log_debug("  obsolete_by %2d \"%s\"",
                  advance_number(b->obsolete_by),
                  advance_rule_name(b->obsolete_by));
      }
      log_debug("  build_cost %3d", b->build_cost);
      log_debug("  upkeep      %2d", b->upkeep);
      log_debug("  sabotage   %3d", b->sabotage);
      if (NULL != b->helptext) {
        strvec_iterate(b->helptext, text) {
        log_debug("  helptext    %s", text);
        } strvec_iterate_end;
      }
    } improvement_iterate_end;
  }
#endif /* DEBUG */

  b->allows_units = FALSE;
  unit_type_iterate(ut) {
    if (ut->need_improvement == b) {
      b->allows_units = TRUE;
      break;
    }
  } unit_type_iterate_end;

  tileset_setup_impr_type(tileset, b);
}

/****************************************************************************
  Packet ruleset_government handler.
****************************************************************************/
void handle_ruleset_government(const struct packet_ruleset_government *p)
{
  int j;
  struct government *gov = government_by_number(p->id);

  fc_assert_ret_msg(NULL != gov, "Bad government %d.", p->id);

  gov->item_number = p->id;

  for (j = 0; j < p->reqs_count; j++) {
    requirement_vector_append(&gov->reqs, p->reqs[j]);
  }
  fc_assert(gov->reqs.size == p->reqs_count);

  names_set(&gov->name, p->name, p->rule_name);
  sz_strlcpy(gov->graphic_str, p->graphic_str);
  sz_strlcpy(gov->graphic_alt, p->graphic_alt);

  PACKET_STRVEC_EXTRACT(gov->helptext, p->helptext);

  tileset_setup_government(tileset, gov);
}

/****************************************************************************
  Packet ruleset_government_ruler_title handler.
****************************************************************************/
void handle_ruleset_government_ruler_title
    (const struct packet_ruleset_government_ruler_title *packet)
{
  struct government *gov = government_by_number(packet->gov);

  fc_assert_ret_msg(NULL != gov, "Bad government %d.", packet->gov);

  (void) government_ruler_title_new(gov, nation_by_number(packet->nation),
                                    packet->male_title,
                                    packet->female_title);
}

/****************************************************************************
  Packet ruleset_terrain handler.
****************************************************************************/
void handle_ruleset_terrain(const struct packet_ruleset_terrain *p)
{
  int j;
  struct terrain *pterrain = terrain_by_number(p->id);

  fc_assert_ret_msg(NULL != pterrain, "Bad terrain %d.", p->id);

  pterrain->native_to = p->native_to;
  names_set(&pterrain->name, p->name, p->rule_name);
  sz_strlcpy(pterrain->graphic_str, p->graphic_str);
  sz_strlcpy(pterrain->graphic_alt, p->graphic_alt);
  pterrain->movement_cost = p->movement_cost;
  pterrain->defense_bonus = p->defense_bonus;

  output_type_iterate(o) {
    pterrain->output[o] = p->output[o];
  } output_type_iterate_end;

  if (pterrain->resources != NULL) {
    free(pterrain->resources);
  }
  pterrain->resources = fc_calloc(p->num_resources + 1,
				  sizeof(*pterrain->resources));
  for (j = 0; j < p->num_resources; j++) {
    pterrain->resources[j] = resource_by_number(p->resources[j]);
    if (!pterrain->resources[j]) {
      log_error("handle_ruleset_terrain() "
                "Mismatched resource %d for terrain \"%s\".",
                p->resources[j], terrain_rule_name(pterrain));
    }
  }
  pterrain->resources[p->num_resources] = NULL;

  output_type_iterate(o) {
    pterrain->road_output_incr_pct[o] = p->road_output_incr_pct[o];
  } output_type_iterate_end;

  pterrain->base_time = p->base_time;
  pterrain->road_time = p->road_time;
  pterrain->irrigation_result = terrain_by_number(p->irrigation_result);
  pterrain->irrigation_food_incr = p->irrigation_food_incr;
  pterrain->irrigation_time = p->irrigation_time;
  pterrain->mining_result = terrain_by_number(p->mining_result);
  pterrain->mining_shield_incr = p->mining_shield_incr;
  pterrain->mining_time = p->mining_time;
  pterrain->transform_result = terrain_by_number(p->transform_result);
  pterrain->transform_time = p->transform_time;
  pterrain->clean_pollution_time = p->clean_pollution_time;
  pterrain->clean_fallout_time = p->clean_fallout_time;

  pterrain->flags = p->flags;

  fc_assert_ret(pterrain->rgb == NULL);
  pterrain->rgb = rgbcolor_new(p->color_red, p->color_green, p->color_blue);

  PACKET_STRVEC_EXTRACT(pterrain->helptext, p->helptext);

  tileset_setup_tile_type(tileset, pterrain);
}

/****************************************************************************
  Handle a packet about a particular terrain resource.
****************************************************************************/
void handle_ruleset_resource(const struct packet_ruleset_resource *p)
{
  struct resource *presource = resource_by_number(p->id);

  fc_assert_ret_msg(NULL != presource, "Bad resource %d.", p->id);

  names_set(&presource->name, p->name, p->rule_name);
  sz_strlcpy(presource->graphic_str, p->graphic_str);
  sz_strlcpy(presource->graphic_alt, p->graphic_alt);

  output_type_iterate(o) {
    presource->output[o] = p->output[o];
  } output_type_iterate_end;

  tileset_setup_resource(tileset, presource);
}

/****************************************************************************
  Handle a packet about a particular base type.
****************************************************************************/
void handle_ruleset_base(const struct packet_ruleset_base *p)
{
  int i;
  struct base_type *pbase = base_by_number(p->id);

  fc_assert_ret_msg(NULL != pbase, "Bad base %d.", p->id);

  names_set(&pbase->name, p->name, p->rule_name);
  sz_strlcpy(pbase->graphic_str, p->graphic_str);
  sz_strlcpy(pbase->graphic_alt, p->graphic_alt);
  sz_strlcpy(pbase->activity_gfx, p->activity_gfx);
  pbase->buildable = p->buildable;
  pbase->pillageable = p->pillageable;

  for (i = 0; i < p->reqs_count; i++) {
    requirement_vector_append(&pbase->reqs, p->reqs[i]);
  }
  fc_assert(pbase->reqs.size == p->reqs_count);

  pbase->native_to = p->native_to;

  pbase->gui_type = p->gui_type;
  pbase->build_time = p->build_time;
  pbase->defense_bonus = p->defense_bonus;
  pbase->border_sq  = p->border_sq;
  pbase->vision_main_sq = p->vision_main_sq;
  pbase->vision_invis_sq = p->vision_invis_sq;

  pbase->flags = p->flags;
  pbase->conflicts = p->conflicts;

  PACKET_STRVEC_EXTRACT(pbase->helptext, p->helptext);

  tileset_setup_base(tileset, pbase);
}

/****************************************************************************
  Handle a packet about a particular road type.
****************************************************************************/
void handle_ruleset_road(const struct packet_ruleset_road *p)
{
  struct road_type *proad = road_by_number(p->id);
  int i;

  fc_assert_ret_msg(NULL != proad, "Bad road %d.", p->id);

  names_set(&proad->name, p->name, p->rule_name);
  sz_strlcpy(proad->graphic_str, p->graphic_str);
  sz_strlcpy(proad->graphic_alt, p->graphic_alt);

  proad->move_cost = p->move_cost;
  proad->build_time = p->build_time;

  output_type_iterate(o) {
    proad->tile_incr[o] = p->tile_incr[o];
    proad->tile_bonus[o] = p->tile_bonus[o];
  } output_type_iterate_end;

  for (i = 0; i < p->reqs_count; i++) {
    requirement_vector_append(&proad->reqs, p->reqs[i]);
  }
  fc_assert(proad->reqs.size == p->reqs_count);

  proad->compat_special = p->compat_special;

  proad->native_to = p->native_to;
  proad->hidden_by = p->hidden_by;
  proad->flags = p->flags;

  tileset_setup_road(tileset, proad);
}

/****************************************************************************
  Handle a packet about a particular disaster type.
****************************************************************************/
void handle_ruleset_disaster(const struct packet_ruleset_disaster *p)
{
  struct disaster_type *pdis = disaster_by_number(p->id);
  int i;

  fc_assert_ret_msg(NULL != pdis, "Bad disaster %d.", p->id);

  names_set(&pdis->name, p->name, p->rule_name);

  for (i = 0; i < p->reqs_count; i++) {
    requirement_vector_append(&pdis->reqs, p->reqs[i]);
  }
  fc_assert(pdis->reqs.size == p->reqs_count);

  for (i = 0; i < p->nreqs_count; i++) {
    requirement_vector_append(&pdis->nreqs, p->nreqs[i]);
  }
  fc_assert(pdis->nreqs.size == p->nreqs_count);

  pdis->frequency = p->frequency;

  pdis->effects = p->effects;
}

/****************************************************************************
  Handle the terrain control ruleset packet sent by the server.
****************************************************************************/
void handle_ruleset_terrain_control
    (const struct packet_ruleset_terrain_control *p)
{
  /* Since terrain_control is the same as packet_ruleset_terrain_control
   * we can just copy the data directly. */
  terrain_control = *p;
}

/****************************************************************************
  Handle the list of groups, sent by the ruleset.
****************************************************************************/
void handle_ruleset_nation_groups
    (const struct packet_ruleset_nation_groups *packet)
{
  int i;

  for (i = 0; i < packet->ngroups; i++) {
    struct nation_group *pgroup;

    pgroup = nation_group_new(packet->groups[i]);
    fc_assert(NULL != pgroup);
    fc_assert(i == nation_group_index(pgroup));
  }
}

/****************************************************************************
  Packet ruleset_nation handler.
****************************************************************************/
void handle_ruleset_nation(const struct packet_ruleset_nation *packet)
{
  struct nation_type *pnation = nation_by_number(packet->id);
  int i;

  fc_assert_ret_msg(NULL != pnation, "Bad nation %d.", packet->id);

  names_set(&pnation->adjective, packet->adjective, packet->rule_name);
  name_set(&pnation->noun_plural, packet->noun_plural);
  sz_strlcpy(pnation->flag_graphic_str, packet->graphic_str);
  sz_strlcpy(pnation->flag_graphic_alt, packet->graphic_alt);
  pnation->city_style = packet->city_style;
  for (i = 0; i < packet->leader_count; i++) {
    (void) nation_leader_new(pnation, packet->leader_name[i],
                             packet->leader_is_male[i]);
  }

  pnation->is_available = packet->is_available;
  pnation->is_playable = packet->is_playable;
  pnation->barb_type = packet->barbarian_type;

  if ('\0' != packet->legend[0]) {
    pnation->legend = fc_strdup(_(packet->legend));
  } else {
    pnation->legend = fc_strdup("");
  }

  for (i = 0; i < packet->ngroups; i++) {
    struct nation_group *pgroup = nation_group_by_number(packet->groups[i]);

    if (NULL != pgroup) {
      nation_group_list_append(pnation->groups, pgroup);
    } else {
      log_error("handle_ruleset_nation() \"%s\" have unknown group %d.",
                nation_rule_name(pnation), packet->groups[i]);
    }
  }

  pnation->init_government = government_by_number(packet->init_government_id);
  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    pnation->init_techs[i] = packet->init_techs[i];
  }
  for (i = 0; i < MAX_NUM_UNIT_LIST; i++) {
    pnation->init_units[i] = utype_by_number(packet->init_units[i]);
  }
  for (i = 0; i < MAX_NUM_BUILDING_LIST; i++) {
    pnation->init_buildings[i] = packet->init_buildings[i];
  }

  tileset_setup_nation_flag(tileset, pnation);
}

/**************************************************************************
  Handle city style packet.
**************************************************************************/
void handle_ruleset_city(const struct packet_ruleset_city *packet)
{
  int id, j;
  struct citystyle *cs;

  id = packet->style_id;
  fc_assert_ret_msg(0 <= id && game.control.styles_count > id,
                    "Bad citystyle %d.", id);
  cs = &city_styles[id];

  for (j = 0; j < packet->reqs_count; j++) {
    requirement_vector_append(&cs->reqs, packet->reqs[j]);
  }
  fc_assert(cs->reqs.size == packet->reqs_count);
  cs->replaced_by = packet->replaced_by;

  names_set(&cs->name, packet->name, packet->rule_name);
  sz_strlcpy(cs->graphic, packet->graphic);
  sz_strlcpy(cs->graphic_alt, packet->graphic_alt);
  sz_strlcpy(cs->oceanic_graphic, packet->oceanic_graphic);
  sz_strlcpy(cs->oceanic_graphic_alt, packet->oceanic_graphic_alt);
  sz_strlcpy(cs->citizens_graphic, packet->citizens_graphic);
  sz_strlcpy(cs->citizens_graphic_alt, packet->citizens_graphic_alt);

  tileset_setup_city_tiles(tileset, id);
}

/****************************************************************************
  Packet ruleset_game handler.
****************************************************************************/
void handle_ruleset_game(const struct packet_ruleset_game *packet)
{
  int i;

  /* Must set num_specialist_types before iterating over them. */
  DEFAULT_SPECIALIST = packet->default_specialist;

  fc_assert_ret(packet->veteran_levels > 0);

  game.veteran = veteran_system_new(packet->veteran_levels);
  game.veteran->levels = packet->veteran_levels;

  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    game.rgame.global_init_techs[i] = packet->global_init_techs[i];
  }
  for (i = 0; i < MAX_NUM_BUILDING_LIST; i++) {
    game.rgame.global_init_buildings[i] = packet->global_init_buildings[i];
  }

  for (i = 0; i < packet->veteran_levels; i++) {
    veteran_system_definition(game.veteran, i, packet->veteran_name[i],
                              packet->power_fact[i], 0, 0,
                              packet->move_bonus[i]);
  }

  if (game.plr_bg_color) {
    rgbcolor_destroy(game.plr_bg_color);
  }

  game.plr_bg_color = rgbcolor_new(packet->background_red,
                                   packet->background_green,
                                   packet->background_blue);

  tileset_background_init(tileset);
}

/****************************************************************************
  Handle info about a single specialist.
****************************************************************************/
void handle_ruleset_specialist(const struct packet_ruleset_specialist *p)
{
  int j;
  struct specialist *s = specialist_by_number(p->id);

  fc_assert_ret_msg(NULL != s, "Bad specialist %d.", p->id);

  names_set(&s->name, p->plural_name, p->rule_name);
  name_set(&s->abbreviation, p->short_name);

  for (j = 0; j < p->reqs_count; j++) {
    requirement_vector_append(&s->reqs, p->reqs[j]);
  }
  fc_assert(s->reqs.size == p->reqs_count);

  PACKET_STRVEC_EXTRACT(s->helptext, p->helptext);

  tileset_setup_specialist_type(tileset, p->id);
}

/**************************************************************************
  Handle reply to our city name request.
**************************************************************************/
void handle_city_name_suggestion_info(int unit_id, const char *name)
{
  struct unit *punit = player_unit_by_number(client_player(), unit_id);

  if (!can_client_issue_orders()) {
    return;
  }

  if (punit) {
    if (ask_city_name) {
      bool other_asking = FALSE;
      unit_list_iterate(unit_tile(punit)->units, other) {
        if (other->client.asking_city_name) {
          other_asking = TRUE;
        }
      } unit_list_iterate_end;
      punit->client.asking_city_name = TRUE;

      if (!other_asking) {
        popup_newcity_dialog(punit, name);
      }
    } else {
      dsend_packet_unit_build_city(&client.conn, unit_id, name);
    }
  }
}

/**************************************************************************
  Handle reply to diplomat action request
**************************************************************************/
void handle_unit_diplomat_answer(int diplomat_id, int target_id, int cost,
                                 enum diplomat_actions action_type)
{
  struct city *pcity = game_city_by_number(target_id);
  struct unit *punit = game_unit_by_number(target_id);
  struct unit *pdiplomat = player_unit_by_number(client_player(),
                                                 diplomat_id);

  fc_assert_ret_msg(NULL != pdiplomat, "Bad diplomat %d.", diplomat_id);

  switch (action_type) {
  case DIPLOMAT_BRIBE:
    if (punit) {
      if (NULL != client.conn.playing
          && !client.conn.playing->ai_controlled) {
        popup_bribe_dialog(punit, cost);
      }
    }
    break;
  case DIPLOMAT_INCITE:
    if (pcity) {
      if (NULL != client.conn.playing
          && !client.conn.playing->ai_controlled) {
        popup_incite_dialog(pcity, cost);
      }
    }
    break;
  case DIPLOMAT_MOVE:
    if (can_client_issue_orders()) {
      process_diplomat_arrival(pdiplomat, target_id);
    }
    break;
  default:
    log_error("handle_unit_diplomat_answer() invalid action_type (%d).",
              action_type);
    break;
  };
}

/**************************************************************************
  Handle list of potenttial buildings to sabotage.
**************************************************************************/
void handle_city_sabotage_list(int diplomat_id, int city_id,
                               bv_imprs improvements)
{
  struct city *pcity = game_city_by_number(city_id);
  struct unit *pdiplomat = player_unit_by_number(client_player(),
                                                 diplomat_id);

  fc_assert_ret_msg(NULL != pdiplomat, "Bad diplomat %d.", diplomat_id);
  fc_assert_ret_msg(NULL != pcity, "Bad city %d.", city_id);

  if (can_client_issue_orders()) {
    improvement_iterate(pimprove) {
      update_improvement_from_packet(pcity, pimprove,
                                     BV_ISSET(improvements,
                                              improvement_index(pimprove)));
    } improvement_iterate_end;

    popup_sabotage_dialog(pcity);
  }
}

/****************************************************************************
  Pass the header information about things be displayed in a gui-specific
  endgame dialog.
****************************************************************************/
void handle_endgame_report(const struct packet_endgame_report *packet)
{
  set_client_state(C_S_OVER);
  endgame_report_dialog_start(packet);
}

/****************************************************************************
  Pass endgame report about single player.
****************************************************************************/
void handle_endgame_player(const struct packet_endgame_player *packet)
{
  endgame_report_dialog_player(packet);
}

/****************************************************************************
  Packet player_attribute_chunk handler.
****************************************************************************/
void handle_player_attribute_chunk
    (const struct packet_player_attribute_chunk *packet)
{
  if (!client_has_player()) {
    return;
  }

  generic_handle_player_attribute_chunk(client_player(), packet);

  if (packet->offset + packet->chunk_length == packet->total_length) {
    /* We successful received the last chunk. The attribute block is
       now complete. */
      attribute_restore();
  }
}

/**************************************************************************
  Handle request to start processing packet.
**************************************************************************/
void handle_processing_started(void)
{
  agents_processing_started();

  fc_assert(client.conn.client.request_id_of_currently_handled_packet == 0);
  client.conn.client.request_id_of_currently_handled_packet =
      get_next_request_id(client.conn.
                          client.last_processed_request_id_seen);
  update_queue_processing_started(client.conn.client.
                                  request_id_of_currently_handled_packet);

  log_debug("start processing packet %d",
            client.conn.client.request_id_of_currently_handled_packet);
}

/**************************************************************************
  Handle request to stop processing packet.
**************************************************************************/
void handle_processing_finished(void)
{
  log_debug("finish processing packet %d",
            client.conn.client.request_id_of_currently_handled_packet);

  fc_assert(client.conn.client.request_id_of_currently_handled_packet != 0);

  client.conn.client.last_processed_request_id_seen =
      client.conn.client.request_id_of_currently_handled_packet;
  update_queue_processing_finished(client.conn.client.
                                   last_processed_request_id_seen);

  client.conn.client.request_id_of_currently_handled_packet = 0;

  agents_processing_finished();
}

/**************************************************************************
  Notify interested parties about incoming packet.
**************************************************************************/
void notify_about_incoming_packet(struct connection *pc,
                                  int packet_type, int size)
{
  fc_assert(pc == &client.conn);
  log_debug("incoming packet={type=%d, size=%d}", packet_type, size);
}

/**************************************************************************
  Notify interested parties about outgoing packet.
**************************************************************************/
void notify_about_outgoing_packet(struct connection *pc,
                                  int packet_type, int size,
                                  int request_id)
{
  fc_assert(pc == &client.conn);
  log_debug("outgoing packet={type=%d, size=%d, request_id=%d}",
            packet_type, size, request_id);

  fc_assert(request_id);
}

/**************************************************************************
  We have received PACKET_FREEZE_CLIENT.
**************************************************************************/
void handle_freeze_client(void)
{
  log_debug("handle_freeze_client");

  agents_freeze_hint();
}

/**************************************************************************
  We have received PACKET_THAW_CLIENT
**************************************************************************/
void handle_thaw_client(void)
{
  log_debug("handle_thaw_client");

  agents_thaw_hint();
  update_turn_done_button_state();
}

/**************************************************************************
  Reply to 'ping' packet with 'pong'
**************************************************************************/
void handle_conn_ping(void)
{
  send_packet_conn_pong(&client.conn);
}

/**************************************************************************
  Handle server shutdown.
**************************************************************************/
void handle_server_shutdown(void)
{
  log_verbose("server shutdown");
}

/****************************************************************************
  Add effect data to ruleset cache.
****************************************************************************/
void handle_ruleset_effect(const struct packet_ruleset_effect *packet)
{
  recv_ruleset_effect(packet);
}

/****************************************************************************
  Add effect requirement data to ruleset cache.
****************************************************************************/
void handle_ruleset_effect_req
    (const struct packet_ruleset_effect_req *packet)
{
  recv_ruleset_effect_req(packet);
}

/**************************************************************************
  Handle a notification from the server that an object was successfully
  created. The 'tag' was previously sent to the server when the client
  requested the creation. The 'id' is the identifier of the newly created
  object.
**************************************************************************/
void handle_edit_object_created(int tag, int id)
{
  editgui_notify_object_created(tag, id);
}

 /****************************************************************************
  Handle start position creation/removal.
****************************************************************************/
void handle_edit_startpos(const struct packet_edit_startpos *packet)
{
  struct tile *ptile = index_to_tile(packet->id);
  bool changed = FALSE;

  /* Check. */
  if (NULL == ptile) {
    log_error("%s(): invalid tile index %d.", __FUNCTION__, packet->id);
    return;
  }

  /* Handle. */
  if (packet->remove) {
    changed = map_startpos_remove(ptile);
  } else {
    if (NULL != map_startpos_get(ptile)) {
      changed = FALSE;
    } else {
      map_startpos_new(ptile);
      changed = TRUE;
    }
  }

  /* Notify. */
  if (changed && can_client_change_view()) {
    refresh_tile_mapcanvas(ptile, TRUE, FALSE);
    if (packet->remove) {
      editgui_notify_object_changed(OBJTYPE_STARTPOS,
                                    packet->id, TRUE);
    } else {
      editgui_notify_object_created(packet->tag, packet->id);
    }
  }
}

/****************************************************************************
  Handle start position internal information.
****************************************************************************/
void handle_edit_startpos_full(const struct packet_edit_startpos_full *
                               packet)
{
  struct tile *ptile = index_to_tile(packet->id);
  struct startpos *psp;

  /* Check. */
  if (NULL == ptile) {
    log_error("%s(): invalid tile index %d.", __FUNCTION__, packet->id);
    return;
  }

  psp = map_startpos_get(ptile);
  if (NULL == psp) {
    log_error("%s(): no start position at (%d, %d)",
              __FUNCTION__, TILE_XY(ptile));
    return;
  }

  /* Handle. */
  if (startpos_unpack(psp, packet) && can_client_change_view()) {
    /* Notify. */
    refresh_tile_mapcanvas(ptile, TRUE, FALSE);
    editgui_notify_object_changed(OBJTYPE_STARTPOS, startpos_number(psp),
                                  FALSE);
  }
}

/**************************************************************************
  A vote no longer exists. Remove from queue and update gui.
**************************************************************************/
void handle_vote_remove(int vote_no)
{
  voteinfo_queue_delayed_remove(vote_no);
  voteinfo_gui_update();
}

/**************************************************************************
  Find and update the corresponding vote and refresh the GUI.
**************************************************************************/
void handle_vote_update(int vote_no, int yes, int no, int abstain,
                        int num_voters)
{
  struct voteinfo *vi;

  vi = voteinfo_queue_find(vote_no);
  fc_assert_ret_msg(NULL != vi,
                    "Got packet_vote_update for non-existant vote %d!",
                    vote_no);

  vi->yes = yes;
  vi->no = no;
  vi->abstain = abstain;
  vi->num_voters = num_voters;

  voteinfo_gui_update();
}

/****************************************************************************
  Create a new vote and add it to the queue. Refresh the GUI.
****************************************************************************/
void handle_vote_new(const struct packet_vote_new *packet)
{
  fc_assert_ret_msg(NULL == voteinfo_queue_find(packet->vote_no),
                    "Got a packet_vote_new for already existing "
                    "vote %d!", packet->vote_no);

  voteinfo_queue_add(packet->vote_no,
                     packet->user,
                     packet->desc,
                     packet->percent_required,
                     packet->flags);
  voteinfo_gui_update();
}

/**************************************************************************
  Update the vote's status and refresh the GUI.
**************************************************************************/
void handle_vote_resolve(int vote_no, bool passed)
{
  struct voteinfo *vi;

  vi = voteinfo_queue_find(vote_no);
  fc_assert_ret_msg(NULL != vi,
                    "Got packet_vote_resolve for non-existant vote %d!",
                    vote_no);

  vi->resolved = TRUE;
  vi->passed = passed;

  voteinfo_gui_update();
}
