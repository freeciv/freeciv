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
#include "civserver.h"
#include "gamelog.h"
#include "gotohand.h"
#include "mapgen.h"		/* assign_continent_numbers */
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "unithand.h"
#include "unittools.h"

#include "aiunit.h"
#include "aitools.h"

#include "unitfunc.h"

extern struct move_cost_map warmap;

/****************************************************************************/

static void wakeup_neighbor_sentries(struct player *pplayer,
				     int cent_x, int cent_y);
static void diplomat_charge_movement (struct unit *pdiplomat, int x, int y);
static int diplomat_success_vs_defender (struct unit *pdefender);
static int diplomat_infiltrate_city (struct player *pplayer, struct player *cplayer,
				     struct unit *pdiplomat, struct city *pcity);
static void diplomat_escape (struct player *pplayer, struct unit *pdiplomat,
			     struct city *pcity);

static int upgrade_would_strand(struct unit *punit, int upgrade_type);

/******************************************************************************
  Poison a city's water supply.

  - Only a Spy can poison a city's water supply.

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
  cplayer = city_owner (pcity);
  if ((cplayer == pplayer) || (cplayer == NULL))
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

  /* Now lets see if the spy survives. */
  diplomat_escape (pplayer, pdiplomat, pcity);
}

/******************************************************************************
  Investigate a city.

  - Either a Diplomat or Spy can investigate a city.

  - It costs some minimal movement to investigate a city.

  - Diplomats die after investigation.
  - Spies always survive.  There is no risk.
****************************************************************************/
void diplomat_investigate(struct player *pplayer, struct unit *pdiplomat,
			  struct city *pcity)
{
  struct player *cplayer;
  struct packet_city_info packet;

  /* Fetch target city's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if ((cplayer == pplayer) || (cplayer == NULL))
    return;

  freelog (LOG_DEBUG, "investigate: unit: %d", pdiplomat->id);

  /* Send city info to investigator's player.
     As this is a special case we bypass send_city_info*/
  update_dumb_city(pplayer, pcity);
  package_city(pcity, &packet);
  packet.diplomat_investigate = 1;
  send_packet_city_info(pplayer->conn, &packet);

  /* Charge a nominal amount of movement for this. */
  (pdiplomat->moves_left)--;
  if (pdiplomat->moves_left < 0) {
    pdiplomat->moves_left = 0;
  }

  /* Spies always survive. Diplomats never do. */
  if (!unit_flag (pdiplomat->type, F_SPY)) {
    wipe_unit (0, pdiplomat);
  } else {
    send_unit_info (pplayer, pdiplomat);
  }
}

/******************************************************************************
  Get list of improvements from city (for purposes of sabotage).

  - Only a Spy can get a a city's sabotage list.

  - Always successful; returns list.

  - Spies always survive.
****************************************************************************/
void spy_get_sabotage_list(struct player *pplayer, struct unit *pdiplomat,
			   struct city *pcity)
{
  struct packet_sabotage_list packet;
  int i;
  char *p;

  /* Send city improvements info to player. */
  p = packet.improvements;
  for(i=0; i<B_LAST; i++)
    *p++=(pcity->improvements[i]) ? '1' : '0';
  *p='\0';
  packet.diplomat_id = pdiplomat->id;
  packet.city_id = pcity->id;
  send_packet_sabotage_list(pplayer->conn, &packet);
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
		      pcity->name, get_nation_name_plural (pplayer->nation));
    notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		      _("You executed a %s the %s had sent to establish"
			" an embassy in %s"),
		      unit_name (pdiplomat->type),
		      get_nation_name_plural (pplayer->nation),
		      pcity->name);
    wipe_unit (0, pdiplomat);
    return;
  }

  /* Check for Barbarian response. */
  if (is_barbarian (cplayer)) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_NOEVENT,
		      _("Game: Your %s was executed in %s by primitive %s."),
		      unit_name (pdiplomat->type),
		      pcity->name, get_nation_name_plural (pplayer->nation));
    wipe_unit (0, pdiplomat);
    return;
  }

  freelog (LOG_DEBUG, "embassy: succeeded");

  /* Establish the embassy. */
  pplayer->embassy |= (1 << pcity->owner);
  send_player_info (pplayer, pplayer);

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

  /* Spies always survive. Diplomats never do. */
  if (!unit_flag (pdiplomat->type, F_SPY)) {
    wipe_unit (0, pdiplomat);
  } else {
    send_unit_info (pplayer, pdiplomat);
  }
}

/******************************************************************************
  Sabotage an enemy unit.

  - Only a Spy can sabotage an enemy unit.

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
  if ((uplayer == pplayer) || (uplayer == NULL))
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

  /* Now lets see if the spy survives. */
  diplomat_escape (pplayer, pdiplomat, NULL);
}

/******************************************************************************
  Bribe an enemy unit.

  - Either a Diplomat or Spy can bribe an enemy unit.

  - Can't bribe a unit if:
    - Owner runs an unbribable government (e.g., democracy).
    - Player doesn't have enough money.
    - It's not the only unit on the square
      (this is handled outside this function).
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
  uplayer = get_player (pvictim->owner);
  if ((uplayer == pplayer) || (uplayer == NULL))
    return;

  freelog (LOG_DEBUG, "bribe-unit: unit: %d", pdiplomat->id);

  /* Update bribe cost. */
  if (pvictim->bribe_cost == -1) {
    freelog (LOG_NORMAL, "Bribe cost -1 in diplomat_bribe by %s",
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

  /* If player doesn't have enough money, can't bribe. */
  if (pplayer->economic.gold < pvictim->bribe_cost) {
    notify_player_ex (pplayer, pdiplomat->x, pdiplomat->y, E_MY_DIPLOMAT,
		      _("Game: You don't have enough money to"
			" bribe %s's %s."),
		      get_player (pvictim->owner)->name,
		      unit_name (pvictim->type));
    freelog (LOG_DEBUG, "bribe-unit: not enough money");
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

  /* Be sure to wipe the converted unit! */
  victim_x = pvictim->x;
  victim_y = pvictim->y;
  wipe_unit (0, pvictim);

  /* Now, try to move the briber onto the victim's square. */
  diplomat_id = pdiplomat->id;
  if (!handle_unit_move_request (pplayer, pdiplomat, victim_x, victim_y, FALSE)) {
    pdiplomat->moves_left = 0;
  }
  if (player_find_unit_by_id(pplayer, diplomat_id)) {
    send_unit_info (pplayer, pdiplomat);
  }

  /* Update clients. */
  send_player_info (pplayer, pplayer);
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
    wipe_unit (0, pdiplomat);
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
		      _("Game: A%s %s %s stole Future Tech. %d"
			" from %s."),
		      n_if_vowel (get_nation_name (pplayer->nation)[0]),
		      get_nation_name (pplayer->nation),
		      unit_name (pdiplomat->type),
		      pplayer->future_tech, pcity->name);
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
		      _("Game: The %s have stolen %s from the %s"),
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

  /* Check if a spy survives her mission. Diplomats never do. */
  diplomat_escape (pplayer, pdiplomat, pcity);
}

/**************************************************************************
  Incite a city to disaffect.

  - Either a Diplomat or Spy can incite a city to disaffect.

  - Can't incite a city to disaffect if:
    - Owner runs an unbribable government (e.g., democracy).
    - Player doesn't have enough money.
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
  struct city *pnewcity;

  /* Fetch target civilization's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if ((cplayer == pplayer) || (cplayer == NULL))
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
    freelog (LOG_NORMAL, "Incite cost -1 in diplomat_incite by %s for %s",
	     pplayer->name, pcity->name);
    city_incite_cost (pcity);
  }
  revolt_cost = pcity->incite_revolt_cost;

  /* Special deal for former owners! */
  if (pplayer->player_no == pcity->original) revolt_cost /= 2;

  /* If player doesn't have enough money, can't incite a revolt. */
  if (pplayer->economic.gold < revolt_cost) {
    notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		      _("Game: You don't have enough money to"
			" subvert %s."),
		      pcity->name);
    freelog (LOG_DEBUG, "incite: not enough money");
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
		      _("Game: You caught a%s %s %s attempting"
			" to incite a revolt in %s!"),
		      n_if_vowel (get_nation_name (pplayer->nation)[0]),
		      get_nation_name (pplayer->nation),
		      unit_name (pdiplomat->type), pcity->name);
    wipe_unit (0, pdiplomat);
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

  /*
   * Transfer city and units supported by this city (that
   * are within one square of the city) to the new owner.
   */
  pcity->shield_stock = 0;
  pnewcity = transfer_city (pplayer, cplayer, pcity, 1, 1, 1, 0);

  /* You get a technology advance, too! */
  get_a_tech (pplayer, cplayer);

  /* Check if a spy survives her mission. Diplomats never do. */
  diplomat_escape (pplayer, pdiplomat, pcity);
}  

/**************************************************************************
  Sabotage enemy city's improvement or production.
  If "improvement" is B_LAST, sabotage a random improvement or production.
  Else, if "improvement" is -1, sabotage current production.
  Otherwise, sabotage the city improvement whose ID is "improvement".
  (Note: Only Spies can select what to sabotage.)

  - Either a Diplomat or Spy can sabotage an enemy city.

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
  if ((cplayer == pplayer) || (cplayer == NULL))
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
		      _("Game: You caught a%s %s %s attempting"
			" sabotage in %s!"),
		      n_if_vowel (get_nation_name (pplayer->nation)[0]),
		      get_nation_name (pplayer->nation),
		      unit_name (pdiplomat->type), pcity->name);
    wipe_unit (0, pdiplomat);
    return;
  }

  freelog (LOG_DEBUG, "sabotage: succeeded");

  /* Examine the city for improvements to sabotage. */
  count = 0;
  for (index = 0; index < B_LAST; index++) {
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
      for (index = 0; index < B_LAST; index++) {
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
			  _("Game: You caught a%s %s %s attempting"
			    " to sabotage the %s in %s!"),
			  n_if_vowel (get_nation_name (pplayer->nation)[0]),
			  get_nation_name (pplayer->nation),
			  unit_name (pdiplomat->type),
			  get_improvement_name (improvement), pcity->name);
	wipe_unit (0, pdiplomat);
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

  /* Check if a spy survives her mission. Diplomats never do. */
  diplomat_escape (pplayer, pdiplomat, pcity);
}

/**************************************************************************
  This subtracts the destination movement cost from a diplomat/spy.
**************************************************************************/
static void diplomat_charge_movement (struct unit *pdiplomat, int x, int y)
{
  pdiplomat->moves_left -=
    tile_move_cost (pdiplomat, pdiplomat->x, pdiplomat->y, x, y);
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

	wipe_unit (0, punit);
      } else {
	/* Attacking Spy/Diplomat dies. */

	notify_player_ex (pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			  _("Game: Your %s was eliminated"
			    " by a defending %s in %s."),
			  unit_name (pdiplomat->type),
			  unit_name (punit->type),
			  pcity->name);
	notify_player_ex (cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			  _("Game: A%s %s %s was eliminated"
			    " while infiltrating %s."), 
			  n_if_vowel (get_nation_name (pplayer->nation)[0]),
			  get_nation_name (pplayer->nation),
			  unit_name (pdiplomat->type),
			  pcity->name);

	wipe_unit (0, pdiplomat);
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
	freelog (LOG_NORMAL, "Bug in diplomat_escape: Unhomed Spy.");
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
	freelog (LOG_NORMAL, "Bug in diplomat_escape: Spy can't teleport.");
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

  wipe_unit (0, pdiplomat);
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
      if ((x != cent_x)||(y != cent_y))
	{
	  unit_list_iterate(map_get_tile(x,y)->units, punit) {
	    if ((pplayer->player_no != punit->owner)&&
		(punit->activity == ACTIVITY_SENTRY))
	      {
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
    if (is_sailing_unit(punit2) && is_ground_units_transport(punit2)) { 
      tile_cap += get_transporter_capacity(punit2);
    } else if (is_ground_unit(punit2)) {
      tile_ncargo++;
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
		      get_location_str_in(pplayer, punit->x, punit->y, ", "));
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
      send_remove_unit(0, punit->id);
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
		       _("Game: Your %s has run out of hit points."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITF, "%s lose a %s (out of hp)", 
	      get_nation_name_plural(pplayer->nation),
	      unit_name(punit->type));
      wipe_unit(0, punit);
    }
    else if(is_air_unit(punit)) {
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
		&& punit2->fuel) {
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
	send_remove_unit(0, punit->id);
	notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
			 _("Game: Your %s has run out of fuel."),
			 unit_name(punit->type));
	gamelog(GAMELOG_UNITF, "%s lose a %s (fuel)", 
		get_nation_name_plural(pplayer->nation),
		unit_name(punit->type));
	wipe_unit(0, punit);
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
	wipe_unit_safe(pplayer, punit, &myiter);
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
void unit_restore_hitpoints(struct player *pplayer, struct unit *punit)
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
void unit_restore_movepoints(struct player *pplayer, struct unit *punit)
{
  punit->moves_left=unit_move_rate(punit);
}

/**************************************************************************
  after a battle this routine is called to decide whether or not the unit
  should become a veteran, if unit isn't already.
  there is a 50/50% chance for it to happend, (100% if player got SUNTZU)
**************************************************************************/
void maybe_make_veteran(struct unit *punit)
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
  int attackpower=get_total_attack_power(attacker,defender);
  int defensepower=get_total_defense_power(attacker,defender);

  freelog(LOG_VERBOSE, "attack:%d, defense:%d", attackpower, defensepower);
  if (!attackpower) {
      attacker->hp=0; 
  } else if (!defensepower) {
      defender->hp=0;
  }
  while (attacker->hp>0 && defender->hp>0) {
    if (myrand(attackpower+defensepower)>= defensepower) {
      defender->hp -= get_unit_type(attacker->type)->firepower
	* game.firepower_factor;
    } else {
      if (is_sailing_unit(defender) && map_get_city(defender->x, defender->y))
	attacker->hp -= game.firepower_factor;      /* pearl harbour */
      else
	attacker->hp -= get_unit_type(defender->type)->firepower
	  * game.firepower_factor;
    }
  }
  if (attacker->hp<0) attacker->hp=0;
  if (defender->hp<0) defender->hp=0;

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
    freelog(LOG_NORMAL, "Whoa!  Creating %s at illegal loc (%d, %d)",
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

  unfog_area(pplayer,x,y,get_unit_type(punit->type)->vision_range);
  send_unit_info(0, punit);
}


/**************************************************************************
  Removes the unit from the game, and notifies the clients.
  TODO: Find out if the homecity is refreshed and resend when this happends
  otherwise (refresh_city(homecity) + send_city(homecity))
**************************************************************************/
void send_remove_unit(struct player *pplayer, int unit_id)
{
  int o;

  struct packet_generic_integer packet;

  packet.value=unit_id;

  for(o=0; o<game.nplayers; o++)           /* dests */
    if(!pplayer || &game.players[o]==pplayer)
      send_packet_generic_integer(game.players[o].conn, PACKET_REMOVE_UNIT,
				  &packet);
}

/**************************************************************************
  iterate through all units and update them.
**************************************************************************/
void update_unit_activities(struct player *pplayer)
{
  unit_list_iterate(pplayer->units, punit)
    update_unit_activity(pplayer, punit);
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
			       " for %d credits."),
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
  Checks for terrain change between ocean and land.  Handles side-effects.
  (Should be called after any potential ocean/land terrain changes.)
**************************************************************************/
static void check_terrain_ocean_land_change(int x, int y,
					    enum tile_terrain_type old)
{
  enum tile_terrain_type new = map_get_terrain(x, y);

  if ((old == T_OCEAN) && (new != T_OCEAN)) {
    /* ocean to land ... */
    city_landlocked_sell_coastal_improvements(x, y);
    assign_continent_numbers();
    gamelog(GAMELOG_MAP, "(%d,%d) land created from ocean", x, y);
  } else if ((old != T_OCEAN) && (new == T_OCEAN)) {
    /* land to ocean ... */
    assign_continent_numbers();
    gamelog(GAMELOG_MAP, "(%d,%d) ocean created from land", x, y);
  }
}

/**************************************************************************
  progress settlers in their current tasks, 
  and units that is pillaging.
  also move units that is on a goto.
  restore unit move points (information needed for settler tasks)
**************************************************************************/
void update_unit_activity(struct player *pplayer, struct unit *punit)
{
  int id = punit->id;
  int mr = get_unit_type (punit->type)->move_rate;
  int unit_activity_done = 0;

  int activity = punit->activity;

  /* to allow a settler to begin a task with no moves left
     without it counting toward the time to finish */
  if (punit->moves_left){
    punit->activity_count += mr/3;
  }

  unit_restore_movepoints(pplayer, punit);

  if (punit->connecting && !can_unit_do_activity(punit, activity)) {
    punit->activity_count = 0;
    do_unit_goto (pplayer, punit, get_activity_move_restriction(activity));
  }

  /* if connecting, automagically build prerequisities first */
  if (punit->connecting && activity == ACTIVITY_RAILROAD &&
      !(map_get_tile(punit->x, punit->y)->special & S_ROAD)) {
    activity = ACTIVITY_ROAD;
  }

  if (activity == ACTIVITY_EXPLORE) {
    ai_manage_explorer(pplayer, punit);
    if (player_find_unit_by_id(pplayer, id))
      handle_unit_activity_request(punit, ACTIVITY_EXPLORE);
    else return;
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
      unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
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
      check_terrain_ocean_land_change(punit->x, punit->y, old);
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
      check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_TRANSFORM) {
    if (total_activity (punit->x, punit->y, ACTIVITY_TRANSFORM) >=
        map_transform_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_transform_tile(punit->x, punit->y);
      check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = 1;
    }
  }

  if (unit_activity_done) {
    send_tile_info(0, punit->x, punit->y);
    unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
      if (punit2->activity == activity) {
	if (punit2->connecting) {
	  punit2->activity_count = 0;
	  do_unit_goto(get_player (punit2->owner), punit2,
		       get_activity_move_restriction(activity));
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
      do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);
    }
    return;
  }
  
  if (punit->activity==ACTIVITY_IDLE && 
     map_get_terrain(punit->x, punit->y)==T_OCEAN &&
     is_ground_unit(punit))
    set_unit_activity(punit, ACTIVITY_SENTRY);

  send_unit_info(0, punit);
}


/**************************************************************************
 nuke a square
 1) remove all units on the square
 2) half the size of the city on the square
 if it isn't a city square or an ocean square then with 50% chance 
 add some pollution, then notify the client about the changes.
**************************************************************************/
void do_nuke_tile(int x, int y)
{
  struct unit_list *punit_list;
  struct city *pcity;
  punit_list=&map_get_tile(x, y)->units;
  
  while(unit_list_size(punit_list)) {
    struct unit *punit=unit_list_get(punit_list, 0);
    wipe_unit(0, punit);
  }

  if((pcity=map_get_city(x,y))) {
    if (pcity->size > 1) { /* size zero cities are ridiculous -- Syela */
      pcity->size/=2;
      auto_arrange_workers(pcity);
      send_city_info(0, pcity);
    }
  }
  else if ((map_get_terrain(x,y)!=T_OCEAN && map_get_terrain(x,y)<=T_TUNDRA) &&
           (!(map_get_special(x,y)&S_POLLUTION)) && myrand(2)) { 
    map_set_special(x,y, S_POLLUTION);
    send_tile_info(0, x, y);
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
int do_airline(struct unit *punit, int x, int y)
{
  struct city *city1, *city2;
  int src_x = punit->x, src_y = punit->y;

  if (!(city1=map_get_city(src_x, src_y)))
    return 0;
  if (!(city2=map_get_city(x, y)))
    return 0;
  if (!unit_can_airlift_to(punit, city2))
    return 0;
  city1->airlift=0;
  city2->airlift=0;
  punit->moves_left = 0;

  unit_list_unlink(&map_get_tile(src_x, src_y)->units, punit);
  punit->x = x; punit->y = y;
  unit_list_insert(&map_get_tile(x, y)->units, punit);

  punit->moved = 1;

  connection_do_buffer(game.players[punit->owner].conn);
  send_unit_info(&game.players[punit->owner], punit);
  send_city_info(city_owner(city1), city1);
  send_city_info(city_owner(city2), city2);
  notify_player_ex(&game.players[punit->owner], punit->x, punit->y, E_NOEVENT,
		   _("Game: %s transported succesfully."),
		   unit_name(punit->type));
  handle_unit_move_consequences(punit, src_x, src_y, x, y, 1);
  connection_do_unbuffer(game.players[punit->owner].conn);

  return 1;
}

/**************************************************************************
Returns whether the drop was made or not. Note that it also returns 1 in
the case where the drop was succesfull, but the unit was killed by
barbarians in a hut
**************************************************************************/
int do_paradrop(struct player *pplayer, struct unit *punit, int x, int y)
{
  if (!unit_flag(punit->type, F_PARATROOPERS)) {
    notify_player_ex(&game.players[punit->owner], punit->x, punit->y, E_NOEVENT,
		     _("Game: This unit type can not be paradropped."));
    return 0;
  }

  if (!can_unit_paradrop(punit))
    return 0;

  if (!map_get_known(x,y,pplayer)) {
    notify_player_ex(&game.players[punit->owner], x, y, E_NOEVENT,
		     _("Game: The destination location is not known."));
    return 0;
  }


  if (map_get_terrain(x,y) == T_OCEAN) {
    notify_player_ex(&game.players[punit->owner], x, y, E_NOEVENT,
		     _("Game: Cannot paradrop into ocean."));
    return 0;    
  }

  /*FIXME: this is a fog-of-war cheat.
    You get to know if there is an enemy on the tile*/
  if (is_enemy_unit_tile(x,y,punit->owner)) {
    notify_player_ex(&game.players[punit->owner], x, y, E_NOEVENT,
		     _("Game: Cannot paradrop because there are"
		       " enemy units on the destination location."));
    return 0;
  }

  {
    int range = get_unit_type(punit->type)->paratroopers_range;
    int distance = real_map_distance(punit->x, punit->y, x, y);
    if (distance > range) {
      notify_player_ex(&game.players[punit->owner], x, y, E_NOEVENT,
		       _("Game: The distance to the target (%i) "
			 "is greater than the unit's range(%i)."),
		       distance, range);
      return 0;
    }
  }

  /* All ok */
  {
    int src_x = punit->x, src_y = punit->y;

    unit_list_unlink(&map_get_tile(punit->x, punit->y)->units, punit);
    punit->x = x; punit->y = y;
    unit_list_insert(&map_get_tile(x, y)->units, punit);

    punit->moves_left -= get_unit_type(punit->type)->paratroopers_mr_sub;
    if(punit->moves_left < 0)
      punit->moves_left = 0;
    punit->paradropped = 1;
    punit->moved = 1;
    send_unit_info(0, punit);

    return handle_unit_move_consequences(punit, src_x, src_y, x, y, 1);
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

  for (i=0;i<B_LAST;i++) {
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
    freelog(LOG_NORMAL, "Bug in get_a_tech");
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
void place_partisans(struct city *pcity,int count)
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
this is a highlevel routine
Remove the unit, and passengers if it is a carrying any.
Remove the _minimum_ number, eg there could be another boat on the square.
Parameter iter, if non-NULL, should be an iterator for a unit list,
and if it points to a unit which we wipe, we advance it first to
avoid dangling pointers.
NOTE: iter should not be an iterator for the map units list, but
city supported, or player units, is ok.  (For the map units list, would
have to pass iter on inside transporter_min_cargo_to_unitlist().)
**************************************************************************/
void wipe_unit_spec_safe(struct player *dest, struct unit *punit,
		    struct genlist_iterator *iter, int wipe_cargo)
{
  if(get_transporter_capacity(punit) 
     && map_get_terrain(punit->x, punit->y)==T_OCEAN
     && wipe_cargo) {
    
    struct unit_list list;
    
    transporter_min_cargo_to_unitlist(punit, &list);
    
    unit_list_iterate(list, punit2) {
      if (iter && ((struct unit*)ITERATOR_PTR((*iter))) == punit2) {
	freelog(LOG_VERBOSE, "iterating over %s in wipe_unit_safe",
		unit_name(punit2->type));
	ITERATOR_NEXT((*iter));
      }
      notify_player_ex(get_player(punit2->owner), 
		       punit2->x, punit2->y, E_UNIT_LOST,
		       _("Game: You lost a%s %s when %s lost."),
		       n_if_vowel(get_unit_type(punit2->type)->name[0]),
		       get_unit_type(punit2->type)->name,
		       get_unit_type(punit->type)->name);
      gamelog(GAMELOG_UNITL, "%s lose a%s %s when %s lost", 
	      get_nation_name_plural(game.players[punit2->owner].nation),
	      n_if_vowel(get_unit_type(punit2->type)->name[0]),
	      get_unit_type(punit2->type)->name,
	      get_unit_type(punit->type)->name);
      send_remove_unit(0, punit2->id);
      server_remove_unit(punit2);
    }
    unit_list_iterate_end;
    unit_list_unlink_all(&list);
  }

  send_remove_unit(0, punit->id);
  server_remove_unit(punit);
}

/**************************************************************************
...
**************************************************************************/

void wipe_unit_safe(struct player *dest, struct unit *punit,
		    struct genlist_iterator *iter){
  wipe_unit_spec_safe(dest, punit, iter, 1);
}


/**************************************************************************
...
**************************************************************************/
void wipe_unit(struct player *dest, struct unit *punit)
{
  wipe_unit_safe(dest, punit, NULL);
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
  char *loc_str   = get_location_str_in(pplayer, punit->x, punit->y, ", ");
  int  num_killed = unit_list_size(&(map_get_tile(punit->x, punit->y)->units));
  int ransom;
  
  /* barbarian leader ransom hack */
  if( is_barbarian(pplayer) && unit_has_role(punit->type, L_BARBARIAN_LEADER) &&
      (num_killed==1) && (is_ground_unit(pkiller) || is_heli_unit(pkiller)) ) {
    ransom = (pplayer->economic.gold >= 100)?100:pplayer->economic.gold;
    notify_player_ex(destroyer, pkiller->x, pkiller->y, E_UNIT_WIN_ATT,
		     _("Game: Barbarian leader captured, %d gold ransom paid."),
                     ransom);
    destroyer->economic.gold += ransom;
    pplayer->economic.gold -= ransom;
    send_player_info(destroyer,0);   /* let me see my new money :-) */
  }

  if( (incity) ||
      (map_get_special(punit->x, punit->y)&S_FORTRESS) ||
      (map_get_special(punit->x, punit->y)&S_AIRBASE) ||
      (num_killed == 1)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST,
		     _("Game: You lost a%s %s under an attack from %s's %s%s."),
		     n_if_vowel(get_unit_type(punit->type)->name[0]),
		     get_unit_type(punit->type)->name, destroyer->name,
		     unit_name(pkiller->type), loc_str);
    gamelog(GAMELOG_UNITL, "%s lose a%s %s to the %s",
	    get_nation_name_plural(pplayer->nation),
	    n_if_vowel(get_unit_type(punit->type)->name[0]),
	    get_unit_type(punit->type)->name,
	    get_nation_name_plural(destroyer->nation));
    
    send_remove_unit(0, punit->id);
    server_remove_unit(punit);
  }
  else {
    notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST,
		     _("Game: You lost %d units under an attack"
		       " from %s's %s%s."),
		     num_killed, destroyer->name,
		     unit_name(pkiller->type), loc_str);
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
	notify_player_ex(&game.players[punit2->owner], 
			 punit2->x, punit2->y, E_UNIT_LOST,
			 _("Game: You lost a%s %s under an attack"
			   " from %s's %s."),
			 n_if_vowel(get_unit_type(punit2->type)->name[0]),
			 get_unit_type(punit2->type)->name, destroyer->name,
                         unit_name(pkiller->type));
	gamelog(GAMELOG_UNITL, "%s lose a%s %s to the %s",
		get_nation_name_plural(pplayer->nation),
		n_if_vowel(get_unit_type(punit2->type)->name[0]),
		get_unit_type(punit2->type)->name,
		get_nation_name_plural(destroyer->nation));
	send_remove_unit(0, punit2->id);
	server_remove_unit(punit2);
    }
    unit_list_iterate_end;
  } 
}

/**************************************************************************
  send the unit to the players who need the info 
  dest = NULL means all players, dosend means send even if the player 
  can't see the unit.
  x and y is where the unit came from, so that the info can be send there too
**************************************************************************/
void send_unit_info_to_onlookers(struct player *dest, struct unit *punit, int x, int y)
{
  int o;
  struct packet_unit_info info;

  info.id=punit->id;
  info.owner=punit->owner;
  info.x=punit->x;
  info.y=punit->y;
  info.homecity=punit->homecity;
  info.veteran=punit->veteran;
  info.type=punit->type;
  info.movesleft=punit->moves_left;
  info.hp=punit->hp / game.firepower_factor;
  info.activity=punit->activity;
  info.activity_count=punit->activity_count;
  info.unhappiness=punit->unhappiness;
  info.upkeep=punit->upkeep;
  info.upkeep_food=punit->upkeep_food;
  info.upkeep_gold=punit->upkeep_gold;
  info.ai=punit->ai.control;
  info.fuel=punit->fuel;
  info.goto_dest_x=punit->goto_dest_x;
  info.goto_dest_y=punit->goto_dest_y;
  info.activity_target=punit->activity_target;
  info.paradropped=punit->paradropped;
  info.connecting=punit->connecting;

  for(o=0; o<game.nplayers; o++)           /* dests */
    if(!dest || &game.players[o]==dest)
      if(map_get_known_and_seen(info.x, info.y, &game.players[o]) ||
	 map_get_known_and_seen(x,y, &game.players[o])){
	send_packet_unit_info(game.players[o].conn, &info);
      }
}

/**************************************************************************
  send the unit to the players who need the info 
  dest = NULL means all players, dosend means send even if the player 
  can't see the unit.
**************************************************************************/
void send_unit_info(struct player *dest, struct unit *punit)
{
  send_unit_info_to_onlookers(dest, punit, punit->x,punit->y);
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
  The prefix is prepended to the message, _except_ for the last case; eg,
  for adding space/punctuation between rest of message and location string.
  Don't call this function directly; use the wrappers below.
**************************************************************************/
static char *get_location_str(struct player *pplayer, int x, int y,
				       char *prefix, int use_at)
{
  static char buffer[MAX_LEN_NAME+64];
  struct city *incity, *nearcity;

  incity = map_get_city(x, y);
  if (incity) {
    if (use_at) {
      my_snprintf(buffer, sizeof(buffer), _("%sat %s"), prefix, incity->name);
    } else {
      my_snprintf(buffer, sizeof(buffer), _("%sin %s"), prefix, incity->name);
    }
  } else {
    nearcity = dist_nearest_city(pplayer, x, y, 0, 0);
    if (nearcity) {
      if (is_tiles_adjacent(x, y, nearcity->x, nearcity->y)) {
	my_snprintf(buffer, sizeof(buffer),
		   _("%soutside %s"), prefix, nearcity->name);
      } else {
	my_snprintf(buffer, sizeof(buffer),
		    _("%snear %s"), prefix, nearcity->name);
      }
    } else {
      buffer[0] = '\0';
    }
  }
  return buffer;
}

char *get_location_str_in(struct player *pplayer, int x, int y, char *prefix)
{
  return get_location_str(pplayer, x, y, prefix, 0);
}

char *get_location_str_at(struct player *pplayer, int x, int y, char *prefix)
{
  return get_location_str(pplayer, x, y, prefix, 1);
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
    restr = GOTO_MOVE_STRAIGHTEST;
    break;
  default:
    restr = GOTO_MOVE_ANY;
    break;
  }

  return (restr);
}


/**************************************************************************
...
**************************************************************************/
void send_all_known_units(struct player *dest)
{
  int o,p;
  for(p=0; p<game.nplayers; p++)  /* send the players units */
    for(o=0; o<game.nplayers; o++)           /* dests */
      if(!dest || &game.players[o]==dest) {
	struct player *unitowner = &game.players[p];
	struct player *pplayer = &game.players[o];
	unit_list_iterate(unitowner->units,punit)
	  send_unit_info(pplayer, punit);
	unit_list_iterate_end;
      }
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

  connection_do_buffer(pplayer->conn);
  punit->type = to_unit;
  unfog_area(pplayer,punit->x,punit->y, get_unit_type(to_unit)->vision_range);
  fog_area(pplayer,punit->x,punit->y,range);
  send_unit_info(0, punit);
  connection_do_unbuffer(pplayer->conn);
}

/**************************************************************************
Does: 1) updates  the units homecity and the city it enters/leaves (the
         cities happiness varies). This also takes into account if the
	 unit enters/leaves a fortress.
      2) handles any huts at the units destination.
      3) awakes any sentried units on neightborin tiles
Returns: if the unit survived (it can die in the hut)

send_unit_info should be called AFTER this function so that the little
angry faces on units gets updated. send_unit_info will detect

FIXME: Sometimes it is not neccesary to send cities because the goverment
       doesn't care if a unit is away or not.
**************************************************************************/
int handle_unit_move_consequences(struct unit *punit, int src_x, int src_y,
				  int dest_x, int dest_y, int enter_hut)
{
  struct city *fromcity = map_get_city(src_x, src_y);
  struct city *tocity = map_get_city(dest_x, dest_y);
  struct city *homecity = NULL;
  struct player *pplayer = get_player(punit->owner);
  /*  struct government *g = get_gov_pplayer(pplayer);*/
  int sentfrom = 0, sentto = 0, senthome = 0;
  if (punit->homecity)
    homecity = find_city_by_id(punit->homecity);

  teleport_unit_sight_points(src_x, src_y, dest_x, dest_y, punit);
  wakeup_neighbor_sentries(pplayer, dest_x, dest_y);

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
      senthome = sentto = 1;
    }

    if (fromcity && fromcity->owner == punit->owner) { /* leaving a city */
      if (!senthome && homecity) {
	city_refresh(homecity);
	send_city_info(pplayer, homecity);
      }
      if (fromcity != homecity && !sentfrom) {
	city_refresh(fromcity);
	send_city_info(pplayer, fromcity);
      }
      senthome = sentfrom = 1;
    }

    /* entering/leaving a fortress */
    if (map_get_tile(dest_x, dest_y)->special&S_FORTRESS
	&& homecity
	&& real_map_distance(homecity->x, homecity->y, dest_x, dest_y) <= 3
	&& !senthome) {
      city_refresh(homecity);
      send_city_info(pplayer, homecity);
    }

    if (map_get_tile(src_x, src_y)->special&S_FORTRESS
	&& homecity
	&& real_map_distance(homecity->x, homecity->y, src_x, src_y) <= 3
	&& !senthome) {
      city_refresh(homecity);
      send_city_info(pplayer, homecity);
    }
  }

  /* punit might get killed by horde of barbarians */
  if(enter_hut && (map_get_tile(dest_x, dest_y)->special&S_HUT)) {
    return handle_unit_enter_hut(punit);
  }
  return 1;
}
