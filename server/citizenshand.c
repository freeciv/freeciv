/*****************************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "log.h"
#include "rand.h"

/* common */
#include "citizens.h"
#include "city.h"
#include "fc_types.h"
#include "game.h"
#include "player.h"

/* server */
#include "cityturn.h"
#include "sanitycheck.h"


#include "citizenshand.h"

#define LOG_CITIZENS_DEBUG

#ifdef LOG_CITIZENS_DEBUG
#define log_citizens log_debug
#ifdef FREECIV_DEBUG
#define LOG_CITIZENS
#endif /* FREECIV_DEBUG      */
#else  /* LOG_CITIZENS_DEBUG */
#define log_citizens log_verbose
#define LOG_CITIZENS
#endif /* LOG_CITIZENS_DEBUG */

#if !defined(FREECIV_NDEBUG) || defined(LOG_CITIZENS)
#define LOG_OR_ASSERT_CITIZENS
#endif

/*************************************************************************//**
  Get the nationality of a unit built by the city and optionally the numbers
  of citizens to go into the unit from different nationalities.
  pcity must be a real city.
  The algorithm is the same as in citizens_update().
*****************************************************************************/
struct player
*citizens_unit_nationality(const struct city *pcity,
                           unsigned pop_cost,
                           struct citizens_reduction *pchange)
{
  struct {
    struct player_slot *pslot;
    citizens change, remain;
    short idx;
  } city_nations[MAX_CITY_NATIONALITIES + 1];
  struct citizens_reduction *pchange_start = pchange;
  int count = 0; /* Number of foreign nationalities in the city */
  int max_nat = 0; /* Maximal number of a nationality gone into the unit */
  struct player_slot *dominant = NULL;

  fc_assert(pop_cost > 0);

  if (!game.info.citizen_nationality) {
    /* Nothing to do */
    return pcity->owner;
  }

  /* Create a list of foreign nationalities. */
  citizens_foreign_iterate(pcity, pslot, nationality) {
    city_nations[count].pslot = pslot; /* Nationality */
    city_nations[count].change = 0; /* How many go into the unit */
    city_nations[count].remain = nationality; /* How many remain yet */
    city_nations[count].idx = -1; /* index in pchange_start (not yet set) */
    count++;
  } citizens_foreign_iterate_end;

  while (count > 0 && pop_cost > 0) {
    int selected = fc_rand(count);
    struct player_slot *pslot = city_nations[selected].pslot;
    citizens nationality = city_nations[selected].remain;
    citizens change = city_nations[selected].change;
    int idx = city_nations[selected].idx;
    int diff; /* Number of citizens sent out on this iteration */

#ifndef FREECIV_NDEBUG
    struct player *pplayer = player_slot_get_player(pslot);
#endif

    fc_assert_ret_val(nationality != 0, NULL);
    fc_assert_ret_val(pplayer != NULL, NULL);

    if (nationality == 1) {
      diff = 1;
      change++;
      /* Remove this nation from the list of nationalities. */
      if (selected != --count) {
        city_nations[selected] = city_nations[count];
      }
    } else {
      diff = MIN(pop_cost, nationality / 2);
      change += diff;
      city_nations[selected].remain -= diff;
      city_nations[selected].change = change;
    }
    if (pchange) {
      if (idx < 0) {
        /* New nationality of the band */
        pchange->pslot = pslot;
        pchange->change = diff;
        if (nationality > 1) {
          /* Remember where to update if we take from this again */
          city_nations[selected].idx = pchange - pchange_start;
        }
        pchange++;
      } else {
        /* Update change */
        pchange_start[idx].change = change;
      }
    }
    if (change > max_nat) {
      max_nat = change;
      dominant = pslot;
    }
    pop_cost -= diff;
  }
  if (pop_cost > 0) {
    struct player_slot *own_slot = pcity->owner->slot;

    /* Take own citizens */
    fc_assert(citizens_nation_get(pcity, own_slot) >= pop_cost);
    /* Even if the assertion fails, citizens_reduction_apply()
     * won't take more than it finds */
    if (pchange) {
      pchange->pslot = own_slot;
      pchange->change = pop_cost;
      pchange++;
    }
    if (pop_cost > max_nat) {
      dominant = own_slot;
    }
  }
  /* Delimiter */
  if (pchange) {
    pchange->change = 0;
  }

  fc_assert(dominant != NULL);
  if (dominant == NULL) {
    return pcity->owner;
  }

  return player_slot_get_player(dominant);
}

#define log_citizens_add(_pcity, _delta, _pplayer)                           \
  log_citizens("%s (size %d; %s): %+d citizen(s) for %s (now: %d)",          \
               city_name_get(_pcity), city_size_get(_pcity),                 \
               player_name(city_owner(_pcity)), _delta,                      \
               player_name(_pplayer),                                        \
               citizens_nation_get(_pcity, _pplayer->slot));
/*************************************************************************//**
  Applies citizens reduction that may be planned previously
  in citizens_unit_nationality(). Does not check or change city size.
  If the nationality has less citizens that are to be removed,
  just removes as many as possible.
*****************************************************************************/
void citizens_reduction_apply(struct city *pcity,
                              const struct citizens_reduction *pchange)
{
#ifdef LOG_CITIZENS
  struct player *pplayer = city_owner(pcity);
#endif

  fc_assert_ret(pcity);
  fc_assert_ret(pchange);

  if (!pcity->nationality) {
    return;
  }

  while (pchange->change) {
    int pres = citizens_nation_get(pcity, pchange->pslot);
    int diff = -MIN(pres, pchange->change);

    citizens_nation_add(pcity, pchange->pslot, diff);
    log_citizens_add(pcity, diff, pplayer);
    pchange++;
  }
}

/*************************************************************************//**
  Update the nationality according to the city size. New citiens are added
  using the nationality of the owner. If the city size is reduced, the
  citizens are removed first from the foreign citizens.
*****************************************************************************/
void citizens_update(struct city *pcity, struct player *plr)
{
  int delta;

  fc_assert_ret(pcity);

  if (pcity->server.debug) {
    /* before */
    citizens_print(pcity);
  }

  if (!game.info.citizen_nationality) {
    return;
  }

  if (pcity->nationality == NULL) {
    /* If nationalities are not set (virtual cities) do nothing. */
    return;
  }

  delta = city_size_get(pcity) - citizens_count(pcity);

  if (delta == 0) {
    /* No change of the city size */
    return;
  }

  if (delta > 0) {
    if (plr != NULL) {
      citizens_nation_add(pcity, plr->slot, delta);
      log_citizens_add(pcity, delta, plr);
    } else {
      /* Add new citizens with the nationality of the current owner. */
      citizens_nation_add(pcity, city_owner(pcity)->slot, delta);
      log_citizens_add(pcity, delta, city_owner(pcity));
    }
  } else {
    /* Removed citizens. */
    struct player_slot *city_nations[MAX_NUM_PLAYER_SLOTS];
    int count = 0;

    /* Create a list of foreign nationalities. */
    citizens_foreign_iterate(pcity, pslot, nationality) {
      city_nations[count] = pslot;
      count++;
    } citizens_foreign_iterate_end;

    /* First remove from foreign nationalities. */
    while (count > 0 && delta < 0) {
      int selected = fc_rand(count);
      struct player_slot *pslot = city_nations[selected];
      citizens nationality = citizens_nation_get(pcity, pslot);
#ifdef LOG_OR_ASSERT_CITIZENS
      struct player *pplayer = player_slot_get_player(pslot);
#endif /* LOG_OR_ASSERT_CITIZENS */

      fc_assert_ret(nationality != 0);
      fc_assert_ret(pplayer != NULL);

      if (nationality == 1) {
        /* Remove one citizen. */
        delta++;
        citizens_nation_set(pcity, pslot, 0);
        /* Remove this nation from the list of nationalities. */
        city_nations[selected] = city_nations[--count];

        log_citizens_add(pcity, -1, pplayer);
      } else {
        /* Get the minimal reduction = the maximum value of two negative
         * numbers. */
        int diff = MAX(delta, - nationality / 2);

        delta -= diff;
        citizens_nation_add(pcity, pslot, diff);
        log_citizens_add(pcity, diff, pplayer);
      }
    }

    if (delta < 0) {
      /* Now take the remaining citizens loss from the nation of the owner. */
      citizens_nation_add(pcity, city_owner(pcity)->slot, delta);
      log_citizens_add(pcity, delta, city_owner(pcity));
    }
  }

  fc_assert_ret(city_size_get(pcity) == citizens_count(pcity));

  if (pcity->server.debug) {
    /* after */
    citizens_print(pcity);
  }
}
#undef log_citizens_add

/*************************************************************************//**
  Print the data about the citizens.
*****************************************************************************/
void citizens_print(const struct city *pcity)
{
  fc_assert_ret(pcity);

  if (!game.info.citizen_nationality) {
    return;
  }

  log_citizens("%s (size %d; %s): %d citizen(s)",
               city_name_get(pcity), city_size_get(pcity),
               player_name(city_owner(pcity)), citizens_count(pcity));

#ifdef LOG_OR_ASSERT_CITIZENS
  citizens_iterate(pcity, pslot, nationality) {
    struct player *pplayer = player_slot_get_player(pslot);

    fc_assert_ret(pplayer != NULL);

    log_citizens("%s (size %d; %s): %d citizen(s) for %s",
                 city_name_get(pcity), city_size_get(pcity),
                 player_name(city_owner(pcity)), nationality,
                 player_name(pplayer));
  } citizens_iterate_end;
#endif /* LOG_OR_ASSERT_CITIZENS */
}

/*************************************************************************//**
  Return whether citizen should be converted this turn.
*****************************************************************************/
static bool citizen_convert_check(struct city *pcity)
{
  if (fc_rand(1000) + 1 > game.info.citizen_convert_speed) {
    return FALSE;
  }

  return TRUE;
}

/*************************************************************************//**
  Convert one (random) foreign citizen to the nationality of the owner.
*****************************************************************************/
void citizens_convert(struct city *pcity)
{
  struct player_slot *city_nations[MAX_CITY_NATIONALITIES + 1], *pslot;
  int count = 0;

  fc_assert_ret(pcity);

  if (!game.info.citizen_nationality) {
    return;
  }

  if (!citizen_convert_check(pcity)) {
    return;
  }

  if (citizens_nation_foreign(pcity) == 0) {
    /* Only our own citizens. */
    return;
  }

  /* Create a list of foreign nationalities. */
  citizens_foreign_iterate(pcity, foreign_slot, nationality) {
    if (nationality != 0) {
      city_nations[count++] = foreign_slot;
    }
  } citizens_foreign_iterate_end;

  /* Now convert one citizens to the city owners nationality. */
  pslot = city_nations[fc_rand(count)];

#ifdef LOG_OR_ASSERT_CITIZENS
  struct player *pplayer = player_slot_get_player(pslot);
#endif

  fc_assert_ret(pplayer != NULL);

  log_citizens("%s (size %d; %s): convert 1 citizen from %s",
               city_name_get(pcity), city_size_get(pcity),
               player_name(city_owner(pcity)), player_name(pplayer));

  citizens_nation_move(pcity, pslot, city_owner(pcity)->slot, 1);
}

/*************************************************************************//**
  Convert citizens to the nationality of the one conquering the city.
*****************************************************************************/
void citizens_convert_conquest(struct city *pcity)
{
  struct player_slot *conqueror;

  if (!game.info.citizen_nationality || game.info.conquest_convert_pct == 0) {
    return;
  }

  conqueror = city_owner(pcity)->slot;

  citizens_foreign_iterate(pcity, pslot, nat) {
    /* Convert 'game.info.conquest_convert_pct' citizens of each foreign
     * nationality to the nation of the new owner (but at least 1). */
    citizens convert = MAX(1, nat * game.info.conquest_convert_pct
                           / 100);

#ifdef LOG_OR_ASSERT_CITIZENS
    struct player *pplayer = player_slot_get_player(pslot);
#endif

    fc_assert_ret(pplayer != NULL);

    log_citizens("%s (size %d; %s): convert %d citizen from %s (conquered)",
                 city_name_get(pcity), city_size_get(pcity),
                 player_name(city_owner(pcity)), convert,
                 player_name(pplayer));
    citizens_nation_move(pcity, pslot, conqueror, convert);
  } citizens_foreign_iterate_end;
}
