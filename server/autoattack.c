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

/* autoattack by:
   Sebastian Fischmeister <sfischme@nexus.lzk.tuwien.ac.at>
   and David Pfitzner <dwp@mso.anu.edu.au>

   ToDo:

   - if have movement and enough hp remaining, should consider
     attacking multiple times if multiple targets (hard, because you
     have to calculate warmap for each choice -> lots of cpu !!!)
     
   - create gui-client for diplomacy for players
     (in work by other persons)

 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "combat.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "player.h"
#include "timing.h"
#include "unit.h"

#include "gotohand.h"
#include "maphand.h"
#include "plrhand.h"
#include "srv_main.h"
#include "unithand.h"
#include "unittools.h"

#include "autoattack.h"


/**************************************************************************
FIXME: Calculate the attractiveness of attacking.
**************************************************************************/
static struct unit *search_best_target(struct player *pplayer,
				       struct city *pcity, struct unit *punit)
{
  struct unit_list *targets;
  struct unit *enemy, *best_enemy = NULL;
  int score, best_score = 0;
  int mv_cost, range;

  /* if a 'F_ONEATTACK' unit,
     range is larger, because they are not going to return this turn anyways
  */

  range = unit_flag(punit, F_ONEATTACK) ?
      punit->moves_left :
      punit->moves_left/2;

  /* attack the next to city */ 
  range = range<4 ? 3 : range;
  
  freelog(LOG_DEBUG, "doing autoattack for %s (%d/%d) in %s,"
	  " range %d(%d), city_options %d",
	  unit_name(punit->type), punit->tile->x, punit->tile->y,
	  pcity->name,
	  range, punit->moves_left, pcity->city_options);
  
  square_iterate(punit->tile, range, ptile) {
    if (same_pos(punit->tile, ptile))
      continue;

    if (map_get_city(ptile)) continue;
    /* don't attack enemy cities (or our own ;-) --dwp */

    targets = &(ptile->units);
    if (unit_list_size(targets) == 0) continue;
    if (!is_enemy_unit_tile(ptile, pplayer))
      continue;

    freelog(LOG_DEBUG,  "found enemy unit/stack at %d,%d",
	    ptile->x, ptile->y);
    enemy = get_defender(punit, ptile);
    if (!enemy) {
      continue;
    }
    freelog(LOG_DEBUG,  "defender is %s", unit_name(enemy->type));

    if (!is_city_option_set(pcity, CITYO_ATT_LAND
			    + unit_type(enemy)->move_type - 1)) {
      freelog(LOG_DEBUG, "wrong target type");
      continue;
    }

    mv_cost = calculate_move_cost(punit, ptile);
    if (mv_cost > range) {
      freelog(LOG_DEBUG, "too far away: %d", mv_cost);
      continue;
    }

    /*
     * Make sure the player can see both the location and the unit at that
     * location, before allowing an auto-attack.  Without this, units attack
     * into locations they cannot see, and maybe submarines are made
     * erroneously visible too.
     *
     * Note, cheating AI may attack units it cannot see unless it has
     * H_TARGETS handicap, but currently AI never uses auto-attack.
     */
    if (ai_handicap(pplayer, H_TARGETS)
	&& !can_player_see_unit(pplayer, enemy)) {
      freelog(LOG_DEBUG, "can't see %s at (%d,%d)", unit_name(enemy->type),
              ptile->x, ptile->y);
      continue;
    }

    /*
     * Without this check, missiles are made useless for auto-attack as they
     * get triggered by fighters and bombers and end up being wasted when they
     * cannot engage.
     */
    if (!can_unit_attack_all_at_tile(punit, ptile)) {
      freelog(LOG_DEBUG, "%s at (%d,%d) cannot attack at (%d,%d)",
	      unit_name(punit->type), punit->tile->x, punit->tile->y,
	      ptile->x, ptile->y);
      continue;
    }

    /*
     *  perhaps there is a better algorithm in the ai-package -- fisch
     */
    score = (unit_type(enemy)->defense_strength + (enemy->hp / 2)
	     + (get_transporter_capacity(enemy) > 0 ? 1 : 0));

    if(!best_enemy || score >= best_score) {
      best_score = score;
      best_enemy = enemy;
    }
  } square_iterate_end;

  if(!best_enemy) return NULL;

  enemy = best_enemy;
  
  freelog(LOG_DEBUG, "chosen target=%s (%d/%d)",
	  get_unit_name(enemy->type), enemy->tile->x, enemy->tile->y);

  if((unit_type(enemy)->defense_strength) >
     unit_type(punit)->attack_strength*1.5) {
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: Auto-Attack: %s's %s found a too "
		       "tough enemy (%s)"),
		     pcity->name, unit_name(punit->type),
		     unit_name(enemy->type));
    punit->ai.control=FALSE;
    handle_unit_activity_request(punit, ACTIVITY_IDLE);
    return NULL;
  }

  return enemy;
}

/**************************************************************************
...
**************************************************************************/
static void auto_attack_with_unit(struct player *pplayer, struct city *pcity,
				  struct unit *punit)
{
  int id = punit->id;
  struct unit *enemy;

  enemy=search_best_target(pplayer,pcity,punit);

  /* nothing found */
  if(!enemy) return;
  
  freelog(LOG_DEBUG, "launching attack");
  
  notify_player_ex(pplayer, enemy->tile, E_NOEVENT,
		   _("Game: Auto-Attack: %s's %s attacking %s's %s"),
		   pcity->name, unit_name(punit->type),
		   unit_owner(enemy)->name, unit_name(enemy->type));
  
  set_unit_activity(punit, ACTIVITY_GOTO);
  punit->goto_tile = enemy->tile;
  
  send_unit_info(NULL, punit);
  (void) do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
  
  punit = find_unit_by_id(id);
  
  if (punit) {
    set_unit_activity(punit, ACTIVITY_GOTO);
    punit->goto_tile = pcity->tile;
    send_unit_info(NULL, punit);
    
    (void) do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
    
    if (unit_list_find(&pcity->tile->units, id)) {
      handle_unit_activity_request(punit, ACTIVITY_IDLE);
    }
  }
  
  return;			/* or maybe: do you want to play again? */
}

/**************************************************************************
...
**************************************************************************/
static void auto_attack_city(struct player *pplayer, struct city *pcity)
{
  unit_list_iterate_safe(pcity->tile->units, punit) {
    if (punit->ai.control
	&& punit->activity == ACTIVITY_IDLE
	&& is_military_unit(punit)) {
      auto_attack_with_unit(pplayer, pcity, punit);
    }
  } unit_list_iterate_safe_end;
}

/**************************************************************************
...
**************************************************************************/
static void auto_attack_player(struct player *pplayer)
{
  freelog(LOG_DEBUG, "doing auto_attack for: %s",pplayer->name);

  city_list_iterate(pplayer->cities, pcity) {
    /* fasten things up -- fisch */
    if ((pcity->city_options & CITYOPT_AUTOATTACK_BITS) != 0
	&& pcity->anarchy == 0) {
      auto_attack_city(pplayer, pcity);
    }
  }
  city_list_iterate_end;

  /* Now handle one-attack units which attacked the previous turn.
     These go home this turn, and don't auto-attack again until
     the next turn.  This could be done better, but this is
     a start and should stop bombers from running out of fuel --dwp.
     (Normally units with ai.control do not have goto done automatically.)
  */
  unit_list_iterate(pplayer->units, punit) {
    if(punit->ai.control
       && is_military_unit(punit)
       && punit->activity == ACTIVITY_GOTO
       && punit->moves_left == unit_move_rate(punit)) {
      (void) do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
    }
  }
  unit_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void auto_attack(void)
{
  static struct timer *t = NULL;      /* alloc once, never free */

  t = renew_timer_start(t, TIMER_CPU, TIMER_DEBUG);

  /* re-use shuffle order from civserver.c */
  shuffled_players_iterate(pplayer) {
    auto_attack_player(pplayer);
  } shuffled_players_iterate_end;
  if (timer_in_use(t)) {
    freelog(LOG_VERBOSE, "autoattack consumed %g milliseconds.",
	    1000.0*read_timer_seconds(t));
  }
}
