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

#include <map.h>
#include <game.h>
#include <unitfunc.h>
#include <unithand.h>
#include <unittools.h>
#include <gotohand.h>

struct move_cost_map warmap;

struct stack_element {
  unsigned char x, y;
} warstack[MAP_MAX_WIDTH * MAP_MAX_HEIGHT];

/* this wastes ~20K of memory and should really be fixed but I'm lazy  -- Syela */

int warstacksize;
int warnodes;

void add_to_stack(int x, int y)
{
  int i;
  struct stack_element tmp;
  warstack[warstacksize].x = x;
  warstack[warstacksize].y = y;
  warstacksize++;
}

void init_warmap(int orig_x, int orig_y, enum unit_move_type which)
{
  int x, y, i, j;
  int maxcost = THRESHOLD * 6 + 2; /* should be big enough without being TOO big */
  for (x = 0; x < map.xsize; x++) {
    if (x > orig_x)
      i = MIN(x - orig_x, orig_x + map.xsize - x);
    else
      i = MIN(orig_x - x, x + map.xsize - orig_x);      
    for (y = 0; y < map.ysize; y++) {
      if (y > orig_y)
        j = MAX(y - orig_y, i);
      else
        j = MAX(orig_y - y, i);
      j *= 3;
      if (j < maxcost) j = maxcost;
      if (j > 255) j = 255;
      if (which == LAND_MOVING) warmap.cost[x][y] = j; /* one if by land */
      else warmap.seacost[x][y] = j;
    }
  }
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
  struct tile *tile0, *tile1;
  struct city *acity;

  if (pcity) { orig_x = pcity->x; orig_y = pcity->y; }
  else { orig_x = punit->x; orig_y = punit->y; }

  init_warmap(orig_x, orig_y, which);
  warstacksize = 0;
  warnodes = 0;

  add_to_stack(orig_x, orig_y);

  if (punit && unit_flag(punit->type, F_IGTER)) igter++;
  if (punit && punit->type == U_SETTLERS) maxcost >>= 1;

  do {
    x = warstack[warnodes].x;
    y = warstack[warnodes].y;
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
        if (map_get_terrain(xx[i], yy[j]) == T_OCEAN) c = maxcost;
/*        if (tile0->move_cost[k] == -3 || tile0->move_cost[k] > 16) c = maxcost;*/
        else if (igter) c = 3; /* NOT c = 1 */
        else if (punit) c = MIN(tile0->move_cost[k], unit_types[punit->type].move_rate);
        else c = tile0->move_cost[k];
        
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
  if (warnodes > 15000) printf("Warning: %d nodes in map #%d for (%d, %d)\n",
     warnodes, which, orig_x, orig_y);
/* printf("Generated warmap for (%d,%d) with %d nodes checked.\n", orig_x, orig_y, warnodes); */
/* warnodes is often as much as 2x the size of the continent -- Syela */
}

void generate_warmap(struct city *pcity, struct unit *punit)
{
/*printf("Generating warmap, pcity = %s, punit = %s\n", (pcity ?
pcity->name : "NULL"), (punit ? unit_types[punit->type].name : "NULL"));*/

  if (punit) {
    if (pcity && pcity == warmap.warcity) return; /* time-saving shortcut */
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
  } else {
    really_generate_warmap(pcity, punit, LAND_MOVING);
    really_generate_warmap(pcity, punit, SEA_MOVING);
  }
}

/* ....... end of old advmilitary.c, beginning of old gotohand.c. ..... */

int could_unit_move_to_tile(struct unit *punit, int x0, int y0, int x, int y)
{ /* this WAS can_unit_move_to_tile with the notifys removed -- Syela */
/* but is now a little more complicated to allow non-adjacent tiles */
  struct tile *ptile,*ptile2;
  struct unit_list *punit_list;
  struct unit *punit2;
  struct city *pcity;
  int zoc;

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
  return (is_my_zoc(punit, x0, y0) || is_my_zoc(punit, x, y));
}

int goto_tile_cost(struct player *pplayer, struct unit *punit, int x0, int y0, int x1, int y1, int m)
{
  if (!pplayer->ai.control && !map_get_known(x1, y1, pplayer)) {
/*    printf("Venturing into the unknown at (%d, %d).\n", x1, y1);*/
    return(3);
  }
  if (get_defender(pplayer, punit, x1, y1)) {
     if (!can_unit_attack_tile(punit, x1, y1)) return(255);
     if (!same_pos(punit->goto_dest_x, punit->goto_dest_y, x1, y1)) return(15);
/* arbitrary deterrent; if we wanted to attack, we wouldn't GOTO */
  } else {
    if (!could_unit_move_to_tile(punit, x0, y0, x1, y1) &&
        !same_pos(x1, y1, punit->goto_dest_x, punit->goto_dest_y)) return(255);
/* in order to allow transports to GOTO shore correctly */
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
  char *d[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };
  int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int igter = 0, xx[3], yy[3], x, y, i, j, k, tm, c;
  int orig_x, orig_y;
  struct tile *tile0;
  enum unit_move_type which = unit_types[punit->type].move_type;
  int maxcost = 255;
  unsigned char local_vector[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  
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

  do {
    x = warstack[warnodes].x;
    y = warstack[warnodes].y;
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
        else if (igter) c = 3; /* NOT c = 1 */
        else c = tile0->move_cost[k];
        c = goto_tile_cost(pplayer, punit, x, y, xx[i], yy[j], c);
        if (!dir_ok(x, y, dest_x, dest_y, k)) c += c;
        if (!c && !dir_ect(x, y, dest_x, dest_y, k)) c = 1;
        tm = warmap.cost[x][y] + c;
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
        else if (punit->type == U_TRIREME && !is_coastline(xx[i], yy[j])) c = 7;
        else c = 3;
        c = goto_tile_cost(pplayer, punit, x, y, xx[i], yy[j], c);
        if (!dir_ok(x, y, dest_x, dest_y, k)) c += c;
        tm = warmap.seacost[x][y] + c;
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
/*printf("GOTO: (%d, %d) -> (%d, %d), %d nodes, cost = %d\n", 
orig_x, orig_y, dest_x, dest_y, warnodes, maxcost - 1);*/
  if (maxcost == 255) return(0);
/* succeeded.  the vector at the destination indicates which way we get there */
/* backtracing */
  warnodes = 0;
  warstacksize = 0;
  add_to_stack(dest_x, dest_y);

  do {
    x = warstack[warnodes].x;
    y = warstack[warnodes].y;
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
/*printf("BACKTRACE: %d nodes\n", warnodes);*/
  return(1);
/* DONE! */
}

int find_a_direction(struct unit *punit)
{
  int k, d[8], x, y, n, a, best = 0, d0, d1, h0, h1, u;
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  struct tile *ptile, *adjtile;
  int nearland = 0;

  for (k = 0; k < 8; k++) {
    if (!(warmap.vector[punit->x][punit->y]&(1<<k))) d[k] = 0;
    else {
      x = punit->x + ii[k]; y = punit->y + jj[k];
      ptile = map_get_tile(x, y);
      d0 = get_virtual_defense_power(punit->type, punit->type, x, y);
      h0 = punit->hp; h1 = 0; d1 = 0; u = 1;
      unit_list_iterate(ptile->units, aunit)
        if (aunit->owner != punit->owner) d1 = -1; /* MINIMUM priority */
        else {
          u++;
          a = get_virtual_defense_power(aunit->type, aunit->type, x, y);
          if (a * aunit->hp > d1 * h1) { d1 = a; h1 = aunit->hp; }
        }
      unit_list_iterate_end;
      if (u == 1) d[k] = d0 * h0;
      else if ((d0 * h0) <= (d1 * h1)) d[k] = (d1 * h1) * (u - 1) / u;
      else d[k] = MIN(d0 * h0 * u, d0 * h0 * d0 * h0 * (u - 1) / MAX(u, (d1 * h1 * u)));
      if (d0 > d1) d1 = d0;

      for (n = 0; n < 8; n++) {
        adjtile = map_get_tile(x + ii[n], y + jj[n]);
        if (adjtile->terrain != T_OCEAN) nearland++;
        if (!((adjtile->known)&(1u<<punit->owner))) d[k]++; /* nice but not important */
        else { /* NOTE: Not being omniscient here!! -- Syela */
          unit_list_iterate(adjtile->units, aunit) /* lookin for trouble */
            if (aunit->owner != punit->owner && (a = get_attack_power(aunit))) {
              if (punit->moves_left < ptile->move_cost[n] + 3) { /* can't fight */
                d[k] -= d1 * (aunit->hp * a * a / (a * a + d1 * d1));
              }
            }
          unit_list_iterate_end;
        } /* end this-tile-is-seen else */
      } /* end tiles-adjacent-to-dest for */
 
      if (punit->type == U_TRIREME && !nearland) {
        if (punit->moves_left < 6) d[k] = 1;
        else if (punit->moves_left == 6) d[k] -= 10;
      }

      if (d[k] < 1) d[k] = 1;
      if (d[k] > best) { 
        best = d[k];
/*printf("New best = %d: (%d, %d) -> (%d, %d)\n", best, punit->x, punit->y, x, y);*/
      }
    } /* end is-a-valid-vector */
  } /* end for */
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
       map_get_terrain(x, y) != T_OCEAN && !map_get_tile(x, y)->city_id &&
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
  char *d[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };

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

  if(find_the_shortest_path(pplayer, punit, 
			    punit->goto_dest_x, punit->goto_dest_y)) {

    do {
      k = find_a_direction(punit);
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
else printf("Did not find the shortest path for %s's %s at (%d, %d) -> (%d, %d)\n",
pplayer->name, unit_types[punit->type].name, punit->x, punit->y, 
punit->goto_dest_x, punit->goto_dest_y);

  punit->activity=ACTIVITY_IDLE;
  send_unit_info(0, punit, 0);
}
