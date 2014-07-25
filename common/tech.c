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
#include <fc_config.h>
#endif

#include <stdlib.h>             /* exit */
#include <string.h>
#include <math.h>

/* utility */
#include "fcintl.h"
#include "iterator.h"
#include "log.h"
#include "mem.h"                /* free */
#include "shared.h"             /* ARRAY_SIZE */
#include "string_vector.h"
#include "support.h"

/* common */
#include "game.h"
#include "research.h"

#include "tech.h"


/* Define this for additional debug information about the tech status
 * (in player_research_update()). */
#undef DEBUG_TECH

struct advance_req_iter {
  struct iterator base;
  bv_techs done;
  const struct advance *array[A_LAST];
  const struct advance **current, **end;
};
#define ADVANCE_REQ_ITER(it) ((struct advance_req_iter *) it)

/* the advances array is now setup in:
 * server/ruleset.c (for the server)
 * client/packhand.c (for the client)
 */
struct advance advances[A_LAST];

static int tech_upkeep_calc(const struct player *pplayer);

static struct user_flag user_tech_flags[MAX_NUM_USER_TECH_FLAGS];

/**************************************************************************
  Return the last item of advances/technologies.
**************************************************************************/
const struct advance *advance_array_last(void)
{
  if (game.control.num_tech_types > 0) {
    return &advances[game.control.num_tech_types - 1];
  }
  return NULL;
}

/**************************************************************************
  Return the number of advances/technologies.
**************************************************************************/
Tech_type_id advance_count(void)
{
  return game.control.num_tech_types;
}

/**************************************************************************
  Return the advance index.

  Currently same as advance_number(), paired with advance_count()
  indicates use as an array index.
**************************************************************************/
Tech_type_id advance_index(const struct advance *padvance)
{
  fc_assert_ret_val(NULL != padvance, -1);
  return padvance - advances;
}

/**************************************************************************
  Return the advance index.
**************************************************************************/
Tech_type_id advance_number(const struct advance *padvance)
{
  fc_assert_ret_val(NULL != padvance, -1);
  return padvance->item_number;
}

/**************************************************************************
  Return the advance for the given advance index.
**************************************************************************/
struct advance *advance_by_number(const Tech_type_id atype)
{
  if (atype < 0 || atype >= game.control.num_tech_types) {
    /* This isn't an error; some callers depend on it. */
    return NULL;
  }
  return &advances[atype];
}

/**************************************************************************
  Accessor for requirements.
**************************************************************************/
Tech_type_id advance_required(const Tech_type_id tech,
			      enum tech_req require)
{
  fc_assert_ret_val(require >= 0 && require < AR_SIZE, -1);
  fc_assert_ret_val(tech >= A_NONE && tech < A_LAST, -1);
  if (A_NEVER == advances[tech].require[require]) {
    /* out of range */
    return A_LAST;
  }
  return advance_number(advances[tech].require[require]);
}

/**************************************************************************
  Accessor for requirements.
**************************************************************************/
struct advance *advance_requires(const struct advance *padvance,
				 enum tech_req require)
{
  fc_assert_ret_val(require >= 0 && require < AR_SIZE, NULL);
  fc_assert_ret_val(NULL != padvance, NULL);
  return padvance->require[require];
}

/**************************************************************************
  Marks all techs which are requirements for goal in
  pplayer->research->inventions[goal].required_techs. Works recursive.
**************************************************************************/
static void build_required_techs_helper(struct player *pplayer,
					Tech_type_id tech,
					Tech_type_id goal)
{
  struct research *presearch = research_get(pplayer);

  /* The research_goal_tech_req condition is true if the tech is
   * already marked */
  if (!research_invention_reachable(presearch, tech)
      || research_invention_state(presearch, tech) == TECH_KNOWN
      || research_goal_tech_req(presearch, goal, tech)) {
    return;
  }

  /* Mark the tech as required for the goal */
  BV_SET(presearch->inventions[goal].required_techs, tech);

  build_required_techs_helper(pplayer, advance_required(tech, AR_ONE), goal);
  build_required_techs_helper(pplayer, advance_required(tech, AR_TWO), goal);
}

/**************************************************************************
  Updates required_techs, num_required_techs and bulbs_required in
  pplayer->research->inventions[goal].
**************************************************************************/
static void build_required_techs(struct player *pplayer, Tech_type_id goal)
{
  int counter;
  struct research *research = research_get(pplayer);

  BV_CLR_ALL(research->inventions[goal].required_techs);
  
  if (research_invention_state(research, goal) == TECH_KNOWN) {
    research->inventions[goal].num_required_techs = 0;
    research->inventions[goal].bulbs_required = 0;
    return;
  }
  
  build_required_techs_helper(pplayer, goal, goal);

  /* Include the goal tech */
  research->inventions[goal].bulbs_required =
      research_total_bulbs_required(research, goal, FALSE);
  research->inventions[goal].num_required_techs = 1;

  counter = 0;
  advance_index_iterate(A_FIRST, i) {
    if (!research_goal_tech_req(research, goal, i)) {
      continue;
    }

    /* 
     * This is needed to get a correct result for the
     * research_total_bulbs_required call.
     */
    research->techs_researched++;
    counter++;

    research->inventions[goal].num_required_techs++;
    research->inventions[goal].bulbs_required +=
        research_total_bulbs_required(research, i, FALSE);
  } advance_index_iterate_end;

  /* Undo the changes made above */
  research->techs_researched -= counter;
}

/**************************************************************************
  Mark as TECH_PREREQS_KNOWN each tech which is available, not known and
  which has all requirements fullfiled.
  
  Recalculate research->num_known_tech_with_flag
  Should always be called after research_invention_set()
**************************************************************************/
void player_research_update(struct player *pplayer)
{
  enum tech_flag_id flag;
  int researchable = 0;
  struct research *research = research_get(pplayer);

  /* This is set when the game starts, but not everybody finds out
   * right away. */
  research_invention_set(research, A_NONE, TECH_KNOWN);

  advance_index_iterate(A_FIRST, i) {
    if (!research_invention_reachable(research, i)) {
      research_invention_set(research, i, TECH_UNKNOWN);
    } else {
      if (research_invention_state(research, i) == TECH_PREREQS_KNOWN) {
        research_invention_set(research, i, TECH_UNKNOWN);
      }

      if (research_invention_state(research, i) == TECH_UNKNOWN
          && research_invention_state(research, advance_required(i, AR_ONE))
             == TECH_KNOWN
          && research_invention_state(research, advance_required(i, AR_TWO))
             == TECH_KNOWN) {
        research_invention_set(research, i, TECH_PREREQS_KNOWN);
        researchable++;
      }
    }
    build_required_techs(pplayer, i);
  } advance_index_iterate_end;

#ifdef DEBUG_TECH
  advance_index_iterate(A_FIRST, i) {
    char buf[advance_count() + 1];

    advance_index_iterate(A_NONE, j) {
      if (BV_ISSET(research->inventions[i].required_techs, j)) {
        buf[j] = '1';
      } else {
        buf[j] = '0';
      }
    } advance_index_iterate_end;
    buf[advance_count()] = '\0';

    log_debug("%s: [%3d] %-25s => %s", player_name(pplayer), i,
              advance_rule_name(advance_by_number(i)),
              tech_state_name(research_invention_state(research, i)));
    log_debug("%s: [%3d] %s", player_name(pplayer), i, buf);
  } advance_index_iterate_end;
#endif /* DEBUG */

  for (flag = 0; flag <= tech_flag_id_max(); flag++) {
    /* iterate over all possible tech flags (0..max) */
    research->num_known_tech_with_flag[flag] = 0;

    advance_index_iterate(A_NONE, i) {
      if (research_invention_state(research, i) == TECH_KNOWN
          && advance_has_flag(i, flag)) {
        research->num_known_tech_with_flag[flag]++;
      }
    } advance_index_iterate_end;
  }

  /* calculate tech upkeep cost */
  if (game.info.tech_upkeep_style != TECH_UPKEEP_NONE) {
    /* upkeep activated in the ruleset */
    research->tech_upkeep = tech_upkeep_calc(pplayer);

    log_debug("[%s (%d)] tech upkeep: %d", player_name(pplayer),
              player_index(pplayer), research->tech_upkeep);
  } else {
    /* upkeep deactivated in the ruleset */
    research->tech_upkeep = 0;
  }
}

/**************************************************************************
  Calculate the bulb upkeep needed for all techs of a player. See also
  research_total_bulbs_required().
**************************************************************************/
static int tech_upkeep_calc(const struct player *pplayer)
{
  struct research *research = research_get(pplayer);
  int f = research->future_tech, t = research->techs_researched;
  double tech_bulb_sum = 0.0;

  /* upkeep cost for 'normal' techs (t) */
  switch (game.info.tech_cost_style) {
  case 0:
    /* sum_1^t x = t * (t + 1) / 2 */
    tech_bulb_sum += game.info.base_tech_cost * t * (t + 1) / 2;
    break;
  case 1:
  case 2:
  case 3:
  case 4:
    advance_index_iterate(A_NONE, i) {
      if (research_invention_state(research, i) == TECH_KNOWN) {
        tech_bulb_sum += advances[i].cost;
      }
    } advance_index_iterate_end;
    break;
  default:
    fc_assert_msg(FALSE, "Invalid tech_cost_style %d",
                  game.info.tech_cost_style);
    tech_bulb_sum = 0.0;
  }

  /* upkeep cost for future techs (f) are calculated using style 0:
   * sum_t^(t+f) x = (f * (2 * t + f + 1) + 2 * t) / 2 */
  tech_bulb_sum += (double)(f * (2 * t + f + 1) + 2 * t) / 2
                           * game.info.base_tech_cost;

  tech_bulb_sum *= get_player_bonus(pplayer, EFT_TECH_COST_FACTOR);
  tech_bulb_sum *= (double)game.info.sciencebox / 100.0;
  tech_bulb_sum /= game.info.tech_upkeep_divider;

  switch (game.info.tech_upkeep_style) {
  case TECH_UPKEEP_BASIC:
    tech_bulb_sum -= get_player_bonus(pplayer, EFT_TECH_UPKEEP_FREE);
    break;
  case TECH_UPKEEP_PER_CITY:
    tech_bulb_sum -= get_player_bonus(pplayer, EFT_TECH_UPKEEP_FREE);
    tech_bulb_sum *= (1 + city_list_size(pplayer->cities));
    break;
  case TECH_UPKEEP_NONE:
    fc_assert(game.info.tech_upkeep_style != TECH_UPKEEP_NONE);
    tech_bulb_sum = 0;
  }

  return MAX((int)tech_bulb_sum, 0);
}

/**************************************************************************
  Returns pointer when the advance "exists" in this game, returns NULL
  otherwise.

  A tech doesn't exist if it has been flagged as removed by setting its
  require values to A_NEVER. Note that this function returns NULL if either
  of req values is A_NEVER, rather than both, to be on the safe side.
**************************************************************************/
struct advance *valid_advance(struct advance *padvance)
{
  if (NULL == padvance
   || A_NEVER == padvance->require[AR_ONE]
   || A_NEVER == padvance->require[AR_TWO]) {
    return NULL;
  }

  return padvance;
}

/**************************************************************************
  Returns pointer when the advance "exists" in this game,
  returns NULL otherwise.

  In addition to valid_advance(), tests for id is out of range.
**************************************************************************/
struct advance *valid_advance_by_number(const Tech_type_id id)
{
  return valid_advance(advance_by_number(id));
}

/**************************************************************************
 Does a linear search of advances[].name.translated
 Returns NULL when none match.
**************************************************************************/
struct advance *advance_by_translated_name(const char *name)
{
  advance_iterate(A_NONE, padvance) {
    if (0 == strcmp(advance_name_translation(padvance), name)) {
      return padvance;
    }
  } advance_iterate_end;

  return NULL;
}

/**************************************************************************
 Does a linear search of advances[].name.vernacular
 Returns NULL when none match.
**************************************************************************/
struct advance *advance_by_rule_name(const char *name)
{
  const char *qname = Qn_(name);

  advance_iterate(A_NONE, padvance) {
    if (0 == fc_strcasecmp(advance_rule_name(padvance), qname)) {
      return padvance;
    }
  } advance_iterate_end;

  return NULL;
}

/**************************************************************************
 Return TRUE if the tech has this flag otherwise FALSE
**************************************************************************/
bool advance_has_flag(Tech_type_id tech, enum tech_flag_id flag)
{
  fc_assert_ret_val(tech_flag_id_is_valid(flag), FALSE);
  return BV_ISSET(advance_by_number(tech)->flags, flag);
}

/****************************************************************************
  Function to precalculate needed data for technologies.
****************************************************************************/
void techs_precalc_data(void)
{
  advance_iterate(A_FIRST, padvance) {
    int num_reqs = 0;

    advance_req_iterate(padvance, preq) {
      (void) preq; /* Compiler wants us to do something with 'preq'. */
      num_reqs++;
    } advance_req_iterate_end;
    padvance->num_reqs = num_reqs;

    switch (game.info.tech_cost_style) {
    case 0:
      padvance->cost = game.info.base_tech_cost * num_reqs;
      break;
    case 2:
      if (-1 != padvance->cost) {
        continue;
      }
    case 1:
      padvance->cost = game.info.base_tech_cost * (1.0 + num_reqs)
                       * sqrt(1.0 + num_reqs) / 2;
      break;
    case 4:
      if (-1 != padvance->cost) {
        continue;
      }
    case 3:
      padvance->cost = game.info.base_tech_cost * ((num_reqs) * (num_reqs)
                           / (1 + sqrt(sqrt(num_reqs + 1))) - 0.5);
      break;
    default:
      log_error("Invalid tech_cost_style %d", game.info.tech_cost_style);
      break;
    }

    if (padvance->cost < game.info.base_tech_cost) {
      padvance->cost = game.info.base_tech_cost;
    }
  } advance_iterate_end;
}

/**************************************************************************
 Is the given tech a future tech.
**************************************************************************/
bool is_future_tech(Tech_type_id tech)
{
  return tech == A_FUTURE;
}

/**************************************************************************
  Return the (translated) name of the given advance/technology.
  You don't have to free the return pointer.
**************************************************************************/
const char *advance_name_translation(const struct advance *padvance)
{
  return name_translation(&padvance->name);
}

/****************************************************************************
  Return the (untranslated) rule name of the advance/technology.
  You don't have to free the return pointer.
****************************************************************************/
const char *advance_rule_name(const struct advance *padvance)
{
  return rule_name(&padvance->name);
}

/**************************************************************************
  Initialize user tech flags.
**************************************************************************/
void user_tech_flags_init(void)
{
  int i;

  for (i = 0; i < MAX_NUM_USER_TECH_FLAGS; i++) {
    user_flag_init(&user_tech_flags[i]);
  }
}

/***************************************************************
  Frees the memory associated with all user tech flags
***************************************************************/
void user_tech_flags_free(void)
{
  int i;

  for (i = 0; i < MAX_NUM_USER_TECH_FLAGS; i++) {
    user_flag_free(&user_tech_flags[i]);
  }
}

/**************************************************************************
  Sets user defined name for tech flag.
**************************************************************************/
void set_user_tech_flag_name(enum tech_flag_id id, const char *name,
                             const char *helptxt)
{
  int tfid = id - TECH_USER_1;

  fc_assert_ret(id >= TECH_USER_1 && id <= TECH_USER_LAST);

  if (user_tech_flags[tfid].name != NULL) {
    FC_FREE(user_tech_flags[tfid].name);
    user_tech_flags[tfid].name = NULL;
  }

  if (name && name[0] != '\0') {
    user_tech_flags[tfid].name = fc_strdup(name);
  }

  if (user_tech_flags[tfid].helptxt != NULL) {
    FC_FREE(user_tech_flags[tfid].helptxt);
    user_tech_flags[tfid].helptxt = NULL;
  }

  if (helptxt && helptxt[0] != '\0') {
    user_tech_flags[tfid].helptxt = fc_strdup(helptxt);
  }
}

/**************************************************************************
  Tech flag name callback, called from specenum code.
**************************************************************************/
char *tech_flag_id_name_cb(enum tech_flag_id flag)
{
  if (flag < TECH_USER_1 || flag > TECH_USER_LAST) {
    return NULL;
  }

  return user_tech_flags[flag-TECH_USER_1].name;
}

/**************************************************************************
  Return the (untranslated) helptxt of the user tech flag.
**************************************************************************/
const char *tech_flag_helptxt(enum tech_flag_id id)
{
  fc_assert(id >= TECH_USER_1 && id <= TECH_USER_LAST);

  return user_tech_flags[id - TECH_USER_1].helptxt;
}

/**************************************************************************
 Returns true if the costs for the given technology will stay constant
 during the game. False otherwise.

 Checking every tech_cost_style with fixed costs seems a waste of system
 resources, when we can check that it is not the one style without fixed
 costs.
**************************************************************************/
bool techs_have_fixed_costs()
{
  return (game.info.tech_leakage == 0 && game.info.tech_cost_style != 0);
}

/****************************************************************************
  Initialize tech structures.
****************************************************************************/
void techs_init(void)
{
  struct advance *a_none = &advances[A_NONE];
  int i;

  memset(advances, 0, sizeof(advances));
  for (i = 0; i < ARRAY_SIZE(advances); i++) {
    advances[i].item_number = i;
    advances[i].cost = -1;
  }

  /* Initialize dummy tech A_NONE */
  /* TRANS: "None" tech */
  name_set(&a_none->name, NULL, N_("None"));
  a_none->require[AR_ONE] = a_none;
  a_none->require[AR_TWO] = a_none;
  a_none->require[AR_ROOT] = A_NEVER;
}

/***************************************************************
 De-allocate resources associated with the given tech.
***************************************************************/
static void tech_free(Tech_type_id tech)
{
  struct advance *p = &advances[tech];

  if (NULL != p->helptext) {
    strvec_destroy(p->helptext);
    p->helptext = NULL;
  }

  if (p->bonus_message) {
    free(p->bonus_message);
    p->bonus_message = NULL;
  }
}

/***************************************************************
 De-allocate resources of all techs.
***************************************************************/
void techs_free(void)
{
  advance_index_iterate(A_FIRST, i) {
    tech_free(i);
  } advance_index_iterate_end;
}


/****************************************************************************
  Return the size of the advance requirements iterator.
****************************************************************************/
size_t advance_req_iter_sizeof(void)
{
  return sizeof(struct advance_req_iter);
}

/****************************************************************************
  Return the current advance.
****************************************************************************/
static void *advance_req_iter_get(const struct iterator *it)
{
  return (void *) *ADVANCE_REQ_ITER(it)->current;
}

/****************************************************************************
  Jump to next advance requirement.
****************************************************************************/
static void advance_req_iter_next(struct iterator *it)
{
  struct advance_req_iter *iter = ADVANCE_REQ_ITER(it);
  const struct advance *padvance = *iter->current, *preq;
  enum tech_req req;
  bool new = FALSE;

  for (req = AR_ONE; req < AR_SIZE; req++) {
    preq = valid_advance(advance_requires(padvance, req));
    if (NULL != preq
        && A_NONE != advance_number(preq)
        && !BV_ISSET(iter->done, advance_number(preq))) {
      BV_SET(iter->done, advance_number(preq));
      if (new) {
        *iter->end++ = preq;
      } else {
        *iter->current = preq;
        new = TRUE;
      }
    }
  }

  if (!new) {
    iter->current++;
  }
}

/****************************************************************************
  Return whether we finished to iterate or not.
****************************************************************************/
static bool advance_req_iter_valid(const struct iterator *it)
{
  const struct advance_req_iter *iter = ADVANCE_REQ_ITER(it);

  return iter->current < iter->end;
}

/****************************************************************************
  Initialize an advance requirements iterator.
****************************************************************************/
struct iterator *advance_req_iter_init(struct advance_req_iter *it,
                                       const struct advance *goal)
{
  struct iterator *base = ITERATOR(it);

  base->get = advance_req_iter_get;
  base->next = advance_req_iter_next;
  base->valid = advance_req_iter_valid;

  BV_CLR_ALL(it->done);
  it->current = it->array;
  *it->current = goal;
  it->end = it->current + 1;

  return base;
}
