/********************************************************************** 
 Freeciv - Copyright (C) 2004 - The Freeciv Project
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

#include "log.h"
#include "player.h"
#include "unit.h"

#include "path_finding.h"
#include "pf_tools.h"

#include "maphand.h"

#include "ailog.h"
#include "aitools.h"

#include "aiexplorer.h"


/**************************************************************************
  Determine if a tile is likely to be water, given information that
  the player actually has. Return the % certainty that it's water
  (100 = certain, 50 = no idea, 0 = certainly not).
**************************************************************************/
static int likely_ocean(struct tile *ptile, struct player *pplayer)
{
  int sum;

  if (map_is_known(ptile, pplayer)) {
    /* we've seen the tile already. */
    return (is_ocean(map_get_terrain(ptile)) ? 100 : 0);
  }
  
  /* Now we're going to do two things at once. We're going to see if
   * we know any cardinally adjacent tiles, since knowing one will
   * give a guaranteed value for the centre tile. Also, we're going
   * to count the non-cardinal (diagonal) tiles, and see how many
   * of them are ocean, which gives a guess for the ocean-ness of 
   * the centre tile. */
  sum = 50;
  adjc_dir_iterate(ptile, ptile1, dir) {
    if (map_is_known(ptile1, pplayer)) {
      if (is_cardinal_dir(dir)) {
	/* If a tile is cardinally adjacent, we can tell if the 
	 * central tile is ocean or not by the appearance of
	 * the adjacent tile. So, given that we can tell, 
	 * it's fair to look at the actual tile. */
        return (is_ocean(map_get_terrain(ptile)) ? 100 : 0);
      } else {
	/* We're diagonal to the tile in question. So we can't
	 * be sure what the central tile is, but the central
	 * tile is likely to be the same as the nearby tiles. 
	 * If all 4 are water, return 90; if all 4 are land, 
	 * return 10. */
        sum += (is_ocean(map_get_terrain(ptile1)) ? 10 : -10);
      }
    }
  } adjc_dir_iterate_end;

  return sum;
}

/***************************************************************
Is a tile likely to be coastline, given information that the 
player actually has.
***************************************************************/
static bool is_likely_coastline(struct tile *ptile, struct player *pplayer)
{
  int likely = 50;
  int t;

  adjc_iterate(ptile, ptile1) {
    if ((t = likely_ocean(ptile1, pplayer)) == 0) {
      return TRUE;
    }
    /* If all t values are 50, likely stays at 50. If all approach zero,
     * ie are unlikely to be ocean, the tile is likely to be coastline, so
     * likely will approach 100. If all approach 100, likely will 
     * approach zero. */
    likely += (50 - t) / 8;
    
  } adjc_iterate_end;

  return (likely > 50);
}

/***************************************************************
Is there a chance that a trireme would be lost, given information that 
the player actually has.
***************************************************************/
static bool is_likely_trireme_loss(struct player *pplayer,
				   struct tile *ptile)
{
  /*
   * If we are in a city or next to land, we have no chance of losing
   * the ship.  To make this really useful for ai planning purposes, we'd
   * need to confirm that we can exist/move at the x,y location we are given.
   */
  if ((likely_ocean(ptile, pplayer) < 50) || 
      is_likely_coastline(ptile, pplayer) ||
      get_player_bonus(pplayer, EFT_NO_SINK_DEEP) > 0) {
    return FALSE;
  } else {
    return TRUE;
  }
}

/**************************************************************************
Return a value indicating how desirable it is to explore the given tile.
In general, we want to discover unknown terrain of the opposite kind to
our natural terrain, i.e. pedestrians like ocean and boats like land.
Even if terrain is known, but of opposite kind, we still want it
-- so that we follow the shoreline.
We also would like discovering tiles which can be harvested by our cities -- 
because that improves citizen placement. We do not currently do this, see
comment below.
**************************************************************************/
#define SAME_TER_SCORE         21
#define DIFF_TER_SCORE         81
#define KNOWN_SAME_TER_SCORE   0
#define KNOWN_DIFF_TER_SCORE   51

/* The maximum number of tiles that the unit might uncover in a move. 
 * #define MAX_NEW_TILES          (1 + 4 * (unit_type(punit)->vision_range))
 * The previous line would be ideal, but we'd like these to be constants
 * for efficiency, so pretend vision_range == 1 */
#define MAX_NEW_TILES          5

/* The number of tiles that the unit can see. =(1 + 2r)^2
 * #define VISION_TILES           (1 + 2 * unit_type(punit)->vision_range)*\
 *                                (1 + 2 * unit_type(punit)->vision_range)
 * As above, set vision_range == 1 */
#define VISION_TILES           9

/* The desirability of the best tile possible without cities or huts. 
 * TER_SCORE is given per 1% of certainty about the terrain, so
 * muliply by 100 to compensate. */
#define BEST_NORMAL_TILE       \
  (100 * MAX_NEW_TILES * DIFF_TER_SCORE +\
   100 * (VISION_TILES - MAX_NEW_TILES) * KNOWN_DIFF_TER_SCORE)

/* We value exploring around our cities just slightly more than exploring
 * tiles fully surrounded by different terrain. */
#define OWN_CITY_SCORE         (BEST_NORMAL_TILE + 1)

/* And we value exploring huts even more than our own cities. */
#define HUT_SCORE              (OWN_CITY_SCORE + 1) 

#define BEST_POSSIBLE_SCORE    (HUT_SCORE + BEST_NORMAL_TILE)

static int explorer_desirable(struct tile *ptile, struct player *pplayer, 
                              struct unit *punit)
{
  int land_score, ocean_score, known_land_score, known_ocean_score;
  int range = unit_type(punit)->vision_range;
  int desirable = 0;
  int unknown = 0;

  /* First do some checks that would make a tile completely non-desirable.
   * If we're a trireme and we could die at the given tile, or if there
   * is a city on the tile, or if the tile is not accessible, or if the 
   * tile is on a different continent, or if we're a barbarian and
   * the tile has a hut, don't go there. */
  if ((unit_flag(punit, F_TRIREME) && 
       is_likely_trireme_loss(pplayer, ptile))
      || map_get_city(ptile)
      || (is_barbarian(pplayer) && map_has_special(ptile, S_HUT))) {
    return 0;
  }

  /* What value we assign to the number of land and water tiles
   * depends on if we're a land or water unit. */
  if (is_ground_unit(punit)) {
    land_score = SAME_TER_SCORE;
    ocean_score = DIFF_TER_SCORE;
    known_land_score = KNOWN_SAME_TER_SCORE;
    known_ocean_score = KNOWN_DIFF_TER_SCORE;
  } else {
    land_score = DIFF_TER_SCORE;
    ocean_score = SAME_TER_SCORE;
    known_land_score = KNOWN_DIFF_TER_SCORE;
    known_ocean_score = KNOWN_SAME_TER_SCORE;
  }

  square_iterate(ptile, range, ptile1) {
    int ocean = likely_ocean(ptile1, pplayer);

    if (!map_is_known(ptile1, pplayer)) {
      unknown++;

      /* FIXME: we should add OWN_CITY_SCORE to desirable if the tile 
       * can be harvested by a city of ours. Just calculating this each
       * time becomes rather expensive. Jason Short suggests:
       * It should be easy to generate this information once, for
       * the entire world.  It can be used by everyone and only 
       * sometimes needs to be recalculated (actually all changes 
       * only require local recalculation, but that could be unstable). */

      desirable += (ocean * ocean_score + (100 - ocean) * land_score);
    } else {
      if(is_tiles_adjacent(ptile, ptile1)) {
	/* we don't value staying offshore from land,
	 * only adjacent. Otherwise destroyers do the wrong thing. */
	desirable += (ocean * known_ocean_score 
                      + (100 - ocean) * known_land_score);
      }
    }
  } square_iterate_end;

  if (unknown <= 0) {
    /* We make sure we'll uncover at least one unexplored tile. */
    desirable = 0;
  }

  if ((!pplayer->ai.control || !ai_handicap(pplayer, H_HUTS))
      && map_is_known(ptile, pplayer)
      && map_has_special(ptile, S_HUT)) {
    /* we want to explore huts whenever we can,
     * even if doing so will not uncover any tiles. */
    desirable += HUT_SCORE;
  }

  return desirable;
}

/**************************************************************************
  Handle eXplore mode of a unit (explorers are always in eXplore mode 
  for AI) - explores unknown territory, finds huts.

  Returns whether there is any more territory to be explored.
**************************************************************************/
bool ai_manage_explorer(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  /* Loop prevention */
  int init_moves = punit->moves_left;

  /* The log of the want of the most desirable tile, 
   * given nearby water, cities, etc. */
  double log_most_desirable = -FC_INFINITY;

  /* The maximum distance we are willing to search. It decreases depending
   * on the want of already discovered tagets.  It is defined as the distance
   * at which a tile with BEST_POSSIBLE_SCORE would have to be found in
   * order to be better than the current most_desirable tile. */
  int max_dist = FC_INFINITY;

  /* Coordinates of most desirable tile. Initialized to make 
   * compiler happy. Also MC to the best tile. */
  struct tile *best_tile = NULL;
  int best_MC = FC_INFINITY;

  /* Path-finding stuff */
  struct pf_map *map;
  struct pf_parameter parameter;

#define DIST_FACTOR   0.6

  double logDF = log(DIST_FACTOR);
  double logBPS = log(BEST_POSSIBLE_SCORE);

  pft_fill_unit_parameter(&parameter, punit);
  parameter.get_TB = no_fights_or_unknown;
  /* When exploring, even AI should pretend to not cheat. */
  parameter.omniscience = FALSE;

  map = pf_create_map(&parameter);
  while (pf_next(map)) {
    int desirable;
    double log_desirable;
    struct pf_position pos;

    pf_next_get_position(map, &pos);
    
    /* Our callback should insure this. */
    assert(map_is_known(pos.tile, pplayer));
    
    desirable = explorer_desirable(pos.tile, pplayer, punit);

    if (desirable <= 0) { 
      /* Totally non-desirable tile. No need to continue. */
      continue;
    }

    /* take the natural log */
    log_desirable = log(desirable);

    /* Ok, the way we calculate goodness is taking the base tile 
     * desirability amortized by the time it takes to get there:
     *
     *     goodness = desirability * DIST_FACTOR^total_MC
     *
     * TODO: JDS notes that we should really make our exponential
     *       term dimensionless by dividing by move_rate.
     * 
     * We want to truncate our search, so we calculate a maximum distance
     * that we would move to find the tile with the most possible desirability
     * (BEST_POSSIBLE_SCORE) that gives us the same goodness as the current
     * tile position we're looking at. Therefore we have:
     *
     *   desirability * DIST_FACTOR^total_MC = 
     *               BEST_POSSIBLE_SCORE * DIST_FACTOR^(max distance)      (1)
     *
     * and then solve for max_dist. We only want to change max_dist when
     * we find a tile that has better goodness than we've found so far; hence
     * the conditional below. It looks cryptic, but all it is is testing which
     * of two goodnesses is bigger after taking the natural log of both sides.
     */
    if (log_desirable + pos.total_MC * logDF 
	> log_most_desirable + best_MC * logDF) {

      log_most_desirable = log_desirable;
      best_tile = pos.tile;
      best_MC = pos.total_MC;

      /* take the natural log and solve equation (1) above.  We round
       * max_dist down (is this correct?). */
      max_dist = best_MC + (log_most_desirable - logBPS)/logDF;
    }

    /* let's not go further than this */
    if (pos.total_MC > max_dist) {
      break;
    }
  }
  pf_destroy_map(map);

  /* Go to the best tile found. */
  if (best_tile != NULL) {
    /* TODO: read the path off the map we made.  Then we can make a path 
     * which goes beside the unknown, with a good EC callback... */
    if (!ai_unit_goto(punit, best_tile)) {
      /* Died?  Strange... */
      return FALSE;
    }
    if (punit->moves_left > 0) {
      /* We can still move on... */
      if (punit->moves_left < init_moves) {
	/* At least we moved (and maybe even got to where we wanted).  
         * Let's do more exploring. 
         * (Checking only whether our position changed is unsafe: can allow
         * yoyoing on a RR) */
	return ai_manage_explorer(punit);          
      } else {
	/* Something went wrong. What to do but return?
	 * Answer: if we're a trireme we could get to this point,
	 * but only with a non-full complement of movement points,
	 * in which case the goto code is simply requesting a
	 * one turn delay (the next tile we would occupy is not safe).
	 * In that case, we should just wait. */
        if (unit_flag(punit, F_TRIREME) 
            && (punit->moves_left != unit_move_rate(punit))) {
          /* we're a trireme with non-full complement of movement points,
           * so wait until next turn. */
          return TRUE;
        }
	return FALSE;
      }
    }
    return TRUE;
  } else {
    /* Didn't find anything. */
    UNIT_LOG(LOG_DEBUG, punit, "failed to explore more");
    return FALSE;
  }
#undef DIST_FACTOR
}

#undef SAME_TER_SCORE
#undef DIFF_TER_SCORE
#undef KNOWN_SAME_TER_SCORE
#undef KNOWN_DIFF_TER_SCORE
#undef OWN_CITY_SCORE
#undef HUT_SCORE
