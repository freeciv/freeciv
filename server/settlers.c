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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "city.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "support.h"
#include "timing.h"

#include "citytools.h"
#include "gotohand.h"
#include "maphand.h"
#include "plrhand.h"
#include "unithand.h"
#include "unittools.h"

#include "aicity.h"
#include "aidata.h"
#include "ailog.h"
#include "aitools.h"
#include "aiunit.h"

#include "settlers.h"

/* negative: in_city_radius, 0: unassigned, positive: city_des */
signed int *minimap;

BV_DEFINE(nearness, MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
static nearness *territory;
#define TERRITORY(map_x, map_y) territory[map_pos_to_index(map_x, map_y)]

BV_DEFINE(enemy_mask, MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
static enemy_mask enemies[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];

static void auto_settler_findwork(struct player *pplayer, 
                                  struct unit *punit);
static void auto_settlers_player(struct player *pplayer); 
static bool is_already_assigned(struct unit *myunit, struct player *pplayer,
				int x, int y);
static int city_desirability(struct player *pplayer, int x, int y);

/**************************************************************************
  Build a city and initialize AI infrastructure cache.
**************************************************************************/
static bool ai_do_build_city(struct player *pplayer, struct unit *punit)
{
  int x = punit->x, y = punit->y;
  struct city *pcity;

  handle_unit_build_city(pplayer, punit->id,
			 city_name_suggestion(pplayer, x, y));
  pcity = map_get_city(x, y);
  if (!pcity) {
    freelog(LOG_ERROR, "%s: Failed to build city at (%d, %d)", 
            pplayer->name, x, y);
    return FALSE;
  }

  /* initialize infrastructure cache for both this city and other cities
     nearby. This is neccesary to avoid having settlers want to transform
     a city into the ocean. */
  map_city_radius_iterate(pcity->x, pcity->y, x_itr, y_itr) {
    struct city *pcity2 = map_get_city(x_itr, y_itr);
    if (pcity2 && city_owner(pcity2) == pplayer)
      initialize_infrastructure_cache(pcity2);
  } map_city_radius_iterate_end;
  return TRUE;
}

/**************************************************************************
  Amortize means gradually paying off a cost or debt over time. In freeciv
  terms this means we calculate how much less worth something is to us
  depending on how long it will take to complete.

  amortize(benefit, delay) returns benefit * ((MORT - 1)/MORT)^delay
  (^ = to the power of)

  Plus, it has tests to prevent the numbers getting too big.  It takes
  advantage of the fact that (23/24)^12 approximately = 3/5 to chug 
  through delay in chunks of 12, and then does the remaining 
  multiplications of (23/24).
**************************************************************************/
int amortize(int benefit, int delay)
{
  int num = MORT - 1;
  int denom;
  int s = 1;
  assert(delay >= 0);
  if (benefit < 0) { s = -1; benefit *= s; }
  while (delay > 0 && benefit != 0) {
    denom = 1;
    while (delay >= 12 && (benefit >> 28) == 0 && (denom >> 27) == 0) {
      benefit *= 3;          /* this is a kluge but it is 99.9% accurate and saves time */
      denom *= 5;      /* as long as MORT remains 24! -- Syela */
      delay -= 12;
    }
    while ((benefit >> 25) == 0 && delay > 0 && (denom >> 25) == 0) {
      benefit *= num;
      denom *= MORT;
      delay--;
    }
    if (denom > 1) { /* The "+ (denom/2)" makes the rounding correct */
      benefit = (benefit + (denom/2)) / denom;
    }
  }
  return(benefit * s);
}

/**************************************************************************
  The minimap marks areas that are occupied by other cities, so we
  don't settle in their area. This also include cities we plan on
  settling.
**************************************************************************/
void init_settlers(void)
{
  /* (Re)allocate map arrays.  Note that the server may run more than one
   * game so the realloc() is necessary. */
  minimap = fc_realloc(minimap, map.xsize * map.ysize * sizeof(*minimap));
  territory = fc_realloc(territory,
			 map.xsize * map.ysize * sizeof(*territory));

  memset(minimap, 0, map.xsize * map.ysize * sizeof(*minimap));

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      map_city_radius_iterate(pcity->x, pcity->y, x_itr, y_itr) {
	MINIMAP(x_itr, y_itr)--;
      } map_city_radius_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
  Remove a city from the minimap. We only do this when we become uncertain
  if our settler will really settle there. That happens a lot, 
  unfortunately. 
**************************************************************************/
void remove_city_from_minimap(int x, int y)
{
  freelog(LOG_DEBUG, "Removing (%d, %d) from minimap.", x, y);
  square_iterate(x, y, CITY_MAP_SIZE-1, x1, y1) {
    int dist = sq_map_distance(x, y, x1, y1);
    if (dist <= CITY_MAP_SIZE) {
      MINIMAP(x1, y1) = MIN(MINIMAP(x1, y1) + 1, 0);
    } else if (dist <= 20) {
      MINIMAP(x1, y1) = MIN(MINIMAP(x1, y1), 0);
    }
  } square_iterate_end;
}    

/**************************************************************************
  Adds a city to the minimap. This is done when a new city is being
  founded, and when the AI assigns a settler to build a city somewhere.
  The minimap also caches city desirability. When the minimap position
  is positive, it is a city want. When it is negative, there is one or
  more cities keeping a semaphoric lock on this tile.
**************************************************************************/
void add_city_to_minimap(int x, int y)
{
  freelog(LOG_DEBUG, "Adding (%d, %d) to minimap.", x, y);
  square_iterate(x, y, CITY_MAP_SIZE-1, x1, y1) {
    int dist = sq_map_distance(x, y, x1, y1);
    if (dist <= CITY_MAP_SIZE) {
      MINIMAP(x1, y1) = MIN(MINIMAP(x1, y1) - 1, -1);
    } else if (dist <= 20) {
      MINIMAP(x1, y1) = MIN(MINIMAP(x1, y1), 0);
    }
  } square_iterate_end;
}

/**************************************************************************
  Calculates the desire for founding a new city at (x, y). This 
  information is cached in the minimap at (x, y) for the remainder of
  the game. The minimap ensures that we do not build cities too close
  to each other.

  FIXME: We need to use a virtual unit here. Some defines added for
  now.- Per

  this whole funct assumes G_REP^H^H^HDEMOCRACY -- Syela
**************************************************************************/
static int city_desirability(struct player *pplayer, int x, int y)
{
#define SETTLER_COST 40
#define SETTLER_LOSS_FACTOR 12 /* totally arbitrary Syelaism */
#define TEMPLE_COST 40
  int taken[CITY_MAP_SIZE][CITY_MAP_SIZE], food[CITY_MAP_SIZE][CITY_MAP_SIZE];
  int shield[CITY_MAP_SIZE][CITY_MAP_SIZE], trade[CITY_MAP_SIZE][CITY_MAP_SIZE];
  int irrig[CITY_MAP_SIZE][CITY_MAP_SIZE], mine[CITY_MAP_SIZE][CITY_MAP_SIZE];
  int road[CITY_MAP_SIZE][CITY_MAP_SIZE];
  int cfood, n, worst, b2, best = 0, ii, jj, val, cur;
  int a, i0, j0, temp=0, tmp=0; /* need some temp variables */
  const bool debug = FALSE; /* turn on extra LOG_DEBUG output */
  struct tile *ptile;
  int continent, continent2;
  bool harbour; /* is near ocean */
  struct tile_type *ptype;
  struct city *pcity;
  int defense_bonus;

  if (is_square_threatened(pplayer, x, y))
    return 0;
  
  ptile = map_get_tile(x, y);
  if (is_ocean(ptile->terrain)) {
    return 0;
  }
  pcity = map_get_city(x, y);
  if (pcity && pcity->size >= game.add_to_size_limit) return 0;
  if (!pcity && MINIMAP(x, y) < 0) {
    return 0;
  }
  if (!pcity && MINIMAP(x, y) > 0) {
    return MINIMAP(x, y);
  }

  harbour = is_ocean_near_tile(x, y);

  continent = ptile->continent;

  memset(taken, 0, sizeof(taken));
  memset(food, 0, sizeof(food));
  memset(shield, 0, sizeof(shield));
  memset(trade, 0, sizeof(trade));
  memset(irrig, 0, sizeof(irrig));
  memset(mine, 0, sizeof(mine));
  memset(road, 0, sizeof(road));

  city_map_checked_iterate(x, y, i, j, map_x, map_y) {
    const int cshields = SHIELD_WEIGHTING * MORT;
    const int ctrade = TRADE_WEIGHTING * MORT; 

    if ((!pcity && MINIMAP(map_x, map_y) >= 0)
	|| (pcity && get_worker_city(pcity, i, j) == C_TILE_EMPTY)) {
      ptile = map_get_tile(map_x, map_y);
      continent2 = ptile->continent;
      ptype = get_tile_type(ptile->terrain);
      food[i][j] = (get_tile_food_base(ptile) - 2) * MORT;
      if (is_city_center(i, j)) {
	food[i][j] += 2 * MORT;
      }
      if (ptype->irrigation_result == ptile->terrain 
          && continent2 == continent) {
	if (tile_has_special(ptile, S_IRRIGATION) || is_city_center(i, j)) {
	  irrig[i][j] = MORT * ptype->irrigation_food_incr;
	}
        else if (is_water_adjacent_to_tile(map_x, map_y) &&
                 ptile->terrain != T_HILLS)
	  irrig[i][j] = MORT * ptype->irrigation_food_incr - 9; /* KLUGE */
/* all of these kluges are hardcoded amortize calls to save much CPU use -- Syela */
      } else if (harbour && is_ocean(ptile->terrain)) {
	food[i][j] += MORT;
      }
      shield[i][j] = get_tile_shield_base(ptile) * cshields;
      if (is_city_center(i, j) && shield[i][j] == 0) {
	shield[i][j] = cshields;
      }
#if 0
      if (harbour && is_ocean(ptile->terrain)) {
	shield[i][j] += cshields;
      }
      /* above line is not sufficiently tested.  AI was building on shores, 
       * but not as far out in the ocean as possible, which limits growth in
       * the very long term (after SEWER).  These cities will all eventually
       * have OFFSHORE, and I need to acknowledge that.  I probably shouldn't
       * treat it as free, but that's the easiest, and I doubt pathological
       * behavior will result. -- Syela */
#endif
      if (tile_has_special(ptile, S_MINE)) {
	mine[i][j] = cshields * ptype->mining_shield_incr;
      } else if (ptile->terrain == T_HILLS && continent2 == continent) {
	mine[i][j] = cshields * ptype->mining_shield_incr - 300; /* KLUGE */
      }
      trade[i][j] = get_tile_trade_base(ptile) * ctrade;
      if (ptype->road_trade_incr > 0) {
	if (tile_has_special(ptile, S_ROAD) || is_city_center(i, j)) {
	  road[i][j] = ctrade * ptype->road_trade_incr;
	}
        else if (continent2 == continent) {
          /* KLUGE */ /* actualy exactly 70 1/2 */
          road[i][j] = ctrade * ptype->road_trade_incr - 70; 
        }
      }
      if (trade[i][j] != 0) trade[i][j] += ctrade;
      else if (road[i][j] != 0) road[i][j] += ctrade;
    }
  } city_map_checked_iterate_end;

  if (pcity) { /* quick-n-dirty immigration routine -- Syela */
    n = pcity->size;
    best = 0; ii = 0; jj = 0; /* blame -Wall -Werror for these */
    city_map_iterate(i, j) {
      cur = (food[i][j]) * food_weighting(n) + /* ignoring irrig on purpose */
            (shield[i][j] + mine[i][j]) +
            (trade[i][j] + road[i][j]);
      if (cur > best && !is_city_center(i, j)) {
	best = cur;
	ii = i;
	jj = j;
      }
    } city_map_iterate_end;
    if (best == 0) return(0);
    val = (shield[ii][jj] + mine[ii][jj])
          + (food[ii][jj] + irrig[ii][jj]) * FOOD_WEIGHTING
          + (trade[ii][jj] + road[ii][jj]);
    val -= amortize(SETTLER_COST * SHIELD_WEIGHTING + (50 - 20)
                    * FOOD_WEIGHTING, SETTLER_LOSS_FACTOR);
    freelog(LOG_DEBUG, "Desire to add ourselves to city %s = %d -> %d",
		  pcity->name, val, (val * 100) / MORT / 70);
    return(val);
  }

  cfood = food[2][2] + irrig[2][2];
  if (cfood < 2 * FOOD_WEIGHTING) {
    /* no starving cities, thank you! -- Syela */
    /* This used to be zero, but I increased the limit to two to avoid
     * some common pathological behaviour. An optimal algorithm should
     * not need such a limitation, but the algorithm used here is far
     * from optimal, hence this hack. The problem is that the AI often
     * builds cities that are unable to produce settlers, which is
     * a lethal mistake. -- Per */
    return 0;
  }

  val = cfood * FOOD_WEIGHTING + /* this needs to be here, strange as it seems */
          (shield[2][2] + mine[2][2]) +
          (trade[2][2] + road[2][2]);
  taken[2][2]++;
  /* val is mort times the real value */
  /* treating harbor as free to approximate advantage of
     building boats. -- Syela */
  defense_bonus = get_tile_type(map_get_terrain(x, y))->defense_bonus;
  if (map_has_special(x, y, S_RIVER))
    defense_bonus += (defense_bonus * terrain_control.river_defense_bonus) / 100;
  val += (4 * defense_bonus - SETTLER_COST) * SHIELD_WEIGHTING;
  /* don't build cities in danger!! FIX! -- Syela */
  val += 8 * MORT; /* one science per city */ /* FIXME: not correct - Per */
  
  freelog(LOG_DEBUG, "City value (%d, %d) = %d, harbour = %d, cfood = %d",
		     x, y, val, harbour, cfood);

  /* loop until size 20 virtual city */
  for (n = 1; n <= 20 && cfood > 0; n++) {
    int d = 0; /* i have no idea wtf this is - per */
    for (a = 1; a > 0; a--) {
      best = 0; worst = -1; b2 = 0; i0 = 0; j0 = 0; ii = 0; jj = 0;
      city_map_iterate(i, j) {
        cur = (food[i][j]) * food_weighting(n) + /* ignoring irrig on purpose */
              (shield[i][j] + mine[i][j]) +
              (trade[i][j] + road[i][j]);
	if (taken[i][j] == 0) {
	  if (cur > best) {
	    b2 = best;
	    best = cur;
	    ii = i;
	    jj = j;
	  } else if (cur > b2) {
	    b2 = cur;
	  }
	} else if (!is_city_center(i, j) && (cur < worst || worst < 0)) {
	  worst = cur;
	  i0 = i;
	  j0 = j;
	}
      } city_map_iterate_end;
      if (best == 0) break;
      cur = amortize((shield[ii][jj] + mine[ii][jj]) +
            (food[ii][jj] + irrig[ii][jj]) * FOOD_WEIGHTING + /* seems to be needed */
            (trade[ii][jj] + road[ii][jj]), d);
      cfood += food[ii][jj] + irrig[ii][jj];
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
        cfood -= (food[i0][j0] + irrig[i0][j0]);
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
    if (best == 0) break;
    if (cfood > 0) d += (game.foodbox * MORT * n + cfood - 1) / cfood;
    if (n == 4) {
      val -= amortize(SETTLER_COST * SHIELD_WEIGHTING + (50 - 20) * 
                      FOOD_WEIGHTING, d); /* settlers */
      temp = amortize(TEMPLE_COST * SHIELD_WEIGHTING, d); /* temple */
      tmp = val;
    }
  }
  if (n > 4) {
    if (val - temp > tmp) val -= temp;
    else val = tmp;
  }
  val -= 110 * SHIELD_WEIGHTING; /* WAG: walls, defenders */
  MINIMAP(x, y) = val;

  if (debug) {
    freelog(LOG_DEBUG, "Total value of (%d, %d) [%d workers] = %d",
	    x, y, n, val);
  }
  return(val);
}
#undef TEMPLE_COST
#undef SETTLER_COST
#undef SETTLER_LOSS_FACTOR

/**************************************************************************
  Manages settlers.
**************************************************************************/
void ai_manage_settler(struct player *pplayer, struct unit *punit)
{
  punit->ai.control = TRUE;
  /* if BUILD_CITY must remain BUILD_CITY, otherwise turn into autosettler */
  if (punit->ai.ai_role == AIUNIT_NONE) {
    ai_unit_new_role(punit, AIUNIT_AUTO_SETTLER, -1, -1);
  }
  return;
}

/**************************************************************************
 return 1 if there is already a unit on this square or one destined for it 
 (via goto)
**************************************************************************/
static bool is_already_assigned(struct unit *myunit, struct player *pplayer, 
    int x, int y)
{
  if (same_pos(myunit->x, myunit->y, x, y)
      || (is_goto_dest_set(myunit) /* HACK */
	  && same_pos(goto_dest_x(myunit), goto_dest_y(myunit), x, y))) {
/* I'm still not sure this is exactly right -- Syela */
    unit_list_iterate(map_get_tile(x, y)->units, punit)
      if (myunit==punit) continue;
      if (!pplayers_allied(unit_owner(punit), pplayer))
        return TRUE; /* oops, tile is occupied! */
      if (unit_flag(punit, F_SETTLERS) && unit_flag(myunit, F_SETTLERS))
        return TRUE;
    unit_list_iterate_end;
    return FALSE;
  }
  return TEST_BIT(map_get_tile(x, y)->assigned, pplayer->player_no);
}

/**************************************************************************
  Calculates the value of removing pollution.
**************************************************************************/
static int ai_calc_pollution(struct city *pcity, int cx, int cy, int best,
			     int mx, int my)
{
  int m;

  if (!map_has_special(mx, my, S_POLLUTION)) return(-1);
  map_clear_special(mx, my, S_POLLUTION);
  m = city_tile_value(pcity, cx, cy, 0, 0);
  map_set_special(mx, my, S_POLLUTION);
  m = (m + best + 50) * 2;
  return(m);
}

/**************************************************************************
  Calculates the value of removing fallout.
**************************************************************************/
static int ai_calc_fallout(struct city *pcity, struct player *pplayer,
			   int cx, int cy, int best, int mx, int my)
{
  int m;

  if (!map_has_special(mx, my, S_FALLOUT)) return(-1);
  map_clear_special(mx, my, S_FALLOUT);
  m = city_tile_value(pcity, cx, cy, 0, 0);
  map_set_special(mx, my, S_FALLOUT);
  if (!pplayer->ai.control)
    m = (m + best + 50) * 2;
  return(m);
}

/**************************************************************************
  Returns TRUE if tile at x, y is useful as a source of irrigation. If
  the given player is an AI, it will ignore fog of war. (Do not "fix" this,
  since the AI does too little exploration yet to manage without this.)
**************************************************************************/
static bool is_wet(struct player *pplayer, int x, int y)
{
  enum tile_terrain_type t;
  enum tile_special_type s;

  if (!pplayer->ai.control && !map_is_known(x, y, pplayer)) {
    return FALSE;
  }

  t=map_get_terrain(x,y);
  if (is_ocean(t)) {
    return TRUE;
  }
  s=map_get_special(x,y);
  if (contains_special(s, S_RIVER) || contains_special(s, S_IRRIGATION)) return TRUE;
  return FALSE;
}

/**************************************************************************
  Returns TRUE if there is an irrigation source adjacent to the given x, y
  position.
**************************************************************************/
static bool is_wet_or_is_wet_cardinal_around(struct player *pplayer, int x,
					    int y)
{
  if (is_wet(pplayer, x, y))
    return TRUE;

  cartesian_adjacent_iterate(x, y, x1, y1) {
    if (is_wet(pplayer, x1, y1))
      return TRUE;
  } cartesian_adjacent_iterate_end;

  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static int ai_calc_irrigate(struct city *pcity, struct player *pplayer,
			    int cx, int cy, int mx, int my)
{
  int m;
  struct tile *ptile = map_get_tile(mx, my);
  enum tile_terrain_type t = ptile->terrain;
  struct tile_type *type = get_tile_type(t);
  enum tile_special_type s = ptile->special;

  if (ptile->terrain != type->irrigation_result &&
      type->irrigation_result != T_LAST) { /* EXPERIMENTAL 980905 -- Syela */
    if (ptile->city && is_ocean(type->irrigation_result)) {
      return -1;
    }
    ptile->terrain = type->irrigation_result;
    map_clear_special(mx, my, S_MINE);
    m = city_tile_value(pcity, cx, cy, 0, 0);
    ptile->terrain = t;
    ptile->special = s;
    return(m);
  } else if((ptile->terrain==type->irrigation_result &&
     !tile_has_special(ptile, S_IRRIGATION) &&
     !tile_has_special(ptile, S_MINE) && !(ptile->city) &&
     (is_wet_or_is_wet_cardinal_around(pplayer, mx, my)))) {
    map_set_special(mx, my, S_IRRIGATION);
    m = city_tile_value(pcity, cx, cy, 0, 0);
    map_clear_special(mx, my, S_IRRIGATION);
    return(m);
  } else if((ptile->terrain==type->irrigation_result &&
    tile_has_special(ptile, S_IRRIGATION) && !tile_has_special(ptile, S_FARMLAND) &&
     player_knows_techs_with_flag(pplayer, TF_FARMLAND) &&
     !tile_has_special(ptile, S_MINE) && !(ptile->city) &&
     (is_wet_or_is_wet_cardinal_around(pplayer, mx, my)))) {
    map_set_special(mx, my, S_FARMLAND);
    m = city_tile_value(pcity, cx, cy, 0, 0);
    map_clear_special(mx, my, S_FARMLAND);
    return(m);
  } else return(-1);
}

/**************************************************************************
  Calculates the value of building a mine.
**************************************************************************/
static int ai_calc_mine(struct city *pcity, int cx, int cy, int mx, int my)
{
  int m;
  struct tile *ptile = map_get_tile(mx, my);

  /* Don't replace existing infrastructure */
  if ((ptile->terrain == T_HILLS || ptile->terrain == T_MOUNTAINS) &&
      !tile_has_special(ptile, S_IRRIGATION) && !tile_has_special(ptile, S_MINE)) {
    map_set_special(mx, my, S_MINE);
    m = city_tile_value(pcity, cx, cy, 0, 0);
    map_clear_special(mx, my, S_MINE);
    return(m);
  } else return(-1);
}

/**************************************************************************
  Calculates the value of doing a terrain transformation.
**************************************************************************/
static int ai_calc_transform(struct city *pcity, int cx, int cy, int mx,
			     int my)
{
  int m;
  struct tile *ptile = map_get_tile(mx, my);
  enum tile_terrain_type t = ptile->terrain;
  struct tile_type *type = get_tile_type(t);
  enum tile_special_type s = ptile->special;
  enum tile_terrain_type r = type->transform_result;

  if (is_ocean(t) && !can_reclaim_ocean(mx, my)) {
    return -1;
  }
  if (is_ocean(r) && !can_channel_land(mx, my)) {
    return -1;
  }

  if ((t == T_ARCTIC || t == T_DESERT || t == T_JUNGLE || t == T_SWAMP  || 
       t == T_TUNDRA || t == T_MOUNTAINS) && r != T_LAST) {
    if (is_ocean(r) && ptile->city) {
      return -1;
    }

    ptile->terrain = r;

    if (get_tile_type(r)->mining_result != r) 
      map_clear_special(mx, my, S_MINE);

    if (get_tile_type(r)->irrigation_result != r) {
      map_clear_special(mx, my, S_FARMLAND);
      map_clear_special(mx, my, S_IRRIGATION);
    }
    
    m = city_tile_value(pcity, cx, cy, 0, 0);
    ptile->terrain = t;
    ptile->special = s;
    return(m);
  } else return(-1);
}

/**************************************************************************
Calculate the attractiveness
"spc" will be S_ROAD or S_RAILROAD for sane calls.
**************************************************************************/
static int road_bonus(int x, int y, enum tile_special_type spc)
{
  int m = 0, k;
  bool rd[12], te[12];
  int ii[12] = { -1, 0, 1, -1, 1, -1, 0, 1, 0, -2, 2, 0 };
  int jj[12] = { -1, -1, -1, 0, 0, 1, 1, 1, -2, 0, 0, 2 };
  struct tile *ptile;
  bool is_border = IS_BORDER_MAP_POS(x, y, 2);
  
  assert(spc == S_ROAD || spc == S_RAILROAD);

  if (!normalize_map_pos(&x, &y))
    return 0;

  for (k = 0; k < 12; k++) {
    int x1 = x + ii[k], y1 = y + jj[k];
    if (is_border && !normalize_map_pos(&x1, &y1)) {
      rd[k] = FALSE;
      te[k] = FALSE;
    } else {
      ptile = map_get_tile(x1, y1);
      rd[k] = tile_has_special(ptile, spc);
      te[k] = (ptile->terrain == T_MOUNTAINS || is_ocean(ptile->terrain));
      if (!rd[k]) {
	unit_list_iterate(ptile->units, punit) {
	  if (punit->activity == ACTIVITY_ROAD 
              || punit->activity == ACTIVITY_RAILROAD) {
	    rd[k] = TRUE;
          }
	} unit_list_iterate_end;
      }
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

/**************************************************************************
  Calculate the value of building a road.
**************************************************************************/
static int ai_calc_road(struct city *pcity, struct player *pplayer,
			int cx, int cy, int mx, int my)
{
  int m;
  struct tile *ptile = map_get_tile(mx, my);

  if (!is_ocean(ptile->terrain) &&
      (!tile_has_special(ptile, S_RIVER) ||
       player_knows_techs_with_flag(pplayer, TF_BRIDGE)) &&
      !tile_has_special(ptile, S_ROAD)) {
    ptile->special|=S_ROAD; /* have to do this to avoid reset_move_costs -- Syela */
    m = city_tile_value(pcity, cx, cy, 0, 0);
    ptile->special&=~S_ROAD;
    return(m);
  } else return(-1);
}

/**************************************************************************
  Calculate the value of building a railroad.
**************************************************************************/
static int ai_calc_railroad(struct city *pcity, struct player *pplayer,
			    int cx, int cy, int mx, int my)
{
  int m;
  enum tile_special_type spe_sav;
  struct tile *ptile = map_get_tile(mx, my);

  if (!is_ocean(ptile->terrain) &&
      player_knows_techs_with_flag(pplayer, TF_RAILROAD) &&
      !tile_has_special(ptile, S_RAILROAD)) {
    spe_sav = ptile->special;
    ptile->special|=(S_ROAD | S_RAILROAD);
    m = city_tile_value(pcity, cx, cy, 0, 0);
    ptile->special = spe_sav;
    return(m);
  } else return(-1);
  /* bonuses for adjacent railroad tiles */
}

/**************************************************************************
  Tries to find a boat for our settler. Requires warmap to be initialized
  with respect to x, y. cap is the requested capacity on the transport.
  Note that it may return a transport with less than cap capacity if this
  transport has zero move cost to x, y.

  The "virtual boats" code is not used. It is probably too unreliable, 
  since the AI switches its production back and forth continously.

  TODO: Kill me.  There is a reliable version of this, find_ferry.
**************************************************************************/
Unit_Type_id find_boat(struct player *pplayer, int *x, int *y, int cap)
{
  int best = 22; /* arbitrary maximum distance, I will admit! */
  Unit_Type_id id = 0;
  unit_list_iterate(pplayer->units, aunit)
    if (is_ground_units_transport(aunit)) {
      if (WARMAP_COST(aunit->x, aunit->y) < best &&
	  (WARMAP_COST(aunit->x, aunit->y) == 0 ||
	   ground_unit_transporter_capacity(aunit->x, aunit->y,
					    pplayer) >= cap)) {
        id = aunit->id;
        best = WARMAP_COST(aunit->x, aunit->y);
        *x = aunit->x;
        *y = aunit->y;
      }
    }
  unit_list_iterate_end;
  if (id != 0) return(id);
  return(id);
}

/**************************************************************************
  Returns TRUE if there are (other) ground units than punit stacked on
  punit's tile.
**************************************************************************/
struct unit *other_passengers(struct unit *punit)
{
  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, aunit)
    if (is_ground_unit(aunit) && aunit != punit) return aunit;
  unit_list_iterate_end;
  return NULL;
}

/****************************************************************************
  Compares the best known tile improvement action with improving the tile
  at (x,y) with activity act.  Calculates the value of improving the tile
  by discounting the total value by the time it would take to do the work
  and multiplying by some factor.
****************************************************************************/
static void consider_settler_action(struct player *pplayer, 
                                    enum unit_activity act, int extra, 
                                    int new_tile_value, int old_tile_value,
				    bool in_use, int delay,
				    int *best_value,
				    int *best_old_tile_value,
				    enum unit_activity *best_act, 
                                    int *gx, int *gy, int x, int y)
{
  int discount_value, base_value = 0;
  int total_value;
  bool consider;

  if (extra >= 0) {
    consider = TRUE;
  } else {
    consider = (new_tile_value > old_tile_value);
  }

  if (consider) {
    int diff = new_tile_value - old_tile_value;

    /* The 64x is because we are dealing with small ints, usually from 0-20,
     * which are insufficiently large to use directly in amortize().  Tiles
     * which are not currently in use do not give us an improvement until
     * a citizen works them, so they are reduced in value by 1/4. */
    base_value = diff * (in_use ? 64 : 16) + extra * 64;
    base_value = MAX(0, base_value);

    discount_value = amortize(base_value, delay);

    /* The total value is (roughly) equal to the base value multiplied by
     * d / (1 - d), where d is the discount. (discount_value / base value)
     * The MAX is a guard against the base value being greater or equal
     * than the discount value, which would only happen if it or the 
     * delay is <= 0. */
    total_value = ((discount_value * base_value)
		   / (MAX(1, base_value - discount_value))) / 64;
  } else {
    total_value = 0;
  }

  if (total_value > *best_value
      || (total_value == *best_value
	  && old_tile_value > *best_old_tile_value)) {
    freelog(LOG_DEBUG,
	    "Replacing (%d, %d) = %d with %s (%d, %d) = %d [d=%d b=%d]",
	    *gx, *gy, *best_value, get_activity_text(act), x, y, total_value,
            delay, base_value);
    *best_value = total_value;
    *best_old_tile_value = old_tile_value;
    *best_act = act;
    *gx = x;
    *gy = y;
  }
}

/**************************************************************************
  Returns how much food a settler will consume out of the city's foodbox
  when created. If unit has id zero it is assumed to be a virtual unit
  inside a city.

  FIXME: This function should be generalised and then moved into 
  common/unittype.c - Per
**************************************************************************/
static int unit_foodbox_cost(struct unit *punit)
{
  int cost = 30;

  if (punit->id == 0) {
    /* It is a virtual unit, so must start in a city... */
    struct city *pcity = map_get_city(punit->x, punit->y);

    assert(pcity != NULL);
    cost = city_granary_size(pcity->size);
    if (city_got_effect(pcity, B_GRANARY)) { cost /= 2; }
  }

  return cost;
}

/**************************************************************************
  Calculates a unit's food upkeep (per turn).
**************************************************************************/
static int unit_food_upkeep(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  int upkeep = utype_food_cost(unit_type(punit),
			       get_gov_pplayer(pplayer));
  if (punit->id != 0 && punit->homecity == 0)
    upkeep = 0; /* thanks, Peter */

  return upkeep;
}

/**************************************************************************
Evaluates all squares within 11 squares of the settler for city building.

best_newv is the current best activity value for the settler.
best_act is the activity of the best value. gx, gy is where the activity
takes place.
If this function finds a better alternative it will modify these values.

ferryboat is a ferryboat the unit can use to go to the tile with value
best_newv.
if gx is -1 and the return value is != 0 then we want to go to another
continent, but need to build a ferryboat first.
**************************************************************************/
static int evaluate_city_building(struct unit *punit,
				  int *gx, int *gy,
				  struct unit **ferryboat)
{
  struct city *mycity = map_get_city(punit->x, punit->y);
  int best_newv = 0, best_moves = 0;
  struct player *pplayer = unit_owner(punit);
  bool nav_known          = (get_invention(pplayer, game.rtech.nav) == TECH_KNOWN);
  int ucont              = map_get_continent(punit->x, punit->y);
  int mv_rate            = unit_type(punit)->move_rate;
  int food_upkeep        = unit_food_upkeep(punit);
  int food_cost          = unit_foodbox_cost(punit);

  int boatid, bx = 0, by = 0;	/* as returned by find_boat */
  enemy_mask my_enemies = enemies[pplayer->player_no]; /* optimalization */

  if (pplayer->ai.control)
    boatid = find_boat(pplayer, &bx, &by, 1); /* might need 2 for bodyguard */
  else
    boatid = 0;
  *ferryboat = unit_list_find(&(map_get_tile(punit->x, punit->y)->units), boatid);
  if (*ferryboat)
    really_generate_warmap(mycity, *ferryboat, SEA_MOVING);

  generate_warmap(mycity, punit);

  /* A default, and also resets minimap if we reserved it previously */
  ai_unit_new_role(punit, AIUNIT_AUTO_SETTLER, -1, -1);

 /* hope 11 is far enough -- Syela */
  square_iterate(punit->x, punit->y, 11, x, y) {
    int near = real_map_distance(punit->x, punit->y, x, y);
    bool w_virtual = FALSE;	/* I'm no entirely sure what this is --dwp */
    int b, mv_cost, newv, moves = 0;

    if (!is_already_assigned(punit, pplayer, x, y)
	&& city_can_be_built_here(x, y, punit)
	&& !BV_CHECK_MASK(TERRITORY(x, y), my_enemies)
	/* pretty good, hope it's enough! -- Syela */
	&& (near < 8 || map_get_continent(x, y) != ucont)
	&& !city_exists_within_city_radius(x, y, FALSE)) {

      /* potential target, calculate mv_cost: */
      if (*ferryboat) {
	/* already aboard ship, can use real warmap */
	if (!is_ocean_near_tile(x, y)) {
	  mv_cost = 9999;
	} else {
	  mv_cost = WARMAP_SEACOST(x, y) * mv_rate /
	      unit_type(*ferryboat)->move_rate;
	}
      } else if (!goto_is_sane(punit, x, y, TRUE) ||
		 WARMAP_COST(x, y) > THRESHOLD * mv_rate) {
	/* for Rome->Carthage */
	if (!is_ocean_near_tile(x, y)) {
	  mv_cost = 9999;
	} else if (boatid != 0) {
	  if (punit->id == 0 && mycity->id == boatid) {
	    w_virtual = TRUE;
	  }
	  mv_cost = WARMAP_COST(bx, by) + real_map_distance(bx, by, x, y)
	    + mv_rate; 
	} else if (punit->id != 0 ||
		   !is_ocean_near_tile(mycity->x, mycity->y)) {
	  mv_cost = 9999;
	} else {
	  mv_cost = WARMAP_SEACOST(x, y) * mv_rate / 9;
	  /* this should be fresh; the only thing that could have
	     munged the seacost is the ferryboat code in
	     k_s_w/f_s_t_k, but only if find_boat succeeded */
	  w_virtual = TRUE;
	}
      } else {
	mv_cost = WARMAP_COST(x, y);
      }
      moves = mv_cost / mv_rate;
      /* without this, the computer will go 6-7 tiles from X to
	 build a city at Y */
      moves *= 2;
      /* and then build its NEXT city halfway between X and Y. -- Syela */
      b = city_desirability(pplayer, x, y);
      newv = amortize(b, moves);

      b = (food_upkeep * FOOD_WEIGHTING) * MORT;
      if (map_get_continent(x, y) != ucont) {
        b += SHIELD_WEIGHTING * MORT;
      }
      newv -= (b - amortize(b, moves));
      /* deal with danger Real Soon Now! -- Syela */
      /* newv is now the value over mort turns */
      newv = (newv * 100) / MORT / ((w_virtual ? 80 : 40) + food_cost);

      /* I added a line to discourage settling in existing cities, but it was
	 inadequate.  It may be true that in the short-term building infrastructure
	 on tiles that won't be worked for ages is not as useful as helping other
	 cities grow and then building engineers later, but this is short-sighted.
	 There was also a problem with workers suddenly coming onto unimproved tiles,
	 either through city growth or de-elvisization, settlers being built and
	 improving those tiles, and then immigrating shortly thereafter. -- Syela
      */
      if (best_newv != 0 && map_get_city(x, y)) newv = 0;
	  
      if (newv>0 && pplayer->ai.expand!=100) {
	newv = (newv * pplayer->ai.expand) / 100;
      }

#ifdef REALLY_DEBUG_THIS
      if (w_virtual) {
	freelog(LOG_DEBUG, "%s: best_newv = %d, w_virtual = 1, newv = %d",
		mycity->name, best_newv, newv);
      }
#endif

      if (map_get_continent(x, y) != ucont && !nav_known && near >= 8) {
#ifdef REALLY_DEBUG_THIS
	freelog(LOG_DEBUG,
		"%s (%d, %d) rejected city at (%d, %d) to %d, newv = %d, moves = %d" \
                " (too far for trireme)",
		(punit->id != 0 ? unit_type(punit)->name : mycity->name), 
		punit->x, punit->y, x, y, b, newv, moves);
#endif
      } else if (newv > best_newv) {
	best_newv = newv;
	best_moves = moves;
	if (w_virtual) {
	  *gx = -1; *gy = -1;
	} else {
	  *gx = x; *gy = y;
	}
      }
    }
  } square_iterate_end;

  freelog(LOG_DEBUG,
	"%s %d(%d, %d) wants city at (%d, %d) with want %d, distance %d moves",
	(punit->id != 0 ? unit_type(punit)->name : mycity->name), 
	(punit->id != 0 ? punit->id : mycity->id), 
	punit->x, punit->y, *gx, *gy, best_newv, best_moves);
  return best_newv;
}

/**************************************************************************
Finds tiles to improve, using punit.
How nice a tile it finds is returned. If it returns >0 gx,gy indicates the
tile it has chosen, and bestact indicates the activity it wants to do.
**************************************************************************/
static int evaluate_improvements(struct unit *punit,
				 enum unit_activity *best_act, 
                                 int *gx, int *gy)
{
  struct city *mycity = map_get_city(punit->x, punit->y);
  struct player *pplayer = unit_owner(punit);
  bool in_use;			/* true if the target square is being used
				   by one of our cities */
  int ucont           = map_get_continent(punit->x, punit->y);
  int mv_rate         = unit_type(punit)->move_rate;
  int mv_turns;			/* estimated turns to move to target square */
  int oldv;			/* current value of consideration tile */
  int best_oldv = 9999;		/* oldv of best target so far; compared if
				   newv==best_newv; not initialized to zero,
				   so that newv=0 activities are not chosen */
  int food_upkeep        = unit_food_upkeep(punit);
  int food_cost          = unit_foodbox_cost(punit);
  bool can_rr = player_knows_techs_with_flag(pplayer, TF_RAILROAD);

  int best_newv = 0;
  enemy_mask my_enemies = enemies[pplayer->player_no]; /* optimalization */

  generate_warmap(mycity, punit);

  city_list_iterate(pplayer->cities, pcity) {
#ifdef REALLY_DEBUG_THIS
    freelog(LOG_DEBUG, "Evaluating improvements for %s...", pcity->name);
#endif
    /* try to work near the city */
    city_map_checked_iterate(pcity->x, pcity->y, i, j, x, y) {
      if (get_worker_city(pcity, i, j) == C_TILE_UNAVAILABLE)
	continue;
      in_use = (get_worker_city(pcity, i, j) == C_TILE_WORKER);
      if (map_get_continent(x, y) == ucont
	  && WARMAP_COST(x, y) <= THRESHOLD * mv_rate
	  && !BV_CHECK_MASK(TERRITORY(x, y), my_enemies)
	  /* pretty good, hope it's enough! -- Syela */
	  && !is_already_assigned(punit, pplayer, x, y)) {
	/* calling is_already_assigned once instead of four times
	   for obvious reasons;  structure is much the same as it once
	   was but subroutines are not -- Syela	*/
	int time;
	mv_turns = (WARMAP_COST(x, y)) / mv_rate;
	oldv = city_tile_value(pcity, i, j, 0, 0);

	/* now, consider various activities... */

	time = (map_build_irrigation_time(x, y)*SINGLE_MOVE + mv_rate - 1)/mv_rate +
	  mv_turns;
	consider_settler_action(pplayer, ACTIVITY_IRRIGATE, -1,
				pcity->ai.irrigate[i][j], oldv, in_use, time,
				&best_newv, &best_oldv, best_act, gx, gy,
				x, y);

	if (unit_flag(punit, F_TRANSFORM)) {
	  time = (map_transform_time(x, y)*SINGLE_MOVE + mv_rate - 1)/mv_rate +
	    mv_turns;
	  consider_settler_action(pplayer, ACTIVITY_TRANSFORM, -1,
				  pcity->ai.transform[i][j], oldv, in_use, time,
				  &best_newv, &best_oldv, best_act, gx, gy,
				  x, y);
	}

	time = (map_build_mine_time(x, y)*SINGLE_MOVE + mv_rate - 1)/mv_rate +
	  mv_turns;
	consider_settler_action(pplayer, ACTIVITY_MINE, -1,
				pcity->ai.mine[i][j], oldv, in_use, time,
				&best_newv, &best_oldv, best_act, gx, gy,
				x, y);

	if (!map_has_special(x, y, S_ROAD)) {
	  time = (map_build_road_time(x, y)*SINGLE_MOVE + mv_rate - 1)/mv_rate +
	    mv_turns;
	  consider_settler_action(pplayer, ACTIVITY_ROAD,
				  road_bonus(x, y, S_ROAD) * 5,
				  pcity->ai.road[i][j], oldv, in_use, time,
				  &best_newv, &best_oldv, best_act, gx, gy,
				  x, y);

	  time = (map_build_rail_time(x, y) * SINGLE_MOVE
		  + map_build_road_time(x, y) * SINGLE_MOVE
		  + mv_rate - 1)/mv_rate + mv_turns;
	  consider_settler_action(pplayer, ACTIVITY_ROAD,
				  road_bonus(x, y, S_RAILROAD) * 3,
				  pcity->ai.railroad[i][j], oldv, in_use, time,
				  &best_newv, &best_oldv, best_act, gx, gy,
				  x, y);
	} else if (!map_has_special(x, y, S_RAILROAD)
		   && can_rr) {
	  time = (map_build_rail_time(x, y) *SINGLE_MOVE
		  + mv_rate - 1)/mv_rate + mv_turns;
	  consider_settler_action(pplayer, ACTIVITY_RAILROAD,
				  road_bonus(x, y, S_RAILROAD) * 3,
				  pcity->ai.railroad[i][j], oldv, in_use, time,
				  &best_newv, &best_oldv, best_act, gx, gy,
				  x, y);
	} /* end S_ROAD else */

	if (map_has_special(x, y, S_POLLUTION)) {
	  time = (map_clean_pollution_time(x, y) * SINGLE_MOVE
		  + mv_rate - 1)/mv_rate + mv_turns;
	  consider_settler_action(pplayer, ACTIVITY_POLLUTION,
				  pplayer->ai.warmth,
				  pcity->ai.detox[i][j], oldv, in_use, time,
				  &best_newv, &best_oldv, best_act, gx, gy,
				  x, y);
	}
      
	if (map_has_special(x, y, S_FALLOUT)) {
	  time = (map_clean_fallout_time(x, y) * SINGLE_MOVE
		  + mv_rate - 1)/mv_rate + mv_turns;
	  consider_settler_action(pplayer, ACTIVITY_FALLOUT,
				  pplayer->ai.warmth,
				  pcity->ai.derad[i][j], oldv, in_use, time,
				  &best_newv, &best_oldv, best_act, gx, gy,
				  x, y);
	}

#ifdef REALLY_DEBUG_THIS
	freelog(LOG_DEBUG,
		"(%d %d) I=%+-4d O=%+-4d M=%+-4d R=%+-4d RR=%+-4d P=%+-4d N=%+-4d",
		i, j,
		pcity->ai.irrigate[i][j], pcity->ai.transform[i][j],
		pcity->ai.mine[i][j], pcity->ai.road[i][j],
		pcity->ai.railroad[i][j], pcity->ai.detox[i][j],
		pcity->ai.derad[i][j]);
#endif
      } /* end if we are a legal destination */
    } city_map_checked_iterate_end;
  } city_list_iterate_end;

  best_newv = (best_newv - food_upkeep * FOOD_WEIGHTING) * 100 / (40 + food_cost);
  if (best_newv < 0)
    best_newv = 0; /* Bad Things happen without this line! :( -- Syela */

  if (best_newv > 0) {
    freelog(LOG_DEBUG,
	    "Settler %d@(%d,%d) wants to %s at (%d,%d) with desire %d",
	    punit->id, punit->x, punit->y, get_activity_text(*best_act),
	    *gx, *gy, best_newv);
  }

  return best_newv;
}

/**************************************************************************
  find some work for the settler
**************************************************************************/
static void auto_settler_findwork(struct player *pplayer, struct unit *punit)
{
  int gx = -1, gy = -1;		/* x,y of target (goto) square */
  int best_newv = 0;		/* newv of best target so far, all cities */
  enum unit_activity best_act = ACTIVITY_IDLE; /* act. of best target so far */
  struct unit *ferryboat = NULL; /* if non-null, boatid boat at unit's x,y */

  /* First find the best square to upgrade,
   * results in: gx, gy, best_newv, best_act */  
  if (unit_flag(punit, F_SETTLERS)) {
    best_newv = evaluate_improvements(punit, &best_act, &gx, &gy);
  }

  /* Found the best square to upgrade, have gx, gy, best_newv, best_act */

  /* Decide whether to build a new city:
   * if so, modify: gx, gy, best_newv, best_act
   */
  if (unit_flag(punit, F_CITIES) &&
      pplayer->ai.control) {
    int nx, ny;
    int want = evaluate_city_building(punit, &nx, &ny, &ferryboat);

    if (want > best_newv) {
      best_newv = want;
      best_act = ACTIVITY_UNKNOWN;
      gx = nx;
      gy = ny;
    }
  }

  /* Mark the square as taken. */
  if (gx != -1 && gy != -1) {
    map_get_tile(gx, gy)->assigned =
      map_get_tile(gx, gy)->assigned | 1<<pplayer->player_no;
  } else {
    /* This line makes non-AI autosettlers go off auto when they run
       out of squares to improve. I would like keep them on, prepared for
       future pollution and warming, but there wasn't consensus to do so. */
    punit->ai.control = FALSE;
    return;
  }

  /* If we intent on building a city then reserve the square. */
  if (unit_flag(punit, F_CITIES) &&
      best_act == ACTIVITY_UNKNOWN /* flag */) {
    ai_unit_new_role(punit, AIUNIT_BUILD_CITY, gx, gy);
  } else {
    ai_unit_new_role(punit, AIUNIT_AUTO_SETTLER, gx, gy);
  }

  /* We've now worked out what to do; go to it! */
  if (!ai_gothere(pplayer, punit, gx, gy)) {
    /* Died or got stuck */
    return;
  }

  /* If we are at the destination then do the activity. */
  if (punit->moves_left > 0
      && same_pos(gx, gy, punit->x, punit->y)) {
    if (best_act == ACTIVITY_UNKNOWN) {
      remove_city_from_minimap(gx, gy); /* yeah, I know. -- Syela */
      handle_unit_activity_request(punit, ACTIVITY_IDLE);
      (void) ai_do_build_city(pplayer, punit);
      return;
    }
    handle_unit_activity_request(punit, best_act);
    send_unit_info(NULL, punit);
    return;
  }
}

/**************************************************************************
  Do all tile improvement calculations and cache them for later.
**************************************************************************/
void initialize_infrastructure_cache(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  int best = best_worker_tile_value(pcity);

  city_map_iterate(cx, cy) {
    pcity->ai.detox[cx][cy] = -1;
    pcity->ai.derad[cx][cy] = -1;
    pcity->ai.mine[cx][cy] = -1;
    pcity->ai.irrigate[cx][cy] = -1;
    pcity->ai.transform[cx][cy] = -1;
    pcity->ai.road[cx][cy] = -1;
    pcity->ai.railroad[cx][cy] = -1;
  } city_map_iterate_end;

  city_map_checked_iterate(pcity->x, pcity->y, cx, cy, mx, my) {
    pcity->ai.detox[cx][cy] = ai_calc_pollution(pcity, cx, cy, best, mx, my);
    pcity->ai.derad[cx][cy] =
	ai_calc_fallout(pcity, pplayer, cx, cy, best, mx, my);
    pcity->ai.mine[cx][cy] = ai_calc_mine(pcity, cx, cy, mx, my);
    pcity->ai.irrigate[cx][cy] =
	ai_calc_irrigate(pcity, pplayer, cx, cy, mx, my);
    pcity->ai.transform[cx][cy] = ai_calc_transform(pcity, cx, cy, mx, my);
    pcity->ai.road[cx][cy] = ai_calc_road(pcity, pplayer, cx, cy, mx, my);
/* gonna handle road_bo dynamically for now since it can change
as punits arrive at adjacent tiles and start laying road -- Syela */
    pcity->ai.railroad[cx][cy] =
	ai_calc_railroad(pcity, pplayer, cx, cy, mx, my);
  } city_map_checked_iterate_end;
}

/************************************************************************** 
  run through all the players settlers and let those on ai.control work 
  automagically
**************************************************************************/
static void auto_settlers_player(struct player *pplayer) 
{
  static struct timer *t = NULL;      /* alloc once, never free */

  t = renew_timer_start(t, TIMER_CPU, TIMER_DEBUG);

  city_list_iterate(pplayer->cities, pcity)
    initialize_infrastructure_cache(pcity); /* saves oodles of time -- Syela */
  city_list_iterate_end;

  pplayer->ai.warmth = WARMING_FACTOR * (game.heating > game.warminglevel ? 2 : 1);

  freelog(LOG_DEBUG, "Warmth = %d, game.globalwarming=%d",
	  pplayer->ai.warmth, game.globalwarming);

  /* Auto-settle with a settler unit if it's under AI control (e.g. human
   * player auto-settler mode) or if the player is an AI.  But don't
   * auto-settle with a unit under orders even for an AI player - these come
   * from the human player and take precedence. */
  unit_list_iterate(pplayer->units, punit) {
    if ((punit->ai.control || pplayer->ai.control)
	&& (unit_flag(punit, F_SETTLERS)
	    || unit_flag(punit, F_CITIES))
	&& !unit_has_orders(punit)) {
      freelog(LOG_DEBUG, "%s's settler at (%d, %d) is ai controlled.",
	      pplayer->name, punit->x, punit->y); 
      if (punit->activity == ACTIVITY_SENTRY) {
	handle_unit_activity_request(punit, ACTIVITY_IDLE);
      }
      if (punit->activity == ACTIVITY_GOTO && punit->moves_left > 0) {
        handle_unit_activity_request(punit, ACTIVITY_IDLE);
      }
      if (punit->activity == ACTIVITY_IDLE) {
	auto_settler_findwork(pplayer, punit);
      }
      freelog(LOG_DEBUG, "Has been processed.");
    }
  }
  unit_list_iterate_end;
  if (timer_in_use(t)) {
    freelog(LOG_VERBOSE, "%s's autosettlers consumed %g milliseconds.",
 	    pplayer->name, 1000.0*read_timer_seconds(t));
  }
}

/************************************************************************** 
  Marks tiles as assigned to a settler. If we are on our way to the tile,
  it is only assigned with respect to our own calculations, ie other
  players' autosettlers may race us to the spot. If we are on the spot,
  the it is marked as assigned for all players.
**************************************************************************/
static void assign_settlers_player(struct player *pplayer)
{
  int i = 1<<pplayer->player_no;
  struct tile *ptile;
  unit_list_iterate(pplayer->units, punit)
    if (unit_flag(punit, F_SETTLERS)
	|| unit_flag(punit, F_CITIES)) {
      if (punit->activity == ACTIVITY_GOTO) {
        ptile = map_get_tile(goto_dest_x(punit), goto_dest_y(punit));
        ptile->assigned = ptile->assigned | i; /* assigned for us only */
      } else {
        ptile = map_get_tile(punit->x, punit->y);
        ptile->assigned = 0xFFFFFFFF; /* assigned for everyone */
      }
    } else {
      ptile = map_get_tile(punit->x, punit->y);
      ptile->assigned = ptile->assigned | (0xFFFFFFFF ^ i); /* assigned for everyone else */
    }
  unit_list_iterate_end;
}

/************************************************************************** 
  Clear previous turn's assignments, then assign autosettlers to uniquely
  to tiles. This prevents autosettlers from messing with each others work.
**************************************************************************/
static void assign_settlers(void)
{
  whole_map_iterate(x, y) {
    map_get_tile(x, y)->assigned = 0;
  } whole_map_iterate_end;

  shuffled_players_iterate(pplayer) {
    assign_settlers_player(pplayer);
  } shuffled_players_iterate_end;
}

/************************************************************************** 
  Assign a region of the map as belonging to a certain player for keeping
  autosettlers out of enemy territory.
**************************************************************************/
static void assign_region(int x, int y, int player_no, int distance, int s)
{
  square_iterate(x, y, distance, x1, y1) {
    if (s == 0 || is_ocean_near_tile(x1, y1)) {
      BV_SET(TERRITORY(x1, y1), player_no);
    }
  } square_iterate_end;
}

/**************************************************************************
  Try to keep autosettlers out of enemy territory. We assign blocks of
  territory to the enemy based on the location of his units and their
  movement.

  FIXME: We totally ignore the possibility of enemies getting to us
  by road or rail. Whatever Syela says, this is just so broken.

  NOTE: Having units with extremely high movement in the game will
  effectively make autosettlers run and hide and never come out again. 
  The cowards.
**************************************************************************/
static void assign_territory_player(struct player *pplayer)
{
  int n = pplayer->player_no;
  unit_list_iterate(pplayer->units, punit)
    if (unit_type(punit)->attack_strength != 0) {
/* I could argue that phalanxes aren't really a threat, but ... */
      if (is_sailing_unit(punit)) {
        assign_region(punit->x, punit->y, n, 1 + unit_type(punit)->move_rate / SINGLE_MOVE, 1);
      } else if (is_ground_unit(punit)) {
        assign_region(punit->x, punit->y, n, 1 + unit_type(punit)->move_rate /
             (unit_flag(punit, F_IGTER) ? 1 : 3), 0);
/* I realize this is not the most accurate, but I don't want to iterate
road networks 100 times/turn, and I can't justifiably abort when I encounter
already assigned territory.  If anyone has a reasonable alternative that won't
noticeably slow the game, feel free to replace this else{}  -- Syela */
      } else {
        assign_region(punit->x, punit->y, n, 1 + unit_type(punit)->move_rate / SINGLE_MOVE, 0);
      } 
    }
  unit_list_iterate_end;
  city_list_iterate(pplayer->cities, pcity)
    assign_region(pcity->x, pcity->y, n, 3, 0);
  city_list_iterate_end;
}

/**************************************************************************
  This function is supposed to keep settlers out of enemy territory
   -- Syela
**************************************************************************/
static void assign_territory(void)
{
  memset(territory, 0, map.xsize * map.ysize * sizeof(*territory));

  players_iterate(pplayer) {
    assign_territory_player(pplayer);
  } players_iterate_end;
  /* An actual territorial assessment a la AI algorithms for go might be
   * appropriate here.  I'm not sure it's necessary, so it's not here yet.
   *  -- Syela
   */
}  


/**************************************************************************
  Recalculate enemies[] table
**************************************************************************/
static void recount_enemy_masks(void)
{
  players_iterate(player1) {
    BV_CLR_ALL(enemies[player1->player_no]);
    players_iterate(player2) {
      if (!pplayers_allied(player1, player2))
        BV_SET(enemies[player1->player_no], player2->player_no);
    } players_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
  Do the auto_settler stuff for all the players. 
**************************************************************************/
void auto_settlers(void)
{
  assign_settlers();
  assign_territory();
  recount_enemy_masks();
  shuffled_players_iterate(pplayer) {
    auto_settlers_player(pplayer);
  } shuffled_players_iterate_end;
}

/**************************************************************************
used to use old crappy formulas for settler want, but now using actual
want!
**************************************************************************/
void contemplate_new_city(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  struct unit *virtualunit;
  int want;
  int gx = 0, gy = 0;
  struct unit *ferryboat = NULL; /* dummy */
  Unit_Type_id unit_type = best_role_unit(pcity, F_CITIES); 

  if (unit_type == U_LAST) {
    freelog(LOG_DEBUG, "No F_CITIES role unit available");
    return;
  }

  /* Create a localized "virtual" unit to do operations with. */
  virtualunit = create_unit_virtual(pplayer, pcity, unit_type, 0);
  virtualunit->x = pcity->x;
  virtualunit->y = pcity->y;
  want = evaluate_city_building(virtualunit, &gx, &gy, &ferryboat);
  free(virtualunit);

  unit_list_iterate(pplayer->units, qpass) {
    /* We want a ferryboat with want 199 */
    if (qpass->ai.ferryboat == pcity->id)
      want = -199;
  } unit_list_iterate_end;

  /* Did we count on using an existing boat.  If yes we need to keep it
   * in the city. */
  pcity->ai.founder_boat = (ferryboat != NULL);

  if (gx == -1) {
    pcity->ai.founder_want = -want;
  } else {
    pcity->ai.founder_want = want; /* boat */
  }
}

/**************************************************************************
used to use old crappy formulas for settler want, but now using actual
want!
**************************************************************************/
void contemplate_terrain_improvements(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  struct unit *virtualunit;
  int want;
  int gx, gy; /* dummies */
  enum unit_activity best_act;
  struct tile *ptile = map_get_tile(pcity->x, pcity->y);
  struct ai_data *ai = ai_data_get(pplayer);
  Unit_Type_id unit_type = best_role_unit(pcity, F_SETTLERS);

  if (unit_type == U_LAST) {
    freelog(LOG_DEBUG, "No F_SETTLERS role unit available");
    return;
  }

  /* Create a localized "virtual" unit to do operations with. */
  virtualunit = create_unit_virtual(pplayer, pcity, unit_type, 0);
  virtualunit->x = pcity->x;
  virtualunit->y = pcity->y;
  want = evaluate_improvements(virtualunit, &best_act, &gx, &gy);
  free(virtualunit);

/* FIXME: AI does not ship F_SETTLERS around, only F_CITIES - Per */
#ifdef AI_SMART
  unit_list_iterate(pplayer->units, qpass) {
    /* We want a ferryboat with want 199 */
    if (qpass->ai.ferryboat == pcity->id)
      want = -199;
  } unit_list_iterate_end; 
#endif

  /* modify our desire based on available statistics to prevent
   * overflooding with worker type units if they come cheap in
   * the ruleset */
  want /= MAX(1, ai->stats.workers[ptile->continent]
                 / MAX(1, ai->stats.cities[ptile->continent]));

  assert(want >= 0);
  pcity->ai.settler_want = want;
}
