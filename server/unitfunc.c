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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "government.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "barbarian.h"
#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "gamelog.h"
#include "gotohand.h"
#include "mapgen.h"		/* assign_continent_numbers */
#include "maphand.h"
#include "plrhand.h"
#include "sernet.h"
#include "settlers.h"
#include "srv_main.h"
#include "unithand.h"
#include "unittools.h"

#include "aiunit.h"
#include "aitools.h"

#include "unitfunc.h"

extern struct move_cost_map warmap;

/****************************************************************************/

static void unit_restore_hitpoints(struct player *pplayer, struct unit *punit);
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit);
static void maybe_make_veteran(struct unit *punit);

static void wakeup_neighbor_sentries(struct player *pplayer,
				     int cent_x, int cent_y);
static void diplomat_charge_movement (struct unit *pdiplomat, int x, int y);
static int diplomat_success_vs_defender (struct unit *pdefender);
static int diplomat_infiltrate_city (struct player *pplayer, struct player *cplayer,
				     struct unit *pdiplomat, struct city *pcity);
static void diplomat_escape (struct player *pplayer, struct unit *pdiplomat,
			     struct city *pcity);
static void maybe_cause_incident(enum diplomat_actions action, struct player *offender,
				 struct unit *victim_unit, struct city *victim_city);
static void update_unit_activity(struct player *pplayer, struct unit *punit,
				 struct genlist_iterator *iter);

static int upgrade_would_strand(struct unit *punit, int upgrade_type);

static void sentry_transported_idle_units(struct unit *ptrans);

/******************************************************************************
  Poison a city's water supply.

  - Only a Spy can poison a city's water supply.
  - Only allowed against players you are at war with.

  - Check for infiltration success.  Our poisoner may not survive this.
  - Only cities of size greater than one may be poisoned.
  - If successful, reduces population by one point.

  - The poisoner may be captured and executed, or escape to its home town.
****************************************************************************/
void spy_poison(struct player *pplayer, struct unit *pdiplomat,
		struct city *pcity)
{
  struct player *cplayer;

  /* Fetch target city's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner(pcity);
  if (cplayer == NULL || !pplayers_at_war(pplayer, cplayer))
    return;

  freelog (LOG_DEBUG, "poison: unit: %d", pdiplomat->id);

  /* If not a Spy, can't poison. */
  if (!unit_flag (pdiplomat->type, F_SPY))
    return;

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_city (pplayer, cplayer, pdiplomat, pcity))
    return;

  freelog (LOG_DEBUG, "poison: infiltrated");

  /* If city is too small, can't poison. */
  if (pcity->size < 2) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: Your %s could not poison the water"
			" supply in %s."), 
		      unit_name (pdiplomat->type), pcity->name);
    freelog (LOG_DEBUG, "poison: target city too small");
    return;
  }

  freelog (LOG_DEBUG, "poison: succeeded");

  /* Poison people! */
  (pcity->size)--;
  city_auto_remove_worker (pcity);

  /* Notify everybody involved. */
  notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		    _("Game: Your %s poisoned the water supply of %s."),
		    unit_name (pdiplomat->type), pcity->name);
  notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		    _("Game: %s is suspected of poisoning the water supply"
		      " of %s."),
		    pplayer->name, pcity->name);

  /* Update clients. */
  city_refresh (pcity);  
  send_city_info (0, pcity);

  /* this may cause a diplomatic incident */
  maybe_cause_incident(SPY_POISON, pplayer, NULL, pcity);

  /* Now lets see if the spy survives. */
  diplomat_escape (pplayer, pdiplomat, pcity);
}

/******************************************************************************
  Investigate a city.

  - Either a Diplomat or Spy can investigate a city.
  - Allowed against all players.

  - It costs some minimal movement to investigate a city.

  - Diplomats die after investigation.
  - Spies always survive.  There is no risk.
****************************************************************************/
void diplomat_investigate(struct player *pplayer, struct unit *pdiplomat,
			  struct city *pcity)
{
  struct player *cplayer;
  int first_packet;
  struct packet_unit_info unit_packet;
  struct packet_city_info city_packet;

  /* Fetch target city's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if ((cplayer == pplayer) || (cplayer == NULL))
    return;

  freelog (LOG_DEBUG, "investigate: unit: %d", pdiplomat->id);

  /* Do It... */
  update_dumb_city(pplayer, pcity);
  /* Special case for a diplomat/spy investigating a city:
     The investigator needs to know the supported and present
     units of a city, whether or not they are fogged. So, we
     send a list of them all before sending the city info.
     As this is a special case we bypass send_unit_info. */
  first_packet = TRUE;
  unit_list_iterate(pcity->units_supported, punit) {
    package_unit(punit, &unit_packet, FALSE, FALSE,
		 UNIT_INFO_CITY_SUPPORTED, pcity->id, first_packet);
    lsend_packet_unit_info(&pplayer->connections, &unit_packet);
    first_packet = FALSE;
  } unit_list_iterate_end;
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
    package_unit(punit, &unit_packet, FALSE, FALSE,
		 UNIT_INFO_CITY_PRESENT, pcity->id, first_packet);
    lsend_packet_unit_info(&pplayer->connections, &unit_packet);
    first_packet = FALSE;
  } unit_list_iterate_end;
  /* Send city info to investigator's player.
     As this is a special case we bypass send_city_info. */
  package_city(pcity, &city_packet, TRUE);
  lsend_packet_city_info(&pplayer->connections, &city_packet);

  /* Charge a nominal amount of movement for this. */
  (pdiplomat->moves_left)--;
  if (pdiplomat->moves_left < 0) {
    pdiplomat->moves_left = 0;
  }

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_INVESTIGATE, pplayer, NULL, pcity);

  /* Spies always survive. Diplomats never do. */
  if (!unit_flag (pdiplomat->type, F_SPY)) {
    wipe_unit (pdiplomat);
  } else {
    send_unit_info (pplayer, pdiplomat);
  }
}

/******************************************************************************
  Get list of improvements from city (for purposes of sabotage).

  - Only a Spy can get a a city's sabotage list.

  - Always successful; returns list.

  - Spies always survive.

  Only send back to the originating connection, if there is one. (?)
****************************************************************************/
void spy_get_sabotage_list(struct player *pplayer, struct unit *pdiplomat,
			   struct city *pcity)
{
  struct packet_sabotage_list packet;
  int i;
  char *p;

  /* Send city improvements info to player. */
  p = packet.improvements;
  for(i=0; i<game.num_impr_types; i++)
    *p++=(pcity->improvements[i]) ? '1' : '0';
  *p='\0';
  packet.diplomat_id = pdiplomat->id;
  packet.city_id = pcity->id;
  lsend_packet_sabotage_list(player_reply_dest(pplayer), &packet);

  /* this may cause a diplomatic incident */
  maybe_cause_incident(SPY_GET_SABOTAGE_LIST, pplayer, NULL, pcity);
}

/******************************************************************************
  Establish an embassy.

  - Either a Diplomat or Spy can establish an embassy.

  - "Foul" ambassadors are detected and executed.
  - Barbarians always execute ambassadors.
  - Otherwise, the embassy is created.
  - It costs some minimal movement to establish an embassy.

  - Diplomats are consumed in creation of embassy.
  - Spies always survive.
****************************************************************************/
void diplomat_embassy(struct player *pplayer, struct unit *pdiplomat,
		      struct city *pcity)
{
  struct player *cplayer;

  /* Fetch target city's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if ((cplayer == pplayer) || (cplayer == NULL))
    return;

  freelog (LOG_DEBUG, "embassy: unit: %d", pdiplomat->id);

  /* Check for "foul" ambassador. */
  if (pdiplomat->foul) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_NOEVENT,
		      _("Game: Your %s was executed in %s on suspicion"
			" of spying.  The %s welcome future diplomatic"
			" efforts providing the Ambassador is reputable."),
		      unit_name (pdiplomat->type),
		      pcity->name, get_nation_name_plural (cplayer->nation));
    notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		      _("You executed a %s the %s had sent to establish"
			" an embassy in %s"),
		      unit_name (pdiplomat->type),
		      get_nation_name_plural (pplayer->nation),
		      pcity->name);
    wipe_unit (pdiplomat);
    return;
  }

  /* Check for Barbarian response. */
  if (is_barbarian (cplayer)) {
    notify_player_ex( pplayer, pcity->x, pcity->y, E_NOEVENT,
		      _("Game: Your %s was executed in %s by primitive %s."),
		      unit_name (pdiplomat->type),
		      pcity->name, get_nation_name_plural (cplayer->nation));
    wipe_unit (pdiplomat);
    return;
  }

  freelog (LOG_DEBUG, "embassy: succeeded");

  /* Establish the embassy. */
  pplayer->embassy |= (1 << pcity->owner);
  send_player_info(pplayer, pplayer);
  send_player_info(pplayer, cplayer);    /* update player dialog with embassy */
  send_player_info(cplayer, pplayer);    /* INFO_EMBASSY level info */

  /* Notify everybody involved. */
  notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		    _("Game: You have established an embassy in %s."),
		    pcity->name);
  notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		    _("Game: The %s have established an embassy in %s."),
		    get_nation_name_plural (pplayer->nation), pcity->name);
  gamelog (GAMELOG_EMBASSY,"%s establish an embassy in %s (%s) (%i,%i)\n",
	   get_nation_name_plural (pplayer->nation),
	   pcity->name,
	   get_nation_name (cplayer->nation),
	   pcity->x, pcity->y);

  /* Charge a nominal amount of movement for this. */
  (pdiplomat->moves_left)--;
  if (pdiplomat->moves_left < 0) {
    pdiplomat->moves_left = 0;
  }

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_EMBASSY, pplayer, NULL, pcity);

  /* Spies always survive. Diplomats never do. */
  if (!unit_flag (pdiplomat->type, F_SPY)) {
    wipe_unit (pdiplomat);
  } else {
    send_unit_info (pplayer, pdiplomat);
  }
}

/******************************************************************************
  Sabotage an enemy unit.

  - Only a Spy can sabotage an enemy unit.
  - Only allowed against players you are at war with.

  - Can't sabotage a unit if:
    - It has only one hit point left.
    - It's not the only unit on the square
      (this is handled outside this function).
  - If successful, reduces hit points by half of those remaining.

  - The saboteur may be captured and executed, or escape to its home town.
****************************************************************************/
void spy_sabotage_unit(struct player *pplayer, struct unit *pdiplomat,
		       struct unit *pvictim)
{
  struct player *uplayer;

  /* Fetch target unit's player.  Sanity checks. */
  if (!pvictim)
    return;
  uplayer = get_player (pvictim->owner);
  if (uplayer == NULL || pplayers_allied(pplayer, uplayer))
    return;

  freelog (LOG_DEBUG, "sabotage-unit: unit: %d", pdiplomat->id);

  /* If not a Spy, can't sabotage unit. */
  if (!unit_flag (pdiplomat->type, F_SPY))
    return;

  /* If unit has too few hp, can't sabotage. */
  if (pvictim->hp < 2) {
    notify_player_ex (pplayer, pvictim->x, pvictim->y, E_MY_DIPLOMAT,
		      _("Game: Your %s could not sabotage %s's %s."),
		      unit_name (pdiplomat->type),
		      get_player (pvictim->owner)->name,
		      unit_name (pvictim->type));
    freelog (LOG_DEBUG, "sabotage-unit: unit has too few hit points");
    return;
  }

  freelog (LOG_DEBUG, "sabotage-unit: succeeded");

  /* Sabotage the unit by removing half its remaining hit points. */
  pvictim->hp /= 2;
  send_unit_info (0, pvictim);

  /* Notify everybody involved. */
  notify_player_ex (pplayer, pvictim->x, pvictim->y, E_MY_DIPLOMAT,
		    _("Game: Your %s succeeded in sabotaging %s's %s."),
		    unit_name (pdiplomat->type),
		    get_player (pvictim->owner)->name,
		    unit_name (pvictim->type));
  notify_player_ex (uplayer, pvictim->x, pvictim->y, E_DIPLOMATED,
		    _("Game: Your %s was sabotaged by %s!"),
		    unit_name (pvictim->type), pplayer->name);

  /* this may cause a diplomatic incident */
  maybe_cause_incident(SPY_SABOTAGE_UNIT, pplayer, pvictim, NULL);

  /* Now lets see if the spy survives. */
  diplomat_escape (pplayer, pdiplomat, NULL);
}

/******************************************************************************
  Bribe an enemy unit.

  - Either a Diplomat or Spy can bribe an other players unit.
  
  - Can't bribe a unit if:
    - Owner runs an unbribable government (e.g., democracy).
    - Player doesn't have enough gold.
    - It's not the only unit on the square
      (this is handled outside this function).
    - You are allied with the unit owner.
  - Otherwise, the unit will be bribed.

  - A successful briber will try to move onto the victim's square.
****************************************************************************/
void diplomat_bribe(struct player *pplayer, struct unit *pdiplomat,
		    struct unit *pvictim)
{
  struct player *uplayer;
  int diplomat_id, victim_x, victim_y;

  /* Fetch target unit's player.  Sanity checks. */
  if (!pvictim)
    return;
  uplayer = get_player(pvictim->owner);
  /* We might make it allowable in peace with a liss of reputaion */
  if (uplayer == NULL || pplayers_allied(pplayer, uplayer))
    return;

  freelog (LOG_DEBUG, "bribe-unit: unit: %d", pdiplomat->id);

  /* Update bribe cost. */
  if (pvictim->bribe_cost == -1) {
    freelog (LOG_ERROR, "Bribe cost -1 in diplomat_bribe by %s",
	     pplayer->name);
    pvictim->bribe_cost = unit_bribe_cost (pvictim);
  }

  /* Check for unit from a bribable government. */
  if (government_has_flag (get_gov_iplayer (pvictim->owner), G_UNBRIBABLE)) {
    notify_player_ex (pplayer, pdiplomat->x, pdiplomat->y, E_NOEVENT,
		      _("Game: You can't bribe a unit from this nation."));
    freelog (LOG_DEBUG, "bribe-unit: unit's government is unbribable");
    return;
  }

  /* If player doesn't have enough gold, can't bribe. */
  if (pplayer->economic.gold < pvictim->bribe_cost) {
    notify_player_ex (pplayer, pdiplomat->x, pdiplomat->y, E_MY_DIPLOMAT,
		      _("Game: You don't have enough gold to"
			" bribe %s's %s."),
		      get_player (pvictim->owner)->name,
		      unit_name (pvictim->type));
    freelog (LOG_DEBUG, "bribe-unit: not enough gold");
    return;
  }

  freelog (LOG_DEBUG, "bribe-unit: succeeded");

  /* Convert the unit to your cause. Fog is lifted in the create algorithm. */
  create_unit_full (pplayer, pvictim->x, pvictim->y,
		    pvictim->type, pvictim->veteran, pdiplomat->homecity,
		    pvictim->moves_left, pvictim->hp);

  /* Notify everybody involved. */
  notify_player_ex (pplayer, pvictim->x, pvictim->y, E_MY_DIPLOMAT,
		    _("Game: Your %s succeeded in bribing %s's %s."),
		    unit_name (pdiplomat->type),
		    get_player (pvictim->owner)->name,
		    unit_name (pvictim->type));
  notify_player_ex (uplayer, pvictim->x, pvictim->y, E_DIPLOMATED,
		    _("Game: Your %s was bribed by %s."),
		    unit_name (pvictim->type), pplayer->name);

  /* This costs! */
  pplayer->economic.gold -= pvictim->bribe_cost;

  /* This may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_BRIBE, pplayer, pvictim, NULL);

  /* Be sure to wipe the converted unit! */
  victim_x = pvictim->x;
  victim_y = pvictim->y;
  wipe_unit (pvictim);

  /* Now, try to move the briber onto the victim's square. */
  diplomat_id = pdiplomat->id;
  if (!handle_unit_move_request(pdiplomat, victim_x, victim_y, FALSE, FALSE)) {
    pdiplomat->moves_left = 0;
  }
  if (player_find_unit_by_id(pplayer, diplomat_id)) {
    send_unit_info (pplayer, pdiplomat);
  }

  /* Update clients. */
  send_player_info (pplayer, 0);
}

/****************************************************************************
  Try to steal a technology from an enemy city.
  If "technology" is game.num_tech_types, steal a random technology.
  Otherwise, steal the technology whose ID is "technology".
  (Note: Only Spies can select what to steal.)

  - Either a Diplomat or Spy can steal a technology.

  - Check for infiltration success.  Our thief may not survive this.
  - Check for basic success.  Again, our thief may not survive this.
  - If a technology has already been stolen from this city at any time:
    - Diplomats will be caught and executed.
    - Spies will have an increasing chance of being caught and executed.
  - Determine target, given arguments and constraints.
  - Steal technology!

  - The thief may be captured and executed, or escape to its home town.

  FIXME: It should give a loss of reputaion to steal from a player you are
  not at war with
****************************************************************************/
void diplomat_get_tech(struct player *pplayer, struct unit *pdiplomat, 
		       struct city  *pcity, int technology)
{
  struct player *cplayer;
  int index, count, which, target;

  /* Fetch target civilization's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if ((cplayer == pplayer) || (cplayer == NULL))
    return;

  freelog (LOG_DEBUG, "steal-tech: unit: %d", pdiplomat->id);

  /* If not a Spy, do something random. */
  if (!unit_flag (pdiplomat->type, F_SPY))
    technology = game.num_tech_types;

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_city (pplayer, cplayer, pdiplomat, pcity))
    return;

  freelog (LOG_DEBUG, "steal-tech: infiltrated");

  /* Check if the Diplomat/Spy succeeds with his/her task. */
  /* (Twice as difficult if target is specified.) */
  /* (If already stolen from, impossible for Diplomats and harder for Spies.) */
  if ((pcity->steal > 0) && (!unit_flag (pdiplomat->type, F_SPY))) {
    /* Already stolen from: Diplomat always fails! */
    count = 1;
    freelog (LOG_DEBUG, "steal-tech: difficulty: impossible");
  } else {
    /* Determine difficulty. */
    count = 1;
    if (technology < game.num_tech_types) count++;
    count += pcity->steal;
    freelog (LOG_DEBUG, "steal-tech: difficulty: %d", count);
    /* Determine success or failure. */
    while (count > 0) {
      if (myrand (100) >= game.diplchance) {
	break;
      }
      count--;
    }
  }
  if (count > 0) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: Your %s was caught in the attempt of"
			" stealing technology from %s."),
		      unit_name (pdiplomat->type), pcity->name);
    notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		      _("Game: %s's %s failed to steal technology from %s."),
		      pplayer->name, unit_name (pdiplomat->type), pcity->name);
    wipe_unit (pdiplomat);
    return;
  }

  freelog (LOG_DEBUG, "steal-tech: succeeded");

  /* Examine the civilization for technologies to steal. */
  count = 0;
  for (index = A_FIRST; index < game.num_tech_types; index++) {
    if ((get_invention (pplayer, index) != TECH_KNOWN) &&
	(get_invention (cplayer, index) == TECH_KNOWN)) {
      count++;
    }
  }

  freelog (LOG_DEBUG, "steal-tech: count of technologies: %d", count);

  /* Determine the target (-1 is future tech). */
  if (!count) {
    /*
     * Either only future-tech or nothing to steal:
     * If nothing to steal, say so, deduct movement cost and return.
     */
    if (cplayer->future_tech > pplayer->future_tech) {
      target = -1;
      freelog (LOG_DEBUG, "steal-tech: targeted future-tech: %d", target);
    } else {
      notify_player_ex (pplayer, pcity->x, pcity->y, E_NOEVENT,
			_("Game: No new technology found in %s."),
			pcity->name);
      diplomat_charge_movement (pdiplomat, pcity->x, pcity->y);
      send_unit_info (pplayer, pdiplomat);
      freelog (LOG_DEBUG, "steal-tech: nothing to steal");
      return;
    }
  } else if (technology >= game.num_tech_types) {
    /* Pick random technology to steal. */
    target = -1;
    which = myrand (count);
    for (index = A_FIRST; index < game.num_tech_types; index++) {
      if ((get_invention (pplayer, index) != TECH_KNOWN) &&
	  (get_invention (cplayer, index) == TECH_KNOWN)) {
	if (which > 0) {
	  which--;
	} else {
	  target = index;
	  break;
	}
      }
    }
    freelog (LOG_DEBUG, "steal-tech: random: targeted technology: %d (%s)",
	     target, advances[target].name);
  } else {
    /*
     * Told which technology to steal:
     * If not available, say so, deduct movement cost and return.
     */
    if ((get_invention (pplayer, technology) != TECH_KNOWN) &&
	(get_invention (cplayer, technology) == TECH_KNOWN)) {
	target = technology;
	freelog (LOG_DEBUG, "steal-tech: specified target technology: %d (%s)",
	       target, advances[target].name);
    } else {
      notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			_("Game: Your %s could not find the %s technology"
			  " to steal in %s."), 
			unit_name (pdiplomat->type),
			advances[technology].name, pcity->name);
      diplomat_charge_movement (pdiplomat, pcity->x, pcity->y);
      send_unit_info (pplayer, pdiplomat);
      freelog (LOG_DEBUG, "steal-tech: target technology not found: %d (%s)",
	       technology, advances[technology].name);
      return;
    }
  }

  /* Now, the fun stuff!  Steal some technology! */
  if (target < 0) {
    /* Steal a future-tech. */

    /* Do it. */
    (pplayer->future_tech)++;
    (pplayer->research.researchpoints)++;
    /* Report it. */
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: Your %s stole Future Tech. %d from %s."),
		      unit_name (pdiplomat->type),
		      pplayer->future_tech, cplayer->name);
    notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		      _("Game: Future Tech. %d stolen by %s %s from %s."),
		      pplayer->future_tech, get_nation_name (pplayer->nation),
		      unit_name (pdiplomat->type), pcity->name);
    gamelog (GAMELOG_TECH, "%s steals Future Tech. %d from the %s",
	     get_nation_name_plural (pplayer->nation),
	     pplayer->future_tech,
	     get_nation_name_plural (cplayer->nation));
    freelog (LOG_DEBUG, "steal-tech: stole future-tech %d",
	     pplayer->future_tech);
  } else {
    /* Steal a technology. */

    /* Do it. */
    do_conquer_cost (pplayer);
    found_new_tech (pplayer, target, 0, 1);
    /* Report it. */
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: Your %s stole %s from %s."),
		      unit_name (pdiplomat->type),
		      advances[target].name, cplayer->name);
    notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		      _("Game: %s's %s stole %s from %s."), 
		      pplayer->name, unit_name (pdiplomat->type),
		      advances[target].name, pcity->name);
    notify_embassies (pplayer, cplayer,
		      _("Game: The %s have stolen %s from the %s."),
		      get_nation_name_plural (pplayer->nation),
		      advances[target].name,
		      get_nation_name_plural (cplayer->nation));
    gamelog (GAMELOG_TECH, "%s steals %s from the %s",
	     get_nation_name_plural (pplayer->nation),
	     advances[target].name,
	     get_nation_name_plural (cplayer->nation));
    freelog (LOG_DEBUG, "steal-tech: stole %s",
	     advances[target].name);
  }

  /* Record the theft. */
  (pcity->steal)++;

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_STEAL, pplayer, NULL, pcity);

  /* Check if a spy survives her mission. Diplomats never do. */
  diplomat_escape (pplayer, pdiplomat, pcity);
}

/**************************************************************************
  Incite a city to disaffect.

  - Either a Diplomat or Spy can incite a city to disaffect.

  - Can't incite a city to disaffect if:
    - Owner runs an unbribable government (e.g., democracy).
    - Player doesn't have enough gold.
    - You are allied with the city owner.
  - Check for infiltration success.  Our provocateur may not survive this.
  - Check for basic success.  Again, our provocateur may not survive this.
  - Otherwise, the city will disaffect:
    - Player gets the city.
    - Player gets certain of the city's present/supported units.
    - Player gets a technology advance, if any were unknown.

  - The provocateur may be captured and executed, or escape to its home town.
**************************************************************************/
void diplomat_incite(struct player *pplayer, struct unit *pdiplomat,
		     struct city *pcity)
{
  struct player *cplayer;
  struct city *capital;
  int revolt_cost;

  /* Fetch target civilization's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if (cplayer == NULL || pplayers_allied(cplayer, pplayer))
    return;

  freelog (LOG_DEBUG, "incite: unit: %d", pdiplomat->id);

  /* Check for city from a bribable government. */
  if (government_has_flag (get_gov_pcity (pcity), G_UNBRIBABLE)) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_NOEVENT,
		      _("Game: You can't subvert a city from this nation."));
    freelog (LOG_DEBUG, "incite: city's government is unbribable");
    return;
  }

  /* Check for city being the capital. */
  capital = find_palace (city_owner (pcity));
  if (pcity == capital) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_NOEVENT,
		      _("Game: You can't subvert the capital of a nation."));
    freelog (LOG_DEBUG, "incite: city is the capital");
    return;
  }

  /* Update incite cost. */
  if (pcity->incite_revolt_cost == -1) {
    freelog (LOG_ERROR, "Incite cost -1 in diplomat_incite by %s for %s",
	     pplayer->name, pcity->name);
    city_incite_cost (pcity);
  }
  revolt_cost = pcity->incite_revolt_cost;

  /* Special deal for former owners! */
  if (pplayer->player_no == pcity->original) revolt_cost /= 2;

  /* If player doesn't have enough gold, can't incite a revolt. */
  if (pplayer->economic.gold < revolt_cost) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: You don't have enough gold to"
			" subvert %s."),
		      pcity->name);
    freelog (LOG_DEBUG, "incite: not enough gold");
    return;
  }

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_city (pplayer, cplayer, pdiplomat, pcity))
    return;

  freelog (LOG_DEBUG, "incite: infiltrated");

  /* Check if the Diplomat/Spy succeeds with his/her task. */
  if (myrand (100) >= game.diplchance) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: Your %s was caught in the attempt"
			" of inciting a revolt!"),
		      unit_name (pdiplomat->type));
    notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		      _("Game: You caught %s %s attempting"
			" to incite a revolt in %s!"),
		      get_nation_name (pplayer->nation),
		      unit_name (pdiplomat->type), pcity->name);
    wipe_unit (pdiplomat);
    return;
  }

  freelog (LOG_DEBUG, "incite: succeeded");

  /* Subvert the city to your cause... */

  /* City loses some population. */
  if (pcity->size > 1) {
    (pcity->size)--;
    city_auto_remove_worker (pcity);
  }

  /* This costs! */
  pplayer->economic.gold -= revolt_cost;

  /* Notify everybody involved. */
  notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		    _("Game: Revolt incited in %s, you now rule the city!"),
		    pcity->name);
  notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		    _("Game: %s has revolted, %s influence suspected."),
		    pcity->name, get_nation_name (pplayer->nation));

  pcity->shield_stock = 0;

  /* You get a technology advance, too! */
  get_a_tech (pplayer, cplayer);

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_INCITE, pplayer, NULL, pcity);

  /* Check if a spy survives her mission. Diplomats never do. */
  diplomat_escape (pplayer, pdiplomat, pcity);

  /* Transfer city and units supported by this city (that
     are within one square of the city) to the new owner.
     Remember that pcity is destroyed as part of the transfer,
     Which is why we do this last */
  transfer_city (pplayer, cplayer, pcity, 1, 1, 1, 0);

  /* Update the players gold in the client */
  send_player_info(pplayer, pplayer);
}  

/**************************************************************************
  Sabotage enemy city's improvement or production.
  If "improvement" is B_LAST, sabotage a random improvement or production.
  Else, if "improvement" is -1, sabotage current production.
  Otherwise, sabotage the city improvement whose ID is "improvement".
  (Note: Only Spies can select what to sabotage.)

  - Either a Diplomat or Spy can sabotage an enemy city.
  - The players must be at war

  - Check for infiltration success.  Our saboteur may not survive this.
  - Check for basic success.  Again, our saboteur may not survive this.
  - Determine target, given arguments and constraints.
  - If specified, city walls and anything in a capital are 50% likely to fail.
  - Do sabotage!

  - The saboteur may be captured and executed, or escape to its home town.
**************************************************************************/
void diplomat_sabotage(struct player *pplayer, struct unit *pdiplomat,
		       struct city *pcity, int improvement)
{
  struct player *cplayer;
  int index, count, which, target;
  char *prod;
  struct city *capital;

  /* Fetch target city's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if (cplayer == NULL || !pplayers_at_war(pplayer, cplayer))
    return;

  freelog (LOG_DEBUG, "sabotage: unit: %d", pdiplomat->id);

  /* If not a Spy, do something random. */
  if (!unit_flag (pdiplomat->type, F_SPY))
    improvement = B_LAST;

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_city (pplayer, cplayer, pdiplomat, pcity))
    return;

  freelog (LOG_DEBUG, "sabotage: infiltrated");

  /* Check if the Diplomat/Spy succeeds with his/her task. */
  /* (Twice as difficult if target is specified.) */
  if ((myrand (100) >= game.diplchance) ||
      ((improvement != B_LAST) && (myrand (100) >= game.diplchance))) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: Your %s was caught in the attempt"
			" of industrial sabotage!"),
		      unit_name (pdiplomat->type));
    notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		      _("Game: You caught %s %s attempting"
			" sabotage in %s!"),
		      get_nation_name (pplayer->nation),
		      unit_name (pdiplomat->type), pcity->name);
    wipe_unit (pdiplomat);
    return;
  }

  freelog (LOG_DEBUG, "sabotage: succeeded");

  /* Examine the city for improvements to sabotage. */
  count = 0;
  for (index = 0; index < game.num_impr_types; index++) {
    if (city_got_building (pcity, index) &&
	(!is_wonder (index)) && (index != B_PALACE)) {
      count++;
    }
  }

  freelog (LOG_DEBUG, "sabotage: count of improvements: %d", count);

  /* Determine the target (-1 is production). */
  if (improvement < 0) {
    /* If told to sabotage production, do so. */
    target = -1;
    freelog (LOG_DEBUG, "sabotage: specified target production: %d", target);
  } else if (improvement >= B_LAST) {
    /*
     * Pick random:
     * 50/50 chance to pick production or some improvement.
     * Won't pick something that doesn't exit.
     * If nothing to do, say so, deduct movement cost and return.
     */
    if (!count && (!(pcity->shield_stock))) {
      notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			_("Game: Your %s could not find anything to"
			  " sabotage in %s."), 
			unit_name (pdiplomat->type), pcity->name);
      diplomat_charge_movement (pdiplomat, pcity->x, pcity->y);
      send_unit_info (pplayer, pdiplomat);
      freelog (LOG_DEBUG, "sabotage: random: nothing to do");
      return;
    }
    if (!count || myrand (2)) {
      target = -1;
      freelog (LOG_DEBUG, "sabotage: random: targeted production: %d", target);
    } else {
      target = -1;
      which = myrand (count);
      for (index = 0; index < game.num_impr_types; index++) {
	if (city_got_building (pcity, index) &&
	    (!is_wonder (index)) && (index != B_PALACE)) {
	  if (which > 0) {
	    which--;
	  } else {
	    target = index;
	    break;
	  }
	}
      }
      freelog (LOG_DEBUG, "sabotage: random: targeted improvement: %d (%s)",
	       target, get_improvement_name (target));
    }
  } else {
    /*
     * Told which improvement to pick:
     * If try for wonder or palace, complain, deduct movement cost and return.
     * If not available, say so, deduct movement cost and return.
     */
    if (city_got_building (pcity, improvement)) {
      if ((!is_wonder (improvement)) && (improvement != B_PALACE)) {
	target = improvement;
	freelog (LOG_DEBUG, "sabotage: specified target improvement: %d (%s)",
	       target, get_improvement_name (target));
      } else {
	notify_player_ex (pplayer, pcity->x, pcity->y, E_NOEVENT,
			  _("Game: You cannot sabotage a wonder or a %s!"),
			  improvement_types[B_PALACE].name);
	diplomat_charge_movement (pdiplomat, pcity->x, pcity->y);
	send_unit_info (pplayer, pdiplomat);
	freelog (LOG_DEBUG, "sabotage: disallowed target improvement: %d (%s)",
	       improvement, get_improvement_name (improvement));
	return;
      }
    } else {
      notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			_("Game: Your %s could not find the %s to"
			  " sabotage in %s."), 
			unit_name (pdiplomat->type),
			get_improvement_name (improvement), pcity->name);
      diplomat_charge_movement (pdiplomat, pcity->x, pcity->y);
      send_unit_info (pplayer, pdiplomat);
      freelog (LOG_DEBUG, "sabotage: target improvement not found: %d (%s)",
	       improvement, get_improvement_name (improvement));
      return;
    }
  }

  /* Now, the fun stuff!  Do the sabotage! */
  if (target < 0) {
    /* Sabotage current production. */

    /* Do it. */
    pcity->shield_stock = 0;
    /* Report it. */
    if (pcity->is_building_unit)
      prod = unit_name (pcity->currently_building);
    else
      prod = get_improvement_name (pcity->currently_building);
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: Your %s succeeded in destroying"
			" the production of %s in %s."),
		      unit_name (pdiplomat->type), prod, pcity->name);
    notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		      _("Game: The production of %s was destroyed in %s,"
			" %s are suspected."),
		      prod, pcity->name,
		      get_nation_name_plural (pplayer->nation));
    freelog (LOG_DEBUG, "sabotage: sabotaged production");
  } else {
    /* Sabotage a city improvement. */

    /*
     * One last chance to get caught:
     * If target was specified, and it is in the capital or are
     * City Walls, then there is a 50% chance of getting caught.
     */
    capital = find_palace (city_owner (pcity));
    if ((pcity == capital) || (improvement == B_CITY)) {
      if (myrand (2)) {
	/* Caught! */
	notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			  _("Game: Your %s was caught in the attempt"
			    " of sabotage!"),
			  unit_name (pdiplomat->type));
	notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			  _("Game: You caught %s %s attempting"
			    " to sabotage the %s in %s!"),
			  get_nation_name (pplayer->nation),
			  unit_name (pdiplomat->type),
			  get_improvement_name (improvement), pcity->name);
	wipe_unit (pdiplomat);
	freelog (LOG_DEBUG, "sabotage: caught in capital or on city walls");
	return;
      }
    }

    /* Do it. */
    pcity->improvements[target] = 0;
    /* Report it. */
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: Your %s destroyed the %s in %s."),
		      unit_name (pdiplomat->type),
		      get_improvement_name (target), pcity->name);
    notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		      _("Game: The %s destroyed the %s in %s."),
		      get_nation_name_plural (pplayer->nation),
		      get_improvement_name (target), pcity->name);
    freelog (LOG_DEBUG, "sabotage: sabotaged improvement: %d (%s)",
	       target, get_improvement_name (target));
  }

  /* Update clients. */
  send_city_info (0, pcity);

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_SABOTAGE, pplayer, NULL, pcity);

  /* Check if a spy survives her mission. Diplomats never do. */
  diplomat_escape (pplayer, pdiplomat, pcity);
}

/**************************************************************************
  This subtracts the destination movement cost from a diplomat/spy.
**************************************************************************/
static void diplomat_charge_movement (struct unit *pdiplomat, int x, int y)
{
  pdiplomat->moves_left -=
    map_move_cost (pdiplomat, x, y);
  if (pdiplomat->moves_left < 0) {
    pdiplomat->moves_left = 0;
  }
}

/**************************************************************************
  This determines if a diplomat/spy succeeds against some defender,
  who is also a diplomat or spy.
  (Note: This is weird in order to try to conform to Civ2 rules.)

  - Depends entirely upon game.diplchance and the defender:
    - Spies are much better.
    - Veterans are somewhat better.

  - Return TRUE if the "attacker" succeeds.
**************************************************************************/
static int diplomat_success_vs_defender (struct unit *pdefender)
{
  int success = game.diplchance;

  if (unit_flag (pdefender->type, F_SPY)) {
    if (success > 66) {
      success -= (100 - success);
    } else {
      success -= (success / 2);
    }
  }

  if (pdefender->veteran) {
    if (success >= 50) {
      success -= ((100 - success) / 2);
    } else {
      success -= (success / 2);
    }
  }

  return (myrand (100) < success);
}

/**************************************************************************
  This determines if a diplomat/spy succeeds in infiltrating a city.

  - The infiltrator must go up against each defender.
  - One or the other is eliminated in each contest.

  - Return TRUE if the infiltrator succeeds.
**************************************************************************/
static int diplomat_infiltrate_city (struct player *pplayer, struct player *cplayer,
			      struct unit *pdiplomat, struct city *pcity)
{
  unit_list_iterate ((map_get_tile (pcity->x, pcity->y))->units, punit)
    if (unit_flag (punit->type, F_DIPLOMAT)) {
      if (diplomat_success_vs_defender (punit)) {
	/* Defending Spy/Diplomat dies. */

	notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			  _("Game: Your %s has been eliminated defending"
			    " against a %s in %s."),
			  unit_name (punit->type),
			  unit_name (pdiplomat->type),
			  pcity->name);

	wipe_unit_safe(punit, &myiter);
      } else {
	/* Attacking Spy/Diplomat dies. */

	notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			  _("Game: Your %s was eliminated"
			    " by a defending %s in %s."),
			  unit_name (pdiplomat->type),
			  unit_name (punit->type),
			  pcity->name);
	notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			  _("Game: Eliminated %s %s while infiltrating %s."), 
			  get_nation_name (pplayer->nation),
			  unit_name (pdiplomat->type),
			  pcity->name);

	wipe_unit_safe(pdiplomat, &myiter);
	return 0;
      }
    }
  unit_list_iterate_end;

  return 1;
}

/**************************************************************************
  This determines if a diplomat/spy survives and escapes.
  If "pcity" is NULL, assume action was in the field.

  - A Diplomat always dies.
  - A Spy has a game.diplchance change of survival:
    - Escapes to home city.
    - Escapee may become a veteran.
**************************************************************************/
static void diplomat_escape (struct player *pplayer, struct unit *pdiplomat,
		      struct city *pcity)
{
  int x, y;

  if (unit_flag (pdiplomat->type, F_SPY)) {
    if (pcity) {
      x = pcity->x;
      y = pcity->y;
    } else {
      x = pdiplomat->x;
      y = pdiplomat->y;
    }

    if (myrand (100) < game.diplchance) {
      /* Attacking Spy/Diplomat survives. */

      struct city *spyhome = find_city_by_id (pdiplomat->homecity);

      if (!spyhome) {
	send_unit_info (pplayer, pdiplomat);
	freelog(LOG_ERROR, "Bug in diplomat_escape: Unhomed Spy.");
	return;
      }

      notify_player_ex (pplayer, x, y, E_NOEVENT,
			_("Game: Your %s has successfully completed"
			  " her mission and returned unharmed to %s."),
			unit_name (pdiplomat->type), spyhome->name);

      /* may become a veteran */
      maybe_make_veteran (pdiplomat);

      /* being teleported costs all movement */
      if (!teleport_unit_to_city (pdiplomat, spyhome, -1, 0)) {
	send_unit_info (pplayer, pdiplomat);
	freelog(LOG_ERROR, "Bug in diplomat_escape: Spy can't teleport.");
	return;
      }

      return;
    } else {
      /* Attacking Spy/Diplomat dies. */

      if (pcity) {
	notify_player_ex (pplayer, x, y, E_NOEVENT,
			  _("Game: Your %s was captured after completing"
			    " her mission in %s."),
			  unit_name (pdiplomat->type), pcity->name);
      } else {
	notify_player_ex (pplayer, x, y, E_NOEVENT,
			  _("Game: Your %s was captured after completing"
			    " her mission."),
			  unit_name (pdiplomat->type));
      }

      /* fall through */
    }
  }

  wipe_unit (pdiplomat);
}

/**************************************************************************
...
**************************************************************************/
static void maybe_cause_incident(enum diplomat_actions action, struct player *offender,
 				 struct unit *victim_unit, struct city *victim_city)
{
  struct player *victim_player = 0;
  int x = 0, y = 0;
  if (victim_city) {
    x = victim_city->x;
    y = victim_city->y;
    victim_player = city_owner(victim_city);
  } else if (victim_unit) {
    x = victim_unit->x;
    y = victim_unit->y;
    victim_player = unit_owner(victim_unit);
  } else {
    freelog(LOG_FATAL, "No victim in call to maybe_cause_incident()");
    abort();
  }

  if (!pplayers_at_war(offender, victim_player) &&
      (myrand(GAME_MAX_REPUTATION) - offender->reputation >
       GAME_MAX_REPUTATION/2 - victim_player->reputation)) {
    enum diplstate_type ds = pplayer_get_diplstate(offender, victim_player)->type;
    int punishment = 0;
    switch (action) {
    case DIPLOMAT_BRIBE:
      notify_player_ex(offender, x, y, E_DIPL_INCIDENT,
		       _("Game: You have caused an incident while bribing "
 			 "%s's %s."),
 		       victim_player->name,
 		       unit_name(victim_unit->type));
      notify_player_ex(victim_player, x, y, E_DIPL_INCIDENT,
		       _("Game: %s has caused an incident while bribing "
 			 "your %s."),
 		       offender->name,
 		       unit_name(victim_unit->type));
      break;
    case DIPLOMAT_STEAL:
      notify_player_ex(offender, x, y, E_DIPL_INCIDENT,
 		       _("Game: You have caused an incident while stealing "
 			 "tech from %s."),
 		       victim_player->name);
      notify_player_ex(victim_player, x, y, E_DIPL_INCIDENT,
		       _("Game: %s has caused an incident while stealing "
			 "tech from you."),
		       offender->name);
      break;
    case DIPLOMAT_INCITE:
      notify_player_ex(offender, x, y, E_DIPL_INCIDENT,
 		       _("Game: You have caused an incident while inciting a "
 			 "revolt in %s."), victim_city->name);
      notify_player_ex(victim_player, x, y, E_DIPL_INCIDENT,
 		       _("Game: %s have caused an incident while inciting a "
 			 "revolt in %s."), offender->name, victim_city->name);
      break;
    case DIPLOMAT_MOVE:
    case DIPLOMAT_EMBASSY:
    case DIPLOMAT_INVESTIGATE:
    case SPY_GET_SABOTAGE_LIST:
      return; /* These are not considered offences */
    case DIPLOMAT_ANY_ACTION:
    case SPY_POISON:
    case SPY_SABOTAGE_UNIT:
    case DIPLOMAT_SABOTAGE:
      /* You can only do these when you are at war, so we should never
 	 get inside this "if" */
      freelog(LOG_FATAL, "Bug in maybe_cause_incident()");
      abort();
    }
    switch (ds) {
    case DS_WAR:
    case DS_NO_CONTACT:
      freelog(LOG_VERBOSE,"Trying to cause an incident between players at war");
      punishment = 0;
      break;
    case DS_NEUTRAL:
      punishment = GAME_MAX_REPUTATION/20;
      break;
    case DS_CEASEFIRE:
    case DS_PEACE:
      punishment = GAME_MAX_REPUTATION/10;
      break;
    case DS_ALLIANCE:
      punishment = GAME_MAX_REPUTATION/5;
      break;
    default:
      freelog(LOG_FATAL, "Illegal diplstate in maybe_cause_incident.");
      abort();
    }
    offender->reputation = MAX(offender->reputation - punishment, 0);
    victim_player->diplstates[offender->player_no].has_reason_to_cancel = 2;
    /* FIXME: Maybe we should try to cause a revolution is the offender's
       government has a senate */
    send_player_info(offender, 0);
    send_player_info(victim_player, 0);
  }

  return;
}

/*****************************************************************
  Will wake up any neighboring enemy sentry units
*****************************************************************/
static void wakeup_neighbor_sentries(struct player *pplayer,
				     int cent_x, int cent_y)
{
  int x,y;
/*  struct unit *punit; The unit_list_iterate defines punit locally. -- Syela */

  for (x = cent_x-1;x <= cent_x+1;x++)
    for (y = cent_y-1;y <= cent_y+1;y++)
      if ((x != cent_x)||(y != cent_y)) {
 	unit_list_iterate(map_get_tile(x,y)->units, punit) {
 	  if (!players_allied(pplayer->player_no, punit->owner)
 	      && punit->activity == ACTIVITY_SENTRY) {
 	    set_unit_activity(punit, ACTIVITY_IDLE);
 	    send_unit_info(0,punit);
  	  }
  	}
 	unit_list_iterate_end;
      }
}

/***************************************************************************
 Return 1 if upgrading this unit could cause passengers to
 get stranded at sea, due to transport capacity changes
 caused by the upgrade.
 Return 0 if ok to upgrade (as far as stranding goes).
***************************************************************************/
static int upgrade_would_strand(struct unit *punit, int upgrade_type)
{
  int old_cap, new_cap, tile_cap, tile_ncargo;
  
  if (!is_sailing_unit(punit))
    return 0;
  if (map_get_terrain(punit->x, punit->y) != T_OCEAN)
    return 0;

  /* With weird non-standard unit types, upgrading these could
     cause air units to run out of fuel; too bad. */
  if (unit_flag(punit->type, F_CARRIER)
      || unit_flag(punit->type, F_MISSILE_CARRIER))
    return 0;

  old_cap = get_transporter_capacity(punit);
  new_cap = unit_types[upgrade_type].transport_capacity;

  if (new_cap >= old_cap)
    return 0;

  tile_cap = 0;
  tile_ncargo = 0;
  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
    if (punit2->owner == punit->owner) {
      if (is_sailing_unit(punit2) && is_ground_units_transport(punit2)) { 
	tile_cap += get_transporter_capacity(punit2);
      } else if (is_ground_unit(punit2)) {
	tile_ncargo++;
      }
    }
  }
  unit_list_iterate_end;

  if (tile_ncargo <= tile_cap - old_cap + new_cap)
    return 0;

  freelog(LOG_VERBOSE, "Can't upgrade %s at (%d,%d)"
	  " because would strand passenger(s)",
	  get_unit_type(punit->type)->name, punit->x, punit->y);
  return 1;
}

/***************************************************************************
Restore unit hitpoints. (movepoint - restoration moved to update_unit_activities)
Do Leonardo's Workshop upgrade if applicable.
Now be careful not to strand units at sea with the Workshop. --dwp
Adjust air units for fuel: air units lose fuel unless in a city,
on a Carrier or on a airbase special (or, for Missles, on a Submarine).
Air units which run out of fuel get wiped.
Carriers and Submarines can now only supply fuel to a limited
number of units each, equal to their transport_capacity. --dwp
(Hitpoint adjustments include Helicopters out of cities, but
that is handled within unit_restore_hitpoints().)
Triremes will be wiped with 50% chance if they're not close to
land, and player doesn't have Lighthouse.
****************************************************************************/
void player_restore_units(struct player *pplayer)
{
  int leonardo, leonardo_variant;
  int lighthouse_effect=-1;	/* 1=yes, 0=no, -1=not yet calculated */
  int upgrade_type, done;

  leonardo = player_owns_active_wonder(pplayer, B_LEONARDO);
  leonardo_variant = improvement_variant(B_LEONARDO);

  /* get Leonardo out of the way first: */
  if (leonardo) {
    unit_list_iterate(pplayer->units, punit) {
      if (leonardo &&
	  (upgrade_type=can_upgrade_unittype(pplayer, punit->type))!=-1
	  && !upgrade_would_strand(punit, upgrade_type)) {
	notify_player(pplayer,
		      _("Game: %s has upgraded %s to %s%s."),
		      improvement_types[B_LEONARDO].name,
		      get_unit_type(punit->type)->name,
		      get_unit_type(upgrade_type)->name,
		      get_location_str_in(pplayer, punit->x, punit->y));
	punit->veteran = 0;
	upgrade_unit(punit,upgrade_type);
	leonardo = leonardo_variant;
      }
    }
    unit_list_iterate_end;
  }

  /* Temporarily use 'fuel' on Carriers and Subs to keep track
     of numbers of supported Air Units:   --dwp */
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit->type, F_CARRIER) || 
	unit_flag(punit->type, F_MISSILE_CARRIER)) {
      punit->fuel = get_unit_type(punit->type)->transport_capacity;
    }
  }
  unit_list_iterate_end;

  unit_list_iterate(pplayer->units, punit) {
    unit_restore_hitpoints(pplayer, punit);

    if(punit->hp<=0) {
      /* This should usually only happen for heli units,
	 but if any other units get 0 hp somehow, catch
	 them too.  --dwp  */
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
		       _("Game: Your %s has run out of hit points."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITF, "%s lose a %s (out of hp)", 
	      get_nation_name_plural(pplayer->nation),
	      unit_name(punit->type));
      wipe_unit_safe(punit, &myiter);
    } else if (is_air_unit(punit)) {
      /* Shall we emergency return home on the last vapors? */
      if (punit->fuel == 1
	  && !is_airunit_refuel_point(punit->x, punit->y,
				      punit->owner, punit->type, 1)) {
	int x_itr, y_itr;
	iterate_outward(punit->x, punit->y, punit->moves_left/3, x_itr, y_itr) {
	  if (is_airunit_refuel_point(x_itr, y_itr, punit->owner, punit->type, 0)
	      && (air_can_move_between(punit->moves_left/3, punit->x, punit->y,
		    x_itr, y_itr, punit->owner) >= 0) ) {
	      punit->goto_dest_x = x_itr;
	      punit->goto_dest_y = y_itr;
	      set_unit_activity(punit, ACTIVITY_GOTO);
	      do_unit_goto(punit, GOTO_MOVE_ANY, 0);
	      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
			       _("Game: Your %s has returned to refuel."),
			       unit_name(punit->type));
	      goto OUT;
	    }
	} iterate_outward_end;
      }
    OUT:

      punit->fuel--;
      if(map_get_city(punit->x, punit->y) ||
         map_get_special(punit->x, punit->y)&S_AIRBASE)
	punit->fuel=get_unit_type(punit->type)->fuel;
      else {
	done = 0;
	if (unit_flag(punit->type, F_MISSILE)) {
	  /* Want to preferentially refuel Missiles from Subs,
	     to leave space on co-located Carriers for normal air units */
	  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) 
	    if (!done && unit_flag(punit2->type, F_MISSILE_CARRIER)
		&& punit2->fuel
		&& punit2->owner == punit->owner) {
	      punit->fuel = get_unit_type(punit->type)->fuel;
	      punit2->fuel--;
	      done = 1;
	    }
	  unit_list_iterate_end;
	}
	if (!done) {
	  /* Non-Missile or not refueled by Subs: use Carriers: */
	  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) 
	    if (!done && unit_flag(punit2->type,F_CARRIER) && punit2->fuel) {
	      punit->fuel = get_unit_type(punit->type)->fuel;
	      punit2->fuel--;
	      done = 1;
	    }
	  unit_list_iterate_end;
	}
      }
      if(punit->fuel<=0) {
	notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
			 _("Game: Your %s has run out of fuel."),
			 unit_name(punit->type));
	gamelog(GAMELOG_UNITF, "%s lose a %s (fuel)", 
		get_nation_name_plural(pplayer->nation),
		unit_name(punit->type));
	wipe_unit_safe(punit, &myiter);
      }
    } else if (unit_flag(punit->type, F_TRIREME) && (lighthouse_effect!=1) &&
	       !is_coastline(punit->x, punit->y) &&
	       (map_get_terrain(punit->x, punit->y) == T_OCEAN)) {
      if (lighthouse_effect == -1) {
	lighthouse_effect = player_owns_active_wonder(pplayer, B_LIGHTHOUSE);
      }
      if ((!lighthouse_effect) && (myrand(100) >= 50)) {
	notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
			 _("Game: Your %s has been lost on the high seas."),
			 unit_name(punit->type));
	gamelog(GAMELOG_UNITTRI, "%s Trireme lost at sea",
		get_nation_name_plural(pplayer->nation));
	wipe_unit_safe(punit, &myiter);
      }
    }
  }
  unit_list_iterate_end;
  
  /* Clean up temporary use of 'fuel' on Carriers and Subs: */
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit->type, F_CARRIER) || 
	unit_flag(punit->type, F_MISSILE_CARRIER)) {
      punit->fuel = 0;
    }
  }
  unit_list_iterate_end;
}

/****************************************************************************
  add hitpoints to the unit, hp_gain_coord returns the amount to add
  united nations will speed up the process by 2 hp's / turn, means helicopters
  will actually not loose hp's every turn if player have that wonder.
  Units which have moved don't gain hp, except the United Nations and
  helicopter effects still occur.
*****************************************************************************/
static void unit_restore_hitpoints(struct player *pplayer, struct unit *punit)
{
  int was_lower;

  was_lower=(punit->hp < get_unit_type(punit->type)->hp);

  if(!punit->moved) {
    punit->hp+=hp_gain_coord(punit);
  }
  
  if (player_owns_active_wonder(pplayer, B_UNITED)) 
    punit->hp+=2;
    
  if(is_heli_unit(punit)) {
    struct city *pcity = map_get_city(punit->x,punit->y);
    if(!pcity) {
      if(!(map_get_special(punit->x,punit->y) & S_AIRBASE))
        punit->hp-=get_unit_type(punit->type)->hp/10;
    }
  }

  if(punit->hp>=get_unit_type(punit->type)->hp) {
    punit->hp=get_unit_type(punit->type)->hp;
    if(was_lower&&punit->activity==ACTIVITY_SENTRY)
      set_unit_activity(punit,ACTIVITY_IDLE);
  }
  if(punit->hp<0)
    punit->hp=0;

  punit->moved=0;
  punit->paradropped=0;
}
  

/***************************************************************************
 move points are trivial, only modifiers to the base value is if it's
  sea units and the player has certain wonders/techs
***************************************************************************/
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit)
{
  punit->moves_left=unit_move_rate(punit);
}

/**************************************************************************
  after a battle this routine is called to decide whether or not the unit
  should become a veteran, if unit isn't already.
  there is a 50/50% chance for it to happend, (100% if player got SUNTZU)
**************************************************************************/
static void maybe_make_veteran(struct unit *punit)
{
    if (punit->veteran) 
      return;
    if(player_owns_active_wonder(get_player(punit->owner), B_SUNTZU)) 
      punit->veteran = 1;
    else
      punit->veteran=myrand(2);
}

/**************************************************************************
  This is the basic unit versus unit combat routine.
  1) ALOT of modifiers bonuses etc is added to the 2 units rates.
  2) the combat loop, which continues until one of the units are dead
  3) the aftermath, the looser (and potentially the stack which is below it)
     is wiped, and the winner gets a chance of gaining veteran status
**************************************************************************/
void unit_versus_unit(struct unit *attacker, struct unit *defender)
{
  int attackpower = get_total_attack_power(attacker,defender);
  int defensepower = get_total_defense_power(attacker,defender);
  int attack_firepower = get_unit_type(attacker->type)->firepower;
  int defense_firepower = get_unit_type(defender->type)->firepower;

  /* pearl harbour */
  if (is_sailing_unit(defender) && map_get_city(defender->x, defender->y))
    defense_firepower = 1;

  /* In land bombardment both units have their firepower reduced to 1 */
  if (is_sailing_unit(attacker)
      && map_get_terrain(defender->x, defender->y) != T_OCEAN
      && is_ground_unit(defender)) {
    attack_firepower = 1;
    defense_firepower = 1;
  }

  freelog(LOG_VERBOSE, "attack:%d, defense:%d, attack firepower:%d, defense firepower:%d",
	  attackpower, defensepower, attack_firepower, defense_firepower);
  if (!attackpower) {
      attacker->hp=0; 
  } else if (!defensepower) {
      defender->hp=0;
  }
  while (attacker->hp>0 && defender->hp>0) {
    if (myrand(attackpower+defensepower) >= defensepower) {
      defender->hp -= attack_firepower * game.firepower_factor;
    } else {
      attacker->hp -= defense_firepower * game.firepower_factor;
    }
  }
  if (attacker->hp<0) attacker->hp = 0;
  if (defender->hp<0) defender->hp = 0;

  if (attacker->hp)
    maybe_make_veteran(attacker); 
  else if (defender->hp)
    maybe_make_veteran(defender);
}

/***************************************************************************
 return the modified attack power of a unit.  Currently they aren't any
 modifications...
***************************************************************************/
int get_total_attack_power(struct unit *attacker, struct unit *defender)
{
  int attackpower=get_attack_power(attacker);

  return attackpower;
}

/***************************************************************************
 Like get_virtual_defense_power, but don't include most of the modifications.
 (For calls which used to be g_v_d_p(U_HOWITZER,...))
 Specifically, include:
 unit def, terrain effect, fortress effect, ground unit in city effect
***************************************************************************/
int get_simple_defense_power(int d_type, int x, int y)
{
  int defensepower=unit_types[d_type].defense_strength;
  struct city *pcity = map_get_city(x, y);
  enum tile_terrain_type t = map_get_terrain(x, y);
  int db;

  if (unit_types[d_type].move_type == LAND_MOVING && t == T_OCEAN) return 0;
/* I had this dorky bug where transports with mech inf aboard would go next
to enemy ships thinking the mech inf would defend them adequately. -- Syela */

  db = get_tile_type(t)->defense_bonus;
  if (map_get_special(x, y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  defensepower *= db;

  if (map_get_special(x, y)&S_FORTRESS && !pcity)
    defensepower+=(defensepower*terrain_control.fortress_defense_bonus)/100;
  if (pcity && unit_types[d_type].move_type == LAND_MOVING)
    defensepower*=1.5;

  return defensepower;
}

int get_virtual_defense_power(int a_type, int d_type, int x, int y)
{
  int defensepower=unit_types[d_type].defense_strength;
  int m_type = unit_types[a_type].move_type;
  struct city *pcity = map_get_city(x, y);
  enum tile_terrain_type t = map_get_terrain(x, y);
  int db;

  if (unit_types[d_type].move_type == LAND_MOVING && t == T_OCEAN) return 0;
/* I had this dorky bug where transports with mech inf aboard would go next
to enemy ships thinking the mech inf would defend them adequately. -- Syela */

  db = get_tile_type(t)->defense_bonus;
  if (map_get_special(x, y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  defensepower *= db;

  if (unit_flag(d_type, F_PIKEMEN) && unit_flag(a_type, F_HORSE)) 
    defensepower*=2;
  if (unit_flag(d_type, F_AEGIS) &&
       (m_type == AIR_MOVING || m_type == HELI_MOVING)) defensepower*=5;
  if (m_type == AIR_MOVING && pcity) {
    if (city_got_building(pcity, B_SAM))
      defensepower*=2;
    if (city_got_building(pcity, B_SDI) && unit_flag(a_type, F_MISSILE))
      defensepower*=2;
  } else if (m_type == SEA_MOVING && pcity) {
    if (city_got_building(pcity, B_COASTAL))
      defensepower*=2;
  }
  if (!unit_flag(a_type, F_IGWALL)
      && (m_type == LAND_MOVING || m_type == HELI_MOVING
	  || (improvement_variant(B_CITY)==1 && m_type == SEA_MOVING))
      && pcity && city_got_citywalls(pcity)) {
    defensepower*=3;
  }
  if (map_get_special(x, y)&S_FORTRESS && !pcity)
    defensepower+=(defensepower*terrain_control.fortress_defense_bonus)/100;
  if (pcity && unit_types[d_type].move_type == LAND_MOVING)
    defensepower*=1.5;

  return defensepower;
}

/***************************************************************************
 return the modified defense power of a unit.
 An veteran aegis cruiser in a mountain city with SAM and SDI defense 
 being attacked by a missile gets defense 288.
***************************************************************************/
int get_total_defense_power(struct unit *attacker, struct unit *defender)
{
  int defensepower=get_defense_power(defender);
  if (unit_flag(defender->type, F_PIKEMEN) && unit_flag(attacker->type, F_HORSE)) 
    defensepower*=2;
  if (unit_flag(defender->type, F_AEGIS) && (is_air_unit(attacker) || is_heli_unit(attacker)))
    defensepower*=5;
  if (is_air_unit(attacker)) {
    if (unit_behind_sam(defender))
      defensepower*=2;
    if (unit_behind_sdi(defender) && unit_flag(attacker->type, F_MISSILE))
      defensepower*=2;
  } else if (is_sailing_unit(attacker)) {
    if (unit_behind_coastal(defender))
      defensepower*=2;
  }
  if (!unit_really_ignores_citywalls(attacker)
      && unit_behind_walls(defender)) 
    defensepower*=3;
  if (unit_on_fortress(defender) && 
      !map_get_city(defender->x, defender->y)) 
    defensepower*=2;
  if ((defender->activity == ACTIVITY_FORTIFIED || 
       map_get_city(defender->x, defender->y)) && 
      is_ground_unit(defender))
    defensepower*=1.5;

  return defensepower;
}

/**************************************************************************
 creates a unit, and set it's initial values, and put it into the right 
 lists.
 TODO: Maybe this procedure should refresh its homecity? so it'll show up 
 immediately on the clients? (refresh_city + send_city_info)
**************************************************************************/

/* This is a wrapper */

void create_unit(struct player *pplayer, int x, int y, Unit_Type_id type,
		 int make_veteran, int homecity_id, int moves_left)
{
  create_unit_full(pplayer,x,y,type,make_veteran,homecity_id,moves_left,-1);
}

/* This is the full call.  We don't want to have to change all other calls to
   this function to ensure the hp are set */

void create_unit_full(struct player *pplayer, int x, int y,
		      Unit_Type_id type, int make_veteran, int homecity_id,
		      int moves_left, int hp_left)
{
  struct unit *punit;
  struct city *pcity;
  punit=fc_calloc(1,sizeof(struct unit));

  punit->type=type;
  punit->id=get_next_id_number();
  idex_register_unit(punit);
  punit->owner=pplayer->player_no;
  punit->x = map_adjust_x(x); /* was = x, caused segfaults -- Syela */
  punit->y=y;
  if (y < 0 || y >= map.ysize) {
    freelog(LOG_ERROR, "Whoa!  Creating %s at illegal loc (%d, %d)",
	    get_unit_type(type)->name, x, y);
  }
  punit->goto_dest_x=0;
  punit->goto_dest_y=0;
  
  pcity=find_city_by_id(homecity_id);
  punit->veteran=make_veteran;
  punit->homecity=homecity_id;

  if(hp_left == -1)
    punit->hp=get_unit_type(punit->type)->hp;
  else
    punit->hp = hp_left;
  set_unit_activity(punit, ACTIVITY_IDLE);

  punit->upkeep=0;
  punit->upkeep_food=0;
  punit->upkeep_gold=0;
  punit->unhappiness=0;

  /* 
     See if this is a spy that has been moved (corrupt and therefore unable 
     to establish an embassy.
  */
  if(moves_left != -1 && unit_flag(punit->type, F_SPY))
    punit->foul=1;
  else
    punit->foul=0;
  
  punit->fuel=get_unit_type(punit->type)->fuel;
  punit->ai.control=0;
  punit->ai.ai_role = AIUNIT_NONE;
  punit->ai.ferryboat = 0;
  punit->ai.passenger = 0;
  punit->ai.bodyguard = 0;
  punit->ai.charge = 0;
  unit_list_insert(&pplayer->units, punit);
  unit_list_insert(&map_get_tile(x, y)->units, punit);
  if (pcity)
    unit_list_insert(&pcity->units_supported, punit);
  punit->bribe_cost=-1;		/* flag value */
  if(moves_left < 0)  
    punit->moves_left=unit_move_rate(punit);
  else
    punit->moves_left=moves_left;

  /* Assume that if moves_left<0 then the unit is "fresh",
     and not moved; else the unit has had something happen
     to it (eg, bribed) which we treat as equivalent to moved.
     (Otherwise could pass moved arg too...)  --dwp
  */
  punit->moved = (moves_left>=0);

  /* Probably not correct when unit changed owner (e.g. bribe) */
  punit->paradropped = 0;

  if( is_barbarian(pplayer) )
    punit->fuel = BARBARIAN_LIFE;

  /* ditto for connecting units */
  punit->connecting = 0;

  punit->transported_by = -1;
  punit->pgr = NULL;

  unfog_area(pplayer,x,y,get_unit_type(punit->type)->vision_range);
  send_unit_info(0, punit);
  maybe_make_first_contact(x, y, punit->owner);

  /* The unit may have changed the available tiles in nearby cities. */
  map_city_radius_iterate(x, y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity && pcity->owner != punit->owner) {
      if (city_check_workers(pcity, 1)) {
	send_city_info(city_owner(pcity), pcity);
      }
    }
  } map_city_radius_iterate_end;
}

/**************************************************************************
  iterate through all units and update them.
**************************************************************************/
void update_unit_activities(struct player *pplayer)
{
  unit_list_iterate(pplayer->units, punit)
    update_unit_activity(pplayer, punit, &myiter);
  unit_list_iterate_end;
}

/**************************************************************************
  Calculate the total amount of activity performed by all units on a tile
  for a given task.
**************************************************************************/
static int total_activity (int x, int y, enum unit_activity act)
{
  struct tile *ptile;
  int total = 0;

  ptile = map_get_tile (x, y);
  unit_list_iterate (ptile->units, punit)
    if (punit->activity == act)
      total += punit->activity_count;
  unit_list_iterate_end;
  return total;
}

/**************************************************************************
  Calculate the total amount of activity performed by all units on a tile
  for a given task and target.
**************************************************************************/
static int total_activity_targeted (int x, int y,
				    enum unit_activity act,
				    int tgt)
{
  struct tile *ptile;
  int total = 0;

  ptile = map_get_tile (x, y);
  unit_list_iterate (ptile->units, punit)
    if ((punit->activity == act) && (punit->activity_target == tgt))
      total += punit->activity_count;
  unit_list_iterate_end;
  return total;
}

/**************************************************************************
  For each city adjacent to (x,y), check if landlocked.  If so, sell all
  improvements in the city that depend upon being next to an ocean tile.
  (Should be called after any ocean to land terrain changes.
   Assumes no city at (x,y).)
**************************************************************************/
static void city_landlocked_sell_coastal_improvements(int x, int y)
{
  /* FIXME: this should come from buildings.ruleset */
  static int coastal_improvements[] =
  {
    B_HARBOUR,
    B_PORT,
    B_COASTAL,
    B_OFFSHORE
  };
  #define coastal_improvements_count \
    (sizeof(coastal_improvements)/sizeof(coastal_improvements[0]))

  int i, j, k;
  struct city *pcity;
  struct player *pplayer;

  for (i=-1; i<=1; i++) {
    for (j=-1; j<=1; j++) {
      if ((i || j) &&
	  (pcity = map_get_city(x+i, y+j)) &&
	  (!(is_terrain_near_tile(x+i, y+j, T_OCEAN)))) {
	pplayer = city_owner(pcity);
	for (k=0; k<coastal_improvements_count; k++) {
	  if (city_got_building(pcity, coastal_improvements[k])) {
	    do_sell_building(pplayer, pcity, coastal_improvements[k]);
	    notify_player_ex(pplayer, x+i, y+j, E_IMP_SOLD,
			     _("Game: You sell %s in %s (now landlocked)"
			       " for %d gold."),
			     get_improvement_name(coastal_improvements[k]),
			     pcity->name,
			     improvement_value(coastal_improvements[k]));
	  }
	}
      }
    }
  }
}

/**************************************************************************
  Set the tile to be a river if required.
  It's required if one of the tiles nearby would otherwise be part of a
  river to nowhere.
  For simplicity, I'm assuming that this is the only exit of the river,
  so I don't need to trace it across the continent.  --CJM
  Also, note that this only works for R_AS_SPECIAL type rivers.  --jjm
**************************************************************************/
static void ocean_to_land_fix_rivers(int x, int y)
{
  /* clear the river if it exists */
  map_clear_special(x, y, S_RIVER);

  /* check north */
  if((map_get_special(x, y-1) & S_RIVER) &&
     (map_get_terrain(x-1, y-1) != T_OCEAN) &&
     (map_get_terrain(x+1, y-1) != T_OCEAN)) {
    map_set_special(x, y, S_RIVER);
    return;
  }

  /* check south */
  if((map_get_special(x, y+1) & S_RIVER) &&
     (map_get_terrain(x-1, y+1) != T_OCEAN) &&
     (map_get_terrain(x+1, y+1) != T_OCEAN)) {
    map_set_special(x, y, S_RIVER);
    return;
  }

  /* check east */
  if((map_get_special(x-1, y) & S_RIVER) &&
     (map_get_terrain(x-1, y-1) != T_OCEAN) &&
     (map_get_terrain(x-1, y+1) != T_OCEAN)) {
    map_set_special(x, y, S_RIVER);
    return;
  }

  /* check west */
  if((map_get_special(x+1, y) & S_RIVER) &&
     (map_get_terrain(x+1, y-1) != T_OCEAN) &&
     (map_get_terrain(x+1, y+1) != T_OCEAN)) {
    map_set_special(x, y, S_RIVER);
    return;
  }
}

/**************************************************************************
  Checks for terrain change between ocean and land.  Handles side-effects.
  (Should be called after any potential ocean/land terrain changes.)
  Also, returns an enum ocean_land_change, describing the change, if any.
**************************************************************************/
enum ocean_land_change { OLC_NONE, OLC_OCEAN_TO_LAND, OLC_LAND_TO_OCEAN };

static enum ocean_land_change check_terrain_ocean_land_change(int x, int y,
					      enum tile_terrain_type oldter)
{
  enum tile_terrain_type newter = map_get_terrain(x, y);

  if ((oldter == T_OCEAN) && (newter != T_OCEAN)) {
    /* ocean to land ... */
    ocean_to_land_fix_rivers(x, y);
    city_landlocked_sell_coastal_improvements(x, y);
    assign_continent_numbers();
    gamelog(GAMELOG_MAP, "(%d,%d) land created from ocean", x, y);
    return OLC_OCEAN_TO_LAND;
  } else if ((oldter != T_OCEAN) && (newter == T_OCEAN)) {
    /* land to ocean ... */
    assign_continent_numbers();
    gamelog(GAMELOG_MAP, "(%d,%d) ocean created from land", x, y);
    return OLC_LAND_TO_OCEAN;
  }
  return OLC_NONE;
}

/**************************************************************************
  progress settlers in their current tasks, 
  and units that is pillaging.
  also move units that is on a goto.
  restore unit move points (information needed for settler tasks)
**************************************************************************/
static void update_unit_activity(struct player *pplayer, struct unit *punit,
				 struct genlist_iterator *iter)
{
  int id = punit->id;
  int mr = get_unit_type (punit->type)->move_rate;
  int unit_activity_done = 0;
  int activity = punit->activity;
  enum ocean_land_change solvency = OLC_NONE;
  struct tile *ptile = map_get_tile(punit->x, punit->y);

  /* to allow a settler to begin a task with no moves left
     without it counting toward the time to finish */
  if (punit->moves_left){
    punit->activity_count += mr/3;
  }

  unit_restore_movepoints(pplayer, punit);

  if (punit->connecting && !can_unit_do_activity(punit, activity)) {
    punit->activity_count = 0;
    do_unit_goto(punit, get_activity_move_restriction(activity), 0);
  }

  /* if connecting, automagically build prerequisities first */
  if (punit->connecting && activity == ACTIVITY_RAILROAD &&
      !(map_get_tile(punit->x, punit->y)->special & S_ROAD)) {
    activity = ACTIVITY_ROAD;
  }

  if (activity == ACTIVITY_EXPLORE) {
    int more_to_explore = ai_manage_explorer(punit);
    if (more_to_explore && player_find_unit_by_id(pplayer, id))
      handle_unit_activity_request(punit, ACTIVITY_EXPLORE);
    else
      return;
  }

  if (activity==ACTIVITY_PILLAGE) {
    if (punit->activity_target == 0) {     /* case for old save files */
      if (punit->activity_count >= 1) {
	int what =
	  get_preferred_pillage(
	    get_tile_infrastructure_set(
	      map_get_tile(punit->x, punit->y)));
	if (what != S_NO_SPECIAL) {
	  map_clear_special(punit->x, punit->y, what);
	  send_tile_info(0, punit->x, punit->y);
	  set_unit_activity(punit, ACTIVITY_IDLE);
	}
      }
    }
    else if (total_activity_targeted (punit->x, punit->y,
				      ACTIVITY_PILLAGE, punit->activity_target) >= 1) {
      int what_pillaged = punit->activity_target;
      map_clear_special(punit->x, punit->y, what_pillaged);
      unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2)
        if ((punit2->activity == ACTIVITY_PILLAGE) &&
	    (punit2->activity_target == what_pillaged)) {
	  set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2);
	}
      unit_list_iterate_end;
      send_tile_info(0, punit->x, punit->y);
    }
  }

  if (activity==ACTIVITY_POLLUTION) {
    if (total_activity (punit->x, punit->y, ACTIVITY_POLLUTION) >= 3) {
      map_clear_special(punit->x, punit->y, S_POLLUTION);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_FALLOUT) {
    if (total_activity (punit->x, punit->y, ACTIVITY_FALLOUT) >= 3) {
      map_clear_special(punit->x, punit->y, S_FALLOUT);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_FORTRESS) {
    if (total_activity (punit->x, punit->y, ACTIVITY_FORTRESS) >= 3) {
      map_set_special(punit->x, punit->y, S_FORTRESS);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_AIRBASE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_AIRBASE) >= 3) {
      map_set_special(punit->x, punit->y, S_AIRBASE);
      unit_activity_done = 1;
    }
  }
  
  if (activity==ACTIVITY_IRRIGATE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_IRRIGATE) >=
        map_build_irrigation_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_irrigate_tile(punit->x, punit->y);
      solvency = check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_ROAD) {
    if (total_activity (punit->x, punit->y, ACTIVITY_ROAD)
	+ total_activity (punit->x, punit->y, ACTIVITY_RAILROAD) >=
        map_build_road_time(punit->x, punit->y)) {
      map_set_special(punit->x, punit->y, S_ROAD);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_RAILROAD) {
    if (total_activity (punit->x, punit->y, ACTIVITY_RAILROAD) >= 3) {
      map_set_special(punit->x, punit->y, S_RAILROAD);
      unit_activity_done = 1;
    }
  }
  
  if (activity==ACTIVITY_MINE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_MINE) >=
        map_build_mine_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_mine_tile(punit->x, punit->y);
      solvency = check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_TRANSFORM) {
    if (total_activity (punit->x, punit->y, ACTIVITY_TRANSFORM) >=
        map_transform_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_transform_tile(punit->x, punit->y);
      solvency = check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = 1;
    }
  }

  if (unit_activity_done) {
    send_tile_info(0, punit->x, punit->y);
    unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2)
      if (punit2->activity == activity) {
	if (punit2->connecting) {
	  punit2->activity_count = 0;
	  do_unit_goto(punit2, get_activity_move_restriction(activity), 0);
	}
	else {
	  set_unit_activity(punit2, ACTIVITY_IDLE);
	}
	send_unit_info(0, punit2);
      }
    unit_list_iterate_end;
  }

  if (activity==ACTIVITY_FORTIFYING) {
    if (punit->activity_count >= 1) {
      set_unit_activity(punit,ACTIVITY_FORTIFIED);
    }
  }

  if (activity==ACTIVITY_GOTO) {
    if (!punit->ai.control && (!is_military_unit(punit) ||
       punit->ai.passenger || !pplayer->ai.control)) {
/* autosettlers otherwise waste time; idling them breaks assignment */
/* Stalling infantry on GOTO so I can see where they're GOing TO. -- Syela */
      do_unit_goto(punit, GOTO_MOVE_ANY, 1);
    }
    return;
  }

  if (punit->activity == ACTIVITY_PATROL)
    goto_route_execute(punit);

  send_unit_info(0, punit);

  /* Any units that landed in water or boats that landed on land as a
     result of settlers changing terrain must be moved back into their
     right environment.
     We advance the unit_list iterator passed into this routine from
     update_unit_activities() if we delete the unit it points to.
     We go to START each time we moved a unit to avoid problems with the
     tile unit list getting corrupted.

     FIXME:  We shouldn't do this at all!  There seems to be another
     bug which is expressed when units wind up on the "wrong" terrain;
     this is the bug that should be fixed.  Also, introduction of the
     "amphibious" movement category would allow the definition of units
     that can "safely" change land<->ocean -- in which case all others
     would (here) be summarily disbanded (suicide to accomplish their
     task, for the greater good :).   --jjm
  */
 START:
  switch (solvency) {
  case OLC_NONE:
    break; /* nothing */

  case OLC_LAND_TO_OCEAN:
    unit_list_iterate(ptile->units, punit2) {
      int x, y;
      if (is_ground_unit(punit2)) {
	/* look for nearby land */
	for (x = punit2->x-1; x <= punit2->x+1; x++)
	  for (y = punit2->y-1; y <= punit2->y+1; y++) {
	    struct tile *ptile2;
	    x = map_adjust_x(x);
	    y = map_adjust_y(y);
	    ptile2 = map_get_tile(x, y);
	    if (ptile2->terrain != T_OCEAN
		&& !is_non_allied_unit_tile(ptile2, punit2->owner)) {
	      if (get_transporter_capacity(punit2))
		sentry_transported_idle_units(punit2);
	      freelog(LOG_VERBOSE,
		      "Moved %s's %s due to changing land to sea at (%d, %d).",
		      unit_owner(punit2)->name, unit_name(punit2->type),
		      punit2->x, punit2->y);
	      notify_player(unit_owner(punit2),
			    _("Game: Moved your %s due to changing"
			      " land to sea at (%d, %d)."),
			    unit_name(punit2->type), punit2->x, punit2->y);
	      move_unit(punit2, x, y, 1, 0, 0);
	      if (punit2->activity == ACTIVITY_SENTRY)
		handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	      goto START;
	    }
	  }
	/* look for nearby transport */
	for (x = punit2->x-1; x <= punit2->x+1; x++)
	  for (y = punit2->y-1; y <= punit2->y+1; y++) {
	    struct tile *ptile2;
	    x = map_adjust_x(x);
	    y = map_adjust_y(y);
	    ptile2 = map_get_tile(x, y);
	    if (ptile2->terrain == T_OCEAN
		&& ground_unit_transporter_capacity(x, y, punit2->owner) > 0) {
	      if (get_transporter_capacity(punit2))
		sentry_transported_idle_units(punit2);
	      freelog(LOG_VERBOSE,
		      "Embarked %s's %s due to changing land to sea at (%d, %d).",
		      unit_owner(punit2)->name, unit_name(punit2->type),
		      punit2->x, punit2->y);
	      notify_player(unit_owner(punit2),
			    _("Game: Embarked your %s due to changing"
			      " land to sea at (%d, %d)."),
			    unit_name(punit2->type), punit2->x, punit2->y);
	      move_unit(punit2, x, y, 1, 0, 0);
	      if (punit2->activity == ACTIVITY_SENTRY)
		handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	      goto START;
	    }
	  }
	/* if we get here we could not move punit2 */
	freelog(LOG_VERBOSE,
		"Disbanded %s's %s due to changing land to sea at (%d, %d).",
		unit_owner(punit2)->name, unit_name(punit2->type),
		punit2->x, punit2->y);
	notify_player(unit_owner(punit2),
		      _("Game: Disbanded your %s due to changing"
			" land to sea at (%d, %d)."),
		      unit_name(punit2->type), punit2->x, punit2->y);
	if (iter && ((struct unit*)ITERATOR_PTR((*iter))) == punit2)
	  ITERATOR_NEXT((*iter));
	wipe_unit_spec_safe(punit2, NULL, 0);
	goto START;
      }
    } unit_list_iterate_end;
    break;
  case OLC_OCEAN_TO_LAND:
    unit_list_iterate(ptile->units, punit2) {
      int x, y;
      if (is_sailing_unit(punit2)) {
	/* look for nearby water */
	for (x = punit2->x-1; x <= punit2->x+1; x++)
	  for (y = punit2->y-1; y <= punit2->y+1; y++) {
	    struct tile *ptile2;
	    x = map_adjust_x(x);
	    y = map_adjust_y(y);
	    ptile2 = map_get_tile(x, y);
	    if (ptile2->terrain == T_OCEAN
		&& !is_non_allied_unit_tile(ptile2, punit2->owner)) {
	      if (get_transporter_capacity(punit2))
		sentry_transported_idle_units(punit2);
	      freelog(LOG_VERBOSE,
		      "Moved %s's %s due to changing sea to land at (%d, %d).",
		      unit_owner(punit2)->name, unit_name(punit2->type),
		      punit2->x, punit2->y);
	      notify_player(unit_owner(punit2),
			    _("Game: Moved your %s due to changing"
			      " sea to land at (%d, %d)."),
			    unit_name(punit2->type), punit2->x, punit2->y);
	      move_unit(punit2, x, y, 1, 1, 0);
	      if (punit2->activity == ACTIVITY_SENTRY)
		handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	      goto START;
	    }
	  }
	/* look for nearby port */
	for (x = punit2->x-1; x <= punit2->x+1; x++)
	  for (y = punit2->y-1; y <= punit2->y+1; y++) {
	    struct tile *ptile2;
	    x = map_adjust_x(x);
	    y = map_adjust_y(y);
	    ptile2 = map_get_tile(x, y);
	    if (is_allied_city_tile(ptile2, punit2->owner)
		&& !is_non_allied_unit_tile(ptile2, punit2->owner)) {
	      if (get_transporter_capacity(punit2))
		sentry_transported_idle_units(punit2);
	      freelog(LOG_VERBOSE,
		      "Docked %s's %s due to changing sea to land at (%d, %d).",
		      unit_owner(punit2)->name, unit_name(punit2->type),
		      punit2->x, punit2->y);
	      notify_player(unit_owner(punit2),
			    _("Game: Docked your %s due to changing"
			      " sea to land at (%d, %d)."),
			    unit_name(punit2->type), punit2->x, punit2->y);
	      move_unit(punit2, x, y, 1, 1, 0);
	      if (punit2->activity == ACTIVITY_SENTRY)
		handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	      goto START;
	    }
	  }
	/* if we get here we could not move punit2 */
	freelog(LOG_VERBOSE,
		"Disbanded %s's %s due to changing sea to land at (%d, %d).",
		unit_owner(punit2)->name, unit_name(punit2->type),
		punit2->x, punit2->y);
	notify_player(unit_owner(punit2),
		      _("Game: Disbanded your %s due to changing"
			" sea to land at (%d, %d)."),
		      unit_name(punit2->type), punit2->x, punit2->y);
	if (iter && ((struct unit*)ITERATOR_PTR((*iter))) == punit2)
	  ITERATOR_NEXT((*iter));
	wipe_unit_spec_safe(punit2, NULL, 0);
	goto START;
      }
    } unit_list_iterate_end;
    break;
  }
}

/**************************************************************************
 nuke a square
 1) remove all units on the square
 2) half the size of the city on the square
 if it isn't a city square or an ocean square then with 50% chance 
 add some fallout, then notify the client about the changes.
**************************************************************************/
static void do_nuke_tile(int x, int y)
{
  struct unit_list *punit_list;
  struct city *pcity;
  punit_list=&map_get_tile(x, y)->units;
  
  while(unit_list_size(punit_list)) {
    struct unit *punit=unit_list_get(punit_list, 0);
    wipe_unit(punit);
  }

  if((pcity=map_get_city(x,y))) {
    if (pcity->size > 1) { /* size zero cities are ridiculous -- Syela */
      pcity->size/=2;
      auto_arrange_workers(pcity);
      send_city_info(0, pcity);
    }
  } else if ((map_get_terrain(x, y) != T_OCEAN &&
	      map_get_terrain(x, y) < T_UNKNOWN) && myrand(2)) {
    if (game.rgame.nuke_contamination == CONTAMINATION_POLLUTION) {
      if (!(map_get_special(x, y) & S_POLLUTION)) {
	map_set_special(x, y, S_POLLUTION);
	send_tile_info(0, x, y);
      }
    } else {
      if (!(map_get_special(x, y) & S_FALLOUT)) {
	map_set_special(x, y, S_FALLOUT);
	send_tile_info(0, x, y);
      }
    }
  }
}

/**************************************************************************
  nuke all the squares in a 3x3 square around the center of the explosion
**************************************************************************/
void do_nuclear_explosion(int x, int y)
{
  int i,j;
  for (i=0;i<3;i++)
    for (j=0;j<3;j++)
      do_nuke_tile(map_adjust_x(x+i-1),map_adjust_y(y+j-1));
}


/**************************************************************************
Move the unit if possible 
  if the unit has less moves than it costs to enter a square, we roll the dice:
  1) either succeed
  2) or have it's moves set to 0
  a unit can always move atleast once
**************************************************************************/
int try_move_unit(struct unit *punit, int dest_x, int dest_y) 
{
  if (myrand(1+map_move_cost(punit, dest_x, dest_y))>punit->moves_left &&
      punit->moves_left<unit_move_rate(punit)) {
    punit->moves_left=0;
    send_unit_info(&game.players[punit->owner], punit);
  }
  return punit->moves_left;
}

/**************************************************************************
  go by airline, if both cities have an airport and neither has been used this
  turn the unit will be transported by it and have it's moves set to 0
**************************************************************************/
int do_airline(struct unit *punit, int dest_x, int dest_y)
{
  struct city *city1, *city2;
  int src_x = punit->x;
  int src_y = punit->y;

  if (!(city1=map_get_city(src_x, src_y)))
    return 0;
  if (!(city2=map_get_city(dest_x, dest_y)))
    return 0;
  if (!unit_can_airlift_to(punit, city2))
    return 0;
  city1->airlift=0;
  city2->airlift=0;

  notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		   _("Game: %s transported succesfully."),
		   unit_name(punit->type));

  move_unit(punit, dest_x, dest_y, 0, 0, punit->moves_left);

  send_city_info(city_owner(city1), city1);
  send_city_info(city_owner(city2), city2);

  return 1;
}

/**************************************************************************
Returns whether the drop was made or not. Note that it also returns 1 in
the case where the drop was succesfull, but the unit was killed by
barbarians in a hut
**************************************************************************/
int do_paradrop(struct unit *punit, int dest_x, int dest_y)
{
  if (!unit_flag(punit->type, F_PARATROOPERS)) {
    notify_player_ex(unit_owner(punit), punit->x, punit->y, E_NOEVENT,
		     _("Game: This unit type can not be paradropped."));
    return 0;
  }

  if (!can_unit_paradrop(punit))
    return 0;

  if (!map_get_known(dest_x, dest_y, unit_owner(punit))) {
    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		     _("Game: The destination location is not known."));
    return 0;
  }


  if (map_get_terrain(dest_x, dest_y) == T_OCEAN) {
    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		     _("Game: Cannot paradrop into ocean."));
    return 0;    
  }

  {
    int range = get_unit_type(punit->type)->paratroopers_range;
    int distance = real_map_distance(punit->x, punit->y, dest_x, dest_y);
    if (distance > range) {
      notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		       _("Game: The distance to the target (%i) "
			 "is greater than the unit's range (%i)."),
		       distance, range);
      return 0;
    }
  }

  /* FIXME: this is a fog-of-war cheat.
     You get to know if there is an enemy on the tile*/
  if (is_non_allied_unit_tile(map_get_tile(dest_x, dest_y), punit->owner)) {
    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		     _("Game: Cannot paradrop because there are"
		       " enemy units on the destination location."));
    return 0;
  }

  /* All ok */
  {
    int move_cost = get_unit_type(punit->type)->paratroopers_mr_sub;
    punit->paradropped = 1;
    return move_unit(punit, dest_x, dest_y, 0, 0, move_cost);
  }
}

/**************************************************************************
  called when a player conquers a city, remove buildings (not wonders and 
  always palace) with game.razechance% chance, barbarians destroy more
  set the city's shield stock to 0
  FIXME: this should be in citytools
**************************************************************************/
void raze_city(struct city *pcity)
{
  int i, razechance = game.razechance;
  pcity->improvements[B_PALACE]=0;

  /* land barbarians are more likely to destroy city improvements */
  if( is_land_barbarian(&game.players[pcity->owner]) )
    razechance += 30;

  for (i=0;i<game.num_impr_types;i++) {
    if (city_got_building(pcity, i) && !is_wonder(i) 
	&& (myrand(100) < razechance)) {
      pcity->improvements[i]=0;
    }
  }
  if (pcity->shield_stock > 0)
    pcity->shield_stock=0;
  /*  advisor_choose_build(pcity);  we add the civ bug here :)*/
}

/**************************************************************************
  if target has more techs than pplayer, pplayer will get a random of these
  the clients will both be notified.
  I have notified only those with embassies in pplayer's country - adm4
  FIXME: this should be in plrhand
**************************************************************************/
void get_a_tech(struct player *pplayer, struct player *target)
{
  int i;
  int j=0;
  for (i=0;i<game.num_tech_types;i++) {
    if (get_invention(pplayer, i)!=TECH_KNOWN && 
	get_invention(target, i)== TECH_KNOWN) {
      j++;
    }
  }
  if (!j)  {
    if (target->future_tech > pplayer->future_tech) {
      pplayer->future_tech++;
      pplayer->research.researchpoints++;

      notify_player(pplayer, _("Game: You acquire Future Tech %d from %s."),
		    pplayer->future_tech, target->name);
      notify_player(target,
		    _("Game: %s discovered Future Tech. %d in the city."), 
		    pplayer->name, pplayer->future_tech);
      notify_embassies(pplayer, target,
		       _("Game: The %s discovered Future Tech %d from the %s (conq)"),
		       get_nation_name_plural(pplayer->nation),
		       pplayer->future_tech, get_nation_name_plural(target->nation));
    }
    return;
  }
  j=myrand(j)+1;
  for (i=0;i<game.num_tech_types;i++) {
    if (get_invention(pplayer, i)!=TECH_KNOWN && 
	get_invention(target, i)== TECH_KNOWN) 
      j--;
    if (!j) break;
  }
  if (i==game.num_tech_types) {
    freelog(LOG_ERROR, "Bug in get_a_tech");
  }
  gamelog(GAMELOG_TECH,"%s acquire %s from %s",
          get_nation_name_plural(pplayer->nation),
          advances[i].name,
          get_nation_name_plural(target->nation));

  notify_player(pplayer, _("Game: You acquired %s from %s."),
		advances[i].name, target->name); 
  notify_player(target, _("Game: %s discovered %s in the city."), pplayer->name, 
		advances[i].name); 
  notify_embassies(pplayer, target, _("Game: The %s have stolen %s from the %s."),
		   get_nation_name_plural(pplayer->nation),
		   advances[i].name,
		   get_nation_name_plural(target->nation));

  do_conquer_cost(pplayer);
  found_new_tech(pplayer,i,0,1);
}

/**************************************************************************
  finds a spot around pcity and place a partisan.
**************************************************************************/
static void place_partisans(struct city *pcity,int count)
{
  int x,y,i;
  int ok[25];
  int total=0;

  for (i = 0,x = pcity->x -2; x < pcity->x + 3; x++) {
    for (y = pcity->y - 2; y < pcity->y + 3; y++, i++) {
      ok[i]=can_place_partisan(map_adjust_x(x), map_adjust_y(y));
      if(ok[i]) total++;
    }
  }

  while(count && total)  {
    for(i=0,x=myrand(total)+1;x;i++) if(ok[i]) x--;
    ok[--i]=0; x=(i/5)-2+pcity->x; y=(i%5)-2+pcity->y;
    create_unit(&game.players[pcity->owner], map_adjust_x(x), map_adjust_y(y),
		get_role_unit(L_PARTISAN,0), 0, 0, -1);
    count--; total--;
  }
}

/**************************************************************************
  if requirements to make partisans when a city is conquered is fullfilled
  this routine makes a lot of partisans based on the city's size.
  To be candidate for partisans the following things must be satisfied:
  1) Guerilla warfare must be known by atleast 1 player
  2) The owner of the city is the original player.
  3) The player must know about communism and gunpowder
  4) the player must run either a democracy or a communist society.
**************************************************************************/
void make_partisans(struct city *pcity)
{
  struct player *pplayer;
  int i, partisans;

  if (num_role_units(L_PARTISAN)==0)
    return;
  if (!tech_exists(game.rtech.u_partisan)
      || !game.global_advances[game.rtech.u_partisan]
      || pcity->original != pcity->owner)
    return;

  if (!government_has_flag(get_gov_pcity(pcity), G_INSPIRES_PARTISANS))
    return;
  
  pplayer = city_owner(pcity);
  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    int tech = game.rtech.partisan_req[i];
    if (tech == A_LAST) break;
    if (get_invention(pplayer, tech) != TECH_KNOWN) return;
    /* Was A_COMMUNISM and A_GUNPOWDER */
  }
  
  partisans = myrand(1 + pcity->size/2) + 1;
  if (partisans > 8) 
    partisans = 8;
  
  place_partisans(pcity,partisans);
}

/**************************************************************************
We remove the unit and see if it's disapperance have affected the homecity
and the city it was in.
**************************************************************************/
static void server_remove_unit(struct unit *punit)
{
  struct packet_generic_integer packet;
  struct city *pcity = map_get_city(punit->x, punit->y);
  struct city *phomecity = find_city_by_id(punit->homecity);
  int punit_x = punit->x, punit_y = punit->y;

  remove_unit_sight_points(punit);

  if (punit->pgr) {
    free(punit->pgr->pos);
    free(punit->pgr);
  }

  packet.value = punit->id;
  /* FIXME: maybe we should only send to those players who can see the unit,
     as the client automatically removes any units in a fogged square, and
     the send_unit_info() only sends units who are in non-fogged square.
     Leaving for now. */
  lsend_packet_generic_integer(&game.game_connections, PACKET_REMOVE_UNIT,
			       &packet);

  game_remove_unit(punit->id);  

  /* This unit may have blocked tiles of adjacent cities. Update them. */
  map_city_radius_iterate(punit_x, punit_y, x, y) {
    struct city *pcity2 = map_get_city(x, y);
    if (pcity2 && pcity2->owner != punit->owner) {
      if (city_check_workers(pcity2, 1)) {
	send_city_info(city_owner(pcity2), pcity2);
      }
    }
  } map_city_radius_iterate_end;

  if (phomecity) {
    city_refresh(phomecity);
    send_city_info(get_player(phomecity->owner), phomecity);
  }
  if (pcity && pcity != phomecity) {
    city_refresh(pcity);
    send_city_info(get_player(pcity->owner), pcity);
  }
}

/**************************************************************************
this is a highlevel routine
Remove the unit, and passengers if it is a carrying any.
Remove the _minimum_ number, eg there could be another boat on the square.
Parameter iter, if non-NULL, should be an iterator for a unit list,
and if it points to a unit which we wipe, we advance it first to
avoid dangling pointers.
NOTE: iter should not be an iterator for the map units list, but
city supported, or player units, is ok.
**************************************************************************/
void wipe_unit_spec_safe(struct unit *punit, struct genlist_iterator *iter,
			 int wipe_cargo)
{
  /* No need to remove air units as they can still fly away */
  if (is_ground_units_transport(punit)
      && map_get_terrain(punit->x, punit->y)==T_OCEAN
      && wipe_cargo) {
    int x = punit->x;
    int y = punit->y;
    int playerid = punit->owner;

    int capacity = ground_unit_transporter_capacity(x, y, playerid);
    capacity -= get_transporter_capacity(punit);

    unit_list_iterate(map_get_tile(x, y)->units, pcargo) {
      if (capacity >= 0)
	break;
      if (is_ground_unit(pcargo)) {
	if (iter && ((struct unit*)ITERATOR_PTR((*iter))) == pcargo) {
	  freelog(LOG_DEBUG, "iterating over %s in wipe_unit_safe",
		  unit_name(pcargo->type));
	  ITERATOR_NEXT((*iter));
	}
	notify_player_ex(get_player(playerid), x, y, E_UNIT_LOST,
			 _("Game: %s lost when %s was lost."),
			 get_unit_type(pcargo->type)->name,
			 get_unit_type(punit->type)->name);
	gamelog(GAMELOG_UNITL, "%s lose %s when %s lost", 
		get_nation_name_plural(game.players[playerid].nation),
		get_unit_type(pcargo->type)->name,
		get_unit_type(punit->type)->name);
	server_remove_unit(pcargo);
	capacity++;
      }
    } unit_list_iterate_end;
  }

  server_remove_unit(punit);
}

/**************************************************************************
...
**************************************************************************/

void wipe_unit_safe(struct unit *punit, struct genlist_iterator *iter){
  wipe_unit_spec_safe(punit, iter, 1);
}

/**************************************************************************
...
**************************************************************************/
void wipe_unit(struct unit *punit)
{
  wipe_unit_safe(punit, NULL);
}

/**************************************************************************
this is a highlevel routine
the unit has been killed in combat => all other units on the
tile dies unless ...
**************************************************************************/
void kill_unit(struct unit *pkiller, struct unit *punit)
{
  struct city   *incity    = map_get_city(punit->x, punit->y);
  struct player *pplayer   = get_player(punit->owner);
  struct player *destroyer = get_player(pkiller->owner);
  char *loc_str = get_location_str_in(pplayer, punit->x, punit->y);
  int num_killed[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  int ransom, unitcount = 0;
  
  /* barbarian leader ransom hack */
  if( is_barbarian(pplayer) && unit_has_role(punit->type, L_BARBARIAN_LEADER)
      && (unit_list_size(&(map_get_tile(punit->x, punit->y)->units)) == 1)
      && (is_ground_unit(pkiller) || is_heli_unit(pkiller)) ) {
    ransom = (pplayer->economic.gold >= 100)?100:pplayer->economic.gold;
    notify_player_ex(destroyer, pkiller->x, pkiller->y, E_UNIT_WIN_ATT,
		     _("Game: Barbarian leader captured, %d gold ransom paid."),
                     ransom);
    destroyer->economic.gold += ransom;
    pplayer->economic.gold -= ransom;
    send_player_info(destroyer,0);   /* let me see my new gold :-) */
    unitcount = 1;
  }

  if (!unitcount) {
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, vunit)
      if (players_at_war(pkiller->owner, vunit->owner))
	unitcount++;
    unit_list_iterate_end;
  }

  if( (incity) ||
      (map_get_special(punit->x, punit->y)&S_FORTRESS) ||
      (map_get_special(punit->x, punit->y)&S_AIRBASE) ||
      unitcount == 1) {
    notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST,
		     _("Game: %s lost to an attack by %s's %s%s."),
		     get_unit_type(punit->type)->name, destroyer->name,
		     unit_name(pkiller->type), loc_str);
    gamelog(GAMELOG_UNITL, "%s lose %s to the %s",
	    get_nation_name_plural(pplayer->nation),
	    get_unit_type(punit->type)->name,
	    get_nation_name_plural(destroyer->nation));

    wipe_unit(punit);
  } else { /* unitcount > 1 */
    int i;
    if (!(unitcount > 1)) {
      freelog(LOG_FATAL, "Error in kill_unit, unitcount is %i", unitcount);
      abort();
    }
    /* initialize */
    for (i = 0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
      num_killed[i] = 0;
    }

    /* count killed units */
    unit_list_iterate((map_get_tile(punit->x ,punit->y)->units), vunit)
      if (players_at_war(pkiller->owner, vunit->owner))
	num_killed[vunit->owner]++;
    unit_list_iterate_end;

    /* inform the owners */
    for (i = 0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
      if (num_killed[i]>0) {
	notify_player_ex(get_player(i), punit->x, punit->y, E_UNIT_LOST,
			 _("Game: You lost %d units to an attack"
			   " from %s's %s%s."),
			 num_killed[i], destroyer->name,
			 unit_name(pkiller->type), loc_str);
      }
    }

    /* remove the units */
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
      if (players_at_war(pkiller->owner, punit2->owner)) {
	notify_player_ex(unit_owner(punit2), 
			 punit2->x, punit2->y, E_UNIT_LOST,
			 _("Game: %s lost to an attack"
			   " from %s's %s."),
			 get_unit_type(punit2->type)->name, destroyer->name,
			 unit_name(pkiller->type));
	gamelog(GAMELOG_UNITL, "%s lose %s to the %s",
		get_nation_name_plural(get_player(punit2->owner)->nation),
		get_unit_type(punit2->type)->name,
		get_nation_name_plural(destroyer->nation));
	wipe_unit_spec_safe(punit2, NULL, 0);
      }
    }
    unit_list_iterate_end;
  }
}

/**************************************************************************
  Send the unit into to those connections in dest which can see the units
  position, or the specified (x,y) (if different).
  Eg, use x and y as where the unit came from, so that the info can be
  sent if the other players can see either the target or destination tile.
  dest = NULL means all connections (game.game_connections)
**************************************************************************/
void send_unit_info_to_onlookers(struct conn_list *dest, struct unit *punit,
				 int x, int y, int carried, int select_it)
{
  struct packet_unit_info info;

  if (dest==NULL) dest = &game.game_connections;
  
  package_unit(punit, &info, carried, select_it,
	       UNIT_INFO_IDENTITY, 0, FALSE);

  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    if (pplayer==NULL && !pconn->observer) continue;
    if (pplayer==NULL
	|| map_get_known_and_seen(info.x, info.y, pplayer->player_no)
	|| map_get_known_and_seen(x, y, pplayer->player_no)) {
      send_packet_unit_info(pconn, &info);
    }
  }
  conn_list_iterate_end;
}

/**************************************************************************
  send the unit to the players who need the info 
  dest = NULL means all connections (game.game_connections)
**************************************************************************/
void send_unit_info(struct player *dest, struct unit *punit)
{
  struct conn_list *conn_dest = (dest ? &dest->connections
				 : &game.game_connections);
  send_unit_info_to_onlookers(conn_dest, punit, punit->x, punit->y, 0, 0);
}

/**************************************************************************
...
**************************************************************************/
void advance_unit_focus(struct unit *punit)
{
  conn_list_iterate(unit_owner(punit)->connections, pconn) {
    struct packet_generic_integer packet;
    packet.value = punit->id;
    send_packet_generic_integer(pconn, PACKET_ADVANCE_FOCUS, &packet);
  } conn_list_iterate_end;
}

/**************************************************************************
  Returns a pointer to a (static) string which gives an informational
  message about location (x,y), in terms of cities known by pplayer.
  One of:
    "in Foo City"  or  "at Foo City" (see below)
    "outside Foo City"
    "near Foo City"
    "" (if no cities known)
  There are two variants for the first case, one when something happens
  inside the city, otherwise when it happens "at" but "outside" the city.
  Eg, when an attacker fails, the attacker dies "at" the city, but
  not "in" the city (since the attacker never made it in).
  Don't call this function directly; use the wrappers below.
**************************************************************************/
static char *get_location_str(struct player *pplayer, int x, int y, int use_at)
{
  static char buffer[MAX_LEN_NAME+64];
  struct city *incity, *nearcity;

  incity = map_get_city(x, y);
  if (incity) {
    if (use_at) {
      my_snprintf(buffer, sizeof(buffer), _(" at %s"), incity->name);
    } else {
      my_snprintf(buffer, sizeof(buffer), _(" in %s"), incity->name);
    }
  } else {
    nearcity = dist_nearest_city(pplayer, x, y, 0, 0);
    if (nearcity) {
      if (is_tiles_adjacent(x, y, nearcity->x, nearcity->y)) {
	my_snprintf(buffer, sizeof(buffer),
		   _(" outside %s"), nearcity->name);
      } else {
	my_snprintf(buffer, sizeof(buffer),
		    _(" near %s"), nearcity->name);
      }
    } else {
      buffer[0] = '\0';
    }
  }
  return buffer;
}

/**************************************************************************
  See get_location_str() above.
**************************************************************************/
char *get_location_str_in(struct player *pplayer, int x, int y)
{
  return get_location_str(pplayer, x, y, 0);
}

/**************************************************************************
  See get_location_str() above.
**************************************************************************/
char *get_location_str_at(struct player *pplayer, int x, int y)
{
  return get_location_str(pplayer, x, y, 1);
}

/**************************************************************************
...
**************************************************************************/
enum goto_move_restriction get_activity_move_restriction(enum unit_activity activity)
{
  enum goto_move_restriction restr;

  switch (activity) {
  case ACTIVITY_IRRIGATE:
    restr = GOTO_MOVE_CARDINAL_ONLY;
    break;
  case ACTIVITY_POLLUTION:
  case ACTIVITY_ROAD:
  case ACTIVITY_MINE:
  case ACTIVITY_FORTRESS:
  case ACTIVITY_RAILROAD:
  case ACTIVITY_PILLAGE:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_AIRBASE:
  case ACTIVITY_FALLOUT:
    restr = GOTO_MOVE_STRAIGHTEST;
    break;
  default:
    restr = GOTO_MOVE_ANY;
    break;
  }

  return (restr);
}

/**************************************************************************
  For each specified connections, send information about all the units
  known to that player/conn.
**************************************************************************/
void send_all_known_units(struct conn_list *dest)
{
  int p;
  
  conn_list_do_buffer(dest);
  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    if (pconn->player==NULL && !pconn->observer) {
      continue;
    }
    for(p=0; p<game.nplayers; p++) { /* send the players units */
      struct player *unitowner = &game.players[p];
      unit_list_iterate(unitowner->units, punit) {
	if (pplayer==NULL
	    || map_get_known_and_seen(punit->x, punit->y, pplayer->player_no)) {
	  send_unit_info_to_onlookers(&pconn->self, punit,
				      punit->x, punit->y, 0, 0);
	}
      }
      unit_list_iterate_end;
    }
  }
  conn_list_iterate_end;
  conn_list_do_unbuffer(dest);
  flush_packets();
}

/**************************************************************************
...
**************************************************************************/
void upgrade_unit(struct unit *punit, Unit_Type_id to_unit)
{
  struct player *pplayer = &game.players[punit->owner];
  int range = get_unit_type(punit->type)->vision_range;

  if (punit->hp==get_unit_type(punit->type)->hp) {
    punit->hp=get_unit_type(to_unit)->hp;
  }

  conn_list_do_buffer(&pplayer->connections);
  punit->type = to_unit;
  unfog_area(pplayer,punit->x,punit->y, get_unit_type(to_unit)->vision_range);
  fog_area(pplayer,punit->x,punit->y,range);
  send_unit_info(0, punit);
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
For all units which are transported by the given unit and that are
currently idle, sentry them.
**************************************************************************/
static void sentry_transported_idle_units(struct unit *ptrans)
{
  int x = ptrans->x;
  int y = ptrans->y;
  struct tile *ptile = map_get_tile(x, y);

  unit_list_iterate(ptile->units, pcargo) {
    if (pcargo->transported_by == ptrans->id
	&& pcargo->id != ptrans->id
	&& pcargo->activity == ACTIVITY_IDLE) {
      pcargo->activity = ACTIVITY_SENTRY;
      send_unit_info(unit_owner(pcargo), pcargo);
    }
  } unit_list_iterate_end;
}

/**************************************************************************
Assigns units on ptrans' tile to ptrans if they should be. This is done by
setting their transported_by fields to the id of ptrans.
Checks a zillion things, some from situations that should never happen.
First drop all previously assigned units that do not fit on the transport.
If on land maybe pick up some extra units (decided by take_from_land variable)

This function is getting a bit larger and ugly. Perhaps it would be nicer
if it was recursive!?

FIXME: If in the open (not city), and leaving with only those units that are
       already assigned to us would strand some units try to reassign the
       transports. This reassign sometimes makes more changes than it needs to.

       Groundunit ground unit transports moving to and from ships never take
       units with them. This is ok, but not always practical.
**************************************************************************/
void assign_units_to_transporter(struct unit *ptrans, int take_from_land)
{
  int x = ptrans->x;
  int y = ptrans->y;
  int playerid = ptrans->owner;
  int capacity = get_transporter_capacity(ptrans);
  struct tile *ptile = map_get_tile(x, y);

  /*** FIXME: We kludge AI compatability problems with the new code away here ***/
  if (is_sailing_unit(ptrans)
      && is_ground_units_transport(ptrans)
      && unit_owner(ptrans)->ai.control) {
    unit_list_iterate(ptile->units, pcargo) {
      if (pcargo->owner == playerid) {
	pcargo->transported_by = -1;
      }
    } unit_list_iterate_end;

    unit_list_iterate(ptile->units, pcargo) {
      if ((ptile->terrain == T_OCEAN || pcargo->activity == ACTIVITY_SENTRY)
	  && capacity > 0
	  && is_ground_unit(pcargo)
	  && pcargo->owner == playerid) {
	pcargo->transported_by = ptrans->id;
	capacity--;
      }
    } unit_list_iterate_end;
    return;
  }

  /*** Ground units transports first ***/
  if (is_ground_units_transport(ptrans)) {
    int available = ground_unit_transporter_capacity(x, y, playerid);

    /* See how many units are dedicated to this transport, and remove extra units */
    unit_list_iterate(ptile->units, pcargo) {
      if (pcargo->transported_by == ptrans->id) {
	if (is_ground_unit(pcargo)
	    && capacity > 0
	    && (ptile->terrain == T_OCEAN || pcargo->activity == ACTIVITY_SENTRY)
	    && pcargo->owner == playerid
	    && pcargo->id != ptrans->id
	    && !(is_ground_unit(ptrans) && (ptile->terrain == T_OCEAN
					    || is_ground_units_transport(pcargo))))
	  capacity--;
	else
	  pcargo->transported_by = -1;
      }
    } unit_list_iterate_end;

    /** We are on an ocean tile. All units must get a transport **/
    if (ptile->terrain == T_OCEAN) {
      /* While the transport is not full and units will strand if we don't take
	 them with we us dedicate them to this transport. */
      if (available - capacity < 0 && !is_ground_unit(ptrans)) {
	unit_list_iterate(ptile->units, pcargo) {
	  if (capacity == 0)
	    break;
	  if (is_ground_unit(pcargo)
	      && pcargo->transported_by != ptrans->id
	      && pcargo->owner == playerid) {
	    capacity--;
	    pcargo->transported_by = ptrans->id;
	  }
	} unit_list_iterate_end;
      }
    } else { /** We are on a land tile **/
      /* If ordered to do so we take extra units with us, provided they
	 are not already commited to another transporter on the tile */
      if (take_from_land) {
	unit_list_iterate(ptile->units, pcargo) {
	  if (capacity == 0)
	    break;
	  if (is_ground_unit(pcargo)
	      && pcargo->transported_by != ptrans->id
	      && pcargo->activity == ACTIVITY_SENTRY
	      && pcargo->id != ptrans->id
	      && pcargo->owner == playerid
	      && !(is_ground_unit(ptrans) && (ptile->terrain == T_OCEAN
					      || is_ground_units_transport(pcargo)))) {
	    int has_trans = 0;
	    unit_list_iterate(ptile->units, ptrans2) {
	      if (ptrans2->id == pcargo->transported_by)
		has_trans = 1;
	    } unit_list_iterate_end;
	    if (!has_trans) {
	      capacity--;
	      pcargo->transported_by = ptrans->id;
	    }
	  }
	} unit_list_iterate_end;
      }
    }
    return;
    /*** Allocate air and missile units ***/
  } else if (is_air_units_transport(ptrans)) {
    struct player_tile *plrtile = map_get_player_tile(x, y, playerid);
    int is_refuel_point = is_allied_city_tile(map_get_tile(x, y), playerid)
      || (plrtile->special&S_AIRBASE
	  && !is_non_allied_unit_tile(map_get_tile(x, y), playerid));
    int missiles_only = unit_flag(ptrans->type, F_MISSILE_CARRIER)
      && !unit_flag(ptrans->type, F_CARRIER);

    /* Make sure we can transport the units marked as being transported by ptrans */
    unit_list_iterate(ptile->units, pcargo) {
      if (ptrans->id == pcargo->transported_by) {
	if (pcargo->owner == playerid
	    && pcargo->id != ptrans->id
	    && (!is_sailing_unit(pcargo))
	    && (unit_flag(pcargo->type, F_MISSILE) || !missiles_only)
	    && !(is_ground_unit(ptrans) && ptile->terrain == T_OCEAN)
	    && (capacity > 0)) {
	  if (is_air_unit(pcargo))
	    capacity--;
	  /* Ground units are handled below */
	} else
	  pcargo->transported_by = -1;
      }
    } unit_list_iterate_end;

    /** We are at a refuel point **/
    if (is_refuel_point) {
      unit_list_iterate(ptile->units, pcargo) {
	if (capacity == 0)
	  break;
	if (is_air_unit(pcargo)
	    && pcargo->id != ptrans->id
	    && pcargo->transported_by != ptrans->id
	    && pcargo->activity == ACTIVITY_SENTRY
	    && (unit_flag(pcargo->type, F_MISSILE) || !missiles_only)
	    && pcargo->owner == playerid) {
	  int has_trans = 0;
	  unit_list_iterate(ptile->units, ptrans2) {
	    if (ptrans2->id == pcargo->transported_by)
	      has_trans = 1;
	  } unit_list_iterate_end;
	  if (!has_trans) {
	    capacity--;
	    pcargo->transported_by = ptrans->id;
	  }
	}
      } unit_list_iterate_end;
    } else { /** We are in the open. All units must have a transport if possible **/
      int aircap = airunit_carrier_capacity(x, y, playerid);
      int miscap = missile_carrier_capacity(x, y, playerid);

      /* Not enough capacity. Take anything we can */
      if ((aircap < capacity || miscap < capacity)
	  && !(is_ground_unit(ptrans) && ptile->terrain == T_OCEAN)) {
	/* We first take nonmissiles, as missiles can be transported on anything,
	   but nonmissiles can not */
	if (!missiles_only) {
	  unit_list_iterate(ptile->units, pcargo) {
	    if (capacity == 0)
	      break;
	    if (is_air_unit(pcargo)
		&& pcargo->id != ptrans->id
		&& pcargo->transported_by != ptrans->id
		&& !unit_flag(pcargo->type, F_MISSILE)
		&& pcargo->owner == playerid) {
	      capacity--;
	      pcargo->transported_by = ptrans->id;
	    }
	  } unit_list_iterate_end;
	}
	
	/* Just take anything there's left.
	   (which must be missiles if we have capacity left) */
	unit_list_iterate(ptile->units, pcargo) {
	  if (capacity == 0)
	    break;
	  if (is_air_unit(pcargo)
	      && pcargo->id != ptrans->id
	      && pcargo->transported_by != ptrans->id
	      && (!missiles_only || unit_flag(pcargo->type, F_MISSILE))
	      && pcargo->owner == playerid) {
	    capacity--;
	    pcargo->transported_by = ptrans->id;
	  }
	} unit_list_iterate_end;
      }
    }

    /** If any of the transported air units have land units on board take them with us **/
    {
      int totcap = 0;
      int available = ground_unit_transporter_capacity(x, y, playerid);
      struct unit_list trans2s;
      unit_list_init(&trans2s);

      unit_list_iterate(ptile->units, pcargo) {
	if (pcargo->transported_by == ptrans->id
	    && is_ground_units_transport(pcargo)
	    && is_air_unit(pcargo)) {
	  totcap += get_transporter_capacity(pcargo);
	  unit_list_insert(&trans2s, pcargo);
	}
      } unit_list_iterate_end;

      unit_list_iterate(ptile->units, pcargo2) {
	int has_trans = 0;
	unit_list_iterate(trans2s, ptrans2) {
	  if (pcargo2->transported_by == ptrans2->id)
	    has_trans = 1;
	} unit_list_iterate_end;
	if (pcargo2->transported_by == ptrans->id)
	  has_trans = 1;

	if (has_trans
	    && is_ground_unit(pcargo2)) {
	  if (totcap > 0
	      && (ptile->terrain == T_OCEAN || pcargo2->activity == ACTIVITY_SENTRY)
	      && pcargo2->owner == playerid
	      && pcargo2 != ptrans) {
	    pcargo2->transported_by = ptrans->id;
	    totcap--;
	  } else
	    pcargo2->transported_by = -1;
	}
      } unit_list_iterate_end;

      /* Uh oh. Not enough space on the square we leave if we don't
	 take extra units with us */
      if (totcap > available && ptile->terrain == T_OCEAN) {
	unit_list_iterate(ptile->units, pcargo2) {
	  if (is_ground_unit(pcargo2)
	      && totcap > 0
	      && pcargo2->owner == playerid
	      && pcargo2->transported_by != ptrans->id) {
	    pcargo2->transported_by = ptrans->id;
	    totcap--;
	  }
	} unit_list_iterate_end;
      }
    }
  } else {
    unit_list_iterate(ptile->units, pcargo) {
      if (ptrans->id == pcargo->transported_by)
	pcargo->transported_by = -1;
    } unit_list_iterate_end;
    freelog (LOG_VERBOSE, "trying to assign cargo to a non-transporter "
	     "of type %s at %i,%i",
	     get_unit_name(ptrans->type), ptrans->x, ptrans->y);
  }
}

/**************************************************************************
Does: 1) updates  the units homecity and the city it enters/leaves (the
         cities happiness varies). This also takes into account if the
	 unit enters/leaves a fortress.
      2) handles any huts at the units destination.
      3) awakes any sentried units on neightboring tiles.
      4) updates adjacent cities' unavailable tiles.

FIXME: Sometimes it is not neccesary to send cities because the goverment
       doesn't care if a unit is away or not.
**************************************************************************/
static void handle_unit_move_consequences(struct unit *punit, int src_x, int src_y,
					  int dest_x, int dest_y)
{
  struct city *fromcity = map_get_city(src_x, src_y);
  struct city *tocity = map_get_city(dest_x, dest_y);
  struct city *homecity = NULL;
  struct player *pplayer = get_player(punit->owner);
  /*  struct government *g = get_gov_pplayer(pplayer);*/
  int senthome = 0;
  if (punit->homecity)
    homecity = find_city_by_id(punit->homecity);

  wakeup_neighbor_sentries(pplayer, dest_x, dest_y);
  maybe_make_first_contact(dest_x, dest_y, punit->owner);

  if (tocity)
    handle_unit_enter_city(punit, tocity);

  /* We only do this for non-AI players to now make sure the AI turns
     doesn't take too long. Perhaps we should make a special refresh_city
     functions that only refreshed happiness. */
  if (!pplayer->ai.control) {
    /* might have changed owners or may be destroyed */
    tocity = map_get_city(dest_x, dest_y);

    if (tocity) { /* entering a city */
      if (tocity->owner == punit->owner) {
	if (tocity != homecity) {
	  city_refresh(tocity);
	  send_city_info(pplayer, tocity);
	}
      }

      if (homecity) {
	city_refresh(homecity);
	send_city_info(pplayer, homecity);
      }
      senthome = 1;
    }

    if (fromcity && fromcity->owner == punit->owner) { /* leaving a city */
      if (!senthome && homecity) {
	city_refresh(homecity);
	send_city_info(pplayer, homecity);
      }
      if (fromcity != homecity) {
	city_refresh(fromcity);
	send_city_info(pplayer, fromcity);
      }
      senthome = 1;
    }

    /* entering/leaving a fortress */
    if (map_get_tile(dest_x, dest_y)->special&S_FORTRESS
	&& homecity
	&& is_friendly_city_near(punit->owner, dest_x, dest_y)
	&& !senthome) {
      city_refresh(homecity);
      send_city_info(pplayer, homecity);
    }

    if (map_get_tile(src_x, src_y)->special&S_FORTRESS
	&& homecity
	&& is_friendly_city_near(punit->owner, src_x, src_y)
	&& !senthome) {
      city_refresh(homecity);
      send_city_info(pplayer, homecity);
    }
  }


  /* The unit block different tiles of adjacent enemy cities before and
     after. Update the relevant cities, without sending their info twice
     if possible... If the city is in the range of both the source and
     the destination we only update it when checking at the destination. */

  /* First check cities near the source. */
  map_city_radius_iterate(src_x, src_y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);

    int is_near_destination = 0;
    map_city_radius_iterate(dest_x, dest_y, x2, y2) {
      struct city *pcity2 = map_get_city(x2, y2);
      if (pcity == pcity2)
	is_near_destination = 1;
    } map_city_radius_iterate_end;

    if (pcity && pcity->owner != punit->owner
	&& !is_near_destination) {
      if (city_check_workers(pcity, 1)) {
	send_city_info(city_owner(pcity), pcity);
      }
    }
  } map_city_radius_iterate_end;

  /* Then check cities near the destination. */
  map_city_radius_iterate(dest_x, dest_y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity && pcity->owner != punit->owner) {
      if (city_check_workers(pcity, 1)) {
	send_city_info(city_owner(pcity), pcity);
      }
    }
  } map_city_radius_iterate_end;
}

/**************************************************************************
Check if the units activity is legal for a move , and reset it if it isn't.
**************************************************************************/
static void check_unit_activity(struct unit *punit)
{
  if (punit->activity != ACTIVITY_IDLE
      && punit->activity != ACTIVITY_GOTO
      && punit->activity != ACTIVITY_SENTRY
      && punit->activity != ACTIVITY_EXPLORE
      && punit->activity != ACTIVITY_PATROL
      && !punit->connecting)
    set_unit_activity(punit, ACTIVITY_IDLE);
}

/**************************************************************************
Moves a unit. No checks whatsoever! This is meant as a practical function
for other functions, like do_airline, which do the checking themselves.
If you move a unit you should always use this function, as it also sets the
transported_by unit field correctly.
take_from_land is only relevant if you have set transport_units.
Note that the src and dest need not be adjacent.
**************************************************************************/
int move_unit(struct unit *punit, const int dest_x, const int dest_y,
	      int transport_units, int take_from_land, int move_cost)
{
  int src_x = punit->x;
  int src_y = punit->y;
  int playerid = punit->owner;
  struct player *pplayer = get_player(playerid);
  struct tile *psrctile = map_get_tile(src_x, src_y);
  struct tile *pdesttile = map_get_tile(dest_x, dest_y);

  conn_list_do_buffer(&pplayer->connections);

  if (punit->ai.ferryboat) {
    struct unit *ferryboat;
    ferryboat = unit_list_find(&psrctile->units, punit->ai.ferryboat);
    if (ferryboat) {
      freelog(LOG_DEBUG, "%d disembarking from ferryboat %d",
	      punit->id, punit->ai.ferryboat);
      ferryboat->ai.passenger = 0;
      punit->ai.ferryboat = 0;
    }
  }
  /* A transporter should not take units with it when on an attack goto -- fisch */
  if ((punit->activity == ACTIVITY_GOTO) &&
      get_defender(pplayer, punit, punit->goto_dest_x, punit->goto_dest_y) &&
      psrctile->terrain != T_OCEAN) {
    transport_units = 0;
  }

  /* A ground unit cannot hold units on an ocean tile */
  if (transport_units
      && is_ground_unit(punit)
      && pdesttile->terrain == T_OCEAN)
    transport_units = 0;

  /* Make sure we don't accidentally insert units into a transporters list */
  unit_list_iterate(pdesttile->units, pcargo) { 
    if (pcargo->transported_by == punit->id)
      pcargo->transported_by = -1;
  } unit_list_iterate_end;

  /* Transporting units. We first make a list of the units to be moved and
     then insert them again. The way this is done makes sure that the
     units stay in the same order. */
  if (get_transporter_capacity(punit) && transport_units) {
    struct unit_list cargo_units;

    /* First make a list of the units to be moved. */
    unit_list_init(&cargo_units);
    assign_units_to_transporter(punit, take_from_land);
    unit_list_iterate(psrctile->units, pcargo) {
      if (pcargo->transported_by == punit->id) {
	unit_list_unlink(&psrctile->units, pcargo);
	unit_list_insert(&cargo_units, pcargo);
      }
    } unit_list_iterate_end;

    /* Insert them again. */
    unit_list_iterate(cargo_units, pcargo) {
      unfog_area(pplayer, dest_x, dest_y, get_unit_type(pcargo->type)->vision_range);
      pcargo->x = dest_x;
      pcargo->y = dest_y;
      unit_list_insert(&pdesttile->units, pcargo);
      check_unit_activity(pcargo);
      send_unit_info_to_onlookers(0, pcargo, src_x, src_y, 1, 0);
      fog_area(pplayer, src_x, src_y, get_unit_type(pcargo->type)->vision_range);
      handle_unit_move_consequences(pcargo, src_x, src_y, dest_x, dest_y);
    } unit_list_iterate_end;
    unit_list_unlink_all(&cargo_units);
  }

  /* We first unfog the destination, then move the unit and send the move,
     and then fog the old territory. This means that the player gets a chance to
     see the newly explored territory while the client moves the unit, and both
     areas are visible during the move */
  unfog_area(pplayer, dest_x, dest_y, get_unit_type(punit->type)->vision_range);

  unit_list_unlink(&psrctile->units, punit);
  punit->x = dest_x;
  punit->y = dest_y;
  punit->moved = 1;
  punit->transported_by = -1;
  punit->moves_left = MAX(0, punit->moves_left - move_cost);
  unit_list_insert(&pdesttile->units, punit);
  check_unit_activity(punit);

  /* set activity to sentry if boarding a ship */
  if (is_ground_unit(punit) &&
      pdesttile->terrain == T_OCEAN &&
      !(pplayer->ai.control)) {
    set_unit_activity(punit, ACTIVITY_SENTRY);
  }
  send_unit_info_to_onlookers(0, punit, src_x, src_y, 0, 0);
  fog_area(pplayer, src_x, src_y, get_unit_type(punit->type)->vision_range);

  handle_unit_move_consequences(punit, src_x, src_y, dest_x, dest_y);

  conn_list_do_unbuffer(&pplayer->connections);

  if (map_get_tile(dest_x, dest_y)->special&S_HUT)
    return handle_unit_enter_hut(punit);
  else
    return 1;
}
