/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Project
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
#include <string.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "hash.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "shared.h"		/* for MIN() */
#include "support.h"
#include "timing.h"

#include "cm.h"

/*
 * Terms used
 * ==========
 *
 * Primary Stats: food, shields and trade
 *
 * Secondary Stats: luxury, science and gold
 *
 * Happy State: disorder (unhappy), content (!unhappy && !happy) and
 * happy (happy)
 *
 * Combination: A combination is a distribution of workers on the city
 * map. There are several realisations of a certain combination. Each
 * realisation has a different number of specialists. All realisations
 * of a certain combination have the same primary stats.
 *
 * Simple Primary Stats: Primary Stats which are calculated as the sum
 * over all city tiles which are used by a worker.
 *
 * Rough description of the alogrithm
 * ==================================
 *
 * 1) for i in [0..max number of workers]:
 * 2)  list_i = generate all possible combinations with use i workers
 * 3)  list_i = filter list_i to discard combinations which are \
 *              worse than others in list_i
 * 4) best_r = null
 * 5) for c in concatenation of all list_i:
 * 6)   x = best realisation of all possible realisations of c
 * 7)   if fitness(x) > fitness(best_r):
 * 8)      best_r = x
 *
 * Reducing expensive calls
 * ========================
 *
 * As it can seen in the outline above the alogrithm is quite
 * computationally expensive. So we want to avoid calculating information
 * a second or third time. The bottleneck here is generic_city_refresh
 * calls. generic_city_refresh recalculates the city locally. This is
 * a quite complex calculation and it can be expected that with the
 * full implementation of generalized improvements it will become even
 * more expensive. generic_city_refresh will calculate based on the
 * worker allocation, the number of specialists, the government,
 * rates of the player, existing traderoutes, the primary and
 * secondary stats, and also the happy state. Fortunately
 * generic_city_refresh has properties which make it possible to avoid
 * calling it:
 * 
 *  a) the primary stats as returned by generic_city_refresh are always
 *  greater than or equal to the simple primary stats (which can be
 *  computed cheaply).
 *  b) the primary stats as computed by generic_city_refresh only
 *  depends on the simple primary stats. So simple primary stats will
 *  yield same primary stats.
 *  c) the secondary stats as computed by generic_city_refresh only
 *  depend on the trade and the number of specialists.
 *  d) the happy state as computed by generic_city_refresh only
 *  depend on the luxury and the number of workers.
 *
 * a) and b) allow the fast comparison of certain combinations in step
 * 3) above by comparing the simple primary stats of the combinations.
 *
 * b) allows it to only have to call generic_city_refresh one time to
 * yield the primary stats of a certain combination and so also of all
 * its realisations.
 *
 * c) and d) allow the almost complete caching of the secondary stats.
 *
 * Top-down description of the alogrithm
 * ==============================================
 * 
 * Main entry point is cm_query_result which calls optimize_final.
 *
 * optimize_final implements all of the above mentioned steps
 * 1)-8). It will use build_cache3 to do steps 1)-3). It will use
 * find_best_specialist_arrangement to do step 6). optimize_final will
 * also test if the realisation --- which makes the spare workers
 * entertainers --- can meet the requirements for the primary stats. The
 * user given goal can only be satisfied if this test is true.
 *
 * build_cache3 will create all possible combinations for a given
 * city. There are at most 2^MAX_FIELDS_USED possible
 * combinations. Usually the number is smaller because a certain
 * combination is worse than another. Only combinations which have the
 * same number of workers can be compared this way. Example: two
 * combinations which both use 2 tiles/worker. The first one yields
 * (food=3, shield=4, trade=2) the second one (food=3, shield=3,
 * trade=1). The second one will be discarded because it is worse than
 * the first one.
 *
 * find_best_specialist_arrangement will try all realisations for a
 * given combination. It will find the best one (according to the
 * fitness function) and will return this one. It may be the case that
 * no realisation can meet the requirements.
 */

/****************************************************************************
 defines, structs, globals, forward declarations
*****************************************************************************/

#define NUM_PRIMARY_STATS				3

#define OPTIMIZE_FINAL_LOG_LEVEL			LOG_DEBUG
#define OPTIMIZE_FINAL_LOG_LEVEL2			LOG_DEBUG
#define FIND_BEST_SPECIALIST_ARRANGEMENT_LOG_LEVEL	LOG_DEBUG
#define CM_QUERY_RESULT_LOG_LEVEL			LOG_DEBUG
#define CALC_FITNESS_LOG_LEVEL				LOG_DEBUG
#define CALC_FITNESS_LOG_LEVEL2				LOG_DEBUG
#define EXPAND_CACHE3_LOG_LEVEL				LOG_DEBUG

#define SHOW_EXPAND_CACHE3_RESULT                       FALSE
#define SHOW_CACHE_STATS                                FALSE
#define SHOW_TIME_STATS                                 FALSE
#define DISABLE_CACHE3                                  FALSE

#define NUM_SPECIALISTS_ROLES				3
#define MAX_FIELDS_USED	       	                        (CITY_TILES - 1)
#define MAX_COMBINATIONS				150

/*
 * Maps (trade, taxmen) -> (gold_production, gold_surplus)
 * Maps (trade, entertainers) -> (luxury_production, luxury_surplus)
 * Maps (trade, scientists) -> (science_production, science_surplus)
 * Maps (luxury, workers) -> (city_is_in_disorder, city_is_happy)
 */
static struct {
  int allocated_trade, allocated_size, allocated_luxury;

  struct secondary_stat {
    bool is_valid;
    short int production, surplus;
  } *secondary_stats;
  struct city_status {
    bool is_valid, disorder, happy;
  } *city_status;
} cache2;

/* 
 * Contains all combinations. Caches all the data about a city across
 * multiple cm_query_result calls about the same city.
 */
static struct {
  int fields_available_total;

  struct {
    struct combination {
      bool is_valid;
      int max_scientists, max_taxmen, worker;
      int production2[NUM_PRIMARY_STATS];
      enum city_tile_type worker_positions[CITY_MAP_SIZE][CITY_MAP_SIZE];

      /* 
       * Cache1 maps scientists and taxmen to result for a certain
       * combination.
       */
      struct cm_result *cache1;
      struct cm_result all_entertainer;
    } combinations[MAX_COMBINATIONS];
  } results[MAX_FIELDS_USED + 1];

  struct city *pcity;
} cache3;

/*
 * Misc statistic to analyze performance.
 */
static struct {
  struct timer *wall_timer;
  int queries;
  struct cache_stats {
    int hits, misses;
  } cache1, cache2, cache3;
} stats;

/*
 * Cached results of city_get_{food,trade,shield}_tile calls. Indexed
 * by city map.
 */
struct tile_stats {
  struct {
    short int stats[NUM_PRIMARY_STATS];
    short int is_valid;
  } tiles[CITY_MAP_SIZE][CITY_MAP_SIZE];
};

#define my_city_map_iterate(pcity, cx, cy) {                           \
  city_map_checked_iterate(pcity->x, pcity->y, cx, cy, map_x, map_y) { \
    if(!is_city_center(cx, cy)) {

#define my_city_map_iterate_end \
    }                                \
  } city_map_checked_iterate_end;    \
}

/****************************************************************************
 * implementation of utility functions (these are relatively independent
 * of the algorithms used)
 ****************************************************************************/

/****************************************************************************
 Returns the number of workers of the given result. The given result
 has to be a result for the given city.
*****************************************************************************/
static int count_worker(struct city *pcity,
			const struct cm_result *const result)
{
  int worker = 0;

  my_city_map_iterate(pcity, x, y) {
    if (result->worker_positions_used[x][y]) {
      worker++;
    }
  } my_city_map_iterate_end;

  return worker;
}

/****************************************************************************
 Returns the number of valid combinations which use the given number
 of fields/tiles.
*****************************************************************************/
static int count_valid_combinations(int fields_used)
{
  int i, result = 0;

  for (i = 0; i < MAX_COMBINATIONS; i++) {
    struct combination *current =
	&cache3.results[fields_used].combinations[i];

    if (current->is_valid) {
      result++;
    }
  }
  return result;
}

/****************************************************************************
 Returns TRUE iff the given field can be used for a worker.
*****************************************************************************/
static bool can_field_be_used_for_worker(struct city *pcity, int x, int y)
{
#if 0
  enum known_type known;
#endif
  int map_x, map_y;
  bool is_real;

  assert(is_valid_city_coords(x, y));

  if (pcity->city_map[x][y] == C_TILE_WORKER) {
    return TRUE;
  }

  if (pcity->city_map[x][y] == C_TILE_UNAVAILABLE) {
    return FALSE;
  }

  is_real = city_map_to_map(&map_x, &map_y, pcity, x, y);
  assert(is_real);

#if 0
  // FIXME
  known = tile_get_known(map_x, map_y);
  assert(known == TILE_KNOWN);
#endif

  return TRUE;
}

/****************************************************************************
 Returns TRUE iff is the result has the required surplus and the city
 isn't in disorder and the city is happy if this is required.
*****************************************************************************/
static bool is_valid_result(const struct cm_parameter *const parameter,
			    const struct cm_result *const result)
{
  int i;

  if (!parameter->allow_specialists
      && (result->specialists[SP_ELVIS] + result->specialists[SP_SCIENTIST]
	  + result->specialists[SP_TAXMAN]) >
      MAX(0,cache3.pcity->size - cache3.fields_available_total)) {
    return FALSE;
  }

  if (!parameter->allow_disorder && result->disorder) {
    return FALSE;
  }
  if (parameter->require_happy && !result->happy) {
    return FALSE;
  }

  for (i = 0; i < NUM_STATS; i++) {
    if (result->surplus[i] < parameter->minimal_surplus[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

/****************************************************************************
 Print the current state of the given city via
 freelog(LOG_NORMAL,...).
*****************************************************************************/
static void print_city(struct city *pcity)
{
  freelog(LOG_NORMAL, "print_city(city='%s'(id=%d))",
	  pcity->name, pcity->id);
  freelog(LOG_NORMAL,
	  "  size=%d, entertainers=%d, scientists=%d, taxmen=%d",
	  pcity->size, pcity->specialists[SP_ELVIS],
	  pcity->specialists[SP_SCIENTIST],
	  pcity->specialists[SP_TAXMAN]);
  freelog(LOG_NORMAL, "  workers at:");
  my_city_map_iterate(pcity, x, y) {
    if (pcity->city_map[x][y] == C_TILE_WORKER) {
      freelog(LOG_NORMAL, "    (%2d,%2d)", x, y);
    }
  } my_city_map_iterate_end;

  freelog(LOG_NORMAL, "  food    = %3d (%+3d)",
	  pcity->food_prod, pcity->food_surplus);
  freelog(LOG_NORMAL, "  shield  = %3d (%+3d)",
	  pcity->shield_prod + pcity->shield_waste, pcity->shield_prod);
  freelog(LOG_NORMAL, "  trade   = %3d (%+3d)",
	  pcity->trade_prod + pcity->corruption, pcity->trade_prod);

  freelog(LOG_NORMAL, "  gold    = %3d (%+3d)", pcity->tax_total,
	  city_gold_surplus(pcity));
  freelog(LOG_NORMAL, "  luxury  = %3d", pcity->luxury_total);
  freelog(LOG_NORMAL, "  science = %3d", pcity->science_total);
}

/****************************************************************************
 Print the given result via freelog(LOG_NORMAL,...). The given result
 has to be a result for the given city.
*****************************************************************************/
static void print_result(struct city *pcity,
			 const struct cm_result *const result)
{
  int y, i, worker = count_worker(pcity, result);

  freelog(LOG_NORMAL, "print_result(result=%p)", result);
  freelog(LOG_NORMAL,
	  "print_result:  found_a_valid=%d disorder=%d happy=%d",
	  result->found_a_valid, result->disorder, result->happy);
#if UNUSED
  freelog(LOG_NORMAL, "print_result:  workers at:");
  my_city_map_iterate(pcity, x, y) {
    if (result->worker_positions_used[x][y]) {
      freelog(LOG_NORMAL, "print_result:    (%2d,%2d)", x, y);
    }
  } my_city_map_iterate_end;
#endif

  for (y = 0; y < CITY_MAP_SIZE; y++) {
    char line[CITY_MAP_SIZE + 1];
    int x;

    line[CITY_MAP_SIZE] = 0;

    for (x = 0; x < CITY_MAP_SIZE; x++) {
      if (!is_valid_city_coords(x, y)) {
	line[x] = '-';
      } else if (is_city_center(x, y)) {
	line[x] = 'c';
      } else if (result->worker_positions_used[x][y]) {
	line[x] = 'w';
      } else {
	line[x] = '.';
      }
    }
    freelog(LOG_NORMAL, "print_result: %s", line);
  }

  freelog(LOG_NORMAL,
	  "print_result:  people: W/E/S/T %d/%d/%d/%d",
	  worker, result->specialists[SP_ELVIS],
	  result->specialists[SP_SCIENTIST],
	  result->specialists[SP_TAXMAN]);

  for (i = 0; i < NUM_STATS; i++) {
    freelog(LOG_NORMAL,
	    "print_result:  %10s production=%d surplus=%d",
	    cm_get_stat_name(i), result->production[i],
	    result->surplus[i]);
  }
}

/****************************************************************************
 Print the given combination via freelog(LOG_NORMAL,...). The given
 combination has to be a result for the given city.
*****************************************************************************/
static void print_combination(struct city *pcity,
			      struct combination *combination)
{
  assert(combination->is_valid);

  freelog(LOG_NORMAL, "combination:  workers at:");
  my_city_map_iterate(pcity, x, y) {
    if (combination->worker_positions[x][y] == C_TILE_WORKER) {
      freelog(LOG_NORMAL, "combination:    (%2d,%2d)", x, y);
    }
  } my_city_map_iterate_end;

  freelog(LOG_NORMAL,
	  "combination:  food=%d shield=%d trade=%d",
	  combination->production2[FOOD], combination->production2[SHIELD],
	  combination->production2[TRADE]);
}

/****************************************************************************
 Copy the current production stats and happy status of the given city
 to the result.
*****************************************************************************/
static void copy_stats(struct city *pcity, struct cm_result *result)
{
  result->production[FOOD] = pcity->food_prod;
  result->production[SHIELD] = pcity->shield_prod + pcity->shield_waste;
  result->production[TRADE] = pcity->trade_prod + pcity->corruption;

  result->surplus[FOOD] = pcity->food_surplus;
  result->surplus[SHIELD] = pcity->shield_surplus;
  result->surplus[TRADE] = pcity->trade_prod;

  result->production[GOLD] = pcity->tax_total;
  result->production[LUXURY] = pcity->luxury_total;
  result->production[SCIENCE] = pcity->science_total;

  result->surplus[GOLD] = city_gold_surplus(pcity);
  result->surplus[LUXURY] = result->production[LUXURY];
  result->surplus[SCIENCE] = result->production[SCIENCE];

  result->disorder = city_unhappy(pcity);
  result->happy = city_happy(pcity);
}

/****************************************************************************
 Wraps the array access to cache2.secondary_stats.
*****************************************************************************/
static struct secondary_stat *get_secondary_stat(int trade, int specialists,
						 enum specialist_type
						 specialist_type)
{
  freelog(LOG_DEBUG, "second: trade=%d spec=%d type=%d", trade, specialists,
	  specialist_type);

  assert(trade >= 0 && trade < cache2.allocated_trade);
  assert(specialists >= 0 && specialists < cache2.allocated_size);

  return &cache2.secondary_stats[NUM_SPECIALISTS_ROLES *
				 (cache2.allocated_size * trade +
				  specialists) + specialist_type];
}

/****************************************************************************
 Wraps the array access to cache2.city_status.
*****************************************************************************/
static struct city_status *get_city_status(int luxury, int workers)
{
  freelog(LOG_DEBUG, "status: lux=%d worker=%d", luxury, workers);

  assert(luxury >=0 && luxury < cache2.allocated_luxury);
  assert(workers >= 0 && workers < cache2.allocated_size);

  return &cache2.city_status[cache2.allocated_size * luxury + workers];
}

/****************************************************************************
 Update the cache2 according to the filled out result. If the info is
 already in the cache check that the two match.
*****************************************************************************/
static void update_cache2(struct city *pcity,
			  const struct cm_result *const result)
{
  struct secondary_stat *p;
  struct city_status *q;

  /*
   * Science is set to 0 if the city is unhappy/in disorder. See
   * unhappy_city_check.
   */
  if (!result->disorder) {
    p = get_secondary_stat(result->production[TRADE],
			   result->specialists[SP_SCIENTIST],
			   SP_SCIENTIST);
    if (!p->is_valid) {
      p->production = result->production[SCIENCE];
      p->surplus = result->surplus[SCIENCE];
      p->is_valid = TRUE;
    } else {
      assert(p->production == result->production[SCIENCE] &&
	     p->surplus == result->surplus[SCIENCE]);
    }
  }

  /*
   * Gold is set to 0 if the city is unhappy/in disorder. See
   * unhappy_city_check.
   */
  if (!result->disorder) {
    p = get_secondary_stat(result->production[TRADE],
			   result->specialists[SP_TAXMAN],
			   SP_TAXMAN);
    if (!p->is_valid && !result->disorder) {
      p->production = result->production[GOLD];
      p->surplus = result->surplus[GOLD];
      p->is_valid = TRUE;
    } else {
      assert(p->production == result->production[GOLD] &&
	     p->surplus == result->surplus[GOLD]);
    }
  }

  p = get_secondary_stat(result->production[TRADE],
			 result->specialists[SP_ELVIS],
			 SP_ELVIS);
  if (!p->is_valid) {
    p->production = result->production[LUXURY];
    p->surplus = result->surplus[LUXURY];
    p->is_valid = TRUE;
  } else {
    if (!result->disorder) {
      assert(p->production == result->production[LUXURY] &&
	     p->surplus == result->surplus[LUXURY]);
    }
  }

  q = get_city_status(result->production[LUXURY],
		      count_worker(pcity, result));
  if (!q->is_valid) {
    q->disorder = result->disorder;
    q->happy = result->happy;
    q->is_valid = TRUE;
  } else {
    assert(q->disorder == result->disorder && q->happy == result->happy);
  }
}

/****************************************************************************
...
*****************************************************************************/
static void clear_cache(void)
{
  int i, j;
  for (i = 0; i < MAX_FIELDS_USED + 1; i++) {
    for (j = 0; j < MAX_COMBINATIONS; j++) {
      if (!cache3.results[i].combinations[j].is_valid) {
	continue;
      }
      if (cache3.results[i].combinations[j].cache1) {
	free(cache3.results[i].combinations[j].cache1);
	cache3.results[i].combinations[j].cache1 = NULL;
      }
    }
  }
  cache3.pcity = NULL;
}

/****************************************************************************
 Uses worker_positions_used, entertainers, scientists and taxmen to
 get the remaining stats.
*****************************************************************************/
static void real_fill_out_result(struct city *pcity,
				 struct cm_result *result)
{
  int worker = count_worker(pcity, result);
  struct city backup;

  freelog(LOG_DEBUG, "real_fill_out_result(city='%s'(%d))", pcity->name,
	  pcity->id);

  /* Do checks */
  if (pcity->size !=
      (worker + result->specialists[SP_ELVIS]
       + result->specialists[SP_SCIENTIST]
       + result->specialists[SP_TAXMAN])) {
    print_city(pcity);
    print_result(pcity, result);
    assert(0);
  }

  /* Backup */
  memcpy(&backup, pcity, sizeof(struct city));

  /* Set new state */
  my_city_map_iterate(pcity, x, y) {
    if (pcity->city_map[x][y] == C_TILE_WORKER) {
      pcity->city_map[x][y] = C_TILE_EMPTY;
    }
  } my_city_map_iterate_end;

  my_city_map_iterate(pcity, x, y) {
    if (result->worker_positions_used[x][y]) {
      pcity->city_map[x][y] = C_TILE_WORKER;
    }
  } my_city_map_iterate_end;

  pcity->specialists[SP_ELVIS] = result->specialists[SP_ELVIS];
  pcity->specialists[SP_SCIENTIST] = result->specialists[SP_SCIENTIST];
  pcity->specialists[SP_TAXMAN] = result->specialists[SP_TAXMAN];

  /* Do a local recalculation of the city */
  generic_city_refresh(pcity, FALSE, NULL);

  copy_stats(pcity, result);

  /* Restore */
  memcpy(pcity, &backup, sizeof(struct city));

  freelog(LOG_DEBUG, "xyz: w=%d e=%d s=%d t=%d trade=%d "
	  "sci=%d lux=%d tax=%d dis=%s happy=%s",
	  count_worker(pcity, result), result->specialists[SP_ELVIS],
	  result->specialists[SP_SCIENTIST], result->specialists[SP_TAXMAN],
	  result->production[TRADE],
	  result->production[SCIENCE],
	  result->production[LUXURY],
	  result->production[GOLD],
	  result->disorder ? "yes" : "no", result->happy ? "yes" : "no");
  update_cache2(pcity, result);
}

/****************************************************************************
 Estimates the fitness of the given result with respect to the given
 parameters. Will fill out major fitnes and minor fitness.

 The minor fitness should be used if the major fitness are equal.
*****************************************************************************/
static void calc_fitness(struct city *pcity,
			 const struct cm_parameter *const parameter,
			 const struct cm_result *const result,
			 int *major_fitness, int *minor_fitness)
{
  int i;

  *major_fitness = 0;
  *minor_fitness = 0;

  for (i = 0; i < NUM_STATS; i++) {
    int base;
    if (parameter->factor_target == FT_SURPLUS) {
      base = result->surplus[i];
    } else if (parameter->factor_target == FT_EXTRA) {
      base = parameter->minimal_surplus[i] - result->surplus[i];
    } else {
      base = 0;
      assert(0);
    }

    *major_fitness += base * parameter->factor[i];
    *minor_fitness += result->surplus[i];
  }

  if (result->happy) {
    *major_fitness += parameter->happy_factor;
  }

  freelog(CALC_FITNESS_LOG_LEVEL2, "calc_fitness()");
  freelog(CALC_FITNESS_LOG_LEVEL,
	  "calc_fitness:   surplus={food=%d, shields=%d, trade=%d",
	  result->surplus[FOOD], result->surplus[SHIELD],
	  result->surplus[TRADE]);
  freelog(CALC_FITNESS_LOG_LEVEL,
	  "calc_fitness:     tax=%d, luxury=%d, science=%d}",
	  result->surplus[GOLD], result->surplus[LUXURY],
	  result->surplus[SCIENCE]);
  freelog(CALC_FITNESS_LOG_LEVEL2,
	  "calc_fitness:   factor={food=%d, shields=%d, trade=%d",
	  parameter->factor[FOOD], parameter->factor[SHIELD],
	  parameter->factor[TRADE]);
  freelog(CALC_FITNESS_LOG_LEVEL2,
	  "calc_fitness:     tax=%d, luxury=%d, science=%d}",
	  parameter->factor[GOLD], parameter->factor[LUXURY],
	  parameter->factor[SCIENCE]);
  freelog(CALC_FITNESS_LOG_LEVEL,
	  "calc_fitness: fitness = %d, minor_fitness=%d", *major_fitness,
	  *minor_fitness);
}


/****************************************************************************
 Prints the data of one cache_stats struct.  Called only if the define
 is on, so compile only then or else we get a warning about being
 unused.
*****************************************************************************/
#if SHOW_CACHE_STATS
static void report_one_cache_stat(struct cache_stats *cache_stat,
				  const char *cache_name)
{
  int total = cache_stat->hits + cache_stat->misses, per_mill;

  if (total != 0) {
    per_mill = (cache_stat->hits * 1000) / total;
  } else {
    per_mill = 0;
  }
  freelog(LOG_NORMAL,
	  "CM: %s: hits=%3d.%d%% misses=%3d.%d%% total=%d",
	  cache_name,
	  per_mill / 10, per_mill % 10, (1000 - per_mill) / 10,
	  (1000 - per_mill) % 10, total);
}
#endif


/****************************************************************************
 Prints the data of the stats struct via freelog(LOG_NORMAL,...).
*****************************************************************************/
static void report_stats(void)
{
#if SHOW_TIME_STATS
  freelog(LOG_NORMAL, "CM: overall=%fs queries=%d %fms / query",
	  read_timer_seconds(stats.wall_timer), stats.queries,
	  (1000.0 * read_timer_seconds(stats.wall_timer)) /
	  ((double) stats.queries));
#endif

#if SHOW_CACHE_STATS
  report_one_cache_stat(&stats.cache1, "CACHE1");
  report_one_cache_stat(&stats.cache2, "CACHE2");
  report_one_cache_stat(&stats.cache3, "CACHE3");
#endif
}

/****************************************************************************
                           algorithmic functions
*****************************************************************************/

/****************************************************************************
 Frontend cache for real_fill_out_result. This method tries to avoid
 calling real_fill_out_result by all means.
*****************************************************************************/
static void fill_out_result(struct city *pcity, struct cm_result *result,
			    struct combination *base_combination,
			    int scientists, int taxmen)
{
  struct cm_result *slot;
  bool got_all;

  assert(base_combination->is_valid);

  /*
   * First try to get a filled out result from cache1 or from the
   * all_entertainer result.
   */
  if (scientists == 0 && taxmen == 0) {
    slot = &base_combination->all_entertainer;
  } else {
    assert(scientists <= base_combination->max_scientists);
    assert(taxmen <= base_combination->max_taxmen);
    assert(base_combination->cache1 != NULL);
    assert(base_combination->all_entertainer.found_a_valid);

    slot = &base_combination->cache1[scientists *
				     (base_combination->max_taxmen + 1) +
				     taxmen];
  }

  freelog(LOG_DEBUG,
	  "fill_out_result(base_comb=%p (w=%d), scientists=%d, taxmen=%d) %s",
	  base_combination, base_combination->worker, scientists,
	  taxmen, slot->found_a_valid ? "CACHED" : "unknown");

  if (slot->found_a_valid) {
    /* Cache1 contains the result */
    stats.cache1.hits++;
    memcpy(result, slot, sizeof(struct cm_result));
    return;
  }
  stats.cache1.misses++;

  my_city_map_iterate(pcity, x, y) {
    result->worker_positions_used[x][y] =
	(base_combination->worker_positions[x][y] == C_TILE_WORKER);
  } my_city_map_iterate_end;

  result->specialists[SP_SCIENTIST] = scientists;
  result->specialists[SP_TAXMAN] = taxmen;
  result->specialists[SP_ELVIS] =
      pcity->size - (base_combination->worker + scientists + taxmen);

  freelog(LOG_DEBUG,
	  "fill_out_result(city='%s'(%d), entrt.s=%d, scien.s=%d, taxmen=%d)",
	  pcity->name, pcity->id, result->specialists[SP_ELVIS],
	  result->specialists[SP_SCIENTIST], result->specialists[SP_TAXMAN]);

  /* try to fill result from cache2 */
  if (!base_combination->all_entertainer.found_a_valid) {
    got_all = FALSE;
  } else {
    struct secondary_stat *p;
    struct city_status *q;
    int i;

    got_all = TRUE;

    /*
     * fill out the primary stats that are known from the
     * all_entertainer result
     */
    for (i = 0; i < NUM_PRIMARY_STATS; i++) {
      result->production[i] =
	  base_combination->all_entertainer.production[i];
      result->surplus[i] = base_combination->all_entertainer.surplus[i];
    }

    p = get_secondary_stat(result->production[TRADE],
			   result->specialists[SP_SCIENTIST],
			   SP_SCIENTIST);
    if (!p->is_valid) {
      got_all = FALSE;
    } else {
      result->production[SCIENCE] = p->production;
      result->surplus[SCIENCE] = p->surplus;
    }

    p = get_secondary_stat(result->production[TRADE],
			   result->specialists[SP_TAXMAN],
			   SP_TAXMAN);
    if (!p->is_valid) {
      got_all = FALSE;
    } else {
      result->production[GOLD] = p->production;
      result->surplus[GOLD] = p->surplus;
    }

    p = get_secondary_stat(result->production[TRADE],
			   result->specialists[SP_ELVIS],
			   SP_ELVIS);
    if (!p->is_valid) {
      got_all = FALSE;
    } else {
      result->production[LUXURY] = p->production;
      result->surplus[LUXURY] = p->surplus;
    }

    q = get_city_status(result->production[LUXURY],
			base_combination->worker);
    if (!q->is_valid) {
      got_all = FALSE;
    } else {
      result->disorder = q->disorder;
      result->happy = q->happy;
    }
  }

  if (got_all) {
    /*
     * All secondary stats and the city status have been filled from
     * cache2.
     */

    stats.cache2.hits++;
    memcpy(slot, result, sizeof(struct cm_result));
    slot->found_a_valid = TRUE;
    return;
  }

  stats.cache2.misses++;

  /*
   * Result can't be constructed from caches. Do the slow
   * re-calculation.
   */
  real_fill_out_result(pcity, result);

  /* Update cache1 */
  memcpy(slot, result, sizeof(struct cm_result));
  slot->found_a_valid = TRUE;
}


/****************************************************************************
 The given combination is added only if no other better combination is
 already known. add_combination will also remove any combination which
 may have become worse than the inserted.
*****************************************************************************/
static void add_combination(int fields_used,
			    struct combination *combination)
{
  static int max_used = 0;
  int i, used;
  /* This one is cached for later. Avoids another loop. */
  struct combination *invalid_slot_for_insert = NULL;

  /* Try to find a better combination. */
  for (i = 0; i < MAX_COMBINATIONS; i++) {
    struct combination *current =
	&cache3.results[fields_used].combinations[i];

    if (!current->is_valid) {
      if (!invalid_slot_for_insert) {
	invalid_slot_for_insert = current;
      }
      continue;
    }

    if (current->production2[FOOD] >= combination->production2[FOOD] &&
	current->production2[SHIELD] >= combination->production2[SHIELD] &&
	current->production2[TRADE] >= combination->production2[TRADE]) {
      /*
         freelog(LOG_NORMAL, "found a better combination:");
         print_combination(current);
       */
      return;
    }
  }

  /*
   * There is no better combination. Remove any combinations which are
   * worse than the given.
   */

  /*
     freelog(LOG_NORMAL, "add_combination()");
     print_combination(combination);
   */

  for (i = 0; i < MAX_COMBINATIONS; i++) {
    struct combination *current =
	&cache3.results[fields_used].combinations[i];

    if (!current->is_valid) {
      continue;
    }

    if (current->production2[FOOD] <= combination->production2[FOOD] &&
	current->production2[SHIELD] <= combination->production2[SHIELD] &&
	current->production2[TRADE] <= combination->production2[TRADE]) {
      /*
         freelog(LOG_NORMAL, "the following is now obsolete:");
         print_combination(current);
       */
      current->is_valid = FALSE;
    }
  }

  /* Insert the given combination. */
  if (invalid_slot_for_insert == NULL) {
    freelog(LOG_FATAL,
	    "No more free combinations left. You may increase "
	    "MAX_COMBINATIONS or \nreport this error to "
	    "freeciv-dev@freeciv.org.\nCurrent MAX_COMBINATIONS=%d",
	    MAX_COMBINATIONS);
    exit(EXIT_FAILURE);
  }

  memcpy(invalid_slot_for_insert, combination, sizeof(struct combination));
  invalid_slot_for_insert->all_entertainer.found_a_valid = FALSE;
  invalid_slot_for_insert->cache1 = NULL;

  used = count_valid_combinations(fields_used);
  if (used > (MAX_COMBINATIONS * 9) / 10
      && (used > max_used || max_used == 0)) {
    max_used = used;
    freelog(LOG_ERROR,
	    "Warning: there are currently %d out of %d combinations used",
	    used, MAX_COMBINATIONS);
  }

  freelog(LOG_DEBUG, "there are now %d combination which use %d tiles",
	  count_valid_combinations(fields_used), fields_used);
}

/****************************************************************************
 Will create combinations which use (fields_to_use) fields from the
 combinations which use (fields_to_use-1) fields.
*****************************************************************************/
static void expand_cache3(struct city *pcity, int fields_to_use,
			  const struct tile_stats *const stats)
{
  int i;

  freelog(EXPAND_CACHE3_LOG_LEVEL,
	  "expand_cache3(fields_to_use=%d) results[%d] "
	  "has %d valid combinations",
	  fields_to_use, fields_to_use - 1,
	  count_valid_combinations(fields_to_use - 1));

  for (i = 0; i < MAX_COMBINATIONS; i++) {
    cache3.results[fields_to_use].combinations[i].is_valid = FALSE;
  }

  for (i = 0; i < MAX_COMBINATIONS; i++) {
    struct combination *current =
	&cache3.results[fields_to_use - 1].combinations[i];

    if (!current->is_valid) {
      continue;
    }

    my_city_map_iterate(pcity, x, y) {
      struct combination new_pc;

      if (current->worker_positions[x][y] != C_TILE_EMPTY) {
	continue;
      }

      memcpy(&new_pc, current, sizeof(struct combination));
      assert(stats->tiles[x][y].is_valid);
      new_pc.production2[FOOD] += stats->tiles[x][y].stats[FOOD];
      new_pc.production2[SHIELD] += stats->tiles[x][y].stats[SHIELD];
      new_pc.production2[TRADE] += stats->tiles[x][y].stats[TRADE];

      new_pc.worker_positions[x][y] = C_TILE_WORKER;
      new_pc.worker = fields_to_use;
      add_combination(fields_to_use, &new_pc);
    } my_city_map_iterate_end;
  }

  freelog(EXPAND_CACHE3_LOG_LEVEL,
	  "expand_cache3(fields_to_use=%d): %d valid combinations",
	  fields_to_use, count_valid_combinations(fields_to_use));

  if (SHOW_EXPAND_CACHE3_RESULT) {
    for (i = 0; i < MAX_COMBINATIONS; i++) {
      struct combination *current =
	  &cache3.results[fields_to_use].combinations[i];

      if (!current->is_valid) {
	continue;
      }

      print_combination(pcity, current);
    }
  }
}

/****************************************************************************
 Expand the secondary_stats and city_status fields of cache2 if this
 is necessary. For this the function tries to estimate the upper limit
 of trade and luxury. It will also invalidate cache2.
*****************************************************************************/
static void ensure_invalid_cache2(struct city *pcity, int total_tile_trade)
{
  bool change_size = FALSE;
  int backup,i, luxury, total_trade = total_tile_trade;

  /* Hack since trade_between_cities accesses pcity->tile_trade */
  backup = pcity->tile_trade;
  pcity->tile_trade = total_tile_trade;
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    struct city *pc2 = find_city_by_id(pcity->trade[i]);

    total_trade += trade_between_cities(pcity, pc2);
  }
  pcity->tile_trade = backup;

  /*
   * Estimate an upper limit for the luxury. We assume that the player
   * has set the luxury rate to 100%. There are two extremal cases: all
   * citizen are entertainers (yielding a luxury of "(pcity->size * 2
   * * get_city_tax_bonus(pcity))/100" = A) or all citizen are
   * working on tiles and the resulting trade is converted to luxury
   * (yielding a luxury of "(total_trade * get_city_tax_bonus(pcity))
   * / 100" = B) . We can't use MAX(A, B) since there may be cases in
   * between them which are better than these two exremal cases. So we
   * use A+B as upper limit.
   */
  luxury =
      ((pcity->size * 2 + total_trade) * get_city_tax_bonus(pcity)) / 100;

  /* +1 because we want to index from 0 to pcity->size inclusive */
  if (pcity->size + 1 > cache2.allocated_size) {
    cache2.allocated_size = pcity->size + 1;
    change_size = TRUE;
  }

  if (total_trade + 1 > cache2.allocated_trade) {
    cache2.allocated_trade = total_trade + 1;
    change_size = TRUE;
  }

  if (luxury + 1 > cache2.allocated_luxury) {
    cache2.allocated_luxury = luxury + 1;
    change_size = TRUE;
  }

  if (change_size) {
    freelog(LOG_DEBUG,
	    "CM: expanding cache2 to size=%d, trade=%d, luxury=%d",
	    cache2.allocated_size, cache2.allocated_trade,
	    cache2.allocated_luxury);
    if (cache2.secondary_stats) {
      free(cache2.secondary_stats);
      cache2.secondary_stats = NULL;
    }
    cache2.secondary_stats =
	fc_malloc(cache2.allocated_trade * cache2.allocated_size *
		  NUM_SPECIALISTS_ROLES * sizeof(struct secondary_stat));

    if (cache2.city_status) {
      free(cache2.city_status);
      cache2.city_status = NULL;
    }
    cache2.city_status =
	fc_malloc(cache2.allocated_luxury * cache2.allocated_size *
		  sizeof(struct city_status));
  }

  /* Make cache2 invalid */
  memset(cache2.secondary_stats, 0,
	 cache2.allocated_trade * cache2.allocated_size *
	 NUM_SPECIALISTS_ROLES * sizeof(struct secondary_stat));
  memset(cache2.city_status, 0,
	 cache2.allocated_luxury * cache2.allocated_size *
	 sizeof(struct city_status));
}
/****************************************************************************
 Setup. Adds the root combination (the combination which doesn't use
 any worker but the production of the city center). Incrementaly calls
 expand_cache3.
*****************************************************************************/
static void build_cache3(struct city *pcity)
{
  struct combination root_combination;
  int i, j, total_tile_trade;
  struct tile_stats tile_stats;
  bool is_celebrating = base_city_celebrating(pcity);

  if (cache3.pcity != pcity) {
    cache3.pcity = NULL;
  } else {
    stats.cache3.hits++;
    return;
  }

  cache3.pcity = pcity;
  stats.cache3.misses++;

  /* Make as invalid the parts of cache3 we'll use. */
  for (i = 0; i < MIN(pcity->size, MAX_FIELDS_USED) + 1; i++) {
    for (j = 0; j < MAX_COMBINATIONS; j++) {
      cache3.results[i].combinations[j].is_valid = FALSE;
      if (cache3.results[i].combinations[j].cache1) {
        free(cache3.results[i].combinations[j].cache1);
        cache3.results[i].combinations[j].cache1 = NULL;
      }
    }
  }

  /*
   * Construct root combination. Update
   * cache3.fields_available_total. Fill tile_stats.
   */
  root_combination.worker = 0;
  root_combination.production2[FOOD] =
      base_city_get_food_tile(CITY_MAP_RADIUS, CITY_MAP_RADIUS,
			      pcity, is_celebrating);
  root_combination.production2[SHIELD] =
      base_city_get_shields_tile(CITY_MAP_RADIUS, CITY_MAP_RADIUS, 
			      pcity, is_celebrating);
  root_combination.production2[TRADE] =
      base_city_get_trade_tile(CITY_MAP_RADIUS, CITY_MAP_RADIUS, 
			     pcity, is_celebrating);

  total_tile_trade = root_combination.production2[TRADE];

  cache3.fields_available_total = 0;

  memset(&tile_stats, 0, sizeof(tile_stats));

  my_city_map_iterate(pcity, x, y) {
    tile_stats.tiles[x][y].is_valid = TRUE;
    tile_stats.tiles[x][y].stats[FOOD] =
	base_city_get_food_tile(x, y, pcity, is_celebrating);
    tile_stats.tiles[x][y].stats[SHIELD] =
	base_city_get_shields_tile(x, y, pcity, is_celebrating);
    tile_stats.tiles[x][y].stats[TRADE] =
	base_city_get_trade_tile(x, y, pcity, is_celebrating);

    if (can_field_be_used_for_worker(pcity, x, y)) {
      cache3.fields_available_total++;
      root_combination.worker_positions[x][y] = C_TILE_EMPTY;
      total_tile_trade += tile_stats.tiles[x][y].stats[TRADE];
    } else {
      root_combination.worker_positions[x][y] = C_TILE_UNAVAILABLE;
    }
  } my_city_map_iterate_end;

  /* Add root combination. */
  root_combination.is_valid = TRUE;
  add_combination(0, &root_combination);

  for (i = 1; i <= MIN(cache3.fields_available_total, pcity->size); i++) {
    expand_cache3(pcity, i, &tile_stats);
  }

  ensure_invalid_cache2(pcity, total_tile_trade);
}

/****************************************************************************
 Creates all realisations of the given combination. Finds the best one.
*****************************************************************************/
static void find_best_specialist_arrangement(struct city *pcity, const struct cm_parameter
					     *const parameter, struct combination
					     *base_combination, struct cm_result
					     *best_result,
					     int *best_major_fitness,
					     int *best_minor_fitness)
{
  int worker = base_combination->worker;
  int specialists = pcity->size - worker;
  int scientists, taxmen;

  if (!base_combination->cache1) {

    /* setup cache1 */

    int i, items;

    if (city_can_use_specialist(pcity, SP_SCIENTIST)) {
      base_combination->max_scientists = specialists;
    } else {
      base_combination->max_scientists = 0;
    }

    if (city_can_use_specialist(pcity, SP_TAXMAN)) {
      base_combination->max_taxmen = specialists;
    } else {
      base_combination->max_taxmen = 0;
    }
    items = (base_combination->max_scientists + 1) *
	(base_combination->max_taxmen + 1);
    base_combination->cache1 =
	fc_malloc(sizeof(struct cm_result) * items);
    for (i = 0; i < items; i++) {
      base_combination->cache1[i].found_a_valid = FALSE;
    }
  }

  best_result->found_a_valid = FALSE;

  for (scientists = 0;
       scientists <= base_combination->max_scientists; scientists++) {
    for (taxmen = 0;
	 taxmen <= base_combination->max_taxmen - scientists; taxmen++) {
      int major_fitness, minor_fitness;
      struct cm_result result;

      freelog(FIND_BEST_SPECIALIST_ARRANGEMENT_LOG_LEVEL,
	      "  optimize_people: using (W/E/S/T) %d/%d/%d/%d",
	      worker, pcity->size - (worker + scientists + taxmen),
	      scientists, taxmen);

      fill_out_result(pcity, &result, base_combination, scientists,
		      taxmen);

      freelog(FIND_BEST_SPECIALIST_ARRANGEMENT_LOG_LEVEL,
	      "  optimize_people: got extra=(tax=%d, luxury=%d, "
	      "science=%d)",
	      result.surplus[GOLD] - parameter->minimal_surplus[GOLD],
	      result.surplus[LUXURY] -
	      parameter->minimal_surplus[LUXURY],
	      result.surplus[SCIENCE] -
	      parameter->minimal_surplus[SCIENCE]);

      if (!is_valid_result(parameter, &result)) {
	freelog(FIND_BEST_SPECIALIST_ARRANGEMENT_LOG_LEVEL,
		"  optimize_people: doesn't have enough surplus or disorder");
	continue;
      }

      calc_fitness(pcity, parameter, &result, &major_fitness,
		   &minor_fitness);

      freelog(FIND_BEST_SPECIALIST_ARRANGEMENT_LOG_LEVEL,
	      "  optimize_people: fitness=(%d,%d)", major_fitness,
	      minor_fitness);

      result.found_a_valid = TRUE;
      if (!best_result->found_a_valid
	  || ((major_fitness > *best_major_fitness)
	      || (major_fitness == *best_major_fitness
		  && minor_fitness > *best_minor_fitness))) {
	memcpy(best_result, &result, sizeof(struct cm_result));
	*best_major_fitness = major_fitness;
	*best_minor_fitness = minor_fitness;
      }
    }				/* for taxmen */
  }				/* for scientists */
}

/****************************************************************************
 The top level optimization method. It finds the realisation with
 the best fitness.
*****************************************************************************/
static void optimize_final(struct city *pcity,
			   const struct cm_parameter *const parameter,
			   struct cm_result *best_result)
{
  int fields_used, i;
  int results_used = 0, not_enough_primary = 0, not_enough_secondary = 0;
  /* Just for the compiler. Guarded by best_result->found_a_valid */
  int best_major_fitness = 0, best_minor_fitness = 0;

  build_cache3(pcity);

  best_result->found_a_valid = FALSE;

  /* Loop over all combinations */
  for (fields_used = 0;
       fields_used <= MIN(cache3.fields_available_total, pcity->size);
       fields_used++) {
    freelog(OPTIMIZE_FINAL_LOG_LEVEL,
	    "there are %d combinations which use %d fields",
	    count_valid_combinations(fields_used), fields_used);
    for (i = 0; i < MAX_COMBINATIONS; i++) {
      struct combination *current =
	  &cache3.results[fields_used].combinations[i];
      int stat, major_fitness, minor_fitness;
      struct cm_result result;

      if (!current->is_valid) {
	continue;
      }

      freelog(OPTIMIZE_FINAL_LOG_LEVEL2, "  trying combination %d", i);

      /* this will set the all_entertainer result */
      fill_out_result(pcity, &result, current, 0, 0);

      /*
       * Check. The actual production can be bigger because of city
       * improvements such a Factory.
       */
      for (stat = 0; stat < NUM_PRIMARY_STATS; stat++) {
	if (result.production[stat] < current->production2[stat]) {
	  freelog(LOG_NORMAL, "expected:");
	  print_combination(pcity, current);
	  freelog(LOG_NORMAL, "got:");
	  print_result(pcity, &result);
	  assert(0);
	}
      }

      /*
       * the secondary stats aren't calculated yet but we want to use
       * is_valid_result()
       */
      result.surplus[GOLD] = parameter->minimal_surplus[GOLD];
      result.surplus[LUXURY] = parameter->minimal_surplus[LUXURY];
      result.surplus[SCIENCE] = parameter->minimal_surplus[SCIENCE];

      if (!is_valid_result(parameter, &result)) {
	not_enough_primary++;
	freelog(OPTIMIZE_FINAL_LOG_LEVEL2, "    not enough primary");
	continue;
      }

      find_best_specialist_arrangement(pcity, parameter, current, &result,
				       &major_fitness, &minor_fitness);
      if (!result.found_a_valid) {
	freelog(OPTIMIZE_FINAL_LOG_LEVEL2, "    not enough secondary");
	not_enough_secondary++;
	continue;
      }

      freelog(OPTIMIZE_FINAL_LOG_LEVEL2, "    is ok");
      results_used++;

      if (!best_result->found_a_valid
	  || ((major_fitness > best_major_fitness)
	      || (major_fitness == best_major_fitness
		  && minor_fitness > best_minor_fitness))) {
	freelog(OPTIMIZE_FINAL_LOG_LEVEL2, "    is new best result");
	memcpy(best_result, &result, sizeof(struct cm_result));
	best_major_fitness = major_fitness;
	best_minor_fitness = minor_fitness;
      } else {
	freelog(OPTIMIZE_FINAL_LOG_LEVEL2,
		"    isn't better than the best result");
      }
    }
  }

  freelog(OPTIMIZE_FINAL_LOG_LEVEL,
	  "%d combinations don't have the required minimal primary surplus",
	  not_enough_primary);

  freelog(OPTIMIZE_FINAL_LOG_LEVEL,
	  "%d combinations don't have the required minimal secondary surplus",
	  not_enough_secondary);

  freelog(OPTIMIZE_FINAL_LOG_LEVEL, "%d combinations did remain",
	  results_used);
}

/*************************** public interface *******************************/
/****************************************************************************
...
*****************************************************************************/
void cm_init(void)
{
  cache3.pcity = NULL;

  /* Reset the stats. */
  memset(&stats, 0, sizeof(stats));
  stats.wall_timer = new_timer(TIMER_USER, TIMER_ACTIVE);
}

/****************************************************************************
...
*****************************************************************************/
void cm_free(void)
{
  free_timer(stats.wall_timer);
  stats.wall_timer = NULL;

  free(cache2.secondary_stats);
  cache2.secondary_stats = NULL;

  free(cache2.city_status);
  cache2.city_status = NULL;

  cache2.allocated_size = 0;
  cache2.allocated_trade = 0;
  cache2.allocated_luxury = 0;
  clear_cache();
}

/****************************************************************************
...
*****************************************************************************/
void cm_query_result(struct city *pcity,
		      const struct cm_parameter *const parameter,
		      struct cm_result *result)
{
  freelog(CM_QUERY_RESULT_LOG_LEVEL, "cm_query_result(city='%s'(%d))",
	  pcity->name, pcity->id);

  start_timer(stats.wall_timer);
  optimize_final(pcity, parameter, result);
  stop_timer(stats.wall_timer);

  stats.queries++;
  freelog(CM_QUERY_RESULT_LOG_LEVEL, "cm_query_result: return");
  if (DISABLE_CACHE3) {
    cm_clear_cache(pcity);
  }
  report_stats();
}

/****************************************************************************
 Invalidate cache3 if the given city is the one which is cached by
 cache3. The other caches (cache1, cache2 and tile_stats) doesn't have
 to be invalidated since they are chained on cache3.
*****************************************************************************/
void cm_clear_cache(struct city *pcity)
{
  freelog(LOG_DEBUG, "cm_clear_cache(city='%s'(%d))", pcity->name,
	  pcity->id);

  if (cache3.pcity == pcity) {
    clear_cache();
  }
}

/****************************************************************************
...
*****************************************************************************/
const char *cm_get_stat_name(enum cm_stat stat)
{
  switch (stat) {
  case FOOD:
    return _("Food");
  case SHIELD:
    return _("Shield");
  case TRADE:
    return _("Trade");
  case GOLD:
    return _("Gold");
  case LUXURY:
    return _("Luxury");
  case SCIENCE:
    return _("Science");
  default:
    die("Unknown stat value in cm_get_stat_name: %d", stat);
    return NULL;
  }
}

/**************************************************************************
 Returns true if the two cm_parameters are equal.
**************************************************************************/
bool cm_are_parameter_equal(const struct cm_parameter *const p1,
			    const struct cm_parameter *const p2)
{
  int i;

  for (i = 0; i < NUM_STATS; i++) {
    if (p1->minimal_surplus[i] != p2->minimal_surplus[i]) {
      return FALSE;
    }
    if (p1->factor[i] != p2->factor[i]) {
      return FALSE;
    }
  }
  if (p1->require_happy != p2->require_happy) {
    return FALSE;
  }
  if (p1->allow_disorder != p2->allow_disorder) {
    return FALSE;
  }
  if (p1->allow_specialists != p2->allow_specialists) {
    return FALSE;
  }
  if (p1->factor_target != p2->factor_target) {
    return FALSE;
  }
  if (p1->happy_factor != p2->happy_factor) {
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
 ...
**************************************************************************/
void cm_copy_parameter(struct cm_parameter *dest,
		       const struct cm_parameter *const src)
{
  memcpy(dest, src, sizeof(struct cm_parameter));
}
