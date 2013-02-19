/**********************************************************************
 Freeciv - Copyright (C) 2003 - The Freeciv Project
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
#include <fc_config.h>
#endif

/* utility */
#include "bitvector.h"
#include "log.h"
#include "mem.h"
#include "pqueue.h"

/* common */
#include "game.h"
#include "map.h"
#include "movement.h"

#include "path_finding.h"

/* For explanations on how to use this module, see "path_finding.h". */

#define INITIAL_QUEUE_SIZE 100

#ifdef DEBUG
/* Extra checks for debugging. */
#define PF_DEBUG
#endif

/* ======================== Internal structures ========================== */

#ifdef PF_DEBUG
/* The mode we use the pf_map. Used for cast converion checks. */
enum pf_mode {
  PF_NORMAL = 1,        /* Usual goto */
  PF_DANGER,            /* Goto with dangerous positions */
  PF_FUEL               /* Goto for fueled units */
};
#endif /* PF_DEBUG */

enum pf_node_status {
  NS_UNINIT = 0,        /* memory is calloced, hence zero means
                         * uninitialised. */
  NS_INIT,              /* node initialized, but we didn't search a route
                         * yet. */
  NS_NEW,               /* the optimal route isn't found yet. */
  NS_WAITING,           /* the optimal route is found, considering waiting.
                         * Only used for pf_danger_map and pf_fuel_map, it
                         * means we are waiting on a safe place for full
                         * moves before crossing a dangerous area. */
  NS_PROCESSED          /* the optimal route is found. */
};

enum pf_zoc_type {
  ZOC_NO = 0,           /* No ZoC. */
  ZOC_ALLIED,           /* Allied ZoC. */
  ZOC_MINE              /* My ZoC. */
};

/* Abstract base class for pf_normal_map, pf_danger_map, and pf_fuel_map. */
struct pf_map {
#ifdef PF_DEBUG
  enum pf_mode mode;    /* The mode of the map, for conversion checking. */
#endif /* PF_DEBUG */

  /* "Virtual" function table. */
  void (*destroy) (struct pf_map *pfm); /* Destructor. */
  int (*get_move_cost) (struct pf_map *pfm, struct tile *ptile);
  struct pf_path * (*get_path) (struct pf_map *pfm, struct tile *ptile);
  bool (*get_position) (struct pf_map *pfm, struct tile *ptile,
                        struct pf_position *pos);
  bool (*iterate) (struct pf_map *pfm);

  /* Private data. */
  struct tile *tile;          /* The current position (aka iterator). */
  struct pf_parameter params; /* Initial parameters. */
};

/* Down-cast macro. */
#define PF_MAP(pfm) ((struct pf_map *) (pfm))

/* Used as an enum direction8. */
#define PF_DIR_NONE (-1)


/* ========================== Common functions =========================== */

/****************************************************************************
  Return the number of "moves" started with.

  This is different from the moves_left_initially because of fuel. For
  units with fuel > 1 all moves on the same tank of fuel are considered to
  be one turn. Thus the rest of the PF code doesn't actually know that the
  unit has fuel, it just thinks it has that many more MP.
****************************************************************************/
static inline int pf_moves_left_initially(const struct pf_parameter *param)
{
  return (param->moves_left_initially
          + (param->fuel_left_initially - 1) * param->move_rate);
}

/****************************************************************************
  Return the "move rate".

  This is different from the parameter's move_rate because of fuel. For
  units with fuel > 1 all moves on the same tank of fuel are considered
  to be one turn. Thus the rest of the PF code doesn't actually know that
  the unit has fuel, it just thinks it has that many more MP.
****************************************************************************/
static inline int pf_move_rate(const struct pf_parameter *param)
{
  return param->move_rate * param->fuel;
}

/****************************************************************************
  Number of turns required to reach node. See comment in the body of
  pf_normal_map_new(), pf_danger_map_new(), and pf_fuel_map_new() functions
  about the usage of moves_left_initially().
****************************************************************************/
static inline int pf_turns(const struct pf_parameter *param, int cost)
{
  int move_rate = param->move_rate;

  if (move_rate <= 0) {
    /* This unit cannot move by itself. */
    return FC_INFINITY;
  }

  /* Negative cost can happen when a unit initially has more MP than its
   * move-rate (due to wonders transfer etc). Although this may be a bug,
   * we'd better be ready.
   *
   * Note that cost == 0 corresponds to the current turn with full MP. */
  return (cost < 0 ? 0
          : cost / move_rate + param->fuel_left_initially - param->fuel);
}

/****************************************************************************
  Moves left after node is reached.
****************************************************************************/
static inline int pf_moves_left(const struct pf_parameter *param, int cost)
{
  int move_rate = pf_move_rate(param);

  if (move_rate <= 0) {
    /* This unit never have moves left. */
    return 0;
  }

  /* Cost may be negative; see pf_turns(). */
  return (cost < 0 ? move_rate - cost : move_rate - (cost % move_rate));
}

/****************************************************************************
  Obtain cost-of-path from pure cost and extra cost
****************************************************************************/
static inline int pf_total_CC(const struct pf_parameter *param,
                              int cost, int extra)
{
  return PF_TURN_FACTOR * cost + extra * pf_move_rate(param);
}

/****************************************************************************
  Take a position previously filled out (as by fill_position) and "finalize"
  it by reversing all fuel multipliers.

  See pf_moves_left_initially() and pf_move_rate().
****************************************************************************/
static inline void pf_finalize_position(const struct pf_parameter *param,
                                        struct pf_position *pos)
{
  int move_rate = param->move_rate;

  pos->turn *= param->fuel;
  if (0 < move_rate) {
    pos->turn += ((move_rate - pos->moves_left) / move_rate);

    /* We add 1 because a fuel of 1 means "no" fuel left; e.g. fuel
     * ranges from [1, ut->fuel] not from [0, ut->fuel) as one may think. */
    pos->fuel_left = pos->moves_left / move_rate + 1;

    pos->moves_left %= move_rate;
  } else {
    /* This unit cannot move by itself. */
    pos->turn = same_pos(pos->tile, param->start_tile) ? 0 : FC_INFINITY;
    pos->fuel_left = 0; /* or 1? */
  }
}

/* Determine if we can enter the node at all. Set it as a macro, because it
 * is used as many node types (pf_normal_node, pf_danger_node,
 * and pf_fuel_node). */
#define CAN_ENTER_NODE(node)                                                \
 ((node)->can_invade && TB_IGNORE != (node)->behavior)

static struct pf_path *
pf_path_new_to_start_tile(const struct pf_parameter *param);
static void pf_position_fill_start_tile(struct pf_position *pos,
                                        const struct pf_parameter *param);


/* ================ Specific pf_normal_* mode structures ================= */

/* Normal path-finding maps are used for most of units with standard rules.
 * See what units make pf_map_new() to pick danger or fuel maps instead. */

/* Node definition. Note we try to have the smallest data as possible. */
struct pf_normal_node {
  signed short cost;    /* total_MC. 'cost' may be negative, see comment in
                         * pf_turns(). */
  unsigned extra_cost;  /* total_EC. Can be huge, (higher than 'cost'). */
  signed dir_to_here : 4; /* Direction from which we came. It can be either
                           * an 'enum direction8' or PF_DIR_NONE (so we need
                           * 4 bits instead of 3). */
  unsigned status : 3;  /* 'enum pf_node_status' really. */

  /* Cached values */
  bool can_invade : 1;
  unsigned node_known_type : 2; /* 'enum known_type' really. */
  unsigned behavior : 2;        /* 'enum tile_behavior' really. */
  unsigned zoc_number : 2;      /* 'enum pf_zoc_type' really. */
  unsigned short extra_tile;    /* EC */
};

/* Derived structure of struct pf_map. */
struct pf_normal_map {
  struct pf_map base_map;   /* Base structure, must be the first! */

  struct pqueue *queue;     /* Queue of nodes we have reached but not
                             * processed yet (NS_NEW), sorted by their
                             * total_CC. */
  struct pf_normal_node *lattice; /* Lattice of nodes. */
};

/* Up-cast macro. */
#ifdef PF_DEBUG
static inline struct pf_normal_map *
pf_normal_map_check(struct pf_map *pfm, const char *file, 
                    const char *function, int line)
{
  fc_assert_full(file, function, line,
                 NULL != pfm && PF_NORMAL == pfm->mode,
                 return NULL, "Wrong pf_map to pf_normal_map conversion.");
  return (struct pf_normal_map *) pfm;
}
#define PF_NORMAL_MAP(pfm)                                                  \
  pf_normal_map_check(pfm, __FILE__, __FUNCTION__, __FC_LINE__)
#else
#define PF_NORMAL_MAP(pfm) ((struct pf_normal_map *) (pfm))
#endif /* PF_DEBUG */

/* ================  Specific pf_normal_* mode functions ================= */

/****************************************************************************
  Calculates cached values of the target node. Set the node status to
  NS_INIT to avoid recalculating all values.
****************************************************************************/
static void pf_normal_node_init(struct pf_normal_map *pfnm,
                                struct pf_normal_node *node,
                                struct tile *ptile)
{
  const struct pf_parameter *params = pf_map_parameter(PF_MAP(pfnm));

#ifdef PF_DEBUG
  fc_assert(NS_UNINIT == node->status);
  /* Else, not a critical problem, but waste of time. */
#endif

  /* Establish the "known" status of node. */
  if (params->omniscience) {
    node->node_known_type = TILE_KNOWN_SEEN;
  } else {
    node->node_known_type = tile_get_known(ptile, params->owner);
  }

  /* Establish the tile behavior. */
  if (NULL != params->get_TB) {
    node->behavior = params->get_TB(ptile, node->node_known_type, params);
  } else {
    /* The default. */
    node->behavior = TB_NORMAL;
  }

  if (NULL != params->get_zoc) {
    struct city *pcity = tile_city(ptile);
    struct terrain *pterrain = tile_terrain(ptile);
    bool my_zoc = (NULL != pcity || pterrain == T_UNKNOWN
                   || terrain_has_flag(pterrain, TER_OCEANIC)
                   || params->get_zoc(params->owner, ptile));
    /* ZoC rules cannot prevent us from moving into/attacking an occupied
     * tile. Other rules can, but we don't care about them here. */
    bool occupied = (0 < unit_list_size(ptile->units) || NULL != pcity);

    /* ZOC_MINE means can move unrestricted from/into it, ZOC_ALLIED means
     * can move unrestricted into it, but not necessarily from it. */
    node->zoc_number = (my_zoc ? ZOC_MINE
                        : (occupied ? ZOC_ALLIED : ZOC_NO));
#ifdef ZERO_VARIABLES_FOR_SEARCHING
  } else {
    /* Nodes are allocated by fc_calloc(), so should be already set to 0. */
    node->zoc_number = 0;
#endif
  }

  /* Evaluate the extra cost of the destination */
  if (NULL != params->get_EC) {
    node->extra_tile = params->get_EC(ptile, node->node_known_type, params);
#ifdef ZERO_VARIABLES_FOR_SEARCHING
  } else {
    /* Nodes are allocated by fc_calloc(), so  should be already set to 0. */
    node->extra_tile = 0;
#endif
  }

  if (NULL != params->can_invade_tile) {
    node->can_invade = params->can_invade_tile(params->owner, ptile);
  } else {
    node->can_invade = TRUE;
  }

  node->status = NS_INIT;
}

/****************************************************************************
  Fill in the position which must be discovered already. A helper
  for pf_normal_map_position(). This also "finalizes" the position.
****************************************************************************/
static void pf_normal_map_fill_position(const struct pf_normal_map *pfnm,
                                        struct tile *ptile,
                                        struct pf_position *pos)
{
  int index = tile_index(ptile);
  struct pf_normal_node *node = pfnm->lattice + index;
  const struct pf_parameter *params = pf_map_parameter(PF_MAP(pfnm));

#ifdef PF_DEBUG
  fc_assert_ret_msg(NS_PROCESSED == node->status,
                    "Unreached destination (%d, %d).", TILE_XY(ptile));
#endif /* PF_DEBUG */

  pos->tile = ptile;
  pos->total_EC = node->extra_cost;
  pos->total_MC = (node->cost - pf_move_rate(params)
                   + pf_moves_left_initially(params));
  pos->turn = pf_turns(params, node->cost);
  pos->moves_left = pf_moves_left(params, node->cost);
  pos->dir_to_here = node->dir_to_here;
  pos->dir_to_next_pos = PF_DIR_NONE;   /* This field does not apply. */

  pf_finalize_position(params, pos);
}

/****************************************************************************
  Read off the path to the node dest_tile, which must already be discovered.
  A helper for pf_normal_map_path functions.
****************************************************************************/
static struct pf_path *
pf_normal_map_construct_path(const struct pf_normal_map *pfnm,
                             struct tile *dest_tile)
{
  struct pf_normal_node *node = pfnm->lattice + tile_index(dest_tile);
  const struct pf_parameter *params = pf_map_parameter(PF_MAP(pfnm));
  enum direction8 dir_next = PF_DIR_NONE;
  struct pf_path *path;
  struct tile *ptile;
  int i;

#ifdef PF_DEBUG
  fc_assert_ret_val_msg(NS_PROCESSED == node->status, NULL,
                        "Unreached destination (%d, %d).",
                        TILE_XY(dest_tile));
#endif /* PF_DEBUG */

  ptile = dest_tile;
  path = fc_malloc(sizeof(*path));

  /* 1: Count the number of steps to get here.
   * To do it, backtrack until we hit the starting point */
  for (i = 0; ; i++) {
    if (same_pos(ptile, params->start_tile)) {
      /* Ah-ha, reached the starting point! */
      break;
    }

    ptile = mapstep(ptile, DIR_REVERSE(node->dir_to_here));
    node = pfnm->lattice + tile_index(ptile);
  }

  /* 2: Allocate the memory */
  path->length = i + 1;
  path->positions = fc_malloc((i + 1) * sizeof(*(path->positions)));

  /* 3: Backtrack again and fill the positions this time */
  ptile = dest_tile;
  node = pfnm->lattice + tile_index(ptile);

  for (; i >= 0; i--) {
    pf_normal_map_fill_position(pfnm, ptile, &path->positions[i]);
    /* fill_position doesn't set direction */
    path->positions[i].dir_to_next_pos = dir_next;

    dir_next = node->dir_to_here;

    if (i > 0) {
      /* Step further back, if we haven't finished yet */
      ptile = mapstep(ptile, DIR_REVERSE(dir_next));
      node = pfnm->lattice + tile_index(ptile);
    }
  }

  return path;
}

/****************************************************************************
  Adjust MC to reflect the move_rate.
****************************************************************************/
static int pf_normal_map_adjust_cost(const struct pf_normal_map *pfnm,
                                     int cost)
{
  const struct pf_parameter *params;
  const struct pf_normal_node *node;
  int moves_left;

  fc_assert_ret_val(cost >= 0, PF_IMPOSSIBLE_MC);

  params = pf_map_parameter(PF_MAP(pfnm));
  node = pfnm->lattice + tile_index(PF_MAP(pfnm)->tile);
  moves_left = pf_moves_left(params, node->cost);
  if (cost > moves_left) {
    cost = moves_left;
  }
  return cost;
}

/****************************************************************************
  Bare-bones PF iterator. All Freeciv rules logic is hidden in 'get_costs'
  callback (compare to pf_normal_map_iterate function). This function is
  called when the pf_map was created with a 'get_cost' callback.

  Plan: 1. Process previous position.
        2. Get new nearest position and return it.

  During the iteration, the node status will be changed:
  A. NS_UNINIT: The node is not initialized, we didn't reach it at all.
  (NS_INIT not used here)
  B. NS_NEW: We have reached this node, but we are not sure it was the best
     path.
  (NS_WAITING not used here)
  C. NS_PROCESSED: Now, we are sure we have the best path. Then, we won't
     do anything more with this node.
****************************************************************************/
static bool pf_jumbo_map_iterate(struct pf_map *pfm)
{
  struct pf_normal_map *pfnm = PF_NORMAL_MAP(pfm);
  struct tile *tile = pfm->tile;
  int index = tile_index(tile);
  struct pf_normal_node *node = pfnm->lattice + index;
  const struct pf_parameter *params = pf_map_parameter(pfm);

  /* Processing Stage */

  /* The previous position is defined by 'tile' (tile pointer), 'node'
   * (the data of the tile for the pf_map), and index (the index of the
   * position in the Freeciv map). */

  adjc_dir_iterate(tile, tile1, dir) {
    /* Calculate the cost of every adjacent position and set them in the
     * priority queue for next call to pf_jumbo_map_iterate(). */
    int index1 = tile_index(tile1);
    struct pf_normal_node *node1 = pfnm->lattice + index1;
    int priority, cost1, extra_cost1;

    /* As for the previous position, 'tile1', 'node1' and 'index1' are
     * defining the adjacent position. */

    if (node1->status == NS_PROCESSED) {
      /* This gives 15% speedup */
      continue;
    }

    if (NS_UNINIT == node1->status) {
      /* Set cost as impossible for initializing, params->get_costs(), will
       * overwrite with the right value. */
      cost1 = PF_IMPOSSIBLE_MC;
      extra_cost1 = 0;
    } else {
      cost1 = node1->cost;
      extra_cost1 = node1->extra_cost;
    }

    /* User-supplied callback 'get_costs' takes care of everything (ZOC,
     * known, costs etc). See explanations in "path_finding.h". */
    priority = params->get_costs(tile, dir, tile1, node->cost,
                                 node->extra_cost, &cost1,
                                 &extra_cost1, params);
    if (priority >= 0) {
      /* We found a better route to 'tile1', record it (the costs are
       * recorded already). Node status step A. to B. */
      node1->cost = cost1;
      node1->extra_cost = extra_cost1;
      node1->status = NS_NEW;
      node1->dir_to_here = dir;
      pq_insert(pfnm->queue, index1, -priority);
    }
  } adjc_dir_iterate_end;

  /* Get the next node (the index with the highest priority). */
  do {
    if (!pq_remove(pfnm->queue, &index)) {
      /* No more indexes in the priority queue, iteration end. */
      return FALSE;
    }
    /* If the node has already been processed, get the next one. */
  } while (NS_NEW != pfnm->lattice[index].status);

  /* Change the pf_map iterator. Node status step B. to C. */
  pfm->tile = index_to_tile(index);
  pfnm->lattice[index].status = NS_PROCESSED;

  return TRUE;
}

/****************************************************************************
  Primary method for iterative path-finding.

  Plan: 1. Process previous position.
        2. Get new nearest position and return it.

  During the iteration, the node status will be changed:
  A. NS_UNINIT: The node is not initialized, we didn't reach it at all.
  B. NS_INIT: We have initialized the cached values, however, we failed to
     reach this node.
  C. NS_NEW: We have reached this node, but we are not sure it was the best
     path.
  (NS_WAITING not used here)
  D. NS_PROCESSED: Now, we are sure we have the best path. Then, we won't
     do anything more with this node.
****************************************************************************/
static bool pf_normal_map_iterate(struct pf_map *pfm)
{
  struct pf_normal_map *pfnm = PF_NORMAL_MAP(pfm);
  struct tile *tile = pfm->tile;
  int index = tile_index(tile);
  struct pf_normal_node *node = pfnm->lattice + index;
  const struct pf_parameter *params = pf_map_parameter(pfm);
  int cost_of_path;

  /* There is no exit from DONT_LEAVE tiles! */
  if (node->behavior != TB_DONT_LEAVE) {
    /* Processing Stage */

    /* The previous position is defined by 'tile' (tile pointer), 'node'
     * (the data of the tile for the pf_map), and index (the index of the
     * position in the Freeciv map). */

    adjc_dir_iterate(tile, tile1, dir) {
      /* Calculate the cost of every adjacent position and set them in the
       * priority queue for next call to pf_normal_map_iterate(). */
      int index1 = tile_index(tile1);
      struct pf_normal_node *node1 = pfnm->lattice + index1;
      int cost;
      int extra = 0;

      /* As for the previous position, 'tile1', 'node1' and 'index1' are
       * defining the adjacent position. */

      if (node1->status == NS_PROCESSED) {
        /* This gives 15% speedup. Node status already at step D. */
        continue;
      }

      if (node1->status == NS_UNINIT) {
        /* Only initialize once. See comment for pf_normal_node_init().
         * Node status step A. to B. */
        pf_normal_node_init(pfnm, node1, tile1);
      }

      /* Can we enter this tile at all? */
      if (!CAN_ENTER_NODE(node1)) {
        continue;
      }

      /* Is the move ZOC-ok? */
      if (NULL != params->get_zoc
          && !(node->zoc_number == ZOC_MINE
               || node1->zoc_number != ZOC_NO)) {
        continue;
      }

      /* Evaluate the cost of the move. */
      if (node1->node_known_type == TILE_UNKNOWN) {
        cost = params->unknown_MC;
      } else {
        cost = params->get_MC(tile, dir, tile1, params);
      }
      if (cost == PF_IMPOSSIBLE_MC) {
        continue;
      }
      cost = pf_normal_map_adjust_cost(pfnm, cost);
      if (cost == PF_IMPOSSIBLE_MC) {
        continue;
      }

      if (node1->behavior == TB_DONT_LEAVE) {
        /* We evaluate moves to TB_DONT_LEAVE tiles as a constant single
         * move for getting straightest paths. */
        cost = SINGLE_MOVE;
      }

      /* Total cost at tile1. Cost may be negative; see pf_turns(). */
      cost += node->cost;

      /* Evaluate the extra cost if it's relevant */
      if (NULL != params->get_EC) {
        extra = node->extra_cost;
        /* Add the cached value */
        extra += node1->extra_tile;
      }

      /* Update costs. */
      cost_of_path = pf_total_CC(params, cost, extra);

      if (NS_INIT == node1->status
          || cost_of_path < pf_total_CC(params, node1->cost,
                                        node1->extra_cost)) {
        /* We are reaching this node for the first time, or we found a
         * better route to 'tile1'. Let's register 'index1' to the
         * priority queue. Node status step B. to C. */
        node1->status = NS_NEW;
        node1->extra_cost = extra;
        node1->cost = cost;
        node1->dir_to_here = dir;
        /* As we prefer lower costs, let's reverse the cost of the path. */
        pq_insert(pfnm->queue, index1, -cost_of_path);
      }
    } adjc_dir_iterate_end;
  }

  /* Get the next node (the index with the highest priority). */
  do {
    if (!pq_remove(pfnm->queue, &index)) {
      /* No more indexes in the priority queue, iteration end. */
      return FALSE;
    }
    /* Discard if this node has already been processed. */
  } while (NS_NEW != pfnm->lattice[index].status);

  /* Change the pf_map iterator. Node status step C. to D. */
  pfm->tile = index_to_tile(index);
  pfnm->lattice[index].status = NS_PROCESSED;

  return TRUE;
}

/****************************************************************************
  Iterate the map until 'ptile' is reached.
****************************************************************************/
static inline bool pf_normal_map_iterate_until(struct pf_normal_map *pfnm,
                                               struct tile *ptile)
{
  struct pf_map *pfm = PF_MAP(pfnm);
  struct pf_normal_node *node = pfnm->lattice + tile_index(ptile);

  if (NULL == pf_map_parameter(pfm)->get_costs) {
    /* Start position is handled in every function calling this function. */
    if (NS_UNINIT == node->status) {
      /* Initialize the node, for doing the following tests. */
      pf_normal_node_init(pfnm, node, ptile);
    }

    /* Simpliciation: if we cannot enter this node at all, don't iterate the
     * whole map. */
    if (!CAN_ENTER_NODE(node)) {
      return FALSE;
    }
  } /* Else, this is a jumbo map, not dealing with normal nodes. */

  while (NS_PROCESSED != node->status) {
    if (!pf_map_iterate(pfm)) {
      /* All reachable destination have been iterated, 'ptile' is
       * unreachable. */
      return FALSE;
    }
  }

  return TRUE;
}

/****************************************************************************
  Return the move cost at ptile. If ptile has not been reached
  yet, iterate the map until we reach it or run out of map. This function
  returns PF_IMPOSSIBLE_MC on unreachable positions.
****************************************************************************/
static int pf_normal_map_move_cost(struct pf_map *pfm, struct tile *ptile)
{
  struct pf_normal_map *pfnm = PF_NORMAL_MAP(pfm);

  if (same_pos(ptile, pfm->params.start_tile)) {
    return 0;
  } else if (pf_normal_map_iterate_until(pfnm, ptile)) {
    return (pfnm->lattice[tile_index(ptile)].cost
            - pf_move_rate(pf_map_parameter(pfm))
            + pf_moves_left_initially(pf_map_parameter(pfm)));
  } else {
    return PF_IMPOSSIBLE_MC;
  }
}

/****************************************************************************
  Return the path to ptile. If ptile has not been reached yet, iterate the
  map until we reach it or run out of map.
****************************************************************************/
static struct pf_path *pf_normal_map_path(struct pf_map *pfm,
                                          struct tile *ptile)
{
  struct pf_normal_map *pfnm = PF_NORMAL_MAP(pfm);

  if (same_pos(ptile, pfm->params.start_tile)) {
    return pf_path_new_to_start_tile(pf_map_parameter(pfm));
  } else if (pf_normal_map_iterate_until(pfnm, ptile)) {
    return pf_normal_map_construct_path(pfnm, ptile);
  } else {
    return NULL;
  }
}

/****************************************************************************
  Get info about position at ptile and put it in pos. If ptile has not been
  reached yet, iterate the map until we reach it. Should _always_ check the
  return value, forthe position might be unreachable.
****************************************************************************/
static bool pf_normal_map_position(struct pf_map *pfm, struct tile *ptile,
                                   struct pf_position *pos)
{
  struct pf_normal_map *pfnm = PF_NORMAL_MAP(pfm);

  if (same_pos(ptile, pfm->params.start_tile)) {
    pf_position_fill_start_tile(pos, pf_map_parameter(pfm));
    return TRUE;
  } else if (pf_normal_map_iterate_until(pfnm, ptile)) {
    pf_normal_map_fill_position(pfnm, ptile, pos);
    return TRUE;
  } else {
    return FALSE;
  }
}

/****************************************************************************
  'pf_normal_map' destructor.
****************************************************************************/
static void pf_normal_map_destroy(struct pf_map *pfm)
{
  struct pf_normal_map *pfnm = PF_NORMAL_MAP(pfm);

  free(pfnm->lattice);
  pq_destroy(pfnm->queue);
  free(pfnm);
}

/****************************************************************************
  'pf_normal_map' constructor.
****************************************************************************/
static struct pf_map *pf_normal_map_new(const struct pf_parameter *parameter)
{
  struct pf_normal_map *pfnm;
  struct pf_map *base_map;
  struct pf_parameter *params;
  struct pf_normal_node *node;

  pfnm = fc_malloc(sizeof(*pfnm));
  base_map = &pfnm->base_map;
  params = &base_map->params;
#ifdef PF_DEBUG
  /* Set the mode, used for cast check. */
  base_map->mode = PF_NORMAL;
#endif /* PF_DEBUG */

  /* Allocate the map. */
  pfnm->lattice = fc_calloc(MAP_INDEX_SIZE, sizeof(struct pf_normal_node));
  pfnm->queue = pq_create(INITIAL_QUEUE_SIZE);

  /* 'get_MC' or 'get_costs' callback must be set. */
  fc_assert_ret_val(NULL != parameter->get_MC
                    || NULL != parameter->get_costs, NULL);

  /* Copy parameters. */
  *params = *parameter;

  /* Initialize virtual function table. */
  base_map->destroy = pf_normal_map_destroy;
  base_map->get_move_cost = pf_normal_map_move_cost;
  base_map->get_path = pf_normal_map_path;
  base_map->get_position = pf_normal_map_position;
  if (NULL != params->get_costs) {
    base_map->iterate = pf_jumbo_map_iterate;
  } else {
    base_map->iterate = pf_normal_map_iterate;
  }

  /* Initialise the iterator. */
  base_map->tile = params->start_tile;

  /* Initialise starting node. */
  node = pfnm->lattice + tile_index(params->start_tile);
  pf_normal_node_init(pfnm, node, params->start_tile);
  /* This makes calculations of turn/moves_left more convenient, but we
   * need to subtract this value before we return cost to the user. Note
   * that cost may be negative if moves_left_initially > move_rate
   * (see pf_turns()). */
  node->cost = pf_move_rate(params) - pf_moves_left_initially(params);
  node->extra_cost = 0;
  node->dir_to_here = PF_DIR_NONE;
  node->status = NS_PROCESSED;

  return PF_MAP(pfnm);
}


/* ================ Specific pf_danger_* mode structures ================= */

/* Danger path-finding maps are used for units which can cross some areas
 * but not ending their turn there. It used to be used for triremes notably.
 * But since Freeciv 2.2, units with the "Trireme" flag just have
 * restricted moves, then it is not use anymore. */

/* Node definition. Note we try to have the smallest data as possible. */
struct pf_danger_node {
  signed short cost;    /* total_MC. 'cost' may be negative, see comment in
                         * pf_turns(). */
  unsigned extra_cost;  /* total_EC. Can be huge, (higher than 'cost'). */
  signed dir_to_here : 4; /* Direction from which we came. It can be either
                           * an 'enum direction8' or PF_DIR_NONE (so we need
                           * 4 bits instead of 3). */
  unsigned status : 3;  /* 'enum pf_node_status' really. */

  /* Cached values */
  bool can_invade : 1;
  unsigned node_known_type : 2; /* 'enum known_type' really. */
  unsigned behavior : 2;        /* 'enum tile_behavior' really. */
  unsigned zoc_number : 2;      /* 'enum pf_zoc_type' really. */
  bool is_dangerous : 1;        /* Whether we cannot end the turn there. */
  bool waited : 1;              /* TRUE if waited to get there. */
  unsigned short extra_tile;    /* EC */

  /* Segment leading across the danger area back to the nearest safe node:
   * need to remeber costs and stuff. */
  struct pf_danger_pos {
    signed short cost;          /* See comment above. */
    unsigned extra_cost;        /* See comment above. */
    signed dir_to_here : 4;     /* See comment above. */
  } *danger_segment;
};

/* Derived structure of struct pf_map. */
struct pf_danger_map {
  struct pf_map base_map;       /* Base structure, must be the first! */

  struct pqueue *queue;         /* Queue of nodes we have reached but not
                                 * processed yet (NS_NEW and NS_WAITING),
                                 * sorted by their total_CC. */
  struct pqueue *danger_queue;  /* Dangerous positions. */
  struct pf_danger_node *lattice; /* Lattice of nodes. */
};

/* Up-cast macro. */
#ifdef PF_DEBUG
static inline struct pf_danger_map *
pf_danger_map_check(struct pf_map *pfm, const char *file,
                    const char *function, int line)
{
  fc_assert_full(file, function, line,
                 NULL != pfm && PF_DANGER == pfm->mode,
                 return NULL, "Wrong pf_map to pf_danger_map conversion.");
  return (struct pf_danger_map *) pfm;
}
#define PF_DANGER_MAP(pfm)                                                  \
  pf_danger_map_check(pfm, __FILE__, __FUNCTION__, __FC_LINE__)
#else
#define PF_DANGER_MAP(pfm) ((struct pf_danger_map *) (pfm))
#endif /* PF_DEBUG */

/* ===============  Specific pf_danger_* mode functions ================== */

/****************************************************************************
  Calculates cached values of the target node. Set the node status to
  NS_INIT to avoid recalculating all values.
****************************************************************************/
static void pf_danger_node_init(struct pf_danger_map *pfdm,
                                struct pf_danger_node *node,
                                struct tile *ptile)
{
  const struct pf_parameter *params = pf_map_parameter(PF_MAP(pfdm));

#ifdef PF_DEBUG
  fc_assert(NS_UNINIT == node->status);
  /* Else, not a critical problem, but waste of time. */
#endif

  /* Establish the "known" status of node. */
  if (params->omniscience) {
    node->node_known_type = TILE_KNOWN_SEEN;
  } else {
    node->node_known_type = tile_get_known(ptile, params->owner);
  }

  /* Establish the tile behavior. */
  if (NULL != params->get_TB) {
    node->behavior = params->get_TB(ptile, node->node_known_type, params);
  } else {
    /* The default. */
    node->behavior = TB_NORMAL;
  }

  if (NULL != params->get_zoc) {
    struct city *pcity = tile_city(ptile);
    struct terrain *pterrain = tile_terrain(ptile);
    bool my_zoc = (NULL != pcity || pterrain == T_UNKNOWN
                   || terrain_has_flag(pterrain, TER_OCEANIC)
                   || params->get_zoc(params->owner, ptile));
    /* ZoC rules cannot prevent us from moving into/attacking an occupied
     * tile. Other rules can, but we don't care about them here. */
    bool occupied = (unit_list_size(ptile->units) > 0 || NULL != pcity);

    /* ZOC_MINE means can move unrestricted from/into it, ZOC_ALLIED means
     * can move unrestricted into it, but not necessarily from it. */
    node->zoc_number = (my_zoc ? ZOC_MINE
                        : (occupied ? ZOC_ALLIED : ZOC_NO));
#ifdef ZERO_VARIABLES_FOR_SEARCHING
  } else {
    /* Nodes are allocated by fc_calloc(), so should be already set to 0. */
    node->zoc_number = 0;
#endif
  }

  /* Evaluate the extra cost of the destination. */
  if (NULL != params->get_EC) {
    node->extra_tile = params->get_EC(ptile, node->node_known_type, params);
#ifdef ZERO_VARIABLES_FOR_SEARCHING
  } else {
    /* Nodes are allocated by fc_calloc(), so should be already set to 0. */
    node->extra_tile = 0;
#endif
  }

  if (NULL != params->can_invade_tile) {
    node->can_invade = params->can_invade_tile(params->owner, ptile);
  } else {
    node->can_invade = TRUE;
  }

#ifdef ZERO_VARIABLES_FOR_SEARCHING
  /* Nodes are allocated by fc_calloc(), so should be already set to
   * FALSE. */
  node->waited = FALSE;
#endif

  node->is_dangerous =
    params->is_pos_dangerous(ptile, node->node_known_type, params);

  node->status = NS_INIT;
}

/****************************************************************************
  Fill in the position which must be discovered already. A helper
  for pf_danger_map_position(). This also "finalizes" the position.
****************************************************************************/
static void pf_danger_map_fill_position(const struct pf_danger_map *pfdm,
                                        struct tile *ptile,
                                        struct pf_position *pos)
{
  int index = tile_index(ptile);
  struct pf_danger_node *node = pfdm->lattice + index;
  const struct pf_parameter *params = pf_map_parameter(PF_MAP(pfdm));

#ifdef PF_DEBUG
  fc_assert_ret_msg(NS_PROCESSED == node->status
                    || NS_WAITING == node->status,
                    "Unreached destination (%d, %d).", TILE_XY(ptile));
#endif /* PF_DEBUG */

  pos->tile = ptile;
  pos->total_EC = node->extra_cost;
  pos->total_MC = (node->cost - pf_move_rate(params)
                   + pf_moves_left_initially(params));
  pos->turn = pf_turns(params, node->cost);
  pos->moves_left = pf_moves_left(params, node->cost);
  pos->dir_to_here = node->dir_to_here;
  pos->dir_to_next_pos = PF_DIR_NONE;   /* This field does not apply. */

  pf_finalize_position(params, pos);
}

/****************************************************************************
  This function returns the fills the cost needed for a position, to get
  full moves at the next turn. This would be called only when the status is
  NS_WAITING.
****************************************************************************/
static inline int
pf_danger_map_fill_cost_for_full_moves(const struct pf_parameter *param,
                                       int cost)
{
  int moves_left = pf_moves_left(param, cost);

  if (moves_left < pf_move_rate(param)) {
    return cost + moves_left;
  } else {
    return cost;
  }
}

/****************************************************************************
  Read off the path to the node 'ptile', but with dangers.
  NB: will only find paths to safe tiles!
****************************************************************************/
static struct pf_path *
pf_danger_map_construct_path(const struct pf_danger_map *pfdm,
                             struct tile *ptile)
{
  struct pf_path *path = fc_malloc(sizeof(*path));
  enum direction8 dir_next = PF_DIR_NONE;
  struct pf_danger_pos *danger_seg = NULL;
  bool waited = FALSE;
  struct pf_danger_node *node = pfdm->lattice + tile_index(ptile);
  int length = 1;
  struct tile *iter_tile = ptile;
  const struct pf_parameter *params = pf_map_parameter(PF_MAP(pfdm));
  struct pf_position *pos;
  int i;

#ifdef PF_DEBUG
  fc_assert_ret_val_msg(NS_PROCESSED == node->status
                        || NS_WAITING == node->status, NULL,
                        "Unreached destination (%d, %d).",
                        TILE_XY(ptile));
#endif /* PF_DEBUG */

  /* First iterate to find path length. */
  while (!same_pos(iter_tile, params->start_tile)) {
    if (!node->is_dangerous && node->waited) {
      length += 2;
    } else {
      length++;
    }

    if (!node->is_dangerous || !danger_seg) {
      /* We are in the normal node and dir_to_here field is valid */
      dir_next = node->dir_to_here;
      /* d_node->danger_segment is the indicator of what lies ahead
       * if it's non-NULL, we are entering a danger segment,
       * if it's NULL, we are not on one so danger_seg should be NULL. */
      danger_seg = node->danger_segment;
    } else {
      /* We are in a danger segment. */
      dir_next = danger_seg->dir_to_here;
      danger_seg++;
    }

    /* Step backward. */
    iter_tile = mapstep(iter_tile, DIR_REVERSE(dir_next));
    node = pfdm->lattice + tile_index(iter_tile);
  }

  /* Allocate memory for path. */
  path->positions = fc_malloc(length * sizeof(struct pf_position));
  path->length = length;

  /* Reset variables for main iteration. */
  iter_tile = ptile;
  node = pfdm->lattice + tile_index(ptile);
  danger_seg = NULL;
  waited = FALSE;

  for (i = length - 1; i >= 0; i--) {
    bool old_waited = FALSE;

    /* 1: Deal with waiting. */
    if (!node->is_dangerous) {
      if (waited) {
        /* Waited at _this_ tile, need to record it twice in the
         * path. Here we record our state _after_ waiting (e.g.
         * full move points). */
        pos = path->positions + i;
        pos->tile = iter_tile;
        pos->total_EC = node->extra_cost;
        pos->turn = pf_turns(params,
            pf_danger_map_fill_cost_for_full_moves(params, node->cost));
        pos->moves_left = params->move_rate;
        pos->total_MC = ((pos->turn - 1) * params->move_rate
                         + params->moves_left_initially);
        pos->dir_to_next_pos = dir_next;
        pf_finalize_position(params, pos);
        /* Set old_waited so that we record PF_DIR_NONE as a direction at
         * the step we were going to wait. */
        old_waited = TRUE;
        i--;
      }
      /* Update "waited" (node->waited means "waited to get here"). */
      waited = node->waited;
    }

    /* 2: Fill the current position. */
    pos = path->positions + i;
    pos->tile = iter_tile;
    if (!node->is_dangerous || !danger_seg) {
      pos->total_MC = node->cost;
      pos->total_EC = node->extra_cost;
    } else {
      /* When on dangerous tiles, must have a valid danger segment. */
      fc_assert_ret_val(danger_seg != NULL, NULL);
      pos->total_MC = danger_seg->cost;
      pos->total_EC = danger_seg->extra_cost;
    }
    pos->turn = pf_turns(params, pos->total_MC);
    pos->moves_left = pf_moves_left(params, pos->total_MC);
    pos->total_MC -= (pf_move_rate(params)
                      - pf_moves_left_initially(params));
    pos->dir_to_next_pos = (old_waited ? PF_DIR_NONE : dir_next);
    pf_finalize_position(params, pos);

    /* 3: Check if we finished. */
    if (i == 0) {
      /* We should be back at the start now! */
      fc_assert_ret_val(same_pos(iter_tile, params->start_tile), NULL);
      return path;
    }

    /* 4: Calculate the next direction. */
    if (!node->is_dangerous || !danger_seg) {
      /* We are in the normal node and dir_to_here field is valid. */
      dir_next = node->dir_to_here;
      /* d_node->danger_segment is the indicator of what lies ahead.
       * If it's non-NULL, we are entering a danger segment,
       * If it's NULL, we are not on one so danger_seg should be NULL. */
      danger_seg = node->danger_segment;
    } else {
      /* We are in a danger segment. */
      dir_next = danger_seg->dir_to_here;
      danger_seg++;
    }

    /* 5: Step further back. */
    iter_tile = mapstep(iter_tile, DIR_REVERSE(dir_next));
    node = pfdm->lattice + tile_index(iter_tile);
  }

  fc_assert_msg(FALSE, "Cannot get to the starting point!");
  return NULL;
}

/****************************************************************************
  Creating path segment going back from node1 to a safe tile. We need to
  remember the whole segment because any node can be crossed by many danger
  segments.

  Example: be A, B, C and D points safe positions, E a dangerous one.
    A B
     E 
    C D
  We have found dangerous path from A to D, and later one from C to B:
    A B             A B
     \               / 
    C D             C D
  If we didn't save the segment from A to D when a new segment passing by E
  is found, then finding the path from A to D will produce an error. (The
  path is always done in reverse order.) D->dir_to_here will point to E,
  which is correct, but E->dir_to_here will point to C.
****************************************************************************/
static void pf_danger_map_create_segment(struct pf_danger_map *pfdm,
                                         struct pf_danger_node *node1)
{
  struct tile *ptile = PF_MAP(pfdm)->tile;
  struct pf_danger_node *node = pfdm->lattice + tile_index(ptile);
  struct pf_danger_pos *pos;
  int length = 0, i;

#ifdef PF_DEBUG
  if (NULL != node1->danger_segment) {
    log_error("Possible memory leak in pf_danger_map_create_segment().");
  }
#endif /* PF_DEBUG */

  /* First iteration for determining segment length */
  while (node->is_dangerous && PF_DIR_NONE != node->dir_to_here) {
    length++;
    ptile = mapstep(ptile, DIR_REVERSE(node->dir_to_here));
    node = pfdm->lattice + tile_index(ptile);
  }

  /* Allocate memory for segment */
  node1->danger_segment = fc_malloc(length * sizeof(struct pf_danger_pos));

  /* Reset tile and node pointers for main iteration */
  ptile = PF_MAP(pfdm)->tile;
  node = pfdm->lattice + tile_index(ptile);

  /* Now fill the positions */
  for (i = 0, pos = node1->danger_segment; i < length; i++, pos++) {
    /* Record the direction */
    pos->dir_to_here = node->dir_to_here;
    pos->cost = node->cost;
    pos->extra_cost = node->extra_cost;
    if (i == length - 1) {
      /* The last dangerous node contains "waiting" info */
      node1->waited = node->waited;
    }

    /* Step further down the tree */
    ptile = mapstep(ptile, DIR_REVERSE(node->dir_to_here));
    node = pfdm->lattice + tile_index(ptile);
  }

#ifdef PF_DEBUG
  /* Make sure we reached a safe node or the start point */
  fc_assert_ret(!node->is_dangerous || PF_DIR_NONE == node->dir_to_here);
#endif
}

/****************************************************************************
  Adjust cost taking into account possibility of making the move.
****************************************************************************/
static inline int
pf_danger_map_adjust_cost(const struct pf_parameter *params,
                          int cost, bool to_danger, int moves_left)
{
  if (cost == PF_IMPOSSIBLE_MC) {
    return PF_IMPOSSIBLE_MC;
  }

  cost = MIN(cost, pf_move_rate(params));

  if (to_danger && cost >= moves_left) {
    /* We would have to end the turn on a dangerous tile! */
    return PF_IMPOSSIBLE_MC;
  } else {
    return cost;
  }
}

/****************************************************************************
  Primary method for iterative path-finding in presence of danger
  Notes:
  1. Whenever the path-finding stumbles upon a dangerous location, it goes
     into a sub-Dijkstra which processes _only_ dangerous locations, by
     means of a separate queue. When this sub-Dijkstra reaches a safe
     location, it records the segment of the path going across the dangerous
     tiles. Hence danger_segment is an extended (and reversed) version of
     the dir_to_here field (see comment for pf_danger_map_create_segment()).
     It can be re-recorded multiple times as we find shorter and shorter
     routes.
  2. Waiting is realised by inserting the (safe) tile back into the queue
     with a lower priority P. This tile might pop back sooner than P,
     because there might be several copies of it in the queue already. But
     that does not seem to present any problems.
  3. For some purposes, NS_WAITING is just another flavour of NS_PROCESSED,
     since the path to a NS_WAITING tile has already been found.
  4. This algorithm cannot guarantee the best safe segments across dangerous
     region. However it will find a safe segment if there is one. To
     gurantee the best (in terms of total_CC) safe segments across danger,
     supply 'get_EC' which returns small extra on dangerous tiles.

  During the iteration, the node status will be changed:
  A. NS_UNINIT: The node is not initialized, we didn't reach it at all.
  B. NS_INIT: We have initialized the cached values, however, we failed to
     reach this node.
  C. NS_NEW: We have reached this node, but we are not sure it was the best
     path. Dangerous tiles never get upper status.
  D. NS_PROCESSED: Now, we are sure we have the best path.
  E. NS_WAITING: The safe node (never the dangerous ones) is re-inserted in
     the priority queue, as explained above (2.). We need to consider if
     waiting for full moves open or not new possibilities for moving accross
     dangerous areas.
  F. NS_PROCESSED: When finished to consider waiting at the node, revert the
     status to NS_PROCESSED.
  In points D., E., and F., the best path to the node can be considered as
  found (only safe nodes).
****************************************************************************/
static bool pf_danger_map_iterate(struct pf_map *pfm)
{
  struct pf_danger_map *const pfdm = PF_DANGER_MAP(pfm);
  const struct pf_parameter *const params = pf_map_parameter(pfm);
  struct tile *tile = pfm->tile;
  int index = tile_index(tile);
  struct pf_danger_node *node = pfdm->lattice + index;

  /* The previous position is defined by 'tile' (tile pointer), 'node'
   * (the data of the tile for the pf_map), and index (the index of the
   * position in the Freeciv map). */

  for (;;) {
    /* There is no exit from DONT_LEAVE tiles! */
    if (node->behavior != TB_DONT_LEAVE) {
      /* Cost at tile but taking into account waiting. */
      int loc_cost;
      if (node->status != NS_WAITING) {
        loc_cost = node->cost;
      } else {
        /* We have waited, so we have full moves. */
        loc_cost = pf_danger_map_fill_cost_for_full_moves(params,
                                                          node->cost);
      }

      adjc_dir_iterate(tile, tile1, dir) {
        /* Calculate the cost of every adjacent position and set them in
         * the priority queues for next call to pf_danger_map_iterate(). */
        int index1 = tile_index(tile1);
        struct pf_danger_node *node1 = pfdm->lattice + index1;
        int cost;
        int extra = 0;

        /* As for the previous position, 'tile1', 'node1' and 'index1' are
         * defining the adjacent position. */

        if (node1->status == NS_PROCESSED || node1->status == NS_WAITING) {
          /* This gives 15% speedup. Node status already at step D., E.
           * or F. */
          continue;
        }

        /* Initialise target tile if necessary. */
        if (node1->status == NS_UNINIT) {
          /* Only initialize once. See comment for pf_danger_node_init().
           * Node status step A. to B. */
          pf_danger_node_init(pfdm, node1, tile1);
        }

        /* Can we enter this tile at all? */
        if (!CAN_ENTER_NODE(node1)) {
          continue;
        }

        /* Is the move ZOC-ok? */
        if (NULL != params->get_zoc
            && !(node->zoc_number == ZOC_MINE
                 || node1->zoc_number != ZOC_NO)) {
          continue;
        }

        /* Evaluate the cost of the move. */
        if (node1->node_known_type == TILE_UNKNOWN) {
          cost = params->unknown_MC;
        } else {
          cost = params->get_MC(tile, dir, tile1, params);
        }
        if (cost == PF_IMPOSSIBLE_MC) {
          continue;
        }
        cost = pf_danger_map_adjust_cost(params, cost, node1->is_dangerous,
                                         pf_moves_left(params, loc_cost));

        if (cost == PF_IMPOSSIBLE_MC) {
          /* This move is deemed impossible. */
          continue;
        }

        if (node1->behavior == TB_DONT_LEAVE) {
          /* We evaluate moves to TB_DONT_LEAVE tiles as a constant single
           * move for getting straightest paths. */
          cost = SINGLE_MOVE;
        }

        /* Total cost at 'tile1'. */
        cost += loc_cost;

        /* Evaluate the extra cost of the destination, if it's relevant. */
        if (NULL != params->get_EC) {
          extra = node1->extra_tile + node->extra_cost;
        }

        /* Update costs and add to queue, if this is a better route
         * to 'tile1'. */
        if (!node1->is_dangerous) {
          int cost_of_path = pf_total_CC(params, cost, extra);

          if (NS_INIT == node1->status
              || (cost_of_path < pf_total_CC(params, node1->cost,
                                             node1->extra_cost))) {
            /* We are reaching this node for the first time, or we found a
             * better route to 'tile1'. Let's register 'index1' to the
             * priority queue. Node status step B. to C. */
            node1->extra_cost = extra;
            node1->cost = cost;
            node1->dir_to_here = dir;
            if (NULL != node1->danger_segment) {
              /* Clear the previously recorded path back. */
              free(node1->danger_segment);
              node1->danger_segment = NULL;
            }
            if (node->is_dangerous) {
              /* We came from a dangerous tile. So we need to record the
               * path we came from until the previous safe position is
               * found. See comment for pf_danger_map_create_segment(). */
              pf_danger_map_create_segment(pfdm, node1);
            } else {
              /* Maybe clear previously "waited" status of the node. */
              node1->waited = FALSE;
            }
            node1->status = NS_NEW;
            pq_insert(pfdm->queue, index1, -cost_of_path);
          }
        } else {
          /* The procedure is slightly different for dangerous nodes.
           * We will update costs if:
           * 1. we are here for the first time;
           * 2. we can possibly go further across dangerous area; or
           * 3. we can have lower extra and will not overwrite anything
           * useful. Node status step B. to C. */
          if (node1->status == NS_INIT
              || (pf_moves_left(params, cost)
                  > pf_moves_left(params, node1->cost))
              || ((pf_total_CC(params, cost, extra)
                   < pf_total_CC(params, node1->cost, node1->extra_cost))
                  && node1->status == NS_PROCESSED)) {
            node1->extra_cost = extra;
            node1->cost = cost;
            node1->dir_to_here = dir;
            node1->status = NS_NEW;
            node1->waited = (node->status == NS_WAITING);
            /* Extra costs of all nodes in danger_queue are equal! */
            pq_insert(pfdm->danger_queue, index1, -cost);
          }
        }
      } adjc_dir_iterate_end;
    }

    if (NS_WAITING == node->status) {
      /* Node status final step E. to F. */
#ifdef PF_DEBUG
      fc_assert(!node->is_dangerous);
#endif
      node->status = NS_PROCESSED;
    } else if (!node->is_dangerous
               && (pf_moves_left(params, node->cost)
                   < pf_move_rate(params))) {
      int fc, cc;
      /* Consider waiting at this node. To do it, put it back into queue.
       * Node status final step D. to E. */
      fc = pf_danger_map_fill_cost_for_full_moves(params, node->cost);
      cc = pf_total_CC(params, fc, node->extra_cost);
      node->status = NS_WAITING;
      pq_insert(pfdm->queue, index, -cc);
    }

    /* Get the next node (the index with the highest priority). First try
     * to get it from danger_queue. */
    if (pq_remove(pfdm->danger_queue, &index)) {
      /* Change the pf_map iterator and reset data. */
      tile = index_to_tile(index);
      pfm->tile = tile;
      node = pfdm->lattice + index;
    } else {
      /* No dangerous nodes to process, go for a safe one. */
      do {
        if (!pq_remove(pfdm->queue, &index)) {
          /* No more indexes in the priority queue, iteration end. */
          return FALSE;
        }
      } while (pfdm->lattice[index].status == NS_PROCESSED);

      /* Change the pf_map iterator and reset data. */
      tile = index_to_tile(index);
      pfm->tile = tile;
      node = pfdm->lattice + index;
      if (NS_WAITING != node->status) {
        /* Node status step C. and D. */
#ifdef PF_DEBUG
        fc_assert(!node->is_dangerous);
#endif
        node->status = NS_PROCESSED;
        return TRUE;
      }
    }

#ifdef PF_DEBUG
    fc_assert(NS_INIT < node->status);

    if (NS_WAITING == node->status) {
      /* We've already returned this node once, skip it. */
      log_debug("Considering waiting at (%d, %d)", TILE_XY(tile));
    } else if (node->is_dangerous) {
      /* We don't return dangerous tiles. */
      log_debug("Reached dangerous tile (%d, %d)", TILE_XY(tile));
    }
#endif
  }

  log_error("%s(): internal error.", __FUNCTION__);
  return FALSE;
}

/****************************************************************************
  Iterate the map until 'ptile' is reached.
****************************************************************************/
static inline bool pf_danger_map_iterate_until(struct pf_danger_map *pfdm,
                                               struct tile *ptile)
{
  struct pf_map *pfm = PF_MAP(pfdm);
  struct pf_danger_node *node = pfdm->lattice + tile_index(ptile);

  /* Start position is handled in every function calling this function. */

  if (NS_UNINIT == node->status) {
    /* Initialize the node, for doing the following tests. */
    pf_danger_node_init(pfdm, node, ptile);
  }

  /* Simpliciation: if we cannot enter this node at all, don't iterate the
   * whole map. */
  if (!CAN_ENTER_NODE(node) || node->is_dangerous) {
    return FALSE;
  }

  while (NS_PROCESSED != node->status && NS_WAITING != node->status) {
    if (!pf_map_iterate(pfm)) {
      /* All reachable destination have been iterated, 'ptile' is
       * unreachable. */
      return FALSE;
    }
  }

  return TRUE;
}

/****************************************************************************
  Return the move cost at ptile. If ptile has not been reached yet, iterate
  the map until we reach it or run out of map. This function returns
  PF_IMPOSSIBLE_MC on unreachable positions.
****************************************************************************/
static int pf_danger_map_move_cost(struct pf_map *pfm, struct tile *ptile)
{
  struct pf_danger_map *pfdm = PF_DANGER_MAP(pfm);

  if (same_pos(ptile, pfm->params.start_tile)) {
    return 0;
  } else if (pf_danger_map_iterate_until(pfdm, ptile)) {
    return (pfdm->lattice[tile_index(ptile)].cost
            - pf_move_rate(pf_map_parameter(pfm))
            + pf_moves_left_initially(pf_map_parameter(pfm)));
  } else {
    return PF_IMPOSSIBLE_MC;
  }
}

/****************************************************************************
  Return the path to ptile. If ptile has not been reached yet, iterate the
  map until we reach it or run out of map.
****************************************************************************/
static struct pf_path *pf_danger_map_path(struct pf_map *pfm,
                                          struct tile *ptile)
{
  struct pf_danger_map *pfdm = PF_DANGER_MAP(pfm);

  if (same_pos(ptile, pfm->params.start_tile)) {
    return pf_path_new_to_start_tile(pf_map_parameter(pfm));
  } else if (pf_danger_map_iterate_until(pfdm, ptile)) {
    return pf_danger_map_construct_path(pfdm, ptile);
  } else {
    return NULL;
  }
}

/***************************************************************************
  Get info about position at ptile and put it in pos . If ptile has not been
  reached yet, iterate the map until we reach it. Should _always_ check the
  return value, for the position might be unreachable.
***************************************************************************/
static bool pf_danger_map_position(struct pf_map *pfm, struct tile *ptile,
                                   struct pf_position *pos)
{
  struct pf_danger_map *pfdm = PF_DANGER_MAP(pfm);

  if (same_pos(ptile, pfm->params.start_tile)) {
    pf_position_fill_start_tile(pos, pf_map_parameter(pfm));
    return TRUE;
  } else if (pf_danger_map_iterate_until(pfdm, ptile)) {
    pf_danger_map_fill_position(pfdm, ptile, pos);
    return TRUE;
  } else {
    return FALSE;
  }
}

/****************************************************************************
  'pf_danger_map' destructor.
****************************************************************************/
static void pf_danger_map_destroy(struct pf_map *pfm)
{
  struct pf_danger_map *pfdm = PF_DANGER_MAP(pfm);
  struct pf_danger_node *node;
  int i;

  /* Need to clean up the dangling danger segments. */
  for (i = 0, node = pfdm->lattice; i < MAP_INDEX_SIZE; i++, node++) {
    if (node->danger_segment) {
      free(node->danger_segment);
    }
  }
  free(pfdm->lattice);
  pq_destroy(pfdm->queue);
  pq_destroy(pfdm->danger_queue);
  free(pfdm);
}

/****************************************************************************
  'pf_danger_map' constructor.
****************************************************************************/
static struct pf_map *pf_danger_map_new(const struct pf_parameter *parameter)
{
  struct pf_danger_map *pfdm;
  struct pf_map *base_map;
  struct pf_parameter *params;
  struct pf_danger_node *node;

  pfdm = fc_malloc(sizeof(*pfdm));
  base_map = &pfdm->base_map;
  params = &base_map->params;
#ifdef PF_DEBUG
  /* Set the mode, used for cast check. */
  base_map->mode = PF_DANGER;
#endif /* PF_DEBUG */

  /* Allocate the map. */
  pfdm->lattice = fc_calloc(MAP_INDEX_SIZE, sizeof(struct pf_danger_node));
  pfdm->queue = pq_create(INITIAL_QUEUE_SIZE);
  pfdm->danger_queue = pq_create(INITIAL_QUEUE_SIZE);

  /* 'get_MC' callback must be set. */
  fc_assert_ret_val(parameter->get_MC != NULL, NULL);

  /* 'is_pos_dangerous' callback must be set. */
  fc_assert_ret_val(parameter->is_pos_dangerous != NULL, NULL);

  /* Copy parameters */
  *params = *parameter;

  /* Initialize virtual function table. */
  base_map->destroy = pf_danger_map_destroy;
  base_map->get_move_cost = pf_danger_map_move_cost;
  base_map->get_path = pf_danger_map_path;
  base_map->get_position = pf_danger_map_position;
  base_map->iterate = pf_danger_map_iterate;

  /* Initialise the iterator. */
  base_map->tile = params->start_tile;

  /* Initialise starting node. */
  node = pfdm->lattice + tile_index(params->start_tile);
  pf_danger_node_init(pfdm, node, params->start_tile);
  /* This makes calculations of turn/moves_left more convenient, but we
   * need to subtract this value before we return cost to the user. Note
   * that cost may be negative if moves_left_initially > move_rate
   * (see pf_turns()). */
  node->cost = pf_move_rate(params) - pf_moves_left_initially(params);
  node->extra_cost = 0;
  node->dir_to_here = PF_DIR_NONE;
  node->status = (node->is_dangerous ? NS_NEW : NS_PROCESSED);

  return PF_MAP(pfdm);
}


/* ================= Specific pf_fuel_* mode structures ================== */

/* Fuel path-finding maps are used for units which need to refuel. Usually
 * for air units such as planes or missiles.
 *
 * A big difference with the danger path-finding maps is that the tiles
 * which are not refuel points are not considered as dangerous because the
 * server uses to move the units at the end of the turn to refuels points. */

/* Node definition. Note we try to have the smallest data as possible. */
struct pf_fuel_node {
  signed short cost;    /* total_MC. 'cost' may be negative, see comment in
                         * pf_turns(). */
  unsigned extra_cost;  /* total_EC. Can be huge, (higher than 'cost'). */
  unsigned moves_left : 12; /* Moves left at this position. */
  signed dir_to_here : 4; /* Direction from which we came. It can be either
                           * an 'enum direction8' or PF_DIR_NONE (so we need
                           * 4 bits instead of 3). */
  unsigned status : 3;  /* 'enum pf_node_status' really. */

  /* Cached values */
  bool can_invade : 1;
  unsigned node_known_type : 2; /* 'enum known_type' really. */
  unsigned behavior : 2;        /* 'enum tile_behavior' really. */
  unsigned zoc_number : 2;      /* 'enum pf_zoc_type' really. */
  bool is_enemy_tile : 1;
  bool waited : 1;              /* TRUE if waited to get here. */
  unsigned moves_left_req : 12; /* The minimum required moves left to reach
                                 * this tile. It the number of moves we need
                                 * to reach the nearest refuel point. A
                                 * value of 0 means this is a refuel point.
                                 * FIXME: this is right only for units with
                                 * constant move costs! */
  unsigned short extra_tile;    /* EC */

  /* Segment leading across the danger area back to the nearest safe node:
   * need to remeber costs and stuff. */
  unsigned size_alloc : 8;      /* The number of allocated
                                 * 'struct pf_fuel_pos'. */
  struct pf_fuel_pos {
    signed short cost;          /* See comment above. */
    unsigned extra_cost;        /* See comment above. */
    unsigned moves_left : 12;   /* See comment above. */
    signed dir_to_here : 4;     /* See comment above. */
  } *fuel_segment;
};

/* Derived structure of struct pf_map. */
struct pf_fuel_map {
  struct pf_map base_map;       /* Base structure, must be the first! */

  struct pqueue *queue;         /* Queue of nodes we have reached but not
                                 * processed yet (NS_NEW), sorted by their
                                 * total_CC */
  struct pqueue *out_of_fuel_queue; /* Dangerous positions go there */
  struct pf_fuel_node *lattice; /* Lattice of nodes */
};

/* Up-cast macro. */
#ifdef PF_DEBUG
static inline struct pf_fuel_map *
pf_fuel_map_check(struct pf_map *pfm, const char *file,
                  const char *function, int line)
{
  fc_assert_full(file, function, line,
                 NULL != pfm && PF_FUEL == pfm->mode,
                 return NULL, "Wrong pf_map to pf_fuel_map conversion.");
  return (struct pf_fuel_map *) pfm;
}
#define PF_FUEL_MAP(pfm) \
  pf_fuel_map_check(pfm, __FILE__, __FUNCTION__, __FC_LINE__)
#else
#define PF_FUEL_MAP(pfm) ((struct pf_fuel_map *) (pfm))
#endif /* PF_DEBUG */

/* =================  Specific pf_fuel_* mode functions ================== */

/****************************************************************************
  Calculates cached values of the target node. Set the node status to
  NS_INIT to avoid recalculating all values.
****************************************************************************/
static void pf_fuel_node_init(struct pf_fuel_map *pffm,
                              struct pf_fuel_node *node,
                              struct tile *ptile)
{
  const struct pf_parameter *params = pf_map_parameter(PF_MAP(pffm));

#ifdef PF_DEBUG
  fc_assert(NS_UNINIT == node->status);
  /* Else, not a critical problem, but waste of time. */
#endif

  /* Establish the "known" status of node. */
  if (params->omniscience) {
    node->node_known_type = TILE_KNOWN_SEEN;
  } else {
    node->node_known_type = tile_get_known(ptile, params->owner);
  }

  /* Establish the tile behavior. */
  if (NULL != params->get_TB) {
    node->behavior = params->get_TB(ptile, node->node_known_type, params);
  } else {
    /* The default. */
    node->behavior = TB_NORMAL;
  }

  if (NULL != params->get_zoc) {
    struct city *pcity = tile_city(ptile);
    struct terrain *pterrain = tile_terrain(ptile);
    bool my_zoc = (NULL != pcity || pterrain == T_UNKNOWN
                   || terrain_has_flag(pterrain, TER_OCEANIC)
                   || params->get_zoc(params->owner, ptile));
    /* ZoC rules cannot prevent us from moving into/attacking an occupied
     * tile. Other rules can, but we don't care about them here. */
    bool occupied = (unit_list_size(ptile->units) > 0 || NULL != pcity);

    /* ZOC_MINE means can move unrestricted from/into it, ZOC_ALLIED means
     * can move unrestricted into it, but not necessarily from it. */
    node->zoc_number = (my_zoc ? ZOC_MINE
                        : (occupied ? ZOC_ALLIED : ZOC_NO));
#ifdef ZERO_VARIABLES_FOR_SEARCHING
  } else {
    /* Nodes are allocated by fc_calloc(), so should be already set to 0. */
    node->zoc_number = 0;
#endif
  }

  /* Evaluate the extra cost of the destination. */
  if (NULL != params->get_EC) {
    node->extra_tile = params->get_EC(ptile, node->node_known_type, params);
#ifdef ZERO_VARIABLES_FOR_SEARCHING
  } else {
    /* Nodes are allocated by fc_calloc(), so should be already set to 0. */
    node->extra_tile = 0;
#endif
  }

  if (NULL != params->can_invade_tile) {
    node->can_invade = params->can_invade_tile(params->owner, ptile);
  } else {
    node->can_invade = TRUE;
  }

  if (is_enemy_unit_tile(ptile, params->owner)
      || (is_enemy_city_tile(ptile, params->owner))) {
    node->is_enemy_tile = TRUE;
#ifdef ZERO_VARIABLES_FOR_SEARCHING
    /* Nodes are allocated by fc_calloc(), so should be already set to 0. */
    node->moves_left_req = 0; /* Attack is always possible theorically. */
#endif
  } else {
#ifdef ZERO_VARIABLES_FOR_SEARCHING
    /* Nodes are allocated by fc_calloc(), so should be already set to
     * FALSE. */
    node->is_enemy_tile = FALSE;
#endif
    node->moves_left_req =
      params->get_moves_left_req(ptile, node->node_known_type, params);
  }

#ifdef ZERO_VARIABLES_FOR_SEARCHING
  /* Nodes are allocated by fc_calloc(), so should be already set to
   * FALSE. */
  node->waited = FALSE;
#endif

  node->status = NS_INIT;
}

/****************************************************************************
  Returns whether this node is dangerous or not.
****************************************************************************/
static inline bool pf_fuel_node_dangerous(const struct pf_fuel_node *node)
{
  return (NULL == node->fuel_segment
          || (node->moves_left < node->moves_left_req
              && !node->is_enemy_tile));
}

/****************************************************************************
  Finalize the fuel position.
****************************************************************************/
static inline void
pf_fuel_finalize_position_base(const struct pf_parameter *param,
                               struct pf_position *pos,
                               int cost, int moves_left)
{
  int move_rate = param->move_rate;

  if (move_rate > 0) {
    pos->turn = pf_turns(param, cost);
    pos->fuel_left = (moves_left - 1) / move_rate + 1;
    pos->moves_left = (moves_left - 1) % move_rate + 1;
  } else {
    /* This unit cannot move by itself. */
    pos->turn = same_pos(pos->tile, param->start_tile) ? 0 : FC_INFINITY;
    pos->fuel_left = 0;
    pos->moves_left = 0;
  }
}

/****************************************************************************
  Finalize the fuel position. If we have a fuel segment, then use it.
****************************************************************************/
static inline void
pf_fuel_finalize_position(struct pf_position *pos,
                          const struct pf_parameter *params,
                          const struct pf_fuel_node *node,
                          const struct pf_fuel_pos *head)
{
  if (head) {
    pf_fuel_finalize_position_base(params, pos,
                                   head->cost, head->moves_left);
  } else {
    pf_fuel_finalize_position_base(params, pos,
                                   node->cost, node->moves_left);
  }
}

/****************************************************************************
  Fill in the position which must be discovered already. A helper
  for pf_fuel_map_position(). This also "finalizes" the position.
****************************************************************************/
static void pf_fuel_map_fill_position(const struct pf_fuel_map *pffm,
                                      struct tile *ptile,
                                      struct pf_position *pos)
{
  int index = tile_index(ptile);
  struct pf_fuel_node *node = pffm->lattice + index;
  struct pf_fuel_pos *head = node->fuel_segment;
  const struct pf_parameter *params = pf_map_parameter(PF_MAP(pffm));

#ifdef PF_DEBUG
  fc_assert_ret_msg(NS_PROCESSED == node->status
                    || NS_WAITING == node->status,
                    "Unreached destination (%d, %d).", TILE_XY(ptile));
#endif /* PF_DEBUG */

  pos->tile = ptile;
  pos->total_EC = head->extra_cost;
  pos->total_MC = (head->cost - pf_move_rate(params)
                   + pf_moves_left_initially(params));
  pos->dir_to_here = head->dir_to_here;
  pos->dir_to_next_pos = PF_DIR_NONE;   /* This field does not apply. */
  pf_fuel_finalize_position(pos, params, node, head);
}

/****************************************************************************
  This function returns the fill cost needed for a position, to get full
  moves at the next turn. This would be called only when the status is
  NS_WAITING.
****************************************************************************/
static inline int
pf_fuel_map_fill_cost_for_full_moves(const struct pf_parameter *param,
                                     int cost, int moves_left)
{
  return cost + moves_left % param->move_rate;
}

/****************************************************************************
  Read off the path to the node 'ptile', but with fuel danger.
****************************************************************************/
static struct pf_path *
pf_fuel_map_construct_path(const struct pf_fuel_map *pffm,
                           struct tile *ptile)
{
  struct pf_path *path = fc_malloc(sizeof(*path));
  enum direction8 dir_next = PF_DIR_NONE;
  struct pf_fuel_pos *segment = NULL;
  struct pf_fuel_node *node = pffm->lattice + tile_index(ptile);
  /* There is no need to wait at destination if it is a refuel point. */
  bool waited = (0 != node->moves_left_req ? node->waited : FALSE);
  int length = 1;
  struct tile *iter_tile = ptile;
  const struct pf_parameter *params = pf_map_parameter(PF_MAP(pffm));
  struct pf_position *pos;
  int i;

#ifdef PF_DEBUG
  fc_assert_ret_val_msg(NS_PROCESSED == node->status
                        || NS_WAITING == node->status, NULL,
                        "Unreached destination (%d, %d).",
                        TILE_XY(ptile));
#endif /* PF_DEBUG */

  /* First iterate to find path length. */
  /* NB: the start point could be reached in the middle of a segment.
   * See comment for pf_fuel_map_create_segment(). */
  while (!same_pos(iter_tile, params->start_tile)
         || (segment && PF_DIR_NONE != segment->dir_to_here)) {

    if (node->moves_left_req == 0) {
      /* A refuel point. */
      if (waited) {
        length += 2;
      } else {
        length++;
      }
      waited = node->waited;
    } else {
      length++;
    }

    if (node->moves_left_req == 0 || !segment) {
      segment = node->fuel_segment;
    }

    if (segment) {
      /* We are in a danger segment. */
      dir_next = segment->dir_to_here;
      segment++;
    } else {
      /* Classical node. */
      dir_next = node->dir_to_here;
    }

    /* Step backward. */
    iter_tile = mapstep(iter_tile, DIR_REVERSE(dir_next));
    node = pffm->lattice + tile_index(iter_tile);
  }
  if (node->moves_left_req == 0 && waited) {
    /* We wait at the start point */
    length++;
  }

  /* Allocate memory for path. */
  path->positions = fc_malloc(length * sizeof(struct pf_position));
  path->length = length;

  /* Reset variables for main iteration. */
  iter_tile = ptile;
  node = pffm->lattice + tile_index(ptile);
  segment = NULL;
  /* There is no need to wait at destination if it is a refuel point. */
  waited = (0 != node->moves_left_req ? node->waited : FALSE);
  dir_next = PF_DIR_NONE;

  for (i = length - 1; i >= 0; i--) {
    bool old_waited = FALSE;

    if (node->moves_left_req == 0 || !segment) {
      segment = node->fuel_segment;
    }

    /* 1: Deal with waiting. */
    if (node->moves_left_req == 0) {
      if (waited) {
        /* Waited at _this_ tile, need to record it twice in the
         * path. Here we record our state _after_ waiting (e.g.
         * full move points). */
        pos = path->positions + i;
        pos->tile = iter_tile;
        if (NULL != segment) {
          pos->total_EC = segment->extra_cost;
          pos->turn = pf_turns(params,
              pf_fuel_map_fill_cost_for_full_moves(params, segment->cost,
                                                   segment->moves_left));
        } else {
          pos->total_EC = node->extra_cost;
          pos->turn = pf_turns(params,
              pf_fuel_map_fill_cost_for_full_moves(params, node->cost,
                                                   node->moves_left));
        }
        pos->total_MC = ((pos->turn - 1) * params->move_rate
                         + params->moves_left_initially);
        pos->moves_left = params->move_rate;
        pos->fuel_left = params->fuel;
        pos->dir_to_next_pos = dir_next;
        /* Set old_waited so that we record PF_DIR_NONE as a direction at
         * the step we were going to wait. */
        old_waited = TRUE;
        i--;
      }
      /* Update "waited" (node->waited means "waited to get here"). */
      waited = node->waited;
    }

    /* 2: Fill the current position. */
    pos = path->positions + i;
    pos->tile = iter_tile;
    if (segment) {
      pos->total_MC = segment->cost;
      pos->total_EC = segment->extra_cost;
    } else {
      pos->total_MC = node->cost;
      pos->total_EC = node->extra_cost;
    }
    pos->total_MC -= (pf_move_rate(params)
                      - pf_moves_left_initially(params));
    pos->dir_to_next_pos = (old_waited ? PF_DIR_NONE : dir_next);
    pf_fuel_finalize_position(pos, params, node, segment);

    /* 3: Check if we finished. */
    if (i == 0) {
      /* We should be back at the start now! */
      fc_assert_ret_val(same_pos(iter_tile, params->start_tile), NULL);
      return path;
    }

    /* 4: Calculate the next direction. */
    if (segment) {
      /* We are in a fuel segment. */
      dir_next = segment->dir_to_here;
      segment++;
    } else {
      /* Classical node. */
      dir_next = node->dir_to_here;
    }

    /* 5: Step further back. */
    iter_tile = mapstep(iter_tile, DIR_REVERSE(dir_next));
    node = pffm->lattice + tile_index(iter_tile);
  }

  fc_assert_msg(FALSE, "Cannot get to the starting point!");
  return NULL;
}

/****************************************************************************
  Creating path segment going back from node1 to a safe tile. We need to
  remember the whole segment because any node can be crossed by many fuel
  segments.

  Example: be A, a refuel point, A and C not. We start the path from B and
  have only (3 * SINGLE_MOVE) moves lefts:
    A B C
  B cannot move to C because we would have only (1 * SINGLE_MOVE) move left
  reaching it, and the refuel point is too far. So C->moves_left_req =
  (4 * SINGLE_MOVE).
  The path would be to return to A, wait the end of turn to get full moves,
  and go to C. In a single line: B A B C. So, the point B would be reached
  twice. but, it needs to stores different data for B->cost, B->extra_cost,
  B->moves_left, and B->dir_to_here. That's why we need to record every
  path to unsafe nodes (not for refuel points).
****************************************************************************/
static void pf_fuel_map_create_segment(struct pf_fuel_map *pffm,
                                       struct tile *tile1,
                                       struct pf_fuel_node *node1)
{
  struct tile *ptile = tile1;
  struct pf_fuel_node *node = node1;
  struct pf_fuel_pos *pos;
  int length = 1;
  int i;

  /* First iteration for determining segment length. */
  do {
    length++;
    ptile = mapstep(ptile, DIR_REVERSE(node->dir_to_here));
    node = pffm->lattice + tile_index(ptile);
    /* 0 != node->moves_left_req means this is not a refuel point.
     * PF_DIR_NONE != node->dir_to_here means we are not at start point. */
  } while (0 != node->moves_left_req && PF_DIR_NONE != node->dir_to_here);

  /* Allocate memory for segment, if needed (for performance). Maybe we can
   * use the previous one. As nodes are allocated with fc_calloc(), initial
   * node1->size_alloc is set to 0. */
  if (length > node1->size_alloc) {
    /* We don't nee fc_realloc() because we don't need to keep old data. */
    if (NULL != node1->fuel_segment) {
      free(node1->fuel_segment);        /* Clear previous segment. */
    }
    node1->fuel_segment = fc_malloc(length * sizeof(*node1->fuel_segment));
#ifdef PF_DEBUG
    fc_assert(256 > length);    /* node1->size_alloc has only 8 bits. */
#endif
    node1->size_alloc = length;
  }

  /* Reset tile and node pointers for main iteration. */
  ptile = tile1;
  node = node1;

  /* Now fill the positions. */
  for (i = 0, pos = node1->fuel_segment; i < length; i++, pos++) {
    /* Record the direction. */
    pos->dir_to_here = node->dir_to_here;
    pos->cost = node->cost;
    pos->extra_cost = node->extra_cost;
    pos->moves_left = node->moves_left;
    if (i == length - 2) {
      /* The node before the last contains "waiting" info. */
      node1->waited = node->waited;
    } else if (i == length - 1) {
      break;
    }

    /* Step further down the tree. */
    ptile = mapstep(ptile, DIR_REVERSE(node->dir_to_here));
    node = pffm->lattice + tile_index(ptile);
  }

#ifdef PF_DEBUG
  /* Make sure we reached a safe node, or the start tile. */
  fc_assert_ret(0 == node->moves_left_req
                || PF_DIR_NONE == node->dir_to_here);
#endif
}

/****************************************************************************
  This function returns whether a unit with or without fuel can attack.

  moves_left: moves left before the attack.
  moves_left_req: required moves left to hold on the tile after attacking.
****************************************************************************/
static inline bool
pf_fuel_map_attack_is_possible(const struct pf_parameter *param,
                               int moves_left, int moves_left_req)
{
  if (BV_ISSET(param->unit_flags, F_ONEATTACK)) {
    if (param->fuel == 1) {
      /* Case missile */
      return TRUE;
    } else {
      /* Case Bombers */
      if (moves_left <= param->move_rate) {
        /* We are in the last turn of fuel, don't attack */
        return FALSE;
      } else {
        return TRUE;
      }
    }
  } else {
    /* Case fighters */
    if (moves_left - SINGLE_MOVE < moves_left_req) {
      return FALSE;
    } else {
      return TRUE;
    }
  }
}

/****************************************************************************
  Primary method for iterative path-finding in presence of fuel dangers.
  Notes:
  1. Whenever the path-finding stumbles upon a dangerous location, it goes
     into a sub-Dijkstra which processes _only_ dangerous locations, by
     means of a separate queue. When this sub-Dijkstra reaches any
     location, it records the segment of the path going across the unsafe
     tiles. Hence segment is an extended (and reversed) version of the
     dir_to_here field (see comment for pf_fuel_map_create_segment()). It
     can be re-recorded multiple times as we find shorter and shorter
     routes.
  2. Waiting is realised by inserting the (safe) tile back into the queue
     with a lower priority P. This tile might pop back sooner than P,
     because there might be several copies of it in the queue already. But
     that does not seem to present any problems.
  3. For some purposes, NS_WAITING is just another flavour of NS_PROCESSED,
     since the path to a NS_WAITING tile has already been found.
  4. This algorithm cannot guarantee the best safe segments across dangerous
     region. However it will find a safe segment if there is one. To
     gurantee the best (in terms of total_CC) safe segments across danger,
     supply 'get_EC' which returns small extra on dangerous tiles.

  During the iteration, the node status will be changed:
  A. NS_UNINIT: The node is not initialized, we didn't reach it at all.
  B. NS_INIT: We have initialized the cached values, however, we failed to
     reach this node.
  C. NS_NEW: We have reached this node, but we are not sure it was the best
     path.
  D. NS_PROCESSED: Now, we are sure we have the best path. Not refuel node
     can even be processed.
  E. NS_WAITING: The refuel node is re-inserted in the priority queue, as
     explained above (2.). We need to consider if waiting for full moves
     open or not new possibilities for moving.
  F. NS_PROCESSED: When finished to consider waiting at the node, revert the
     status to NS_PROCESSED.
  In points D., E., and F., the best path to the node can be considered as
  found.
****************************************************************************/
static bool pf_fuel_map_iterate(struct pf_map *pfm)
{
  struct pf_fuel_map *const pffm = PF_FUEL_MAP(pfm);
  const struct pf_parameter *const params = pf_map_parameter(pfm);
  struct tile *tile = pfm->tile;
  int index = tile_index(tile);
  struct pf_fuel_node *node = pffm->lattice + index;

  /* The previous position is defined by 'tile' (tile pointer), 'node'
   * (the data of the tile for the pf_map), and index (the index of the
   * position in the Freeciv map). */

  for (;;) {
    /* There is no exit from DONT_LEAVE tiles! */
    if (node->behavior != TB_DONT_LEAVE) {
      int loc_cost, loc_moves_left;

      if (node->status != NS_WAITING) {
        loc_cost = node->cost;
        loc_moves_left = node->moves_left;
      } else {
        /* Cost and moves left at tile but taking into account waiting. */
        loc_cost = pf_fuel_map_fill_cost_for_full_moves(params, node->cost,
                                                        node->moves_left);
        loc_moves_left = pf_move_rate(params);
      }

      adjc_dir_iterate(tile, tile1, dir) {
        /* Calculate the cost of every adjacent position and set them in
         * the priority queues for next call to pf_fuel_map_iterate(). */
        int index1 = tile_index(tile1);
        struct pf_fuel_node *node1 = pffm->lattice + index1;
        int cost, extra = 0;
        int moves_left, mlr = 0;
        int cost_of_path, old_cost_of_path;
        struct tile *prev_tile;
        struct pf_fuel_pos *pos;

        /* As for the previous position, 'tile1', 'node1' and 'index1' are
         * defining the adjacent position. */

        /* Non-full fuel tiles can be updated even after being processed. */
        if ((NS_PROCESSED == node1->status || NS_WAITING == node1->status)
            && 0 == node1->moves_left_req) {
          /* This gives 15% speedup. */
          continue;
        }

        /* Initialise target tile if necessary */
        if (node1->status == NS_UNINIT) {
          /* Only initialize once. See comment for pf_fuel_node_init().
           * Node status step A. to B. */
          pf_fuel_node_init(pffm, node1, tile1);
        }

        /* Cannot move there, this is an unreachable tile. */
        if (node1->moves_left_req == PF_IMPOSSIBLE_MC) {
          continue;
        }

        /* Can we enter this tile at all? */
        if (!CAN_ENTER_NODE(node1)) {
          continue;
        }

        /* Is the move ZOC-ok? */
        if (NULL != params->get_zoc
            && !(node->zoc_number == ZOC_MINE
                 || node1->zoc_number != ZOC_NO)) {
          continue;
        }

        /* Evaluate the cost of the move. */
        if (node1->node_known_type == TILE_UNKNOWN) {
          cost = params->unknown_MC;
        } else {
          cost = params->get_MC(tile, dir, tile1, params);
        }
        if (cost == PF_IMPOSSIBLE_MC) {
          continue;
        }

        moves_left = loc_moves_left - cost;
        if (moves_left < node1->moves_left_req
            && (!BV_ISSET(params->unit_flags, F_ONEATTACK)
                || 1 != params->fuel
                || 0 >= moves_left)) {
          /* We don't have enough moves left, but missiles
           * can do suicidal attacks. */
          continue;
        }

        if (node1->is_enemy_tile
            && !pf_fuel_map_attack_is_possible(params, loc_moves_left,
                                               node->moves_left_req)) {
          /* We wouldn't have enough moves left after attacking. */
          continue;
        }

        if (node1->behavior == TB_DONT_LEAVE) {
          /* We evaluate moves to TB_DONT_LEAVE tiles as a constant single
           * move for getting straightest paths. */
          cost = SINGLE_MOVE;
        }

        /* Total cost at 'tile1' */
        cost += loc_cost;

        /* Evaluate the extra cost of the destination, if it's relevant. */
        if (NULL != params->get_EC) {
          extra = node1->extra_tile + node->extra_cost;
        }

        /* Update costs and add to queue, if this is a better route
         * to tile1. Case safe tiles or reached directly without waiting. */
        pos = node1->fuel_segment;
        cost_of_path = pf_total_CC(params, cost, extra);
        if (node1->status == NS_INIT) {
          /* Not calculated yet. */
          old_cost_of_path = 0;
        } else if (pos) {
          /* We have a path to this tile. The default values could have been
           * overwritten if we had more moves left to deal with waiting.
           * Then, we have to get back the value of this node to calculate
           * the cost. */
          old_cost_of_path = pf_total_CC(params, pos->cost, pos->extra_cost);
        } else {
          /* Default cost */
          old_cost_of_path = pf_total_CC(params, node1->cost,
                                         node1->extra_cost);
        }

        /* We would prefer to be the nearest possible of a refuel point,
         * especially in the attack case. */
        if (cost_of_path == old_cost_of_path) {
          prev_tile = mapstep(tile1, DIR_REVERSE(pos ? pos->dir_to_here
                                                 : node1->dir_to_here));
          mlr = pffm->lattice[tile_index(prev_tile)].moves_left_req;
        } else {
          prev_tile = NULL;
        }

        /* Step 1: We test if this route is the best to this tile, by a
         * direct way, not taking in account waiting. */

        if (NS_INIT == node1->status
            || cost_of_path < old_cost_of_path
            || (prev_tile && mlr > node->moves_left_req)) {
          /* We are reaching this node for the first time, or we found a
           * better route to 'tile1', or we would have more moves lefts
           * at previous position. Let's register 'index1' to the
           * priority queue. */
          node1->extra_cost = extra;
          node1->cost = cost;
          node1->moves_left = moves_left;
          node1->dir_to_here = dir;
          /* Always record the segment, including when it is not dangerous
           * to move there. */
          pf_fuel_map_create_segment(pffm, tile1, node1);
          /* It is the first part of a fuel segment. */
          node1->waited = (NS_WAITING == node->status);
          node1->waited = TRUE;
          if (NS_PROCESSED != node1->status) {
            /* Node status B. to C. */
            node1->status = NS_NEW;
          } /* else staying at D. */
          if (0 == node1->moves_left_req) {
            pq_insert(pffm->queue, index1, -cost_of_path);
          } else {
            /* Extra costs of all nodes in out_of_fuel_queue are equal! */
            pq_insert(pffm->out_of_fuel_queue, index1, -cost);
          }
          continue;     /* adjc_dir_iterate() */
        }

        /* Step 2: We test if this route could open better routes for other
         * tiles, if we waited somewhere. */

        if (0 == node1->moves_left_req) {
          /* Waiting to cross over a refuel is senseless. */
          continue;     /* adjc_dir_iterate() */
        }

        if (pos) {
          /* We had a path to this tile. Here, we have to use back the
           * current values because we could have more moves left if we
           * waited somewhere. */
          old_cost_of_path = pf_total_CC(params, node1->cost,
                                         node1->extra_cost);
        }

        if (moves_left > node1->moves_left
            || cost_of_path < old_cost_of_path) {
          /* We will update costs if:
           * 1. we would have more moves left than previously on this node.
           * 2. we can have lower extra and will not overwrite anything
           *    useful.
           * Node status step B. to C. or D. to D.*/
          node1->extra_cost = extra;
          node1->cost = cost;
          node1->moves_left = moves_left;
          node1->dir_to_here = dir;
          node1->waited = (NS_WAITING == node->status);
          if (NS_PROCESSED != node1->status) {
            /* Node status B. to C. */
            node1->status = NS_NEW;
          } /* else staying at D. */
          /* Extra costs of all nodes in out_of_fuel_queue are equal! */
          pq_insert(pffm->out_of_fuel_queue, index1, -cost);
        }
      } adjc_dir_iterate_end;
    }

    if (NS_WAITING == node->status) {
      /* Node status final step E. to F. */
#ifdef PF_DEBUG
      fc_assert(0 == node->moves_left_req);
#endif
      node->status = NS_PROCESSED;
    } else if (0 == node->moves_left_req
               && !node->is_enemy_tile
               && node->moves_left < pf_move_rate(params)) {
      int fc, cc;
      /* Consider waiting at this node. To do it, put it back into queue.
       * Node status final step D. to E. */
      node->status = NS_WAITING;
      fc = pf_fuel_map_fill_cost_for_full_moves(params, node->cost,
                                                node->moves_left);
      cc = pf_total_CC(params, fc, node->extra_cost);
      pq_insert(pffm->queue, index, -cc);
    }

    /* Get the next node (the index with the highest priority). First try
     * to get it from danger_queue. */
    if (pq_remove(pffm->out_of_fuel_queue, &index)) {
      /* Change the pf_map iterator and reset data. */
      tile = index_to_tile(index);
      pfm->tile = tile;
      node = pffm->lattice + index;
      if (!pf_fuel_node_dangerous(node)) {
        /* Node status step C. and D. */
#ifdef PF_DEBUG
        fc_assert(0 < node->moves_left_req);
#endif
        node->status = NS_PROCESSED;
        return TRUE;
      }
    } else {
      /* No dangerous nodes to process, go for a safe one. */
      do {
        if (!pq_remove(pffm->queue, &index)) {
          return FALSE;
        }
      } while (pffm->lattice[index].status == NS_PROCESSED);

      /* Change the pf_map iterator and reset data. */
      tile = index_to_tile(index);
      pfm->tile = tile;
      node = pffm->lattice + index;
      if (NS_WAITING != node->status) {
        /* Node status step C. and D. */
#ifdef PF_DEBUG
        fc_assert(0 == node->moves_left_req);
#endif
        node->status = NS_PROCESSED;
        return TRUE;
      }
    }

#ifdef PF_DEBUG
    fc_assert(NS_INIT < node->status);

    if (NS_WAITING == node->status) {
      /* We've already returned this node once, skip it. */
      log_debug("Considering waiting at (%d, %d)", TILE_XY(tile));
    } else if (pf_fuel_node_dangerous(node)) {
      /* We don't return dangerous tiles. */
      log_debug("Reached dangerous tile (%d, %d)", TILE_XY(tile));
    }
#endif /* PF_DEBUG */
  }

  log_error("%s(): internal error.", __FUNCTION__);
  return FALSE;
}

/****************************************************************************
  Iterate the map until 'ptile' is reached.
****************************************************************************/
static inline bool pf_fuel_map_iterate_until(struct pf_fuel_map *pffm,
                                             struct tile *ptile)
{
  struct pf_map *pfm = PF_MAP(pffm);
  struct pf_fuel_node *node = pffm->lattice + tile_index(ptile);

  /* Start position is handled in every function calling this function. */

  if (NS_UNINIT == node->status) {
    /* Initialize the node, for doing the following tests. */
    pf_fuel_node_init(pffm, node, ptile);
  }

  /* Simpliciation: if we cannot enter this node at all, don't iterate the
   * whole map. */
  if (!CAN_ENTER_NODE(node) || PF_IMPOSSIBLE_MC == node->moves_left_req) {
    return FALSE;
  }

  while (NS_PROCESSED != node->status && NS_WAITING != node->status) {
    if (!pf_map_iterate(pfm)) {
      /* All reachable destination have been iterated, 'ptile' is
       * unreachable. */
      return FALSE;
    }
  }

  return TRUE;
}

/****************************************************************************
  Return the move cost at ptile. If 'ptile' has not been reached yet,
  iterate the map until we reach it or run out of map. This function
  returns PF_IMPOSSIBLE_MC on unreachable positions.
****************************************************************************/
static int pf_fuel_map_move_cost(struct pf_map *pfm, struct tile *ptile)
{
  struct pf_fuel_map *pffm = PF_FUEL_MAP(pfm);

  if (same_pos(ptile, pfm->params.start_tile)) {
    return 0;
  } else if (pf_fuel_map_iterate_until(pffm, ptile)) {
    const struct pf_fuel_node *node = pffm->lattice + tile_index(ptile);

    return ((node->fuel_segment ? node->fuel_segment->cost : node->cost)
            - pf_move_rate(pf_map_parameter(pfm))
            + pf_moves_left_initially(pf_map_parameter(pfm)));
  } else {
    return PF_IMPOSSIBLE_MC;
  }
}

/****************************************************************************
  Return the path to ptile. If 'ptile' has not been reached yet, iterate
  the map until we reach it or run out of map.
****************************************************************************/
static struct pf_path *pf_fuel_map_path(struct pf_map *pfm,
                                        struct tile *ptile)
{
  struct pf_fuel_map *pffm = PF_FUEL_MAP(pfm);

  if (same_pos(ptile, pfm->params.start_tile)) {
    return pf_path_new_to_start_tile(pf_map_parameter(pfm));
  } else if (pf_fuel_map_iterate_until(pffm, ptile)) {
    return pf_fuel_map_construct_path(pffm, ptile);
  } else {
    return NULL;
  }
}

/****************************************************************************
  Get info about position at ptile and put it in pos. If 'ptile' has not
  been reached yet, iterate the map until we reach it. Should _always_
  check the return value, forthe position might be unreachable.
****************************************************************************/
static bool pf_fuel_map_position(struct pf_map *pfm, struct tile *ptile,
                                 struct pf_position *pos)
{
  struct pf_fuel_map *pffm = PF_FUEL_MAP(pfm);

  if (same_pos(ptile, pfm->params.start_tile)) {
    pf_position_fill_start_tile(pos, pf_map_parameter(pfm));
    return TRUE;
  } else if (pf_fuel_map_iterate_until(pffm, ptile)) {
    pf_fuel_map_fill_position(pffm, ptile, pos);
    return TRUE;
  } else {
    return FALSE;
  }
}

/****************************************************************************
  'pf_fuel_map' destructor.
****************************************************************************/
static void pf_fuel_map_destroy(struct pf_map *pfm)
{
  struct pf_fuel_map *pffm = PF_FUEL_MAP(pfm);
  struct pf_fuel_node *node;
  int i;

  /* Need to clean up the dangling fuel segments. */
  for (i = 0, node = pffm->lattice; i < MAP_INDEX_SIZE; i++, node++) {
    if (node->fuel_segment) {
      free(node->fuel_segment);
    }
  }
  free(pffm->lattice);
  pq_destroy(pffm->queue);
  pq_destroy(pffm->out_of_fuel_queue);
  free(pffm);
}

/****************************************************************************
  'pf_fuel_map' constructor.
****************************************************************************/
static struct pf_map *pf_fuel_map_new(const struct pf_parameter *parameter)
{
  struct pf_fuel_map *pffm;
  struct pf_map *base_map;
  struct pf_parameter *params;
  struct pf_fuel_node *node;

  pffm = fc_malloc(sizeof(*pffm));
  base_map = &pffm->base_map;
  params = &base_map->params;
#ifdef PF_DEBUG
  /* Set the mode, used for cast check. */
  base_map->mode = PF_FUEL;
#endif /* PF_DEBUG */

  /* Allocate the map. */
  pffm->lattice = fc_calloc(MAP_INDEX_SIZE, sizeof(struct pf_fuel_node));
  pffm->queue = pq_create(INITIAL_QUEUE_SIZE);
  pffm->out_of_fuel_queue = pq_create(INITIAL_QUEUE_SIZE);

  /* 'get_MC' callback must be set. */
  fc_assert_ret_val(parameter->get_MC != NULL, NULL);

  /* 'get_moves_left_req' callback must be set. */
  fc_assert_ret_val(parameter->get_moves_left_req != NULL, NULL);

  /* Copy parameters. */
  *params = *parameter;

  /* Initialize virtual function table. */
  base_map->destroy = pf_fuel_map_destroy;
  base_map->get_move_cost = pf_fuel_map_move_cost;
  base_map->get_path = pf_fuel_map_path;
  base_map->get_position = pf_fuel_map_position;
  base_map->iterate = pf_fuel_map_iterate;

  /* Initialise the iterator. */
  base_map->tile = params->start_tile;

  /* Initialise starting node. */
  node = pffm->lattice + tile_index(params->start_tile);
  pf_fuel_node_init(pffm, node, params->start_tile);
  /* This makes calculations of turn/moves_left more convenient, but we
   * need to subtract this value before we return cost to the user. Note
   * that cost may be negative if moves_left_initially > move_rate
   * (see pf_turns()). */
  node->moves_left = pf_moves_left_initially(params);
  node->cost = pf_move_rate(params) - node->moves_left;
  node->extra_cost = 0;
  node->dir_to_here = PF_DIR_NONE;
  node->status = NS_PROCESSED;

  return PF_MAP(pffm);
}



/* ====================== pf_map public functions ======================= */

/****************************************************************************
  Factory function to create a new map according to the parameter.
  Does not do any iterations.
****************************************************************************/
struct pf_map *pf_map_new(const struct pf_parameter *parameter)
{
  if (parameter->is_pos_dangerous) {
    if (parameter->get_moves_left_req) {
      log_error("path finding code cannot deal with dangers "
                "and fuel together.");
    }
    if (parameter->get_costs) {
      log_error("jumbo callbacks for danger maps are not yet implemented.");
    }
    return pf_danger_map_new(parameter);
  } else if (parameter->get_moves_left_req) {
    if (parameter->get_costs) {
      log_error("jumbo callbacks for fuel maps are not yet implemented.");
    }
    return pf_fuel_map_new(parameter);
  }

  return pf_normal_map_new(parameter);
}

/****************************************************************************
  After usage the map must be destroyed.
****************************************************************************/
void pf_map_destroy(struct pf_map *pfm)
{
  pfm->destroy(pfm);
}

/****************************************************************************
  Tries to find the minimal move cost to reach ptile. Returns
  PF_IMPOSSIBLE_MC if not reachable. If ptile has not been reached yet,
  iterate the map until we reach it or run out of map.
****************************************************************************/
int pf_map_move_cost(struct pf_map *pfm, struct tile *ptile)
{
  return pfm->get_move_cost(pfm, ptile);
}

/***************************************************************************
  Tries to find the best path in the given map to the position ptile.
  If NULL is returned no path could be found. The pf_path_last_position()
  of such path would be the same (almost) as the result of the call to 
  pf_map_position(). If ptile has not been reached yet, iterate the map
  until we reach it or run out of map.
****************************************************************************/
struct pf_path *pf_map_path(struct pf_map *pfm, struct tile *ptile)
{
  return pfm->get_path(pfm, ptile);
}

/****************************************************************************
  Get info about position at ptile and put it in pos. If ptile has not been
  reached yet, iterate the map until we reach it. Should _always_ check the
  return value, forthe position might be unreachable.
****************************************************************************/
bool pf_map_position(struct pf_map *pfm, struct tile *ptile,
                     struct pf_position *pos)
{
  return pfm->get_position(pfm, ptile, pos);
}

/****************************************************************************
  Iterates the path-finding algorithm one step further, to the next nearest
  position. This full info on this position and the best path to it can be
  obtained using pf_map_iter_move_cost(), pf_map_iter_path(), and
  pf_map_iter_position() correspondingly. Returns FALSE if no further
  positions are available in this map.

  NB: If pf_map_move_cost(pfm, ptile), pf_map_path(pfm, ptile), or
  pf_map_position(pfm, ptile) has been called before the call to
  pf_map_iterate(), the iteration will resume from 'ptile'.
****************************************************************************/
bool pf_map_iterate(struct pf_map *pfm)
{
  if (NULL == pfm->tile) {
    /* The end of the iteration was already reached. Don't try to iterate
     * again. */
    return FALSE;
  }

  if (0 >= pf_move_rate(pf_map_parameter(pfm))) {
    /* The unit cannot moves itself. */
    pfm->tile = NULL;
    return FALSE;
  }

  if (!pfm->iterate(pfm)) {
    /* End of iteration. */
    pfm->tile = NULL;
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Return the current tile.
****************************************************************************/
struct tile *pf_map_iter(struct pf_map *pfm)
{
  return pfm->tile;
}

/****************************************************************************
  Return the move cost at the current position. This is equivalent to
  pf_map_move_cost(pfm, pf_map_iter(pfm)).
****************************************************************************/
int pf_map_iter_move_cost(struct pf_map *pfm)
{
  return pfm->get_move_cost(pfm, pfm->tile);
}

/****************************************************************************
  Return the path to our current position.This is equivalent to
  pf_map_path(pfm, pf_map_iter(pfm)).
****************************************************************************/
struct pf_path *pf_map_iter_path(struct pf_map *pfm)
{
  return pfm->get_path(pfm, pfm->tile);
}

/****************************************************************************
  Read all info about the current position into pos. This is equivalent to
  pf_map_position(pfm, pf_map_iter(pfm), &pos).
****************************************************************************/
void pf_map_iter_position(struct pf_map *pfm, struct pf_position *pos)
{
  if (!pfm->get_position(pfm, pfm->tile, pos)) {
    /* Always fails. */
    fc_assert(pfm->get_position(pfm, pfm->tile, pos));
  }
}

/****************************************************************************
  Return the pf_parameter for given pf_map.
****************************************************************************/
const struct pf_parameter *pf_map_parameter(const struct pf_map *pfm)
{
  return &pfm->params;
}


/* ====================== pf_path public functions ======================= */

/****************************************************************************
  Fill the position for the start tile of a parameter.
****************************************************************************/
static void pf_position_fill_start_tile(struct pf_position *pos,
                                        const struct pf_parameter *param)
{
  pos->tile = param->start_tile;
  pos->turn = 0;
  pos->moves_left = param->moves_left_initially;
  pos->fuel_left = param->fuel_left_initially;
  pos->total_MC = 0;
  pos->total_EC = 0;
  pos->dir_to_next_pos = PF_DIR_NONE;
  pos->dir_to_here = PF_DIR_NONE;
}

/****************************************************************************
  Create a path to the start tile of a parameter.
****************************************************************************/
static struct pf_path *
pf_path_new_to_start_tile(const struct pf_parameter *param)
{
  struct pf_path *path = fc_malloc(sizeof(*path));
  struct pf_position *pos = fc_malloc(sizeof(*pos));

  path->length = 1;
  pf_position_fill_start_tile(pos, param);
  path->positions = pos;
  return path;
}

/****************************************************************************
  After use, a path must be destroyed. Note this function accept NULL as
  argument.
****************************************************************************/
void pf_path_destroy(struct pf_path *path)
{
  if (NULL != path) {
    free(path->positions);
    free(path);
  }
}

/****************************************************************************
  Get the last position of the path.
****************************************************************************/
const struct pf_position *pf_path_last_position(const struct pf_path *path)
{
  return path->positions + (path->length - 1);
}

/****************************************************************************
  Debug a path. This function shouldn't be called directly, see
  pf_path_print() defined in "path_finding.h".
****************************************************************************/
void pf_path_print_real(const struct pf_path *path, enum log_level level,
                        const char *file, const char *function, int line)
{
  struct pf_position *pos;
  int i;

  if (path) {
    do_log(file, function, line, TRUE, level,
           "PF: path (at %p) consists of %d positions:",
           (void *) path, path->length);
  } else {
    do_log(file, function, line, TRUE, level, "PF: path is NULL");
    return;
  }

  for (i = 0, pos = path->positions; i < path->length; i++, pos++) {
    do_log(file, function, line, FALSE, level,
           "PF:   %2d/%2d: (%2d,%2d) dir=%-2s cost=%2d (%2d, %d) EC=%d",
           i + 1, path->length, TILE_XY(pos->tile),
           dir_get_name(pos->dir_to_next_pos), pos->total_MC,
           pos->turn, pos->moves_left, pos->total_EC);
  }
}


/* ===================== pf_reverse_map functions ======================== */

/* The path-finding reverse maps are used check the move costs that the
 * units needs to reach the start tile. It stores a pf_map for every unit
 * type. */

/* The reverse map structure. */
struct pf_reverse_map {
  struct pf_parameter param;    /* Keep a parameter ready for usage. */
  struct pf_map **maps;         /* A vector of pf_map for every unit_type. */
};

/****************************************************************************
  This function estime the cost for unit moves to reach the start tile.

  NB: The costs are calculated in the invert order because we want to know
  how many costs needs the units to REACH the tile, and not to leave it.
****************************************************************************/
static int pf_reverse_map_get_costs(const struct tile *to_tile,
                                    enum direction8 dir,
                                    const struct tile *from_tile,
                                    int to_cost, int to_extra,
                                    int *from_cost, int *from_extra,
                                    const struct pf_parameter *param)
{
  int cost;

  if (!param->omniscience
      && TILE_UNKNOWN == tile_get_known(to_tile, param->owner)) {
    cost = SINGLE_MOVE;
  } else if (!is_native_tile_to_class(param->uclass, to_tile)
             && !tile_city(to_tile)) {
    return -1;  /* Impossible move. */
  } else if (uclass_has_flag(param->uclass, UCF_TERRAIN_SPEED)) {
    if (BV_ISSET(param->unit_flags, F_IGTER)) {
      cost = MIN(map_move_cost(param->owner, from_tile, to_tile), SINGLE_MOVE);
    } else {
      cost = map_move_cost(param->owner, from_tile, to_tile);
    }
  } else {
    cost = SINGLE_MOVE;
  }

  if (to_cost + cost > FC_PTR_TO_INT(param->data)) {
    return -1;  /* We reached the maximum we wanted. */
  } else if (*from_cost == PF_IMPOSSIBLE_MC     /* Uninitialized yet. */
             || to_cost + cost < *from_cost) {
    *from_cost = to_cost + cost;
    /* N.B.: We don't deal with from_extra. */
  }

  /* Let's calculate some priority. */
  return MAX(3 * SINGLE_MOVE - cost, 0);
}

/****************************************************************************
  'pf_reverse_map' constructor. If 'max_turns' is positive, then it won't
  try to iterate the maps beyond this number of turns.
****************************************************************************/
struct pf_reverse_map *pf_reverse_map_new(struct player *pplayer,
                                          struct tile *start_tile,
                                          int max_turns)
{
  struct pf_reverse_map *pfrm = fc_malloc(sizeof(struct pf_reverse_map));
  struct pf_parameter *param = &pfrm->param;

  /* Initialize the parameter. */
  memset(param, 0, sizeof(*param));
  param->get_costs = pf_reverse_map_get_costs;
  param->start_tile = start_tile;
  param->owner = pplayer;
  param->omniscience = !ai_handicap(pplayer, H_MAP);
  param->data = FC_INT_TO_PTR(max_turns);

  /* Initialize the map vector. */
  pfrm->maps = fc_calloc(utype_count(), sizeof(*pfrm->maps));

  return pfrm;
}

/****************************************************************************
  'pf_reverse_map' constructor for city. If 'max_turns' is positive, then
  it won't try to iterate the maps beyond this number of turns.
****************************************************************************/
struct pf_reverse_map *pf_reverse_map_new_for_city(const struct city *pcity,
                                                   int max_turns)
{
  return pf_reverse_map_new(city_owner(pcity), city_tile(pcity), max_turns);
}

/****************************************************************************
  'pf_reverse_map' constructor for unit. If 'max_turns' is positive, then
  it won't try to iterate the maps beyond this number of turns.
****************************************************************************/
struct pf_reverse_map *pf_reverse_map_new_for_unit(const struct unit *punit,
                                                   int max_turns)
{
  return pf_reverse_map_new(unit_owner(punit), unit_tile(punit), max_turns);
}

/****************************************************************************
  'pf_reverse_map' destructor.
****************************************************************************/
void pf_reverse_map_destroy(struct pf_reverse_map *pfrm)
{
  struct pf_map **ppfm;
  size_t i;

  fc_assert_ret(NULL != pfrm);

  for (i = 0, ppfm = pfrm->maps; i < utype_count(); i++, ppfm++) {
    if (NULL != *ppfm) {
      pf_map_destroy(*ppfm);
    }
  }
  free(pfrm->maps);
  free(pfrm);
}

/****************************************************************************
  Returns the map for the unit type. Creates it if needed.
****************************************************************************/
static inline struct pf_map *
pf_reverse_map_utype_map(struct pf_reverse_map *pfrm,
                         const struct unit_type *punittype)
{
  Unit_type_id index = utype_index(punittype);
  struct pf_map *pfm = pfrm->maps[index];

  if (NULL == pfm) {
    struct pf_parameter *param = &pfrm->param;
    int max_turns = FC_PTR_TO_INT(param->data);

    /* Not created yet. */
    param->uclass = utype_class(punittype);
    param->unit_flags = punittype->flags;
    param->move_rate = punittype->move_rate;
    param->moves_left_initially = punittype->move_rate;
    if (utype_fuel(punittype)) {
      param->fuel = utype_fuel(punittype);
      param->fuel_left_initially = utype_fuel(punittype);
    } else {
      param->fuel = 1;
      param->fuel_left_initially = 1;
    }
    pfm = pf_map_new(param);
    pfm->params.data =
        FC_INT_TO_PTR(0 <= max_turns && FC_INFINITY > max_turns
                      ? max_turns * param->move_rate : FC_INFINITY);
    pfrm->maps[index] = pfm;
  }

  return pfm;
}

/****************************************************************************
  Get the move costs that a unit type needs to reach the start tile. Returns
  PF_IMPOSSIBLE_MC if the tile is unreachable.
****************************************************************************/
int pf_reverse_map_utype_move_cost(struct pf_reverse_map *pfrm,
                                   const struct unit_type *punittype,
                                   struct tile *ptile)
{
  struct pf_map *pfm = pf_reverse_map_utype_map(pfrm, punittype);

  return pfm->get_move_cost(pfm, ptile);
}

/****************************************************************************
  Get the move costs that a unit needs to reach the start tile. Returns
  PF_IMPOSSIBLE_MC if the tile is unreachable.
****************************************************************************/
int pf_reverse_map_unit_move_cost(struct pf_reverse_map *pfrm,
                                  const struct unit *punit)
{
  struct pf_map *pfm = pf_reverse_map_utype_map(pfrm, unit_type(punit));

  return pfm->get_move_cost(pfm, unit_tile(punit));
}

/****************************************************************************
  Get the path to the target from 'ptile'. Note that the path will be in
  reverse order.
****************************************************************************/
struct pf_path *pf_reverse_map_utype_path(struct pf_reverse_map *pfrm,
                                          const struct unit_type *punittype,
                                          struct tile *ptile)
{
  struct pf_map *pfm = pf_reverse_map_utype_map(pfrm, punittype);

  return pfm->get_path(pfm, ptile);
}

/****************************************************************************
  Get the path to the target from 'punit'. Note that the path will be in
  reverse order.
****************************************************************************/
struct pf_path *pf_reverse_map_unit_path(struct pf_reverse_map *pfrm,
                                         const struct unit *punit)
{
  struct pf_map *pfm = pf_reverse_map_utype_map(pfrm, unit_type(punit));

  return pfm->get_path(pfm, unit_tile(punit));
}

/****************************************************************************
  Fill the position. Return TRUE if the tile is reachable.
****************************************************************************/
bool pf_reverse_map_utype_position(struct pf_reverse_map *pfrm,
                                   const struct unit_type *punittype,
                                   struct tile *ptile,
                                   struct pf_position *pos)
{
  struct pf_map *pfm = pf_reverse_map_utype_map(pfrm, punittype);

  return pfm->get_position(pfm, ptile, pos);
}

/****************************************************************************
  Fill the position. Return TRUE if the tile is reachable.
****************************************************************************/
bool pf_reverse_map_unit_position(struct pf_reverse_map *pfrm,
                                  const struct unit *punit,
                                  struct pf_position *pos)
{
  struct pf_map *pfm = pf_reverse_map_utype_map(pfrm, unit_type(punit));

  return pfm->get_position(pfm, unit_tile(punit), pos);
}
