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

#include "capability.h"
#include "events.h"
#include "fcintl.h"
#include "government.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "tech.h"

#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplhand.h"
#include "gamelog.h"
#include "maphand.h"
#include "sernet.h"
#include "settlers.h"
#include "srv_main.h"
#include "unitfunc.h" 
#include "unittools.h"

#include "aihand.h"
#include "aitech.h"

#include "plrhand.h"

static void update_player_aliveness(struct player *pplayer);

static void package_player_common(struct player *plr,
                                  struct packet_player_info *packet);

static void package_player_info(struct player *plr,
                                struct packet_player_info *packet,
                                struct player *receiver,
                                enum plr_info_level min_info_level);

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
      players_iterate(pplayer2) {
	if (pplayer->gives_shared_vision & (1<<pplayer2->player_no))
	  remove_shared_vision(pplayer, pplayer2);
      } players_iterate_end;
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
    for (wonder = 0; wonder < game.num_impr_types; wonder++)
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
...
**************************************************************************/
static void refresh_players_cities_if_player2_near(struct player *pplayer,
						   struct player *pplayer2)
{
  city_list_iterate(pplayer->cities, pcity) {
    struct unit *refresh_because_of = NULL;
    map_city_radius_iterate(pcity->x, pcity->y, x, y) {
      unit_list_iterate(map_get_tile(x, y)->units, penemy) {
	if (unit_owner(penemy) == pplayer2)
	  refresh_because_of = penemy;
      } unit_list_iterate_end;
    } map_city_radius_iterate_end;

    if (refresh_because_of) {
      city_check_workers(pcity, 1);
      send_city_info(city_owner(pcity), pcity);
    }
  } city_list_iterate_end;
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

  /* Refresh all cities which have a unit of the other side within city range. */
  refresh_players_cities_if_player2_near(pplayer, pplayer2);
  refresh_players_cities_if_player2_near(pplayer2, pplayer);

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
  players_iterate(pplayer) {
    if(!src || pplayer==src) {
      struct packet_player_info info;

      package_player_common(pplayer, &info);

      conn_list_iterate(*dest, pconn) {

        /* Observer for all players. */
        if (!pconn->player && pconn->observer)
          package_player_info(pplayer, &info, 0, INFO_FULL);

        /* Client not yet attached to player. */
        else if (!pconn->player)
          package_player_info(pplayer, &info, 0, INFO_MINIMUM);

        /* Player clients (including one player observers) */
        else
          package_player_info(pplayer, &info, pconn->player, INFO_MINIMUM);

        send_packet_player_info(pconn, &info);
      } conn_list_iterate_end;
    }
  } players_iterate_end;
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
 Package player information that is always sent.
**************************************************************************/
static void package_player_common(struct player *plr,
                                  struct packet_player_info *packet)
{
  packet->playerno=plr->player_no;
  sz_strlcpy(packet->name, plr->name);
  packet->nation=plr->nation;
  packet->is_male=plr->is_male;
  packet->city_style=plr->city_style;

  packet->is_alive=plr->is_alive;
  packet->is_connected=plr->is_connected;
  packet->ai=plr->ai.control;
  packet->is_barbarian=plr->ai.is_barbarian;
  packet->reputation=plr->reputation;

  packet->turn_done=plr->turn_done;
  packet->nturns_idle=plr->nturns_idle;

  /* Remove when "conn_info" capability removed: now sent in
   * conn_info packet.  For old clients (conditional in packets.c)
   * use player_addr_hack() and capstr of first connection:
   */
  sz_strlcpy(packet->addr, player_addr_hack(plr));
  if (conn_list_size(&plr->connections)) {
    sz_strlcpy(packet->capability,
               conn_list_get(&plr->connections, 0)->capability);
  } else {
  packet->capability[0] = '\0';
  }
  /* Remove to here */
}

/**************************************************************************
 Package player info dependant on info_level.
**************************************************************************/
static void package_player_info(struct player *plr,
                                struct packet_player_info *packet,
                                struct player *receiver,
                                enum plr_info_level min_info_level)
{
  int i;
  enum plr_info_level info_level;

  info_level = player_info_level(plr, receiver);
  if (info_level < min_info_level)
    info_level = min_info_level;

  if (info_level >= INFO_MEETING) {
    packet->gold            = plr->economic.gold;
    for(i=0; i<game.num_tech_types; i++)
      packet->inventions[i] = plr->research.inventions[i]+'0';
    packet->inventions[i]   = '\0';
    packet->government      = plr->government;
  } else {
    packet->gold            = 0;
    for(i=0; i<game.num_tech_types; i++)
      packet->inventions[i] = '0';
    packet->inventions[i]   = '\0';

    /* Ideally, we should check whether receiver really sees any cities owned
       by player before this. */
    packet->inventions[city_styles[get_player_city_style(plr)].techreq] = '1';

    packet->government      = G_MAGIC;
  }

  if (info_level >= INFO_EMBASSY) {
    packet->tax             = plr->economic.tax;
    packet->science         = plr->economic.science;
    packet->luxury          = plr->economic.luxury;
    packet->researched      = plr->research.researched;
    packet->researchpoints  = plr->research.researchpoints;
    packet->researching     = plr->research.researching;
    packet->future_tech     = plr->future_tech;
    if (plr->revolution)
      packet->revolution    = 1;
    else
      packet->revolution    = 0;
  } else {
    packet->tax             = 0;
    packet->science         = 0;
    packet->luxury          = 0;
    packet->researched      = 0;
    packet->researchpoints  = 0;
    packet->researching     = A_NONE;
    packet->future_tech     = 0;
    packet->revolution      = 0;
  }

  if (info_level >= INFO_FULL) {
    packet->embassy=plr->embassy;
    packet->gives_shared_vision = plr->gives_shared_vision;
    for(i=0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
      packet->diplstates[i].type       = plr->diplstates[i].type;
      packet->diplstates[i].turns_left = plr->diplstates[i].turns_left;
      packet->diplstates[i].has_reason_to_cancel = plr->diplstates[i].has_reason_to_cancel;
    }
    packet->tech_goal       = plr->ai.tech_goal;
    for (i = 0; i < MAX_NUM_WORKLISTS; i++)
      copy_worklist(&packet->worklists[i], &plr->worklists[i]);
  } else {
    if (!receiver || !player_has_embassy(plr, receiver))
      packet->embassy  = 0;
    else
      packet->embassy  = 1 << receiver->player_no;
    if (!receiver || !(plr->gives_shared_vision & (1<<receiver->player_no)))
      packet->gives_shared_vision = 0;
    else
      packet->gives_shared_vision = 1 << receiver->player_no;
    for(i=0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
      packet->diplstates[i].type       = DS_NEUTRAL;
      packet->diplstates[i].turns_left = 0;
      packet->diplstates[i].has_reason_to_cancel = 0;
    }
    packet->tech_goal       = A_NONE;
    for (i = 0; i < MAX_NUM_WORKLISTS; i++)
      init_worklist(&packet->worklists[i]);
  }
}

/**************************************************************************
  ...
**************************************************************************/
enum plr_info_level player_info_level(struct player *plr,
                                      struct player *receiver)
{
  if (plr == receiver)
    return INFO_FULL;
  if (receiver && player_has_embassy(receiver, plr))
    return INFO_EMBASSY;
  if (receiver && find_treaty(plr, receiver))
    return INFO_MEETING;
  return INFO_MINIMUM;
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

/**************************************************************************
  To be used only by shuffle_players() and shuffled_player() below:
**************************************************************************/
static struct player *shuffled_plr[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
static int shuffled_nplayers = 0;

/**************************************************************************
  Shuffle or reshuffle the player order, storing in static variables above.
**************************************************************************/
void shuffle_players(void)
{
  int i, pos;
  struct player *tmp_plr;

  freelog(LOG_DEBUG, "shuffling %d players", game.nplayers);

  /* Initialize array in unshuffled order: */
  for(i=0; i<game.nplayers; i++) {
    shuffled_plr[i] = &game.players[i];
  }

  /* Now shuffle them: */
  for(i=0; i<game.nplayers-1; i++) {
    /* for each run: shuffled[ <i ] is already shuffled [Kero+dwp] */
    pos = i + myrand(game.nplayers-i);
    tmp_plr = shuffled_plr[i]; 
    shuffled_plr[i] = shuffled_plr[pos];
    shuffled_plr[pos] = tmp_plr;
  }

  /* Record how many players there were when shuffled: */
  shuffled_nplayers = game.nplayers;
}

/**************************************************************************
  Return i'th shuffled player.  If number of players has grown between
  re-shuffles, added players are given in unshuffled order at the end.
  Number of players should not have shrunk.
**************************************************************************/
struct player *shuffled_player(int i)
{
  assert(i>=0 && i<game.nplayers);
  
  if (shuffled_nplayers == 0) {
    freelog(LOG_ERROR, "shuffled_player() called before shuffled");
    return &game.players[i];
  }
  /* This shouldn't happen: */
  if (game.nplayers < shuffled_nplayers) {
    freelog(LOG_ERROR, "number of players shrunk between shuffles (%d < %d)",
	    game.nplayers, shuffled_nplayers);
    return &game.players[i];	/* ?? */
  }
  if (i < shuffled_nplayers) {
    return shuffled_plr[i];
  } else {
    return &game.players[i];
  }
}
