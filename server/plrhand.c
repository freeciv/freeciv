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
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "capability.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"
#include "tech.h"
#include "unit.h"
#include "version.h"

#include "cityhand.h" 
#include "citytools.h"
#include "cityturn.h"
#include "gamelog.h"
#include "maphand.h"
#include "sernet.h"
#include "settlers.h"
#include "spacerace.h"
#include "srv_main.h"
#include "unitfunc.h" 
#include "unittools.h"
#include "unithand.h"

#include "aicity.h"
#include "aihand.h"
#include "aitech.h"
#include "aiunit.h"

#include "plrhand.h"

static char dec2hex[] = "0123456789abcdef"; /* FIXME: copied from maphand.c */
static char terrain_chars[] = "adfghjm prstu";

static void update_player_aliveness(struct player *pplayer);

/**************************************************************************
...
**************************************************************************/
void do_dipl_cost(struct player *pplayer)
{
  pplayer->research.researched -=(research_time(pplayer)*game.diplcost)/100;
}
void do_free_cost(struct player *pplayer)
{
  pplayer->research.researched -=(research_time(pplayer)*game.freecost)/100;
}
void do_conquer_cost(struct player *pplayer)
{
  pplayer->research.researched -=(research_time(pplayer)*game.conquercost)/100;
}

/**************************************************************************
  Send end-of-turn notifications relevant to specified dests.
  If dest is NULL, do all players, sending to pplayer->connections.
**************************************************************************/
void send_player_turn_notifications(struct conn_list *dest)
{
  if (dest) {
    conn_list_iterate(*dest, pconn) {
      struct player *pplayer = pconn->player;
      if (pplayer) {
	city_list_iterate(pplayer->cities, pcity) {
	  send_city_turn_notifications(&pconn->self, pcity);
	}
	city_list_iterate_end;
      }
    }
    conn_list_iterate_end;
  }
  else {
    int i;
    for (i=0; i<game.nplayers; i++) {
      struct player *pplayer = &game.players[i];
      city_list_iterate(pplayer->cities, pcity) {
	send_city_turn_notifications(&pplayer->connections, pcity);
      }
      city_list_iterate_end;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void great_library(struct player *pplayer)
{
  int i;
  if (wonder_obsolete(B_GREAT)) 
    return;
  if (find_city_wonder(B_GREAT)) {
    if (pplayer->player_no==find_city_wonder(B_GREAT)->owner) {
      for (i=0;i<game.num_tech_types;i++) {
	if (get_invention(pplayer, i)!=TECH_KNOWN 
	    && game.global_advances[i]>=2) {
	  notify_player_ex(pplayer, -1, -1, E_TECH_GAIN,
			   _("Game: %s acquired from The Great Library!"),
			   advances[i].name);
	  gamelog(GAMELOG_TECH,"%s discover %s (Library)",
		  get_nation_name_plural(pplayer->nation),advances[i].name);
	  notify_embassies(pplayer,(struct player*)0,
			   _("Game: The %s have aquired %s"
			     " from the Great Library."),
			   get_nation_name_plural(pplayer->nation),
			   advances[i].name);

	  do_free_cost(pplayer);
	  found_new_tech(pplayer,i,0,0);
	  break;
	}
      }
    }
  }
}

/**************************************************************************
Count down if the player are in a revolution, notify him when revolution
has ended.
**************************************************************************/
static void update_revolution(struct player *pplayer)
{
  if(pplayer->revolution) 
    pplayer->revolution--;
}

/**************************************************************************
Turn info update loop, for each player at beginning of turn.
**************************************************************************/
void begin_player_turn(struct player *pplayer)
{
  begin_cities_turn(pplayer);
}

/**************************************************************************
Main update loop, for each player at end of turn.
**************************************************************************/
void update_player_activities(struct player *pplayer) 
{
  if (pplayer->ai.control) {
    ai_do_last_activities(pplayer); /* why was this AFTER aliveness? */
  }
  great_library(pplayer);
  update_revolution(pplayer);
  player_restore_units(pplayer); /*note: restoring move point moved
				   to update_unit_activities*/
  update_city_activities(pplayer);
#ifdef CITIES_PROVIDE_RESEARCH
  if (city_list_size(&pplayer->cities)) /* has to be below the above for got_tech */ 
    update_tech(pplayer, city_list_size(&pplayer->cities));
#endif
  pplayer->research.changed_from=-1;
  update_unit_activities(pplayer);
  update_player_aliveness(pplayer);
}

/**************************************************************************
...
**************************************************************************/
static void update_player_aliveness(struct player *pplayer)
{
  if(pplayer->is_alive) {
    if(unit_list_size(&pplayer->units)==0 && 
       city_list_size(&pplayer->cities)==0) {
      pplayer->is_alive=0;
      if( !is_barbarian(pplayer) ) {
        notify_player_ex(0, -1, -1, E_DESTROYED, _("Game: The %s are no more!"), 
		         get_nation_name_plural(pplayer->nation));
        gamelog(GAMELOG_GENO, "%s civilization destroyed",
                get_nation_name(pplayer->nation));
      }
      map_know_and_see_all(pplayer);
    }
  }
}

/**************************************************************************
Player has a new technology (from somewhere)
was_discovery is passed on to upgrade_city_rails
Logging & notification is not done here as it depends on how the tech came.
**************************************************************************/
void found_new_tech(struct player *plr, int tech_found, char was_discovery,
		    char saving_bulbs)
{
  int philohack=0;
  int was_first=0;
  int saved_bulbs;
  int wonder;
  struct city *pcity;

  plr->got_tech=1;
  plr->research.researchpoints++;
  was_first = !game.global_advances[tech_found];
  if (was_first) {
    gamelog(GAMELOG_TECH,_("%s are first to learn %s"),
	    get_nation_name_plural(plr->nation),
	    advances[tech_found].name
	    );
    /* Alert the owners of any wonders that have been made obsolete */
    for (wonder = 0; wonder < B_LAST; wonder++)
      if (game.global_wonders[wonder] && is_wonder(wonder) &&
	  improvement_types[wonder].obsolete_by == tech_found &&
	  (pcity = find_city_by_id(game.global_wonders[wonder]))) {
	notify_player_ex(city_owner(pcity), -1, -1, E_WONDER_OBSOLETE,
	      _("Game: Discovery of %s OBSOLETES %s in %s!"), 
	      advances[tech_found].name, get_improvement_name(wonder),
	      pcity->name);
      }
  }
    
  if (tech_found==game.rtech.get_bonus_tech && was_first)
    philohack=1;

  set_invention(plr, tech_found, TECH_KNOWN);
  update_research(plr);
  remove_obsolete_buildings(plr);
  if (tech_flag(tech_found,TF_RAILROAD)) {
    upgrade_city_rails(plr, was_discovery);
  }

  if (tech_found==plr->ai.tech_goal)
    plr->ai.tech_goal=A_NONE;

  if (tech_found==plr->research.researching) {
    /* need to pick new tech to research */

    saved_bulbs=plr->research.researched;

    if (choose_goal_tech(plr)) {
      notify_player(plr, 
		    _("Game: Learned %s.  "
		      "Our scientists focus on %s, goal is %s."),
		    advances[tech_found].name, 
		    advances[plr->research.researching].name,
		    advances[plr->ai.tech_goal].name);
    } else {
      choose_random_tech(plr);
      if (plr->research.researching!=A_NONE && tech_found != A_NONE)
	notify_player(plr,
		     _("Game: Learned %s.  Scientists choose to research %s."),
		     advances[tech_found].name,
		     advances[plr->research.researching].name);
      else if (tech_found != A_NONE)
	notify_player(plr,
		      _("Game: Learned %s.  Scientists choose to research "
			"Future Tech. 1."),
		      advances[tech_found].name);
      else {
	plr->future_tech++;
	notify_player(plr,
		      _("Game: Learned Future Tech. %d.  "
			"Researching Future Tech. %d."),
		      plr->future_tech,(plr->future_tech)+1);
      }
    }
    if(saving_bulbs)
      plr->research.researched=saved_bulbs;
  }

  if (philohack) {
    notify_player(plr, _("Game: Great philosophers from all the world join "
			 "your civilization; you get an immediate advance."));
    tech_researched(plr);
  }
}

/**************************************************************************
Player has researched a new technology
**************************************************************************/
void tech_researched(struct player* plr)
{
  /* plr will be notified when new tech is chosen */

  if (plr->research.researching != A_NONE) {
    notify_embassies(plr, (struct player*)0,
		     _("Game: The %s have researched %s."), 
		     get_nation_name_plural(plr->nation),
		     advances[plr->research.researching].name);

    gamelog(GAMELOG_TECH,_("%s discover %s"),
	    get_nation_name_plural(plr->nation),
	    advances[plr->research.researching].name
	    );
  } else {
    notify_embassies(plr, (struct player*)0,
		     _("Game: The %s have researched Future Tech. %d."), 
		     get_nation_name_plural(plr->nation),
		     plr->future_tech);
  
    gamelog(GAMELOG_TECH,_("%s discover Future Tech %d"),
	    get_nation_name_plural(plr->nation),
	    plr->future_tech
	    );
  }

  /* do all the updates needed after finding new tech */
  found_new_tech(plr, plr->research.researching, 1, 0);
}

/**************************************************************************
Called from each city to update the research.
**************************************************************************/
int update_tech(struct player *plr, int bulbs)
{
  plr->research.researched+=bulbs;
  if (plr->research.researched < research_time(plr)) {
    return 0;
  } else {
    
    tech_researched( plr );
    
    return 1;
  }
}

/**************************************************************************
...
**************************************************************************/
int choose_goal_tech(struct player *plr)
{
  int sub_goal;

  if (plr->research.researched >0)
    plr->research.researched = 0;
  update_research(plr);
  if (plr->ai.control) {
    ai_next_tech_goal(plr); /* tech-AI has been changed */
    sub_goal = get_next_tech(plr, plr->ai.tech_goal); /* should be changed */
  } else sub_goal = get_next_tech(plr, plr->ai.tech_goal);
  if (!sub_goal) {
    if (plr->ai.control || plr->research.researchpoints == 1) {
      ai_next_tech_goal(plr);
      sub_goal = get_next_tech(plr, plr->ai.tech_goal);
    } else {
      plr->ai.tech_goal = A_NONE; /* clear goal when it is achieved */
    }
  }

  if (sub_goal) {
    plr->research.researching=sub_goal;
  }   
  return sub_goal;
}


/**************************************************************************
...
**************************************************************************/
void choose_random_tech(struct player *plr)
{
  int researchable=0;
  int i;
  int choosen;
  if (plr->research.researched >0)
    plr->research.researched = 0;
  update_research(plr);
  for (i=0;i<game.num_tech_types;i++)
    if (get_invention(plr, i)==TECH_REACHABLE) 
      researchable++;
  if (researchable==0) { 
    plr->research.researching=A_NONE;
    return;
  }
  choosen=myrand(researchable)+1;
  
  for (i=0;i<game.num_tech_types;i++)
    if (get_invention(plr, i)==TECH_REACHABLE) {
      choosen--;
      if (!choosen) break;
    }
  plr->research.researching=i;
}

/**************************************************************************
...
**************************************************************************/
void choose_tech(struct player *plr, int tech)
{
  if (plr->research.researching==tech)
    return;
  update_research(plr);
  if (get_invention(plr, tech)!=TECH_REACHABLE) { /* can't research this */
    return;
  }
  if (!plr->got_tech && plr->research.changed_from == -1) {
    plr->research.before_researched = plr->research.researched;
    plr->research.changed_from = plr->research.researching;
    /* subtract a penalty because we changed subject */
    if (plr->research.researched > 0)
      plr->research.researched -= ((plr->research.researched * game.techpenalty) / 100);
  } else if (tech == plr->research.changed_from) {
    plr->research.researched = plr->research.before_researched;
    plr->research.changed_from = -1;
  }
  plr->research.researching=tech;
}

void choose_tech_goal(struct player *plr, int tech)
{
  notify_player(plr, _("Game: Technology goal is %s."), advances[tech].name);
  plr->ai.tech_goal = tech;
}

/**************************************************************************
...
**************************************************************************/
void init_tech(struct player *plr, int tech)
{
  int i;
  for (i=0;i<game.num_tech_types;i++) 
    set_invention(plr, i, 0);
  set_invention(plr, A_NONE, TECH_KNOWN);

  plr->research.researchpoints=1;
  for (i=0;i<tech;i++) {
    choose_random_tech(plr); /* could be choose_goal_tech -- Syela */
    set_invention(plr, plr->research.researching, TECH_KNOWN);
  }
  choose_goal_tech(plr);
  update_research(plr);	
}

/**************************************************************************
...
**************************************************************************/
void handle_player_rates(struct player *pplayer, 
                         struct packet_player_request *preq)
{
  int maxrate;

  if (server_state!=RUN_GAME_STATE) {
    freelog(LOG_ERROR, "received player_rates packet from %s before start",
	    pplayer->name);
    notify_player(pplayer, _("Game: Cannot change rates before game start."));
    return;
  }
	
  if (preq->tax+preq->luxury+preq->science!=100)
    return;
  if (preq->tax<0 || preq->tax >100) return;
  if (preq->luxury<0 || preq->luxury > 100) return;
  if (preq->science<0 || preq->science >100) return;
  maxrate=get_government_max_rate (pplayer->government);
  if (preq->tax>maxrate || preq->luxury>maxrate || preq->science>maxrate) {
    char *rtype;
    if (preq->tax > maxrate)
      rtype = _("Tax");
    else if (preq->luxury > maxrate)
      rtype = _("Luxury");
    else
      rtype = _("Science");
    notify_player(pplayer, _("Game: %s rate exceeds the max rate for %s."),
                  rtype, get_government_name(pplayer->government));
  } else {
    pplayer->economic.tax=preq->tax;
    pplayer->economic.luxury=preq->luxury;
    pplayer->economic.science=preq->science;
    gamelog(GAMELOG_EVERYTHING, "RATE CHANGE: %s %i %i %i", 
	    get_nation_name_plural(pplayer->nation), preq->tax, 
	    preq->luxury, preq->science);
    conn_list_do_buffer(&pplayer->connections);
    send_player_info(pplayer, pplayer);
    global_city_refresh(pplayer);
    conn_list_do_unbuffer(&pplayer->connections);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_player_research(struct player *pplayer,
			    struct packet_player_request *preq)
{
  choose_tech(pplayer, preq->tech);
  send_player_info(pplayer, pplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_player_tech_goal(struct player *pplayer,
			    struct packet_player_request *preq)
{
  choose_tech_goal(pplayer, preq->tech);
  send_player_info(pplayer, pplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_player_worklist(struct player *pplayer,
			    struct packet_player_request *preq)
{
  if (preq->wl_idx < 0 || preq->wl_idx >= MAX_NUM_WORKLISTS) {
    freelog(LOG_ERROR, "Bad worklist index (%d) received from %s",
	    preq->wl_idx, pplayer->name);
    return;
  }

  copy_worklist(&pplayer->worklists[preq->wl_idx], &preq->worklist);
  send_player_info(pplayer, pplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_player_government(struct player *pplayer,
			     struct packet_player_request *preq)
{
  if( pplayer->government!=game.government_when_anarchy || 
     !can_change_to_government(pplayer, preq->government) 
    )
    return;

  if((pplayer->revolution<=5) && (pplayer->revolution>0))
    return;

  pplayer->government=preq->government;
  notify_player(pplayer, _("Game: %s now governs the %s as a %s."), 
		pplayer->name, 
  	        get_nation_name_plural(pplayer->nation),
		get_government_name(preq->government));  
  gamelog(GAMELOG_GOVERNMENT,"%s form a %s",
          get_nation_name_plural(pplayer->nation),
          get_government_name(preq->government));

  if (!pplayer->ai.control) {
    /* Keep luxuries if we have any.  Try to max out science. -GJW */
    pplayer->economic.science = MIN (100 - pplayer->economic.luxury,
				     get_government_max_rate (pplayer->government));
    pplayer->economic.tax = 100 - (pplayer->economic.luxury +
				   pplayer->economic.science);
  }

  check_player_government_rates(pplayer);

  global_city_refresh(pplayer);

  send_player_info(pplayer, pplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_player_revolution(struct player *pplayer)
{
  if ((pplayer->revolution<=5) &&
      (pplayer->revolution>0) &&
      ( pplayer->government==game.government_when_anarchy))
    return;
  pplayer->revolution=myrand(5)+1;
  pplayer->government=game.government_when_anarchy;
  notify_player(pplayer, _("Game: The %s have incited a revolt!"), 
		get_nation_name_plural(pplayer->nation));
  gamelog(GAMELOG_REVOLT,"The %s revolt!",
                get_nation_name_plural(pplayer->nation));

  check_player_government_rates(pplayer);

  global_city_refresh(pplayer);

  send_player_info(pplayer, pplayer);
  if (player_owns_active_govchange_wonder(pplayer))
    pplayer->revolution=1;
}

/**************************************************************************
The following checks that government rates are acceptable for the present
form of government. Has to be called when switching governments or when
toggling from AI to human.
**************************************************************************/
void check_player_government_rates(struct player *pplayer)
{
  struct player_economic old_econ = pplayer->economic;
  int changed = FALSE;
  player_limit_to_government_rates(pplayer);
  if (pplayer->economic.tax != old_econ.tax) {
    changed = TRUE;
    notify_player(pplayer,
		  _("Game: Tax rate exceeded the max rate for %s; adjusted."), 
		  get_government_name(pplayer->government));
  }
  if (pplayer->economic.science != old_econ.science) {
    changed = TRUE;
    notify_player(pplayer,
		  _("Game: Science rate exceeded the max rate for %s; adjusted."), 
		  get_government_name(pplayer->government));
  }
  if (pplayer->economic.luxury != old_econ.luxury) {
    changed = TRUE;
    notify_player(pplayer,
		  _("Game: Luxury rate exceeded the max rate for %s; adjusted."), 
		  get_government_name(pplayer->government));
  }

  if (changed) {
    gamelog(GAMELOG_EVERYTHING, "RATE CHANGE: %s %i %i %i",
	    get_nation_name_plural(pplayer->nation), pplayer->economic.tax,
	    pplayer->economic.luxury, pplayer->economic.science);
  }
}

/**************************************************************************
Handles a player cancelling a "pact" with another player.
**************************************************************************/
void handle_player_cancel_pact(struct player *pplayer, int other_player)
{
  enum diplstate_type old_type = pplayer->diplstates[other_player].type;
  enum diplstate_type new_type;
  struct player *pplayer2 = &game.players[other_player];
  int reppenalty = 0;
  int has_senate =
    government_has_flag(get_gov_pplayer(pplayer), G_HAS_SENATE);

  /* can't break a pact with yourself */
  if (pplayer == pplayer2)
    return;

  /* check what the new status will be, and what will happen to our
     reputation */
  switch(old_type) {
  case DS_NEUTRAL:
    new_type = DS_WAR;
    reppenalty = 0;
    break;
  case DS_CEASEFIRE:
    new_type = DS_NEUTRAL;
    reppenalty = GAME_MAX_REPUTATION/6;
    break;
  case DS_PEACE:
    new_type = DS_NEUTRAL;
    reppenalty = GAME_MAX_REPUTATION/5;
    break;
  case DS_ALLIANCE:
    new_type = DS_PEACE;
    reppenalty = GAME_MAX_REPUTATION/4;
    break;
  default:
    freelog(LOG_VERBOSE, "non-pact diplstate in handle_player_cancel_pact");
    return;
  }

  /* if there's a reason to cancel the pact, do it without penalty */
  if (pplayer->diplstates[pplayer2->player_no].has_reason_to_cancel) {
    pplayer->diplstates[pplayer2->player_no].has_reason_to_cancel = 0;
    if (has_senate)
      notify_player(pplayer, _("The senate passes your bill because of the "
			       "constant provocations of the %s."),
		    get_nation_name_plural(pplayer2->nation));
  }
  /* no reason to cancel, apply penalty (and maybe suffer a revolution) */
  /* FIXME: according to civII rules, republics and democracies
     have different chances of revolution; maybe we need to
     extend the govt rulesets to mimic this -- pt */
  else {
    pplayer->reputation = MAX(pplayer->reputation - reppenalty, 0);
    notify_player(pplayer, _("Game: Your reputation is now %s."),
		  reputation_text(pplayer->reputation));
    if (has_senate) {
      if (myrand(GAME_MAX_REPUTATION) > pplayer->reputation) {
	notify_player(pplayer, _("Game: The senate decides to dissolve "
				 "rather than support your actions any longer."));
	handle_player_revolution(pplayer);
      }
    }
  }

  /* do the change */
  pplayer->diplstates[pplayer2->player_no].type =
    pplayer2->diplstates[pplayer->player_no].type =
    new_type;
  pplayer->diplstates[pplayer2->player_no].turns_left =
    pplayer2->diplstates[pplayer->player_no].turns_left =
    16;

  send_player_info(pplayer, 0);
  send_player_info(pplayer2, 0);

  /* If the old state was alliance the players' units can share tiles
     illegally, and we need to call resolve_unit_stack() on all the players'
     units. We can't just iterate through the unitlist as it could get
     corrupted when resolve_unit_stack() deletes units. */
  if (old_type == DS_ALLIANCE) {
    int *resolve_list = NULL;
    int tot_units = unit_list_size(&(pplayer->units))
      + unit_list_size(&(pplayer2->units));
    int no_units = unit_list_size(&(pplayer->units));
    int i;

    if (tot_units > 0)
      resolve_list = fc_malloc(tot_units * 2 * sizeof(int));
    i = 0;
    unit_list_iterate(pplayer->units, punit) {
      resolve_list[i]   = punit->x;
      resolve_list[i+1] = punit->y;
      i += 2;
    } unit_list_iterate_end;

    no_units = unit_list_size(&(pplayer2->units));
    unit_list_iterate(pplayer2->units, punit) {
      resolve_list[i]   = punit->x;
      resolve_list[i+1] = punit->y;
      i += 2;
    } unit_list_iterate_end;

    if (resolve_list) {
      for (i = 0; i < tot_units * 2; i += 2)
	resolve_unit_stack(resolve_list[i], resolve_list[i+1], 1);
      free(resolve_list);
    }
  }

  notify_player(pplayer,
		_("Game: The diplomatic state between the %s and the %s is now %s."),
		get_nation_name_plural(pplayer->nation),
		get_nation_name_plural(pplayer2->nation),
		diplstate_text(new_type));
  notify_player_ex(pplayer2, -1, -1, E_NOEVENT,
		   _("Game: %s cancelled the diplomatic agreement!"),
		   pplayer->name);
  notify_player_ex(pplayer2, -1, -1, E_CANCEL_PACT,
		   _("Game: The diplomatic state between the %s and the %s is now %s."),
		   get_nation_name_plural(pplayer2->nation),
		   get_nation_name_plural(pplayer->nation),
		   diplstate_text(new_type));
}

/**************************************************************************
  This is the basis for following notify_conn* and notify_player* functions.
  Notify specified connections of an event of specified type (from events.h)
  and specified (x,y) coords associated with the event.  Coords will only
  apply if game has started and the conn's player knows that tile (or
  pconn->player==NULL && pconn->observer).  If coords are not required,
  caller should specify (x,y) = (-1,-1).  For generic event use E_NOEVENT.
  (But current clients do not use (x,y) data for E_NOEVENT events.)
**************************************************************************/
static void vnotify_conn_ex(struct conn_list *dest, int x, int y, int event,
			    const char *format, va_list vargs) 
{
  struct packet_generic_message genmsg;
  
  my_vsnprintf(genmsg.message, sizeof(genmsg.message), format, vargs);
  genmsg.event = event;

  conn_list_iterate(*dest, pconn) {
    if (y >= 0 && y < map.ysize && server_state >= RUN_GAME_STATE
	&& ((pconn->player==NULL && pconn->observer)
	    || (pconn->player!=NULL && map_get_known(x, y, pconn->player)))) {
      genmsg.x = x;
      genmsg.y = y;
    } else {
      genmsg.x = -1;
      genmsg.y = -1;
    }
    send_packet_generic_message(pconn, PACKET_CHAT_MSG, &genmsg);
  }
  conn_list_iterate_end;
}

/**************************************************************************
  See vnotify_conn_ex - this is just the "non-v" version, with varargs.
**************************************************************************/
void notify_conn_ex(struct conn_list *dest, int x, int y, int event,
		    const char *format, ...) 
{
  va_list args;
  va_start(args, format);
  vnotify_conn_ex(dest, x, y, event, format, args);
  va_end(args);
}

/**************************************************************************
  See vnotify_conn_ex - this is varargs, and cannot specify (x,y), event.
**************************************************************************/
void notify_conn(struct conn_list *dest, const char *format, ...) 
{
  va_list args;
  va_start(args, format);
  vnotify_conn_ex(dest, -1, -1, E_NOEVENT, format, args);
  va_end(args);
}

/**************************************************************************
  Similar to vnotify_conn_ex (see also), but takes player as "destination".
  If player != NULL, sends to all connections for that player.
  If player == NULL, sends to all established connections, to support
  old code, but this feature may go away - should use notify_conn with
  explicitly game.est_connections or game.game_connections as dest.
**************************************************************************/
void notify_player_ex(const struct player *pplayer, int x, int y,
		      int event, const char *format, ...) 
{
  struct conn_list *dest;
  va_list args;

  if (pplayer) {
    dest = (struct conn_list*)&pplayer->connections;
  } else {
    dest = &game.est_connections;
  }
  
  va_start(args, format);
  vnotify_conn_ex(dest, x, y, event, format, args);
  va_end(args);
}

/**************************************************************************
  Just like notify_player_ex, but no (x,y) nor event type.
**************************************************************************/
void notify_player(const struct player *pplayer, const char *format, ...) 
{
  struct conn_list *dest;
  va_list args;

  if (pplayer) {
    dest = (struct conn_list*)&pplayer->connections;
  } else {
    dest = &game.est_connections;
  }
  
  va_start(args, format);
  vnotify_conn_ex(dest, -1, -1, E_NOEVENT, format, args);
  va_end(args);
}

/**************************************************************************
  Send message to all players who have an embassy with pplayer,
  but excluding specified player.
**************************************************************************/
void notify_embassies(struct player *pplayer, struct player *exclude,
		      const char *format, ...) 
{
  int i;
  struct packet_generic_message genmsg;
  va_list args;
  va_start(args, format);
  my_vsnprintf(genmsg.message, sizeof(genmsg.message), format, args);
  va_end(args);
  genmsg.x = -1;
  genmsg.y = -1;
  genmsg.event = E_NOEVENT;
  for(i=0; i<game.nplayers; i++) {
    if(player_has_embassy(&game.players[i], pplayer)
       && exclude != &game.players[i]) {
      lsend_packet_generic_message(&game.players[i].connections,
				   PACKET_CHAT_MSG, &genmsg);
    }
  }
}

/**************************************************************************
  Send information about player src, or all players if src is NULL,
  to specified clients dest (dest may not be NULL).
**************************************************************************/
void send_player_info_c(struct player *src, struct conn_list *dest)
{
  int i, j;

  for(i=0; i<game.nplayers; i++) {    /* srcs  */
    if(!src || &game.players[i]==src) {
      struct packet_player_info info;
      info.playerno=i;
      sz_strlcpy(info.name, game.players[i].name);
      info.nation=game.players[i].nation;
      info.is_male=game.players[i].is_male;

      info.gold=game.players[i].economic.gold;
      info.tax=game.players[i].economic.tax;
      info.science=game.players[i].economic.science;
      info.luxury=game.players[i].economic.luxury;
      info.government=game.players[i].government;
      info.embassy=game.players[i].embassy;
      info.city_style=game.players[i].city_style;

      info.reputation=game.players[i].reputation;
      for(j=0; j<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; j++) {
	info.diplstates[j].type=game.players[i].diplstates[j].type;
	info.diplstates[j].turns_left=game.players[i].diplstates[j].turns_left;
	info.diplstates[j].has_reason_to_cancel =
	  game.players[i].diplstates[j].has_reason_to_cancel;
      }

      info.researched=game.players[i].research.researched;
      info.researchpoints=game.players[i].research.researchpoints;
      info.researching=game.players[i].research.researching;
      info.tech_goal=game.players[i].ai.tech_goal;
      for(j=0; j<game.num_tech_types; j++)
	info.inventions[j]=game.players[i].research.inventions[j]+'0';
      info.inventions[j]='\0';
      info.future_tech=game.players[i].future_tech;
      info.turn_done=game.players[i].turn_done;
      info.nturns_idle=game.players[i].nturns_idle;
      info.is_alive=game.players[i].is_alive;
      info.is_connected=game.players[i].is_connected;
      info.revolution=game.players[i].revolution;
      info.ai=game.players[i].ai.control;
      info.is_barbarian=game.players[i].ai.is_barbarian;

      /* Remove when "conn_info" capability removed: now sent in
       * conn_info packet.  For old clients (conditional in packets.c)
       * use player_addr_hack() and capstr of first connection:
       */
      sz_strlcpy(info.addr, player_addr_hack(&game.players[i]));
      if (conn_list_size(&game.players[i].connections)) {
	sz_strlcpy(info.capability,
		   conn_list_get(&game.players[i].connections, 0)->capability);
      } else {
	info.capability[0] = '\0';
      }
      /* Remove to here */
      
      for (j = 0; j < MAX_NUM_WORKLISTS; j++)
	copy_worklist(&info.worklists[j], &game.players[i].worklists[j]);
      
      lsend_packet_player_info(dest, &info);
    }
  }
}

/**************************************************************************
  Convenience form of send_player_info_c.
  Send information about player src, or all players if src is NULL,
  to specified players dest (that is, to dest->connections).
  As convenience to old code, dest may be NULL meaning send to
  game.est_connections.  (In general this may be overkill and may only
  want game.game_connections, but this is safest for now...?)
**************************************************************************/
void send_player_info(struct player *src, struct player *dest)
{
  send_player_info_c(src, (dest ? &dest->connections : &game.est_connections));
}

/**************************************************************************
  Fill in packet_conn_info from full connection struct.
**************************************************************************/
static void package_conn_info(struct connection *pconn,
			      struct packet_conn_info *packet)
{
  packet->id           = pconn->id;
  packet->used         = pconn->used;
  packet->established  = pconn->established;
  packet->player_num   = pconn->player ? pconn->player->player_no : -1;
  packet->observer     = pconn->observer;
  packet->access_level = pconn->access_level;

  sz_strlcpy(packet->name, pconn->name);
  sz_strlcpy(packet->addr, pconn->addr);
  sz_strlcpy(packet->capability, pconn->capability);
}

/**************************************************************************
  Handle both send_conn_info() and send_conn_info_removed(), depending
  on 'remove' arg.  Sends conn_info packets for 'src' to 'dest', turning
  off 'used' if 'remove' is specified.
**************************************************************************/
static void send_conn_info_arg(struct conn_list *src,
			       struct conn_list *dest, int remove)
{
  struct packet_conn_info packet;
  
  conn_list_iterate(*src, psrc) {
    package_conn_info(psrc, &packet);
    if (remove) {
      packet.used = 0;
    }
    lsend_packet_conn_info(dest, &packet);
  }
  conn_list_iterate_end;
}

/**************************************************************************
  Send conn_info packets to tell 'dest' connections all about
  'src' connections.
**************************************************************************/
void send_conn_info(struct conn_list *src, struct conn_list *dest)
{
  send_conn_info_arg(src, dest, 0);
}

/**************************************************************************
  Like send_conn_info(), but turn off the 'used' bits to tell clients
  to remove info about these connections instead of adding it.
**************************************************************************/
void send_conn_info_remove(struct conn_list *src, struct conn_list *dest)
{
  send_conn_info_arg(src, dest, 1);
}

/**************************************************************************
  Convenience function to return "reply" destination connection list
  for player: pplayer->current_conn if set, else pplayer->connections.
**************************************************************************/
struct conn_list *player_reply_dest(struct player *pplayer)
{
  return (pplayer->current_conn ?
	  &pplayer->current_conn->self :
	  &pplayer->connections);
}

/***************************************************************
...
***************************************************************/
void player_load(struct player *plr, int plrno, struct section_file *file)
{
  int i, j, x, y, nunits, ncities;
  char *p;
  char *savefile_options = " ";

  player_map_allocate(plr);

  if ((game.version == 10604 && section_file_lookup(file,"savefile.options"))
      || (game.version > 10604))
    savefile_options = secfile_lookup_str(file,"savefile.options");  
  /* else leave savefile_options empty */
  
  /* Note -- as of v1.6.4 you should use savefile_options (instead of
     game.version) to determine which variables you can expect to 
     find in a savegame file.  Or even better (IMO --dwp) is to use
     section_file_lookup(), secfile_lookup_int_default(),
     secfile_lookup_str_default(), etc.
  */

  plr->ai.is_barbarian = secfile_lookup_int_default(file, 0, "player%d.ai.is_barbarian",
                                                    plrno);
  if (is_barbarian(plr)) game.nbarbarians++;

  sz_strlcpy(plr->name, secfile_lookup_str(file, "player%d.name", plrno));
  sz_strlcpy(plr->username,
	     secfile_lookup_str_default(file, "", "player%d.username", plrno));
  plr->nation=secfile_lookup_int(file, "player%d.race", plrno);
  if (plr->ai.is_barbarian) {
    plr->nation=game.nation_count-1;
  }
  plr->government=secfile_lookup_int(file, "player%d.government", plrno);
  plr->embassy=secfile_lookup_int(file, "player%d.embassy", plrno);
  plr->city_style=secfile_lookup_int_default(file, get_nation_city_style(plr->nation),
                                             "player%d.city_style", plrno);
  for (j = 0; j < MAX_NUM_WORKLISTS; j++) {
    plr->worklists[j].is_valid = 
      secfile_lookup_int_default(file, 0, 
				 "player%d.worklist%d.is_valid", plrno, j);
    strcpy(plr->worklists[j].name, 
	   secfile_lookup_str_default(file, "",
				      "player%d.worklist%d.name", plrno, j));
    for (i = 0; i < MAX_LEN_WORKLIST; i++)
      plr->worklists[j].ids[i] = 
	secfile_lookup_int_default(file, WORKLIST_END,
				   "player%d.worklist%d.ids%d", plrno, j, i);
  }

  plr->nturns_idle=0;
  plr->is_male=secfile_lookup_int_default(file, 1, "player%d.is_male", plrno);
  plr->is_alive=secfile_lookup_int(file, "player%d.is_alive", plrno);
  plr->ai.control = secfile_lookup_int(file, "player%d.ai.control", plrno);
  plr->ai.tech_goal = secfile_lookup_int(file, "player%d.ai.tech_goal", plrno);
  plr->ai.handicap = 0;		/* set later */
  plr->ai.fuzzy = 0;		/* set later */
  plr->ai.expand = 100;		/* set later */
  plr->ai.skill_level =
    secfile_lookup_int_default(file, game.skill_level,
			       "player%d.ai.skill_level", plrno);
  if (plr->ai.control && plr->ai.skill_level==0) {
    plr->ai.skill_level = GAME_OLD_DEFAULT_SKILL_LEVEL;
  }

  plr->economic.gold=secfile_lookup_int(file, "player%d.gold", plrno);
  plr->economic.tax=secfile_lookup_int(file, "player%d.tax", plrno);
  plr->economic.science=secfile_lookup_int(file, "player%d.science", plrno);
  plr->economic.luxury=secfile_lookup_int(file, "player%d.luxury", plrno);

  plr->future_tech=secfile_lookup_int(file, "player%d.futuretech", plrno);

  plr->research.researched=secfile_lookup_int(file, 
					     "player%d.researched", plrno);
  plr->research.researchpoints=secfile_lookup_int(file, 
					     "player%d.researchpoints", plrno);
  plr->research.researching=secfile_lookup_int(file, 
					     "player%d.researching", plrno);

  p=secfile_lookup_str(file, "player%d.invs", plrno);
    
  plr->capital=secfile_lookup_int(file, "player%d.capital", plrno);
  plr->revolution=secfile_lookup_int_default(file, 0, "player%d.revolution",
                                             plrno);

  for(i=0; i<game.num_tech_types; i++)
    set_invention(plr, i, (p[i]=='1') ? TECH_KNOWN : TECH_UNKNOWN);

  plr->reputation=secfile_lookup_int_default(file, GAME_DEFAULT_REPUTATION,
					     "player%d.reputation", plrno);
  for(i=0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
    plr->diplstates[i].type = 
      secfile_lookup_int_default(file, DS_NEUTRAL,
				 "player%d.diplstate%d.type", plrno, i);
    plr->diplstates[i].turns_left = 
      secfile_lookup_int_default(file, 0,
				 "player%d.diplstate%d.turns_left", plrno, i);
    plr->diplstates[i].has_reason_to_cancel = 
      secfile_lookup_int_default(file, 0,
				 "player%d.diplstate%d.has_reason_to_cancel",
				 plrno, i);
  }
  
  if (has_capability("spacerace", savefile_options)) {
    struct player_spaceship *ship = &plr->spaceship;
    char prefix[32];
    int arrival_year;
    
    my_snprintf(prefix, sizeof(prefix), "player%d.spaceship", plrno);
    spaceship_init(ship);
    arrival_year = secfile_lookup_int(file, "%s.arrival_year", prefix);
    ship->structurals = secfile_lookup_int(file, "%s.structurals", prefix);
    ship->components = secfile_lookup_int(file, "%s.components", prefix);
    ship->modules = secfile_lookup_int(file, "%s.modules", prefix);
    ship->state = secfile_lookup_int(file, "%s.state", prefix);
    if (ship->state >= SSHIP_LAUNCHED) {
      ship->launch_year = (arrival_year - 15);
    }
    /* auto-assign to individual parts: */
    ship->habitation = (ship->modules + 2)/3;
    ship->life_support = (ship->modules + 1)/3;
    ship->solar_panels = ship->modules/3;
    ship->fuel = (ship->components + 1)/2;
    ship->propulsion = ship->components/2;
    for(i=0; i<ship->structurals; i++) {
      ship->structure[i] = 1;
    }
    spaceship_calc_derived(ship);
  }
  else if(has_capability("spacerace2", savefile_options)) {
    struct player_spaceship *ship = &plr->spaceship;
    char prefix[32];
    char *st;
    
    my_snprintf(prefix, sizeof(prefix), "player%d.spaceship", plrno);
    spaceship_init(ship);
    ship->state = secfile_lookup_int(file, "%s.state", prefix);

    if (ship->state != SSHIP_NONE) {
      ship->structurals = secfile_lookup_int(file, "%s.structurals", prefix);
      ship->components = secfile_lookup_int(file, "%s.components", prefix);
      ship->modules = secfile_lookup_int(file, "%s.modules", prefix);
      ship->fuel = secfile_lookup_int(file, "%s.fuel", prefix);
      ship->propulsion = secfile_lookup_int(file, "%s.propulsion", prefix);
      ship->habitation = secfile_lookup_int(file, "%s.habitation", prefix);
      ship->life_support = secfile_lookup_int(file, "%s.life_support", prefix);
      ship->solar_panels = secfile_lookup_int(file, "%s.solar_panels", prefix);
      st = secfile_lookup_str(file, "%s.structure", prefix);
      for(i=0; i<NUM_SS_STRUCTURALS && *st; i++, st++) {
	ship->structure[i] = ((*st) == '1');
      }
      if (ship->state >= SSHIP_LAUNCHED) {
	ship->launch_year = secfile_lookup_int(file, "%s.launch_year", prefix);
      }
      spaceship_calc_derived(ship);
    }
  }

  city_list_init(&plr->cities);
  ncities=secfile_lookup_int(file, "player%d.ncities", plrno);
  
  /* this should def. be placed in city.c later - PU */
  for(i=0; i<ncities; i++) { /* read the cities */
    struct city *pcity;
    
    pcity=fc_malloc(sizeof(struct city));
    pcity->ai.ai_role = AICITY_NONE;
    pcity->ai.trade_want = TRADE_WEIGHTING;
    memset(pcity->ai.building_want, 0, sizeof(pcity->ai.building_want));
    pcity->ai.workremain = 1; /* there's always work to be done! */
    pcity->ai.danger = -1; /* flag, may come in handy later */
    pcity->corruption = 0;
    pcity->shield_bonus = 100;
    pcity->tax_bonus = 100;
    pcity->science_bonus = 100;
 
    pcity->id=secfile_lookup_int(file, "player%d.c%d.id", plrno, i);
    alloc_id(pcity->id);
    idex_register_city(pcity);
    pcity->owner=plrno;
    pcity->x=secfile_lookup_int(file, "player%d.c%d.x", plrno, i);
    pcity->y=secfile_lookup_int(file, "player%d.c%d.y", plrno, i);
    
    sz_strlcpy(pcity->name,
	       secfile_lookup_str(file, "player%d.c%d.name", plrno, i));
    if (section_file_lookup(file, "player%d.c%d.original", plrno, i))
      pcity->original = secfile_lookup_int(file, "player%d.c%d.original", 
					   plrno,i);
    else 
      pcity->original = plrno;
    pcity->size=secfile_lookup_int(file, "player%d.c%d.size", plrno, i);

    pcity->steal=secfile_lookup_int(file, "player%d.c%d.steal", plrno, i);

    pcity->ppl_elvis=secfile_lookup_int(file, "player%d.c%d.nelvis", plrno, i);
    pcity->ppl_scientist=secfile_lookup_int(file, 
					  "player%d.c%d.nscientist", plrno, i);
    pcity->ppl_taxman=secfile_lookup_int(file, "player%d.c%d.ntaxman",
					 plrno, i);

    for(j=0; j<4; j++)
      pcity->trade[j]=secfile_lookup_int(file, "player%d.c%d.traderoute%d",
					 plrno, i, j);
    
    pcity->food_stock=secfile_lookup_int(file, "player%d.c%d.food_stock", 
						 plrno, i);
    pcity->shield_stock=secfile_lookup_int(file, "player%d.c%d.shield_stock", 
						   plrno, i);
    pcity->tile_trade=pcity->trade_prod=0;
    pcity->anarchy=secfile_lookup_int(file, "player%d.c%d.anarchy", plrno,i);
    pcity->rapture=secfile_lookup_int_default(file, 0, "player%d.c%d.rapture", plrno,i);
    pcity->was_happy=secfile_lookup_int(file, "player%d.c%d.was_happy", plrno,i);
    pcity->is_building_unit=
      secfile_lookup_int(file, 
			 "player%d.c%d.is_building_unit", plrno, i);
    pcity->currently_building=
      secfile_lookup_int(file, 
			 "player%d.c%d.currently_building", plrno, i);
    pcity->turn_last_built=
      secfile_lookup_int_default(file, GAME_START_YEAR,
				 "player%d.c%d.turn_last_built", plrno, i);
    pcity->turn_changed_target=
      secfile_lookup_int_default(file, GAME_START_YEAR,
				 "player%d.c%d.turn_changed_target", plrno, i);
    pcity->changed_from_id=
      secfile_lookup_int_default(file, pcity->currently_building,
				 "player%d.c%d.changed_from_id", plrno, i);
    pcity->changed_from_is_unit=
      secfile_lookup_int_default(file, pcity->is_building_unit,
				 "player%d.c%d.changed_from_is_unit", plrno, i);
    pcity->before_change_shields=
      secfile_lookup_int_default(file, pcity->shield_stock,
				 "player%d.c%d.before_change_shields", plrno, i);

    pcity->did_buy=secfile_lookup_int(file,
				      "player%d.c%d.did_buy", plrno,i);
    pcity->did_sell =
      secfile_lookup_int_default(file, 0, "player%d.c%d.did_sell", plrno,i);
    
    if (game.version >=10300) 
      pcity->airlift=secfile_lookup_int(file,
					"player%d.c%d.airlift", plrno,i);
    else
      pcity->airlift=0;

    pcity->city_options =
      secfile_lookup_int_default(file, CITYOPT_DEFAULT,
				 "player%d.c%d.options", plrno, i);
    
    /* adding the cities contribution to fog-of-war */
    map_unfog_pseudo_city_area(&game.players[plrno],pcity->x,pcity->y);

    unit_list_init(&pcity->units_supported);

    /* Initialize pcity->city_map[][], using set_worker_city() so that
       ptile->worked gets initialized correctly.  The pre-initialisation
       to C_TILE_EMPTY is necessary because set_worker_city() accesses
       the existing value to possibly adjust ptile->worked, so need to
       initialize a non-worked value so ptile->worked (possibly already
       set from neighbouring city) does not get unset for C_TILE_EMPTY
       or C_TILE_UNAVAILABLE here.  -- dwp
    */
    p=secfile_lookup_str(file, "player%d.c%d.workers", plrno, i);
    for(y=0; y<CITY_MAP_SIZE; y++) {
      for(x=0; x<CITY_MAP_SIZE; x++) {
	pcity->city_map[x][y] = C_TILE_EMPTY;
        if (*p=='0') {
	  set_worker_city(pcity, x, y, C_TILE_EMPTY);
	} else if (*p=='1') {
	  if (map_get_tile(pcity->x+x-2, pcity->y+y-2)->worked) {
	    /* oops, inconsistent savegame; minimal fix: */
	    freelog(LOG_VERBOSE, "Inconsistent worked for %s (%d,%d), "
		    "converting to elvis", pcity->name, x, y);
	    pcity->ppl_elvis++;
	    set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	  } else {
	    set_worker_city(pcity, x, y, C_TILE_WORKER);
	  }
	} else {
	  set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	}
        p++;
      }
    }

    p=secfile_lookup_str(file, "player%d.c%d.improvements", plrno, i);
    
    for(x=0; x<B_LAST; x++)
      pcity->improvements[x]=(*p++=='1') ? 1 : 0;

    pcity->worklist = create_worklist();
    for(j=0; j<MAX_LEN_WORKLIST; j++)
      pcity->worklist->ids[j]=
	secfile_lookup_int_default(file, WORKLIST_END,
				   "player%d.c%d.worklist%d", plrno, i, j);

    map_set_city(pcity->x, pcity->y, pcity);

    pcity->incite_revolt_cost = -1; /* flag value */
    
    city_list_insert_back(&plr->cities, pcity);
  }

  unit_list_init(&plr->units);
  nunits=secfile_lookup_int(file, "player%d.nunits", plrno);
  
  /* this should def. be placed in unit.c later - PU */
  for(i=0; i<nunits; i++) { /* read the units */
    struct unit *punit;
    struct city *pcity;
    
    punit=fc_malloc(sizeof(struct unit));
    punit->id=secfile_lookup_int(file, "player%d.u%d.id", plrno, i);
    alloc_id(punit->id);
    idex_register_unit(punit);
    punit->owner=plrno;
    punit->x=secfile_lookup_int(file, "player%d.u%d.x", plrno, i);
    punit->y=secfile_lookup_int(file, "player%d.u%d.y", plrno, i);

    punit->veteran=secfile_lookup_int(file, "player%d.u%d.veteran", plrno, i);
    punit->foul=secfile_lookup_int_default(file, 0, "player%d.u%d.foul",
					   plrno, i);
    punit->homecity=secfile_lookup_int(file, "player%d.u%d.homecity",
				       plrno, i);

    if((pcity=find_city_by_id(punit->homecity)))
      unit_list_insert(&pcity->units_supported, punit);
    else
      punit->homecity=0;
    
    punit->type=secfile_lookup_int(file, "player%d.u%d.type", plrno, i);

    punit->moves_left=secfile_lookup_int(file, "player%d.u%d.moves", plrno, i);
    punit->fuel= secfile_lookup_int(file, "player%d.u%d.fuel", plrno, i);
    set_unit_activity(punit, secfile_lookup_int(file, "player%d.u%d.activity",plrno, i));
/* need to do this to assign/deassign settlers correctly -- Syela */
/* was punit->activity=secfile_lookup_int(file, "player%d.u%d.activity",plrno, i); */
    punit->activity_count=secfile_lookup_int(file, 
					     "player%d.u%d.activity_count",
					     plrno, i);
    punit->activity_target=secfile_lookup_int_default(file, 0,
						"player%d.u%d.activity_target",
						plrno, i);

    punit->connecting=secfile_lookup_int_default(file, 0,
						"player%d.u%d.connecting",
						plrno, i);

    punit->goto_dest_x=secfile_lookup_int(file, 
					  "player%d.u%d.goto_x", plrno,i);
    punit->goto_dest_y=secfile_lookup_int(file, 
					  "player%d.u%d.goto_y", plrno,i);
    punit->ai.control=secfile_lookup_int(file, "player%d.u%d.ai", plrno,i);
    punit->ai.ai_role = AIUNIT_NONE;
    punit->ai.ferryboat = 0;
    punit->ai.passenger = 0;
    punit->ai.bodyguard = 0;
    punit->ai.charge = 0;
    punit->hp=secfile_lookup_int(file, "player%d.u%d.hp", plrno, i);
    punit->bribe_cost=-1;	/* flag value */
    
    punit->ord_map=secfile_lookup_int_default(file, 0, "player%d.u%d.ord_map",
					      plrno, i);
    punit->ord_city=secfile_lookup_int_default(file, 0, "player%d.u%d.ord_city",
					       plrno, i);
    punit->moved=secfile_lookup_int_default(file, 0, "player%d.u%d.moved",
					    plrno, i);
    punit->paradropped=secfile_lookup_int_default(file, 0, "player%d.u%d.paradropped",
                                                  plrno, i);
    punit->transported_by=secfile_lookup_int_default(file, -1, "player%d.u%d.transported_by",
                                                  plrno, i);
    /* Initialize upkeep values: these are hopefully initialized
       elsewhere before use (specifically, in city_support(); but
       fixme: check whether always correctly initialized?).
       Below is mainly for units which don't have homecity --
       otherwise these don't get initialized (and AI calculations
       etc may use junk values).
    */
    punit->unhappiness = 0;
    punit->upkeep      = 0;
    punit->upkeep_food = 0;
    punit->upkeep_gold = 0;
    
    /* allocate the unit's contribution to fog of war */
    unfog_area(&game.players[punit->owner],
	       punit->x,punit->y,get_unit_type(punit->type)->vision_range);

    unit_list_insert_back(&plr->units, punit);

    unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);
  }
}

/********************************************************************** 
The private map for fog of war
***********************************************************************/
void player_map_load(struct player *plr, int plrno, struct section_file *file)
{
  int x,y,i;

  if (!plr->is_alive)
    for (x=0; x<map.xsize; x++)
      for (y=0; y<map.ysize; y++)
	map_change_seen(x, y, plrno, +1);

  /* load map if:
     1) it from a fog of war build
     2) fog of war was on (otherwise the private map wasn't saved)
     3) is not from a "unit only" fog of war save file
  */
  if (secfile_lookup_int_default(file, -1, "game.fogofwar") != -1
      && game.fogofwar == 1
      && secfile_lookup_int_default(file, -1,"player%d.total_ncities", plrno) != -1) {
    /* get the terrain type */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_t%03d", plrno, y);
      for(x=0; x<map.xsize; x++) {
	char *pch;
	if(!(pch=strchr(terrain_chars, terline[x]))) {
	  freelog(LOG_FATAL, "unknown terrain type (map.t) in map "
		  "at position (%d,%d): %d '%c'", x, y, terline[x], terline[x]);
	  exit(1);
	}
	map_get_player_tile(x, y, plrno)->terrain=pch-terrain_chars;
      }
    }
    
    /* get lower 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_l%03d",plrno, y);
      
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];
	
	if(isxdigit(ch)) {
	  map_get_player_tile(x, y, plrno)->special=ch-(isdigit(ch) ? '0' : ('a'-10));
	} else if(ch!=' ') {
	  freelog(LOG_FATAL, "unknown special flag(lower) (map.l) in map "
		  "at position(%d,%d): %d '%c'", x, y, ch, ch);
	  exit(1);
	}
	else
	  map_get_player_tile(x, y, plrno)->special=S_NO_SPECIAL;
      }
    }
    
    /* get upper 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_u%03d", plrno, y);
      
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];
	
	if(isxdigit(ch)) {
	  map_get_player_tile(x, y, plrno)->special|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<4;
	} else if(ch!=' ') {
	  freelog(LOG_FATAL, "unknown special flag(upper) (map.u) in map "
		  "at position(%d,%d): %d '%c'", x, y, ch, ch);
	  exit(1);
	}
      }
    }
    
    /* get "next" 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str_default(file, NULL, "player%d.map_n%03d", plrno, y);
      
      if (terline) {
	for(x=0; x<map.xsize; x++) {
	  char ch=terline[x];
	  
	  if(isxdigit(ch)) {
	    map_get_player_tile(x, y, plrno)->special|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<8;
	  } else if(ch!=' ') {
	    freelog(LOG_FATAL, "unknown special flag(next) (map.n) in map "
		    "at position(%d,%d): %d '%c'", x, y, ch, ch);
	    exit(1);
	  }
	}
      }
    }
    
    
    /* get lower 4 bits of updated flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_ua%03d",plrno, y);
      
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];
	map_get_player_tile(x, y, plrno)->last_updated = ch-(isdigit(ch) ? '0' : ('a'-10));
      }
    }
    
    /* get upper 4 bits of updated flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_ub%03d", plrno, y);
      
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];
	map_get_player_tile(x, y, plrno)->last_updated |= (ch-(isdigit(ch) ? '0' : 'a'-10))<<4;
      }
    }
    
    /* get "next" 4 bits of updated flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_uc%03d", plrno, y);
      
      if (terline) {
	for(x=0; x<map.xsize; x++) {
	  char ch=terline[x];
	  map_get_player_tile(x, y, plrno)->last_updated |= (ch-(isdigit(ch) ? '0' : 'a'-10))<<8;
	}
      }
    }
    
    /* get "last" 4 bits of updated flags */
    for(y=0; y<map.ysize; y++) {
      char *terline=secfile_lookup_str(file, "player%d.map_ud%03d", plrno, y);
      
      if (terline) {
	for(x=0; x<map.xsize; x++) {
	  char ch=terline[x];
	  map_get_player_tile(x, y, plrno)->last_updated |= (ch-(isdigit(ch) ? '0' : 'a'-10))<<12;
	}
      }
    }
    
    {
      int j;
      struct dumb_city *pdcity;
      i = secfile_lookup_int(file, "player%d.total_ncities", plrno);
      for (j = 0; j < i; j++) {
	pdcity = fc_malloc(sizeof(struct dumb_city));
	pdcity->id = secfile_lookup_int(file, "player%d.dc%d.id", plrno, j);
	x = secfile_lookup_int(file, "player%d.dc%d.x", plrno, j);
	y = secfile_lookup_int(file, "player%d.dc%d.y", plrno, j);
	sz_strlcpy(pdcity->name, secfile_lookup_str(file, "player%d.dc%d.name", plrno, j));
	pdcity->size = secfile_lookup_int(file, "player%d.dc%d.size", plrno, j);
	pdcity->has_walls = secfile_lookup_int(file, "player%d.dc%d.has_walls", plrno, j);    
	pdcity->owner = secfile_lookup_int(file, "player%d.dc%d.owner", plrno, j);
	map_get_player_tile(x, y, plrno)->city = pdcity;
	alloc_id(pdcity->id);
      }
    }

    /* This shouldn't be neccesary if the savegame was consistent, but there
       is a bug in some pre-1.11 savegames. Anyway, it can't hurt */
    for (x=0; x<map.xsize; x++) {
      for (y=0; y<map.ysize; y++) {
	if (map_get_known_and_seen(x, y, plrno)) {
	  update_tile_knowledge(plr, x, y);
	  reality_check_city(plr, x, y);
	  if (map_get_city(x, y)) {
	    update_dumb_city(plr, map_get_city(x, y));
	  }
	}
      }
    }

  } else {
    /* We have an old savegame or fog of war was turned off; the players private
       knowledge is set to be what he could see without fog of war */
    for(y=0; y<map.ysize; y++)
      for(x=0; x<map.xsize; x++)
	if (map_get_known(x, y, plr)) {
	  struct city *pcity = map_get_city(x,y);
	  update_player_tile_last_seen(plr, x, y);
	  update_tile_knowledge(plr, x, y);
	  if (pcity)
	    update_dumb_city(plr, pcity);
	}
  }
}

/***************************************************************
...
***************************************************************/
void player_save(struct player *plr, int plrno, struct section_file *file)
{
  int i, j, x, y;
  char invs[A_LAST+1];
  struct player_spaceship *ship = &plr->spaceship;
  char *pbuf=fc_malloc(map.xsize+1);

  secfile_insert_str(file, plr->name, "player%d.name", plrno);
  secfile_insert_str(file, plr->username, "player%d.username", plrno);
  secfile_insert_int(file, plr->nation, "player%d.race", plrno);
  secfile_insert_int(file, plr->government, "player%d.government", plrno);
  secfile_insert_int(file, plr->embassy, "player%d.embassy", plrno);
   
  for (j = 0; j < MAX_NUM_WORKLISTS; j++) {
    secfile_insert_int(file, plr->worklists[j].is_valid, 
		       "player%d.worklist%d.is_valid", plrno, j);
    secfile_insert_str(file, plr->worklists[j].name, 
		       "player%d.worklist%d.name", plrno, j);
    for (i = 0; i < MAX_LEN_WORKLIST; i++)
      secfile_insert_int(file, plr->worklists[j].ids[i], 
			 "player%d.worklist%d.ids%d", plrno, j, i);
  }

  secfile_insert_int(file, plr->city_style, "player%d.city_style", plrno);
  secfile_insert_int(file, plr->is_male, "player%d.is_male", plrno);
  secfile_insert_int(file, plr->is_alive, "player%d.is_alive", plrno);
  secfile_insert_int(file, plr->ai.control, "player%d.ai.control", plrno);
  secfile_insert_int(file, plr->ai.tech_goal, "player%d.ai.tech_goal", plrno);
  secfile_insert_int(file, plr->ai.skill_level,
		     "player%d.ai.skill_level", plrno);
  secfile_insert_int(file, plr->ai.is_barbarian, "player%d.ai.is_barbarian", plrno);
  secfile_insert_int(file, plr->economic.gold, "player%d.gold", plrno);
  secfile_insert_int(file, plr->economic.tax, "player%d.tax", plrno);
  secfile_insert_int(file, plr->economic.science, "player%d.science", plrno);
  secfile_insert_int(file, plr->economic.luxury, "player%d.luxury", plrno);

  secfile_insert_int(file,plr->future_tech,"player%d.futuretech", plrno);

  secfile_insert_int(file, plr->research.researched, 
		     "player%d.researched", plrno);
  secfile_insert_int(file, plr->research.researchpoints,
		     "player%d.researchpoints", plrno);
  secfile_insert_int(file, plr->research.researching,
		     "player%d.researching", plrno);  

  secfile_insert_int(file, plr->capital, "player%d.capital", plrno);
  secfile_insert_int(file, plr->revolution, "player%d.revolution", plrno);

  for(i=0; i<game.num_tech_types; i++)
    invs[i]=(get_invention(plr, i)==TECH_KNOWN) ? '1' : '0';
  invs[i]='\0';
  secfile_insert_str(file, invs, "player%d.invs", plrno);

  secfile_insert_int(file, plr->reputation, "player%d.reputation", plrno);
  for (i = 0; i < MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
    secfile_insert_int(file, plr->diplstates[i].type,
		       "player%d.diplstate%d.type", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].turns_left,
		       "player%d.diplstate%d.turns_left", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].has_reason_to_cancel,
		       "player%d.diplstate%d.has_reason_to_cancel", plrno, i);
  }
  
  secfile_insert_int(file, ship->state, "player%d.spaceship.state", plrno);

  if (ship->state != SSHIP_NONE) {
    char prefix[32];
    char st[NUM_SS_STRUCTURALS+1];
    int i;
    
    my_snprintf(prefix, sizeof(prefix), "player%d.spaceship", plrno);

    secfile_insert_int(file, ship->structurals, "%s.structurals", prefix);
    secfile_insert_int(file, ship->components, "%s.components", prefix);
    secfile_insert_int(file, ship->modules, "%s.modules", prefix);
    secfile_insert_int(file, ship->fuel, "%s.fuel", prefix);
    secfile_insert_int(file, ship->propulsion, "%s.propulsion", prefix);
    secfile_insert_int(file, ship->habitation, "%s.habitation", prefix);
    secfile_insert_int(file, ship->life_support, "%s.life_support", prefix);
    secfile_insert_int(file, ship->solar_panels, "%s.solar_panels", prefix);
    
    for(i=0; i<NUM_SS_STRUCTURALS; i++) {
      st[i] = (ship->structure[i]) ? '1' : '0';
    }
    st[i] = '\0';
    secfile_insert_str(file, st, "%s.structure", prefix);
    if (ship->state >= SSHIP_LAUNCHED) {
      secfile_insert_int(file, ship->launch_year, "%s.launch_year", prefix);
    }
  }

  secfile_insert_int(file, unit_list_size(&plr->units), "player%d.nunits", 
		     plrno);
  secfile_insert_int(file, city_list_size(&plr->cities), "player%d.ncities", 
		     plrno);

  i = -1;
  unit_list_iterate(plr->units, punit) {
    i++;
    secfile_insert_int(file, punit->id, "player%d.u%d.id", plrno, i);
    secfile_insert_int(file, punit->x, "player%d.u%d.x", plrno, i);
    secfile_insert_int(file, punit->y, "player%d.u%d.y", plrno, i);
    secfile_insert_int(file, punit->veteran, "player%d.u%d.veteran", 
				plrno, i);
    secfile_insert_int(file, punit->foul, "player%d.u%d.foul", 
				plrno, i);
    secfile_insert_int(file, punit->hp, "player%d.u%d.hp", plrno, i);
    secfile_insert_int(file, punit->homecity, "player%d.u%d.homecity",
				plrno, i);
    secfile_insert_int(file, punit->type, "player%d.u%d.type",
				plrno, i);
    secfile_insert_int(file, punit->activity, "player%d.u%d.activity",
				plrno, i);
    secfile_insert_int(file, punit->activity_count, 
				"player%d.u%d.activity_count",
				plrno, i);
    secfile_insert_int(file, punit->activity_target, 
				"player%d.u%d.activity_target",
				plrno, i);
    secfile_insert_int(file, punit->connecting, 
				"player%d.u%d.connecting",
				plrno, i);
    secfile_insert_int(file, punit->moves_left, "player%d.u%d.moves",
		                plrno, i);
    secfile_insert_int(file, punit->fuel, "player%d.u%d.fuel",
		                plrno, i);

    secfile_insert_int(file, punit->goto_dest_x, "player%d.u%d.goto_x", plrno, i);
    secfile_insert_int(file, punit->goto_dest_y, "player%d.u%d.goto_y", plrno, i);
    secfile_insert_int(file, punit->ai.control, "player%d.u%d.ai", plrno, i);
    secfile_insert_int(file, punit->ord_map, "player%d.u%d.ord_map", plrno, i);
    secfile_insert_int(file, punit->ord_city, "player%d.u%d.ord_city", plrno, i);
    secfile_insert_int(file, punit->moved, "player%d.u%d.moved", plrno, i);
    secfile_insert_int(file, punit->paradropped, "player%d.u%d.paradropped", plrno, i);
    secfile_insert_int(file, punit->transported_by,
		       "player%d.u%d.transported_by", plrno, i);
  }
  unit_list_iterate_end;

  i = -1;
  city_list_iterate(plr->cities, pcity) {
    int j, x, y;
    char buf[512];

    i++;
    secfile_insert_int(file, pcity->id, "player%d.c%d.id", plrno, i);
    secfile_insert_int(file, pcity->x, "player%d.c%d.x", plrno, i);
    secfile_insert_int(file, pcity->y, "player%d.c%d.y", plrno, i);
    secfile_insert_str(file, pcity->name, "player%d.c%d.name", plrno, i);
    secfile_insert_int(file, pcity->original, "player%d.c%d.original", 
		       plrno, i);
    secfile_insert_int(file, pcity->size, "player%d.c%d.size", plrno, i);
    secfile_insert_int(file, pcity->steal, "player%d.c%d.steal", plrno, i);
    secfile_insert_int(file, pcity->ppl_elvis, "player%d.c%d.nelvis", plrno, i);
    secfile_insert_int(file, pcity->ppl_scientist, "player%d.c%d.nscientist", 
		       plrno, i);
    secfile_insert_int(file, pcity->ppl_taxman, "player%d.c%d.ntaxman", plrno, i);

    for(j=0; j<4; j++)
      secfile_insert_int(file, pcity->trade[j], "player%d.c%d.traderoute%d", 
			 plrno, i, j);
    
    secfile_insert_int(file, pcity->food_stock, "player%d.c%d.food_stock", 
		       plrno, i);
    secfile_insert_int(file, pcity->shield_stock, "player%d.c%d.shield_stock", 
		       plrno, i);
    secfile_insert_int(file, pcity->turn_last_built,
		       "player%d.c%d.turn_last_built", plrno, i);
    secfile_insert_int(file, pcity->turn_changed_target,
		       "player%d.c%d.turn_changed_target", plrno, i);
    secfile_insert_int(file, pcity->changed_from_id,
		       "player%d.c%d.changed_from_id", plrno, i);
    secfile_insert_int(file, pcity->changed_from_is_unit,
		       "player%d.c%d.changed_from_is_unit", plrno, i);
    secfile_insert_int(file, pcity->before_change_shields,
		       "player%d.c%d.before_change_shields", plrno, i);
    secfile_insert_int(file, pcity->anarchy, "player%d.c%d.anarchy", plrno,i);
    secfile_insert_int(file, pcity->rapture, "player%d.c%d.rapture", plrno,i);
    secfile_insert_int(file, pcity->was_happy, "player%d.c%d.was_happy", plrno,i);
    secfile_insert_int(file, pcity->did_buy, "player%d.c%d.did_buy", plrno,i);
    secfile_insert_int(file, pcity->did_sell, "player%d.c%d.did_sell", plrno,i);
    secfile_insert_int(file, pcity->airlift, "player%d.c%d.airlift", plrno,i);

    /* for auto_attack */
    secfile_insert_int(file, pcity->city_options,
		       "player%d.c%d.options", plrno, i);
    
    j=0;
    for(y=0; y<CITY_MAP_SIZE; y++) {
      for(x=0; x<CITY_MAP_SIZE; x++) {
	switch (get_worker_city(pcity, x, y)) {
	  case C_TILE_EMPTY:       buf[j++] = '0'; break;
	  case C_TILE_WORKER:      buf[j++] = '1'; break;
	  case C_TILE_UNAVAILABLE: buf[j++] = '2'; break;
	}
      }
    }
    buf[j]='\0';
    secfile_insert_str(file, buf, "player%d.c%d.workers", plrno, i);

    secfile_insert_int(file, pcity->is_building_unit, 
		       "player%d.c%d.is_building_unit", plrno, i);
    secfile_insert_int(file, pcity->currently_building, 
		       "player%d.c%d.currently_building", plrno, i);

    for(j=0; j<B_LAST; j++)
      buf[j]=(pcity->improvements[j]) ? '1' : '0';
    buf[j]='\0';
    secfile_insert_str(file, buf, "player%d.c%d.improvements", plrno, i);

    for(j=0; j<MAX_LEN_WORKLIST; j++)
      secfile_insert_int(file, pcity->worklist->ids[j], 
			 "player%d.c%d.worklist%d", plrno, i, j);

  }
  city_list_iterate_end;

  /********** Put the players private map **********/
 /* Otherwise the player can see all, and there's no reason to save the private map. */
  if (game.fogofwar) {
    /* put the terrain type */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=terrain_chars[map_get_player_tile(x, y, plrno)->terrain];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_t%03d", plrno, y);
    }
    
    /* put lower 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[map_get_player_tile(x, y, plrno)->special&0xf];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_l%03d",plrno,  y);
    }
    
    /* put upper 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->special&0xf0)>>4];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_u%03d", plrno,  y);
    }
    
    /* put "next" 4 bits of special flags */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->special&0xf00)>>8];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_n%03d", plrno, y);
    }
    
    /* put lower 4 bits of updated */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[map_get_player_tile(x, y, plrno)->last_updated&0xf];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_ua%03d",plrno,  y);
    }
    
    /* put upper 4 bits of updated */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->last_updated&0xf0)>>4];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_ub%03d", plrno,  y);
    }
    
    /* put "next" 4 bits of updated */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->last_updated&0xf00)>>8];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_uc%03d", plrno, y);
    }
    
    /* put "yet next" 4 bits of updated */
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++)
	pbuf[x]=dec2hex[(map_get_player_tile(x, y, plrno)->last_updated&0xf000)>>12];
      pbuf[x]='\0';
      
      secfile_insert_str(file, pbuf, "player%d.map_ud%03d", plrno, y);
    }
    
    free(pbuf);
    
    if (1) {
      struct dumb_city *pdcity;
      i = 0;
      
      for (x = 0; x < map.xsize; x++)
	for (y = 0; y < map.ysize; y++)
	  if ((pdcity = map_get_player_tile(x, y, plrno)->city)) {
	    secfile_insert_int(file, pdcity->id, "player%d.dc%d.id", plrno, i);
	    secfile_insert_int(file, x, "player%d.dc%d.x", plrno, i);
	    secfile_insert_int(file, y, "player%d.dc%d.y", plrno, i);
	    secfile_insert_str(file, pdcity->name, "player%d.dc%d.name", plrno, i);
	    secfile_insert_int(file, pdcity->size, "player%d.dc%d.size", plrno, i);
	    secfile_insert_int(file, pdcity->has_walls, "player%d.dc%d.has_walls", plrno, i);    
	    secfile_insert_int(file, pdcity->owner, "player%d.dc%d.owner", plrno, i);
	    i++;
	  }
    }
    secfile_insert_int(file, i, "player%d.total_ncities", plrno);
  }
}

/********************************************************************** 
The initmap option is used because we don't want to initialize the map
before the x and y sizes have been determined
***********************************************************************/
void server_player_init(struct player *pplayer, int initmap)
{
  if (initmap)
    player_map_allocate(pplayer);
  player_init(pplayer);
}

/********************************************************************** 
...
***********************************************************************/
void server_remove_player(struct player *pplayer)
{
  struct packet_generic_integer pack;

  /* Not allowed after a game has started */
  if (!(game.is_new_game && (server_state==PRE_GAME_STATE ||
			     server_state==SELECT_RACES_STATE))) {
    freelog(LOG_FATAL, "You can't remove players after the game has started!");
    abort();
  }

  freelog(LOG_NORMAL, _("Removing player %s."), pplayer->name);
  notify_player(pplayer, _("Game: You've been removed from the game!"));

  /* Note it is ok to remove the _current_ item in a list_iterate: */
  conn_list_iterate(pplayer->connections, pconn) {
    unassociate_player_connection(pplayer, pconn);
    close_connection(pconn);
  }
  conn_list_iterate_end;
  
  notify_conn(&game.est_connections,
	      _("Game: %s has been removed from the game."), pplayer->name);
  
  pack.value=pplayer->player_no;
  lsend_packet_generic_integer(&game.est_connections,
			       PACKET_REMOVE_PLAYER, &pack);

  game_remove_player(pplayer->player_no);
  game_renumber_players(pplayer->player_no);
}

/**************************************************************************
...
**************************************************************************/
void make_contact(int player1, int player2, int x, int y)
{
  struct player *pplayer1 = get_player(player1);
  struct player *pplayer2 = get_player(player2);

  if (pplayer1 == pplayer2
      || !pplayer1->is_alive || !pplayer2->is_alive
      || is_barbarian(pplayer1) || is_barbarian(pplayer2)
      || pplayer_get_diplstate(pplayer1, pplayer2)->type != DS_NO_CONTACT)
    return;

  /* FIXME: Always declairing war for the AI is a kludge until AI
     diplomacy is implemented. */
  pplayer1->diplstates[player2].type
    = pplayer2->diplstates[player1].type
    = pplayer1->ai.control || pplayer2->ai.control ? DS_WAR : DS_NEUTRAL;
  notify_player_ex(pplayer1, x, y,
		   E_FIRST_CONTACT,
		   _("Game: You have made contact with the %s, ruled by %s."),
		   get_nation_name_plural(pplayer2->nation), pplayer2->name);
  notify_player_ex(pplayer2, x, y,
		   E_FIRST_CONTACT,
		   _("Game: You have made contact with the %s, ruled by %s."),
		   get_nation_name_plural(pplayer1->nation), pplayer1->name);
  send_player_info(pplayer1, pplayer1);
  send_player_info(pplayer1, pplayer2);
  send_player_info(pplayer2, pplayer1);
  send_player_info(pplayer2, pplayer2);
}

/**************************************************************************
...
**************************************************************************/
void maybe_make_first_contact(int x, int y, int playerid)
{
  int x_itr, y_itr;

  square_iterate(x, y, 1, x_itr, y_itr) {
    struct tile *ptile = map_get_tile(x_itr, y_itr);
    struct city *pcity = ptile->city;
    if (pcity)
      make_contact(playerid, pcity->owner, x, y);
    unit_list_iterate(ptile->units, punit) {
      make_contact(playerid, punit->owner, x, y);
    } unit_list_iterate_end;
  } square_iterate_end;
}

/***************************************************************************
FIXME: This is a kluge to keep the AI working for the moment.
When the AI is taught to handle diplomacy, remove this and the call to it.
***************************************************************************/
void neutralize_ai_player(struct player *pplayer)
{
  int other_player;
  /* To make sure the rulesets are loaded we must do this.
     If the game is a new game all players (inclusive ai's) would be
     DS_NO_CONTACT, which is just as good as war.
  */
  if (game.is_new_game)
    return;

  for (other_player = 0; other_player < game.nplayers; other_player++) {
    struct player *pother = get_player(other_player);
    if (!pother->is_alive || pplayer == pother
	|| pplayer_get_diplstate(pplayer, pother)->type == DS_NO_CONTACT)
      continue;
    while (pplayers_non_attack(pplayer, pother) || pplayers_allied(pplayer, pother)) {
      handle_player_cancel_pact(pplayer, other_player);
    }
  }
}

/**************************************************************************
  Setup pconn as a client connected to pplayer:
  Updates pconn->player, pplayer->connections, pplayer->is_connected.
  Note "observer" connections do not count for is_connected.
**************************************************************************/
void associate_player_connection(struct player *pplayer,
				 struct connection *pconn)
{
  assert(pplayer && pconn);
  
  pconn->player = pplayer;
  conn_list_insert_back(&pplayer->connections, pconn);
  if (!pconn->observer) {
    pplayer->is_connected = 1;
  }
}

/**************************************************************************
  Remove pconn as a client connected to pplayer:
  Update pplayer->connections, pplayer->is_connected.
  Sets pconn->player to NULL (normally expect pconn->player==pplayer
  when function entered, but not checked).
**************************************************************************/
void unassociate_player_connection(struct player *pplayer,
				   struct connection *pconn)
{
  assert(pplayer && pconn);

  pconn->player = NULL;
  conn_list_unlink(&pplayer->connections, pconn);

  pplayer->is_connected = 0;
  conn_list_iterate(pplayer->connections, aconn) {
    if (!aconn->observer) {
      pplayer->is_connected = 1;
      break;
    }
  }
  conn_list_iterate_end;
}
