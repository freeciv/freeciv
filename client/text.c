/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Poject
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

#include "astring.h"
#include "map.h"
#include "combat.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"
#include "climisc.h"
#include "government.h"
#include "civclient.h"

#include "control.h"
#include "goto.h"
#include "text.h"

/****************************************************************************
  Return a (static) string with a tile's food/prod/trade
****************************************************************************/
const char *get_tile_output_text(const struct tile *ptile)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);
  astr_add_line(&str, "%d/%d/%d",
		get_output_tile(ptile, O_FOOD),
		get_output_tile(ptile, O_SHIELD),
		get_output_tile(ptile, O_TRADE));

  return str.str;
}

/****************************************************************************
  Text to popup on a middle-click in the mapview.
****************************************************************************/
const char *popup_info_text(struct tile *ptile)
{
  const char *activity_text;
  struct city *pcity = ptile->city;
  struct unit *punit = find_visible_unit(ptile);
  const char *diplo_nation_plural_adjectives[DS_LAST] =
    {Q_("?nation:Neutral"), Q_("?nation:Hostile"),
     "" /* unused, DS_CEASEFIRE*/,
     Q_("?nation:Peaceful"), Q_("?nation:Friendly"), 
     Q_("?nation:Mysterious"), Q_("?nation:Friendly(team)")};
  const char *diplo_city_adjectives[DS_LAST] =
    {Q_("?city:Neutral"), Q_("?city:Hostile"),
     "" /*unused, DS_CEASEFIRE */,
     Q_("?city:Peaceful"), Q_("?city:Friendly"), Q_("?city:Mysterious"),
     Q_("?city:Friendly(team)")};
  int infracount;
  bv_special infra;
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);
#ifdef DEBUG
  astr_add_line(&str, _("Location: (%d, %d) [%d]"), 
		ptile->x, ptile->y, ptile->continent); 
#endif /*DEBUG*/
  astr_add_line(&str, _("Terrain: %s"),  tile_get_info_text(ptile));
  astr_add_line(&str, _("Food/Prod/Trade: %s"),
		get_tile_output_text(ptile));
  if (tile_has_special(ptile, S_HUT)) {
    astr_add_line(&str, _("Minor Tribe Village"));
  }
  if (game.info.borders > 0 && !pcity) {
    struct player *owner = tile_get_owner(ptile);
    struct player_diplstate *ds = game.player_ptr->diplstates;

    if (owner == game.player_ptr){
      astr_add_line(&str, _("Our Territory"));
    } else if (owner) {
      if (ds[owner->player_no].type == DS_CEASEFIRE) {
	int turns = ds[owner->player_no].turns_left;

	/* TRANS: "Polish territory (5 turn ceasefire)" */
	astr_add_line(&str, PL_("%s territory (%d turn ceasefire)",
				"%s territory (%d turn ceasefire)",
				turns),
		      get_nation_name(owner->nation), turns);
      } else {
	/* TRANS: "Territory of the friendly Polish".  See the
	 * ?nation adjectives. */
	int type = ds[owner->player_no].type;

	astr_add_line(&str, _("Territory of the %s %s"),
		      diplo_nation_plural_adjectives[type],
		      get_nation_name_plural(owner->nation));
      }
    } else {
      astr_add_line(&str, _("Unclaimed territory"));
    }
  }
  if (pcity) {
    /* Look at city owner, not tile owner (the two should be the same, if
     * borders are in use). */
    struct player *owner = city_owner(pcity);
    struct player_diplstate *ds = game.player_ptr->diplstates;
    struct unit *apunit;

    if (owner == game.player_ptr){
      /* TRANS: "City: Warsaw (Polish)" */
      astr_add_line(&str, _("City: %s (%s)"), pcity->name,
		    get_nation_name(owner->nation));
    } else if (owner) {
      if (ds[owner->player_no].type == DS_CEASEFIRE) {
	int turns = ds[owner->player_no].turns_left;

	/* TRANS:  "City: Warsaw (Polish, 5 turn ceasefire)" */
        astr_add_line(&str, PL_("City: %s (%s, %d turn ceasefire)",
				"City: %s (%s, %d turn ceasefire)",
				turns),
		      pcity->name,
		      get_nation_name(owner->nation),
		      turns);
      } else {
        /* TRANS: "City: Warsaw (Polish,friendly)" */
        astr_add_line(&str, _("City: %s (%s, %s)"), pcity->name,
		      get_nation_name(owner->nation),
		      diplo_city_adjectives[ds[owner->player_no].type]);
      }
    }
    if (city_got_citywalls(pcity)) {
      /* TRANS: previous lines gave other information about the city. */
      astr_add(&str, " %s", _("with City Walls"));
    }

    if ((apunit = get_unit_in_focus())) {
      struct city *hcity = find_city_by_id(apunit->homecity);

      if (unit_flag(apunit, F_TRADE_ROUTE)
	  && can_cities_trade(hcity, pcity)
	  && can_establish_trade_route(hcity, pcity)) {
	/* TRANS: "Trade from Warsaw: 5" */
	astr_add_line(&str, _("Trade from %s: %d"),
		      hcity->name, trade_between_cities(hcity, pcity));
      }
    } 
  }
  infra = get_tile_infrastructure_set(ptile, &infracount);
  if (infracount > 0) {
    astr_add_line(&str, _("Infrastructure: %s"),
		  get_infrastructure_text(ptile->special));
  }
  activity_text = concat_tile_activity_text(ptile);
  if (strlen(activity_text) > 0) {
    astr_add_line(&str, _("Activity: %s"), activity_text);
  }
  if (punit && !pcity) {
    struct player *owner = unit_owner(punit);
    struct player_diplstate *ds = game.player_ptr->diplstates;
    struct unit_type *ptype = unit_type(punit);

    if (owner == game.player_ptr){
      struct city *pcity;
      char tmp[64] = {0};

      pcity = player_find_city_by_id(game.player_ptr, punit->homecity);
      if (pcity) {
	my_snprintf(tmp, sizeof(tmp), "/%s", pcity->name);
      }
      /* TRANS: "Unit: Musketeers (Polish/Warsaw)" */
      astr_add_line(&str, _("Unit: %s (%s%s)"), ptype->name,
		    get_nation_name(owner->nation),
		    tmp);
    } else if (owner) {
      if (ds[owner->player_no].type == DS_CEASEFIRE) {
	int turns = ds[owner->player_no].turns_left;

	/* TRANS:  "Unit: Musketeers (Polish, 5 turn ceasefire)" */
        astr_add_line(&str, PL_("Unit: %s (%s, %d turn ceasefire)",
				"Unit: %s (%s, %d turn ceasefire)",
				turns),
		      ptype->name,
		      get_nation_name(owner->nation),
		      turns);
      } else {
	/* TRANS: "Unit: Musketeers (Polish,friendly)" */
	astr_add_line(&str, _("Unit: %s (%s, %s)"), ptype->name,
		      get_nation_name(owner->nation),
		      diplo_city_adjectives[ds[owner->player_no].type]);
      }
    }

    if (owner != game.player_ptr){
      struct unit *apunit;
      if ((apunit = get_unit_in_focus())) {
	/* chance to win when active unit is attacking the selected unit */
	int att_chance = unit_win_chance(apunit, punit) * 100;
	
	/* chance to win when selected unit is attacking the active unit */
	int def_chance = (1.0 - unit_win_chance(punit, apunit)) * 100;

	/* TRANS: "Chance to win: A:95% D:46%" */
	astr_add_line(&str, _("Chance to win: A:%d%% D:%d%%"),
		      att_chance, def_chance);
      }
    }

    /* TRANS: A is attack power, D is defense power, FP is firepower,
     * HP is hitpoints (current and max). */
    /* FIXME: veteran status isn't handled properly here. */
    astr_add_line(&str, _("A:%d D:%d FP:%d HP:%d/%d%s"),
		  ptype->attack_strength, 
		  ptype->defense_strength, ptype->firepower, punit->hp, 
		  ptype->hp, punit->veteran ? _(" V") : "");
    if (owner == game.player_ptr
	&& unit_list_size(ptile->units) >= 2) {
      /* TRANS: "5 more" units on this tile */
      astr_add(&str, _("  (%d more)"), unit_list_size(ptile->units) - 1);
    }
  } 
  return str.str;
}

/****************************************************************************
  Creates the activity progress text for the given tile.

  This should only be used inside popup_info_text and should eventually be
  made static.
****************************************************************************/
const char *concat_tile_activity_text(struct tile *ptile)
{
  int activity_total[ACTIVITY_LAST];
  int activity_units[ACTIVITY_LAST];
  int num_activities = 0;
  int remains, turns, i;
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  memset(activity_total, 0, sizeof(activity_total));
  memset(activity_units, 0, sizeof(activity_units));

  unit_list_iterate(ptile->units, punit) {
    activity_total[punit->activity] += punit->activity_count;
    activity_total[punit->activity] += get_activity_rate_this_turn(punit);
    activity_units[punit->activity] += get_activity_rate(punit);
  } unit_list_iterate_end;

  for (i = 0; i < ACTIVITY_LAST; i++) {
    if (is_build_or_clean_activity(i) && activity_units[i] > 0) {
      if (num_activities > 0) {
	astr_add(&str, "/");
      }
      remains = tile_activity_time(i, ptile) - activity_total[i];
      if (remains > 0) {
	turns = 1 + (remains + activity_units[i] - 1) / activity_units[i];
      } else {
	/* activity will be finished this turn */
	turns = 1;
      }
      astr_add(&str, "%s(%d)", get_activity_text(i), turns);
      num_activities++;
    }
  }

  return str.str;
}

#define FAR_CITY_SQUARE_DIST (2*(6*6))

/****************************************************************************
  Returns the text describing the city and its distance.
****************************************************************************/
const char *get_nearest_city_text(struct city *pcity, int sq_dist)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  /* just to be sure */
  if (!pcity) {
    sq_dist = -1;
  }

  astr_add(&str, (sq_dist >= FAR_CITY_SQUARE_DIST) ? _("far from %s")
      : (sq_dist > 0) ? _("near %s")
      : (sq_dist == 0) ? _("in %s")
      : "%s", pcity ? pcity->name : "");

  return str.str;
}

/****************************************************************************
  Returns the unit description.
****************************************************************************/
const char *unit_description(struct unit *punit)
{
  int pcity_near_dist;
  struct city *pcity =
      player_find_city_by_id(game.player_ptr, punit->homecity);
  struct city *pcity_near = get_nearest_city(punit, &pcity_near_dist);
  struct unit_type *ptype = unit_type(punit);
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add(&str, "%s", ptype->name);

  if (ptype->veteran[punit->veteran].name[0] != '\0') {
    astr_add(&str, " (%s)", _(ptype->veteran[punit->veteran].name));
  }
  astr_add(&str, "\n");
  astr_add_line(&str, "%s", unit_activity_text(punit));

  if (pcity) {
    /* TRANS: "from Warsaw" */
    astr_add_line(&str, _("from %s"), pcity->name);
  } else {
    astr_add(&str, "\n");
  }

  astr_add_line(&str, "%s",
		get_nearest_city_text(pcity_near, pcity_near_dist));
#ifdef DEBUG
  astr_add_line(&str, "Unit ID: %d", punit->id);
#endif

  return str.str;
}

/****************************************************************************
  Return total expected bulbs.
****************************************************************************/
static int get_bulbs_per_turn(int *pours, int *ptheirs)
{
  struct player *plr = game.player_ptr;
  int ours = 0, theirs = 0;

  /* Sum up science */
  players_iterate(pplayer) {
    enum diplstate_type ds = pplayer_get_diplstate(plr, pplayer)->type;

    if (plr == pplayer) {
      city_list_iterate(pplayer->cities, pcity) {
        ours += pcity->prod[O_SCIENCE];
      } city_list_iterate_end;
    } else if (ds == DS_TEAM) {
      theirs += pplayer->bulbs_last_turn;
    }
  } players_iterate_end;

  if (pours) {
    *pours = ours;
  }
  if (ptheirs) {
    *ptheirs = theirs;
  }
  return ours + theirs;
}

/****************************************************************************
  Returns the text to display in the science dialog.
****************************************************************************/
const char *science_dialog_text(void)
{
  int turns_to_advance;
  struct player *plr = game.player_ptr;
  int ours, theirs;
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  get_bulbs_per_turn(&ours, &theirs);

  if (ours == 0 && theirs == 0) {
    astr_add(&str, _("Progress: no research"));
    return str.str;
  }
  assert(ours >= 0 && theirs >= 0);
  if (get_player_research(plr)->researching == A_UNSET) {
    if (theirs == 0) {
      astr_add(&str, _("Progress: no research target (%d pts/turn)"), ours);
    } else {
      astr_add(&str, _("Progress: no research target "
	    "(%d pts/turn, %d pts/turn from team)"), ours, theirs);
    }
    return str.str;
  }
  turns_to_advance = (total_bulbs_required(plr) + ours + theirs - 1)
                     / (ours + theirs);
  if (theirs == 0) {
    /* Simple version, no techpool */
    astr_add(&str, PL_("Progress: %d turn/advance (%d pts/turn)",
	    "Progress: %d turns/advance (%d pts/turn)",
	    turns_to_advance), turns_to_advance, ours);
  } else {
    /* Techpool version */
    astr_add(&str, PL_("Progress: %d turn/advance (%d pts/turn, "
	    "%d pts/turn from team)",
	    "Progress: %d turns/advance (%d pts/turn, "
	    "%d pts/turn from team)",
	    turns_to_advance), turns_to_advance, ours, theirs);
  }
  return str.str;
}

/****************************************************************************
  Set the science-goal-label text as if we're researching the given goal.
****************************************************************************/
const char *get_science_goal_text(Tech_type_id goal)
{
  int steps = num_unknown_techs_for_goal(game.player_ptr, goal);
  int bulbs = total_bulbs_required_for_goal(game.player_ptr, goal);
  int bulbs_needed = bulbs, turns;
  int perturn = get_bulbs_per_turn(NULL, NULL);
  char buf1[256], buf2[256], buf3[256];
  struct player_research* research = get_player_research(game.player_ptr);
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (is_tech_a_req_for_goal(game.player_ptr,
			     research->researching, goal)
      || research->researching == goal) {
    bulbs_needed -= research->bulbs_researched;
  }

  my_snprintf(buf1, sizeof(buf1),
	      PL_("%d step", "%d steps", steps), steps);
  my_snprintf(buf2, sizeof(buf2),
	      PL_("%d bulb", "%d bulbs", bulbs), bulbs);
  if (perturn > 0) {
    turns = (bulbs_needed + perturn - 1) / perturn;
    my_snprintf(buf3, sizeof(buf3),
		PL_("%d turn", "%d turns", turns), turns);
  } else {
    my_snprintf(buf3, sizeof(buf3), _("never"));
  }
  astr_add_line(&str, "(%s - %s - %s)", buf1, buf2, buf3);

  return str.str;
}

/****************************************************************************
  Return the text for the label on the info panel.  (This is traditionally
  shown to the left of the mapview.)
****************************************************************************/
const char *get_info_label_text(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add_line(&str, _("Population: %s"),
		population_to_text(civ_population(game.player_ptr)));
  astr_add_line(&str, _("Year: %s (T%d)"),
		textyear(game.info.year), game.info.turn);
  astr_add_line(&str, _("Gold: %d (%+d)"), game.player_ptr->economic.gold,
		player_get_expected_income(game.player_ptr));
  astr_add_line(&str, _("Tax: %d Lux: %d Sci: %d"),
		game.player_ptr->economic.tax,
		game.player_ptr->economic.luxury,
		game.player_ptr->economic.science);
  if (!game.info.simultaneous_phases) {
    astr_add_line(&str, _("Moving: %s"), get_player(game.info.phase)->name);
  }
  return str.str;
}

/****************************************************************************
  Return the title text for the unit info shown in the info panel.

  FIXME: this should be renamed.
****************************************************************************/
const char *get_unit_info_label_text1(struct unit *punit)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (punit) {
    struct unit_type *ptype = unit_type(punit);

    astr_add(&str, "%s", ptype->name);
  }
  return str.str;
}

/****************************************************************************
  Return the text body for the unit info shown in the info panel.

  FIXME: this should be renamed.
****************************************************************************/
const char *get_unit_info_label_text2(struct unit *punit)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  /* This text should always have the same number of lines.  Otherwise the
   * GUI widgets may be confused and try to resize themselves. */
  if (punit) {
    struct city *pcity =
	player_find_city_by_id(game.player_ptr, punit->homecity);
    int infracount;
    bv_special infrastructure =
      get_tile_infrastructure_set(punit->tile, &infracount);

    if (hover_unit == punit->id) {
      astr_add_line(&str, _("Turns to target: %d"), get_goto_turns());
    } else {
      astr_add_line(&str, "%s", unit_activity_text(punit));
    }

    astr_add_line(&str, "%s", tile_get_info_text(punit->tile));
    if (infracount > 0) {
      astr_add_line(&str, "%s", get_infrastructure_text(infrastructure));
    } else {
      astr_add_line(&str, " ");
    }
    if (pcity) {
      astr_add_line(&str, "%s", pcity->name);
    } else {
      astr_add_line(&str, " ");
    }
#ifdef DEBUG
    astr_add_line(&str, "(Unit ID %d)", punit->id);
#endif
  } else {
    astr_add(&str, "\n\n\n");
  }
  return str.str;
}

/****************************************************************************
  Get a tooltip text for the info panel research indicator.  See
  client_research_sprite().
****************************************************************************/
const char *get_bulb_tooltip(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add_line(&str, _("Shows your progress in "
			"researching the current technology."));
  if (get_player_research(game.player_ptr)->researching == A_UNSET) {
    astr_add_line(&str, _("no research target."));
  } else {
    /* TRANS: <tech>: <amount>/<total bulbs> */
    struct player_research *research = get_player_research(game.player_ptr);

    astr_add_line(&str, _("%s: %d/%d."),
		  get_tech_name(game.player_ptr, research->researching),
		  research->bulbs_researched,
		  total_bulbs_required(game.player_ptr));
  }
  return str.str;
}

/****************************************************************************
  Get a tooltip text for the info panel global warning indicator.  See also
  client_warming_sprite().
****************************************************************************/
const char *get_global_warming_tooltip(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  /* This mirrors the logic in update_environmental_upset. */
  astr_add_line(&str, _("Shows the progress of global warming:"));
  astr_add_line(&str, _("Pollution rate: %d%%"),
		DIVIDE(game.info.heating - game.info.warminglevel + 1, 2));
  astr_add_line(&str, _("Chance of catastrophic warming each turn: %d%%"),
		CLIP(0, (game.info.globalwarming + 1) / 2, 100));
  return str.str;
}

/****************************************************************************
  Get a tooltip text for the info panel nuclear winter indicator.  See also
  client_cooling_sprite().
****************************************************************************/
const char *get_nuclear_winter_tooltip(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  /* This mirrors the logic in update_environmental_upset. */
  astr_add_line(&str, _("Shows the progress of nuclear winter:"));
  astr_add_line(&str, _("Fallout rate: %d%%"),
		DIVIDE(game.info.heating - game.info.warminglevel + 1, 2));
  astr_add_line(&str, _("Chance of catastrophic winter each turn: %d%%"),
		CLIP(0, (game.info.globalwarming + 1) / 2, 100));
  return str.str;
}

/****************************************************************************
  Get a tooltip text for the info panel government indicator.  See also
  get_government(...)->sprite.
****************************************************************************/
const char *get_government_tooltip(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add(&str, _("Shows your current government:\n%s."),
      get_government_name(game.player_ptr->government));
  return str.str;
}

/****************************************************************************
  Returns a description of the given spaceship.  If there is no spaceship
  (pship is NULL) then text with dummy values is returned.
****************************************************************************/
const char *get_spaceship_descr(struct player_spaceship *pship)
{
  struct player_spaceship ship;
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (!pship) {
    pship = &ship;
    memset(&ship, 0, sizeof(ship));
  }

  /* TRANS: spaceship text; should have constant width. */
  astr_add_line(&str, _("Population:      %5d"), pship->population);

  /* TRANS: spaceship text; should have constant width. */
  astr_add_line(&str, _("Support:         %5d %%"),
		(int) (pship->support_rate * 100.0));

  /* TRANS: spaceship text; should have constant width. */
  astr_add_line(&str, _("Energy:          %5d %%"),
		(int) (pship->energy_rate * 100.0));

  /* TRANS: spaceship text; should have constant width. */
  astr_add_line(&str, PL_("Mass:            %5d ton",
			  "Mass:            %5d tons",
			  pship->mass), pship->mass);

  if (pship->propulsion > 0) {
    /* TRANS: spaceship text; should have constant width. */
    astr_add_line(&str, _("Travel time:     %5.1f years"),
		  (float) (0.1 * ((int) (pship->travel_time * 10.0))));
  } else {
    /* TRANS: spaceship text; should have constant width. */
    astr_add_line(&str, "%s", _("Travel time:        N/A     "));
  }

  /* TRANS: spaceship text; should have constant width. */
  astr_add_line(&str, _("Success prob.:   %5d %%"),
		(int) (pship->success_rate * 100.0));

  /* TRANS: spaceship text; should have constant width. */
  astr_add_line(&str, _("Year of arrival: %8s"),
		(pship->state == SSHIP_LAUNCHED)
		? textyear((int) (pship->launch_year +
				  (int) pship->travel_time))
		: "-   ");

  return str.str;
}

/****************************************************************************
  Get the text showing the timeout.  This is generally disaplyed on the info
  panel.
****************************************************************************/
const char *get_timeout_label_text(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (game.info.timeout <= 0) {
    astr_add(&str, "%s", Q_("?timeout:off"));
  } else {
    astr_add(&str, "%s", format_duration(get_seconds_to_turndone()));
  }

  return str.str;
}

/****************************************************************************
  Format a duration, in seconds, so it comes up in minutes or hours if
  that would be more meaningful.

  (7 characters, maximum.  Enough for, e.g., "99h 59m".)
****************************************************************************/
const char *format_duration(int duration)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (duration < 0) {
    duration = 0;
  }
  if (duration < 60) {
    astr_add(&str, Q_("?seconds:%02ds"), duration);
  } else if (duration < 3600) {	/* < 60 minutes */
    astr_add(&str, Q_("?mins/secs:%02dm %02ds"), duration / 60, duration % 60);
  } else if (duration < 360000) {	/* < 100 hours */
    astr_add(&str, Q_("?hrs/mns:%02dh %02dm"), duration / 3600, (duration / 60) % 60);
  } else if (duration < 8640000) {	/* < 100 days */
    astr_add(&str, Q_("?dys/hrs:%02dd %02dh"), duration / 86400,
	(duration / 3600) % 24);
  } else {
    astr_add(&str, "%s", Q_("?duration:overflow"));
  }

  return str.str;
}

/****************************************************************************
  Return text giving the ping time for the player.  This is generally used
  used in the playerdlg.  This should only be used in playerdlg_common.c.
****************************************************************************/
const char *get_ping_time_text(const struct player *pplayer)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (conn_list_size(pplayer->connections) > 0
      && conn_list_get(pplayer->connections, 0)->ping_time != -1.0) {
    double ping_time_in_ms =
	1000 * conn_list_get(pplayer->connections, 0)->ping_time;

    astr_add(&str, _("%6d.%02d ms"), (int) ping_time_in_ms,
	((int) (ping_time_in_ms * 100.0)) % 100);
  }

  return str.str;
}

/****************************************************************************
  Return text giving the score of the player. This should only be used 
  in playerdlg_common.c.
****************************************************************************/
const char *get_score_text(const struct player *pplayer)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (pplayer->score.game > 0 || pplayer == game.player_ptr) {
    astr_add(&str, "%d", pplayer->score.game);
  } else {
    astr_add(&str, "?");
  }

  return str.str;
}

/****************************************************************************
  Get the title for a "report".  This may include the city, economy,
  military, trade, player, etc., reports.  Some clients may generate the
  text themselves to get a better GUI layout.
****************************************************************************/
const char *get_report_title(const char *report_name)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add_line(&str, "%s", report_name);

  /* TRANS: "Republic of the Polish" */
  astr_add_line(&str, _("%s of the %s"),
		get_government_name(game.player_ptr->government),
		get_nation_name_plural(game.player_ptr->nation));

  astr_add_line(&str, "%s %s: %s",
		get_ruler_title(game.player_ptr->government,
				game.player_ptr->is_male,
				game.player_ptr->nation),
		game.player_ptr->name,
		textyear(game.info.year));
  return str.str;
}

/****************************************************************************
  Get the text describing buildings that affect happiness.
****************************************************************************/
const char *get_happiness_buildings(const struct city *pcity)
{
  int faces = 0;
  struct effect_list *plist = effect_list_new();
  char buf[512];
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add_line(&str, _("Buildings: "));

  get_city_bonus_effects(plist, pcity, NULL, EFT_MAKE_CONTENT);

  effect_list_iterate(plist, peffect) {
    if (faces != 0) {
      astr_add(&str, _(", "));
    }
    get_effect_req_text(peffect, buf, sizeof(buf));
    astr_add(&str, _("%s"), buf);
    faces++;
  } effect_list_iterate_end;
  effect_list_unlink_all(plist);
  effect_list_free(plist);

  if (faces == 0) {
    astr_add(&str, _("None. "));
  } else {
    astr_add(&str, _("."));
  }

  return str.str;
}

/****************************************************************************
  Get the text describing wonders that affect happiness.
****************************************************************************/
const char *get_happiness_wonders(const struct city *pcity)
{
  int faces = 0;
  struct effect_list *plist = effect_list_new();
  char buf[512];
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add_line(&str, _("Wonders: "));
  get_city_bonus_effects(plist, pcity, NULL, EFT_MAKE_HAPPY);
  get_city_bonus_effects(plist, pcity, NULL, EFT_FORCE_CONTENT);
  get_city_bonus_effects(plist, pcity, NULL, EFT_NO_UNHAPPY);

  effect_list_iterate(plist, peffect) {
    if (faces != 0) {
      astr_add(&str, _(", "));
    }
    get_effect_req_text(peffect, buf, sizeof(buf));
    astr_add(&str, _("%s"), buf);
    faces++;
  } effect_list_iterate_end;

  effect_list_unlink_all(plist);
  effect_list_free(plist);

  if (faces == 0) {
    astr_add(&str, _("None. "));
  } else {
    astr_add(&str, _("."));
  }

  return str.str;
}
