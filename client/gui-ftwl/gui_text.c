/********************************************************************** 
 Freeciv - Copyright (C) 2004 - The Freeciv Poject
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
#include <stdarg.h>
#include <string.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "map.h"
#include "combat.h"
#include "government.h"

#include "climisc.h"
#include "civclient.h"
#include "control.h"
#include "goto.h"

#include "gui_text.h"

/****************************************************************************
  Return a short tooltip text for a terrain tile.
****************************************************************************/
const char *mapview_get_terrain_tooltip_text(int x, int y)
{
  int infrastructure = get_tile_infrastructure_set(map_get_tile(x, y));
  INIT;

  add_line(_("Location: (%d, %d) [%d]"),
	   x, y, map_get_tile(x, y)->continent);
  add_line("%s", map_get_tile_info_text(x, y));
  if (infrastructure) {
    add_line("%s",
	     map_get_infrastructure_text(infrastructure));
  }
  RETURN;
}

/****************************************************************************
  Calculate the effects of various unit activities.
****************************************************************************/
static void calc_effect(enum unit_activity activity, int x, int y, int diff[3])
{
  struct tile backup;
  int stats_before[3], stats_after[3];

  stats_before[0] = get_food_tile(x,y);
  stats_before[1] = get_shields_tile(x,y);
  stats_before[2] = get_trade_tile(x,y);

  /* BEWARE UGLY HACK AHEAD */

  backup = *map_get_tile(x, y);

  switch (activity) {
  case ACTIVITY_ROAD:
    map_set_special(x, y, S_ROAD);
    break;
  case ACTIVITY_RAILROAD:
    map_set_special(x, y, S_RAILROAD);
    break;
  case ACTIVITY_MINE:
    map_mine_tile(x, y);
    break;

  case ACTIVITY_IRRIGATE:
    map_irrigate_tile(x, y);
    break;

  case ACTIVITY_TRANSFORM:
    map_transform_tile(x, y);
    break;
  default:
    assert(0);
  }

  stats_after[0] = get_food_tile(x,y);
  stats_after[1] = get_shields_tile(x,y);
  stats_after[2] = get_trade_tile(x,y);

  *map_get_tile(x, y) = backup;
  reset_move_costs();
  /* hopefully everything is now back in place */

  diff[0] = stats_after[0] - stats_before[0];
  diff[1] = stats_after[1] - stats_before[1];
  diff[2] = stats_after[2] - stats_before[2];
}

/****************************************************************************
  Return a text describing what would be the results from a unit activity.
****************************************************************************/
static const char *format_effect(enum unit_activity activity,
				 struct unit *punit)
{
  char parts[3][25];
  int diff[3];
  int n = 0;
  INIT;

  calc_effect(activity, punit->x, punit->y, diff);

  if (diff[0] != 0) {
    my_snprintf(parts[n], sizeof(parts[n]), _("%+d food"), diff[0]);
    n++;
  }

  if (diff[1] != 0) {
    my_snprintf(parts[n], sizeof(parts[n]), _("%+d shield"), diff[1]);
    n++;
  }

  if (diff[2] != 0) {
    my_snprintf(parts[n], sizeof(parts[n]), _("%+d trade"), diff[2]);
    n++;
  }
  if (n == 0) {
    add(_("none"));
  } else if (n == 1) {
    add("%s", parts[0]);
  } else if (n == 2) {
    add("%s %s", parts[0], parts[1]);
  } else if (n == 3) {
    add("%s %s %s", parts[0], parts[1],		parts[2]);
  } else {
    assert(0);
  }
  RETURN;
}

/****************************************************************************
  Return a short text for tooltip describing what a unit action does.
****************************************************************************/
const char *mapview_get_unit_action_tooltip(struct unit *punit,
					    const char *action,
					    const char *shortcut_)
{
  char shortcut[256];
  INIT;

  if (shortcut_) {
    my_snprintf(shortcut, sizeof(shortcut), " (%s)", shortcut_);
  } else {
    my_snprintf(shortcut, sizeof(shortcut), "%s", "");
  }

  if (strcmp(action, "unit_fortifying") == 0) {
    add_line(_("Fortify%s"), shortcut);
    add_line(_("Time: 1 turn, then until changed"));
    add_line(_("Effect: +50%% defense bonus"));
  } else if (strcmp(action, "unit_disband") == 0) {
    add_line(_("Disband%s"), shortcut);
    add_line(_("Time: instantly, unit is destroyed"));
  } else if (strcmp(action, "unit_return_nearest") == 0) {
    add_line(_("Return to nearest city%s"), shortcut);
    add_line(_("Time: unknown"));
  } else if (strcmp(action, "unit_sentry") == 0) {
    add_line(_("Sentry%s"), shortcut);
    add_line(_("Time: instantly, lasts until changed"));
    add_line(_("Effect: Unit wakes up if enemy is near"));
  } else if (strcmp(action, "unit_add_to_city") == 0) {
    add_line(_("Add to city%s"), shortcut);
    add_line(_("Time: instantly, unit is destroyed"));
    add_line(_("Effect: city size +1"));
  } else if (strcmp(action, "unit_build_city") == 0) {
    add_line(_("Build city%s"), shortcut);
    add_line(_("Time: instantly, unit is destroyed"));
    add_line(_("Effect: create a city of size 1"));
  } else if (strcmp(action, "unit_road") == 0) {
    add_line(_("Build road%s"), shortcut);
    add_line(_("Time: %d turns"),
          get_turns_for_activity_at(punit, ACTIVITY_ROAD, punit->x, punit->y));
    add_line(_("Effect: %s"),
	     format_effect(ACTIVITY_ROAD, punit));
  } else if (strcmp(action, "unit_irrigate") == 0) {
    add_line(_("Build irrigation%s"),shortcut);
    add_line(_("Time: %d turns"),
      get_turns_for_activity_at(punit, ACTIVITY_IRRIGATE, punit->x, punit->y));
    add_line(_("Effect: %s"),
	     format_effect(ACTIVITY_IRRIGATE, punit));
  } else if (strcmp(action, "unit_mine") == 0) {
    add_line(_("Build mine%s"),shortcut);
    add_line(_("Time: %d turns"),
	 get_turns_for_activity_at(punit, ACTIVITY_MINE, punit->x, punit->y));
    add_line(_("Effect: %s"),
	     format_effect(ACTIVITY_MINE, punit));
  } else if (strcmp(action, "unit_auto_settler") == 0) {
    add_line(_("Auto-Settle%s"),shortcut);
    add_line(_("Time: unknown"));
    add_line(_("Effect: the computer performs settler activities"));
  } else {
#if 0
  ttype = map_get_tile(punit->x, punit->y)->terrain;
  tinfo = get_tile_type(ttype);
  if ((tinfo->irrigation_result != T_LAST)
      && (tinfo->irrigation_result != ttype)) {
    my_snprintf(irrtext, sizeof(irrtext), irrfmt,
		(get_tile_type(tinfo->irrigation_result))->terrain_name);
  } else if (map_has_special(punit->x, punit->y, S_IRRIGATION)
	     && player_knows_techs_with_flag(game.player_ptr, TF_FARMLAND)) {
    sz_strlcpy(irrtext, _("Bu_ild Farmland"));
  }
  if ((tinfo->mining_result != T_LAST) && (tinfo->mining_result != ttype)) {
    my_snprintf(mintext, sizeof(mintext), minfmt,
		(get_tile_type(tinfo->mining_result))->terrain_name);
  }
  if ((tinfo->transform_result != T_LAST)
      && (tinfo->transform_result != ttype)) {
    my_snprintf(transtext, sizeof(transtext), transfmt,
		(get_tile_type(tinfo->transform_result))->terrain_name);
  }

  menus_rename("<main>/_Orders/Build _Irrigation", irrtext);
  menus_rename("<main>/_Orders/Build _Mine", mintext);
  menus_rename("<main>/_Orders/Transf_orm Terrain", transtext);
#endif
    add_line("tooltip for action %s isn't written yet",
	     action);
    freelog(LOG_NORMAL, "warning: get_unit_action_tooltip: unknown action %s",
	    action);
  }
  RETURN;
}

/****************************************************************************
  Get a tooltip for a possible city action.
****************************************************************************/
const char *mapview_get_city_action_tooltip(struct city *pcity,
					    const char *action,
					    const char *shortcut_)
{
  INIT;

  if (strcmp(action, "city_buy") == 0) {
    const char *name;

    if (pcity->is_building_unit) {
      name = get_unit_type(pcity->currently_building)->name;
    } else {
      name = get_impr_name_ex(pcity, pcity->currently_building);
    }

    add_line(_("Buy production"));
    add_line(_("Cost: %d (%d in treasury)"),
	     city_buy_cost(pcity), game.player_ptr->economic.gold);
    add_line(_("Producting: %s (%d turns)"), name,
	     city_turns_to_build(pcity, pcity->currently_building,
				 pcity->is_building_unit, TRUE));
  } else {
    add_line("tooltip for action %s isn't written yet", action);
    freelog(LOG_NORMAL,
	    "warning: get_city_action_tooltip: unknown action %s", action);
  }
  RETURN;
}  

/************************************************************************
  Text to popup on middle-click
************************************************************************/
const char *mapview_get_terrain_info_text(int map_x, int map_y)
{
  const char *activity_text = concat_tile_activity_text(map_x, map_y);
  struct tile *ptile = map_get_tile(map_x, map_y);
  const char *diplo_nation_plural_adjectives[DS_LAST] =
    {Q_("?nation:Neutral"), Q_("?nation:Hostile"),
     "" /* unused, DS_CEASEFIRE*/,
     Q_("?nation:Peaceful"), Q_("?nation:Friendly"), 
     Q_("?nation:Mysterious")};
  INIT;

  add_line(_("Terrain: %s"),
	   map_get_tile_info_text(map_x, map_y));
  add_line(_("Food/Prod/Trade: %s"),
	   map_get_tile_fpt_text(map_x, map_y));
  if (tile_has_special(ptile, S_HUT)) {
    add_line(_("Minor Tribe Village"));
  }
  if (game.borders > 0) {
    struct player *owner = map_get_owner(map_x, map_y);
    struct player_diplstate *ds = game.player_ptr->diplstates;

    if (owner == game.player_ptr){
      add_line(_("Our Territory"));
    } else if (owner) {
      if (ds[owner->player_no].type == DS_CEASEFIRE) {
	int turns = ds[owner->player_no].turns_left;

	add_line(PL_("%s territory (%d turn ceasefire)",
				       "%s territory (%d turn ceasefire)",
				       turns),
		 get_nation_name(owner->nation), turns);
      } else {
	add_line(_("Territory of the %s %s"),
		 diplo_nation_plural_adjectives[ds[owner->player_no].type],
		 get_nation_name_plural(owner->nation));
      }
    } else {
      add_line(_("Unclaimed territory"));
    }
  }
  if (get_tile_infrastructure_set(ptile)) {
    add_line(_("Infrastructure: %s"),
	     map_get_infrastructure_text(ptile->special));
  }
  if (strlen(activity_text)) {
    add_line(_("Activity: %s"), activity_text);
  }
  RETURN;
}

/****************************************************************************
  Get a short tooltip for a city.
****************************************************************************/
const char *mapview_get_city_tooltip_text(struct city *pcity)
{
  struct player *owner = city_owner(pcity);
  INIT;

  add_line("%s", pcity->name);
  add_line("%s", owner->name);
  RETURN;
}

/****************************************************************************
  Get a longer tooltip for a city.
****************************************************************************/
const char *mapview_get_city_info_text(struct city *pcity)
{
  struct player *owner = city_owner(pcity);
  INIT;

  add_line(_("City: %s (%s)"), pcity->name,
	   get_nation_name(owner->nation));
  if (city_got_citywalls(pcity)) {
    add(_(" with City Walls"));
  }
  RETURN;
}

/****************************************************************************
  Get a tooltip for a unit.
****************************************************************************/
const char *mapview_get_unit_tooltip_text(struct unit *punit)
{
  struct unit_type *ptype = unit_type(punit);
  struct city *pcity =
      player_find_city_by_id(game.player_ptr, punit->homecity);
  INIT;

  add("%s", ptype->name);
  if (ptype->veteran[punit->veteran].name[0] != '\0') {
    add(" (%s)", ptype->veteran[punit->veteran].name);
  }
  add("\n");
  add_line("%s", unit_activity_text(punit));
  if (pcity) {
    add_line("%s", pcity->name);
  }
  RETURN;
}

/****************************************************************************
  Get a longer tooltip for a unit.
****************************************************************************/
const char *mapview_get_unit_info_text(struct unit *punit)
{
  int map_x = punit->x;
  int map_y = punit->y;
  const char *activity_text = concat_tile_activity_text(map_x, map_y);
  INIT;

  if (strlen(activity_text)) {
    add_line(_("Activity: %s"), activity_text);
  }
  if (punit) {
    char tmp[64] = { 0 };
    struct unit_type *ptype = unit_type(punit);

    if (punit->owner == game.player_idx) {
      struct city *pcity =
	  player_find_city_by_id(game.player_ptr, punit->homecity);

      if (pcity){
	my_snprintf(tmp, sizeof(tmp), "/%s", pcity->name);
      }
    }
    add_line(_("Unit: %s(%s%s)"), ptype->name,
	     get_nation_name(unit_owner(punit)->nation), tmp);
    if (punit->owner != game.player_idx) {
      struct unit *apunit = get_unit_in_focus();

      if (apunit) {
	/* chance to win when active unit is attacking the selected unit */
	int att_chance = unit_win_chance(apunit, punit) * 100;
	
	/* chance to win when selected unit is attacking the active unit */
	int def_chance = (1.0 - unit_win_chance(punit, apunit)) * 100;
	
	add_line(_("Chance to win: A:%d%% D:%d%%"), att_chance, def_chance);
      }
    }
    add_line(_("A:%d D:%d FP:%d HP:%d/%d%s"),
	     ptype->attack_strength,
	     ptype->defense_strength, ptype->firepower, punit->hp,
	     ptype->hp, punit->veteran ? _(" V") : "");
  } 
  RETURN;
}
