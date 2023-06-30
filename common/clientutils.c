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

/* utility */
#include "astring.h"

/* common */
#include "combat.h"
#include "extras.h"
#include "fc_types.h"
#include "game.h"  /* FIXME it's extra_type_iterate that needs this really */
#include "tile.h"
#include "world_object.h"

#include "clientutils.h"

/* This module contains functions that would belong to the client,
 * except that in case of freeciv-web, server does handle these
 * for the web client. */

/* Stores the expected completion time for all activities on a tile.
 * (If multiple activities can create/remove an extra, they will
 * proceed in parallel and the first to complete will win, so they
 * are accounted separately here.) */
struct actcalc {
  int extra_turns[MAX_EXTRA_TYPES][ACTIVITY_LAST];
  int rmextra_turns[MAX_EXTRA_TYPES][ACTIVITY_LAST];
  int activity_turns[ACTIVITY_LAST];
};

/************************************************************************//**
  Calculate completion time for all unit activities on tile.
  If pmodunit is supplied, take into account the effect if it changed to
  doing new_act on new_tgt instead of whatever it's currently doing (if
  anything).
****************************************************************************/
static void calc_activity(struct actcalc *calc, const struct tile *ptile,
                          const struct unit *pmodunit,
                          Activity_type_id new_act,
                          const struct extra_type *new_tgt)
{
  /* This temporary working state is a bit big to allocate on the stack */
  struct {
    int extra_total[MAX_EXTRA_TYPES][ACTIVITY_LAST];
    int extra_units[MAX_EXTRA_TYPES][ACTIVITY_LAST];
    int rmextra_total[MAX_EXTRA_TYPES][ACTIVITY_LAST];
    int rmextra_units[MAX_EXTRA_TYPES][ACTIVITY_LAST];
    int activity_total[ACTIVITY_LAST];
    int activity_units[ACTIVITY_LAST];
  } *t;

  t = fc_calloc(1, sizeof(*t));

  /* Contributions from real units */
  unit_list_iterate(ptile->units, punit) {
    Activity_type_id act = punit->activity;

    if (punit == pmodunit) {
      /* We'll account for this one later */
      continue;
    }

    /* Client needs check for activity_target even when the
     * activity is a build activity, and SHOULD have target.
     * Server may still be sending more information about tile or
     * unit activity changes, and client data is not yet consistent. */
    if (is_build_activity(act)
        && punit->activity_target != NULL) {
      int eidx = extra_index(punit->activity_target);

      t->extra_total[eidx][act] += punit->activity_count;
      t->extra_total[eidx][act] += get_activity_rate_this_turn(punit);
      t->extra_units[eidx][act] += get_activity_rate(punit);
    } else if (is_clean_activity(act)
               && punit->activity_target != NULL) {
      int eidx = extra_index(punit->activity_target);

      t->rmextra_total[eidx][act] += punit->activity_count;
      t->rmextra_total[eidx][act] += get_activity_rate_this_turn(punit);
      t->rmextra_units[eidx][act] += get_activity_rate(punit);
    } else {
      t->activity_total[act] += punit->activity_count;
      t->activity_total[act] += get_activity_rate_this_turn(punit);
      t->activity_units[act] += get_activity_rate(punit);
    }
  } unit_list_iterate_end;

  /* Hypothetical contribution from pmodunit, if it changed to specified
   * activity/target */
  if (pmodunit) {
    if (is_build_activity(new_act)) {
      int eidx = extra_index(new_tgt);

      if (new_act == pmodunit->changed_from
          && new_tgt == pmodunit->changed_from_target) {
        t->extra_total[eidx][new_act] += pmodunit->changed_from_count;
      }
      t->extra_total[eidx][new_act] += get_activity_rate_this_turn(pmodunit);
      t->extra_units[eidx][new_act] += get_activity_rate(pmodunit);
    } else if (is_clean_activity(new_act)) {
      int eidx = extra_index(new_tgt);

      if (new_act == pmodunit->changed_from
          && new_tgt == pmodunit->changed_from_target) {
        t->rmextra_total[eidx][new_act] += pmodunit->changed_from_count;
      }
      t->rmextra_total[eidx][new_act] += get_activity_rate_this_turn(pmodunit);
      t->rmextra_units[eidx][new_act] += get_activity_rate(pmodunit);
    } else {
      if (new_act == pmodunit->changed_from) {
        t->activity_total[new_act] += pmodunit->changed_from_count;
      }
      t->activity_total[new_act] += get_activity_rate_this_turn(pmodunit);
      t->activity_units[new_act] += get_activity_rate(pmodunit);
    }
  }

  /* Turn activity counts into turn estimates */
  activity_type_iterate(act) {
    int remains, turns;

    if (is_build_activity(act)) {
      extra_type_iterate(ep) {
        int ei = extra_index(ep);
        int units_total = t->extra_units[ei][act];

        if (units_total > 0) {
          remains
            = tile_activity_time(act, ptile, ep)- t->extra_total[ei][act];
          if (remains > 0) {
            turns = 1 + (remains + units_total - 1) / units_total;
          } else {
            /* Extra will be finished this turn */
            turns = 1;
          }
        } else {
          turns = 0;
        }

        calc->extra_turns[ei][act] = turns;
      } extra_type_iterate_end;
    } else if (is_clean_activity(act)) {
      extra_type_iterate(ep) {
        int ei = extra_index(ep);
        int units_total = t->rmextra_units[ei][act];

        if (units_total > 0) {
          remains
            = tile_activity_time(act, ptile, ep) - t->rmextra_total[ei][act];
          if (remains > 0) {
            turns = 1 + (remains + units_total - 1) / units_total;
          } else {
            /* Extra will be removed this turn */
            turns = 1;
          }
        } else {
          turns = 0;
        }

        calc->rmextra_turns[ei][act] = turns;
      } extra_type_iterate_end;
    } else {
      int units_total = t->activity_units[act];

      if (units_total > 0) {
        remains = tile_activity_time(act, ptile, NULL) - t->activity_total[act];
        if (remains > 0) {
          turns = 1 + (remains + units_total - 1) / units_total;
        } else {
          /* Activity will be finished this turn */
          turns = 1;
        }
      } else {
        turns = 0;
      }

      calc->activity_turns[act] = turns;
    }
  } activity_type_iterate_end;

  free(t);
}

/************************************************************************//**
  How many turns until the activity 'act' on target 'tgt' at 'ptile' would
  be complete, taking into account existing units and possible contribution
  from 'pmodunit' if it were also to help with the activity ('pmodunit' may
  be NULL to just account for current activities).
****************************************************************************/
int turns_to_activity_done(const struct tile *ptile,
                           Activity_type_id act,
                           const struct extra_type *tgt,
                           const struct unit *pmodunit)
{
  struct actcalc *calc = malloc(sizeof(struct actcalc));
  int turns;

  /* Calculate time for _all_ tile activities */
  /* XXX: this is quite expensive */
  calc_activity(calc, ptile, pmodunit, act, tgt);

  /* ...and extract just the one we want. */
  if (is_build_activity(act)) {
    int tgti = extra_index(tgt);

    turns = calc->extra_turns[tgti][act];
  } else if (is_clean_activity(act)) {
    int tgti = extra_index(tgt);

    turns = calc->rmextra_turns[tgti][act];
  } else {
    turns = calc->activity_turns[act];
  }

  free(calc);

  return turns;
}

/************************************************************************//**
  Creates the activity progress text for the given tile.
****************************************************************************/
const char *concat_tile_activity_text(struct tile *ptile)
{
  struct actcalc *calc = fc_malloc(sizeof(struct actcalc));
  int num_activities = 0;
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);

  calc_activity(calc, ptile, NULL, ACTIVITY_LAST, NULL);

  activity_type_iterate(i) {
    if (is_build_activity(i)) {
      extra_type_iterate(ep) {
        int ei = extra_index(ep);

        if (calc->extra_turns[ei][i] > 0) {
          if (num_activities > 0) {
            astr_add(&str, "/");
          }
          astr_add(&str, "%s(%d)", extra_name_translation(ep),
                   calc->extra_turns[ei][i]);
          num_activities++;
        }
      } extra_type_iterate_end;
    } else if (is_clean_activity(i)) {
      enum extra_rmcause rmcause = ERM_NONE;

      switch (i) {
      case ACTIVITY_PILLAGE:
        rmcause = ERM_PILLAGE;
        break;
      case ACTIVITY_CLEAN:
        rmcause = ERM_CLEAN;
        break;
      default:
        fc_assert(rmcause != ERM_NONE);
        break;
      };

      if (rmcause != ERM_NONE) {
        extra_type_by_rmcause_iterate(rmcause, ep) {
          int ei = extra_index(ep);

          if (calc->rmextra_turns[ei][i] > 0) {
            if (num_activities > 0) {
              astr_add(&str, "/");
            }
            astr_add(&str,
                     rmcause == ERM_PILLAGE ? _("Pillage %s(%d)")
                                            : _("Clean %s(%d)"),
                     extra_name_translation(ep), calc->rmextra_turns[ei][i]);
            num_activities++;
          }
        } extra_type_by_rmcause_iterate_end;
      }
    } else if (is_tile_activity(i)) {
      if (calc->activity_turns[i] > 0) {
        if (num_activities > 0) {
          astr_add(&str, "/");
        }
        astr_add(&str, "%s(%d)",
                 get_activity_text(i), calc->activity_turns[i]);
        num_activities++;
      }
    }
  } activity_type_iterate_end;

  free(calc);

  return astr_str(&str);
}

/************************************************************************//**
  Add lines about the combat odds to the str.
****************************************************************************/
void combat_odds_to_astr(struct astring *str, struct unit_list *punits,
                         const struct tile *ptile, const struct unit *punit,
                         const char *pct_str)
{
  const struct unit_type *ptype = unit_type_get(punit);
  struct civ_map *nmap = &(wld.map);

  unit_list_iterate(punits, pfocus) {
    int att_chance = FC_INFINITY, def_chance = FC_INFINITY;
    bool found = FALSE;

    unit_list_iterate(ptile->units, tile_unit) {
      if (unit_owner(tile_unit) != unit_owner(pfocus)) {
        int att = unit_win_chance(nmap, pfocus, tile_unit, NULL) * 100;
        int def = (1.0 - unit_win_chance(nmap, tile_unit, pfocus,
                                         NULL)) * 100;

        found = TRUE;

        /* Presumably the best attacker and defender will be used. */
        att_chance = MIN(att, att_chance);
        def_chance = MIN(def, def_chance);
      }
    } unit_list_iterate_end;

    if (found) {
      /* TRANS: "Chance to win: A:95% D:46%" - "%s" are just the percent signs. */
      astr_add_line(str, _("Chance to win: A:%d%s D:%d%s"),
                    att_chance, pct_str, def_chance, pct_str);
    }
  } unit_list_iterate_end;

  /* TRANS: A is attack power, D is defense power, FP is firepower,
   * HP is hitpoints (current and max). */
  astr_add_line(str, _("A:%d D:%d FP:%d HP:%d/%d"),
                ptype->attack_strength, ptype->defense_strength,
                ptype->firepower, punit->hp, ptype->hp);

  {
    const char *veteran_name =
      utype_veteran_name_translation(ptype, punit->veteran);

    if (veteran_name != NULL) {
      astr_add(str, " (%s)", veteran_name);
    }
  }
}
