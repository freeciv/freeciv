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
#include <string.h>
#include <assert.h>

#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "packets.h"
#include "support.h"
#include "timing.h"

#include "cityhand.h"
#include "citytools.h"
#include "gotohand.h"
#include "maphand.h"
#include "unitfunc.h"
#include "unithand.h"
#include "unittools.h"

#include "aicity.h"
#include "aiunit.h"

#include "settlers.h"

extern struct move_cost_map warmap;

signed short int minimap[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
static unsigned short int territory[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
/* negative: in_city_radius, 0: unassigned, positive: city_des */

/*************************************************************************/

static int auto_settler_findwork(struct player *pplayer, struct unit *punit); 
static void auto_settlers_player(struct player *pplayer); 

static int is_already_assigned(struct unit *myunit, struct player *pplayer, 
			       int x, int y);

/**************************************************************************
...
**************************************************************************/
const char *get_a_name(struct player *pplayer)
{
  static char buf[80];
  static int x=0;
  sz_strlcpy(buf, city_name_suggestion(pplayer));
  if (!buf[0]) my_snprintf(buf, sizeof(buf), "city%d", x++);
  return buf;
}

/**************************************************************************
...
**************************************************************************/
static int ai_do_build_city(struct player *pplayer, struct unit *punit)
{
  int x,y;
  struct packet_unit_request req;
  struct city *pcity;
  req.unit_id=punit->id;
  sz_strlcpy(req.name, get_a_name(pplayer));
  x = punit->x; y = punit->y; /* Trevor Pering points out that punit gets freed */
  handle_unit_build_city(pplayer, &req);        
  pcity=map_get_city(x, y); /* so we need to cache x and y for a very short time */
  if (!pcity)
    return 0;
  for (y=0;y<5;y++)
    for (x=0;x<5;x++) {
      if ((x==0 || x==4) && (y==0 || y==4)) 
        continue;
      light_square(pplayer, x+pcity->x-2, y+pcity->y-2, 0);
    }
  return 1;
}

int amortize(int b, int d)
{
  int num = MORT - 1;
  int denom;
  int s = 1;
  assert(d >= 0);
  if (b < 0) { s = -1; b *= s; }
  while (d && b) {
    denom = 1;
    while (d >= 12 && !(b>>28) && !(denom>>27)) {
      b *= 3;          /* this is a kluge but it is 99.9% accurate and saves time */
      denom *= 5;      /* as long as MORT remains 24! -- Syela */
      d -= 12;
    }
    while (!(b>>25) && d && !(denom>>25)) {
      b *= num;
      denom *= MORT;
      d--;
    }
    if (denom > 1) {
      b = (b + (denom/2)) / denom;
    }
  }
  return(b * s);
}

void generate_minimap(void)
{
  int a, i, j;

  memset(minimap, 0, sizeof(minimap));
  for (a = 0; a < game.nplayers; a++) {
    city_list_iterate(game.players[a].cities, pcity)
      city_map_iterate(i, j) {
        minimap[map_adjust_x(pcity->x+i-2)][map_adjust_y(pcity->y+j-2)]--;
      }
    city_list_iterate_end;
  }
}

void remove_city_from_minimap(int x, int y)
{
  int i, j, n, xx;

  freelog(LOG_DEBUG, "Removing (%d, %d) from minimap.", x, y);
  for (j = -4; j <= 4; j++) {
    if (y+j < 0 || y+j >= map.ysize) continue;
    for (i = -4; i <= 4; i++) {
      xx = map_adjust_x(x+i);
      n = i * i + j * j;
      if (n <= 5) {
        if (minimap[xx][y+j] < 0) minimap[xx][y+j]++;
        else minimap[xx][y+j] = 0;
      } else if (n <= 20) {
        if (minimap[xx][y+j] > 0) minimap[xx][y+j] = 0;
      }
    }
  }
}    

void add_city_to_minimap(int x, int y)
{
  int i, j, n, xx;
  
  freelog(LOG_DEBUG, "Adding (%d, %d) to minimap.", x, y);
  for (j = -4; j <= 4; j++) {
    if (y+j < 0 || y+j >= map.ysize) continue;
    for (i = -4; i <= 4; i++) {
      xx = map_adjust_x(x+i);
      n = i * i + j * j;
      if (n <= 5) {
        if (minimap[xx][y+j] < 0) minimap[xx][y+j]--;
        else minimap[xx][y+j] = -1;
      } else if (n <= 20) {
        if (minimap[xx][y+j] > 0) minimap[xx][y+j] = 0;
      }
    }
  }
}    

void locally_zero_minimap(int x, int y)
{
  int i, j, n, xx;
  for (j = -2; j <= 2; j++) {
    if (y+j < 0 || y+j >= map.ysize) continue;
    for (i = -2; i <= 2; i++) {
      xx = map_adjust_x(x+i);
      n = i * i + j * j;
      if (n <= 5) {
        if (minimap[xx][y+j] > 0) minimap[xx][y+j] = 0;
      }
    }
  }
}  

int city_desirability(struct player *pplayer, int x, int y)
{ /* this whole funct assumes G_REP^H^H^HDEMOCRACY -- Syela */
  int taken[5][5], food[5][5], shield[5][5], trade[5][5];
  int irrig[5][5], mine[5][5], road[5][5];
  int i, j, f, n = 0;
  int worst, b2, best, ii, jj, val, cur;
  int d = 0;
  int a, i0, j0; /* need some temp variables */
  int temp=0, tmp=0;
  int debug = 0;
  int g = 1;
  struct tile *ptile;
  int con, con2;
  int har, t, sh;
  struct tile_type *ptype;
  struct city *pcity;
  int db;

  if (is_square_threatened(pplayer, x, y))
    return 0;
  
  ptile = map_get_tile(x, y);
  if (ptile->terrain == T_OCEAN) return(0);
  pcity = map_get_city(x, y);
  if (pcity && pcity->size > (game.add_to_size_limit - 1)) return(0);
  if (!pcity && minimap[x][y] < 0) return(0);
  if (!pcity && minimap[x][y] > 0) return(minimap[x][y]);

  har = is_terrain_near_tile(x, y, T_OCEAN);

  sh = SHIELD_WEIGHTING * MORT;
  t = TRADE_WEIGHTING * MORT; /* assuming DEMOCRACY now, much easier! - Syela */

  con = ptile->continent;

/* not worth the computations AFAICT -- Syela
   wrapped anyway in case it comes back -- dwp
  if (improvement_variant(B_PYRAMIDS)==0) {     
    city_list_iterate(pplayer->cities, acity) 
      if (city_got_building(acity, B_PYRAMIDS)) g++;
    city_list_iterate_end;
  }
*/

  memset(taken, 0, sizeof(taken));
  memset(food, 0, sizeof(food));
  memset(shield, 0, sizeof(shield));
  memset(trade, 0, sizeof(trade));
  memset(irrig, 0, sizeof(irrig));
  memset(mine, 0, sizeof(mine));
  memset(road, 0, sizeof(road));

  city_map_iterate(i, j) {
    if ((minimap[map_adjust_x(x+i-2)][map_adjust_y(y+j-2)] >= 0 && !pcity) ||
       (pcity && get_worker_city(pcity, i, j) == C_TILE_EMPTY)) {
      ptile = map_get_tile(x+i-2, y+j-2);
      con2 = ptile->continent;
      ptype = get_tile_type(ptile->terrain);
      food[i][j] = (get_tile_food_base(ptile) - 2) * MORT;
      if (i == 2 && j == 2) food[i][j] += 2 * MORT;
      if (ptype->irrigation_result == ptile->terrain && con2 == con) {
        if (ptile->special&S_IRRIGATION || (i == 2 && j == 2))
	  irrig[i][j] = MORT * ptype->irrigation_food_incr;
        else if (is_water_adjacent_to_tile(x+i-2, y+j-2) &&
                 ptile->terrain != T_HILLS)
	  irrig[i][j] = MORT * ptype->irrigation_food_incr - 9; /* KLUGE */
/* all of these kluges are hardcoded amortize calls to save much CPU use -- Syela */
      } else if (ptile->terrain == T_OCEAN && har) food[i][j] += MORT; /* harbor */
      shield[i][j] = get_tile_shield_base(ptile) * sh;
      if (i == 2 && j == 2 && !shield[i][j]) shield[i][j] = sh;
      if (ptile->terrain == T_OCEAN && har) shield[i][j] += sh;
/* above line is not sufficiently tested.  AI was building on shores, but not
as far out in the ocean as possible, which limits growth in the very long
term (after SEWER).  These cities will all eventually have OFFSHORE, and
I need to acknowledge that.  I probably shouldn't treat it as free, but
that's the easiest, and I doubt pathological behavior will result. -- Syela */
      if (ptile->special&S_MINE)
	mine[i][j] = sh * ptype->mining_shield_incr;
      else if (ptile->terrain == T_HILLS && con2 == con)
	mine[i][j] = sh * ptype->mining_shield_incr - 300; /* KLUGE */
      trade[i][j] = get_tile_trade_base(ptile) * t;
      if (ptype->road_trade_incr > 0) {
        if (ptile->special&S_ROAD || (i == 2 && j == 2))
	  road[i][j] = t * ptype->road_trade_incr;
        else if (con2 == con)
          road[i][j] = t * ptype->road_trade_incr - 70; /* KLUGE */ /* actualy exactly 70 1/2 */
      }
      if (trade[i][j]) trade[i][j] += t;
      else if (road[i][j]) road[i][j] += t;
    }
  }

  if (pcity) { /* quick-n-dirty immigration routine -- Syela */
    n = pcity->size;
    best = 0; ii = 0; jj = 0; /* blame -Wall -Werror for these */
    city_map_iterate(i, j) {
      cur = (food[i][j]) * food_weighting(n) + /* ignoring irrig on purpose */
            (shield[i][j] + mine[i][j]) +
            (trade[i][j] + road[i][j]);
      if (cur > best && (i != 2 || j != 2)) { best = cur; ii = i; jj = j; }
    }
    if (!best) return(0);
    val = (shield[ii][jj] + mine[ii][jj]) +
          (food[ii][jj] + irrig[ii][jj]) * FOOD_WEIGHTING + /* seems to be needed */
          (trade[ii][jj] + road[ii][jj]);
    val -= amortize(40 * SHIELD_WEIGHTING + (50 - 20 * g) * FOOD_WEIGHTING, 12);
    /* 12 is arbitrary; need deterrent to represent loss
       of a settlers -- Syela */
    freelog(LOG_DEBUG, "Desire to immigrate to %s = %d -> %d",
		  pcity->name, val, (val * 100) / MORT / 70);
    return(val);
  }

  f = food[2][2] + irrig[2][2];
  if (!f) return(0); /* no starving cities, thank you! -- Syela */
  val = f * FOOD_WEIGHTING + /* this needs to be here, strange as it seems */
          (shield[2][2] + mine[2][2]) +
          (trade[2][2] + road[2][2]);
  taken[2][2]++;
  /* val is mort times the real value */
  /* treating harbor as free to approximate advantage of
     building boats. -- Syela */
  db = get_tile_type(map_get_terrain(x, y))->defense_bonus;
  if (map_get_special(x, y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  val += (4 * db - 40) * SHIELD_WEIGHTING;
  /* don't build cities in danger!! FIX! -- Syela */
  val += 8 * MORT; /* one science per city */
  
  if (debug) freelog(LOG_DEBUG, "City value (%d, %d) = %d, har = %d, f = %d",
		     x, y, val, har, f);

  for (n = 1; n <= 20 && f > 0; n++) {
    for (a = 1; a; a--) {
      best = 0; worst = -1; b2 = 0; i0 = 0; j0 = 0; ii = 0; jj = 0;
      city_map_iterate(i, j) {
        cur = (food[i][j]) * food_weighting(n) + /* ignoring irrig on purpose */
              (shield[i][j] + mine[i][j]) +
              (trade[i][j] + road[i][j]);
        if (!taken[i][j]) {
          if (cur > best) { b2 = best; best = cur; ii = i; jj = j; }
          else if (cur > b2) b2 = cur;
        } else if (i != 2 || j != 2) {
          if (cur < worst || worst < 0) { worst = cur; i0 = i; j0 = j; }
        }
      }
      if (!best) break;
      cur = amortize((shield[ii][jj] + mine[ii][jj]) +
            (food[ii][jj] + irrig[ii][jj]) * FOOD_WEIGHTING + /* seems to be needed */
            (trade[ii][jj] + road[ii][jj]), d);
      f += food[ii][jj] + irrig[ii][jj];
      if (cur > 0) val += cur;
      taken[ii][jj]++;
      
      if (debug) {
	freelog(LOG_DEBUG,
		"Value of (%d, %d) = %d food = %d, type = %s, n = %d, d = %d",
		ii, jj, cur, (food[ii][jj] + irrig[ii][jj]),
		get_tile_type(map_get_tile(x + ii - 2,
					   y + jj - 2)->terrain)->terrain_name,
		n, d);
      }

/* I hoped to avoid the following, but it seems I can't take ANY shortcuts
in this unspeakable routine, so here comes even more CPU usage in order to
get this EXACTLY right instead of just reasonably close. -- Syela */
      if (worst < b2 && worst >= 0) {
        cur = amortize((shield[i0][j0] + mine[i0][j0]) +
              (trade[i0][j0] + road[i0][j0]), d);
        f -= (food[i0][j0] + irrig[i0][j0]);
        val -= cur;
        taken[i0][j0]--;
        a++;
	
	if (debug) {
	  freelog(LOG_DEBUG, "REJECTING Value of (%d, %d) = %d"
		  " food = %d, type = %s, n = %d, d = %d",
		  i0, j0, cur, (food[i0][j0] + irrig[i0][j0]),
		  get_tile_type(map_get_tile(x + i0 - 2,
					     y + j0 - 2)->terrain)->terrain_name,
		  n, d);
	}
      }
    }
    if (!best) break;
    if (f > 0) d += (game.foodbox * MORT * n + (f*g) - 1) / (f*g);
    if (n == 4) {
      val -= amortize(40 * SHIELD_WEIGHTING + (50 - 20 * g) * FOOD_WEIGHTING, d); /* lers */
      temp = amortize(40 * SHIELD_WEIGHTING, d); /* temple */
      tmp = val;
    }
  } /* end while */
  if (n > 4) {
    if (val - temp > tmp) val -= temp;
    else val = tmp;
  }
  val -= 110 * SHIELD_WEIGHTING; /* WAG: walls, defenders */
  minimap[x][y] = val;

  if (debug) {
    freelog(LOG_DEBUG, "Total value of (%d, %d) [%d workers] = %d",
	    x, y, n, val);
  }
  return(val);
}

/**************************************************************************
...
**************************************************************************/
void ai_manage_settler(struct player *pplayer, struct unit *punit)
{
  punit->ai.control = 1;
  if (punit->ai.ai_role == AIUNIT_NONE) /* if BUILD_CITY must remain BUILD_CITY */
    punit->ai.ai_role = AIUNIT_AUTO_SETTLER;
/* gonna handle city-building in the auto-settler routine -- Syela */
  return;
}

/*************************************************************************
  returns dy according to the wrap rules
**************************************************************************/
int make_dy(int y1, int y2)
{
  int dy=y2-y1;
  if (dy<0) dy=-dy;
  return dy;
}

/*************************************************************************
  returns dx according to the wrap rules
**************************************************************************/
int make_dx(int x1, int x2)
{
  int tmp;
  x1=map_adjust_x(x1);
  x2=map_adjust_x(x2);
  if(x1>x2)
    tmp=x1, x1=x2, x2=tmp;

  return MIN(x2-x1, map.xsize-x2+x1);
}

/**************************************************************************
 return 1 if there is already a unit on this square or one destined for it 
 (via goto)
**************************************************************************/
int is_already_assigned(struct unit *myunit, struct player *pplayer, int x, int y)
{ 
  x=map_adjust_x(x);
  y=map_adjust_y(y);
  if (same_pos(myunit->x, myunit->y, x, y) || 
      same_pos(myunit->goto_dest_x, myunit->goto_dest_y, x, y)) {
/* I'm still not sure this is exactly right -- Syela */
    unit_list_iterate(map_get_tile(x, y)->units, punit)
      if (myunit==punit) continue;
      if (punit->owner!=pplayer->player_no)
        return 1;
      if (unit_flag(punit->type, F_SETTLERS) && unit_flag(myunit->type, F_SETTLERS))
        return 1;
    unit_list_iterate_end;
    return 0;
  }
  return(map_get_tile(x, y)->assigned & (1<<pplayer->player_no));
}

#ifdef UNUSED
int old_is_already_assigned(struct unit *myunit, struct player *pplayer, int x, int y)
{
  x=map_adjust_x(x);
  y=map_adjust_y(y);
#ifdef RIDICULOUS
  if (same_pos(myunit->x, myunit->y, x, y))
    return 0;
#endif
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (myunit==punit) continue;
    if (punit->owner!=pplayer->player_no)
      return 1;
    if (unit_flag(punit->type, F_SETTLERS) && unit_flag(myunit->type, F_SETTLERS))
      return 1;
  }
  unit_list_iterate_end;
#ifndef RIDICULOUS
  if (same_pos(myunit->x, myunit->y, x, y))
    return 0;
#endif
  unit_list_iterate(pplayer->units, punit) {
    if (myunit==punit) continue;
    if (punit->owner!=pplayer->player_no)
      return 1;
    if (same_pos(punit->goto_dest_x, punit->goto_dest_y, x, y) && unit_flag(punit->type, F_SETTLERS) && unit_flag(myunit->type,F_SETTLERS) && punit->activity==ACTIVITY_GOTO)
      return 1;
    if (same_pos(punit->goto_dest_x, punit->goto_dest_y, x, y) && !unit_flag(myunit->type, F_SETTLERS) && punit->activity==ACTIVITY_GOTO)
      return 1;
  }
  unit_list_iterate_end;
  return 0;
}
#endif

/* all of the benefit and ai_calc routines rewritten by Syela */
/* to conform with city_tile_value and related calculations elsewhere */
/* all of these functions are VERY CPU-inefficient and are being cached */
/* I don't really want to rewrite them and possibly screw them up. */
/* The cache should keep the CPU increase linear instead of quadratic. -- Syela */

int ai_calc_pollution(struct city *pcity, struct player *pplayer, int i, int j)
{
  int x, y, m;
  x = pcity->x + i - 2; y = pcity->y + j - 2;
  if (!(map_get_special(x, y) & S_POLLUTION)) return(-1);
  map_clear_special(x, y, S_POLLUTION);
  m = city_tile_value(pcity, i, j, 0, 0);
  map_set_special(x, y, S_POLLUTION);
  return(m);
}

static int is_wet(struct player *pplayer, int x, int y)
{
  enum tile_terrain_type t;
  enum tile_special_type s;

  if (!pplayer->ai.control && !map_get_known(x, y, pplayer)) return 0;

  t=map_get_terrain(x,y);
  if (t == T_OCEAN || t == T_RIVER) return 1;
  s=map_get_special(x,y);
  if ((s & S_RIVER) || (s & S_IRRIGATION)) return 1;
  return 0;
}

int ai_calc_irrigate(struct city *pcity, struct player *pplayer, int i, int j)
{
  int m, x = pcity->x + i - 2, y = pcity->y + j - 2;
  struct tile *ptile = map_get_tile(x, y);
  enum tile_terrain_type t = ptile->terrain;
  struct tile_type *type = get_tile_type(t);
  int s = ptile->special;

  if (ptile->terrain != type->irrigation_result &&
      type->irrigation_result != T_LAST) { /* EXPERIMENTAL 980905 -- Syela */
    ptile->terrain = type->irrigation_result;
    map_clear_special(x, y, S_MINE);
    m = city_tile_value(pcity, i, j, 0, 0);
    ptile->terrain = t;
    ptile->special = s;
    return(m);
  } else if((ptile->terrain==type->irrigation_result &&
     !(ptile->special&S_IRRIGATION) &&
     !(ptile->special&S_MINE) && !(ptile->city) &&
     (is_wet(pplayer,x,y) || is_wet(pplayer,x,y-1) || is_wet(pplayer,x,y+1) ||
     is_wet(pplayer,x-1,y) || is_wet(pplayer,x+1,y)))) {
    map_set_special(x, y, S_IRRIGATION);
    m = city_tile_value(pcity, i, j, 0, 0);
    map_clear_special(x, y, S_IRRIGATION);
    return(m);
  } else if((ptile->terrain==type->irrigation_result &&
     (ptile->special&S_IRRIGATION) && !(ptile->special&S_FARMLAND) &&
     player_knows_techs_with_flag(pplayer, TF_FARMLAND) &&
     !(ptile->special&S_MINE) && !(ptile->city) &&
     (is_wet(pplayer,x,y) || is_wet(pplayer,x,y-1) || is_wet(pplayer,x,y+1) ||
     is_wet(pplayer,x-1,y) || is_wet(pplayer,x+1,y)))) {
    map_set_special(x, y, S_FARMLAND);
    m = city_tile_value(pcity, i, j, 0, 0);
    map_clear_special(x, y, S_FARMLAND);
    return(m);
  } else return(-1);
}

int ai_calc_mine(struct city *pcity, struct player *pplayer, int i, int j)
{
  int m, x = pcity->x + i - 2, y = pcity->y + j - 2;
  struct tile *ptile = map_get_tile(x, y);
#if 0
  enum tile_terrain_type t = ptile->terrain;
  struct tile_type *type = get_tile_type(t);
  int s = ptile->special;

  if (ptile->terrain != type->mining_result &&
      type->mining_result != T_LAST) { /* EXPERIMENTAL 980905 -- Syela */
    ptile->terrain = type->mining_result;
    map_clear_special(x, y, S_FARMLAND);
    map_clear_special(x, y, S_IRRIGATION);
    m = city_tile_value(pcity, i, j, 0, 0);
    ptile->terrain = t;
    ptile->special = s;
    return(m);
  } else 
#endif
  if ((ptile->terrain == T_HILLS || ptile->terrain == T_MOUNTAINS) &&
      !(ptile->special&S_IRRIGATION) && !(ptile->special&S_MINE)) {
    map_set_special(x, y, S_MINE);
    m = city_tile_value(pcity, i, j, 0, 0);
    map_clear_special(x, y, S_MINE);
    return(m);
  } else return(-1);
}

int ai_calc_transform(struct city *pcity, struct player *pplayer, int i, int j)
{
  int m, x = pcity->x + i - 2, y = pcity->y + j - 2;
  struct tile *ptile = map_get_tile(x, y);

  enum tile_terrain_type t = ptile->terrain;
  struct tile_type *type = get_tile_type(t);
  int s = ptile->special;
  enum tile_terrain_type r = type->transform_result;
  
  if ((t == T_ARCTIC || t == T_DESERT || t == T_JUNGLE || t == T_SWAMP  || 
       t == T_TUNDRA || t == T_MOUNTAINS) && r != T_LAST) {
    ptile->terrain = r;

    if (get_tile_type(r)->mining_result != r) 
      map_clear_special(x, y, S_MINE);

    if (get_tile_type(r)->irrigation_result != r) {
      map_clear_special(x, y, S_FARMLAND);
      map_clear_special(x, y, S_IRRIGATION);
    }

    m = city_tile_value(pcity, i, j, 0, 0);
    ptile->terrain = t;
    ptile->special = s;
    return(m);
  } else return(-1);
}

static int road_bonus(int x, int y, int spc)
{
  int m = 0, k;
  int rd[12], te[12];
  int ii[12] = { -1, 0, 1, -1, 1, -1, 0, 1, 0, -2, 2, 0 };
  int jj[12] = { -1, -1, -1, 0, 0, 1, 1, 1, -2, 0, 0, 2 };
  struct tile *ptile;
  for (k = 0; k < 12; k++) {
    ptile = map_get_tile(x + ii[k], y + jj[k]);
    rd[k] = ptile->special&spc;
    te[k] = (ptile->terrain == T_MOUNTAINS || ptile->terrain == T_OCEAN);
    if (!rd[k]) {
      unit_list_iterate(ptile->units, punit)
        if (punit->activity == ACTIVITY_ROAD || punit->activity == ACTIVITY_RAILROAD)
          rd[k] = spc;
      unit_list_iterate_end;
    }
  }

  if (rd[0] && !rd[1] && !rd[3] && (!rd[2] || !rd[8]) &&
      (!te[2] || !te[4] || !te[7] || !te[6] || !te[5])) m++;
  if (rd[2] && !rd[1] && !rd[4] && (!rd[7] || !rd[10]) &&
      (!te[0] || !te[3] || !te[7] || !te[6] || !te[5])) m++;
  if (rd[5] && !rd[6] && !rd[3] && (!rd[5] || !rd[11]) &&
      (!te[2] || !te[4] || !te[7] || !te[1] || !te[0])) m++;
  if (rd[7] && !rd[6] && !rd[4] && (!rd[0] || !rd[9]) &&
      (!te[2] || !te[3] || !te[0] || !te[1] || !te[5])) m++;

  if (rd[1] && !rd[4] && !rd[3] && (!te[5] || !te[6] || !te[7])) m++;
  if (rd[3] && !rd[1] && !rd[6] && (!te[2] || !te[4] || !te[7])) m++;
  if (rd[4] && !rd[1] && !rd[6] && (!te[0] || !te[3] || !te[5])) m++;
  if (rd[6] && !rd[4] && !rd[3] && (!te[0] || !te[1] || !te[2])) m++;
  return(m);
}

int ai_calc_road(struct city *pcity, struct player *pplayer, int i, int j)
{
  int x, y, m;
  struct tile *ptile;
  x = pcity->x + i - 2; y = pcity->y + j - 2;
  ptile = map_get_tile(x, y);
  if (ptile->terrain != T_OCEAN &&
      (((ptile->terrain != T_RIVER) && !(ptile->special&S_RIVER)) ||
       player_knows_techs_with_flag(pplayer, TF_BRIDGE)) &&
      !(ptile->special&S_ROAD)) {
    ptile->special|=S_ROAD; /* have to do this to avoid reset_move_costs -- Syela */
    m = city_tile_value(pcity, i, j, 0, 0);
    ptile->special&=~S_ROAD;
    return(m);
  } else return(-1);
}

int ai_calc_railroad(struct city *pcity, struct player *pplayer, int i, int j)
{
  int x, y, m;
  struct tile *ptile;
  enum tile_special_type spe_sav;
  
  x = pcity->x + i - 2; y = pcity->y + j - 2;
  ptile = map_get_tile(x, y);
  if (ptile->terrain != T_OCEAN &&
      player_knows_techs_with_flag(pplayer, TF_RAILROAD) &&
      !(ptile->special&S_RAILROAD)) {
    spe_sav = ptile->special;
    ptile->special|=(S_ROAD | S_RAILROAD);
    m = city_tile_value(pcity, i, j, 0, 0);
    ptile->special = spe_sav;
    return(m);
  } else return(-1);
  /* bonuses for adjacent railroad tiles */
}

/*************************************************************************
  return how good this square is for a new city.
**************************************************************************/
int is_ok_city_spot(int x, int y)
{
  int dx, dy, i;

  switch (map_get_terrain(x,y)) {
  case T_OCEAN:
  case T_UNKNOWN:
  case T_MOUNTAINS:
  case T_FOREST:
  case T_HILLS:
  case T_ARCTIC:
  case T_JUNGLE:
  case T_SWAMP:
  case T_TUNDRA:
  case T_LAST:
    return 0;
  case T_DESERT:
    if
    (
     !((map_get_tile(x, y))->special&S_SPECIAL_1)
    &&
     !((map_get_tile(x, y))->special&S_SPECIAL_2)
    )
      return 0;
  case T_GRASSLAND:
  case T_PLAINS:
  case T_RIVER:
    break;
  default:
    break;
  }
  for (i = 0; i < game.nplayers; i++) {
    city_list_iterate(game.players[i].cities, pcity) {
      if (map_distance(x, y, pcity->x, pcity->y)<=8) {
        dx=make_dx(pcity->x, x);
        dy=make_dy(pcity->y, y);
        if (dx<=5 && dy<5)
          return 0;
        if (dx<5 && dy<=5)
          return 0;
      }
    }
    city_list_iterate_end;
  }
  return 1;
}

/*************************************************************************
  return the city if any that is in range of this square.
**************************************************************************/
int in_city_radius(int x, int y)
{
  int i, j;
  city_map_iterate(i, j) {
    if (map_get_city(x+i-2, y+j-2)) return 1;
  }
  return 0;
}

/**************************************************************************
  simply puts the settler unit into goto
**************************************************************************/
int auto_settler_do_goto(struct player *pplayer, struct unit *punit, int x, int y)
{
  punit->goto_dest_x=map_adjust_x(x);
  punit->goto_dest_y=map_adjust_y(y);
  set_unit_activity(punit, ACTIVITY_GOTO);
  send_unit_info(0, punit, 0);
  do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);
  return 1;
}

int find_boat(struct player *pplayer, int *x, int *y, int cap)
{ /* this function uses the current warmap, whatever it may hold */
/* unit is no longer an arg!  we just trust the map! -- Syela */
  int best = 22, id = 0; /* arbitrary maximum distance, I will admit! */
  unit_list_iterate(pplayer->units, aunit)
    if (is_ground_units_transport(aunit)) {
      if (warmap.cost[aunit->x][aunit->y] < best &&
          (warmap.cost[aunit->x][aunit->y] == 0 ||
          is_transporter_with_free_space(pplayer, aunit->x, aunit->y) >= cap)) {
        id = aunit->id;
        best = warmap.cost[aunit->x][aunit->y];
        *x = aunit->x;
        *y = aunit->y;
      }
    }
  unit_list_iterate_end;
  if (id) return(id);
#ifdef ALLOW_VIRTUAL_BOATS
  city_list_iterate(pplayer->cities, pcity)
    if (pcity->is_building_unit &&
        unit_types[pcity->currently_building].transport_capacity &&
        !unit_flag(pcity->currently_building, F_CARRIER) &&
	!unit_flag(pcity->currently_building, F_SUBMARINE)) {
      if (warmap.cost[pcity->x][pcity->y] < best) {
        id = pcity->id;
        best = warmap.cost[pcity->x][pcity->y];
        *x = pcity->x;
        *y = pcity->y;
      }
    }
  city_list_iterate_end;
#endif
  return(id);
}

struct unit *other_passengers(struct unit *punit)
{
  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, aunit)
    if (is_ground_unit(aunit) && aunit != punit) return aunit;
  unit_list_iterate_end;
  return 0;
}

/********************************************************************
  find some work for the settler
*********************************************************************/
int auto_settler_findwork(struct player *pplayer, struct unit *punit)
{
  struct city *mycity = map_get_city(punit->x, punit->y);
  int nav_known       = (get_invention(pplayer, game.rtech.nav) == TECH_KNOWN);
  int ucont           = map_get_continent(punit->x, punit->y);
  int mv_rate         = unit_types[punit->type].move_rate;
  int player_num      = pplayer->player_no;
  int save_id         = punit->id;              /* in case unit dies */
  
  int gx,gy;			/* x,y of target (goto) square */
  int mv_turns;			/* estimated turns to move to target square */
  int oldv;			/* current value of consideration tile */
  int newv;			/* upgraded value of consideration tile */
  int best_newv = 0;		/* newv of best target so far, all cities */
  int best_oldv = 9999;		/* oldv of best target so far; compared if
				   newv==best_newv; not initialized to zero,
				   so that newv=0 activities are not chosen */
  int best_act = 0;		/* ACTIVITY_ of best target so far */
  int food_upkeep;		/* upkeep food value for single settler  */
  int food_cost;		/* estimated food cost to produce settler */
  
  int boatid, bx, by;		/* as returned by find_boat */
  struct unit *ferryboat;	/* if non-null, boatid boat at unit's x,y */
  
  int x, y, i, j;
  int a, b=0, d=0;
  int worst;
  int save_newv;		/* for debugging only */
  
  struct ai_choice choice;	/* for nav want only */

  choice.type = 1;
  choice.want = 0;		/* will change as needed */
  choice.choice = get_role_unit(L_FERRYBOAT, -1);
  /* was U_CARAVEL (first non-trireme boat?) but gets set
   * properly later anyway */

  if (punit->id) food_cost = 30;
  else {
    if (mycity->size == 1) food_cost = 20;
    else food_cost = 40 * (mycity->size - 1) / mycity->size;
    if (city_got_effect(mycity, B_GRANARY)) food_cost -= 20;
  }
  food_upkeep = utype_food_cost(get_unit_type(punit->type),
				get_gov_pplayer(pplayer));
  if (punit->id && !punit->homecity) food_upkeep = 0; /* thanks, Peter */

  /** First find the best square to upgrade,
   ** results in: gx, gy, best_oldv, best_newv, best_act */
  
  gx=-1;
  gy=-1;
  /* iterating over the whole map is just ridiculous.  let's only look at
     our own cities.  The old method wasted billions of CPU cycles and led to
     AI settlers improving enemy cities.  arguably should include city_spot  */
  generate_warmap(mycity, punit);
  city_list_iterate(pplayer->cities, pcity)
    worst = worst_worker_tile_value(pcity);
    /* try to work near the city */
    city_map_iterate_outwards(i, j)
      if (get_worker_city(pcity, i, j) == C_TILE_UNAVAILABLE) continue;
      x = map_adjust_x(pcity->x + i - 2);
      y = pcity->y + j - 2;	/* No adjust! */
      if ((y >= 0 && y < map.ysize)
	  && map_get_continent(x, y) == ucont
	  && warmap.cost[x][y] <= THRESHOLD * mv_rate
	  && (territory[x][y]&(1<<player_num))
				/* pretty good, hope it's enough! -- Syela */
          && !is_already_assigned(punit, pplayer, x, y)) {
	/* calling is_already_assigned once instead of four times
	   for obvious reasons;  structure is much the same as it once
	   was but subroutines are not -- Syela	*/
	mv_turns = (warmap.cost[x][y]) / mv_rate;
        oldv = city_tile_value(pcity, i, j, 0, 0);
        if (worst > oldv && (i != 2 || j != 2)) oldv = worst;
	/* perhaps I should just subtract worst rather than oldv but
	   I prefer this -- Syela */

/* Christopher Neufeld provided a rather crufty patch to make settlers
needlessly idle less often.  I didn't want to apply it, but the ideas
for the -1 return values in ai_calc_*, the !irrigation in calc_mine,
and the prioritization of useless (b <= 0) activities are his. -- Syela
*/

	newv = pcity->ai.irrigate[i][j];
        if (newv >= oldv) { /* worth evaluating */
          b = MAX((newv - oldv)*64, MORT);
          d = (map_build_irrigation_time(x, y) * 3 + mv_rate - 1) / mv_rate
	    + mv_turns;
          a = amortize(b, d);
          newv = ((a * b) / (MAX(1, b - a)))/64;
        } else newv = 0;
        if ((newv > best_newv || (newv == best_newv && oldv > best_oldv))
	    && ai_fuzzy(pplayer, 1)) {
	  freelog(LOG_DEBUG,
		  "Replacing (%d, %d) = %d with (%d, %d) I=%d d=%d, b=%d",
		  gx, gy, best_newv, x, y, newv, d, b);
	  best_act = ACTIVITY_IRRIGATE;
	  best_newv=newv; gx=x; gy=y; best_oldv=oldv;
        }

	newv = pcity->ai.transform[i][j];
        if (newv >= oldv) { /* worth evaluating */
          b = MAX((newv - oldv)*64, MORT);
          d = (map_transform_time(x, y) * 3 + mv_rate - 1) / mv_rate
	    + mv_turns;
          a = amortize(b, d);
          newv = ((a * b) / (MAX(1, b - a)))/64;
        } else newv = 0;
        if ((newv > best_newv || (newv == best_newv && oldv > best_oldv))
	    && ai_fuzzy(pplayer, 1)) {
	  freelog(LOG_DEBUG,
		  "Replacing (%d, %d) = %d with (%d, %d) I=%d d=%d, b=%d",
		  gx, gy, best_newv, x, y, newv, d, b);
	  best_act = ACTIVITY_TRANSFORM;
	  best_newv=newv; gx=x; gy=y; best_oldv=oldv;
        }

	newv = pcity->ai.mine[i][j];
        if (newv >= oldv) {
          b = MAX((newv - oldv)*64, MORT);
          d = (map_build_mine_time(x, y) * 3 + mv_rate - 1) / mv_rate
	    + mv_turns;
          a = amortize(b, d);
          newv = ((a * b) / (MAX(1, b - a)))/64;
        } else newv = 0;
	if ((newv > best_newv || (newv == best_newv && oldv > best_oldv))
	    && ai_fuzzy(pplayer, 1)) {
	  freelog(LOG_DEBUG,
		  "Replacing (%d, %d) = %d with (%d, %d) M=%d d=%d, b=%d",
		  gx, gy, best_newv, x, y, newv, d, b);
	  best_act = ACTIVITY_MINE;
	  best_newv=newv; gx=x; gy=y; best_oldv=oldv;
        }

        if (!(map_get_tile(x,y)->special&S_ROAD)) {
  	  newv = pcity->ai.road[i][j];
          if (newv >= 0) {    
            newv = MAX(newv, oldv) + road_bonus(x, y, S_ROAD) * 8;
	    /* guessing about the weighting based on unit upkeeps;
	       (* 11) was too high! -- Syela */
            b = MAX((newv - oldv)*64, MORT);
            d = (map_build_road_time(x, y) * 3 + 3 + mv_rate - 1) / mv_rate
	      + mv_turns; /* uniquely weird! */
            a = amortize(b, d);
            newv = ((a * b) / (MAX(1, b - a)))/64;
          } else newv = 0;
          if ((newv > best_newv || (newv == best_newv && oldv > best_oldv))
	      && ai_fuzzy(pplayer, 1)) {
	    freelog(LOG_DEBUG, "Replacing (%d, %d) = %d"
		    " with (%d, %d) R=%d d=%d, b=%d",
		    gx, gy, best_newv, x, y, newv, d, b);
	    best_act = ACTIVITY_ROAD;
	    best_newv=newv; gx=x; gy=y; best_oldv=oldv;
          }

	  newv = pcity->ai.railroad[i][j];
          if (newv >= 0) {    
            newv = MAX(newv, oldv) + road_bonus(x, y, S_RAILROAD) * 4;
            b = MAX((newv - oldv)*64, MORT);
            d = (3 * 3 + 3 * map_build_road_time(x,y) + 3 + mv_rate - 1)
	         / mv_rate + mv_turns;
            a = amortize(b, d);
            newv = ((a * b) / (MAX(1, b - a)))/64;
          } else newv = 0;
          if ((newv > best_newv || (newv == best_newv && oldv > best_oldv))
	      && ai_fuzzy(pplayer, 1)) {
  	    best_act = ACTIVITY_ROAD;
	    best_newv=newv; gx=x; gy=y; best_oldv=oldv;
          }
        } else {
	  newv = pcity->ai.railroad[i][j];
          if (newv >= 0) {    
            newv = MAX(newv, oldv) + road_bonus(x, y, S_RAILROAD) * 4;
            b = MAX((newv - oldv)*64, MORT);
            d = (3 * 3 + mv_rate - 1) / mv_rate + mv_turns;
            a = amortize(b, d);
            newv = ((a * b) / (MAX(1, b - a)))/64;
          } else newv = 0;
          if ((newv > best_newv || (newv == best_newv && oldv > best_oldv))
	      && ai_fuzzy(pplayer, 1)) {
  	    best_act = ACTIVITY_RAILROAD;
	    best_newv=newv; gx=x; gy=y; best_oldv=oldv;
          }
        } /* end S_ROAD else */

	newv = pcity->ai.detox[i][j];
        if (newv >= 0) {
          newv = MAX(newv, oldv) + pplayer->ai.warmth;
          b = MAX((newv - oldv)*64, MORT);
          d = (3 * 3 + mv_turns + mv_rate - 1) / mv_rate + mv_turns;
          a = amortize(b, d);
          newv = ((a * b) / (MAX(1, b - a)))/64;
        } else newv = 0;
        if (newv > best_newv || (newv == best_newv && oldv > best_oldv)) {
	  best_act = ACTIVITY_POLLUTION;
	  best_newv=newv; gx=x; gy=y; best_oldv=oldv;
        }
      } /* end if we are a legal destination */
    city_map_iterate_outwards_end;
  city_list_iterate_end;

  /** Found the best square to upgrade, have gx, gy, best_newv, best_act **/

  best_newv = (best_newv - food_upkeep * FOOD_WEIGHTING) * 100 /
                                                     (40 + food_cost);
  if (best_newv < 0)
    best_newv = 0; /* Bad Things happen without this line! :( -- Syela */

  if (best_newv > 0) {
    freelog(LOG_DEBUG,
	    "Settler %d@(%d,%d) wants to %d at (%d,%d) with desire %d",
	    punit->id, punit->x, punit->y, best_act, gx, gy, best_newv);
  }
  save_newv = best_newv;
  
  boatid = find_boat(pplayer, &bx, &by, 1); /* might need 2 for body */
  ferryboat = unit_list_find(&(map_get_tile(punit->x, punit->y)->units), boatid);
  if (ferryboat)
    really_generate_warmap(mycity, ferryboat, SEA_MOVING);

  /** Decide whether to build a new city:
   ** if so, modify: gx, gy, best_newv, best_act */

  if (unit_flag(punit->type, F_CITIES) &&
      pplayer->ai.control &&
      ai_fuzzy(pplayer,1)) {    /* don't want to make cities otherwise */
    if (punit->ai.ai_role == AIUNIT_BUILD_CITY) {
      remove_city_from_minimap(punit->goto_dest_x, punit->goto_dest_y);
    }
    punit->ai.ai_role = AIUNIT_AUTO_SETTLER; /* here and not before! -- Syela */
    for (j = -11; j <= 11; j++) { /* hope this is far enough -- Syela */
      y = punit->y + j;
      if (y < 0 || y >= map.ysize) continue;
      for (i = -11; i <= 11; i++) {
        int near = (MAX((MAX(i, -i)),(MAX(j, -j))));
        int w_virtual = 0;	/* I'm no entirely sure what this is --dwp */
	int mv_cost;
        x = map_adjust_x(punit->x + i);
        if (!is_already_assigned(punit, pplayer, x, y)
	    && map_get_terrain(x, y) != T_OCEAN
	    && (territory[x][y]&(1<<player_num))
				/* pretty good, hope it's enough! -- Syela */
            && (near < 8 || map_get_continent(x, y) != ucont)) {

	  /* potential target, calculate mv_cost: */
          if (ferryboat) {
	    /* already aboard ship, can use real warmap */
            if (!is_terrain_near_tile(x, y, T_OCEAN)) {
	      mv_cost = 9999;
	    } else {
	      mv_cost = warmap.seacost[x][y] * mv_rate /
		    unit_types[ferryboat->type].move_rate;
	    }
          } else if (!goto_is_sane(pplayer, punit, x, y, 1) ||
		     warmap.cost[x][y] > THRESHOLD * mv_rate) {
				/* for Rome->Carthage */
	    if (!is_terrain_near_tile(x, y, T_OCEAN)) {
	      mv_cost = 9999;
	    } else if (boatid) {
              if (!punit->id && mycity->id == boatid) w_virtual = 1;
              mv_cost = warmap.cost[bx][by] + real_map_distance(bx, by, x, y)
		   + mv_rate; 
            } else if (punit->id ||
		       !is_terrain_near_tile(mycity->x, mycity->y, T_OCEAN)) {
	      mv_cost = 9999;
	    } else {
              mv_cost = warmap.seacost[x][y] * mv_rate / 9;
	      /* this should be fresh; the only thing that could have
		 munged the seacost is the ferryboat code in
		 k_s_w/f_s_t_k, but only if find_boat succeeded */
              w_virtual = 1;
            }
          } else {
	    mv_cost = warmap.cost[x][y];
	  }
          d = mv_cost / mv_rate;
	  /* without this, the computer will go 6-7 tiles from X to
	     build a city at Y */
          d *= 2;
	  /* and then build its NEXT city halfway between X and Y. -- Syela */
          b = city_desirability(pplayer, x, y) * ai_fuzzy(pplayer,1);
          newv = amortize(b, d);
          
          b = (food_upkeep * FOOD_WEIGHTING) * MORT;
          if (map_get_continent(x, y) != ucont) b += SHIELD_WEIGHTING * MORT;
          newv -= (b - amortize(b, d));
	  /* deal with danger Real Soon Now! -- Syela */
	  /* newv is now the value over mort turns */
          newv = (newv * 100) / MORT / ((w_virtual ? 80 : 40) + food_cost);

          if (best_newv && map_get_city(x, y)) newv = 0;
	  
/* I added a line to discourage settling in existing cities, but it was
inadequate.  It may be true that in the short-term building infrastructure
on tiles that won't be worked for ages is not as useful as helping other
cities grow and then building engineers later, but this is short-sighted.
There was also a problem with workers suddenly coming onto unimproved tiles,
either through city growth or de-elvisization, settlers being built and
improving those tiles, and then immigrating shortly thereafter. -- Syela
*/
 	  if (newv>0 && pplayer->ai.expand!=100) {
 	    newv = (newv * pplayer->ai.expand) / 100;
 	  }
 
	  if (w_virtual) {
	    freelog(LOG_DEBUG, "%s: best_newv = %d, w_virtual = 1, newv = %d",
		    mycity->name, best_newv, newv);
	  }
	  
          if (map_get_continent(x, y) != ucont && !nav_known && near >= 8) {
            if (newv > choice.want && !punit->id) choice.want = newv;
	    freelog(LOG_DEBUG,
		    "%s@(%d, %d) city_des (%d, %d) = %d, newv = %d, d = %d", 
		    (punit->id ? unit_types[punit->type].name : mycity->name), 
		    punit->x, punit->y, x, y, b, newv, d);
          } else if (newv > best_newv) {
	    best_act = ACTIVITY_UNKNOWN; /* flag value */
	    best_newv = newv;
            if (w_virtual) {
              gx = -1; gy = -1;
            } else {
	      gx = x; gy = y;
            }
          }
        }
      }
    }
  }

  choice.want -= best_newv;
  if (choice.want > 0) ai_choose_ferryboat(pplayer, mycity, &choice);

#ifdef DEBUG
  if (best_newv != save_newv) {
    freelog(LOG_DEBUG,
	    "Settler %d@(%d,%d) wants to %d at (%d,%d) with desire %d",
	    punit->id, punit->x, punit->y, best_act, gx, gy, best_newv);
  }
  if (map_get_terrain(punit->x, punit->y) == T_OCEAN) {
    freelog(LOG_DEBUG, "Punit %d@(%d,%d) wants to %d at (%d,%d) with desire %d",
	    punit->id, punit->x, punit->y, best_act, gx, gy, best_newv);
  }
#endif
  
  /* I had the return here, but it led to stupidity where several engineers
     would be built to solve one problem.  Moving the return down will solve
     this. -- Syela */

  if (gx!=-1 && gy!=-1) {
      map_get_tile(gx, gy)->assigned =
        map_get_tile(gx, gy)->assigned | 1<<pplayer->player_no;
  } else if (pplayer->ai.control) { /* settler has no purpose */
    /* possibly add Gilligan's Island here */
    ;
  }

  /** If this is a virtual unit for assessing settler want: **/
  if (!punit->id) {
    /* has to be before we try to send_unit_info()! -- Syela */
    freelog(LOG_DEBUG,
	    "%s (%d, %d) settler-want = %d, task = %d, target = (%d, %d)",
	    mycity->name, mycity->x, mycity->y, best_newv,
	    best_act, gx, gy);
    if (gx < 0 && gy < 0) {
      return(0 - best_newv);
    } else {
      return(best_newv);
    }
  }

  /** We've now worked out what to do; go to it! **/
  if (gx!=-1 && gy!=-1) {
    if (unit_flag(punit->type, F_CITIES) &&
	(best_act == ACTIVITY_UNKNOWN /* flag */)) {
      punit->ai.ai_role = AIUNIT_BUILD_CITY;
      add_city_to_minimap(gx, gy);
    } else {
      punit->ai.ai_role = AIUNIT_AUTO_SETTLER;
    }
    if (!same_pos(gx, gy, punit->x, punit->y)) {
      if (!goto_is_sane(pplayer, punit, gx, gy, 1)
	  || (ferryboat
	      && goto_is_sane(pplayer, ferryboat, gx, gy, 1)
	      && (!is_tiles_adjacent(punit->x, punit->y, gx, gy)
		 || !could_unit_move_to_tile(punit, punit->x, punit->y,
					     gx, gy)))) {
        punit->ai.ferryboat = find_boat(pplayer, &x, &y, 1); /* might need 2 */
	freelog(LOG_DEBUG, "%d@(%d, %d): Looking for BOAT.",
		      punit->id, punit->x, punit->y);
        if (!same_pos(x, y, punit->x, punit->y)) {
          auto_settler_do_goto(pplayer, punit, x, y);
          if (!unit_list_find(&pplayer->units, save_id)) return(0); /* died */
        }
        ferryboat = unit_list_find(&(map_get_tile(punit->x, punit->y)->units),
				   punit->ai.ferryboat);
        punit->goto_dest_x = gx;
        punit->goto_dest_y = gy;
        if (ferryboat && (!ferryboat->ai.passenger
			  || ferryboat->ai.passenger == punit->id)) {
	  freelog(LOG_DEBUG,
		  "We have FOUND BOAT, %d ABOARD %d@(%d,%d)->(%d,%d).",
		  punit->id, ferryboat->id, punit->x, punit->y, gx, gy);
          set_unit_activity(punit, ACTIVITY_SENTRY);
				/* kinda cheating -- Syela */
          ferryboat->ai.passenger = punit->id;
          auto_settler_do_goto(pplayer, ferryboat, gx, gy);
          set_unit_activity(punit, ACTIVITY_IDLE);
        } /* need to zero pass & ferryboat at some point. */
      }
      if (goto_is_sane(pplayer, punit, gx, gy, 1)
	  && punit->moves_left
	  && ((!ferryboat)
	      || (is_tiles_adjacent(punit->x, punit->y, gx, gy)
		  && could_unit_move_to_tile(punit, punit->x, punit->y,
					     gx, gy)))) {
        auto_settler_do_goto(pplayer, punit, gx, gy);
        if (!unit_list_find(&pplayer->units, save_id)) return(0); /* died */
        punit->ai.ferryboat = 0;
      }
    }
  } else punit->ai.control=0;

  if (punit->ai.control
      && punit->moves_left
      && punit->activity == ACTIVITY_IDLE) {
    /* if players can build with 0 moves left, so should the AI */
    if (same_pos(gx, gy, punit->x, punit->y)) {
      if (best_act == ACTIVITY_UNKNOWN) {
        remove_city_from_minimap(gx, gy); /* yeah, I know. -- Syela */
        ai_do_build_city(pplayer, punit);
        return(0);
      }
      set_unit_activity(punit, best_act);
      send_unit_info(0, punit, 0);
      return(0);
    }
  }
  return(0);
}

void initialize_infrastructure_cache(struct city *pcity)
{
  int i, j;
  struct player *pplayer = &game.players[pcity->owner];
  city_map_iterate(i, j) {
    pcity->ai.detox[i][j] = ai_calc_pollution(pcity, pplayer, i, j);
    pcity->ai.mine[i][j] = ai_calc_mine(pcity, pplayer, i, j);
    pcity->ai.irrigate[i][j] = ai_calc_irrigate(pcity, pplayer, i, j);
    pcity->ai.transform[i][j] = ai_calc_transform(pcity, pplayer, i, j);
    pcity->ai.road[i][j] = ai_calc_road(pcity, pplayer, i, j);
/* gonna handle road_bo dynamically for now since it can change
as punits arrive at adjacent tiles and start laying road -- Syela */
    pcity->ai.railroad[i][j] = ai_calc_railroad(pcity, pplayer, i, j);
  }
}

/************************************************************************** 
  run through all the players settlers and let those on ai.control work 
  automagically
**************************************************************************/
void auto_settlers_player(struct player *pplayer) 
{
  static struct timer *t = NULL;      /* alloc once, never free */

  t = renew_timer_start(t, TIMER_CPU, TIMER_DEBUG);

  city_list_iterate(pplayer->cities, pcity)
    initialize_infrastructure_cache(pcity); /* saves oodles of time -- Syela */
  city_list_iterate_end;
  pplayer->ai.warmth = WARMING_FACTOR * total_player_citizens(pplayer) * 10 *
                       (game.globalwarming + game.heating) / (map.xsize *
                        map.ysize * map.landpercent * 2); /* threat of warming */
  freelog(LOG_DEBUG, "Warmth = %d, game.globalwarming=%d",
	  pplayer->ai.warmth, game.globalwarming);
  unit_list_iterate(pplayer->units, punit) {
    if (punit->ai.control && unit_flag(punit->type, F_SETTLERS)) {
      freelog(LOG_DEBUG, "%s's settler at (%d, %d) is ai controlled.",
	      pplayer->name, punit->x, punit->y); 
      if(punit->activity == ACTIVITY_SENTRY)
	set_unit_activity(punit, ACTIVITY_IDLE);
      if (punit->activity == ACTIVITY_GOTO && punit->moves_left)
        set_unit_activity(punit, ACTIVITY_IDLE);
      if (punit->activity == ACTIVITY_IDLE)
	auto_settler_findwork(pplayer, punit);
      freelog(LOG_DEBUG, "Has been processed.");
    }
  }
  unit_list_iterate_end;
  if (timer_in_use(t)) {
    freelog(LOG_VERBOSE, "%s's autosettlers consumed %g milliseconds.",
 	    pplayer->name, 1000.0*read_timer_seconds(t));
  }
}

static void assign_settlers_player(struct player *pplayer)
{
  int i = 1<<pplayer->player_no;
  struct tile *ptile;
  unit_list_iterate(pplayer->units, punit)
    if (unit_flag(punit->type, F_SETTLERS)) {
      if (punit->activity == ACTIVITY_GOTO) {
        ptile = map_get_tile(punit->goto_dest_x, punit->goto_dest_y);
        ptile->assigned = ptile->assigned | i; /* assigned for us only */
      } else {
        ptile = map_get_tile(punit->x, punit->y);
        ptile->assigned = 32767; /* assigned for everyone */
      }
    } else {
      ptile = map_get_tile(punit->x, punit->y);
      ptile->assigned = ptile->assigned | (32767 ^ i); /* assigned for everyone else */
    }
  unit_list_iterate_end;
}

static void assign_settlers(void)
{
  int i, x, y;
  for (x = 0; x < map.xsize; x++)
    for (y = 0; y < map.ysize; y++)
      map_get_tile(x, y)->assigned = 0;

  for (i = 0; i < game.nplayers; i++) {
    assign_settlers_player(&game.players[i]);
  }
}

static void assign_region(int x, int y, int n, int d, int s)
{
  int i, j;
  for (j = MAX(0, y - d); j <= MIN(map.ysize - 1, y + d); j++) {
    for (i = x - d; i <= x + d; i++) {
      if (!s || is_terrain_near_tile(i, j, T_OCEAN))
        territory[map_adjust_x(i)][j] &= (1<<n);
    }
  }
}

static void assign_territory_player(struct player *pplayer)
{
  int n = pplayer->player_no;
  unit_list_iterate(pplayer->units, punit)
    if (unit_types[punit->type].attack_strength) {
/* I could argue that phalanxes aren't really a threat, but ... */
      if (is_sailing_unit(punit)) {
        assign_region(punit->x, punit->y, n, 1 + unit_types[punit->type].move_rate / 3, 1);
      } else if (is_ground_unit(punit)) {
        assign_region(punit->x, punit->y, n, 1 + unit_types[punit->type].move_rate /
             (unit_flag(punit->type, F_IGTER) ? 1 : 3), 0);
/* I realize this is not the most accurate, but I don't want to iterate
road networks 100 times/turn, and I can't justifiably abort when I encounter
already assigned territory.  If anyone has a reasonable alternative that won't
noticeably slow the game, feel free to replace this else{}  -- Syela */
      } else {
        assign_region(punit->x, punit->y, n, 1 + unit_types[punit->type].move_rate / 3, 0);
      } 
    }
  unit_list_iterate_end;
  city_list_iterate(pplayer->cities, pcity)
    assign_region(pcity->x, pcity->y, n, 3, 0);
  city_list_iterate_end;
}

static void assign_territory(void)
{ /* this funct is supposed to keep settlers out of enemy territory -- Syela */
  int i, x, y;
  for (x = 0; x < map.xsize; x++)
    for (y = 0; y < map.ysize; y++)
      territory[x][y] = 65535;

  for (i = 0; i < game.nplayers; i++) {
    assign_territory_player(&game.players[i]);
  }
/* an actual territorial assessment a la AI algorithms for go might be
appropriate here.  I'm not sure it's necessary, so it's not here yet. -- Syela */
}  

/**************************************************************************
  Do the auto_settler stuff for all the players. 
**************************************************************************/
void auto_settlers(void)
{
  int i;
  assign_settlers();
  assign_territory();
  for (i = 0; i < game.nplayers; i++) {
    auto_settlers_player(&game.players[i]);
  }
}

void contemplate_settling(struct player *pplayer, struct city *pcity)
{
  struct unit virtualunit;
  int want;

/* used to use old crappy formulas for settler want, but now using actual want! */

  memset(&virtualunit, 0, sizeof(struct unit));
  virtualunit.id = 0;
  virtualunit.owner = pplayer->player_no;
  virtualunit.x = pcity->x;
  virtualunit.y = pcity->y;
  virtualunit.type = best_role_unit(pcity, F_SETTLERS);
  virtualunit.moves_left = unit_types[virtualunit.type].move_rate;
  virtualunit.hp = unit_types[virtualunit.type].hp;  
  want = auto_settler_findwork(pplayer, &virtualunit);
  unit_list_iterate(pplayer->units, qpass)
    if (qpass->ai.ferryboat == pcity->id) want = -199;
  unit_list_iterate_end;
  pcity->ai.settler_want = want;

  if (unit_flag(virtualunit.type, F_CITIES)) {
    pcity->ai.founder_want = pcity->ai.settler_want;
  } else {
    memset(&virtualunit, 0, sizeof(struct unit));
    virtualunit.id = 0;
    virtualunit.owner = pplayer->player_no;
    virtualunit.x = pcity->x;
    virtualunit.y = pcity->y;
    virtualunit.type = best_role_unit(pcity, F_CITIES);
    virtualunit.moves_left = unit_types[virtualunit.type].move_rate;
    virtualunit.hp = unit_types[virtualunit.type].hp;  
    want = auto_settler_findwork(pplayer, &virtualunit);
    unit_list_iterate(pplayer->units, qpass)
      if (qpass->ai.ferryboat == pcity->id) want = -199;
    unit_list_iterate_end;
    pcity->ai.founder_want = want;
  }
}
