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

#include <stdio.h>
#include <stdlib.h>

#include "events.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "player.h"
#include "timing.h"
#include "unit.h"

#include "gotohand.h"
#include "plrhand.h"
#include "srv_main.h"
#include "unitfunc.h"
#include "unithand.h"
#include "unittools.h"

#include "autoattack.h"


/**************************************************************************
...
**************************************************************************/
static struct unit *search_best_target(struct player *pplayer,
				       struct city *pcity, struct unit *punit)
{
  struct unit_list *targets;
  struct unit *enemy, *best_enemy = NULL;
  int score, best_score = 0;
  int mv_cost, range;
  int x,y,i,j;
  int debug=0;

  /* if a 'F_ONEATTACK' unit,
     range is larger, because they are not going to return this turn anyways
  */

  range = unit_flag(punit->type,F_ONEATTACK) ?
      punit->moves_left :
      punit->moves_left/2;

  /* attack the next to city */ 
  range = range<4 ? 3 : range;
  
  if (debug) {
    freelog(LOG_DEBUG, "doing autoattack for %s (%d/%d) in %s,"
	 " range %d(%d), city_options %d",
	 unit_name(punit->type), punit->x, punit->y, pcity->name,
	 range, punit->moves_left, pcity->city_options);
  }
  
  for(i=-range;i<=range;i++) {
    x = map_adjust_x(punit->x+i);
    for(j=-range;j<=range;j++) {
      y = punit->y + j;		/* no adjust; map_get_tile will use void_tile */

      if (i==0 && j==0) continue;
      /* we wont wanna do it for the city itself - fisch */
      
      if (map_get_city(x, y)) continue;
      /* don't attack enemy cities (or our own ;-) --dwp */
      
      targets = &(map_get_tile(x, y)->units);
      if (unit_list_size(targets) == 0) continue;
      if (!is_enemy_unit_tile(map_get_tile(x, y), pplayer->player_no))
	continue;
      
      if (debug) freelog(LOG_DEBUG,  "found enemy unit/stack at %d,%d", x, y);
      enemy = get_defender(pplayer, punit, x, y);
      if (debug) freelog(LOG_DEBUG,  "defender is %s", unit_name(enemy->type));
      
      if (!(pcity->city_options
	    & 1<<(get_unit_type(enemy->type)->move_type-1+CITYO_ATT_LAND))) {
	if (debug) freelog(LOG_DEBUG, "wrong target type");
	continue;
      }
   
      mv_cost = calculate_move_cost(punit, x, y);
      if (mv_cost > range) {
        if(debug) freelog(LOG_DEBUG, "too far away: %d", mv_cost);
        continue;
      }
      
      /*
       *  perhaps there is a better algorithm in the ai-package -- fisch
       */
      score = (get_unit_type(enemy->type)->defense_strength
	       + (enemy->hp/2)
	       + (get_transporter_capacity(enemy) ? 1 : 0));
      
      if(best_enemy == NULL || score >= best_score) {
	best_score = score;
	best_enemy = enemy;
      }
    }
  }
  
  if(best_enemy == NULL) return NULL;

  enemy = best_enemy;
  
  if(debug) {
    freelog(LOG_DEBUG,"choosen target=%s (%d/%d)",
	 get_unit_name(enemy->type), enemy->x,enemy->y);
  }

  if((get_unit_type(enemy->type)->defense_strength) >
     get_unit_type(punit->type)->attack_strength*1.5) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     "Game: Auto-Attack: %s's %s found a too tough enemy (%s)",
		     pcity->name, unit_name(punit->type),
		     unit_name(enemy->type));
    punit->ai.control=0;
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
  int debug = 0;

  enemy=search_best_target(pplayer,pcity,punit);

  /* nothing found */
  if(enemy == NULL) return;
  
  if (debug) freelog(LOG_DEBUG, "launching attack");
  
  notify_player_ex(pplayer, enemy->x,enemy->y, E_NOEVENT,
                   "Game: Auto-Attack: %s's %s attacking %s's %s",
                   pcity->name, unit_name(punit->type),
                   get_player(enemy->owner)->name,
                   unit_name(enemy->type));
  
  set_unit_activity(punit, ACTIVITY_GOTO);
  punit->goto_dest_x=enemy->x;
  punit->goto_dest_y=enemy->y;
  
  send_unit_info(0,punit);
  do_unit_goto(punit, GOTO_MOVE_ANY);
  
  punit = find_unit_by_id(id);
  
  if (punit) {
    set_unit_activity(punit, ACTIVITY_GOTO);
    punit->goto_dest_x=pcity->x;
    punit->goto_dest_y=pcity->y;
    send_unit_info(0,punit);
    
    do_unit_goto(punit, GOTO_MOVE_ANY);
    
    if (unit_list_find(&map_get_tile(pcity->x, pcity->y)->units, id)) {
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
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
    if (punit->ai.control
	&& punit->activity == ACTIVITY_IDLE
	&& is_military_unit(punit)) {
      auto_attack_with_unit(pplayer, pcity, punit);
    }
  }
  unit_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void auto_attack_player(struct player *pplayer)
{
  freelog(LOG_DEBUG, "doing auto_attack for: %s",pplayer->name);

  city_list_iterate(pplayer->cities, pcity) {
    /* fasten things up -- fisch */
    if ((pcity->city_options & CITYOPT_AUTOATTACK_BITS)
	&& !pcity->anarchy) {
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
       && punit->moves_left == get_unit_type(punit->type)->move_rate) {
      do_unit_goto(punit, GOTO_MOVE_ANY);
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
  int i;

  t = renew_timer_start(t, TIMER_CPU, TIMER_DEBUG);

  /* re-use shuffle order from civserver.c */
  for (i = 0; i < game.nplayers; i++) {
    auto_attack_player(shuffled_player(i));
  }
  if (timer_in_use(t)) {
    freelog(LOG_VERBOSE, "autoattack consumed %g milliseconds.",
	    1000.0*read_timer_seconds(t));
  }
}
