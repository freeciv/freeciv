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
#include "extras.h"
#include "fc_types.h"
#include "game.h"  /* FIXME it's extra_type_iterate that needs this really */
#include "tile.h"

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
****************************************************************************/
static void calc_activity(struct actcalc *calc, const struct tile *ptile)
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

  memset(calc, 0, sizeof(*calc));

  unit_list_iterate(ptile->units, punit) {
    Activity_type_id act = punit->activity;

    if (is_build_activity(act, ptile)) {
      int eidx = extra_index(punit->activity_target);

      t->extra_total[eidx][act] += punit->activity_count;
      t->extra_total[eidx][act] += get_activity_rate_this_turn(punit);
      t->extra_units[eidx][act] += get_activity_rate(punit);
    } else if (is_clean_activity(act)) {
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

  /* Turn activity counts into turn estimates */
  activity_type_iterate(act) {
    int remains, turns;

    extra_type_iterate(ep) {
      int ei = extra_index(ep);

      {
        int units_total = t->extra_units[ei][act];

        if (units_total > 0) {
          remains
              = tile_activity_time(act, ptile, ep)- t->extra_total[ei][act];
          if (remains > 0) {
            turns = 1 + (remains + units_total - 1) / units_total;
          } else {
            /* extra will be finished this turn */
            turns = 1;
          }
          calc->extra_turns[ei][act] = turns;
        }
      }
      {
        int units_total = t->rmextra_units[ei][act];

        if (units_total > 0) {
          remains
              = tile_activity_time(act, ptile, ep) - t->rmextra_total[ei][act];
          if (remains > 0) {
            turns = 1 + (remains + units_total - 1) / units_total;
          } else {
            /* extra will be removed this turn */
            turns = 1;
          }
          calc->rmextra_turns[ei][act] = turns;
        }
      }
    } extra_type_iterate_end;

    int units_total = t->activity_units[act];

    if (units_total > 0) {
      remains = tile_activity_time(act, ptile, NULL) - t->activity_total[act];
      if (remains > 0) {
        turns = 1 + (remains + units_total - 1) / units_total;
      } else {
        /* activity will be finished this turn */
        turns = 1;
      }
      calc->activity_turns[act] = turns;
    }
  } activity_type_iterate_end;
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

  calc_activity(calc, ptile);

  activity_type_iterate(i) {
    if (is_build_activity(i, ptile)) {
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
      case ACTIVITY_POLLUTION:
        rmcause = ERM_CLEANPOLLUTION;
        break;
      case ACTIVITY_FALLOUT:
        rmcause = ERM_CLEANFALLOUT;
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

  FC_FREE(calc);

  return astr_str(&str);
}
