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

/********************************************************************** 
This module contains various general - mostly highlevel - functions
used throughout the client.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "diptreaty.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "packets.h"
#include "spaceship.h"
#include "support.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "cityrep_g.h"
#include "civclient.h"
#include "climap.h"
#include "clinet.h"
#include "control.h"
#include "mapview_g.h"
#include "messagewin_common.h"
#include "packhand.h"
#include "plrdlg_common.h"
#include "repodlgs_common.h"
#include "tilespec.h"

#include "climisc.h"

/**************************************************************************
...
**************************************************************************/
void client_remove_player(int plrno)
{
  game_remove_player(get_player(plrno));
  game_renumber_players(plrno);
}

/**************************************************************************
...
**************************************************************************/
void client_remove_unit(struct unit *punit)
{
  struct city *pcity;
  int x = punit->x;
  int y = punit->y;
  int hc = punit->homecity;
  struct unit *ufocus = get_unit_in_focus();

  freelog(LOG_DEBUG, "removing unit %d, %s %s (%d %d) hcity %d",
	  punit->id, get_nation_name(unit_owner(punit)->nation),
	  unit_name(punit->type), punit->x, punit->y, hc);

  if (punit == ufocus) {
    set_unit_focus(NULL);
    game_remove_unit(punit);
    punit = ufocus = NULL;
    advance_unit_focus();
  } else {
    /* calculate before punit disappears, use after punit removed: */
    bool update = (ufocus
		   && same_pos(ufocus->x, ufocus->y, punit->x, punit->y));

    game_remove_unit(punit);
    punit = NULL;
    if (update) {
      update_unit_pix_label(ufocus);
    }
  }

  pcity = map_get_city(x, y);
  if (pcity) {
    if (can_player_see_units_in_city(game.player_ptr, pcity)) {
      pcity->client.occupied =
	(unit_list_size(&(map_get_tile(pcity->x, pcity->y)->units)) > 0);
    }

    refresh_city_dialog(pcity);
    freelog(LOG_DEBUG, "map city %s, %s, (%d %d)", pcity->name,
	    get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y);
  }

  pcity = player_find_city_by_id(game.player_ptr, hc);
  if (pcity) {
    refresh_city_dialog(pcity);
    freelog(LOG_DEBUG, "home city %s, %s, (%d %d)", pcity->name,
	    get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y);
  }

  refresh_tile_mapcanvas(x, y, FALSE);
}

/**************************************************************************
...
**************************************************************************/
void client_remove_city(struct city *pcity)
{
  bool effect_update;
  int x=pcity->x;
  int y=pcity->y;

  freelog(LOG_DEBUG, "removing city %s, %s, (%d %d)", pcity->name,
	  get_nation_name(city_owner(pcity)->nation), x, y);

  /* Explicitly remove all improvements, to properly remove any global effects
     and to handle the preservation of "destroyed" effects. */
  effect_update=FALSE;

  built_impr_iterate(pcity, i) {
    effect_update = TRUE;
    city_remove_improvement(pcity, i);
  } built_impr_iterate_end;

  if (effect_update) {
    /* nothing yet */
  }

  popdown_city_dialog(pcity);
  game_remove_city(pcity);
  city_report_dialog_update();
  refresh_tile_mapcanvas(x, y, FALSE);
}

/**************************************************************************
Change all cities building X to building Y, if possible.  X and Y
could be improvements or units. X and Y are compound ids.
**************************************************************************/
void client_change_all(cid x, cid y)
{
  int fr_id = cid_id(x), to_id = cid_id(y);
  bool fr_is_unit = cid_is_unit(x), to_is_unit = cid_is_unit(y);
  char buf[512];
  int last_request_id = 0;

  my_snprintf(buf, sizeof(buf),
	      _("Game: Changing production of every %s into %s."),
	      fr_is_unit ? get_unit_type(fr_id)->name :
	      get_improvement_name(fr_id),
	      to_is_unit ? get_unit_type(to_id)->
	      name : get_improvement_name(to_id));
  append_output_window(buf);

  connection_do_buffer(&aconnection);
  city_list_iterate (game.player_ptr->cities, pcity) {
    if (((fr_is_unit &&
	  (pcity->is_building_unit) &&
	  (pcity->currently_building == fr_id)) ||
	 (!fr_is_unit &&
	  !(pcity->is_building_unit) &&
	  (pcity->currently_building == fr_id))) &&
	((to_is_unit &&
	  can_build_unit (pcity, to_id)) ||
	 (!to_is_unit &&
	  can_build_improvement (pcity, to_id))))
      {
	last_request_id = city_change_production(pcity, to_is_unit, to_id);
      }
  } city_list_iterate_end;

  connection_do_unbuffer(&aconnection);
  reports_freeze_till(last_request_id);
}

/***************************************************************************
  Return a string indicating one nation's embassy status with another
***************************************************************************/
const char *get_embassy_status(struct player *me, struct player *them)
{
  if (me == them
      || !them->is_alive
      || !me->is_alive) {
    return "-";
  }
  if (player_has_embassy(me, them)) {
    if (player_has_embassy(them, me)) {
      return Q_("?embassy:Both");
    } else {
      return Q_("?embassy:Yes");
    }
  } else if (player_has_embassy(them, me)) {
    return Q_("?embassy:With Us");
  } else if (me->diplstates[them->player_no].contact_turns_left > 0
             || them->diplstates[me->player_no].contact_turns_left > 0) {
    return Q_("?embassy:Contact");
  } else {
    return "";
  }
}

/***************************************************************************
  Return a string indicating one nation's shaed vision status with another
***************************************************************************/
const char *get_vision_status(struct player *me, struct player *them)
{
  if (gives_shared_vision(me, them)) {
    if (gives_shared_vision(them, me)) {
      return Q_("?vision:Both");
    } else {
      return Q_("?vision:To Them");
    }
  } else if (gives_shared_vision(them, me)) {
    return Q_("?vision:To Us");
  } else {
    return "";
  }
}

/**************************************************************************
Copy a string that describes the given clause into the return buffer.
**************************************************************************/
void client_diplomacy_clause_string(char *buf, int bufsiz,
				    struct Clause *pclause)
{
  struct city *pcity;

  switch(pclause->type) {
  case CLAUSE_ADVANCE:
    my_snprintf(buf, bufsiz, _("The %s give %s"),
		get_nation_name_plural(pclause->from->nation),
		advances[pclause->value].name);
    break;
  case CLAUSE_CITY:
    pcity = find_city_by_id(pclause->value);
    if (pcity) {
      my_snprintf(buf, bufsiz, _("The %s give %s"),
                  get_nation_name_plural(pclause->from->nation),
		  pcity->name);
    } else {
      my_snprintf(buf, bufsiz,_("The %s give unknown city."),
                  get_nation_name_plural(pclause->from->nation));
    }
    break;
  case CLAUSE_GOLD:
    my_snprintf(buf, bufsiz, _("The %s give %d gold"),
		get_nation_name_plural(pclause->from->nation),
		pclause->value);
    break;
  case CLAUSE_MAP:
    my_snprintf(buf, bufsiz, _("The %s give their worldmap"),
		get_nation_name_plural(pclause->from->nation));
    break;
  case CLAUSE_SEAMAP:
    my_snprintf(buf, bufsiz, _("The %s give their seamap"),
		get_nation_name_plural(pclause->from->nation));
    break;
  case CLAUSE_CEASEFIRE:
    my_snprintf(buf, bufsiz, _("The parties agree on a cease-fire"));
    break;
  case CLAUSE_PEACE:
    my_snprintf(buf, bufsiz, _("The parties agree on a peace"));
    break;
  case CLAUSE_ALLIANCE:
    my_snprintf(buf, bufsiz, _("The parties create an alliance"));
    break;
  case CLAUSE_TEAM:
    my_snprintf(buf, bufsiz, _("The parties resume the research pool"));
    break;
  case CLAUSE_VISION:
    my_snprintf(buf, bufsiz, _("The %s gives shared vision"),
		get_nation_name_plural(pclause->from->nation));
    break;
  case CLAUSE_EMBASSY:
    my_snprintf(buf, bufsiz, _("The %s gives an embassy"),
                get_nation_name_plural(pclause->from->nation));
    break;
  default:
    assert(FALSE);
    if (bufsiz > 0) {
      *buf = '\0';
    }
    break;
  }
}

/**************************************************************************
Return the sprite index for the research indicator.
**************************************************************************/
int client_research_sprite(void)
{
  return (NUM_TILES_PROGRESS *
	  game.player_ptr->research.bulbs_researched) /
      (total_bulbs_required(game.player_ptr) + 1);
}

/**************************************************************************
Return the sprite index for the global-warming indicator.
**************************************************************************/
int client_warming_sprite(void)
{
  int index;
  if ((game.globalwarming <= 0) &&
      (game.heating < (NUM_TILES_PROGRESS / 2))) {
    index = MAX(0, game.heating);
  } else {
    index = MIN(NUM_TILES_PROGRESS,
		(MAX(0, 4 + game.globalwarming) / 5) +
		((NUM_TILES_PROGRESS / 2) - 1));
  }
  return index;
}

/**************************************************************************
Return the sprite index for the global-cooling indicator.
**************************************************************************/
int client_cooling_sprite(void)
{
  int index;
  if ((game.nuclearwinter <= 0) &&
      (game.cooling < (NUM_TILES_PROGRESS / 2))) {
    index = MAX(0, game.cooling);
  } else {
    index = MIN(NUM_TILES_PROGRESS,
		(MAX(0, 4 + game.nuclearwinter) / 5) +
		((NUM_TILES_PROGRESS / 2) - 1));
  }
  return index;
}

/**************************************************************************
Find something sensible to display. This is used to overwrite the
intro gfx.
**************************************************************************/
void center_on_something(void)
{
  struct city *pcity;
  struct unit *punit;

  if (!can_client_change_view()) {
    return;
  }

  if ((punit = get_unit_in_focus())) {
    center_tile_mapcanvas(punit->x, punit->y);
  } else if ((pcity = find_palace(game.player_ptr))) {
    /* Else focus on the capital. */
    center_tile_mapcanvas(pcity->x, pcity->y);
  } else if (city_list_size(&game.player_ptr->cities) > 0) {
    /* Just focus on any city. */
    pcity = city_list_get(&game.player_ptr->cities, 0);
    assert(pcity != NULL);
    center_tile_mapcanvas(pcity->x, pcity->y);
  } else if (unit_list_size(&game.player_ptr->units) > 0) {
    /* Just focus on any unit. */
    punit = unit_list_get(&game.player_ptr->units, 0);
    assert(punit != NULL);
    center_tile_mapcanvas(punit->x, punit->y);
  } else {
    /* Just any known tile will do; search near the middle first. */
    int center_map_x, center_map_y;

    native_to_map_pos(&center_map_x, &center_map_y,
		      map.xsize / 2, map.ysize / 2);

    /* Iterate outward from the center tile.  We have to give a radius that
     * is guaranteed to be larger than the map will be.  Although this is
     * a misuse of map.xsize and map.ysize (which are native dimensions),
     * it should give a sufficiently large radius. */
    iterate_outward(center_map_x, center_map_y,
		    map.xsize + map.ysize, x, y) {
      if (tile_get_known(x, y) != TILE_UNKNOWN) {
	center_tile_mapcanvas(x, y);
	return;
      }
    } iterate_outward_end;

    /* If we get here we didn't find a known tile.
       Refresh a random place to clear the intro gfx. */
    center_tile_mapcanvas(center_map_x, center_map_y);
  }
}

/**************************************************************************
...
**************************************************************************/
cid cid_encode(bool is_unit, int id)
{
  return id + (is_unit ? B_LAST : 0);
}

/**************************************************************************
...
**************************************************************************/
cid cid_encode_from_city(struct city * pcity)
{
  return cid_encode(pcity->is_building_unit, pcity->currently_building);
}

/**************************************************************************
...
**************************************************************************/
void cid_decode(cid cid, bool *is_unit, int *id)
{
  *is_unit = cid_is_unit(cid);
  *id = cid_id(cid);
}

/**************************************************************************
...
**************************************************************************/
bool cid_is_unit(cid cid)
{
  return (cid >= B_LAST);
}

/**************************************************************************
...
**************************************************************************/
int cid_id(cid cid)
{
  return (cid >= B_LAST) ? (cid - B_LAST) : cid;
}

/**************************************************************************
...
**************************************************************************/
wid wid_encode(bool is_unit, bool is_worklist, int id)
{
  assert(!is_unit || !is_worklist);

  if (is_unit)
    return id + B_LAST;
  if (is_worklist)
    return id + B_LAST + U_LAST;
  return id;
}

/**************************************************************************
...
**************************************************************************/
bool wid_is_unit(wid wid)
{
  assert(wid != WORKLIST_END);

  return (wid >= B_LAST && wid < B_LAST + U_LAST);
}

/**************************************************************************
...
**************************************************************************/
bool wid_is_worklist(wid wid)
{
  assert(wid != WORKLIST_END);

  return (wid >= B_LAST + U_LAST);
}

/**************************************************************************
...
**************************************************************************/
int wid_id(wid wid)
{
  assert(wid != WORKLIST_END);

  if (wid >= B_LAST + U_LAST)
    return wid - (B_LAST + U_LAST);
  if (wid >= B_LAST)
    return wid - B_LAST;
  return wid;
}

/****************************************************************
...
*****************************************************************/
bool city_can_build_impr_or_unit(struct city *pcity, cid cid)
{
  if (cid_is_unit(cid))
    return can_build_unit(pcity, cid_id(cid));
  else
    return can_build_improvement(pcity, cid_id(cid));
}

/****************************************************************
...
*****************************************************************/
bool city_unit_supported(struct city *pcity, cid cid)
{
  if (cid_is_unit(cid)) {
    int unit_type = cid_id(cid);

    unit_list_iterate(pcity->units_supported, punit) {
      if (punit->type == unit_type)
	return TRUE;
    }
    unit_list_iterate_end;
  }
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
bool city_unit_present(struct city *pcity, cid cid)
{
  if (cid_is_unit(cid)) {
    int unit_type = cid_id(cid);

    unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
      if (punit->type == unit_type)
	return TRUE;
    }
    unit_list_iterate_end;
  }
  return FALSE;
}

/****************************************************************************
  A TestCityFunc to tell whether the item is a building and is present.
****************************************************************************/
bool city_building_present(struct city *pcity, cid cid)
{
  if (!cid_is_unit(cid)) {
    int impr_type = cid_id(cid);

    return city_got_building(pcity, impr_type);
  }
  return FALSE;
}

/**************************************************************************
 Helper for name_and_sort_items.
**************************************************************************/
static int my_cmp(const void *p1, const void *p2)
{
  const struct item *i1 = (const struct item *) p1;
  const struct item *i2 = (const struct item *) p2;

  if (i1->section == i2->section)
    return mystrcasecmp(i1->descr, i2->descr);
  return (i1->section - i2->section);
}

/**************************************************************************
 Takes an array of compound ids (cids). It will fill out an array of
 struct items and also sort it.

 section 0: normal buildings
 section 1: B_CAPITAL
 section 2: F_NONMIL units
 section 3: other units
 section 4: wonders
**************************************************************************/
void name_and_sort_items(int *pcids, int num_cids, struct item *items,
			 bool show_cost, struct city *pcity)
{
  int i;

  for (i = 0; i < num_cids; i++) {
    bool is_unit = cid_is_unit(pcids[i]);
    int id = cid_id(pcids[i]), cost;
    struct item *pitem = &items[i];
    const char *name;

    pitem->cid = pcids[i];

    if (is_unit) {
      name = get_unit_name(id);
      cost = unit_build_shield_cost(id);
      pitem->section = unit_type_flag(id, F_NONMIL) ? 2 : 3;
    } else {
      name = get_impr_name_ex(pcity, id);
      if (id == B_CAPITAL) {
	cost = -1;
	pitem->section = 1;
      } else {
	cost = impr_build_shield_cost(id);
	if (is_wonder(id)) {
      	  pitem->section = 4;
        } else {
	  pitem->section = 0;
	}
      }
    }

    if (show_cost) {
      if (cost < 0) {
	my_snprintf(pitem->descr, sizeof(pitem->descr), "%s (XX)", name);
      } else {
	my_snprintf(pitem->descr, sizeof(pitem->descr), "%s (%d)", name, cost);
      }
    } else {
      (void) mystrlcpy(pitem->descr, name, sizeof(pitem->descr));
    }
  }

  qsort(items, num_cids, sizeof(struct item), my_cmp);
}

/**************************************************************************
...
**************************************************************************/
int collect_cids1(cid * dest_cids, struct city **selected_cities,
		  int num_selected_cities, bool append_units,
		  bool append_wonders, bool change_prod,
		  bool (*test_func) (struct city *, int))
{
  cid first = append_units ? B_LAST : 0;
  cid last = append_units ? game.num_unit_types + B_LAST : B_LAST;
  cid cid;
  int items_used = 0;

  for (cid = first; cid < last; cid++) {
    bool append = FALSE;
    int id = cid_id(cid);

    if (!append_units && (append_wonders != is_wonder(id)))
      continue;

    if (!change_prod) {
      city_list_iterate(game.player_ptr->cities, pcity) {
	append |= test_func(pcity, cid);
      }
      city_list_iterate_end;
    } else {
      int i;

      for (i = 0; i < num_selected_cities; i++) {
	append |= test_func(selected_cities[i], cid);
      }
    }

    if (!append)
      continue;

    dest_cids[items_used] = cid;
    items_used++;
  }
  return items_used;
}

/**************************************************************************
 Collect the cids of all targets (improvements and units) which are
 currently built in a city.
**************************************************************************/
int collect_cids2(cid * dest_cids)
{
  bool mapping[B_LAST + U_LAST];
  int cids_used = 0;
  cid cid;

  memset(mapping, 0, sizeof(mapping));
  city_list_iterate(game.player_ptr->cities, pcity) {
    mapping[cid_encode_from_city(pcity)] = TRUE;
  }
  city_list_iterate_end;

  for (cid = 0; cid < ARRAY_SIZE(mapping); cid++) {
    if (mapping[cid]) {
      dest_cids[cids_used] = cid;
      cids_used++;
    }
  }
  return cids_used;
}

/**************************************************************************
 Collect the cids of all targets (improvements and units) which can
 be build in a city.
**************************************************************************/
int collect_cids3(cid * dest_cids)
{
  int cids_used = 0;

  impr_type_iterate(id) {
    if (can_player_build_improvement(game.player_ptr, id)) {
      dest_cids[cids_used] = cid_encode(FALSE, id);
      cids_used++;
    }
  } impr_type_iterate_end;

  unit_type_iterate(id) {
    if (can_player_build_unit(game.player_ptr, id)) {
      dest_cids[cids_used] = cid_encode(TRUE, id);
      cids_used++;
    }
  } unit_type_iterate_end

  return cids_used;
}

/**************************************************************************
 Collect the cids of all targets which can be build by this city or
 in general.
**************************************************************************/
int collect_cids4(cid * dest_cids, struct city *pcity, bool advanced_tech)
{
  int cids_used = 0;

  impr_type_iterate(id) {
    bool can_build = can_player_build_improvement(game.player_ptr, id);
    bool can_eventually_build =
	could_player_eventually_build_improvement(game.player_ptr, id);

    /* If there's a city, can the city build the improvement? */
    if (pcity) {
      can_build = can_build && can_build_improvement(pcity, id);
      can_eventually_build = can_eventually_build &&
	  can_eventually_build_improvement(pcity, id);
    }

    if ((advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build)) {
      dest_cids[cids_used] = cid_encode(FALSE, id);
      cids_used++;
    }
  } impr_type_iterate_end;

  unit_type_iterate(id) {
    bool can_build = can_player_build_unit(game.player_ptr, id);
    bool can_eventually_build =
	can_player_eventually_build_unit(game.player_ptr, id);

    /* If there's a city, can the city build the unit? */
    if (pcity) {
      can_build = can_build && can_build_unit(pcity, id);
      can_eventually_build = can_eventually_build &&
	  can_eventually_build_unit(pcity, id);
    }

    if ((advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build)) {
      dest_cids[cids_used] = cid_encode(TRUE, id);
      cids_used++;
    }
  } unit_type_iterate_end;

  return cids_used;
}

/**************************************************************************
 Collect the cids of all improvements which are built in the given city.
**************************************************************************/
int collect_cids5(cid * dest_cids, struct city *pcity)
{
  int cids_used = 0;

  assert(pcity != NULL);

  built_impr_iterate(pcity, id) {
    dest_cids[cids_used] = cid_encode(FALSE, id);
    cids_used++;
  } built_impr_iterate_end;

  return cids_used;
}

/**************************************************************************
 Collect the wids of all possible targets of the given city.
**************************************************************************/
int collect_wids1(wid * dest_wids, struct city *pcity, bool wl_first, 
                  bool advanced_tech)
{
  cid cids[U_LAST + B_LAST];
  int item, cids_used, wids_used = 0;
  struct item items[U_LAST + B_LAST];

  /* Fill in the global worklists now?                      */
  /* perhaps judicious use of goto would be good here? -mck */
  if (wl_first && game.player_ptr->worklists[0].is_valid && pcity) {
    int i;
    for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
      if (game.player_ptr->worklists[i].is_valid) {
	dest_wids[wids_used] = wid_encode(FALSE, TRUE, i);
	wids_used++;
      }
    }
  }

  /* Fill in improvements and units */
  cids_used = collect_cids4(cids, pcity, advanced_tech);
  name_and_sort_items(cids, cids_used, items, FALSE, pcity);

  for (item = 0; item < cids_used; item++) {
    cid cid = items[item].cid;
    dest_wids[wids_used] = wid_encode(cid_is_unit(cid), FALSE, cid_id(cid));
    wids_used++;
  }

  /* we didn't fill in the global worklists above */
  if (!wl_first && game.player_ptr->worklists[0].is_valid && pcity) {
    int i;
    for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
      if (game.player_ptr->worklists[i].is_valid) {
        dest_wids[wids_used] = wid_encode(FALSE, TRUE, i);
        wids_used++;
      }
    }
  }

  return wids_used;
}

/**************************************************************************
...
**************************************************************************/
int num_supported_units_in_city(struct city *pcity)
{
  struct unit_list *plist;

  if (pcity->owner != game.player_idx) {
    plist = &pcity->info_units_supported;
  } else {
    plist = &pcity->units_supported;
  }

  return unit_list_size(plist);
}

/**************************************************************************
...
**************************************************************************/
int num_present_units_in_city(struct city *pcity)
{
  struct unit_list *plist;

  if (pcity->owner != game.player_idx) {
    plist = &pcity->info_units_present;
  } else {
    plist = &map_get_tile(pcity->x, pcity->y)->units;
  }

  return unit_list_size(plist);
}

/**************************************************************************
  Creates a struct packet_generic_message packet and injects it via
  handle_chat_msg.
**************************************************************************/
void create_event(int x, int y, enum event_type event,
		  const char *format, ...)
{
  va_list ap;
  char message[MAX_LEN_MSG];

  va_start(ap, format);
  my_vsnprintf(message, sizeof(message), format, ap);
  va_end(ap);

  handle_chat_msg(message, x, y, event);
}

/**************************************************************************
  Writes the supplied string into the file civgame.log.
**************************************************************************/
void write_chatline_content(const char *txt)
{
  FILE *fp = fopen("civgame.log", "w");	/* should allow choice of name? */

  append_output_window(_("Exporting output window to civgame.log ..."));
  if (fp) {
    fputs(txt, fp);
    fclose(fp);
    append_output_window(_("Export complete."));
  } else {
    append_output_window(_("Export failed, couldn't write to file."));
  }
}

/**************************************************************************
  Freeze all reports and other GUI elements.
**************************************************************************/
void reports_freeze(void)
{
  freelog(LOG_DEBUG, "reports_freeze");

  meswin_freeze();
  plrdlg_freeze();
  report_dialogs_freeze();
  output_window_freeze();
}

/**************************************************************************
  Freeze all reports and other GUI elements until the given request
  was executed.
**************************************************************************/
void reports_freeze_till(int request_id)
{
  if (request_id != 0) {
    reports_freeze();
    set_reports_thaw_request(request_id);
  }
}

/**************************************************************************
  Thaw all reports and other GUI elements.
**************************************************************************/
void reports_thaw(void)
{
  freelog(LOG_DEBUG, "reports_thaw");

  meswin_thaw();
  plrdlg_thaw();
  report_dialogs_thaw();
  output_window_thaw();
}

/**************************************************************************
  Thaw all reports and other GUI elements unconditionally.
**************************************************************************/
void reports_force_thaw(void)
{
  meswin_force_thaw();
  plrdlg_force_thaw();
  report_dialogs_force_thaw();
  output_window_force_thaw();
}

/*************************************************************************
...
*************************************************************************/
enum known_type map_get_known(int x, int y, struct player *pplayer)
{
  assert(pplayer == game.player_ptr);
  return tile_get_known(x, y);
}

/**************************************************************************
  Find city nearest to given unit and optionally return squared city
  distance Parameter sq_dist may be NULL. Returns NULL only if no city is
  known. Favors punit owner's cities over other cities if equally distant.
**************************************************************************/
struct city *get_nearest_city(struct unit *punit, int *sq_dist)
{
  struct city *pcity_near;
  int pcity_near_dist;

  if ((pcity_near = map_get_city(punit->x, punit->y))) {
    pcity_near_dist = 0;
  } else {
    pcity_near = NULL;
    pcity_near_dist = -1;
    players_iterate(pplayer) {
      city_list_iterate(pplayer->cities, pcity_current) {
        int dist = sq_map_distance(pcity_current->x, pcity_current->y,
				   punit->x, punit->y);
        if (pcity_near_dist == -1 || dist < pcity_near_dist
	    || (dist == pcity_near_dist
		&& punit->owner == pcity_current->owner)) {
          pcity_near = pcity_current;
          pcity_near_dist = dist;
        }
      } city_list_iterate_end;
    } players_iterate_end;
  }

  if (sq_dist) {
    *sq_dist = pcity_near_dist;
  }

  return pcity_near;
}

/**************************************************************************
  Called when the "Buy" button is pressed in the city report for every
  selected city. Checks for coinage and sufficient funds or request the
  purchase if everything is ok.
**************************************************************************/
void cityrep_buy(struct city *pcity)
{
  int value = city_buy_cost(pcity);

  if (!pcity->is_building_unit && pcity->currently_building == B_CAPITAL) {
    char buf[512];

    my_snprintf(buf, sizeof(buf),
		_("Game: You don't buy %s in %s!"),
		improvement_types[B_CAPITAL].name, pcity->name);
    append_output_window(buf);
    return;
  }

  if (game.player_ptr->economic.gold >= value) {
    city_buy_production(pcity);
  } else {
    char buf[512];
    const char *name;

    if (pcity->is_building_unit) {
      name = get_unit_type(pcity->currently_building)->name;
    } else {
      name = get_impr_name_ex(pcity, pcity->currently_building);
    }

    my_snprintf(buf, sizeof(buf),
		_("Game: %s costs %d gold and you only have %d gold."),
		name, value, game.player_ptr->economic.gold);
    append_output_window(buf);
  }
}

void common_taxrates_callback(int i)
{
  int tax_end, lux_end, sci_end, tax, lux, sci;
  int delta = 10;

  if (!can_client_issue_orders()) {
    return;
  }

  lux_end = game.player_ptr->economic.luxury;
  sci_end = lux_end + game.player_ptr->economic.science;
  tax_end = 100;

  lux = game.player_ptr->economic.luxury;
  sci = game.player_ptr->economic.science;
  tax = game.player_ptr->economic.tax;

  i *= 10;
  if (i < lux_end) {
    lux -= delta;
    sci += delta;
  } else if (i < sci_end) {
    sci -= delta;
    tax += delta;
  } else {
    tax -= delta;
    lux += delta;
  }
  dsend_packet_player_rates(&aconnection, tax, lux, sci);
}
