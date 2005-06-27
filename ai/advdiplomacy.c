/********************************************************************** 
 Freeciv - Copyright (C) 2003 - The Freeciv Team
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

#include "aisupport.h"
#include "city.h"
#include "diptreaty.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "nation.h"
#include "shared.h"
#include "spaceship.h"
#include "support.h"
#include "tech.h"

#include "citytools.h"
#include "diplhand.h"
#include "plrhand.h"
#include "maphand.h"
#include "settlers.h"  /* amortize */

#include "aidata.h"
#include "ailog.h"
#include "aitools.h"
#include "advmilitary.h"

#include "advdiplomacy.h"

/*

  "When a lobsterman leaves a trap out in the sea and can't get
  to it for a while, he will find the remains of many lobsters,
  but only one survivor. You might think that the survivor of the 
  lobster-battles would be the biggest lobster, but actually it 
  will always be the SECOND-SMALLEST. That's because when there 
  are a bunch of lobsters in the tank, they always gang up on the 
  biggest one first, until there are only two left, and then the 
  bigger one wins."
  (Anecdote by banjo@actrix.com)

Although the AIs are not this flawlessly deterministic in choosing their
enemies (they respect alliances and try to preserve treaties among other
things), this is the basic premise. Gang up on the biggest threat and
bring it down. Don't be too afraid to ally up with the others and don't
worry about our own safety. If everyone think the same way, there'll be
safety in numbers :-) If not, well, at least it makes a good show.

*/

#define LOG_DIPL LOG_DEBUG
#define LOG_DIPL2 LOG_DEBUG

/* one hundred thousand */
#define BIG_NUMBER 100000

/* turn this off when we don't want functions to message players */
static bool diplomacy_verbose = TRUE;

/**********************************************************************
  Send a diplomatic message. Use this instead of notify directly
  because we may want to highligh/present these messages differently
  in the future.
***********************************************************************/
static void notify(struct player *pplayer, const char *text, ...)
{
  if (diplomacy_verbose) {
    va_list ap;
    struct conn_list *dest = (struct conn_list*)&pplayer->connections;

    va_start(ap, text);
    vnotify_conn_ex(dest, NULL, E_DIPLOMACY, text, ap);
    va_end(ap);
  }
}

/********************************************************************** 
  This is your typical human reaction. Convert lack of love into 
  lust for gold.
***********************************************************************/
static int greed(int missing_love)
{
  if (missing_love > 0) {
    return 0;
  } else {
    /* Don't change the operation order here.
     * We do not want integer overflows */
    return -((missing_love * MAX_AI_LOVE) / 1000) * 
           ((missing_love * MAX_AI_LOVE) / 1000) /
	   50;
  }
}

/********************************************************************** 
  How much is a tech worth to player measured in gold
***********************************************************************/
static int ai_goldequiv_tech(struct player *pplayer, Tech_Type_id tech)
{
  int worth;

  if (get_invention(pplayer, tech) == TECH_KNOWN) {
    return 0;
  }
  worth = total_bulbs_required_for_goal(pplayer, tech) * 3;
  worth += MAX(pplayer->ai.tech_want[tech], 0) / MAX(game.turn, 1);
  if (get_invention(pplayer, tech) == TECH_REACHABLE) {
    worth /= 2;
  }
  return worth;
}

/************************************************************************
  Avoid giving pplayer's vision to non-allied player through aplayer 
  (shared vision is transitive).
************************************************************************/
static bool shared_vision_is_safe(struct player* pplayer,
                                  struct player* aplayer)
{
  if (pplayer->team != TEAM_NONE && pplayer->team == aplayer->team) {
    return TRUE;
  }
  players_iterate(eplayer) {
    if (eplayer == pplayer || eplayer == aplayer || !eplayer->is_alive) {
      continue;
    }
    if (gives_shared_vision(aplayer, eplayer)) {
      enum diplstate_type ds = pplayer_get_diplstate(pplayer, eplayer)->type;

      if (ds != DS_NO_CONTACT && ds != DS_ALLIANCE) {
        return FALSE;
      }
    }
  } players_iterate_end;
  return TRUE;
}

/********************************************************************** 
  Checks if player1 can agree on ceasefire with player2
  This function should only be used for ai players
**********************************************************************/
static bool ai_players_can_agree_on_ceasefire(struct player* player1,
                                              struct player* player2)
{
  struct ai_data *ai1;
  ai1 = ai_data_get(player1);
  return (ai1->diplomacy.target != player2 && 
          (player1 == ai1->diplomacy.alliance_leader ||
           !pplayers_at_war(player2, ai1->diplomacy.alliance_leader)) &&
	  player1->ai.love[player2->player_no] > - (MAX_AI_LOVE * 4 / 10)  &&
	  (ai1->diplomacy.target == NULL || 
	   !pplayers_allied(ai1->diplomacy.target, player2)));
}

/********************************************************************** 
  Evaluate gold worth of a single clause in a treaty. Note that it
  sometimes matter a great deal who is giving what to whom, and
  sometimes (such as with treaties) it does not matter at all.
***********************************************************************/
static int ai_goldequiv_clause(struct player *pplayer, 
                               struct player *aplayer,
                               struct Clause *pclause,
                               struct ai_data *ai,
                               bool verbose)
{
  int worth = 0; /* worth for pplayer of what aplayer gives */
  bool give = (pplayer == pclause->from);
  int giver;
  struct ai_dip_intel *adip = &ai->diplomacy.player_intel[aplayer->player_no];

  diplomacy_verbose = verbose;

  giver = pclause->from->player_no;

  switch (pclause->type) {
  case CLAUSE_ADVANCE:
    if (give) {
      worth -= ai_goldequiv_tech(aplayer, pclause->value);
    } else if (get_invention(pplayer, pclause->value) != TECH_KNOWN) {
      worth += ai_goldequiv_tech(pplayer, pclause->value);
    }

    /* Share and expect being shared brotherly between allies */
    if (pplayers_allied(pplayer, aplayer)) {
      worth /= 2;
    }
    if (players_on_same_team(pplayer, aplayer)) {
      worth = 0;
      break;
    }

    /* Do not bother wanting a tech that we already have. */
    if (!give && get_invention(pplayer, pclause->value) == TECH_KNOWN) {
      break;
    }

    /* Calculate in tech leak to our opponents, guess 50% chance */
    players_iterate(eplayer) {
      if (eplayer == aplayer
          || eplayer == pplayer
          || !eplayer->is_alive
          || get_invention(eplayer, pclause->value) == TECH_KNOWN) {
        continue;
      }
      if (give && pplayers_allied(aplayer, eplayer)) {
        if (is_player_dangerous(pplayer, eplayer)) {
          /* Don't risk it falling into enemy hands */
          worth = -BIG_NUMBER;
          break;
        }
        worth -= ai_goldequiv_tech(eplayer, pclause->value) / 2;
      } else if (!give && pplayers_allied(pplayer, eplayer)) {
        /* We can enrichen our side with this tech */
        worth += ai_goldequiv_tech(eplayer, pclause->value) / 4;
      }
    } players_iterate_end;
  break;

  case CLAUSE_ALLIANCE:
  case CLAUSE_PEACE:
  case CLAUSE_CEASEFIRE:
    /* Don't do anything in away mode */
    if (ai_handicap(pplayer, H_AWAY)) {
      notify(aplayer, _("*%s (AI)* In away mode AI can't sign such a treaty"),
             pplayer->name);
      worth = -BIG_NUMBER;
      break;
    }

    /* This guy is at war with our alliance and we're not alliance
     * leader. */
    if (pplayer != ai->diplomacy.alliance_leader
        && pplayers_at_war(aplayer, ai->diplomacy.alliance_leader)) {
      notify(aplayer, _("*%s (AI)* %s leads our alliance. You must contact "
             "and make peace with him first."), pplayer->name, 
             ai->diplomacy.alliance_leader->name);
      worth = -BIG_NUMBER;
      break;
    }

    /* And this guy is allied to one of our enemies. Only accept
     * ceasefire. */
    if (adip->is_allied_with_enemy
        && pclause->type != CLAUSE_CEASEFIRE) {
      notify(aplayer, _("*%s (AI)* First break alliance with %s, %s"),
             pplayer->name, adip->is_allied_with_enemy->name,
             aplayer->name);
      worth = -BIG_NUMBER;
      break;
    }

    /* Check if we can trust this guy. If we have to crash spacerace leader,
     * we don't care, though. */
    if (ai->diplomacy.acceptable_reputation > aplayer->reputation
        && ai->diplomacy.strategy != WIN_CAPITAL
	&& (pclause->type != CLAUSE_CEASEFIRE
	    || ai->diplomacy.acceptable_reputation_for_ceasefire > 
	       aplayer->reputation)) {
      notify(aplayer, _("*%s (AI)* Begone scoundrel, we all know that"
             " you cannot be trusted!"), pplayer->name);
      worth = -BIG_NUMBER;
      break;
    }

    /* Reduce treaty level?? */
    {
      enum diplstate_type ds = pplayer_get_diplstate(pplayer, aplayer)->type;

      if ((pclause->type == CLAUSE_PEACE && ds > DS_PEACE)
          || (pclause->type == CLAUSE_CEASEFIRE && ds > DS_CEASEFIRE)) {
        notify(aplayer, _("*%s (AI)* I will not let you go that easy, %s. "
               "The current treaty stands."), pplayer->name, aplayer->name);
        worth = -BIG_NUMBER;
        break;
      }
    }

    /* Let's all hold hands in one happy family! */
    if (adip->is_allied_with_ally) {
      worth = 0;
      break;
    }

    /* If this lucky fella got a ceasefire with da boss, then
     * let him live. */
    if (pplayer_get_diplstate(aplayer, ai->diplomacy.alliance_leader)->type
        == DS_CEASEFIRE && pclause->type == CLAUSE_CEASEFIRE) {
        notify(aplayer, _("*%s (AI)* %s recommended that I give you a ceasefire."
               " This is your lucky day."), pplayer->name,
               ai->diplomacy.alliance_leader->name);
        if (ai->diplomacy.target == aplayer) {
          /* Damn, we lost our target, too! Stupid boss! */
          ai->diplomacy.target = NULL;
          ai->diplomacy.timer = 0;
          ai->diplomacy.countdown = 0;
        }
        worth = 0;
        break;
    }

    /* Breaking treaties give us penalties on future diplomacy, so
     * avoid flip-flopping treaty/war with our chosen enemy. */
    if (aplayer == ai->diplomacy.target) {
      worth = -BIG_NUMBER;
      break;
    }

    /* Steps of the ladder */
    if (pclause->type == CLAUSE_PEACE) {
      if (!pplayers_non_attack(pplayer, aplayer)) {
        notify(aplayer, _("*%s (AI)* Let us first cease hostilies, %s"),
               pplayer->name, aplayer->name);
        worth = -BIG_NUMBER;
      } else {
        worth = greed(pplayer->ai.love[aplayer->player_no]
                      - ai->diplomacy.req_love_for_peace);
      }
    } else if (pclause->type == CLAUSE_ALLIANCE) {
      if (!pplayers_in_peace(pplayer, aplayer)) {
        notify(aplayer, _("*%s (AI)* Let us first make peace, %s"),
               pplayer->name, aplayer->name);
        worth = -BIG_NUMBER;
      } else {
        worth = greed(pplayer->ai.love[aplayer->player_no]
                      - ai->diplomacy.req_love_for_alliance);
      }
    } else {
      if (pplayer->ai.control && aplayer->ai.control &&
         ai_players_can_agree_on_ceasefire(pplayer, aplayer)) {
	 worth = 0;
      } else {
        worth = greed(pplayer->ai.love[aplayer->player_no]
                      - ai->diplomacy.req_love_for_ceasefire);
      }
    }
  break;

  case CLAUSE_GOLD:
    if (give) {
      worth -= pclause->value;
    } else {
      worth += pclause->value;
    }
    break;

  case CLAUSE_SEAMAP:
    if (!give || pplayers_allied(pplayer, aplayer)) {
      /* Useless to us - we're omniscient! And allies get it for free! */
      worth = 0;
    } else {
      /* Very silly algorithm 1: Sea map more worth if enemy has more
         cities. Reasoning is he has more use of seamap for settling
         new areas the more cities he has already. */
      worth -= 15 * city_list_size(&aplayer->cities);

      /* Make maps from novice player cheap */
      if (ai_handicap(pplayer, H_DIPLOMACY)) {
        worth /= 2;
      }
    }
    break;

  case CLAUSE_MAP:
    if (!give || pplayers_allied(pplayer, aplayer)) {
      /* Useless to us - we're omniscient! And allies get it for free! */
      worth = 0;
    } else {
      /* Very silly algorithm 2: Land map more worth the more cities
         we have, since we expose all of these to the enemy. */
      worth -= 50 * MAX(city_list_size(&pplayer->cities), 3);
      /* Inflate numbers if not peace */
      if (!pplayers_in_peace(pplayer, aplayer)) {
        worth *= 4;
      }
      /* Make maps from novice player cheap */
      if (ai_handicap(pplayer, H_DIPLOMACY)) {
        worth /= 6;
      }
    }
    break;

  case CLAUSE_CITY: {
    struct city *offer = city_list_find_id(&(pclause->from)->cities, 
                                           pclause->value);

    if (!offer || offer->owner != giver) {
      /* City destroyed or taken during negotiations */
      notify(aplayer, _("*%s (AI)* You don't have the offered city!"),
             pplayer->name);
      worth = 0;
    } else if (give) {
      /* AI must be crazy to trade away its cities */
      worth -= city_gold_worth(offer);
      if (is_capital(offer)) {
        worth = -BIG_NUMBER; /* Never! Ever! */
      } else {
        worth *= 15;
      }
      if (aplayer->player_no == offer->original) {
        /* Let them buy back their own city cheaper. */
        worth /= 2;
      }
    } else {
      worth = city_gold_worth(offer);      
    }
    break;
  }

  case CLAUSE_VISION:
    if (give) {
      if (pplayers_allied(pplayer, aplayer)) {
        if (!shared_vision_is_safe(pplayer, aplayer)) {
          notify(aplayer, _("*%s (AI)* Sorry, sharing vision with you "
	                    "is not safe."),
                 pplayer->name);
	  worth = -BIG_NUMBER;
	} else {
          worth = 0;
	}
      } else {
        /* so out of the question */
        worth = -BIG_NUMBER;
      }
    } else {
      worth = 0; /* We are omniscient, so... */
    }
    break;
  case CLAUSE_EMBASSY:
    if (give) {
      if (pplayers_in_peace(pplayer, aplayer)) {
        worth = 0;
      } else {
        worth = -BIG_NUMBER; /* No. */
      }
    } else {
      worth = 0; /* We don't need no stinkin' embassy, do we? */
    }
    break;
  case CLAUSE_UNUSED:
  case CLAUSE_LAST:
    break;
  } /* end of switch */

  diplomacy_verbose = TRUE;
  return worth;
}

/********************************************************************** 
  pplayer is AI player, aplayer is the other player involved, treaty
  is the treaty being considered. It is all a question about money :-)
***********************************************************************/
void ai_treaty_evaluate(struct player *pplayer, struct player *aplayer,
                        struct Treaty *ptreaty)
{
  int total_balance = 0;
  bool has_treaty = FALSE;
  bool only_gifts = TRUE;
  struct ai_data *ai = ai_data_get(pplayer);
  int given_cities = 0;

  assert(!is_barbarian(pplayer));

  /* Evaluate clauses */
  clause_list_iterate(ptreaty->clauses, pclause) {
    total_balance += ai_goldequiv_clause(pplayer, aplayer, pclause, ai, TRUE);
    if (is_pact_clause(pclause->type)) {
      has_treaty = TRUE;
    }
    if (pclause->type == CLAUSE_CITY && pclause->from == pplayer) {
	given_cities++;
    }
    if (pclause->type != CLAUSE_GOLD && pclause->type != CLAUSE_MAP
        && pclause->type != CLAUSE_SEAMAP && pclause->type != CLAUSE_VISION
        && (pclause->type != CLAUSE_ADVANCE 
            || pclause->value == pplayer->ai.tech_goal
            || pclause->value == pplayer->research.researching
            || is_tech_a_req_for_goal(pplayer, pclause->value, 
                                      pplayer->ai.tech_goal))) {
      /* We accept the above list of clauses as gifts, even if we are
       * at war. We do not accept tech or cities since these can be used
       * against us, unless we know that we want this tech anyway. */
      only_gifts = FALSE;
    }
  } clause_list_iterate_end;

  /* If we are at war, and no peace is offered, then no deal, unless
   * it is just gifts, in which case we gratefully accept. */
  if (pplayers_at_war(pplayer, aplayer) && !has_treaty && !only_gifts) {
    return;
  }

  if (given_cities > 0) {
    /* alway keep at least two cities */
    if (city_list_size(&pplayer->cities) - given_cities <= 2) {
      return;
    }
  }

  /* Accept if balance is good */
  if (total_balance >= 0) {
    handle_diplomacy_accept_treaty_req(pplayer, aplayer->player_no);
  }
}

/********************************************************************** 
  Comments to player from AI on clauses being agreed on. Does not
  alter any state.
***********************************************************************/
static void ai_treaty_react(struct player *pplayer,
                            struct player *aplayer,
                            struct Clause *pclause)
{
  struct ai_data *ai = ai_data_get(pplayer);
  struct ai_dip_intel *adip = &ai->diplomacy.player_intel[aplayer->player_no];

  switch (pclause->type) {
    case CLAUSE_ALLIANCE:
      if (adip->is_allied_with_ally) {
        notify(aplayer, _("*%s (AI)* Welcome into our alliance %s!"),
               pplayer->name, aplayer->name);
      } else {
        notify(aplayer, _("*%s (AI)* Yes, may we forever stand united, %s"),
               pplayer->name, aplayer->name);
      }
      break;
    case CLAUSE_PEACE:
      notify(aplayer, _("*%s (AI)* Yes, peace in our time!"),
             pplayer->name);
      break;
    case CLAUSE_CEASEFIRE:
      notify(aplayer, _("*%s (AI)* Agreed. No more hostilities, %s"),
             pplayer->name, aplayer->name);
      break;
    default:
      break;
  }
}

/********************************************************************** 
  This function is called when a treaty has been concluded, to deal
  with followup issues like comments and relationship (love) changes.

  pplayer is AI player, aplayer is the other player involved, ptreaty
  is the treaty accepted.
***********************************************************************/
void ai_treaty_accepted(struct player *pplayer, struct player *aplayer,
                        struct Treaty *ptreaty)
{
  int total_balance = 0;
  bool gift = TRUE;
  struct ai_data *ai = ai_data_get(pplayer);

  /* Evaluate clauses */
  clause_list_iterate(ptreaty->clauses, pclause) {
    int balance = ai_goldequiv_clause(pplayer, aplayer, pclause, ai, TRUE);
    total_balance += balance;
    gift = (gift && (balance >= 0));
    ai_treaty_react(pplayer, aplayer, pclause);
    if (pclause->type == CLAUSE_ALLIANCE && ai->diplomacy.target == aplayer) {
      ai->diplomacy.target = NULL; /* Oooops... */
    }
  } clause_list_iterate_end;

  /* Rather arbitrary algorithm to increase our love for a player if
   * he or she offers us gifts. It is only a gift if _all_ the clauses
   * are beneficial to us. */
  if (total_balance > 0 && gift) {
    int i = total_balance / ((city_list_size(&pplayer->cities) * 50) + 1);

    i = MIN(i, ai->diplomacy.love_incr * 150) * 10;
    pplayer->ai.love[aplayer->player_no] += i;
    PLAYER_LOG(LOG_DIPL2, pplayer, ai, "%s's gift to %s increased love by %d",
            aplayer->name, pplayer->name, i);
  }
}

/********************************************************************** 
  Calculate our desire to go to war against aplayer.
***********************************************************************/
static int ai_war_desire(struct player *pplayer, struct player *aplayer,
                         struct ai_data *ai)
{
  int kill_desire;
  struct player_spaceship *ship = &aplayer->spaceship;
  struct ai_dip_intel *adip = &ai->diplomacy.player_intel[aplayer->player_no];

  /* Number of cities is a player's base potential. */
  kill_desire = city_list_size(&aplayer->cities);

  /* Count settlers in production for us, indicating our expansionism,
   * while counting all enemy settlers as (worst case) indicators of
   * enemy expansionism */
  city_list_iterate(pplayer->cities, pcity) {
    if (pcity->is_building_unit 
        && unit_type_flag(pcity->currently_building, F_CITIES)) {
      kill_desire -= 1;
    }
  } city_list_iterate_end;
  unit_list_iterate(aplayer->units, punit) { 
    if (unit_flag(punit, F_CITIES)) {
      kill_desire += 1;
    }
  } unit_list_iterate_end;

  /* Count big cities as twice the threat */
  city_list_iterate(aplayer->cities, pcity) {
    kill_desire += pcity->size > 8 ? 1 : 0;
  } city_list_iterate_end;

  /* Tech lead is worrisome */
  kill_desire += MAX(aplayer->research.techs_researched -
                     pplayer->research.techs_researched, 0);

  /* Spacerace loss we will not allow! */
  if (ship->state >= SSHIP_STARTED) {
    /* add potential */
    kill_desire += city_list_size(&aplayer->cities);
  }
  if (ai->diplomacy.spacerace_leader == aplayer) {
    ai->diplomacy.strategy = WIN_CAPITAL;
    return BIG_NUMBER; /* do NOT amortize this number! */
  }

  /* Modify by which treaties we would have to break, and what
   * excuses we have to do so. FIXME: We only consider immediate
   * allies, but we might trigger a wider chain reaction. */
  players_iterate(eplayer) {
    bool cancel_excuse =
	pplayer->diplstates[eplayer->player_no].has_reason_to_cancel != 0;
    enum diplstate_type ds = pplayer_get_diplstate(pplayer, eplayer)->type;

    if (eplayer == pplayer || !eplayer->is_alive) {
      continue;
    }

    /* Remember: pplayers_allied() returns true when aplayer == eplayer */
    if (!cancel_excuse && pplayers_allied(aplayer, eplayer)) {
      if (ds == DS_CEASEFIRE) {
        kill_desire -= kill_desire / 10; /* 10% off */
      } else if (ds == DS_NEUTRAL) {
        kill_desire -= kill_desire / 7; /* 15% off */
      } else if (ds == DS_PEACE) {
        kill_desire -= kill_desire / 5; /* 20% off */
      } else if (ds == DS_ALLIANCE) {
        kill_desire -= kill_desire / 3; /* 33% off here, more later */
      }
    }
  } players_iterate_end;

  /* Modify by love. Increase the divisor to make ai go to war earlier */
  kill_desire -= MAX(0, kill_desire 
                        * pplayer->ai.love[aplayer->player_no] 
                        / (2 * MAX_AI_LOVE));

  /* Make novice AI more peaceful with human players */
  if (ai_handicap(pplayer, H_DIPLOMACY) && !aplayer->ai.control) {
    kill_desire = kill_desire / 2 - 5;
  }

  /* Amortize by distance */
  return amortize(kill_desire, adip->distance);
}

/********************************************************************** 
  Suggest a treaty from pplayer to aplayer
***********************************************************************/
static void ai_diplomacy_suggest(struct player *pplayer, 
                                 struct player *aplayer,
                                 enum clause_type what,
                                 int value)
{
  if (!could_meet_with_player(pplayer, aplayer)) {
    freelog(LOG_DIPL2, "%s tries to do diplomacy to %s without contact",
            pplayer->name, aplayer->name);
    return;
  }

  handle_diplomacy_init_meeting_req(pplayer, aplayer->player_no);
  handle_diplomacy_create_clause_req(pplayer, aplayer->player_no,
				     pplayer->player_no, what, value);
}

/********************************************************************** 
  Calculate our diplomatic predispositions here. Don't do anything.

  Only ever called for AI players and never for barbarians.
***********************************************************************/
void ai_diplomacy_calculate(struct player *pplayer, struct ai_data *ai)
{
  int war_desire[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  int best_desire = 0;
  struct player *target = NULL;

  memset(war_desire, 0, sizeof(war_desire));

  assert(pplayer->ai.control);
  if (!pplayer->is_alive) {
    return; /* duh */
  }

  /* Time to make love. If we've been wronged, hold off that love
   * for a while. Also, cool our head each turn with love_coeff. */
  players_iterate(aplayer) {
    int a = aplayer->player_no;
    struct ai_dip_intel *adip = &ai->diplomacy.player_intel[a];

    if (pplayer == aplayer || !aplayer->is_alive) {
      continue;
    }
    pplayer->ai.love[aplayer->player_no] -= 
                         pplayer->diplstates[a].has_reason_to_cancel;
    if ((pplayers_non_attack(pplayer, aplayer) 
         || pplayers_allied(pplayer, aplayer))
        && pplayer->diplstates[a].has_reason_to_cancel == 0
        && !adip->is_allied_with_enemy
        && !adip->at_war_with_ally
        && adip->ally_patience >= 0) {
      pplayer->ai.love[aplayer->player_no] += ai->diplomacy.love_incr;
      PLAYER_LOG(LOG_DEBUG, pplayer, ai, "Increased love for %s (now %d)",
                 aplayer->name, pplayer->ai.love[aplayer->player_no]);
    } else if (pplayer->diplstates[aplayer->player_no].type == DS_WAR) {
      pplayer->ai.love[aplayer->player_no] -= ai->diplomacy.love_incr;
      if (ai->diplomacy.target != aplayer && 
          pplayer->ai.love[aplayer->player_no] < 0) {
        /* Give him a better chance for a cease fire */
        pplayer->ai.love[aplayer->player_no] += (MAX_AI_LOVE) * 3 / 100;
      }
      PLAYER_LOG(LOG_DEBUG, pplayer, ai, "Reduced love for %s (now %d) ",
                 aplayer->name, pplayer->ai.love[aplayer->player_no]);
    } else if (pplayer->diplstates[a].has_reason_to_cancel != 0) {
      /* Provoked in time of peace */
      if (pplayer->ai.love[aplayer->player_no] > 0) {
        PLAYER_LOG(LOG_DEBUG, pplayer, ai, "Provoked by %s! Love halved "
                   "(was %d)", aplayer->name, 
                   pplayer->ai.love[aplayer->player_no]);
        pplayer->ai.love[aplayer->player_no] /= 2;
      }
      pplayer->ai.love[aplayer->player_no] -= ai->diplomacy.love_incr;
    }
    /* Reduce love by number of units in our territory.
     * AI is so naive, that we have to count it even if players are allied */
    pplayer->ai.love[aplayer->player_no] -=
      MIN(player_in_territory(pplayer, aplayer) * (MAX_AI_LOVE / 100),
          pplayers_allied(aplayer, pplayer) ? 
	    ai->diplomacy.love_incr - 1 : (MAX_AI_LOVE / 2));
 
    /* Increase the love if aplayer has got a building that makes 
     * us love him more. Typically it's Eiffel Tower */
    pplayer->ai.love[aplayer->player_no] +=
      get_player_bonus(aplayer, EFT_GAIN_AI_LOVE) * MAX_AI_LOVE / 1000;
  	  
    /* Massage our numbers to keep love and its opposite on the ground. 
     * Gravitate towards zero. */
    pplayer->ai.love[aplayer->player_no] -= 
       (pplayer->ai.love[aplayer->player_no] * ai->diplomacy.love_coeff / 100);
       
    /* ai love should always be in range [-MAX_AI_LOVE..MAX_AI_LOVE] */
    pplayer->ai.love[aplayer->player_no] = 
      MAX(-MAX_AI_LOVE,
          MIN(MAX_AI_LOVE, pplayer->ai.love[aplayer->player_no]));
  } players_iterate_end;

  /* Stop war against a dead player */
  if (ai->diplomacy.target && !ai->diplomacy.target->is_alive) {
    PLAYER_LOG(LOG_DIPL2, pplayer, ai, "Target player %s is dead! Victory!",
               ai->diplomacy.target->name);
    ai->diplomacy.timer = 0;
    ai->diplomacy.countdown = 0;
    ai->diplomacy.target = NULL;
    if (ai->diplomacy.strategy == WIN_CAPITAL) {
      ai->diplomacy.strategy = WIN_OPEN;
    }
  }

  /* Can we win by space race? */
  if (ai->diplomacy.spacerace_leader == pplayer) {
    freelog(LOG_DIPL2, "%s going for space race victory!", pplayer->name);
    ai->diplomacy.strategy = WIN_SPACE; /* Yes! */
  } else {
    if (ai->diplomacy.strategy == WIN_SPACE) {
       ai->diplomacy.strategy = WIN_OPEN;
    }
  }

  if (ai->diplomacy.countdown > 0) {
    ai->diplomacy.countdown--;
  }

  /* Ensure that we don't prematurely end an ongoing war */
  if (ai->diplomacy.timer-- > 0) {
    return;
  }

  /* Calculate average distances to other players' empires. */
  players_iterate(aplayer) {
    ai->diplomacy.player_intel[aplayer->player_no].distance = 
          player_distance_to_player(pplayer, aplayer);
  } players_iterate_end;

  /* Calculate our desires, and find desired war target */
  players_iterate(aplayer) {
    enum diplstate_type ds = pplayer_get_diplstate(pplayer, aplayer)->type;
    struct ai_dip_intel *adip = &ai->diplomacy.player_intel[aplayer->player_no];

    /* We don't hate ourselves, those we don't know or team members
     * Defer judgement on alliance members we're not (yet) allied to
     * to the alliance leader. Always respect ceasefires the boss has signed. 
     */
    if (aplayer == pplayer
        || !aplayer->is_alive
        || ds == DS_NO_CONTACT
        || players_on_same_team(pplayer, aplayer)
        || (pplayer != ai->diplomacy.alliance_leader && 
	    aplayer != ai->diplomacy.alliance_leader &&
            adip->is_allied_with_ally)
        || (pplayer_get_diplstate(aplayer, ai->diplomacy.alliance_leader)->type
            == DS_CEASEFIRE)) {
      continue;
    }
    war_desire[aplayer->player_no] = ai_war_desire(pplayer, aplayer, ai);

    /* We don't want war if we can win through the space race. */
    if (ai->diplomacy.strategy == WIN_SPACE && !adip->at_war_with_ally) {
      continue;    
    }

    /* Strongly prefer players we are at war with already. */
    if (!pplayers_at_war(pplayer, aplayer)) {
      war_desire[aplayer->player_no] /= 2;
    }
    
    PLAYER_LOG(LOG_DEBUG, pplayer, ai, "Against %s we have war desire "
            "%d ", aplayer->name, war_desire[aplayer->player_no]);

    /* Find best target */
    if (war_desire[aplayer->player_no] > best_desire) {
      target = aplayer;
      best_desire = war_desire[aplayer->player_no];
    }
  } players_iterate_end;

  if (!target) {
    PLAYER_LOG(LOG_DEBUG, pplayer, ai, "Found no target.");
    ai->diplomacy.target = NULL;
    return;
  }

  /* Switch to target */
  if (target != ai->diplomacy.target) {
    PLAYER_LOG(LOG_DIPL, pplayer, ai, "Setting target to %s", target->name);
    ai->diplomacy.target = target;
    if (ai->diplomacy.strategy == WIN_CAPITAL) {
      ai->diplomacy.countdown = 1; /* Quickly!! */
    } else if (pplayer->diplstates[target->player_no].has_reason_to_cancel > 1) {
      /* Turns until we lose our casus bellum, exploit that. */
      ai->diplomacy.countdown = 
                    pplayer->diplstates[target->player_no].has_reason_to_cancel
                    - 1;
    } else {
      ai->diplomacy.countdown = 6; /* Take the time we need - WAG */
    }
    /* Don't reevaluate too often. */
    ai->diplomacy.timer = myrand(6) + 6 + ai->diplomacy.countdown;
    players_iterate(aplayer) {
      ai->diplomacy.player_intel[aplayer->player_no].ally_patience = 0;
    } players_iterate_end;
  }
}

/********************************************************************** 
  Offer techs and stuff to other player and ask for techs we need.
***********************************************************************/
static void ai_share(struct player *pplayer, struct player *aplayer)
{
  int index;

  /* Only share techs with team mates */
  if (players_on_same_team(pplayer, aplayer)) {
    for (index = A_FIRST; index < game.num_tech_types; index++) {
      if ((get_invention(pplayer, index) != TECH_KNOWN)
          && (get_invention(aplayer, index) == TECH_KNOWN)) {
       ai_diplomacy_suggest(aplayer, pplayer, CLAUSE_ADVANCE, index);
      } else if ((get_invention(pplayer, index) == TECH_KNOWN)
          && (get_invention(aplayer, index) != TECH_KNOWN)) {
        ai_diplomacy_suggest(pplayer, aplayer, CLAUSE_ADVANCE, index);
      }
    }
  }
  if (!gives_shared_vision(pplayer, aplayer) && 
      shared_vision_is_safe(pplayer, aplayer)) {
    ai_diplomacy_suggest(pplayer, aplayer, CLAUSE_VISION, 0);
  }
  if (!gives_shared_vision(aplayer, pplayer) &&
      shared_vision_is_safe(aplayer, pplayer)) {
    ai_diplomacy_suggest(aplayer, pplayer, CLAUSE_VISION, 0);
  }
  if (!player_has_embassy(pplayer, aplayer)) {
    ai_diplomacy_suggest(aplayer, pplayer, CLAUSE_EMBASSY, 0);
  }
  if (!player_has_embassy(aplayer, pplayer)) {
    ai_diplomacy_suggest(pplayer, aplayer, CLAUSE_EMBASSY, 0);
  }
}

/********************************************************************** 
  Go to war.
***********************************************************************/
static void ai_go_to_war(struct player *pplayer, struct ai_data *ai,
                         struct player *target)
{
  if (gives_shared_vision(pplayer, target)) {
    remove_shared_vision(pplayer, target);
  }

  /* will take us straight to war */
  handle_diplomacy_cancel_pact(pplayer, target->player_no, CLAUSE_LAST);

  /* Continue war at least in this arbitrary number of turns to show 
   * some spine */
  ai->diplomacy.timer = myrand(4) + 3;
  if (pplayer->ai.love[target->player_no] < 0) {
    ai->diplomacy.timer -= pplayer->ai.love[target->player_no] / 10;
  } else {
    /* We DO NOT love our enemies! AIs are heatens! */
    pplayer->ai.love[target->player_no] = -1; 
  }
  assert(!gives_shared_vision(pplayer, target));
}

/********************************************************************** 
  Do diplomatic actions. Must be called only after calculate function
  above has been run for _all_ AI players.

  Only ever called for AI players and never for barbarians.
***********************************************************************/
void ai_diplomacy_actions(struct player *pplayer)
{
  struct ai_data *ai = ai_data_get(pplayer);
  struct player *target = ai->diplomacy.target;

  assert(pplayer->ai.control);
  if (!pplayer->is_alive) {
    return;
  }

  /*** If we are greviously insulted, go to war immediately. ***/

  players_iterate(aplayer) {
    if (ai->diplomacy.acceptable_reputation > aplayer->reputation
        && pplayer->ai.love[aplayer->player_no] < 0
        && pplayer->diplstates[aplayer->player_no].has_reason_to_cancel >= 2) {
      PLAYER_LOG(LOG_DIPL2, pplayer, ai, "Declaring war on %s in revenge",
                 target->name);
      notify(target, _("*%s (AI)* I will NOT accept such behaviour! This "
             "means WAR!"), pplayer->name);
      ai_go_to_war(pplayer, ai, aplayer);
    }
  } players_iterate_end;

  /*** Stop other players from winning by space race ***/

  if (ai->diplomacy.strategy != WIN_SPACE) {
    players_iterate(aplayer) {
      struct ai_dip_intel *adip =
                         &ai->diplomacy.player_intel[aplayer->player_no];
      struct player_spaceship *ship = &aplayer->spaceship;

      if (!aplayer->is_alive || aplayer == pplayer
          || players_on_same_team(pplayer, aplayer)
          || ship->state == SSHIP_NONE) {
        continue;
      }
      /* A spaceship victory is always one single player's or team's victory */
      if (aplayer->spaceship.state == SSHIP_LAUNCHED
          && ai->diplomacy.spacerace_leader == aplayer
          && pplayers_allied(pplayer, aplayer)) {
        notify(aplayer, _("*%s (AI)* Your attempt to conquer space for "
               "yourself alone betray your true intentions, and I "
               "will have no more of our alliance!"), pplayer->name);
	handle_diplomacy_cancel_pact(pplayer, aplayer->player_no,
				     CLAUSE_ALLIANCE);
        if (gives_shared_vision(pplayer, aplayer)) {
          remove_shared_vision(pplayer, aplayer);
        }
        /* Never forgive this */
        pplayer->ai.love[aplayer->player_no] = -(BIG_NUMBER);
      } else if (ship->state == SSHIP_STARTED 
		 && adip->warned_about_space == 0) {
        adip->warned_about_space = 10 + myrand(6);
        notify(aplayer, _("*%s (AI)* Your attempt to unilaterally "
               "dominate outer space is highly offensive."), pplayer->name);
        notify(aplayer, _("*%s (AI)* If you do not stop constructing your "
               "spaceship, I may be forced to take action!"), pplayer->name);
      }
      if (aplayer->spaceship.state == SSHIP_LAUNCHED
          && aplayer == ai->diplomacy.spacerace_leader) {
        /* This means war!!! */
        ai->diplomacy.timer = 0; /* Force reevaluation next turn */
      }
    } players_iterate_end;
  }

  /*** Declare war - against target ***/

  if (target && pplayers_at_war(pplayer, target)) {
    ai->diplomacy.countdown = 0; /* cosmetic */
  }
  if (target && !pplayers_at_war(pplayer, target)
      && ai->diplomacy.countdown <= 0
      && !ai_handicap(pplayer, H_AWAY)) {
    if (pplayers_allied(pplayer, target)) {
      PLAYER_LOG(LOG_DEBUG, pplayer, ai, "Went to war against %s, who is "
                 "an ally!", target->name); /* Oh, my. */
    }
    if (pplayer->diplstates[target->player_no].has_reason_to_cancel > 0) {
      /* We have good reason */
      notify(target, _("*%s (AI)* Your despicable actions will not go "
             "unpunished!"), pplayer->name);
    } if (pplayer->ai.love[target->player_no] < 0) {
      /* We have a reason of sorts from way back. */
      notify(target, _("*%s (AI)* Finally I get around to you! Did "
             "you really think you could get away with your crimes?"),
             pplayer->name);
    } else {
      /* We have no legimitate reason... So what? */
      notify(target, _("*%s (AI)* Peace in ... some other time"),
             pplayer->name);
    }
    ai_go_to_war(pplayer, ai, target);
  }

  /*** Declare war - against enemies of allies ***/

  players_iterate(aplayer) {
    struct ai_dip_intel *adip = &ai->diplomacy.player_intel[aplayer->player_no];

    if (aplayer->is_alive
        && adip->at_war_with_ally
        && !adip->is_allied_with_ally
        && !pplayers_at_war(pplayer, aplayer)
	&& (pplayer_get_diplstate(pplayer, aplayer)->type != DS_CEASEFIRE || 
	    myrand(5) < 1)) {
      notify(aplayer, _("*%s (AI)* Your aggression against my allies was "
			"your last mistake!"), pplayer->name);
      ai_go_to_war(pplayer, ai, aplayer);
    }
  } players_iterate_end;

  /*** Opportunism, Inc. Try to make peace with everyone else ***/

  players_iterate(aplayer) {
    enum diplstate_type ds = pplayer_get_diplstate(pplayer, aplayer)->type;
    struct ai_dip_intel *adip = &ai->diplomacy.player_intel[aplayer->player_no];
    struct Clause clause;

    /* Meaningless values, but rather not have them unset. */
    clause.from = pplayer;
    clause.value = 0;

    /* Remove shared vision if we are not allied or it is no longer safe. */
    if (gives_shared_vision(pplayer, aplayer)) {
      if (!pplayers_allied(pplayer, aplayer)) {
        remove_shared_vision(pplayer, aplayer);
      } else if (!shared_vision_is_safe(pplayer, aplayer)) {
        notify(aplayer, _("*%s (AI)* Sorry, sharing vision with you "
	                    "is no longer safe."),
	       pplayer->name);
	remove_shared_vision(pplayer, aplayer);
      }
    }

    /* No peace to enemies of our allies... or pointless peace. */
    if (is_barbarian(aplayer)
        || aplayer == pplayer
        || aplayer == target     /* no mercy */
        || !aplayer->is_alive
        || !could_meet_with_player(pplayer, aplayer)
        || adip->at_war_with_ally) {
      continue;
    }

    /* Spam control */
    adip->asked_about_peace = MAX(adip->asked_about_peace - 1, 0);
    adip->asked_about_alliance = MAX(adip->asked_about_alliance - 1, 0);
    adip->asked_about_ceasefire = MAX(adip->asked_about_ceasefire - 1, 0);
    adip->warned_about_space = MAX(adip->warned_about_space - 1, 0);
    adip->spam = MAX(adip->spam - 1, 0);
    if (adip->spam > 0) {
      /* Don't spam */
      continue;
    }

    /* Canvass support from existing friends for our war, and try to
     * make friends with enemies. Then we wait some turns until next time
     * we spam them with our gibbering chatter. */
    if (!aplayer->ai.control) {
      if (!pplayers_allied(pplayer, aplayer)) {
        adip->spam = myrand(4) + 3; /* Bugger allies often. */
      } else {
        adip->spam = myrand(8) + 6; /* Others are less important. */
      }
    }

    switch (ds) {
    case DS_TEAM:
      ai_share(pplayer, aplayer);
      break;
    case DS_ALLIANCE:
      if (players_on_same_team(pplayer, aplayer)
          || (target && (!pplayers_at_war(pplayer, target)
              || pplayers_at_war(aplayer, target)))) {
        /* Share techs only with team mates and _reliable_ allies.
         * This means, if we have a target, then unless we are still
         * in countdown mode, we _except_ our allies to be at war
         * with our target too! */
        ai_share(pplayer, aplayer);
        adip->ally_patience = 0;
        break;
      } else if (!target) {
        adip->ally_patience = 0;
        break;
      }
      if (target && pplayer->ai.control) {
        PLAYER_LOG(LOG_DIPL2, pplayer, ai, "Ally %s not at war with enemy %s "
                "(patience %d, %s %s)", aplayer->name, 
                target->name, adip->ally_patience, adip->at_war_with_ally
                ? "war_with_ally" : "", adip->is_allied_with_ally ? 
                "allied_with_ally" : "");

      }
      switch (adip->ally_patience--) {
        case 0:
          notify(aplayer, _("*%s (AI)* Greetings our most trustworthy "
                 "ally, we call upon you to destroy our enemy, %s"), 
                 pplayer->name, target->name);
          break;
        case -1:
          notify(aplayer, _("*%s (AI)* Greetings ally, I see you have not yet "
                 "made war with our enemy, %s. Why do I need to remind "
                 "you of your promises?"), pplayer->name, target->name);
          break;
        case -2:
          notify(aplayer, _("*%s (AI)* Dishonoured one, we made a pact of "
                 "alliance, and yet you remain at peace with our mortal "
                 "enemy, %s! This is unacceptable, our alliance is no "
                 "more!"), pplayer->name, target->name);
          PLAYER_LOG(LOG_DIPL2, pplayer, ai, "breaking useless alliance with "
                     "%s", aplayer->name);
	  /* to peace */
	  handle_diplomacy_cancel_pact(pplayer, aplayer->player_no,
				       CLAUSE_ALLIANCE);
          pplayer->ai.love[aplayer->player_no] = 
                                 MIN(pplayer->ai.love[aplayer->player_no], 0);
          if (gives_shared_vision(pplayer, aplayer)) {
            remove_shared_vision(pplayer, aplayer);
          }
          assert(!gives_shared_vision(pplayer, aplayer));
          break;
      }
      break;

    case DS_PEACE:
      clause.type = CLAUSE_ALLIANCE;
      if (ai_goldequiv_clause(pplayer, aplayer, &clause, ai, FALSE) < 0
          || (adip->asked_about_alliance > 0 && !aplayer->ai.control)
          || !target) {
        /* Note that we don't ever ask for alliance unless we have a target */
        break; 
      }
      ai_diplomacy_suggest(pplayer, aplayer, CLAUSE_ALLIANCE, 0);
      adip->asked_about_alliance = !aplayer->ai.control ? 13 : 0;
      notify(aplayer, _("*%s (AI)* Greetings friend, may we suggest "
             "a joint campaign against %s?"), pplayer->name, target->name);
      break;

    case DS_CEASEFIRE:
    case DS_NEUTRAL:
      clause.type = CLAUSE_PEACE;
      if (ai_goldequiv_clause(pplayer, aplayer, &clause, ai, FALSE) < 0
          || (adip->asked_about_peace > 0 && !aplayer->ai.control)
          || !target) {
        /* Note that we don't ever ask for peace unless we have a target */
        break; /* never */
      }
      ai_diplomacy_suggest(pplayer, aplayer, CLAUSE_PEACE, 0);
      adip->asked_about_peace = !aplayer->ai.control ? 12 : 0;
      notify(aplayer, _("*%s (AI)* Greetings neighbour, may we suggest "
             "a joint campaign against %s?"), pplayer->name, target->name);
      break;

    case DS_NO_CONTACT: /* but we do have embassy! weird. */
    case DS_WAR:
      clause.type = CLAUSE_CEASEFIRE;
      if (ai_goldequiv_clause(pplayer, aplayer, &clause, ai, FALSE) < 0
          || (adip->asked_about_ceasefire > 0 && !aplayer->ai.control)
          || !target) {
        break; /* Fight until the end! */
      }
      ai_diplomacy_suggest(pplayer, aplayer, CLAUSE_CEASEFIRE, 0);
      adip->asked_about_ceasefire = !aplayer->ai.control ? 9 : 0;
      notify(aplayer, _("*%s (AI)* %s is threatening us both, may we "
             "suggest a cessation of hostilities?"), pplayer->name,
             target->name);
      break;
    default:
      die("Unknown pact type");
      break;
    }
  } players_iterate_end;
}
