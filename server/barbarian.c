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
  Functions for creating barbarians in huts, land and sea
  Started by Jerzy Klek <jekl@altavista.net>
  with more ideas from Falk Hueffner 
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "nation.h"
#include "rand.h"
#include "tech.h"

#include "civserver.h"
#include "gamehand.h"
#include "maphand.h"
#include "plrhand.h"
#include "stdinhand.h"
#include "unitfunc.h"
#include "unithand.h"
#include "unittools.h"

#include "aitools.h"

#include "barbarian.h"

/*
 IDEAS:
 1. Unrest factors configurable via rulesets (distance and gov factor)
 2. Separate nations for Sea Raiders and Land Barbarians

 - are these good ideas ??????
*/

/**************************************************************************
Is player a sea or land barbarian
**************************************************************************/

int is_land_barbarian(struct player *pplayer)
{
  return (pplayer->ai.is_barbarian == LAND_BARBARIAN);
}

int is_sea_barbarian(struct player *pplayer)
{
  return (pplayer->ai.is_barbarian == SEA_BARBARIAN);
}

/**************************************************************************
Random neighbouring cell - it maybe a stupid idea. Can anyone say how to
move barbarians out of the hut?
**************************************************************************/

static void rand_neighbour( int x0, int y0, int *x, int *y)
{
  int choice;
  int xoff[] = { -1,  0,  1, -1,  1, -1,  0,  1 };
  int yoff[] = { -1, -1, -1,  0,  0,  1,  1,  1 };

  if (y0 == 0) {
    choice = 3 + myrand(5);
  } else if(y0 == map.ysize-1){
    choice = myrand(5);
  } else {
    choice = myrand(8);
  }
  *x = x0 + xoff[choice];
  *y = y0 + yoff[choice];
  
#if 0
  /* shouldn't need this now: */
  *y = map_adjust_y(*y);
  if( *x == x0 && *y == y0 ) /* might happen after y adjustments */
    *x += 1;                 /* stupid but safe, rare anyway     */
#endif
  
  *x = map_adjust_x(*x);
}

/**************************************************************************
Creates the land/sea barbarian player and inits some stuff.
If barbarian player already exists, return player pointer.
If barbarians are dead, revive them with a new leader :-)
Dead barbarians forget the map and lose the money.
**************************************************************************/

static struct player *create_barbarian_player(int land)
{
  int newplayer = game.nplayers;
  struct player *barbarians;
  int i, j, k;

  for( i = 0; i < game.nplayers; i++ ) {
    barbarians = &game.players[i];
    if( (land && is_land_barbarian(barbarians)) ||
        (!land && is_sea_barbarian(barbarians)) ) {
      if( barbarians->is_alive == 0 ) {
        barbarians->economic.gold = 0;
        barbarians->is_alive = 1;
        pick_ai_player_name( game.nation_count-1, barbarians->name);
        /* I need to make them to forget the map, I think */
        for( j = 0; j < map.xsize; j++ )
          for( k = 0; k < map.ysize; k++ )
            map_clear_known( j, k, barbarians);
      }
      barbarians->economic.gold += 100;  /* New leader, new money */
      return barbarians;
    }
  }

  if( newplayer >= MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS ) {
    freelog( LOG_FATAL, "Too many players?");
    exit(1);
  }

  barbarians = &game.players[newplayer];

  /* make a new player */

  server_player_init(barbarians, 1);

  barbarians->nation = game.nation_count-1;
  pick_ai_player_name( game.nation_count-1, barbarians->name);

  barbarians->is_connected = 0;
  barbarians->conn = NULL;
  barbarians->government = game.government_when_anarchy; 
  barbarians->capital = 0;
  barbarians->economic.gold = 100;

  barbarians->turn_done = 1;

  /* Do the ai */

  barbarians->ai.control = 1;
  if( land )
    barbarians->ai.is_barbarian = LAND_BARBARIAN;
  else
    barbarians->ai.is_barbarian = SEA_BARBARIAN;
  set_ai_level_directer(barbarians, game.skill_level);
  init_tech(barbarians, game.tech);

  game.nplayers++;
  game.max_players = game.nplayers;

  freelog(LOG_VERBOSE, "Created barbarian %s, player %d", barbarians->name, 
                       barbarians->player_no);

  send_game_info(0);
  send_player_info(barbarians,0);

  return barbarians;
}

/**************************************************************************
Check if a tile is land and free of enemy units
**************************************************************************/
static int is_free_land(int x, int y, int who)
{
  if( y < 0 || y >= map.ysize ||
      map_get_terrain(x,y) == T_OCEAN ||
      is_enemy_unit_on_tile(x, y, who) )
    return 0;
  else
    return 1;
}

/**************************************************************************
Check if a tile is sea and free of enemy units
**************************************************************************/
static int is_free_sea(int x, int y, int who)
{
  if( y < 0 || y >= map.ysize ||
      map_get_terrain(x,y) != T_OCEAN ||
      is_enemy_unit_on_tile(x, y, who) )
    return 0;
  else
    return 1;
}

/**************************************************************************
Unleash barbarians means give barbarian player some units and move them out
of the hut, unless there's no place to go.
Barbarian unit deployment algorithm: if enough free land around, deploy
on land, if not enough land but some sea free, load some of them on boats,
otherwise (not much land and no sea) kill enemy unit and stay in a village.
The return value indicates if the explorer survived entering the vilage.
**************************************************************************/

int unleash_barbarians(struct player* victim, int x, int y)
{
  struct player *barbarians;
  int unit, unit_cnt, land_cnt=0, sea_cnt=0;
  int boat;
  int i, xu, yu, me;
  int xx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int yy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  int alive = 1;     /* explorer survived */

  if(!game.barbarianrate || (game.year < game.onsetbarbarian)) {
    unit_list_iterate(map_get_tile(x, y)->units, punit) {
      wipe_unit_safe(0, punit, &myiter);
    } unit_list_iterate_end;
    return 0;
  }

  unit_cnt = 3 + myrand(4);

  barbarians = create_barbarian_player(1);
  me = barbarians->player_no;

  for( i=0; i<unit_cnt; i++) {
    unit = find_a_unit_type(L_BARBARIAN, L_BARBARIAN_TECH);
    create_unit( barbarians, x, y, unit, 0, 0, -1);
    freelog(LOG_DEBUG, "Created barbarian unit %s",unit_types[unit].name);
  }

  for( i = 0; i < 8; i++ ) {
    land_cnt += is_free_land(x+xx[i], y+yy[i], me);
    sea_cnt += is_free_sea(x+xx[i], y+yy[i], me);
  }

  if( land_cnt >= 3 ) {           /* enough land, scatter guys around */
    unit_list_iterate(map_get_tile(x, y)->units, punit2) {
      if( punit2->owner == me ) {
        send_unit_info( 0, punit2);
        do { 
          do rand_neighbour(x, y, &xu, &yu); 
          while( !is_free_land(xu, yu, me) );
        } while( !handle_unit_move_request(barbarians, punit2, xu, yu, TRUE) );
        freelog(LOG_DEBUG, "Moved barbarian unit from %d %d to %d, %d", x,y,xu,yu);
      }
    }
    unit_list_iterate_end;
  }
  else {
    if( sea_cnt > 0 ) {         /* maybe it's an island, try to get on boats */
      int xb = -1, yb = -1;
      unit_list_iterate(map_get_tile(x, y)->units, punit2)
        if( punit2->owner == me ) {
          send_unit_info( 0, punit2);
          while(1) {
	    rand_neighbour(x, y, &xu, &yu);
	    if( can_unit_move_to_tile(punit2, xu, yu, TRUE) ) break;
	    if( can_unit_move_to_tile(punit2, xb, yb, TRUE) ) {
              xu = xb; yu = yb;
              break;
	    }
            if( is_free_sea(xu, yu, me) ) {
              boat = find_a_unit_type(L_BARBARIAN_BOAT, -1);
	      create_unit( barbarians, xu, yu, boat, 0, 0, -1);
	      xb = xu; yb = yu;
	      break;
	    }
          }
          handle_unit_move_request(barbarians, punit2, xu, yu, TRUE);
        }
      unit_list_iterate_end;
    }
    else {               /* The village is surrounded! Kill the explorer. */
      unit_list_iterate(map_get_tile(x, y)->units, punit2)
        if( punit2->owner != me ) {
          wipe_unit_safe(barbarians, punit2, &myiter);
          alive = 0;
        }
        else
          send_unit_info( 0, punit2);
      unit_list_iterate_end;
    }
  }

  /* FIXME: I don't know if this is needed*/
  show_area(barbarians, xu, yu, 3);

  return alive;
}

/**************************************************************************
Is sea not further than a couple of tiles away from land
**************************************************************************/

static int is_near_land( int x0, int y0)
{
  int x, y;

  for( x = x0-4; x < x0+5; x++) {
    for(y = y0-4; y < y0+5; y++) {
      if( y < 0 || y >= map.ysize )
        continue;
      if( map_get_terrain(x,y) != T_OCEAN )
        return 1;
    }
  }
  return 0;
}

/**************************************************************************
return this or a neighbouring tile that is free of any units
**************************************************************************/

static int find_empty_tile_nearby(int x0, int y0, int *x, int *y)
{
  int xx[9] = { 0, -1, 0, 1, -1, 1, -1, 0, 1 };
  int yy[9] = { 0, -1, -1, -1, 0, 0, 1, 1, 1 };
  int i;

  for( i = 0; i < 9; i++) {
    *x = x0 + xx[i];
    *y = map_adjust_y(y0 + yy[i]);
    if( unit_list_size(&map_get_tile(*x, *y)->units) == 0 )
      return 1;
  }
  return 0;
}

/**************************************************************************
The barbarians are summoned at a randomly chosen place if:
1. It's not closer than MIN_UNREST_DIST and not further than MAX_UNREST_DIST
   from the nearest city. City owner is called 'victim' here.
2. The place or a neighbouring tile must be empty to deploy the units.
3. If it's the sea it shouldn't be far from the land. (questionable)
4. Place must be known to the victim
5. The uprising chance depends also on the victim empire size, its
   government (civil_war_chance) and barbarian difficulty level.
6. The number of land units also depends slightly on victim's empire size
   and barbarian difficulty level.
Q: The empire size is used so there are no uprisings in the beginning of
   the game (year is not good as it can be customized), but it seems a bit
   unjust if someone is always small. So maybe it should rather be an average
   number of cities (all cities/player num)?
   Depending on the victim government type is also questionable.
**************************************************************************/

static void try_summon_barbarians(void)
{
  int x, y, xu, yu;
  int i, boat, cap, dist, unit;
  int uprise = 1;
  struct city *pc;
  struct player *barbarians, *victim;

  x = myrand(map.xsize);
  y = 1 + myrand(map.ysize-2);  /* No uprising on North or South Pole */

  if( !(pc = dist_nearest_city(NULL, x, y, 1, 0)) )       /* any city */
    return;

  victim = &game.players[pc->owner];

  dist = real_map_distance(x, y, pc->x, pc->y);
  freelog(LOG_DEBUG,"Closest city to %d %d is %s at %d %d which is %d far", 
                    x, y, pc->name, pc->x, pc->y, dist);
  if( dist > MAX_UNREST_DIST || dist < MIN_UNREST_DIST )
    return;

  /* I think Sea Raiders can come out of unknown sea territory */
  if( !find_empty_tile_nearby(x,y,&xu,&yu) ||
      (!map_get_known(xu, yu, victim) && map_get_terrain(xu, yu) != T_OCEAN) ||
      !is_near_land(xu, yu) )
    return;

  /* do not harass small civs - in practice: do not uprise at the beginning */
  if( (int)myrand(UPRISE_CIV_MORE) >
           (int)city_list_size(&victim->cities) -
                UPRISE_CIV_SIZE/(game.barbarianrate-1)
      || myrand(100) > get_gov_pcity(pc)->civil_war )
    return;
  freelog(LOG_DEBUG,"Barbarians are willing to fight");

  if(map_get_special(x,y) & S_HUT) { /* remove the hut in place of uprising */
    map_clear_special(x,y,S_HUT);
    send_tile_info(0, x, y);
  }

  if( map_get_terrain(xu,yu) != T_OCEAN ) {        /* land barbarians */
    barbarians = create_barbarian_player(1);
    if( city_list_size(&victim->cities) > UPRISE_CIV_MOST )
      uprise = 3;
    for( i=0; i < myrand(3) + uprise*(game.barbarianrate); i++) {
      unit = find_a_unit_type(L_BARBARIAN,L_BARBARIAN_TECH);
      create_unit( barbarians, xu, yu, unit, 0, 0, -1);
      freelog(LOG_DEBUG, "Created barbarian unit %s",unit_types[unit].name);
    }
    create_unit( barbarians, xu, yu, get_role_unit(L_BARBARIAN_LEADER,0), 0, 0, -1);
  }
  else {                   /* sea raiders - their units will be veteran */

    barbarians = create_barbarian_player(0);
    boat = find_a_unit_type(L_BARBARIAN_BOAT,-1);
    create_unit( barbarians, xu, yu, boat, 1, 0, -1);
    cap = get_transporter_capacity(unit_list_get(&map_get_tile(xu, yu)->units, 0));
    for( i=0; i<cap-1; i++) {
      unit = find_a_unit_type(L_BARBARIAN_SEA,L_BARBARIAN_SEA_TECH);
      create_unit( barbarians, xu, yu, unit, 1, 0, -1);
      freelog(LOG_DEBUG, "Created barbarian unit %s",unit_types[unit].name);
    }
    create_unit( barbarians, xu, yu, get_role_unit(L_BARBARIAN_LEADER,0), 0, 0, -1);
  }

  unit_list_iterate(map_get_tile(x,y)->units, punit2)
    send_unit_info(0, punit2);
  unit_list_iterate_end;

  /* to let them know where to get you */
  show_area(barbarians, xu, yu, 3);
  show_area(barbarians, pc->x, pc->y, 3);

  /* There should probably be a different message about Sea Raiders */
  if( is_land_barbarian(barbarians) )
    notify_player_ex( victim, xu, yu, E_UPRISING, _("Native unrest near %s led by %s."),
                      pc->name, barbarians->name);
  else if( map_get_known_and_seen(xu, yu, victim) )
    notify_player_ex( victim, xu, yu, E_UPRISING, _("Sea raiders seen near %s!"),
                      pc->name);
}

/**************************************************************************
Summon barbarians out of the blue. Try more times for more difficult levels
- which means there can be more than one uprising in one year!
**************************************************************************/

void summon_barbarians(void)
{
  int i, n;

  if(!game.barbarianrate)
    return;

  if(game.year < game.onsetbarbarian)
    return;

  n = map.xsize*map.ysize / MAP_FACTOR;    /* map size adjustment */

  for( i=0; i < n*(game.barbarianrate-1); i++)
    try_summon_barbarians();
}

