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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <map.h>
#include <game.h>
#include <unitfunc.h>
#include <unithand.h>
#include <unittools.h>
#include <gotohand.h>
#include <settlers.h>
#include <log.h>

struct move_cost_map warmap;

#define WARSTACK_DIM 16384
/* Must be a power of 2, size of order (MAP_MAX_WIDTH*MAP_MAX_HEIGHT);
 * (Could be non-power-of-2 and then use modulus in add_to_stack
 * and get_from_warstack.)
 */
   
struct stack_element {
  unsigned char x, y;
} warstack[WARSTACK_DIM];

/* This wastes ~20K of memory and should really be fixed but I'm lazy  -- Syela
 *
 * Note this is really a queue.  And now its a circular queue to avoid
 * problems with large maps.  Note that a single (x,y) can appear multiple
 * times in warstack due to a sequence of paths with successively
 * smaller costs.  --dwp
 */

unsigned int warstacksize;
unsigned int warnodes;

static void add_to_stack(int x, int y)
{
  unsigned int i = warstacksize & (WARSTACK_DIM-1);
  warstack[i].x = x;
  warstack[i].y = y;
  warstacksize++;
}

static void get_from_warstack(unsigned int i, int *x, int *y)
{
  assert(i<warstacksize && warstacksize-i<WARSTACK_DIM);
  i &= (WARSTACK_DIM-1);
  *x = warstack[i].x;
  *y = warstack[i].y;
}

void init_warmap(int orig_x, int orig_y, enum unit_move_type which)
{
  int x, y;
  for (x = 0; x < map.xsize; x++) 
    for (y = 0; y < map.ysize; y++) 
      if (which == LAND_MOVING) warmap.cost[x][y] = 255; /* one if by land */
      else warmap.seacost[x][y] = 255;
  if (which == LAND_MOVING) warmap.cost[orig_x][orig_y] = 0;
  else warmap.seacost[orig_x][orig_y] = 0;
}  

void really_generate_warmap(struct city *pcity, struct unit *punit, enum unit_move_type which)
{ /* let generate_warmap interface to this function */
  int x, y, c, i, j, k, xx[3], yy[3], tm;
  int orig_x, orig_y;
  int igter = 0;
  int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int maxcost = THRESHOLD * 6 + 2; /* should be big enough without being TOO big */
  struct tile *tile0;
  struct player *pplayer;

  if (pcity) {
    orig_x = pcity->x;
    orig_y = pcity->y;
    pplayer = &game.players[pcity->owner];
  } else {
    orig_x = punit->x;
    orig_y = punit->y;
    pplayer = &game.players[punit->owner];
  }

  init_warmap(orig_x, orig_y, which);
  warstacksize = 0;
  warnodes = 0;

  add_to_stack(orig_x, orig_y);

  if (punit && unit_flag(punit->type, F_IGTER)) igter++;
  if (punit && unit_flag(punit->type, F_SETTLERS)
      && get_unit_type(punit->type)->move_rate==3) maxcost >>= 1;
  /* (?) was punit->type == U_SETTLERS -- dwp */

  do {
    get_from_warstack(warnodes, &x, &y);
    warnodes++; /* for debug purposes */
    tile0 = map_get_tile(x, y);
    if((xx[2]=x+1)==map.xsize) xx[2]=0;
    if((xx[0]=x-1)==-1) xx[0]=map.xsize-1;
    xx[1] = x;
    if ((yy[0]=y-1)==-1) yy[0] = 0;
    if ((yy[2]=y+1)==map.ysize) yy[2]=y;
    yy[1] = y;
    for (k = 0; k < 8; k++) {
      i = ii[k]; j = jj[k]; /* saves CPU cycles? */
      if (which == LAND_MOVING) {
/*        if (tile0->move_cost[k] == -3 || tile0->move_cost[k] > 16) c = maxcost;*/
        if (map_get_terrain(xx[i], yy[j]) == T_OCEAN) {
          if (punit && is_transporter_with_free_space(pplayer, xx[i], yy[j])) c = 3;
          else c = maxcost;
        } else if (tile0->terrain == T_OCEAN) c = 3;
        else if (igter) c = 3; /* NOT c = 1 */
        else if (punit) c = MIN(tile0->move_cost[k], unit_types[punit->type].move_rate);
/*        else c = tile0->move_cost[k]; 
This led to a bad bug where a unit in a swamp was considered too far away */
        else {
          tm = map_get_tile(xx[i], yy[j])->move_cost[7-k];
          c = (tile0->move_cost[k] + tm + (tile0->move_cost[k] > tm ? 1 : 0))>>1;
        }
        
        tm = warmap.cost[x][y] + c;
        if (warmap.cost[xx[i]][yy[j]] > tm && tm < maxcost) {
          warmap.cost[xx[i]][yy[j]] = tm;
          add_to_stack(xx[i], yy[j]);
        }
      } else {
        c = 3; /* allow for shore bombardment/transport/etc */
        tm = warmap.seacost[x][y] + c;
        if (warmap.seacost[xx[i]][yy[j]] > tm && tm < maxcost) {
          warmap.seacost[xx[i]][yy[j]] = tm;
          if (tile0->move_cost[k] == -3) add_to_stack(xx[i], yy[j]);
        }
      }
    } /* end for */
  } while (warstacksize > warnodes);
  if (warnodes > WARSTACK_DIM) {
    flog(LOG_DEBUG, "Warning: %u nodes in map #%d for (%d, %d)",
	 warnodes, which, orig_x, orig_y);
  }
/* printf("Generated warmap for (%d,%d) with %u nodes checked.\n", orig_x, orig_y, warnodes); */
/* warnodes is often as much as 2x the size of the continent -- Syela */
}

void generate_warmap(struct city *pcity, struct unit *punit)
{
/*printf("Generating warmap, pcity = %s, punit = %s\n", (pcity ?
pcity->name : "NULL"), (punit ? unit_types[punit->type].name : "NULL"));*/

  if (punit) {
    if (pcity && pcity == warmap.warcity) return; /* time-saving shortcut */
    if (warmap.warunit == punit && !warmap.cost[punit->x][punit->y]) return;
    pcity = 0;
  }

  warmap.warcity = pcity;
  warmap.warunit = punit;

  if (punit) {
    if (is_sailing_unit(punit)) {
      init_warmap(punit->x, punit->y, LAND_MOVING);
      really_generate_warmap(pcity, punit, SEA_MOVING);
    } else {
      init_warmap(punit->x, punit->y, SEA_MOVING);
      really_generate_warmap(pcity, punit, LAND_MOVING);
    }
    warmap.orig_x = punit->x;
    warmap.orig_y = punit->y;
  } else {
    really_generate_warmap(pcity, punit, LAND_MOVING);
    really_generate_warmap(pcity, punit, SEA_MOVING);
    warmap.orig_x = pcity->x;
    warmap.orig_y = pcity->y;
  }
}

/* ....... end of old advmilitary.c, beginning of old gotohand.c. ..... */
int dir_ok(int x0, int y0, int x1, int y1, int k)
{ /* The idea of this is to check less nodes in the wrong direction.
These if's might cost some CPU but hopefully less overall. -- Syela */
  int n = 0, s = 0, e = 0, w = 0, dx;
  if (y1 > y0) s = y1 - y0;
  else n = y0 - y1;
  dx = x1 - x0;
  if (dx > map.xsize>>1) w = map.xsize - dx;
  else if (dx > 0) e = dx;
  else if (dx + (map.xsize>>1) > 0) w = 0 - dx;
  else e = map.xsize + dx;
  if (e == map.xsize / 2 || w == map.xsize / 2) { /* thanks, Massimo */
    if (k < 3 && s >= MAX(e, w)) return 0;
    if (k > 4 && n >= MAX(e, w)) return 0;
    return 1;
  }
  switch(k) {
    case 0:
      if (e >= n && s >= w) return 0;
      else return 1;
    case 1:
      if (s && s >= e && s >= w) return 0;
      else return 1;
    case 2:
      if (w >= n && s >= e) return 0;
      else return 1;
    case 3:
      if (e && e >= n && e >= s) return 0;
      else return 1;
    case 4:
      if (w && w >= n && w >= s) return 0;
      else return 1;
    case 5:
      if (e >= s && n >= w) return 0;
      else return 1;
    case 6:
      if (n && n >= w && n >= e) return 0;
      else return 1;
    case 7:
      if (w >= s && n >= e) return 0;
      else return 1;
    default:
      printf("Bad dir_ok call: (%d, %d) -> (%d, %d), %d\n", x0, y0, x1, y1, k);
      return 0;
  }
}

int could_be_my_zoc(struct unit *myunit, int x0, int y0)
{ /* Fix to bizarre did-not-find bug.  Thanks, Katvrr -- Syela */
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  int ax, ay, k;
  int owner=myunit->owner;

  if (same_pos(x0, y0, myunit->x, myunit->y)) return 0; /* can't be my zoc */
  if (is_tiles_adjacent(x0, y0, myunit->x, myunit->y) &&
      !is_enemy_unit_tile(x0, y0, owner)) return 0;

  for (k = 0; k < 8; k++) {
    ax = map_adjust_x(myunit->x + ii[k]); ay = map_adjust_y(myunit->y + jj[k]);
    if (is_enemy_city_tile(ax,ay,owner))
      return 0;
    if (!dir_ok(x0, y0, myunit->goto_dest_x, myunit->goto_dest_y, k)) continue;
/* don't care too much about ZOC of units we've already gone past -- Syela */
    if (is_enemy_unit_tile(ax,ay,owner))
      if((is_sailing_unit(myunit) && (map_get_terrain(ax,ay)==T_OCEAN)) ||
         (!is_sailing_unit(myunit) && (map_get_terrain(ax,ay)!=T_OCEAN)) )
        return 0;
  }
  return -1; /* flag value */
}


int could_unit_move_to_tile(struct unit *punit, int x0, int y0, int x, int y)
{ /* this WAS can_unit_move_to_tile with the notifys removed -- Syela */
/* but is now a little more complicated to allow non-adjacent tiles */
  struct tile *ptile,*ptile2;
  struct city *pcity;

  if(punit->activity!=ACTIVITY_IDLE && punit->activity!=ACTIVITY_GOTO)
    return 0;
  
  if(x<0 || x>=map.xsize || y<0 || y>=map.ysize)
    return 0;
  
  if(!is_tiles_adjacent(x0, y0, x, y))
    return 0; 

  if(is_enemy_unit_tile(x,y,punit->owner))
    return 0;

  ptile=map_get_tile(x, y);
  ptile2=map_get_tile(x0, y0);
  if(is_ground_unit(punit)) {
    /* Check condition 4 */
    if(ptile->terrain==T_OCEAN &&
       !is_transporter_with_free_space(&game.players[punit->owner], x, y))
	return 0;

    /* Moving from ocean */
    if(ptile2->terrain==T_OCEAN) {
      /* Can't attack a city from ocean unless marines */
      if(!unit_flag(punit->type, F_MARINES) && is_enemy_city_tile(x,y,punit->owner)) {
	return 0;
      }
    }
  } else if(is_sailing_unit(punit)) {
    if(ptile->terrain!=T_OCEAN && ptile->terrain!=T_UNKNOWN)
      if(!is_friendly_city_tile(x,y,punit->owner))
	return 0;
  } 

  if((pcity=map_get_city(x, y))) {
    if ((pcity->owner!=punit->owner && (is_air_unit(punit) || 
                                        !is_military_unit(punit)))) {
        return 0;  
    }
  }

/*  zoc = zoc_ok_move(punit, x, y);      can't use this, so reproducing ... */
  /* if you have units there, you can move */
  if (is_friendly_unit_tile(x,y,punit->owner))
    return 1;
  if (!is_ground_unit(punit) || unit_flag(punit->type, F_IGZOC))
    return 1;
  if (map_get_city(x,y) || map_get_city(x0, y0))
    return 1;
  if (is_my_zoc(punit, x0, y0) || is_my_zoc(punit, x, y)) return 1;
  return (could_be_my_zoc(punit, x0, y0));
}

int goto_tile_cost(struct player *pplayer, struct unit *punit, int x0, int y0, int x1, int y1, int m)
{
  int i;
  if (!pplayer->ai.control && !map_get_known(x1, y1, pplayer)) {
/*    printf("Venturing into the unknown at (%d, %d).\n", x1, y1);*/
/*    return(3);   People seemed not to like this. -- Syela */
    return(15); /* arbitrary deterrent. */
  }
  if (get_defender(pplayer, punit, x1, y1)) {
     if (same_pos(punit->goto_dest_x, punit->goto_dest_y, x1, y1))
       return(MIN(m, unit_types[punit->type].move_rate));
     if (!can_unit_attack_tile(punit, x1, y1)) return(255);
     return(15);
/* arbitrary deterrent; if we wanted to attack, we wouldn't GOTO */
  } else {
    i = could_unit_move_to_tile(punit, x0, y0, x1, y1);
    if (!i && !unit_flag(punit->type, F_IGZOC) && is_ground_unit(punit) &&
        !is_my_zoc(punit, x0, y0) && !is_my_zoc(punit, x1, y1) &&
        !could_be_my_zoc(punit, x0, y0)) return(255);
    if (!i && !same_pos(x1, y1, punit->goto_dest_x, punit->goto_dest_y)) return(255);
/* in order to allow transports to GOTO shore correctly */
    if (i < 0) return(15); /* there's that deterrent again */
  }
  return(MIN(m, unit_types[punit->type].move_rate));
}

void init_gotomap(int orig_x, int orig_y)
{
  int x, y;
  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      warmap.seacost[x][y] = 255;
      warmap.cost[x][y] = 255;
      warmap.vector[x][y] = 0;
    }
  }
  warmap.cost[orig_x][orig_y] = 0;
  warmap.seacost[orig_x][orig_y] = 0;
  return;
} 

int dir_ect(int x0, int y0, int x1, int y1, int k)
{
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  int x, y;
  x = map_adjust_x(ii[k] + x0);
  y = jj[k] + y0; /* trusted, since I know who's calling us. -- Syela */
  if (same_pos(x, y, x1, y1)) return 1; /* as direct as it gets! */
  return (1 - dir_ok(x, y, x1, y1, 7-k));
}

int find_the_shortest_path(struct player *pplayer, struct unit *punit,
                           int dest_x, int dest_y)
{
/*  char *d[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };*/
  int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int igter = 0, xx[3], yy[3], x, y, i, j, k, tm, c;
  int orig_x, orig_y;
  struct tile *tile0;
  enum unit_move_type which = unit_types[punit->type].move_type;
  int maxcost = 255;
  unsigned char local_vector[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  struct unit *passenger; /* and I ride and I ride */
  
/* Often we'll already have a working warmap, but I don't want to
deal with merging these functions yet.  Once they work correctly
and independently I can worry about optimizing them. -- Syela */

  memset(local_vector, 0, sizeof(local_vector));
/* I think I only need to zero (orig_x, orig_y), but ... */

  init_gotomap(punit->x, punit->y);
  warstacksize = 0;
  warnodes = 0;

  orig_x = punit->x;
  orig_y = punit->y;

  add_to_stack(orig_x, orig_y);

  if (punit && unit_flag(punit->type, F_IGTER)) igter++;
  if (which == SEA_MOVING) passenger = other_passengers(punit);
  else passenger = 0;

  if (passenger)
    if (map_get_terrain(dest_x, dest_y) == T_OCEAN ||
          is_friendly_unit_tile(dest_x, dest_y, passenger->owner) ||
          is_friendly_city_tile(dest_x, dest_y, passenger->owner) ||
          unit_flag(passenger->type, F_MARINES) ||
          is_my_zoc(passenger, dest_x, dest_y)) passenger = 0;

/* if passenger is nonzero, the next-to-last tile had better be zoc-ok! */

  do {
    get_from_warstack(warnodes, &x, &y);
    warnodes++; /* for debug purposes */
    tile0 = map_get_tile(x, y);
    if((xx[2]=x+1)==map.xsize) xx[2]=0;
    if((xx[0]=x-1)==-1) xx[0]=map.xsize-1;
    xx[1] = x;
    if ((yy[0]=y-1)==-1) yy[0] = 0;
    if ((yy[2]=y+1)==map.ysize) yy[2]=y;
    yy[1] = y;
    
    for (k = 0; k < 8; k++) {
      i = ii[k]; j = jj[k]; /* saves CPU cycles? */
      if (which != SEA_MOVING) {
        if (which != LAND_MOVING) c = 3;
        else if (tile0->move_cost[k] == -3 || tile0->move_cost[k] > 16) c = maxcost; 
        else if (igter) c = (tile0->move_cost[k] ? 3 : 0); /* Reinier's fix -- Syela */
        else c = tile0->move_cost[k];
        c = goto_tile_cost(pplayer, punit, x, y, xx[i], yy[j], c);
        if (!dir_ok(x, y, dest_x, dest_y, k)) c += c;
        if (!c && !dir_ect(x, y, dest_x, dest_y, k)) c = 1;
        tm = warmap.cost[x][y] + c;
#ifdef ACTUALLYTESTED
        if (warmap.cost[x][y] < punit->moves_left && tm < maxcost &&
            tm >= punit->moves_left - MIN(3, c) && enemies_at(punit, xx[i], yy[j])) {
          tm += unit_types[punit->type].move_rate;
/*printf("%s#%d@(%d,%d) dissuaded from (%d,%d) -> (%d,%d)\n",
unit_types[punit->type].name, punit->id, punit->x, punit->y, xx[i], yy[j],
punit->goto_dest_x, punit->goto_dest_y);*/
        }
#endif
        if (tm < maxcost) {
          if (warmap.cost[xx[i]][yy[j]] > tm) {
            warmap.cost[xx[i]][yy[j]] = tm;
            add_to_stack(xx[i], yy[j]);
            local_vector[xx[i]][yy[j]] = 128>>k;
/*printf("Candidate: %s from (%d, %d) to (%d, %d) +%d to %d\n", d[k], x, y, xx[i], yy[j], c, tm);*/
          } else if (warmap.cost[xx[i]][yy[j]] == tm) {
            local_vector[xx[i]][yy[j]] |= 128>>k;
/*printf("Co-Candidate: %s from (%d, %d) to (%d, %d) +%d to %d\n", d[k], x, y, xx[i], yy[j], c, tm);*/
          }
        }
      } else {
        if (tile0->move_cost[k] != -3) c = maxcost;
        else if (unit_flag(punit->type, F_TRIREME) && !is_coastline(xx[i], yy[j])) c = 7;
        else c = 3;
/* I want to disable these totally but for some reason it bugs. -- Syela */
        c = goto_tile_cost(pplayer, punit, x, y, xx[i], yy[j], c);
        if (xx[i] == dest_x && yy[j] == dest_y && passenger && c < 60 &&
            !is_my_zoc(passenger, x, y)) c = 60; /* passenger cannot disembark */
        if (!dir_ok(x, y, dest_x, dest_y, k)) c += c;
        tm = warmap.seacost[x][y] + c;
        if (warmap.seacost[x][y] < punit->moves_left && tm < maxcost &&
            (pplayer->ai.control || !same_pos(xx[i], yy[j], dest_x, dest_y)) &&
            tm >= punit->moves_left - (get_transporter_capacity(punit) >
            unit_types[punit->type].attack_strength ? 3 : 2) &&
            enemies_at(punit, xx[i], yy[j])) {
          tm += unit_types[punit->type].move_rate;
/*printf("%s#%d@(%d,%d) dissuaded from (%d,%d) -> (%d,%d)\n",
unit_types[punit->type].name, punit->id, punit->x, punit->y, xx[i], yy[j],
punit->goto_dest_x, punit->goto_dest_y);*/
        }
        if (tm < maxcost) {
          if (warmap.seacost[xx[i]][yy[j]] > tm) {
            warmap.seacost[xx[i]][yy[j]] = tm;
            add_to_stack(xx[i], yy[j]);
            local_vector[xx[i]][yy[j]] = 128>>k;
          } else if (warmap.seacost[xx[i]][yy[j]] == tm) {
            local_vector[xx[i]][yy[j]] |= 128>>k;
          }
        }
      }
      if (xx[i] == dest_x && yy[j] == dest_y && maxcost > tm) {
/*printf("Found path, cost = %d\n", tm);*/
        maxcost = tm + 1; /* NOT = tm.  Duh! -- Syela */
      }
    } /* end for */
  } while (warstacksize > warnodes);
/*printf("GOTO: (%d, %d) -> (%d, %d), %u nodes, cost = %d\n", 
orig_x, orig_y, dest_x, dest_y, warnodes, maxcost - 1);*/
  if (maxcost == 255) return(0);
/* succeeded.  the vector at the destination indicates which way we get there */
/* backtracing */
  warnodes = 0;
  warstacksize = 0;
  add_to_stack(dest_x, dest_y);

  do {
    get_from_warstack(warnodes, &x, &y);
    warnodes++; /* for debug purposes */
    tile0 = map_get_tile(x, y);
    if((xx[2]=x+1)==map.xsize) xx[2]=0;
    if((xx[0]=x-1)==-1) xx[0]=map.xsize-1;
    xx[1] = x;
    if ((yy[0]=y-1)==-1) yy[0] = 0;
    if ((yy[2]=y+1)==map.ysize) yy[2]=y;
    yy[1] = y;

    for (k = 0; k < 8; k++) {
      i = ii[k]; j = jj[k]; /* saves CPU cycles? */
      if (local_vector[x][y] & (1<<k)) {
/* && (local_vector[xx[i]][yy[j]] || !warmap.vector[xx[i]][yy[j]])) {
is not adequate to prevent RR loops.  Bummer. -- Syela */
        add_to_stack(xx[i], yy[j]);
        warmap.vector[xx[i]][yy[j]] |= 128>>k;
        local_vector[x][y] -= 1<<k; /* avoid repetition */
/*printf("PATH-SEGMENT: %s from (%d, %d) to (%d, %d)\n", d[7-k], xx[i], yy[j], x, y);*/
      }
    }
  } while (warstacksize > warnodes);
/*printf("BACKTRACE: %u nodes\n", warnodes);*/
  return(1);
/* DONE! */
}

int find_a_direction(struct unit *punit)
{
  int k, d[8], x, y, n, a, best = 0, d0, d1, h0, h1, u, c;
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  struct tile *ptile, *adjtile;
  int nearland;
  struct city *pcity;
  struct unit *passenger;
  struct player *pplayer = get_player(punit->owner);

  if (map_get_terrain(punit->x, punit->y) == T_OCEAN)
    passenger = other_passengers(punit);
  else passenger = 0;

  for (k = 0; k < 8; k++) {
    if (!(warmap.vector[punit->x][punit->y]&(1<<k))) d[k] = 0;
    else {
      if (is_ground_unit(punit))
        c = map_get_tile(punit->x, punit->y)->move_cost[k];
      else c = 3;
      if (unit_flag(punit->type, F_IGTER) && c) c = 1;
      x = map_adjust_x(punit->x + ii[k]); y = map_adjust_y(punit->y + jj[k]);
/*if (passenger) printf("%d@(%d,%d) evaluating (%d,%d)[%d/%d]\n",
punit->id, punit->x, punit->y, x, y, c, punit->moves_left);*/
      ptile = map_get_tile(x, y);
      d0 = get_simple_defense_power(punit->type, x, y);
      pcity = map_get_city(x, y);
      n = 2;
      if (pcity) { /* this code block inspired by David Pfitzner -- Syela */
        if (city_got_citywalls(pcity)) n += 2;
        if (city_got_building(pcity, B_SDI)) n++;
        if (city_got_building(pcity, B_SAM)) n++;
        if (city_got_building(pcity, B_COASTAL)) n++;
      }
      d0 = (d0 * n) / 2;
      h0 = punit->hp; h1 = 0; d1 = 0; u = 1;
      unit_list_iterate(ptile->units, aunit)
        if (aunit->owner != punit->owner) d1 = -1; /* MINIMUM priority */
        else {
          u++;
          a = get_simple_defense_power(aunit->type, x, y) * n / 2;
          if (a * aunit->hp > d1 * h1) { d1 = a; h1 = aunit->hp; }
        }
      unit_list_iterate_end;
      if (u == 1) d[k] = d0 * h0;
      else if (pcity || ptile->special&S_FORTRESS)
        d[k] = MAX(d0 * h0, d1 * h1);
      else if ((d0 * h0) <= (d1 * h1)) d[k] = (d1 * h1) * (u - 1) / u;
      else d[k] = MIN(d0 * h0 * u, d0 * h0 * d0 * h0 * (u - 1) / MAX(u, (d1 * h1 * u)));
      if (d0 > d1) d1 = d0;

      if (ptile->special&S_ROAD) d[k] += 10; /* in case we change directions */
      if (ptile->special&S_RAILROAD) d[k] += 10; /* next turn, roads are nice */

      nearland = 0;
      if (!pplayer->ai.control && !map_get_known(x, y, pplayer)) nearland++;
      for (n = 0; n < 8; n++) {
        adjtile = map_get_tile(x + ii[n], y + jj[n]);
        if (adjtile->terrain != T_OCEAN) nearland++;
        if (!((adjtile->known)&(1u<<punit->owner))) {
          if (punit->moves_left <= c) d[k] -= (d[k]>>4); /* Avoid the unknown */
          else d[k]++; /* nice but not important */
        } else { /* NOTE: Not being omniscient here!! -- Syela */
          unit_list_iterate(adjtile->units, aunit) /* lookin for trouble */
            if (aunit->owner != punit->owner && (a = get_attack_power(aunit))) {
              if (punit->moves_left < c + 3) { /* can't fight */
                if (passenger && !is_ground_unit(aunit)) d[k] = -99;
                else d[k] -= d1 * (aunit->hp * a * a / (a * a + d1 * d1));
              }
            }
          unit_list_iterate_end;
        } /* end this-tile-is-seen else */
      } /* end tiles-adjacent-to-dest for */
 
      if (unit_flag(punit->type, F_TRIREME) && !nearland) {
        if (punit->moves_left < 6) d[k] = -1; /* Tired of Kaput!! -- Syela */
        else if (punit->moves_left == 6) {
          for (n = 0; n < 8; n++) {
            if ((warmap.vector[x][y]&(1<<n))) {
              if (is_coastline(x + ii[n], y + jj[n])) nearland++;
            }
          }
          if (!nearland) d[k] = 1;
        }
      }

      if (d[k] < 1 && (!game.players[punit->owner].ai.control ||
         !punit->ai.passenger || punit->moves_left >= 6)) d[k] = 1;
      if (d[k] > best) { 
        best = d[k];
/*printf("New best = %d: (%d, %d) -> (%d, %d)\n", best, punit->x, punit->y, x, y);*/
      }
    } /* end is-a-valid-vector */
  } /* end for */

  if (!best) {
    return(-1);
  }

  do {
    k = myrand(8);
  } while (d[k] < best);
  return(k);
}

int goto_is_sane(struct player *pplayer, struct unit *punit, int x, int y, int omni)
{  
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  int k, possible = 0;
  if (same_pos(punit->x, punit->y, x, y)) return 1;
  if (is_ground_unit(punit) && 
          (omni || map_get_known(x, y, pplayer))) {
    if (map_get_terrain(x, y) == T_OCEAN) {
      if (is_transporter_with_free_space(pplayer, x, y)) {
        for (k = 0; k < 8; k++) {
          if (map_get_continent(punit->x, punit->y) ==
              map_get_continent(x + ii[k], y + jj[k]))
            possible++;
        }
      }
    } else { /* going to a land tile */
      if (map_get_continent(punit->x, punit->y) ==
            map_get_continent(x, y))
         possible++;
      else {
        for (k = 0; k < 8; k++) {
          if (map_get_continent(punit->x + ii[k], punit->y + jj[k]) ==
              map_get_continent(x, y))
            possible++;
        }
      }
    }
    return(possible);
  } else if (is_sailing_unit(punit) && (omni || map_get_known(x, y, pplayer)) &&
       map_get_terrain(x, y) != T_OCEAN && !map_get_city(x, y) &&
       !is_terrain_near_tile(x, y, T_OCEAN)) {
    return(0);
  } /* end pre-emption subroutine. */
  return(1);
}


/**************************************************************************
...
**************************************************************************/
void do_unit_goto(struct player *pplayer, struct unit *punit)
{
  int x, y, k;
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
/*  char *d[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };*/

  int id=punit->id;

/* there was a really oogy if here.  Mighty Mouse told me not to axe it
because it would cost oodles of CPU time.  He's right for the most part
but Peter and others have recommended more flexibility, so this is a little
different but should still pre-empt calculation of impossible GOTO's. -- Syela */

  if (same_pos(punit->x, punit->y, punit->goto_dest_x, punit->goto_dest_y) ||
      !goto_is_sane(pplayer, punit, punit->goto_dest_x, punit->goto_dest_y, 0)) {
    punit->activity=ACTIVITY_IDLE;
    send_unit_info(0, punit, 0);
    return;
  }

  punit->activity = ACTIVITY_GOTO; /* adding this as a failsafe -- Syela */

  if(!punit->moves_left) {
    send_unit_info(0, punit, 0);
    return;
  }

  if(find_the_shortest_path(pplayer, punit, 
			    punit->goto_dest_x, punit->goto_dest_y)) {

    do {
      if(!punit->moves_left) return;
      k = find_a_direction(punit);
      if (k < 0) {
/*printf("%s#%d@(%d,%d) stalling so it won't be killed.\n",
unit_types[punit->type].name, punit->id, punit->x, punit->y);*/
/*        punit->activity=ACTIVITY_IDLE;*/
	send_unit_info(0, punit, 0);
	return;
      }
/*printf("Going %s\n", d[k]);*/
      x = map_adjust_x(punit->x + ii[k]);
      y = punit->y + jj[k]; /* no need to adjust this */

      if(!punit->moves_left) return;
      if(!handle_unit_move_request(pplayer, punit, x, y)) {
/*printf("Couldn't handle it.\n");*/
	if(punit->moves_left) {
	  punit->activity=ACTIVITY_IDLE;
	  send_unit_info(0, punit, 0);
	  return;
	};
      } /*else printf("Handled.\n");*/
      if(!unit_list_find(&pplayer->units, id))
	return; /* unit died during goto! */

      if(punit->x!=x || punit->y!=y) {
/*printf("Aborting, out of movepoints.\n");*/
	send_unit_info(0, punit, 0);
	return; /* out of movepoints */
      }
/*printf("Moving on.\n");*/
    } while(!(x==punit->goto_dest_x && y==punit->goto_dest_y));
  }
  else {
    flog(LOG_DEBUG, "Did not find the shortest path for "
	 "%s's %s at (%d, %d) -> (%d, %d)",
	 pplayer->name, unit_types[punit->type].name,
	 punit->x, punit->y, punit->goto_dest_x, punit->goto_dest_y);
  }

  punit->activity=ACTIVITY_IDLE;
  send_unit_info(0, punit, 0);
}
