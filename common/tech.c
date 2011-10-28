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
#include "game.h"
#include "log.h"
#include "mem.h"                /* free */
#include "shared.h"             /* ARRAY_SIZE */
#include "string_vector.h"
#include "support.h"

/* common */
#include "research.h"


#include "tech.h"

/* Define this for additional debug information about the tech status
 * (in player_research_update()). */
#undef DEBUG_TECH

/* the advances array is now setup in:
 * server/ruleset.c (for the server)
 * client/packhand.c (for the client)
 */
struct advance advances[A_LAST];

/* Precalculated costs according to techcost style 1.  These do not include
 * the sciencebox multiplier. */
static double techcoststyle1[A_LAST];

static int tech_upkeep_calc(const struct player *pplayer);

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
  Returns state of the tech for current pplayer.
  This can be: TECH_KNOWN, TECH_UNKNOWN, or TECH_PREREQS_KNOWN
  Should be called with existing techs or A_FUTURE

  If pplayer is NULL this checks whether any player knows the tech (used
  by the client).
**************************************************************************/
enum tech_state player_invention_state(const struct player *pplayer,
				       Tech_type_id tech)
{
  fc_assert_ret_val(tech == A_FUTURE
                    || (tech >= 0 && tech < game.control.num_tech_types),
                    -1);

  if (!pplayer) {
    if (tech != A_FUTURE && game.info.global_advances[tech]) {
      return TECH_KNOWN;
    } else {
      return TECH_UNKNOWN;
    }
  } else {
    struct player_research *research = player_research_get(pplayer);

    /* Research can be null in client when looking for tech_leakage
     * from player not yet received. */
    if (research) {
      return research->inventions[tech].state;
    } else {
      return TECH_UNKNOWN;
    }
  }
}

/**************************************************************************
  Set player knowledge about tech to given state.
**************************************************************************/
enum tech_state player_invention_set(struct player *pplayer,
				     Tech_type_id tech,
				     enum tech_state value)
{
  struct player_research *research = player_research_get(pplayer);
  enum tech_state old = research->inventions[tech].state;

  if (old == value) {
    return old;
  }
  research->inventions[tech].state = value;

  if (value == TECH_KNOWN) {
    game.info.global_advances[tech] = TRUE;
  }
  return old;
}

/**************************************************************************
  Returns if the given tech has to be researched to reach the
  goal. The goal itself isn't a requirement of itself.

  pplayer may be NULL; however the function will always return FALSE in
  that case.
**************************************************************************/
bool is_tech_a_req_for_goal(const struct player *pplayer, Tech_type_id tech,
			    Tech_type_id goal)
{
  if (tech == goal) {
    return FALSE;
  } else if (!pplayer) {
    /* FIXME: We need a proper implementation here! */
    return FALSE;
  } else {
    return
      BV_ISSET(player_research_get(pplayer)->inventions[goal].required_techs,
               tech);
  }
}

/**************************************************************************
  Accessor for requirements.
**************************************************************************/
Tech_type_id advance_required(const Tech_type_id tech,
			      enum tech_req require)
{
  fc_assert_ret_val(require >= 0 && require < AR_SIZE, -1);
  fc_assert_ret_val(tech >= A_NONE || tech < A_LAST, -1);
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
  /* The is_tech_a_req_for_goal condition is true if the tech is
   * already marked */
  if (!player_invention_reachable(pplayer, tech, FALSE)
      || player_invention_state(pplayer, tech) == TECH_KNOWN
      || is_tech_a_req_for_goal(pplayer, tech, goal)) {
    return;
  }

  /* Mark the tech as required for the goal */
  BV_SET(player_research_get(pplayer)->inventions[goal].required_techs, tech);

  if (advance_required(tech, AR_ONE) == goal
      || advance_required(tech, AR_TWO) == goal) {
    log_fatal("tech \"%s\": requires itself",
              advance_name_by_player(pplayer, goal));
    exit(EXIT_FAILURE);
  }

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
  struct player_research *research = player_research_get(pplayer);

  BV_CLR_ALL(research->inventions[goal].required_techs);
  
  if (player_invention_state(pplayer, goal) == TECH_KNOWN) {
    research->inventions[goal].num_required_techs = 0;
    research->inventions[goal].bulbs_required = 0;
    return;
  }
  
  build_required_techs_helper(pplayer, goal, goal);

  /* Include the goal tech */
  research->inventions[goal].bulbs_required =
      base_total_bulbs_required(pplayer, goal);
  research->inventions[goal].num_required_techs = 1;

  counter = 0;
  advance_index_iterate(A_FIRST, i) {
    if (!is_tech_a_req_for_goal(pplayer, i, goal)) {
      continue;
    }

    /* 
     * This is needed to get a correct result for the
     * base_total_bulbs_required call.
     */
    research->techs_researched++;
    counter++;

    research->inventions[goal].num_required_techs++;
    research->inventions[goal].bulbs_required +=
	base_total_bulbs_required(pplayer, i);
  } advance_index_iterate_end;

  /* Undo the changes made above */
  research->techs_researched -= counter;
}

/**************************************************************************
  Returns TRUE iff the given tech is ever reachable by the given player
  by checking tech tree limitations. If allow_prereqs is TRUE check if the
  player can ever reach this tech.

  pplayer may be NULL in which case a simplified result is returned
  (used by the client).
**************************************************************************/
bool player_invention_reachable(const struct player *pplayer,
                                const Tech_type_id tech,
                                bool allow_prereqs)
{
  Tech_type_id root;

  if (!valid_advance_by_number(tech)) {
    return FALSE;
  }

  root = advance_required(tech, AR_ROOT);
  if (A_NONE != root) {
    if (root == tech) {
      /* This tech requires itself; it can only be reached by special means
       * (lua script). */
      return FALSE;
    } else if (allow_prereqs) {
      /* Recursive check if the player can ever reach this tech (root tech
       * and both requirements). */
      return (player_invention_reachable(pplayer, root, TRUE)
              && player_invention_reachable(pplayer,
                                            advance_required(tech, AR_ONE),
                                            allow_prereqs)
              && player_invention_reachable(pplayer,
                                            advance_required(tech, AR_TWO),
                                            allow_prereqs));
    } else if (TECH_KNOWN != player_invention_state(pplayer, root)
               || !player_invention_reachable(pplayer,
                                              advance_required(tech, AR_ONE),
                                              allow_prereqs)
               || !player_invention_reachable(pplayer,
                                              advance_required(tech, AR_TWO),
                                              allow_prereqs)) {
      /* This tech requires knowledge of another tech (root tech or recursive
       * a root tech of a requirement) before being available. Prevents
       * sharing of untransferable techs. */
      return FALSE;
    }
  }

  return TRUE;
}

/**************************************************************************
  Mark as TECH_PREREQS_KNOWN each tech which is available, not known and
  which has all requirements fullfiled.
  If there is no such a tech mark A_FUTURE as researchable.
  
  Recalculate research->num_known_tech_with_flag
  Should always be called after player_invention_set()
**************************************************************************/
void player_research_update(struct player *pplayer)
{
  enum tech_flag_id flag;
  int researchable = 0;
  struct player_research *research = player_research_get(pplayer);

  /* This is set when the game starts, but not everybody finds out
   * right away. */
  player_invention_set(pplayer, A_NONE, TECH_KNOWN);

  advance_index_iterate(A_FIRST, i) {
    if (!player_invention_reachable(pplayer, i, FALSE)) {
      player_invention_set(pplayer, i, TECH_UNKNOWN);
    } else {
      if (player_invention_state(pplayer, i) == TECH_PREREQS_KNOWN) {
        player_invention_set(pplayer, i, TECH_UNKNOWN);
      }

      if (player_invention_state(pplayer, i) == TECH_UNKNOWN
          && player_invention_state(pplayer, advance_required(i, AR_ONE))
             == TECH_KNOWN
          && player_invention_state(pplayer, advance_required(i, AR_TWO))
             == TECH_KNOWN) {
        player_invention_set(pplayer, i, TECH_PREREQS_KNOWN);
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
              tech_state_name(player_invention_state(pplayer, i)));
    log_debug("%s: [%3d] %s", player_name(pplayer), i, buf);
  } advance_index_iterate_end;
#endif /* DEBUG */

  /* No techs we can research? Mark A_FUTURE as researchable */
  if (researchable == 0) {
    player_invention_set(pplayer, A_FUTURE, TECH_PREREQS_KNOWN);
  }

  for (flag = 0; flag <= tech_flag_id_max(); flag++) {
    /* iterate over all possible tech flags (0..max) */
    research->num_known_tech_with_flag[flag] = 0;

    advance_index_iterate(A_NONE, i) {
      if (player_invention_state(pplayer, i) == TECH_KNOWN
          && advance_has_flag(i, flag)) {
        research->num_known_tech_with_flag[flag]++;
      }
    } advance_index_iterate_end;
  }

  /* calculate tech upkeep cost */
  if (game.info.tech_upkeep_style == 1) {
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
  base_total_bulbs_required().
**************************************************************************/
static int tech_upkeep_calc(const struct player *pplayer)
{
  struct player_research *research = player_research_get(pplayer);
  int tech_cost_style = game.info.tech_cost_style;
  int f = research->future_tech, t = research->techs_researched;
  double tech_bulb_sum = 0.0;

  if (!pplayer) {
    return 0;
  }

  /* upkeep cost for 'normal' techs (t) */
  switch (tech_cost_style) {
  case 0:
    /* sum_1^t x = t * (t + 1) / 2 */
    tech_bulb_sum += (double)t * (t + 1) / 2 * game.info.base_tech_cost;
    break;
  case 1:
  case 3:
    advance_index_iterate(A_NONE, i) {
      if (player_invention_state(pplayer, i) == TECH_KNOWN) {
        tech_bulb_sum += techcoststyle1[i];
      }
    } advance_index_iterate_end;
    break;
  case 2:
  case 4:
    advance_index_iterate(A_NONE, i) {
      if (player_invention_state(pplayer, i) == TECH_KNOWN) {
        if (advances[i].preset_cost != -1) {
          tech_bulb_sum = advances[i].preset_cost;
        } else {
          tech_bulb_sum += techcoststyle1[i];
        }
      }
    } advance_index_iterate_end;
    break;
  default:
    fc_assert_ret_val_msg(FALSE, 0, "Invalid tech_cost_style %d %d",
                          game.info.tech_cost_style,
                          tech_cost_style);
  }

  /* upkeep cost for future techs (f) are calculated using style 0:
   * sum_t^(t+f) x = (f * (2 * t + f + 1) + 2 * t) / 2 */
  tech_bulb_sum += (double)(f * (2 * t + f + 1) + 2 * t) / 2
                           * game.info.base_tech_cost;

  tech_bulb_sum *= get_player_bonus(pplayer, EFT_TECH_COST_FACTOR);
  tech_bulb_sum *= (double)game.info.sciencebox / 100.0;
  tech_bulb_sum /= game.info.tech_upkeep_divider;
  tech_bulb_sum -= get_player_bonus(pplayer, EFT_TECH_UPKEEP_FREE);

  return MAX((int)tech_bulb_sum, 0);
}

/**************************************************************************
  Return the next tech we should research to advance towards our goal.
  Returns A_UNSET if nothing is available or the goal is already known.
**************************************************************************/
Tech_type_id player_research_step(const struct player *pplayer,
				  Tech_type_id goal)
{
  Tech_type_id sub_goal;

  if (!player_invention_reachable(pplayer, goal, FALSE)) {
    return A_UNSET;
  }
  switch (player_invention_state(pplayer, goal)) {
  case TECH_KNOWN:
    return A_UNSET;
  case TECH_PREREQS_KNOWN:
    return goal;
  case TECH_UNKNOWN:
  default:
    break;
  };
  sub_goal = player_research_step(pplayer, advance_required(goal, AR_ONE));
  if (sub_goal != A_UNSET) {
    return sub_goal;
  } else {
    return player_research_step(pplayer, advance_required(goal, AR_TWO));
  }
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

/**************************************************************************
 Search for a tech with a given flag starting at index
 Returns A_LAST if no tech has been found
**************************************************************************/
Tech_type_id advance_by_flag(Tech_type_id index, enum tech_flag_id flag)
{
  advance_index_iterate(index, i)
  {
    if(advance_has_flag(i,flag)) return i;
  } advance_index_iterate_end
  return A_LAST;
}

/**************************************************************************
  Returns the number of bulbs which are required to finished the
  currently researched tech denoted by
  pplayer->research->researching. This is _NOT_ the number of bulbs
  which are left to get the advance. Use the term
  "total_bulbs_required(pplayer) - pplayer->research->bulbs_researched"
  if you want this.
**************************************************************************/
int total_bulbs_required(const struct player *pplayer)
{
  return base_total_bulbs_required(pplayer,
    player_research_get(pplayer)->researching);
}

/****************************************************************************
  Function to determine cost for technology.  The equation is determined
  from game.info.tech_cost_style and game.info.tech_leakage.

  tech_cost_style:
  0 - Civ (I|II) style. Every new tech adds N to the cost of the next tech.
  1 - Cost of technology is:
        (1 + parents) * 10 * sqrt(1 + parents)
      where num_parents == number of requirement for tech (recursive).
  2 - Cost are read from tech.ruleset. Missing costs are generated by
      style 1.
  3 - Cost of technology is:
        cost = base * (reqs - 1)^2 / (1 + sqrt(sqrt(reqs))) - base/2
  4 - Cost are read from tech.ruleset. Missing costs are generated by
      style 3.

  tech_leakage:
  0 - No reduction of the technology cost.
  1 - Technology cost is reduced depending on the number of players
      which already know the tech and you have an embassy with.
  2 - Technology cost is reduced depending on the number of all players
      (human, AI and barbarians) which already know the tech.
  3 - Technology cost is reduced depending on the number of normal
      players (human and AI) which already know the tech.

  At the end we multiply by the sciencebox value, as a percentage.  The
  cost can never be less than 1.

  pplayer may be NULL in which case a simplified result is returned (used
  by client and manual code).
****************************************************************************/
int base_total_bulbs_required(const struct player *pplayer,
			      Tech_type_id tech)
{
  int tech_cost_style = game.info.tech_cost_style;
  double base_cost;

  if (pplayer
      && !is_future_tech(tech)
      && player_invention_state(pplayer, tech) == TECH_KNOWN) {
    /* A non-future tech which is already known costs nothing. */
    return 0;
  }

  if (is_future_tech(tech)) {
    /* Future techs use style 0 */
    tech_cost_style = 0;
  }

  if (tech_cost_style == 2 && advances[tech].preset_cost == -1) {
    /* No preset, using style 1 */
    tech_cost_style = 1;
  }

  if (tech_cost_style == 4 && advances[tech].preset_cost == -1) {
    /* No preset, using style 3 */
    tech_cost_style = 3;
  }

  switch (tech_cost_style) {
  case 0:
    if (pplayer) {
      base_cost = player_research_get(pplayer)->techs_researched 
	* game.info.base_tech_cost;
    } else {
      base_cost = 0;
    }
    break;
  case 1:
  case 3:
    base_cost = techcoststyle1[tech];
    break;
  case 2:
  case 4:
    base_cost = advances[tech].preset_cost;
    break;
  default:
    log_error("Invalid tech_cost_style %d %d", game.info.tech_cost_style,
              tech_cost_style);
    base_cost = 0.0;
  }

  base_cost *= get_player_bonus(pplayer, EFT_TECH_COST_FACTOR);

  switch (game.info.tech_leakage) {
  case 0:
    /* no change */
    break;

  case 1:
    {
      int players = 0, players_with_tech_and_embassy = 0;

      players_iterate_alive(other) {
	players++;
	if (player_invention_state(other, tech) == TECH_KNOWN
	    && pplayer && player_has_embassy(pplayer, other)) {
	  players_with_tech_and_embassy++;
	}
      } players_iterate_alive_end;

      base_cost *= (double)(players - players_with_tech_and_embassy);
      base_cost /= (double)players;
    }
    break;

  case 2:
    {
      int players = 0, players_with_tech = 0;

      players_iterate_alive(other) {
	players++;
	if (player_invention_state(other, tech) == TECH_KNOWN) {
	  players_with_tech++;
	}
      } players_iterate_alive_end;

      base_cost *= (double)(players - players_with_tech);
      base_cost /= (double)players;
    }
    break;

  case 3:
    {
      int players = 0, players_with_tech = 0;

      players_iterate_alive(other) {
	if (is_barbarian(other)) {
	  continue;
	}
	players++;
	if (player_invention_state(other, tech) == TECH_KNOWN) {
	  players_with_tech++;
	}
      } players_iterate_alive_end;

      base_cost *= (double)(players - players_with_tech);
      base_cost /= (double)players;
    }
    break;

  default:
    log_error("Invalid tech_leakage %d", game.info.tech_leakage);
  }

  /* Assign a science penalty to the AI at easier skill levels.  This code
   * can also be adopted to create an extra-hard AI skill level where the AI
   * gets science benefits */

  if (pplayer && pplayer->ai_controlled) {
    fc_assert_ret_val(pplayer->ai_common.science_cost > 0, FC_INFINITY);
    base_cost *= (double)pplayer->ai_common.science_cost / 100.0;
  }

  base_cost *= (double)game.info.sciencebox / 100.0;

  return MAX(base_cost, 1);
}

/**************************************************************************
 Returns the number of technologies the player need to research to get
 the goal technology. This includes the goal technology. Technologies
 are only counted once.

  pplayer may be NULL; however the wrong value will be return in this case.
**************************************************************************/
int num_unknown_techs_for_goal(const struct player *pplayer,
			       Tech_type_id goal)
{
  if (!pplayer) {
    /* FIXME: need an implementation for this! */
    return 0;
  }
  return player_research_get(pplayer)->inventions[goal].num_required_techs;
}

/**************************************************************************
 Function to determine cost (in bulbs) of reaching goal
 technology. These costs _include_ the cost for researching the goal
 technology itself.

  pplayer may be NULL; however the wrong value will be return in this case.
**************************************************************************/
int total_bulbs_required_for_goal(const struct player *pplayer,
				  Tech_type_id goal)
{
  if (!pplayer) {
    /* FIXME: need an implementation for this! */
    return 0;
  }
  return player_research_get(pplayer)->inventions[goal].bulbs_required;
}

/**************************************************************************
 Returns number of requirements for the given tech. To not count techs
 double a memory (the counted array) is needed.
**************************************************************************/
static int precalc_tech_data_helper(Tech_type_id tech, bool *counted)
{
  if (tech == A_NONE || !valid_advance_by_number(tech) || counted[tech]) {
    return 0;
  }

  counted[tech] = TRUE;

  return 1 + 
      precalc_tech_data_helper(advance_required(tech, AR_ONE), counted)+ 
      precalc_tech_data_helper(advance_required(tech, AR_TWO), counted);
}

/**************************************************************************
 Function to precalculate needed data for technologies.
 Styles 3 and 4 use the same table as styles 1 and 2 so we do not have to
 modify any function that reads it.
**************************************************************************/
void precalc_tech_data()
{
  bool counted[A_LAST];

  advance_index_iterate(A_NONE, tech) {
    memset(counted, 0, sizeof(counted));
    advances[tech].num_reqs = precalc_tech_data_helper(tech, counted);
  } advance_index_iterate_end;

  advance_index_iterate(A_NONE, tech) {
    /* FIXME: Why are we counting the current tech twice? */
    double reqs = advances[tech].num_reqs + 1;
    double cost = 0;
    const double base = game.info.base_tech_cost;

    switch (game.info.tech_cost_style) {
    case 0:
      break;
    case 1:
    case 2:
      cost = base * reqs * sqrt(reqs) / 2;
      break;
    case 3:
    case 4:
      cost = base * (reqs - 1) * (reqs - 1) / (1 + sqrt(sqrt(reqs))) - base/2;
      break;
    default:
      log_error("Invalid tech_cost_style %d", game.info.tech_cost_style);
      break;
    }

    techcoststyle1[tech] = MAX(cost, game.info.base_tech_cost);
  } advance_index_iterate_end;
}

/**************************************************************************
 Is the given tech a future tech.
**************************************************************************/
bool is_future_tech(Tech_type_id tech)
{
  return tech == A_FUTURE;
}

#define SPECVEC_TAG string
#define SPECVEC_TYPE char *
#include "specvec.h"

/**************************************************************************
  Return the rule name of the given tech (including A_FUTURE). 
  You don't have to free the return pointer.

  pplayer may be NULL.
**************************************************************************/
const char *advance_name_by_player(const struct player *pplayer, Tech_type_id tech)
{
  /* We don't return a static buffer because that would break anything that
   * needed to work with more than one name at a time.
   * FIXME: The caller should provide a buffer to write that name. */
  static struct string_vector future;

  switch (tech) {
  case A_FUTURE:
    if (pplayer) {
      struct player_research *research = player_research_get(pplayer);
      int i;
  
      /* pplayer->future_tech == 0 means "Future Tech. 1". */
      for (i = future.size; i <= research->future_tech; i++) {
        string_vector_append(&future, NULL);
      }
      if (!future.p[research->future_tech]) {
        char buffer[1024];

        fc_snprintf(buffer, sizeof(buffer), "%s %d",
                    advance_rule_name(&advances[tech]),
                    research->future_tech + 1);
        future.p[research->future_tech] = fc_strdup(buffer);
      }
      return future.p[research->future_tech];
    } else {
      return advance_rule_name(&advances[tech]);
    }
  case A_UNKNOWN:
  case A_UNSET:
    return advance_rule_name(&advances[tech]);
  default:
    /* Includes A_NONE */
    return advance_rule_name(advance_by_number(tech));
  };
}

/**************************************************************************
  Return the translated name of the given tech (including A_FUTURE). 
  You don't have to free the return pointer.

  pplayer may be NULL.
**************************************************************************/
const char *advance_name_for_player(const struct player *pplayer, Tech_type_id tech)
{
  /* We don't return a static buffer because that would break anything that
   * needed to work with more than one name at a time.
   * FIXME: The caller should provide a buffer to write that name. */
  static struct string_vector future;

  switch (tech) {
  case A_FUTURE:
    if (pplayer) {
      struct player_research *research = player_research_get(pplayer);
      int i;
  
      /* pplayer->future_tech == 0 means "Future Tech. 1". */
      for (i = future.size; i <= research->future_tech; i++) {
        string_vector_append(&future, NULL);
      }
      if (!future.p[research->future_tech]) {
        char buffer[1024];

        fc_snprintf(buffer, sizeof(buffer), _("Future Tech. %d"),
                    research->future_tech + 1);
        future.p[research->future_tech] = fc_strdup(buffer);
      }
      return future.p[research->future_tech];
    } else {
      return advance_name_translation(&advances[tech]);
    }
  case A_UNKNOWN:
  case A_UNSET:
    return advance_name_translation(&advances[tech]);
  default:
    /* Includes A_NONE */
    return advance_name_translation(advance_by_number(tech));
  };
}

/**************************************************************************
  Return the translated name of the given research (including A_FUTURE). 
  You don't have to free the return pointer.

  pplayer must not be NULL.
**************************************************************************/
const char *advance_name_researching(const struct player *pplayer)
{
  return advance_name_for_player(pplayer,
    player_research_get(pplayer)->researching);
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
  int i;

  for (i = 0; i < ARRAY_SIZE(advances); i++) {
    advances[i].item_number = i;
  }

  /* Initialize dummy tech A_NONE */
  /* TRANS: "None" tech */
  name_set(&advances[A_NONE].name, N_("None"));

  /* Initialize dummy tech A_UNSET */
  name_set(&advances[A_UNSET].name, N_("None"));

  /* Initialize dummy tech A_FUTURE */
  name_set(&advances[A_FUTURE].name, N_("Future Tech."));

  /* Initialize dummy tech A_UNKNOWN */
  /* TRANS: "Unknown" advance/technology */
  name_set(&advances[A_UNKNOWN].name, N_("(Unknown)"));
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
