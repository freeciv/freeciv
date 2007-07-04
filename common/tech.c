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
#include <stdlib.h>		/* exit */
#include <string.h>
#include <math.h>

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "mem.h"		/* free */
#include "player.h"
#include "shared.h"		/* ARRAY_SIZE */
#include "support.h"

#include "tech.h"

struct advance advances[A_LAST];
/* the advances array is now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client) */

/* Precalculated costs according to techcost style 1.  These do not include
 * the sciencebox multiplier. */
static double techcoststyle1[A_LAST];

static const char *flag_names[] = {
  "Bonus_Tech", "Bridge", "Railroad",
  "Population_Pollution_Inc", 
  "Farmland",
  "Build_Airborne"
};
/* Note that these strings must correspond with the enums in tech_flag_id,
   in common/tech.h */

/**************************************************************************
  Returns state of the tech for current pplayer.
  This can be: TECH_KNOW, TECH_UNKNOWN or TECH_REACHABLE
  Should be called with existing techs or A_FUTURE

  If pplayer is NULL this checks whether any player knows the tech (used
  by the client).
**************************************************************************/
enum tech_state get_invention(const struct player *pplayer,
			      Tech_type_id tech)
{
  assert(tech == A_FUTURE
         || (tech >= 0 && tech < game.control.num_tech_types));

  if (!pplayer) {
    if (tech != A_FUTURE && game.info.global_advances[tech]) {
      return TECH_KNOWN;
    } else {
      return TECH_UNKNOWN;
    }
  } else {
    return get_player_research(pplayer)->inventions[tech].state;
  }
}

/**************************************************************************
...
**************************************************************************/
void set_invention(struct player *pplayer, Tech_type_id tech,
		   enum tech_state value)
{
  struct player_research *research = get_player_research(pplayer);

  if (research->inventions[tech].state == value) {
    return;
  }

  research->inventions[tech].state = value;

  if (value == TECH_KNOWN) {
    game.info.global_advances[tech] = TRUE;
  }
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
      BV_ISSET(get_player_research(pplayer)->inventions[goal].required_techs,
               tech);
  }
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
  if (!tech_is_available(pplayer, tech)
      || get_invention(pplayer, tech) == TECH_KNOWN
      || is_tech_a_req_for_goal(pplayer, tech, goal)) {
    return;
  }

  /* Mark the tech as required for the goal */
  BV_SET(get_player_research(pplayer)->inventions[goal].required_techs, tech);

  if (advances[tech].req[0] == goal || advances[tech].req[1] == goal) {
    freelog(LOG_FATAL, "tech \"%s\": requires itself",
	    advance_name_by_player(pplayer, goal));
    exit(EXIT_FAILURE);
  }

  build_required_techs_helper(pplayer, advances[tech].req[0], goal);
  build_required_techs_helper(pplayer, advances[tech].req[1], goal);
}

/**************************************************************************
  Updates required_techs, num_required_techs and bulbs_required in
  pplayer->research->inventions[goal].
**************************************************************************/
static void build_required_techs(struct player *pplayer, Tech_type_id goal)
{
  int counter;
  struct player_research *research = get_player_research(pplayer);

  BV_CLR_ALL(research->inventions[goal].required_techs);
  
  if (get_invention(pplayer, goal) == TECH_KNOWN) {
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
  tech_type_iterate(i) {
    if (i == A_NONE || !is_tech_a_req_for_goal(pplayer, i, goal)) {
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
  } tech_type_iterate_end;

  /* Undo the changes made above */
  research->techs_researched -= counter;
}

/**************************************************************************
  Returns TRUE iff the given tech is ever reachable by the given player
  by checking tech tree limitations.

  pplayer may be NULL in which case a simplified result is returned
  (used by the client).
**************************************************************************/
bool tech_is_available(const struct player *pplayer, Tech_type_id id)
{
  if (!tech_exists(id)) {
    return FALSE;
  }

  if (advances[id].root_req != A_NONE
      && get_invention(pplayer, advances[id].root_req) != TECH_KNOWN) {
    /* This tech requires knowledge of another tech before being 
     * available. Prevents sharing of untransferable techs. */
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
  Mark as TECH_REACHABLE each tech which is available, not known and
  which has all requirements fullfiled.
  If there is no such a tech mark A_FUTURE as researchable.
  
  Recalculate research->num_known_tech_with_flag
  Should be called always after set_invention()
**************************************************************************/
void update_research(struct player *pplayer)
{
  enum tech_flag_id flag;
  int researchable = 0;

  tech_type_iterate(i) {
    if (i == A_NONE) {
      /* This is set when the game starts, but not everybody finds out
       * right away. */
      set_invention(pplayer, i, TECH_KNOWN);
    } else if (!tech_is_available(pplayer, i)) {
      set_invention(pplayer, i, TECH_UNKNOWN);
    } else {
      if (get_invention(pplayer, i) == TECH_REACHABLE) {
	set_invention(pplayer, i, TECH_UNKNOWN);
      }

      if (get_invention(pplayer, i) == TECH_UNKNOWN
	  && get_invention(pplayer, advances[i].req[0]) == TECH_KNOWN
	  && get_invention(pplayer, advances[i].req[1]) == TECH_KNOWN) {
	set_invention(pplayer, i, TECH_REACHABLE);
	researchable++;
      }
    }
    build_required_techs(pplayer, i);
  } tech_type_iterate_end;
  
  /* No techs we can research? Mark A_FUTURE as researchable */
  if (researchable == 0) {
    set_invention(pplayer, A_FUTURE, TECH_REACHABLE);
  }

  for (flag = 0; flag < TF_LAST; flag++) {
    get_player_research(pplayer)->num_known_tech_with_flag[flag] = 0;

    tech_type_iterate(i) {
      if (get_invention(pplayer, i) == TECH_KNOWN && advance_has_flag(i, flag)) {
	get_player_research(pplayer)->num_known_tech_with_flag[flag]++;
      }
    } tech_type_iterate_end;
  }
}

/**************************************************************************
  Return the next tech we should research to advance towards our goal.
  Returns A_UNSET if nothing is available or the goal is already known.
**************************************************************************/
Tech_type_id get_next_tech(const struct player *pplayer, Tech_type_id goal)
{
  Tech_type_id sub_goal;

  if (!tech_is_available(pplayer, goal)
      || get_invention(pplayer, goal) == TECH_KNOWN) {
    return A_UNSET;
  }
  if (get_invention(pplayer, goal) == TECH_REACHABLE) {
    return goal;
  }
  sub_goal = get_next_tech(pplayer, advances[goal].req[0]);
  if (sub_goal != A_UNSET) {
    return sub_goal;
  } else {
    return get_next_tech(pplayer, advances[goal].req[1]);
  }
}

/**************************************************************************
Returns 1 if the tech "exists" in this game, 0 otherwise.
A tech doesn't exist if one of:
- id is out of range
- the tech has been flagged as removed by setting its req values
  to A_LAST (this function returns 0 if either req is A_LAST, rather
  than both, to be on the safe side)
**************************************************************************/
bool tech_exists(Tech_type_id id)
{
  if (id < 0 || id >= game.control.num_tech_types) {
    return FALSE;
  } else {
    return advances[id].req[0] != A_LAST && advances[id].req[1] != A_LAST;
  }
}

/**************************************************************************
 Does a linear search of advances[].name.translated
 Returns A_LAST if none match.
**************************************************************************/
Tech_type_id find_advance_by_translated_name(const char *s)
{
  tech_type_iterate(i) {
    if (0 == strcmp(advance_name_translation(i), s)) {
      return i;
    }
  } tech_type_iterate_end;
  return A_LAST;
}

/**************************************************************************
 Does a linear search of advances[].name.vernacular
 Returns A_LAST if none match.
**************************************************************************/
Tech_type_id find_advance_by_rule_name(const char *s)
{
  const char *qs = Qn_(s);

  tech_type_iterate(i) {
    if (0 == mystrcasecmp(advance_rule_name(i), qs)) {
      return i;
    }
  } tech_type_iterate_end;
  return A_LAST;
}

/**************************************************************************
 Return TRUE if the tech has this flag otherwise FALSE
**************************************************************************/
bool advance_has_flag(Tech_type_id tech, enum tech_flag_id flag)
{
  assert(flag >= 0 && flag < TF_LAST);
  return TEST_BIT(advances[tech].flags, flag);
}

/**************************************************************************
 Convert flag names to enum; case insensitive;
 returns TF_LAST if can't match.
**************************************************************************/
enum tech_flag_id find_advance_flag_by_rule_name(const char *s)
{
  enum tech_flag_id i;

  assert(ARRAY_SIZE(flag_names) == TF_LAST);
  
  for(i=0; i<TF_LAST; i++) {
    if (mystrcasecmp(flag_names[i], s)==0) {
      return i;
    }
  }
  return TF_LAST;
}

/**************************************************************************
 Search for a tech with a given flag starting at index
 Returns A_LAST if no tech has been found
**************************************************************************/
Tech_type_id find_advance_by_flag(int index, enum tech_flag_id flag)
{
  Tech_type_id i;
  for(i = index;i < game.control.num_tech_types; i++)
  {
    if(advance_has_flag(i,flag)) return i;
  }
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
    get_player_research(pplayer)->researching);
}

/****************************************************************************
  Function to determine cost for technology.  The equation is determined
  from game.info.tech_cost_style and game.info.tech_leakage.

  tech_cost_style:
  0 - Civ (I|II) style. Every new tech adds N to the cost of the next tech.
  1 - Cost of technology is 
        (1 + parents) * 10 * sqrt(1 + parents)
      where num_parents == number of requirement for tech (recursive).
  2 - Cost are read from tech.ruleset. Missing costs are generated by
      style 1.

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
      && get_invention(pplayer, tech) == TECH_KNOWN) {
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

  switch (tech_cost_style) {
  case 0:
    if (pplayer) {
      base_cost = get_player_research(pplayer)->techs_researched 
	* game.info.base_tech_cost;
    } else {
      base_cost = 0;
    }
    break;
  case 1:
    base_cost = techcoststyle1[tech];
    break;
  case 2:
    base_cost = advances[tech].preset_cost;
    break;
  default:
    die("Invalid tech_cost_style %d %d", game.info.tech_cost_style,
	tech_cost_style);
    base_cost = 0.0;
  }

  /* Research becomes more expensive this year and after. */
  if (game.info.tech_cost_double_year != 0
      && game.info.year >= game.info.tech_cost_double_year) {
    base_cost *= 2.0;
  }

  switch (game.info.tech_leakage) {
  case 0:
    /* no change */
    break;

  case 1:
    {
      int players = 0, players_with_tech_and_embassy = 0;

      players_iterate(other) {
	players++;
	if (get_invention(other, tech) == TECH_KNOWN
	    && pplayer && player_has_embassy(pplayer, other)) {
	  players_with_tech_and_embassy++;
	}
      } players_iterate_end;

      base_cost *= (double)(players - players_with_tech_and_embassy);
      base_cost /= (double)players;
    }
    break;

  case 2:
    {
      int players = 0, players_with_tech = 0;

      players_iterate(other) {
	players++;
	if (get_invention(other, tech) == TECH_KNOWN) {
	  players_with_tech++;
	}
      } players_iterate_end;

      base_cost *= (double)(players - players_with_tech);
      base_cost /= (double)players;
    }
    break;

  case 3:
    {
      int players = 0, players_with_tech = 0;

      players_iterate(other) {
	if (is_barbarian(other)) {
	  continue;
	}
	players++;
	if (get_invention(other, tech) == TECH_KNOWN) {
	  players_with_tech++;
	}
      } players_iterate_end;

      base_cost *= (double)(players - players_with_tech);
      base_cost /= (double)players;
    }
    break;

  default:
    die("Invalid tech_leakage %d", game.info.tech_leakage);
  }

  /* Assign a science penalty to the AI at easier skill levels.  This code
   * can also be adpoted to create an extra-hard AI skill level where the AI
   * gets science benefits */

  if (pplayer && pplayer->ai.control) {
    assert(pplayer->ai.science_cost > 0);
    base_cost *= (double)pplayer->ai.science_cost / 100.0;
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
  return get_player_research(pplayer)->inventions[goal].num_required_techs;
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
  return get_player_research(pplayer)->inventions[goal].bulbs_required;
}

/**************************************************************************
 Returns number of requirements for the given tech. To not count techs
 double a memory (the counted array) is needed.
**************************************************************************/
static int precalc_tech_data_helper(Tech_type_id tech, bool *counted)
{
  if (tech == A_NONE || !tech_exists(tech) || counted[tech]) {
    return 0;
  }

  counted[tech] = TRUE;

  return 1 + 
      precalc_tech_data_helper(advances[tech].req[0], counted)+ 
      precalc_tech_data_helper(advances[tech].req[1], counted);
}

/**************************************************************************
 Function to precalculate needed data for technologies.
**************************************************************************/
void precalc_tech_data()
{
  bool counted[A_LAST];

  tech_type_iterate(tech) {
    memset(counted, 0, sizeof(counted));
    advances[tech].num_reqs = precalc_tech_data_helper(tech, counted);
  } tech_type_iterate_end;

  tech_type_iterate(tech) {
    double reqs = advances[tech].num_reqs + 1;
    const double base = game.info.base_tech_cost / 2;
    const double cost = base * reqs * sqrt(reqs);

    techcoststyle1[tech] = MAX(cost, game.info.base_tech_cost);
  } tech_type_iterate_end;
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
   * needed to work with more than one name at a time. */
  static struct string_vector future;

  switch (tech) {
  case A_FUTURE:
    if (pplayer) {
      struct player_research *research = get_player_research(pplayer);
      int i;
  
      /* pplayer->future_tech == 0 means "Future Tech. 1". */
      for (i = future.size; i <= research->future_tech; i++) {
        char *ptr = NULL;
  
        string_vector_append(&future, &ptr);
      }
      if (!future.p[research->future_tech]) {
        char buffer[1024];
  
        my_snprintf(buffer, sizeof(buffer), "%s %d",
                    advance_rule_name(tech),
                    research->future_tech + 1);
        future.p[research->future_tech] = mystrdup(buffer);
      }
      return future.p[research->future_tech];
    } else {
      return advance_rule_name(tech);
    }
  default:
    /* Includes A_NONE */
    return advance_rule_name(tech);
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
   * needed to work with more than one name at a time. */
  static struct string_vector future;

  switch (tech) {
  case A_FUTURE:
    if (pplayer) {
      struct player_research *research = get_player_research(pplayer);
      int i;
  
      /* pplayer->future_tech == 0 means "Future Tech. 1". */
      for (i = future.size; i <= research->future_tech; i++) {
        char *ptr = NULL;
  
        string_vector_append(&future, &ptr);
      }
      if (!future.p[research->future_tech]) {
        char buffer[1024];
  
        my_snprintf(buffer, sizeof(buffer), _("Future Tech. %d"),
                    research->future_tech + 1);
        future.p[research->future_tech] = mystrdup(buffer);
      }
      return future.p[research->future_tech];
    } else {
      return advance_name_translation(tech);
    }
  default:
    /* Includes A_NONE */
    return advance_name_translation(tech);
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
    get_player_research(pplayer)->researching);
}

/**************************************************************************
  Return the (translated) name of the given advance/technology.
  You don't have to free the return pointer.
**************************************************************************/
const char *advance_name_translation(Tech_type_id tech)
{
/*  assert(tech_exists(tech)); now called for A_UNSET, A_FUTURE */
  if (NULL == advances[tech].name.translated) {
    /* delayed (unified) translation */
    advances[tech].name.translated = ('\0' == advances[tech].name.vernacular[0])
				     ? advances[tech].name.vernacular
				     : Q_(advances[tech].name.vernacular);
  }
  return advances[tech].name.translated;
}

/****************************************************************************
  Return the (untranslated) rule name of the advance/technology.
  You don't have to free the return pointer.
****************************************************************************/
const char *advance_rule_name(Tech_type_id tech)
{
  return Qn_(advances[tech].name.vernacular); 
}

/**************************************************************************
 Returns true if the costs for the given technology will stay constant
 during the game. False otherwise.
**************************************************************************/
bool techs_have_fixed_costs()
{
  return ((game.info.tech_cost_style == 1
	   || game.info.tech_cost_style == 2)
	  && game.info.tech_leakage == 0);
}

/****************************************************************************
  Initialize tech structures.
****************************************************************************/
void techs_init(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(advances); i++) {
    advances[i].index = i;
  }

  /* Initialize dummy tech A_NONE */
  /* TRANS: "None" tech */
  sz_strlcpy(advances[A_NONE].name.vernacular, N_("None"));
  advances[A_NONE].name.translated = NULL;

  /* Initialize dummy tech A_UNSET */
  sz_strlcpy(advances[A_UNSET].name.vernacular, N_("None"));
  advances[A_UNSET].name.translated = NULL;

  /* Initialize dummy tech A_FUTURE */
  sz_strlcpy(advances[A_FUTURE].name.vernacular, N_("Future Tech."));
  advances[A_FUTURE].name.translated = NULL;

  /* Initialize dummy tech A_UNKNOWN */
  /* TRANS: "Unknown" tech */
  sz_strlcpy(advances[A_UNKNOWN].name.vernacular, N_("(Unknown)"));
  advances[A_UNKNOWN].name.translated = NULL;
}

/***************************************************************
 De-allocate resources associated with the given tech.
***************************************************************/
static void tech_free(Tech_type_id tech)
{
  struct advance *p = &advances[tech];

  free(p->helptext);
  p->helptext = NULL;

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
  Tech_type_id i;

  for (i = A_FIRST; i < game.control.num_tech_types; i++) {
    tech_free(i);
  }
}

/***************************************************************
 Fill the structure with some sane values
***************************************************************/
void player_research_init(struct player_research* research)
{
  memset(research, 0, sizeof(*research));
  research->tech_goal = A_UNSET;
  research->changed_from = -1;
}
