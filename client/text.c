/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Project
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

#include <stdarg.h>
#include <string.h>
#include <math.h> /* ceil */

/* utility */
#include "astring.h"
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"

/* common */
#include "combat.h"
#include "fc_types.h" /* LINE_BREAK */
#include "game.h"
#include "government.h"
#include "map.h"
#include "research.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "climap.h"
#include "climisc.h"
#include "control.h"
#include "goto.h"

#include "text.h"


static int get_bulbs_per_turn(int *pours, bool *pteam, int *ptheirs);

/****************************************************************************
  Return a (static) string with a tile's food/prod/trade
****************************************************************************/
const char *get_tile_output_text(const struct tile *ptile)
{
  static struct astring str = ASTRING_INIT;
  int i;
  char output_text[O_LAST][16];

  for (i = 0; i < O_LAST; i++) {
    int before_penalty = 0;
    int x = city_tile_output(NULL, ptile, FALSE, i);

    if (NULL != client.conn.playing) {
      before_penalty = get_player_output_bonus(client.conn.playing,
                                               get_output_type(i),
                                               EFT_OUTPUT_PENALTY_TILE);
    }

    if (before_penalty > 0 && x > before_penalty) {
      fc_snprintf(output_text[i], sizeof(output_text[i]), "%d(-1)", x);
    } else {
      fc_snprintf(output_text[i], sizeof(output_text[i]), "%d", x);
    }
  }

  astr_set(&str, "%s/%s/%s", output_text[O_FOOD],
           output_text[O_SHIELD], output_text[O_TRADE]);

  return astr_str(&str);
}

/****************************************************************************
  For AIs, fill the buffer with their player name prefixed with "AI". For
  humans, just fill it with their username.
****************************************************************************/
static inline void get_full_username(char *buf, int buflen,
                                     const struct player *pplayer)
{
  if (!buf || buflen < 1) {
    return;
  }

  if (!pplayer) {
    buf[0] = '\0';
    return;
  }

  if (pplayer->ai_controlled) {
    /* TRANS: "AI <player name>" */
    fc_snprintf(buf, buflen, _("AI %s"), pplayer->name);
  } else {
    fc_strlcpy(buf, pplayer->username, buflen);
  }
}

/****************************************************************************
  Fill the buffer with the player's nation name (in adjective form) and
  optionally add the player's team name.
****************************************************************************/
static inline void get_full_nation(char *buf, int buflen,
                                   const struct player *pplayer)
{
  if (!buf || buflen < 1) {
    return;
  }

  if (!pplayer) {
    buf[0] = '\0';
    return;
  }

  if (pplayer->team) {
    /* TRANS: "<nation adjective>, team <team name>" */
    fc_snprintf(buf, buflen, _("%s, team %s"),
                nation_adjective_for_player(pplayer),
                team_name_translation(pplayer->team));
  } else {
    fc_strlcpy(buf, nation_adjective_for_player(pplayer), buflen);
  }
}

/****************************************************************************
  Text to popup on a middle-click in the mapview.
****************************************************************************/
const char *popup_info_text(struct tile *ptile)
{
  const char *activity_text;
  struct city *pcity = tile_city(ptile);
  struct unit *punit = find_visible_unit(ptile);
  const char *diplo_nation_plural_adjectives[DS_LAST] =
    {Q_("?nation:Neutral"), Q_("?nation:Hostile"),
     Q_("?nation:Neutral"),
     Q_("?nation:Peaceful"), Q_("?nation:Friendly"), 
     Q_("?nation:Mysterious"), Q_("?nation:Friendly(team)")};
  const char *diplo_city_adjectives[DS_LAST] =
    {Q_("?city:Neutral"), Q_("?city:Hostile"),
     Q_("?nation:Neutral"),
     Q_("?city:Peaceful"), Q_("?city:Friendly"), Q_("?city:Mysterious"),
     Q_("?city:Friendly(team)")};
  static struct astring str = ASTRING_INIT;
  char username[MAX_LEN_NAME + 32];
  char nation[2 * MAX_LEN_NAME + 32];
  int tile_x, tile_y, nat_x, nat_y;

  astr_clear(&str);
  index_to_map_pos(&tile_x, &tile_y, tile_index(ptile));
  astr_add_line(&str, _("Location: (%d, %d) [%d]"),
                tile_x, tile_y, tile_continent(ptile));
  index_to_native_pos(&nat_x, &nat_y, tile_index(ptile));
  astr_add_line(&str, _("Native coordinates: (%d, %d)"),
                nat_x, nat_y);

  if (client_tile_get_known(ptile) == TILE_UNKNOWN) {
    astr_add(&str, _("Unknown"));
    return astr_str(&str);
  }
  astr_add_line(&str, _("Terrain: %s"),  tile_get_info_text(ptile, 0));
  astr_add_line(&str, _("Food/Prod/Trade: %s"),
		get_tile_output_text(ptile));
  if (tile_has_special(ptile, S_HUT)) {
    astr_add_line(&str, _("Minor Tribe Village"));
  }
  if (BORDERS_DISABLED != game.info.borders && !pcity) {
    struct player *owner = tile_owner(ptile);

    get_full_username(username, sizeof(username), owner);
    get_full_nation(nation, sizeof(nation), owner);

    if (NULL != client.conn.playing && owner == client.conn.playing) {
      astr_add_line(&str, _("Our territory"));
    } else if (NULL != owner && NULL == client.conn.playing) {
      /* TRANS: "Territory of <username> (<nation + team>)" */
      astr_add_line(&str, _("Territory of %s (%s)"), username, nation);
    } else if (NULL != owner) {
      struct player_diplstate *ds = player_diplstate_get(client.conn.playing,
                                                         owner);

      if (ds->type == DS_CEASEFIRE) {
        int turns = ds->turns_left;

        astr_add_line(&str,
                      /* TRANS: "Territory of <username> (<nation + team>)
                       * (<number> turn cease-fire)" */
                      PL_("Territory of %s (%s) (%d turn cease-fire)",
                          "Territory of %s (%s) (%d turn cease-fire)",
                          turns),
                      username, nation, turns);
      } else {
        int type = ds->type;

        /* TRANS: "Territory of <username>
         * (<nation + team> | <diplomatic state>)" */
        astr_add_line(&str, _("Territory of %s (%s | %s)"),
                      username, nation,
                      diplo_nation_plural_adjectives[type]);
      }
    } else {
      astr_add_line(&str, _("Unclaimed territory"));
    }
  }
  if (pcity) {
    /* Look at city owner, not tile owner (the two should be the same, if
     * borders are in use). */
    struct player *owner = city_owner(pcity);
    const char *improvements[improvement_count()];
    int has_improvements = 0;

    get_full_username(username, sizeof(username), owner);
    get_full_nation(nation, sizeof(nation), owner);

    if (NULL == client.conn.playing || owner == client.conn.playing) {
      /* TRANS: "City: <city name> | <username> (<nation + team>)" */
      astr_add_line(&str, _("City: %s | %s (%s)"),
                    city_name(pcity), username, nation);
    } else {
      struct player_diplstate *ds
        = player_diplstate_get(client_player(), owner);
      if (ds->type == DS_CEASEFIRE) {
        int turns = ds->turns_left;

        /* TRANS:  "City: <city name> | <username>
         * (<nation + team>, <number> turn cease-fire)" */
        astr_add_line(&str, PL_("City: %s | %s (%s, %d turn cease-fire)",
                                "City: %s | %s (%s, %d turn cease-fire)",
                                turns),
                      city_name(pcity), username, nation, turns);
      } else {
        /* TRANS: "City: <city name> | <username>
         * (<nation + team>, <diplomatic state>)" */
        astr_add_line(&str, _("City: %s | %s (%s, %s)"),
                      city_name(pcity), username, nation,
                      diplo_city_adjectives[ds->type]);
      }
    }
    if (can_player_see_units_in_city(client_player(), pcity)) {
      int count = unit_list_size(ptile->units);

      if (count > 0) {
        astr_add(&str, PL_(" | Occupied with %d unit.",
                                " | Occupied with %d units.", count), count);
      } else {
        astr_add(&str, _(" | Not occupied."));
      }
    } else {
      if (pcity->client.occupied) {
        astr_add(&str, _(" | Occupied."));
      } else {
        astr_add(&str, _(" | Not occupied."));
      }
    }
    improvement_iterate(pimprove) {
      if (is_improvement_visible(pimprove)
          && city_has_building(pcity, pimprove)) {
        improvements[has_improvements++] =
            improvement_name_translation(pimprove);
      }
    } improvement_iterate_end;

    if (0 < has_improvements) {
      struct astring list = ASTRING_INIT;

      astr_build_and_list(&list, improvements, has_improvements);
      /* TRANS: %s is a list of "and"-separated improvements. */
      astr_add_line(&str, _("   with %s."), astr_str(&list));
      astr_free(&list);
    }

    unit_list_iterate(get_units_in_focus(), pfocus_unit) {
      struct city *hcity = game_city_by_number(pfocus_unit->homecity);

      if (unit_has_type_flag(pfocus_unit, UTYF_TRADE_ROUTE)
	  && can_cities_trade(hcity, pcity)
	  && can_establish_trade_route(hcity, pcity)) {
	/* TRANS: "Trade from Warsaw: 5" */
	astr_add_line(&str, _("Trade from %s: %d"),
		      city_name(hcity),
		      trade_between_cities(hcity, pcity));
      }
    } unit_list_iterate_end;
  }
  {
    const char *infratext = get_infrastructure_text(ptile->special,
                                                    ptile->bases,
                                                    ptile->roads);
    if (*infratext != '\0') {
      astr_add_line(&str, _("Infrastructure: %s"), infratext);
    }
  }
  activity_text = concat_tile_activity_text(ptile);
  if (strlen(activity_text) > 0) {
    astr_add_line(&str, _("Activity: %s"), activity_text);
  }
  if (punit && !pcity) {
    struct player *owner = unit_owner(punit);
    struct unit_type *ptype = unit_type(punit);

    get_full_username(username, sizeof(username), owner);
    get_full_nation(nation, sizeof(nation), owner);

    if (!client_player() || owner == client_player()) {
      struct city *pcity = player_city_by_number(owner, punit->homecity);

      if (pcity) {
        /* TRANS: "Unit: <unit type> | <username>
         * (<nation + team>, <homecity>)" */
        astr_add_line(&str, _("Unit: %s | %s (%s, %s)"),
                      utype_name_translation(ptype), username,
                      nation, city_name(pcity));
      } else {
        /* TRANS: "Unit: <unit type> | <username> (<nation + team>)" */
        astr_add_line(&str, _("Unit: %s | %s (%s)"),
                      utype_name_translation(ptype), username, nation);
      }
    } else if (NULL != owner) {
      struct player_diplstate *ds = player_diplstate_get(client_player(),
                                                         owner);
      if (ds->type == DS_CEASEFIRE) {
        int turns = ds->turns_left;

        /* TRANS:  "Unit: <unit type> | <username> (<nation + team>,
         * <number> turn cease-fire)" */
        astr_add_line(&str, PL_("Unit: %s | %s (%s, %d turn cease-fire)",
                                "Unit: %s | %s (%s, %d turn cease-fire)",
                                turns),
                      utype_name_translation(ptype),
                      username, nation, turns);
      } else {
        /* TRANS: "Unit: <unit type> | <username> (<nation + team>,
         * <diplomatic state>)" */
        astr_add_line(&str, _("Unit: %s | %s (%s, %s)"),
                      utype_name_translation(ptype), username, nation,
                      diplo_city_adjectives[ds->type]);
      }
    }

    unit_list_iterate(get_units_in_focus(), pfocus_unit) {
      int att_chance = FC_INFINITY, def_chance = FC_INFINITY;
      bool found = FALSE;

      unit_list_iterate(ptile->units, tile_unit) {
	if (unit_owner(tile_unit) != unit_owner(pfocus_unit)) {
	  int att = unit_win_chance(pfocus_unit, tile_unit) * 100;
	  int def = (1.0 - unit_win_chance(tile_unit, pfocus_unit)) * 100;

	  found = TRUE;

	  /* Presumably the best attacker and defender will be used. */
	  att_chance = MIN(att, att_chance);
	  def_chance = MIN(def, def_chance);
	}
      } unit_list_iterate_end;

      if (found) {
	/* TRANS: "Chance to win: A:95% D:46%" */
	astr_add_line(&str, _("Chance to win: A:%d%% D:%d%%"),
		      att_chance, def_chance);	
      }
    } unit_list_iterate_end;

    /* TRANS: A is attack power, D is defense power, FP is firepower,
     * HP is hitpoints (current and max). */
    astr_add_line(&str, _("A:%d D:%d FP:%d HP:%d/%d"),
                  ptype->attack_strength, ptype->defense_strength,
                  ptype->firepower, punit->hp, ptype->hp);
    {
      const char *veteran_name =
        utype_veteran_name_translation(ptype, punit->veteran);
      if (veteran_name) {
        astr_add(&str, " (%s)", veteran_name);
      }
    }

    if (unit_owner(punit) == client_player()
        || client_is_global_observer()) {
      /* Show bribe cost for own units. */
      astr_add_line(&str, _("Bribe cost: %d"), unit_bribe_cost(punit));
    } else {
      /* We can only give an (lower) boundary for units of other players. */
      astr_add_line(&str, _("Estimated bribe cost: > %d"),
                    unit_bribe_cost(punit));
    }

    if ((NULL == client.conn.playing || owner == client.conn.playing)
        && unit_list_size(ptile->units) >= 2) {
      /* TRANS: "5 more" units on this tile */
      astr_add(&str, _("  (%d more)"), unit_list_size(ptile->units) - 1);
    }
  }

  astr_break_lines(&str, LINE_BREAK);
  return astr_str(&str);
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
  int base_total[MAX_BASE_TYPES];
  int base_units[MAX_BASE_TYPES];
  int road_total[MAX_ROAD_TYPES];
  int road_units[MAX_ROAD_TYPES];
  int num_activities = 0;
  int pillaging = 0;
  int remains, turns, i;
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  memset(activity_total, 0, sizeof(activity_total));
  memset(activity_units, 0, sizeof(activity_units));
  memset(base_total, 0, sizeof(base_total));
  memset(base_units, 0, sizeof(base_units));
  memset(road_total, 0, sizeof(road_total));
  memset(road_units, 0, sizeof(road_units));

  unit_list_iterate(ptile->units, punit) {
    if (punit->activity == ACTIVITY_PILLAGE) {
      pillaging = 1;
    } else if (punit->activity == ACTIVITY_BASE) {
      base_total[punit->activity_target.obj.base] += punit->activity_count;
      base_total[punit->activity_target.obj.base] += get_activity_rate_this_turn(punit);
      base_units[punit->activity_target.obj.base] += get_activity_rate(punit);
    } else if (punit->activity == ACTIVITY_GEN_ROAD) {
      road_total[punit->activity_target.obj.road] += punit->activity_count;
      road_total[punit->activity_target.obj.road] += get_activity_rate_this_turn(punit);
      road_units[punit->activity_target.obj.road] += get_activity_rate(punit);
    } else {
      activity_total[punit->activity] += punit->activity_count;
      activity_total[punit->activity] += get_activity_rate_this_turn(punit);
      activity_units[punit->activity] += get_activity_rate(punit);
    }
  } unit_list_iterate_end;

  if (pillaging) {
    bv_special pillage_spe = get_unit_tile_pillage_set(ptile);
    bv_bases pillage_bases = get_unit_tile_pillage_base_set(ptile);
    bv_roads pillage_roads = get_unit_tile_pillage_road_set(ptile);
    if (BV_ISSET_ANY(pillage_spe)
        || BV_ISSET_ANY(pillage_bases)
        || BV_ISSET_ANY(pillage_roads)) {
      astr_add(&str, "%s(%s)", _("Pillage"),
               get_infrastructure_text(pillage_spe, pillage_bases, pillage_roads));
    } else {
      /* Untargeted pillaging is happening. */
      astr_add(&str, "%s", _("Pillage"));
    }
    num_activities++;
  }

  for (i = 0; i < ACTIVITY_LAST; i++) {
    if (i == ACTIVITY_BASE) {
      base_type_iterate(bp) {
        Base_type_id b = base_index(bp);
	if (base_units[b] > 0) {
	  remains = tile_activity_base_time(ptile, b) - base_total[b];
	  if (remains > 0) {
	    turns = 1 + (remains + base_units[b] - 1) / base_units[b];
	  } else {
	    /* base will be finished this turn */
	    turns = 1;
	  }
	  if (num_activities > 0) {
	    astr_add(&str, "/");
	  }
	  astr_add(&str, "%s(%d)", base_name_translation(bp), turns);
	  num_activities++;
	}
      } base_type_iterate_end;
    } else if (i == ACTIVITY_GEN_ROAD) {
      road_type_iterate(rp) {
        Road_type_id r = road_index(rp);
	if (road_units[r] > 0) {
	  remains = tile_activity_road_time(ptile, r) - road_total[r];
	  if (remains > 0) {
	    turns = 1 + (remains + road_units[r] - 1) / road_units[r];
	  } else {
	    /* road will be finished this turn */
	    turns = 1;
	  }
	  if (num_activities > 0) {
	    astr_add(&str, "/");
	  }
	  astr_add(&str, "%s(%d)", road_name_translation(rp), turns);
	  num_activities++;
	}
      } road_type_iterate_end;
    } else if (is_build_or_clean_activity(i) && activity_units[i] > 0) {
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

  return astr_str(&str);
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

  astr_add(&str, (sq_dist >= FAR_CITY_SQUARE_DIST)
                 /* TRANS: on own line immediately following \n, ... <city> */
                 ? _("far from %s")
                 : (sq_dist > 0)
                   /* TRANS: on own line immediately following \n, ... <city> */
                   ? _("near %s")
                   : (sq_dist == 0)
                     /* TRANS: on own line immediately following \n, ... <city> */
                     ? _("in %s")
                     : "%s",
                 pcity
                 ? city_name(pcity)
                 : "");

  return astr_str(&str);
}

/****************************************************************************
  Returns the unit description.

  FIXME: This function is not re-entrant because it returns a pointer to
  static data.
****************************************************************************/
const char *unit_description(struct unit *punit)
{
  int pcity_near_dist;
  struct city *pcity =
      player_city_by_number(unit_owner(punit), punit->homecity);
  struct city *pcity_near = get_nearest_city(punit, &pcity_near_dist);
  struct unit_type *ptype = unit_type(punit);
  static struct astring str = ASTRING_INIT;
  const struct player *pplayer = client_player();

  astr_clear(&str);

  astr_add(&str, "%s", utype_name_translation(ptype));

  {
    const char *veteran_name =
      utype_veteran_name_translation(ptype, punit->veteran);
    if (veteran_name) {
      astr_add(&str, " (%s)", veteran_name);
    }
  }

  if (pplayer == unit_owner(punit)) {
    unit_upkeep_astr(punit, &str);
  }
  astr_add(&str, "\n");
  unit_activity_astr(punit, &str);

  if (pcity) {
    /* TRANS: on own line immediately following \n, ... <city> */
    astr_add_line(&str, _("from %s"), city_name(pcity));
  } else {
    astr_add(&str, "\n");
  }

  astr_add_line(&str, "%s",
		get_nearest_city_text(pcity_near, pcity_near_dist));
#ifdef DEBUG
  astr_add_line(&str, "Unit ID: %d", punit->id);
#endif

  return astr_str(&str);
}

/****************************************************************************
  Describe the airlift capacity of a city for the given units (from their
  current positions).
  If pdest is non-NULL, describe its capacity as a destination, otherwise
  describe the capacity of the city the unit's currently in (if any) as a
  source. (If the units in the list are in different cities, this will
  probably not give a useful result in this case.)
  If not all of the listed units can be airlifted, return the description
  for those that can.
  Returns NULL if an airlift is not possible for any of the units.
****************************************************************************/
const char *get_airlift_text(const struct unit_list *punits,
                             const struct city *pdest)
{
  static struct astring str = ASTRING_INIT;
  bool src = (pdest == NULL);
  enum texttype { AL_IMPOSSIBLE, AL_UNKNOWN, AL_FINITE, AL_INFINITE }
      best = AL_IMPOSSIBLE;
  int cur = 0, max = 0;

  unit_list_iterate(punits, punit) {
    enum texttype this = AL_IMPOSSIBLE;
    enum unit_airlift_result result;

    /* NULL will tell us about the capability of airlifting from source */
    result = test_unit_can_airlift_to(client_player(), punit, pdest);

    switch(result) {
    case AR_NO_MOVES:
    case AR_WRONG_UNITTYPE:
    case AR_OCCUPIED:
    case AR_NOT_IN_CITY:
    case AR_BAD_SRC_CITY:
    case AR_BAD_DST_CITY:
      /* No chance of an airlift. */
      this = AL_IMPOSSIBLE;
      break;
    case AR_OK:
    case AR_OK_SRC_UNKNOWN:
    case AR_OK_DST_UNKNOWN:
    case AR_SRC_NO_FLIGHTS:
    case AR_DST_NO_FLIGHTS:
      /* May or may not be able to airlift now, but there's a chance we could
       * later */
      {
        const struct city *pcity = src ? tile_city(unit_tile(punit)) : pdest;
        fc_assert_ret_val(pcity != NULL, fc_strdup("-"));
        if (!src && (game.info.airlifting_style & AIRLIFTING_UNLIMITED_DEST)) {
          /* No restrictions on destination (and we can infer this even for
           * other players' cities). */
          this = AL_INFINITE;
        } else if (client_player() == city_owner(pcity)) {
          /* A city we know about. */
          int this_cur = pcity->airlift, this_max = city_airlift_max(pcity);
          if (this_max <= 0) {
            /* City known not to be airlift-capable. */
            this = AL_IMPOSSIBLE;
          } else {
            if (src
                && (game.info.airlifting_style & AIRLIFTING_UNLIMITED_SRC)) {
              /* Unlimited capacity. */
              this = AL_INFINITE;
            } else {
              /* Limited capacity (possibly zero right now). */
              this = AL_FINITE;
              /* Store the numbers. This whole setup assumes that numeric
               * capacity isn't unit-dependent. */
              if (best == AL_FINITE) {
                fc_assert(cur == this_cur && max == this_max);
              }
              cur = this_cur;
              max = this_max;
            }
          }
        } else {
          /* Unknown capacity. */
          this = AL_UNKNOWN;
        }
      }
      break;
    }

    /* Now take the most optimistic view. */
    best = MAX(best, this);
  } unit_list_iterate_end;

  switch(best) {
  case AL_IMPOSSIBLE:
    return NULL;
  case AL_UNKNOWN:
    astr_set(&str, "?");
    break;
  case AL_FINITE:
    astr_set(&str, "%d/%d", cur, max);
    break;
  case AL_INFINITE:
    astr_set(&str, _("Yes"));
    break;
  }

  return astr_str(&str);
}

/****************************************************************************
  Return total expected bulbs.
****************************************************************************/
static int get_bulbs_per_turn(int *pours, bool *pteam, int *ptheirs)
{
  const struct player_research *presearch;
  int ours = 0, theirs = 0;
  bool team = FALSE;

  if (!client_has_player()) {
    return 0;
  }
  presearch = player_research_get(client_player());

  /* Sum up science */
  players_iterate(pplayer) {
    if (pplayer == client_player()) {
      city_list_iterate(pplayer->cities, pcity) {
        ours += pcity->prod[O_SCIENCE];
      } city_list_iterate_end;

      if (game.info.tech_upkeep_style == 1) {
        ours -= player_research_get(pplayer)->tech_upkeep;
      }
    } else if (presearch == player_research_get(pplayer)) {
      team = TRUE;
      theirs += pplayer->bulbs_last_turn;

      if (game.info.tech_upkeep_style == 1) {
        theirs -= presearch->tech_upkeep;
      }
    }
  } players_iterate_end;

  if (pours) {
    *pours = ours;
  }
  if (pteam) {
    *pteam = team;
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
  bool team;
  int ours, theirs, perturn, upkeep;
  static struct astring str = ASTRING_INIT;
  struct astring ourbuf = ASTRING_INIT, theirbuf = ASTRING_INIT;
  struct player_research *research;

  astr_clear(&str);

  perturn = get_bulbs_per_turn(&ours, &team, &theirs);

  research = player_research_get(client_player());
  upkeep = research->tech_upkeep;

  if (NULL == client.conn.playing || (ours == 0 && theirs == 0
                                      && upkeep == 0)) {
    return _("Progress: no research");
  }

  if (A_UNSET == research->researching) {
    astr_add(&str, _("Progress: no research"));
  } else {
    int done = research->bulbs_researched;
    int total = total_bulbs_required(client_player());

    if (perturn > 0) {
      int turns = MAX(1, ceil((double)total) / perturn);

      astr_add(&str, PL_("Progress: %d turn/advance",
                         "Progress: %d turns/advance",
                         turns), turns);
    } else if (perturn < 0 ) {
      /* negative number of bulbs per turn due to tech upkeep */
      int turns = ceil((double) done / -perturn);

      astr_add(&str, PL_("Progress: %d turn/advance loss",
                         "Progress: %d turns/advance loss",
                         turns), turns);
    } else {
      /* no research */
      astr_add(&str, _("Progress: none"));
    }
  }
  astr_set(&ourbuf, PL_("%d bulb/turn", "%d bulbs/turn", ours), ours);
  if (team) {
    /* Techpool version */
    astr_set(&theirbuf,
             /* TRANS: This is appended to "%d bulb/turn" text */
             PL_(", %d bulb/turn from team",
                 ", %d bulbs/turn from team", theirs), theirs);
  } else {
    astr_clear(&theirbuf);
  }
  astr_add(&str, " (%s%s)", astr_str(&ourbuf), astr_str(&theirbuf));
  astr_free(&ourbuf);
  astr_free(&theirbuf);

  if (game.info.tech_upkeep_style == 1) {
    /* perturn is defined as: (bulbs produced) - upkeep */
    astr_add_line(&str, _("Bulbs produced per turn: %d"), perturn + upkeep);
    /* TRANS: keep leading space; appended to "Bulbs produced per turn: %d" */
    astr_add(&str, _(" (needed for technology upkeep: %d)"), upkeep);
  }

  return astr_str(&str);
}

/****************************************************************************
  Get the short science-target text.  This is usually shown directly in
  the progress bar.

     5/28 - 3 turns

  The "percent" value, if given, will be set to the completion percentage
  of the research target (actually it's a [0,1] scale not a percent).
****************************************************************************/
const char *get_science_target_text(double *percent)
{
  struct player_research *research = player_research_get(client_player());
  static struct astring str = ASTRING_INIT;

  if (!research) {
    return "-";
  }

  astr_clear(&str);
  if (research->researching == A_UNSET) {
    astr_add(&str, _("%d/- (never)"), research->bulbs_researched);
    if (percent) {
      *percent = 0.0;
    }
  } else {
    int total = total_bulbs_required(client.conn.playing);
    int done = research->bulbs_researched;
    int perturn = get_bulbs_per_turn(NULL, NULL, NULL);

    if (perturn > 0) {
      int turns = ceil( (double)(total - done) / perturn );

      astr_add(&str, PL_("%d/%d (%d turn)", "%d/%d (%d turns)", turns),
               done, total, turns);
    } else if (perturn < 0 ) {
      /* negative number of bulbs per turn due to tech upkeep */
      int turns = ceil( (double)done / -perturn );

      astr_add(&str, PL_("%d/%d (%d turn)", "%d/%d (%d turns)", turns),
               done, perturn, turns);
    } else {
      /* no research */
      astr_add(&str, _("%d/%d (never)"), done, total);
    }
    if (percent) {
      *percent = (double)done / (double)total;
      *percent = CLIP(0.0, *percent, 1.0);
    }
  }

  return astr_str(&str);
}

/****************************************************************************
  Set the science-goal-label text as if we're researching the given goal.
****************************************************************************/
const char *get_science_goal_text(Tech_type_id goal)
{
  int steps = num_unknown_techs_for_goal(client.conn.playing, goal);
  int bulbs_needed = total_bulbs_required_for_goal(client.conn.playing, goal);
  int turns;
  int perturn = get_bulbs_per_turn(NULL, NULL, NULL);
  struct player_research* research = player_research_get(client_player());
  static struct astring str = ASTRING_INIT;
  struct astring buf1 = ASTRING_INIT,
                 buf2 = ASTRING_INIT,
                 buf3 = ASTRING_INIT;

  if (!research) {
    return "-";
  }

  astr_clear(&str);

  if (is_tech_a_req_for_goal(client.conn.playing,
			     research->researching, goal)
      || research->researching == goal) {
    bulbs_needed -= research->bulbs_researched;
  }

  astr_set(&buf1,
           PL_("%d step", "%d steps", steps), steps);
  astr_set(&buf2,
           PL_("%d bulb", "%d bulbs", bulbs_needed), bulbs_needed);
  if (perturn > 0) {
    turns = (bulbs_needed + perturn - 1) / perturn;
    astr_set(&buf3,
             PL_("%d turn", "%d turns", turns), turns);
  } else {
    astr_set(&buf3, _("never"));
  }
  astr_add_line(&str, "(%s - %s - %s)",
                astr_str(&buf1), astr_str(&buf2), astr_str(&buf3));
  astr_free(&buf1);
  astr_free(&buf2);
  astr_free(&buf3);

  return astr_str(&str);
}

/****************************************************************************
  Return the text for the label on the info panel.  (This is traditionally
  shown to the left of the mapview.)

  Clicking on this text should bring up the get_info_label_text_popup text.
****************************************************************************/
const char *get_info_label_text(bool moreinfo)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (NULL != client.conn.playing) {
    astr_add_line(&str, _("Population: %s"),
		  population_to_text(civ_population(client.conn.playing)));
  }
  astr_add_line(&str, _("Year: %s (T%d)"),
		textyear(game.info.year), game.info.turn);

  if (NULL != client.conn.playing) {
    astr_add_line(&str, _("Gold: %d (%+d)"),
                  client.conn.playing->economic.gold,
                  player_get_expected_income(client.conn.playing));
    astr_add_line(&str, _("Tax: %d Lux: %d Sci: %d"),
                  client.conn.playing->economic.tax,
                  client.conn.playing->economic.luxury,
                  client.conn.playing->economic.science);
  }
  if (game.info.phase_mode == PMT_PLAYERS_ALTERNATE) {
    if (game.info.phase < 0 || game.info.phase >= player_count()) {
      astr_add_line(&str, _("Moving: Nobody"));
    } else {
      astr_add_line(&str, _("Moving: %s"),
                    player_name(player_by_number(game.info.phase)));
    }
  } else if (game.info.phase_mode == PMT_TEAMS_ALTERNATE) {
    if (game.info.phase < 0 || game.info.phase >= team_count()) {
      astr_add_line(&str, _("Moving: Nobody"));
    } else {
      astr_add_line(&str, _("Moving: %s"),
                    team_name_translation(team_by_number(game.info.phase)));
    }
  }

  if (moreinfo) {
    astr_add_line(&str, _("(Click for more info)"));
  }

  return astr_str(&str);
}

/****************************************************************************
  Return the text for the popup label on the info panel.  (This is
  traditionally done as a popup whenever the regular info text is clicked
  on.)
****************************************************************************/
const char *get_info_label_text_popup(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (NULL != client.conn.playing) {
    astr_add_line(&str, _("%s People"),
		  population_to_text(civ_population(client.conn.playing)));
  }
  astr_add_line(&str, _("Year: %s"), textyear(game.info.year));
  astr_add_line(&str, _("Turn: %d"), game.info.turn);

  if (NULL != client.conn.playing) {
    int perturn = get_bulbs_per_turn(NULL, NULL, NULL);
    int upkeep = player_research_get(client_player())->tech_upkeep;

    astr_add_line(&str, _("Gold: %d"),
		  client.conn.playing->economic.gold);
    astr_add_line(&str, _("Net Income: %d"),
		  player_get_expected_income(client.conn.playing));
    /* TRANS: Gold, luxury, and science rates are in percentage values. */
    astr_add_line(&str, _("Tax rates: Gold:%d%% Luxury:%d%% Science:%d%%"),
		  client.conn.playing->economic.tax,
		  client.conn.playing->economic.luxury,
		  client.conn.playing->economic.science);
    astr_add_line(&str, _("Researching %s: %s"),
		  advance_name_researching(client.conn.playing),
		  get_science_target_text(NULL));
    /* perturn is defined as: (bulbs produced) - upkeep */
    if (game.info.tech_upkeep_style == 1) {
      astr_add_line(&str, _("Bulbs per turn: %d - %d = %d"), perturn + upkeep,
                    upkeep, perturn);
    } else {
      fc_assert(upkeep == 0);
      astr_add_line(&str, _("Bulbs per turn: %d"), perturn);
    }
  }

  /* See also get_global_warming_tooltip and get_nuclear_winter_tooltip. */

  if (game.info.global_warming) {
    int chance, rate;
    global_warming_scaled(&chance, &rate, 100);
    astr_add_line(&str, _("Global warming chance: %d%% (%+d%%/turn)"),
                  chance, rate);
  } else {
    astr_add_line(&str, _("Global warming deactivated."));
  }

  if (game.info.nuclear_winter) {
    int chance, rate;
    nuclear_winter_scaled(&chance, &rate, 100);
    astr_add_line(&str, _("Nuclear winter chance: %d%% (%+d%%/turn)"),
                  chance, rate);
  } else {
    astr_add_line(&str, _("Nuclear winter deactivated."));
  }

  if (NULL != client.conn.playing) {
    astr_add_line(&str, _("Government: %s"),
		  government_name_for_player(client.conn.playing));
  }

  return astr_str(&str);
}

/****************************************************************************
  Return the title text for the unit info shown in the info panel.

  FIXME: this should be renamed.
****************************************************************************/
const char *get_unit_info_label_text1(struct unit_list *punits)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (punits) {
    int count = unit_list_size(punits);

    if (count == 1) {
      astr_add(&str, "%s", unit_name_translation(unit_list_get(punits, 0)));
    } else {
      astr_add(&str, PL_("%d unit", "%d units", count), count);
    }
  }
  return astr_str(&str);
}

/****************************************************************************
  Return the text body for the unit info shown in the info panel.

  FIXME: this should be renamed.
****************************************************************************/
const char *get_unit_info_label_text2(struct unit_list *punits, int linebreaks)
{
  static struct astring str = ASTRING_INIT;
  int count;

  astr_clear(&str);

  if (!punits) {
    return "";
  }

  count = unit_list_size(punits);

  /* This text should always have the same number of lines if
   * 'linebreaks' has no flags at all. Otherwise the GUI widgets may be
   * confused and try to resize themselves. If caller asks for
   * conditional 'linebreaks', it should take care of these problems
   * itself. */

  /* Line 1. Goto or activity text. */
  if (count > 0 && hover_state != HOVER_NONE) {
    int min, max;

    if (!goto_get_turns(&min, &max)) {
      /* TRANS: Impossible to reach goto target tile */
      astr_add_line(&str, "%s", Q_("?goto:Unreachable"));
    } else if (min == max) {
      astr_add_line(&str, _("Turns to target: %d"), max);
    } else {
      astr_add_line(&str, _("Turns to target: %d to %d"), min, max);
    }
  } else if (count == 1) {
    astr_add_line(&str, "%s",
		  unit_activity_text(unit_list_get(punits, 0)));
  } else if (count > 1) {
    astr_add_line(&str, PL_("%d unit selected",
			    "%d units selected",
			    count),
		  count);
  } else {
    astr_add_line(&str, _("No units selected."));
  }

  /* Lines 2, 3, and 4 vary. */
  if (count == 1) {
    struct unit *punit = unit_list_get(punits, 0);
    struct city *pcity = player_city_by_number(unit_owner(punit),
                                               punit->homecity);

    astr_add_line(&str, "%s", tile_get_info_text(unit_tile(punit),
                                                 linebreaks));
    {
      const char *infratext
        = get_infrastructure_text(unit_tile(punit)->special,
                                  unit_tile(punit)->bases,
                                  unit_tile(punit)->roads);
      if (*infratext != '\0') {
        astr_add_line(&str, "%s", infratext);
      } else {
        astr_add_line(&str, " ");
      }
    }
    if (pcity) {
      astr_add_line(&str, "%s", city_name(pcity));
    } else {
      astr_add_line(&str, " ");
    }
  } else if (count > 1) {
    int mil = 0, nonmil = 0;
    int types_count[U_LAST], i;
    struct unit_type *top[3];

    memset(types_count, 0, sizeof(types_count));
    unit_list_iterate(punits, punit) {
      if (unit_has_type_flag(punit, UTYF_CIVILIAN)) {
	nonmil++;
      } else {
	mil++;
      }
      types_count[utype_index(unit_type(punit))]++;
    } unit_list_iterate_end;

    top[0] = top[1] = top[2] = NULL;
    unit_type_iterate(utype) {
      if (!top[2]
	  || types_count[utype_index(top[2])] < types_count[utype_index(utype)]) {
	top[2] = utype;

	if (!top[1]
	    || types_count[utype_index(top[1])] < types_count[utype_index(top[2])]) {
	  top[2] = top[1];
	  top[1] = utype;

	  if (!top[0]
	      || types_count[utype_index(top[0])] < types_count[utype_index(utype)]) {
	    top[1] = top[0];
	    top[0] = utype;
	  }
	}
      }
    } unit_type_iterate_end;

    for (i = 0; i < 2; i++) {
      if (top[i] && types_count[utype_index(top[i])] > 0) {
	if (utype_has_flag(top[i], UTYF_CIVILIAN)) {
	  nonmil -= types_count[utype_index(top[i])];
	} else {
	  mil -= types_count[utype_index(top[i])];
	}
	astr_add_line(&str, "%d: %s",
		      types_count[utype_index(top[i])],
		      utype_name_translation(top[i]));
      } else {
	astr_add_line(&str, " ");
      }
    }

    if (top[2] && types_count[utype_index(top[2])] > 0
        && types_count[utype_index(top[2])] == nonmil + mil) {
      astr_add_line(&str, "%d: %s", types_count[utype_index(top[2])],
                    utype_name_translation(top[2]));
    } else if (nonmil > 0 && mil > 0) {
      astr_add_line(&str, _("Others: %d civil; %d military"), nonmil, mil);
    } else if (nonmil > 0) {
      astr_add_line(&str, _("Others: %d civilian"), nonmil);
    } else if (mil > 0) {
      astr_add_line(&str, _("Others: %d military"), mil);
    } else {
      astr_add_line(&str, " ");
    }
  } else {
    astr_add_line(&str, " ");
    astr_add_line(&str, " ");
    astr_add_line(&str, " ");
  }

  /* Line 5. Debug text. */
#ifdef DEBUG
  if (count == 1) {
    astr_add_line(&str, "(Unit ID %d)", unit_list_get(punits, 0)->id);
  } else {
    astr_add_line(&str, " ");
  }
#endif /* DEBUG */

  return astr_str(&str);
}

/****************************************************************************
  Return text about upgrading these unit lists.

  Returns TRUE iff any units can be upgraded.
****************************************************************************/
bool get_units_upgrade_info(char *buf, size_t bufsz,
			    struct unit_list *punits)
{
  if (unit_list_size(punits) == 0) {
    fc_snprintf(buf, bufsz, _("No units to upgrade!"));
    return FALSE;
  } else if (unit_list_size(punits) == 1) {
    return (UU_OK == unit_upgrade_info(unit_list_front(punits), buf, bufsz));
  } else {
    int upgrade_cost = 0;
    int num_upgraded = 0;
    int min_upgrade_cost = FC_INFINITY;

    unit_list_iterate(punits, punit) {
      if (unit_owner(punit) == client_player()
          && UU_OK == unit_upgrade_test(punit, FALSE)) {
	struct unit_type *from_unittype = unit_type(punit);
	struct unit_type *to_unittype = can_upgrade_unittype(client.conn.playing,
							     unit_type(punit));
	int cost = unit_upgrade_price(unit_owner(punit),
					   from_unittype, to_unittype);

	num_upgraded++;
	upgrade_cost += cost;
	min_upgrade_cost = MIN(min_upgrade_cost, cost);
      }
    } unit_list_iterate_end;
    if (num_upgraded == 0) {
      fc_snprintf(buf, bufsz, _("None of these units may be upgraded."));
      return FALSE;
    } else {
      /* This may trigger sometimes if you don't have enough money for
       * a full upgrade.  If you have enough to upgrade at least one, it
       * will do it. */
      /* Construct prompt in several parts to allow separate pluralisation
       * by localizations */
      char tbuf[MAX_LEN_MSG], ubuf[MAX_LEN_MSG];
      fc_snprintf(tbuf, ARRAY_SIZE(tbuf), PL_("Treasury contains %d gold.",
                                              "Treasury contains %d gold.",
                                              client_player()->economic.gold),
                  client_player()->economic.gold);
      /* TRANS: this whole string is a sentence fragment that is only ever
       * used by including it in another string (search comments for this
       * string to find it) */
      fc_snprintf(ubuf, ARRAY_SIZE(ubuf), PL_("Upgrade %d unit",
                                              "Upgrade %d units",
                                              num_upgraded),
                  num_upgraded);
      /* TRANS: This is complicated. The first %s is a pre-pluralised
       * sentence fragment "Upgrade %d unit(s)"; the second is pre-pluralised
       * "Treasury contains %d gold." So the whole thing reads
       * "Upgrade 13 units for 1000 gold?\nTreasury contains 2000 gold." */
      fc_snprintf(buf, bufsz, PL_("%s for %d gold?\n%s",
                                  "%s for %d gold?\n%s", upgrade_cost),
                  ubuf, upgrade_cost, tbuf);
      return TRUE;
    }
  }
}

/****************************************************************************
  Return text about disbanding these units.

  Returns TRUE iff any units can be disbanded.
****************************************************************************/
bool get_units_disband_info(char *buf, size_t bufsz,
			    struct unit_list *punits)
{
  if (unit_list_size(punits) == 0) {
    fc_snprintf(buf, bufsz, _("No units to disband!"));
    return FALSE;
  } else if (unit_list_size(punits) == 1) {
    if (unit_has_type_flag(unit_list_front(punits), UTYF_UNDISBANDABLE)) {
      fc_snprintf(buf, bufsz, _("%s refuses to disband!"),
                  unit_name_translation(unit_list_front(punits)));
      return FALSE;
    } else {
      /* TRANS: %s is a unit type */
      fc_snprintf(buf, bufsz, _("Disband %s?"),
                  unit_name_translation(unit_list_front(punits)));
      return TRUE;
    }
  } else {
    int count = 0;
    unit_list_iterate(punits, punit) {
      if (!unit_has_type_flag(punit, UTYF_UNDISBANDABLE)) {
        count++;
      }
    } unit_list_iterate_end;
    if (count == 0) {
      fc_snprintf(buf, bufsz, _("None of these units may be disbanded."));
      return FALSE;
    } else {
      /* TRANS: %d is never 0 or 1 */
      fc_snprintf(buf, bufsz, PL_("Disband %d unit?",
                                  "Disband %d units?", count), count);
      return TRUE;
    }
  }
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

  if (NULL != client.conn.playing) {
    struct player_research *research = player_research_get(client_player());

    if (research->researching == A_UNSET) {
      astr_add_line(&str, _("no research target."));
    } else {
      int turns = 0;
      int perturn = get_bulbs_per_turn(NULL, NULL, NULL);
      int done = research->bulbs_researched;
      int total = total_bulbs_required(client_player());
      struct astring buf1 = ASTRING_INIT, buf2 = ASTRING_INIT;

      if (perturn > 0) {
        turns = MAX(1, ceil((double) (total - done) / perturn));
      } else if (perturn < 0 ) {
        turns = ceil((double) done / -perturn);
      }

      if (turns == 0) {
        astr_set(&buf1, _("No progress"));
      } else {
        astr_set(&buf1, PL_("%d turn", "%d turns", turns), turns);
      }

      /* TRANS: <perturn> bulbs/turn */
      astr_set(&buf2, PL_("%d bulb/turn", "%d bulbs/turn", perturn), perturn);

      /* TRANS: <tech>: <amount>/<total bulbs> */
      astr_add_line(&str, _("%s: %d/%d (%s, %s)."),
                    advance_name_researching(client.conn.playing),
                    research->bulbs_researched,
                    total_bulbs_required(client.conn.playing),
                    astr_str(&buf1), astr_str(&buf2));
      
      astr_free(&buf1);
      astr_free(&buf2);
    }
  }
  return astr_str(&str);
}

/****************************************************************************
  Get a tooltip text for the info panel global warning indicator.  See also
  client_warming_sprite().
****************************************************************************/
const char *get_global_warming_tooltip(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (!game.info.global_warming) {
    astr_add_line(&str, _("Global warming deactivated."));
  } else {
    int chance, rate;
    global_warming_scaled(&chance, &rate, 100);
    astr_add_line(&str, _("Shows the progress of global warming:"));
    astr_add_line(&str, _("Pollution rate: %d%%"), rate);
    astr_add_line(&str, _("Chance of catastrophic warming each turn: %d%%"),
                  chance);
  }

  return astr_str(&str);
}

/****************************************************************************
  Get a tooltip text for the info panel nuclear winter indicator.  See also
  client_cooling_sprite().
****************************************************************************/
const char *get_nuclear_winter_tooltip(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (!game.info.nuclear_winter) {
    astr_add_line(&str, _("Nuclear winter deactivated."));
  } else {
    int chance, rate;
    nuclear_winter_scaled(&chance, &rate, 100);
    astr_add_line(&str, _("Shows the progress of nuclear winter:"));
    astr_add_line(&str, _("Fallout rate: %d%%"), rate);
    astr_add_line(&str, _("Chance of catastrophic winter each turn: %d%%"),
                  chance);
  }

  return astr_str(&str);
}

/****************************************************************************
  Get a tooltip text for the info panel government indicator.  See also
  government_by_number(...)->sprite.
****************************************************************************/
const char *get_government_tooltip(void)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add_line(&str, _("Shows your current government:"));

  if (NULL != client.conn.playing) {
    astr_add_line(&str, "%s",
		  government_name_for_player(client.conn.playing));
  }
  return astr_str(&str);
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

  return astr_str(&str);
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

  return astr_str(&str);
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

  return astr_str(&str);
}

/****************************************************************************
  Return text giving the ping time for the player.  This is generally used
  used in the playerdlg.  This should only be used in playerdlg_common.c.
****************************************************************************/
const char *get_ping_time_text(const struct player *pplayer)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  conn_list_iterate(pplayer->connections, pconn) {
    if (!pconn->observer
	/* Certainly not needed, but safer. */
	&& 0 == strcmp(pconn->username, pplayer->username)) {
      if (pconn->ping_time != -1) {
	double ping_time_in_ms = 1000 * pconn->ping_time;

	astr_add(&str, _("%6d.%02d ms"), (int) ping_time_in_ms,
		 ((int) (ping_time_in_ms * 100.0)) % 100);
      }
      break;
    }
  } conn_list_iterate_end;

  return astr_str(&str);
}

/****************************************************************************
  Return text giving the score of the player. This should only be used 
  in playerdlg_common.c.
****************************************************************************/
const char *get_score_text(const struct player *pplayer)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (pplayer->score.game > 0
      || NULL == client.conn.playing
      || pplayer == client.conn.playing) {
    astr_add(&str, "%d", pplayer->score.game);
  } else {
    astr_add(&str, "?");
  }

  return astr_str(&str);
}

/****************************************************************************
  Get the title for a "report".  This may include the city, economy,
  military, trade, player, etc., reports.  Some clients may generate the
  text themselves to get a better GUI layout.
****************************************************************************/
const char *get_report_title(const char *report_name)
{
  static struct astring str = ASTRING_INIT;
  const struct player *pplayer = client_player();

  astr_clear(&str);

  astr_add_line(&str, "%s", report_name);

  if (pplayer != NULL) {
    char buf[4 * MAX_LEN_NAME];

    /* TRANS: <nation adjective> <government name>.
     * E.g. "Polish Republic". */
    astr_add_line(&str, Q_("?nationgovernment:%s %s"),
                  nation_adjective_for_player(pplayer),
                  government_name_for_player(pplayer));

    /* TRANS: Just happending 2 strings, using the correct localized
     * syntax. */
    astr_add_line(&str, _("%s - %s"),
                  ruler_title_for_player(pplayer, buf, sizeof(buf)),
                  textyear(game.info.year));
  } else {
    /* TRANS: "Observer: 1985" */
    astr_add_line(&str, _("Observer - %s"),
		  textyear(game.info.year));
  }
  return astr_str(&str);
}

/****************************************************************************
  Describing buildings that affect happiness.
****************************************************************************/
const char *text_happiness_buildings(const struct city *pcity)
{
  char buf[512];
  int faces = 0;
  struct effect_list *plist = effect_list_new();
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add_line(&str, _("Buildings: "));

  get_city_bonus_effects(plist, pcity, NULL, EFT_MAKE_CONTENT);

  effect_list_iterate(plist, peffect) {
    get_effect_req_text(peffect, buf, sizeof(buf));
    if (faces++ > 0) {
      /* only one comment to translators needed. */
      astr_add(&str, Q_("?clistmore:, %s"), buf);
    } else {
      astr_add(&str, "%s", buf);
    }
  } effect_list_iterate_end;
  effect_list_destroy(plist);

  if (faces == 0) {
    astr_add(&str, _("None. "));
  } else {
    astr_add(&str, "%s", Q_("?clistend:."));
  }

  /* Add line breaks after 80 characters. */
  astr_break_lines(&str, 80);

  return astr_str(&str);
}

/****************************************************************************
  Describing wonders that affect happiness.
****************************************************************************/
const char *text_happiness_wonders(const struct city *pcity)
{
  char buf[512];
  int faces = 0;
  struct effect_list *plist = effect_list_new();
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add_line(&str, _("Wonders: "));
  get_city_bonus_effects(plist, pcity, NULL, EFT_MAKE_HAPPY);
  get_city_bonus_effects(plist, pcity, NULL, EFT_FORCE_CONTENT);
  get_city_bonus_effects(plist, pcity, NULL, EFT_NO_UNHAPPY);

  effect_list_iterate(plist, peffect) {
    get_effect_req_text(peffect, buf, sizeof(buf));
    if (faces++ > 0) {
      /* only one comment to translators needed. */
      astr_add(&str, Q_("?clistmore:, %s"), buf);
    } else {
      astr_add(&str, "%s", buf);
    }
  } effect_list_iterate_end;

  effect_list_destroy(plist);

  if (faces == 0) {
    astr_add(&str, _("None. "));
  } else {
    astr_add(&str, "%s",  Q_("?clistend:."));
  }

  /* Add line breaks after 80 characters. */
  astr_break_lines(&str, 80);

  return astr_str(&str);
}

/****************************************************************************
  Describing city factors that affect happiness.
****************************************************************************/
const char *text_happiness_cities(const struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  int cities = city_list_size(pplayer->cities);
  int content = get_player_bonus(pplayer, EFT_CITY_UNHAPPY_SIZE);
  int basis = get_player_bonus(pplayer, EFT_EMPIRE_SIZE_BASE);
  int step = get_player_bonus(pplayer, EFT_EMPIRE_SIZE_STEP);
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (basis+step <= 0) {
    /* Special case where penalty is disabled; see
     * player_content_citizens(). */
    astr_add_line(&str,
                  _("Cities: %d total, but no penalty for empire size."),
                cities);
  } else {
    int excess = cities - basis;
    int penalty = 0;

    if (excess > 0) {
      if (step > 0)
        penalty = 1 + (excess - 1) / step;
      else
        penalty = 1;
    } else {
      excess = 0;
      penalty = 0;
    }

    astr_add_line(&str,
                  _("Cities: %d total, %d over threshold of %d cities."),
                cities, excess, basis);
    astr_add_line(&str,
                  /* TRANS: 0-21 content [citizen(s)] ... */
                  PL_("%d content before penalty.",
                      "%d content before penalty.",
                      content),
                  content);
    astr_add_line(&str,
                  /* TRANS: 0-21 unhappy citizen(s). */
                  PL_("%d additional unhappy citizen.",
                      "%d additional unhappy citizens.",
                      penalty),
                  penalty);
  }

  return astr_str(&str);
}

/****************************************************************************
  Describing units that affect happiness.
****************************************************************************/
const char *text_happiness_units(const struct city *pcity)
{
  int mlmax = get_city_bonus(pcity, EFT_MARTIAL_LAW_MAX);
  int uhcfac = get_city_bonus(pcity, EFT_UNHAPPY_FACTOR);
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  if (mlmax > 0) {
    int mleach = get_city_bonus(pcity, EFT_MARTIAL_LAW_EACH);
    if (mlmax == 100) {
      astr_add_line(&str, "%s", _("Unlimited martial law in effect."));
    } else {
      astr_add_line(&str, PL_("%d military unit may impose martial law.",
                              "Up to %d military units may impose martial "
                              "law.", mlmax), mlmax);
    }
    astr_add_line(&str, PL_("Each military unit makes %d "
                            "unhappy citizen content.",
                            "Each military unit makes %d "
                            "unhappy citizens content.",
                            mleach), mleach);
  } else if (uhcfac > 0) {
    astr_add_line(&str,
                  _("Military units in the field may cause unhappiness. "));
  } else {
    astr_add_line(&str,
                  _("Military units have no happiness effect. "));
  }
  return astr_str(&str);
}

/****************************************************************************
  Describing luxuries that affect happiness.
****************************************************************************/
const char *text_happiness_luxuries(const struct city *pcity)
{
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  astr_add_line(&str,
                _("Luxury: %d total."),
                pcity->prod[O_LUXURY]);
  return astr_str(&str);
}
