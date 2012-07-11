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

#include <stdio.h>

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "rand.h"

/* common */
#include "base.h"
#include "events.h"
#include "game.h"
#include "government.h"
#include "movement.h"
#include "player.h"
#include "research.h"
#include "unitlist.h"

/* server */
#include "aiiface.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplhand.h"
#include "diplomats.h"
#include "notify.h"
#include "plrhand.h"
#include "techtools.h"
#include "unithand.h"
#include "unittools.h"


/****************************************************************************/

static void diplomat_charge_movement (struct unit *pdiplomat,
                                      struct tile *ptile);
static bool diplomat_success_vs_defender(struct unit *patt, struct unit *pdef,
                                         struct tile *pdefender_tile);
static bool diplomat_infiltrate_tile(struct player *pplayer,
                                     struct player *cplayer,
                                     struct unit *pdiplomat,
                                     struct tile *ptile);
static void diplomat_escape(struct player *pplayer, struct unit *pdiplomat,
                            const struct city *pcity);
static void maybe_cause_incident(enum diplomat_actions action,
                                 struct player *offender,
                                 struct player *victim_player,
                                 struct tile *victim_tile,
                                 const char *victim_link);

/******************************************************************************
  Poison a city's water supply.

  - Only a Spy can poison a city's water supply.
  - Only allowed against players you are at war with.

  - Check for infiltration success.  Our poisoner may not survive this.
  - Only cities of size greater than one may be poisoned.
  - If successful, reduces population by one point.

  - The poisoner may be captured and executed, or escape to its home town.

  'pplayer' is the player who tries to poison 'pcity' with its unit
  'pdiplomat'.
****************************************************************************/
void spy_poison(struct player *pplayer, struct unit *pdiplomat,
		struct city *pcity)
{
  struct player *cplayer;

  /* Fetch target city's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner(pcity);
  if (!cplayer || !pplayers_at_war(pplayer, cplayer))
    return;

  log_debug("poison: unit: %d", pdiplomat->id);

  /* If not a Spy, can't poison. */
  if (!unit_has_type_flag(pdiplomat, UTYF_SPY))
    return;

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_tile(pplayer, cplayer, pdiplomat, 
                                pcity->tile)) {
    return;
  }

  log_debug("poison: infiltrated");

  /* If city is too small, can't poison. */
  if (city_size_get(pcity) < 2) {
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s could not poison the water"
                    " supply in %s."),
                  unit_link(pdiplomat),
                  city_link(pcity));
    log_debug("poison: target city too small");
    return;
  }

  log_debug("poison: succeeded");

  /* Poison people! */
  city_reduce_size(pcity, 1, pplayer);

  /* Notify everybody involved. */
  notify_player(pplayer, city_tile(pcity), E_MY_DIPLOMAT_POISON, ftc_server,
                _("Your %s poisoned the water supply of %s."),
                unit_link(pdiplomat),
                city_link(pcity));
  notify_player(cplayer, city_tile(pcity),
                E_ENEMY_DIPLOMAT_POISON, ftc_server,
                _("%s is suspected of poisoning the water supply of %s."),
                player_name(pplayer),
                city_link(pcity));

  /* Update clients. */
  city_refresh (pcity);  
  send_city_info(NULL, pcity);

  /* this may cause a diplomatic incident */
  maybe_cause_incident(SPY_POISON, pplayer, cplayer,
                       city_tile(pcity), city_link(pcity));

  /* Now lets see if the spy survives. */
  diplomat_escape(pplayer, pdiplomat, pcity);
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
  bool first_packet;
  struct packet_unit_short_info unit_packet;
  struct packet_city_info city_packet;

  /* Fetch target city's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if ((cplayer == pplayer) || !cplayer)
    return;

  log_debug("investigate: unit: %d", pdiplomat->id);

  /* Do It... */
  update_dumb_city(pplayer, pcity);
  /* Special case for a diplomat/spy investigating a city:
     The investigator needs to know the supported and present
     units of a city, whether or not they are fogged. So, we
     send a list of them all before sending the city info.
     As this is a special case we bypass send_unit_info. */
  first_packet = TRUE;
  unit_list_iterate(pcity->units_supported, punit) {
    package_short_unit(punit, &unit_packet,
                       UNIT_INFO_CITY_SUPPORTED, pcity->id, first_packet);
    lsend_packet_unit_short_info(pplayer->connections, &unit_packet);
    first_packet = FALSE;
  } unit_list_iterate_end;
  unit_list_iterate((pcity->tile)->units, punit) {
    package_short_unit(punit, &unit_packet,
                       UNIT_INFO_CITY_PRESENT, pcity->id, first_packet);
    lsend_packet_unit_short_info(pplayer->connections, &unit_packet);
    first_packet = FALSE;
  } unit_list_iterate_end;
  /* Send city info to investigator's player.
     As this is a special case we bypass send_city_info. */
  package_city(pcity, &city_packet, TRUE);
  /* We need to force to send the packet to ensure the client will receive
   * something and popup the city dialog. */
  lsend_packet_city_info(pplayer->connections, &city_packet, TRUE);

  /* Charge a nominal amount of movement for this. */
  (pdiplomat->moves_left)--;
  if (pdiplomat->moves_left < 0) {
    pdiplomat->moves_left = 0;
  }

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_INVESTIGATE, pplayer, cplayer,
                       city_tile(pcity), city_link(pcity));

  /* Spies always survive. Diplomats never do. */
  if (!unit_has_type_flag(pdiplomat, UTYF_SPY)) {
    wipe_unit(pdiplomat, ULR_USED);
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
void spy_send_sabotage_list(struct connection *pc, struct unit *pdiplomat,
			    struct city *pcity)
{
  struct packet_city_sabotage_list packet;

  /* Send city improvements info to player. */
  BV_CLR_ALL(packet.improvements);

  improvement_iterate(ptarget) {
    if (city_has_building(pcity, ptarget)) {
      BV_SET(packet.improvements, improvement_index(ptarget));
    }
  } improvement_iterate_end;

  packet.diplomat_id = pdiplomat->id;
  packet.city_id = pcity->id;
  send_packet_city_sabotage_list(pc, &packet);
}

/******************************************************************************
  Establish an embassy.

  - Either a Diplomat or Spy can establish an embassy.

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
  if ((cplayer == pplayer) || !cplayer)
    return;

  log_debug("embassy: unit: %d", pdiplomat->id);

  /* Check for Barbarian response. */
  if (get_player_bonus(cplayer, EFT_NO_DIPLOMACY)) {
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s was executed in %s by primitive %s."),
                  unit_tile_link(pdiplomat),
                  city_link(pcity),
                  nation_plural_for_player(cplayer));
    wipe_unit(pdiplomat, ULR_EXECUTED);
    return;
  }

  log_debug("embassy: succeeded");

  establish_embassy(pplayer, cplayer);

  /* Notify everybody involved. */
  notify_player(pplayer, city_tile(pcity),
                E_MY_DIPLOMAT_EMBASSY, ftc_server,
                _("You have established an embassy in %s."),
                city_link(pcity));
  notify_player(cplayer, city_tile(pcity),
                E_ENEMY_DIPLOMAT_EMBASSY, ftc_server,
                _("The %s have established an embassy in %s."),
                nation_plural_for_player(pplayer),
                city_link(pcity));

  /* Charge a nominal amount of movement for this. */
  (pdiplomat->moves_left)--;
  if (pdiplomat->moves_left < 0) {
    pdiplomat->moves_left = 0;
  }

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_EMBASSY, pplayer, cplayer,
                       city_tile(pcity), city_link(pcity));

  /* Spies always survive. Diplomats never do. */
  if (!unit_has_type_flag(pdiplomat, UTYF_SPY)) {
    wipe_unit(pdiplomat, ULR_USED);
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
  char victim_link[MAX_LEN_LINK];
  struct player *uplayer;

  /* Fetch target unit's player.  Sanity checks. */
  if (!pvictim)
    return;
  uplayer = unit_owner(pvictim);
  if (!uplayer || pplayers_allied(pplayer, uplayer))
    return;

  log_debug("sabotage-unit: unit: %d", pdiplomat->id);

  /* If not a Spy, can't sabotage unit. */
  if (!unit_has_type_flag(pdiplomat, UTYF_SPY))
    return;

  /* N.B: unit_link() always returns the same pointer. */
  sz_strlcpy(victim_link, unit_link(pvictim));

  /* If unit has too few hp, can't sabotage. */
  if (pvictim->hp < 2) {
    notify_player(pplayer, unit_tile(pvictim),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s could not sabotage the %s %s."),
                  unit_link(pdiplomat),
                  nation_adjective_for_player(uplayer),
                  victim_link);
    log_debug("sabotage-unit: unit has too few hit points");
    return;
  }

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_tile(pplayer, uplayer, pdiplomat, 
                                unit_tile(pvictim))) {
    return;
  }

  log_debug("sabotage-unit: succeeded");

  /* Sabotage the unit by removing half its remaining hit points. */
  pvictim->hp /= 2;
  send_unit_info(NULL, pvictim);

  /* Notify everybody involved. */
  notify_player(pplayer, unit_tile(pvictim),
                E_MY_DIPLOMAT_SABOTAGE, ftc_server,
                _("Your %s succeeded in sabotaging the %s %s."),
                unit_link(pdiplomat),
                nation_adjective_for_player(uplayer),
                victim_link);
  notify_player(uplayer, unit_tile(pvictim),
                E_ENEMY_DIPLOMAT_SABOTAGE, ftc_server,
                /* TRANS: ... the Poles! */
                _("Your %s was sabotaged by the %s!"),
                victim_link,
                nation_plural_for_player(pplayer));

  /* this may cause a diplomatic incident */
  maybe_cause_incident(SPY_SABOTAGE_UNIT, pplayer, uplayer,
                       unit_tile(pvictim), victim_link);

  /* Now lets see if the spy survives. */
  diplomat_escape(pplayer, pdiplomat, NULL);
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
  char victim_link[MAX_LEN_LINK];
  struct player *uplayer;
  struct tile *victim_tile;
  int bribe_cost;
  int diplomat_id = pdiplomat->id;

  /* Fetch target unit's player.  Sanity checks. */
  if (!pvictim) {
    return;
  }
  uplayer = unit_owner(pvictim);
  /* We might make it allowable in peace with a loss of reputation */
  if (!uplayer || pplayers_allied(pplayer, uplayer)) {
    return;
  }

  log_debug("bribe-unit: unit: %d", pdiplomat->id);

  /* Check for unit from a bribable government. */
  if (get_player_bonus(uplayer, EFT_UNBRIBABLE_UNITS)) {
    notify_player(pplayer, unit_tile(pdiplomat),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("You can't bribe a unit from this nation."));
    return;
  }

  /* Get bribe cost, ignoring any previously saved value. */
  bribe_cost = unit_bribe_cost(pvictim);

  /* If player doesn't have enough gold, can't bribe. */
  if (pplayer->economic.gold < bribe_cost) {
    notify_player(pplayer, unit_tile(pdiplomat),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("You don't have enough gold to bribe the %s %s."),
                  nation_adjective_for_player(uplayer),
                  unit_link(pvictim));
    log_debug("bribe-unit: not enough gold");
    return;
  }

  if (unit_has_type_flag(pvictim, UTYF_UNBRIBABLE)) {
    notify_player(pplayer, unit_tile(pdiplomat),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("You cannot bribe the %s!"),
                  unit_link(pvictim));
    return;
  }

  log_debug("bribe-unit: succeeded");

  victim_tile = unit_tile(pvictim);
  pvictim = unit_change_owner(pvictim, pplayer, pdiplomat->homecity,
                              ULR_BRIBED);

  /* N.B.: unit_link always returns the same pointer. As unit_change_owner()
   * currently remove the old unit and replace by a new one (with a new id),
   * we want to make link to the new unit. */
  sz_strlcpy(victim_link, unit_link(pvictim));

  /* Notify everybody involved. */
  notify_player(pplayer, victim_tile, E_MY_DIPLOMAT_BRIBE, ftc_server,
                /* TRANS: <diplomat> ... <unit> */
                _("Your %s succeeded in bribing the %s."),
                unit_link(pdiplomat), victim_link);
  if (maybe_make_veteran(pdiplomat)) {
    notify_unit_experience(pdiplomat);
  }
  notify_player(uplayer, victim_tile, E_ENEMY_DIPLOMAT_BRIBE, ftc_server,
                /* TRANS: <unit> ... <Poles> */
                _("Your %s was bribed by the %s."),
                victim_link, nation_plural_for_player(pplayer));

  /* This costs! */
  pplayer->economic.gold -= bribe_cost;

  /* This may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_BRIBE, pplayer, uplayer,
                       victim_tile, victim_link);

  if (!unit_alive(diplomat_id)) {
    return;
  }

  /* Now, try to move the briber onto the victim's square. */
  if (!unit_move_handling(pdiplomat, victim_tile, FALSE, FALSE)) {
    pdiplomat->moves_left = 0;
  }
  if (NULL != player_unit_by_number(pplayer, diplomat_id)) {
    send_unit_info(pplayer, pdiplomat);
  }

  /* Update clients. */
  send_player_all_c(pplayer, NULL);
}

/****************************************************************************
  Try to steal a technology from an enemy city.
  If "technology" is A_UNSET, steal a random technology.
  Otherwise, steal the technology whose ID is "technology".
  (Note: Only Spies can select what to steal.)

  - Either a Diplomat or Spy can steal a technology.

  - Check for infiltration success.  Our thief may not survive this.
  - Check for basic success.  Again, our thief may not survive this.
  - If a technology has already been stolen from this city at any time:
    - Diplomats will be caught and executed.
    - Spies will have an increasing chance of being caught and executed.
  - Steal technology!

  - The thief may be captured and executed, or escape to its home town.

  FIXME: It should give a loss of reputation to steal from a player you are
  not at war with
****************************************************************************/
void diplomat_get_tech(struct player *pplayer, struct unit *pdiplomat, 
		       struct city  *pcity, Tech_type_id technology)
{
  struct player *cplayer;
  int count;
  Tech_type_id tech_stolen;

  /* We have to check arguments. They are sent to us by a client,
     so we cannot trust them */
  if (!pcity) {
    return;
  }
  
  cplayer = city_owner (pcity);
  if ((cplayer == pplayer) || !cplayer) {
    return;
  }
  
  if (technology < 0 || technology == A_NONE || technology >= A_LAST) {
    return;
  }
  
  if (technology != A_FUTURE && technology != A_UNSET
      && !valid_advance_by_number(technology)) {
    return;
  }
  
  if (technology == A_FUTURE) {
    if (player_invention_state(pplayer, A_FUTURE) != TECH_PREREQS_KNOWN
        || player_research_get(pplayer)->future_tech >= 
	   player_research_get(pplayer)->future_tech) {
      return;
    }
  } else if (technology != A_UNSET) {
    if (player_invention_state(pplayer, technology) == TECH_KNOWN) {
      return;
    }
    if (player_invention_state(cplayer, technology) != TECH_KNOWN) {
      return;
    }
  }

  log_debug("steal-tech: unit: %d", pdiplomat->id);

  /* If not a Spy, do something random. */
  if (!unit_has_type_flag(pdiplomat, UTYF_SPY)) {
    technology = A_UNSET;
  }

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_tile(pplayer, cplayer, pdiplomat, 
                                pcity->tile)) {
    return;
  }

  log_debug("steal-tech: infiltrated");

  /* Check if the Diplomat/Spy succeeds with his/her task. */
  /* (Twice as difficult if target is specified.) */
  /* (If already stolen from, impossible for Diplomats and harder for Spies.) */
  if (pcity->server.steal > 0 && !unit_has_type_flag(pdiplomat, UTYF_SPY)) {
    /* Already stolen from: Diplomat always fails! */
    count = 1;
    log_debug("steal-tech: difficulty: impossible");
  } else {
    /* Determine difficulty. */
    count = 1;
    if (technology != A_UNSET) {
      count++;
    }
    count += pcity->server.steal;
    log_debug("steal-tech: difficulty: %d", count);
    /* Determine success or failure. */
    while (count > 0) {
      if (fc_rand (100) >= game.server.diplchance) {
        break;
      }
      count--;
    }
  }
  
  if (count > 0) {
    if (pcity->server.steal > 0 && !unit_has_type_flag(pdiplomat, UTYF_SPY)) {
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("%s was expecting your attempt to steal technology "
                      "again. Your %s was caught and executed."),
                    city_link(pcity),
                    unit_tile_link(pdiplomat));
    } else {
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s was caught in the attempt of"
                      " stealing technology from %s."),
                    unit_tile_link(pdiplomat),
                    city_link(pcity));
    }
    notify_player(cplayer, city_tile(pcity),
                  E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                  _("The %s %s failed to steal technology from %s."),
                  nation_adjective_for_player(pplayer),
                  unit_tile_link(pdiplomat),
                  city_link(pcity));
    /* this may cause a diplomatic incident */
    maybe_cause_incident(DIPLOMAT_STEAL, pplayer, cplayer,
                         city_tile(pcity), city_link(pcity));
    wipe_unit(pdiplomat, ULR_CAUGHT);
    return;
  } 

  tech_stolen = steal_a_tech(pplayer, cplayer, technology);

  if (tech_stolen == A_NONE) {
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("No new technology found in %s."),
                  city_link(pcity));
    diplomat_charge_movement (pdiplomat, pcity->tile);
    send_unit_info (pplayer, pdiplomat);
    return;
  }

  /* Update stealing player's science progress and research fields */
  send_player_all_c(pplayer, NULL);

  /* Record the theft. */
  (pcity->server.steal)++;

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_STEAL, pplayer, cplayer,
                       city_tile(pcity), city_link(pcity));

  /* Check if a spy survives her mission. Diplomats never do. */
  diplomat_escape(pplayer, pdiplomat, pcity);
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
  int revolt_cost;

  /* Fetch target civilization's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if (!cplayer || pplayers_allied(cplayer, pplayer))
    return;

  log_debug("incite: unit: %d", pdiplomat->id);

  /* See if the city is subvertable. */
  if (get_city_bonus(pcity, EFT_NO_INCITE) > 0) {
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("You can't subvert this city."));
    log_debug("incite: city is protected");
    return;
  }

  /* Get incite cost, ignoring any previously saved value. */
  revolt_cost = city_incite_cost(pplayer, pcity);

  /* If player doesn't have enough gold, can't incite a revolt. */
  if (pplayer->economic.gold < revolt_cost) {
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("You don't have enough gold to subvert %s."),
                  city_link(pcity));
    log_debug("incite: not enough gold");
    return;
  }

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_tile(pplayer, cplayer, pdiplomat, 
                                pcity->tile)) {
    return;
  }

  log_debug("incite: infiltrated");

  /* Check if the Diplomat/Spy succeeds with his/her task. */
  if (fc_rand (100) >= game.server.diplchance) {
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s was caught in the attempt"
                    " of inciting a revolt!"),
                  unit_tile_link(pdiplomat));
    notify_player(cplayer, city_tile(pcity),
                  E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                  _("You caught %s %s attempting"
                    " to incite a revolt in %s!"),
                  nation_adjective_for_player(pplayer),
                  unit_tile_link(pdiplomat),
                  city_link(pcity));
    wipe_unit(pdiplomat, ULR_CAUGHT);
    return;
  }

  log_debug("incite: succeeded");

  /* Subvert the city to your cause... */

  /* City loses some population. */
  if (city_size_get(pcity) > 1) {
    city_reduce_size(pcity, 1, pplayer);
  }

  /* This costs! */
  pplayer->economic.gold -= revolt_cost;

  /* Notify everybody involved. */
  notify_player(pplayer, city_tile(pcity),
                E_MY_DIPLOMAT_INCITE, ftc_server,
                _("Revolt incited in %s, you now rule the city!"),
                city_link(pcity));
  notify_player(cplayer, city_tile(pcity),
                E_ENEMY_DIPLOMAT_INCITE, ftc_server,
                _("%s has revolted, %s influence suspected."),
                city_link(pcity),
                nation_adjective_for_player(pplayer));

  pcity->shield_stock = 0;
  nullify_prechange_production(pcity);

  /* You get a technology advance, too! */
  steal_a_tech (pplayer, cplayer, A_UNSET);

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_INCITE, pplayer, cplayer,
                       city_tile(pcity), city_link(pcity));

  /* Transfer city and units supported by this city (that
     are within one square of the city) to the new owner. */
  transfer_city (pplayer, pcity, 1, TRUE, TRUE, FALSE);

  /* Check if a spy survives her mission. Diplomats never do.
   * _After_ transferring the city, or the city area is first fogged
   * when the diplomat is removed, and then unfogged when the city
   * is transferred. */
  diplomat_escape(pplayer, pdiplomat, pcity);

  /* Update the players gold in the client */
  send_player_info_c(pplayer, pplayer->connections);
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
		       struct city *pcity, Impr_type_id improvement)
{
  struct player *cplayer;
  struct impr_type *ptarget;
  int count, which;
  int success_prob;

  /* Fetch target city's player.  Sanity checks. */
  if (!pcity)
    return;
  cplayer = city_owner (pcity);
  if (!cplayer || !pplayers_at_war(pplayer, cplayer))
    return;

  log_debug("sabotage: unit: %d", pdiplomat->id);

  /* If not a Spy, do something random. */
  if (!unit_has_type_flag(pdiplomat, UTYF_SPY))
    improvement = B_LAST;

  /* Twice as difficult if target is specified. */
  success_prob = (improvement >= B_LAST ? game.server.diplchance 
                  : game.server.diplchance / 2); 

  /* Check if the Diplomat/Spy succeeds against defending Diplomats/Spies. */
  if (!diplomat_infiltrate_tile(pplayer, cplayer, pdiplomat, 
                                pcity->tile)) {
    return;
  }

  log_debug("sabotage: infiltrated");

  /* Check if the Diplomat/Spy succeeds with his/her task. */
  if (fc_rand (100) >= success_prob) {
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_FAILED, ftc_server,
                  _("Your %s was caught in the attempt"
                    " of industrial sabotage!"),
                  unit_tile_link(pdiplomat));
    notify_player(cplayer, city_tile(pcity),
                  E_ENEMY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("You caught %s %s attempting sabotage in %s!"),
                  nation_adjective_for_player(pplayer),
                  unit_tile_link(pdiplomat),
                  city_link(pcity));
    wipe_unit(pdiplomat, ULR_CAUGHT);
    return;
  }

  log_debug("sabotage: succeeded");

  /* Examine the city for improvements to sabotage. */
  count = 0;
  city_built_iterate(pcity, pimprove) {
    if (pimprove->sabotage > 0) {
      count++;
    }
  } city_built_iterate_end;

  log_debug("sabotage: count of improvements: %d", count);

  /* Determine the target (-1 is production). */
  if (improvement < 0) {
    /* If told to sabotage production, do so. */
    ptarget = NULL;
    log_debug("sabotage: specified target production");
  } else if (improvement >= B_LAST) {
    /*
     * Pick random:
     * 50/50 chance to pick production or some improvement.
     * Won't pick something that doesn't exit.
     * If nothing to do, say so, deduct movement cost and return.
     */
    if (count == 0 && pcity->shield_stock == 0) {
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s could not find anything to sabotage in %s."),
                    unit_link(pdiplomat),
                    city_link(pcity));
      diplomat_charge_movement(pdiplomat, pcity->tile);
      send_unit_info(pplayer, pdiplomat);
      log_debug("sabotage: random: nothing to do");
      return;
    }
    if (count == 0 || fc_rand (2) == 1) {
      ptarget = NULL;
      log_debug("sabotage: random: targeted production");
    } else {
      ptarget = NULL;
      which = fc_rand (count);

      city_built_iterate(pcity, pimprove) {
	if (pimprove->sabotage > 0) {
	  if (which > 0) {
	    which--;
	  } else {
	    ptarget = pimprove;
	    break;
	  }
	}
      } city_built_iterate_end;

      if (NULL != ptarget) {
        log_debug("sabotage: random: targeted improvement: %d (%s)",
                  improvement_number(ptarget),
                  improvement_rule_name(ptarget));
      } else {
        log_error("sabotage: random: targeted improvement error!");
      }
    }
  } else {
    struct impr_type *pimprove = improvement_by_number(improvement);
    /*
     * Told which improvement to pick:
     * If try for wonder or palace, complain, deduct movement cost and return.
     * If not available, say so, deduct movement cost and return.
     */
    if (city_has_building(pcity, pimprove)) {
      if (pimprove->sabotage > 0) {
	ptarget = pimprove;
        log_debug("sabotage: specified target improvement: %d (%s)",
                  improvement, improvement_rule_name(pimprove));
      } else {
        notify_player(pplayer, city_tile(pcity),
                      E_MY_DIPLOMAT_FAILED, ftc_server,
                      _("You cannot sabotage a %s!"),
                      improvement_name_translation(pimprove));
        diplomat_charge_movement(pdiplomat, pcity->tile);
        send_unit_info(pplayer, pdiplomat);
        log_debug("sabotage: disallowed target improvement: %d (%s)",
                  improvement, improvement_rule_name(pimprove));
        return;
      }
    } else {
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s could not find the %s to sabotage in %s."),
                    unit_name_translation(pdiplomat),
                    improvement_name_translation(pimprove),
                    city_link(pcity));
      diplomat_charge_movement(pdiplomat, pcity->tile);
      send_unit_info(pplayer, pdiplomat);
      log_debug("sabotage: target improvement not found: %d (%s)",
                improvement, improvement_rule_name(pimprove));
      return;
    }
  }

  /* Now, the fun stuff!  Do the sabotage! */
  if (NULL == ptarget) {
     char prod[256];

    /* Do it. */
    pcity->shield_stock = 0;
    nullify_prechange_production(pcity); /* Make it impossible to recover */

    /* Report it. */
    universal_name_translation(&pcity->production, prod, sizeof(prod));

    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("Your %s succeeded in destroying"
                    " the production of %s in %s."),
                  unit_link(pdiplomat),
                  prod,
                  city_name(pcity));
    notify_player(cplayer, city_tile(pcity),
                  E_ENEMY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("The production of %s was destroyed in %s,"
                    " %s are suspected."),
                  prod,
                  city_link(pcity),
                  nation_plural_for_player(pplayer));
    log_debug("sabotage: sabotaged production");
  } else {
    int vulnerability = ptarget->sabotage;

    /* Sabotage a city improvement. */

    /*
     * One last chance to get caught:
     * If target was specified, and it is in the capital or are
     * City Walls, then there is a 50% chance of getting caught.
     */
    vulnerability -= (vulnerability
		      * get_city_bonus(pcity, EFT_SPY_RESISTANT) / 100);

    if (fc_rand(100) >= vulnerability) {
      /* Caught! */
      notify_player(pplayer, city_tile(pcity),
                    E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s was caught in the attempt of sabotage!"),
                    unit_tile_link(pdiplomat));
      notify_player(cplayer, city_tile(pcity),
                    E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                    _("You caught %s %s attempting"
                      " to sabotage the %s in %s!"),
                    nation_adjective_for_player(pplayer),
                    unit_tile_link(pdiplomat),
                    improvement_name_translation(ptarget),
                    city_link(pcity));
      wipe_unit(pdiplomat, ULR_CAUGHT);
      log_debug("sabotage: caught in capital or on city walls");
      return;
    }

    /* Report it. */
    notify_player(pplayer, city_tile(pcity),
                  E_MY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("Your %s destroyed the %s in %s."),
                  unit_link(pdiplomat),
                  improvement_name_translation(ptarget),
                  city_link(pcity));
    notify_player(cplayer, city_tile(pcity),
                  E_ENEMY_DIPLOMAT_SABOTAGE, ftc_server,
                  _("The %s destroyed the %s in %s."),
                  nation_plural_for_player(pplayer),
                  improvement_name_translation(ptarget),
                  city_link(pcity));
    log_debug("sabotage: sabotaged improvement: %d (%s)",
              improvement_number(ptarget),
              improvement_rule_name(ptarget));

    /* Do it. */
    building_lost(pcity, ptarget);
  }

  /* Update clients. */
  send_city_info(NULL, pcity);

  /* this may cause a diplomatic incident */
  maybe_cause_incident(DIPLOMAT_SABOTAGE, pplayer, cplayer,
                       city_tile(pcity), city_link(pcity));

  /* Check if a spy survives her mission. Diplomats never do. */
  diplomat_escape(pplayer, pdiplomat, pcity);
}

/**************************************************************************
  This subtracts the destination movement cost from a diplomat/spy.
**************************************************************************/
static void diplomat_charge_movement (struct unit *pdiplomat, struct tile *ptile)
{
  pdiplomat->moves_left -=
    map_move_cost_unit(pdiplomat, ptile);
  if (pdiplomat->moves_left < 0) {
    pdiplomat->moves_left = 0;
  }
}

/**************************************************************************
  This determines if a diplomat/spy succeeds against some defender,
  who is also a diplomat or spy. Note: a superspy attacker always
  succeeds, otherwise a superspy defender always wins.

  Return TRUE if the "attacker" succeeds.
**************************************************************************/
static bool diplomat_success_vs_defender(struct unit *pattacker,
                                         struct unit *pdefender,
                                         struct tile *pdefender_tile)
{
  int chance = 50; /* Base 50% chance */

  if (unit_has_type_flag(pattacker, UTYF_SUPERSPY)) {
    return TRUE;
  }
  if (unit_has_type_flag(pdefender, UTYF_SUPERSPY)) {
    return FALSE;
  }

  /* Add or remove 25% if spy flag. */
  if (unit_has_type_flag(pattacker, UTYF_SPY)) {
    chance += 25;
  }
  if (unit_has_type_flag(pdefender, UTYF_SPY)) {
    chance -= 25;
  }

  /* Use power_fact from veteran level to modify chance in a linear way.
   * Equal veteran levels cancel out.
   * It's probably not good for rulesets to allow this to have more than
   * 20% effect. */
  {
    const struct veteran_level
      *vatt = utype_veteran_level(unit_type(pattacker), pattacker->veteran);
    const struct veteran_level
      *vdef = utype_veteran_level(unit_type(pdefender), pdefender->veteran);
    fc_assert_ret_val(vatt != NULL && vdef != NULL, FALSE);
    chance += vatt->power_fact - vdef->power_fact;
  }

  if (tile_city(pdefender_tile)) {
    /* Reduce the chance of an attack by EFT_SPY_RESISTANT percent. */
    chance -= chance * get_city_bonus(tile_city(pdefender_tile),
                                      EFT_SPY_RESISTANT) / 100;
  } else {
    /* Reduce the chance of an attack if BF_DIPLOMAT_DEFENSE is active. */
    if (tile_has_base_flag_for_unit(pdefender_tile, unit_type(pdefender),
                                    BF_DIPLOMAT_DEFENSE)) {
      chance -= chance * 25 / 100; /* 25% penalty */
    }
  }

  return (int)fc_rand(100) < chance;
}

/**************************************************************************
  This determines if a diplomat/spy succeeds in infiltrating a tile.

  - The infiltrator must go up against each defender.
  - One or the other is eliminated in each contest.

  - Return TRUE if the infiltrator succeeds.

  'pplayer' is the player who tries to do a spy/diplomat action on 'ptile'
  with the unit 'pdiplomat' against 'cplayer'.
**************************************************************************/
static bool diplomat_infiltrate_tile(struct player *pplayer,
                                     struct player *cplayer,
                                     struct unit *pdiplomat,
                                     struct tile *ptile)
{
  char link_city[MAX_LEN_LINK] = "";
  char link_diplomat[MAX_LEN_LINK];
  char link_unit[MAX_LEN_LINK];
  struct city *pcity = tile_city(ptile);

  if (pcity) {
    /* N.B.: *_link() always returns the same pointer. */
    sz_strlcpy(link_city, city_link(pcity));
  }

  /* We don't need a _safe iterate since no transporters should be
   * destroyed. */
  unit_list_iterate(ptile->units, punit) {
    struct player *uplayer = unit_owner(punit);

    if (unit_has_type_flag(punit, UTYF_DIPLOMAT)
        || unit_has_type_flag(punit, UTYF_SUPERSPY)) {
      /* A UTYF_SUPERSPY unit may not actually be a spy, but a superboss
       * which we cannot allow puny diplomats from getting the better
       * of. Note that diplomat_success_vs_defender() is always TRUE
       * if the attacker is UTYF_SUPERSPY. Hence UTYF_SUPERSPY vs UTYF_SUPERSPY
       * in a diplomatic contest always kills the attacker. */

      if (diplomat_success_vs_defender(pdiplomat, punit, ptile) 
          && !unit_has_type_flag(punit, UTYF_SUPERSPY)) {
        /* Defending Spy/Diplomat dies. */

        /* N.B.: *_link() always returns the same pointer. */
        sz_strlcpy(link_unit, unit_tile_link(punit));
        sz_strlcpy(link_diplomat, unit_link(pdiplomat));

        notify_player(pplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                      /* TRANS: <unit> ... <diplomat> */
                      _("An enemy %s has been eliminated by your %s."),
                      link_unit, link_diplomat);

        if (pcity) {
          if (uplayer == cplayer) {
            notify_player(cplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                          /* TRANS: <unit> ... <city> ... <diplomat> */
                          _("Your %s has been eliminated defending %s"
                            " against a %s."), link_unit, link_city,
                          link_diplomat);
          } else {
            notify_player(cplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                          /* TRANS: <nation adj> <unit> ... <city>
                           * TRANS: ... <diplomat> */
                          _("A %s %s has been eliminated defending %s "
                            "against a %s."),
                          nation_adjective_for_player(uplayer),
                          link_unit, link_city, link_diplomat);
            notify_player(uplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                          /* TRANS: ... <unit> ... <nation adj> <city>
                           * TRANS: ... <diplomat> */
                          _("Your %s has been eliminated defending %s %s "
                            "against a %s."), link_unit,
                          nation_adjective_for_player(cplayer),
                          link_city, link_diplomat);
          }
        } else {
          if (uplayer == cplayer) {
            notify_player(cplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                          /* TRANS: <unit> ... <diplomat> */
                          _("Your %s has been eliminated defending "
                            "against a %s."), link_unit, link_diplomat);
          } else {
            notify_player(cplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                          /* TRANS: <nation adj> <unit> ... <diplomat> */
                          _("A %s %s has been eliminated defending "
                            "against a %s."),
                          nation_adjective_for_player(uplayer),
                          link_unit, link_diplomat);
            notify_player(uplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                          /* TRANS: ... <unit> ... <diplomat> */
                          _("Your %s has been eliminated defending "
                            "against a %s."), link_unit, link_diplomat);
          }
        }

        pdiplomat->moves_left = MAX(0, pdiplomat->moves_left - SINGLE_MOVE);

        /* Attacking unit became more experienced? */
        if (maybe_make_veteran(pdiplomat)) {
          notify_unit_experience(pdiplomat);
        }
        send_unit_info(pplayer, pdiplomat);
        wipe_unit(punit, ULR_ELIMINATED);
        return FALSE;
      } else {
        /* Attacking Spy/Diplomat dies. */

        /* N.B.: *_link() always returns the same pointer. */
        sz_strlcpy(link_unit, unit_link(punit));
        sz_strlcpy(link_diplomat, unit_tile_link(pdiplomat));

        notify_player(pplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                      _("Your %s was eliminated by a defending %s."),
                      link_diplomat, link_unit);

        if (pcity) {
          if (uplayer == cplayer) {
            notify_player(cplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                          _("Eliminated a %s %s while infiltrating %s."),
                          nation_adjective_for_player(pplayer),
                          link_diplomat, link_city);
          } else {
            notify_player(cplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                          _("A %s %s eliminated a %s %s while infiltrating "
                            "%s."), nation_adjective_for_player(uplayer),
                          link_unit, nation_adjective_for_player(pplayer),
                          link_diplomat, link_city);
            notify_player(uplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                          _("Your %s eliminated a %s %s while infiltrating "
                            "%s."), link_unit,
                          nation_adjective_for_player(pplayer),
                          link_diplomat, link_city);
          }
        } else {
          if (uplayer == cplayer) {
            notify_player(cplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                          _("Eliminated a %s %s while infiltrating our troops."),
                          nation_adjective_for_player(pplayer),
                          link_diplomat);
          } else {
            notify_player(cplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                          _("A %s %s eliminated a %s %s while infiltrating our "
                            "troops."), nation_adjective_for_player(uplayer),
                          link_unit, nation_adjective_for_player(pplayer),
                          link_diplomat);
            notify_player(uplayer, ptile, E_ENEMY_DIPLOMAT_FAILED, ftc_server,
                          /* TRANS: ... <unit> ... <diplomat> */
                          _("Your %s eliminated a %s %s while infiltrating our "
                            "troops."), link_unit,
                          nation_adjective_for_player(pplayer),
                          link_diplomat);
          }
        }

	/* Defending unit became more experienced? */
	if (maybe_make_veteran(punit)) {
	  notify_unit_experience(punit);
	}
        wipe_unit(pdiplomat, ULR_ELIMINATED);
        return FALSE;
      }
    }
  } unit_list_iterate_end;

  return TRUE;
}

/**************************************************************************
  This determines if a diplomat/spy survives and escapes.
  If "pcity" is NULL, assume action was in the field.

  Spies have a game.server.diplchance specified chance of survival (better 
  if veteran):
    - Diplomats always die.
    - Escapes to home city.
    - Escapee may become a veteran.
**************************************************************************/
static void diplomat_escape(struct player *pplayer, struct unit *pdiplomat,
                            const struct city *pcity)
{
  struct tile *ptile;
  int escapechance;
  struct city *spyhome;

  escapechance = game.server.diplchance + pdiplomat->veteran * 5;

  if (pcity) {
    ptile = pcity->tile;
  } else {
    ptile = unit_tile(pdiplomat);
  }

  /* find closest city for escape target */
  spyhome = find_closest_city(ptile, NULL, unit_owner(pdiplomat), FALSE,
                              FALSE, FALSE, TRUE, FALSE);

  if (spyhome
      && unit_has_type_flag(pdiplomat, UTYF_SPY)
      && (unit_has_type_flag(pdiplomat, UTYF_SUPERSPY)
          || fc_rand (100) < escapechance)) {
    /* Attacking Spy/Diplomat survives. */
    notify_player(pplayer, ptile, E_MY_DIPLOMAT_ESCAPE, ftc_server,
                  _("Your %s has successfully completed"
                    " the mission and returned unharmed to %s."),
                  unit_link(pdiplomat),
                  city_link(spyhome));
    if (maybe_make_veteran(pdiplomat)) {
      notify_unit_experience(pdiplomat);
    }

    /* being teleported costs all movement */
    if (!teleport_unit_to_city (pdiplomat, spyhome, -1, FALSE)) {
      send_unit_info (pplayer, pdiplomat);
      log_error("Bug in diplomat_escape: Spy can't teleport.");
      return;
    }

    return;
  } else {
    if (pcity) {
      notify_player(pplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s was captured after completing"
                      " the mission in %s."),
                    unit_tile_link(pdiplomat),
                    city_link(pcity));
    } else {
      notify_player(pplayer, ptile, E_MY_DIPLOMAT_FAILED, ftc_server,
                    _("Your %s was captured after completing"
                      " the mission."),
                    unit_tile_link(pdiplomat));
    }
  }

  wipe_unit(pdiplomat, ULR_CAUGHT);
}

/****************************************************************************
  After an action of a diplomat, maybe it will cause a diplomatic incident
  with the victim.
****************************************************************************/
static void maybe_cause_incident(enum diplomat_actions action,
                                 struct player *offender,
                                 struct player *victim_player,
                                 struct tile *victim_tile,
                                 const char *victim_link)
{
  if (!pplayers_at_war(offender, victim_player)) {
    switch (action) {
    case DIPLOMAT_BRIBE:
      notify_player(offender, victim_tile,
                    E_DIPLOMATIC_INCIDENT, ftc_server,
                    _("You have caused an incident while bribing "
                      "the %s %s."),
                    nation_adjective_for_player(victim_player), victim_link);
      notify_player(victim_player, victim_tile,
                    E_DIPLOMATIC_INCIDENT, ftc_server,
                    _("%s has caused an incident while bribing your %s."),
                    player_name(offender), victim_link);
      break;
    case DIPLOMAT_STEAL:
      notify_player(offender, victim_tile,
                    E_DIPLOMATIC_INCIDENT, ftc_server,
                    _("You have caused an incident while attempting "
                      "to steal tech from %s."), player_name(victim_player));
      notify_player(victim_player, victim_tile,
                    E_DIPLOMATIC_INCIDENT, ftc_server,
                    _("%s has caused an incident while attempting "
                      "to steal tech from you."), player_name(offender));
      break;
    case DIPLOMAT_INCITE:
      notify_player(offender, victim_tile,
                    E_DIPLOMATIC_INCIDENT, ftc_server,
                    _("You have caused an incident while inciting a "
                      "revolt in %s."), victim_link);
      notify_player(victim_player, victim_tile,
                    E_DIPLOMATIC_INCIDENT, ftc_server,
                    _("The %s have caused an incident while inciting a "
                      "revolt in %s."),
                    nation_plural_for_player(offender), victim_link);
      break;
    case DIPLOMAT_MOVE:
    case DIPLOMAT_EMBASSY:
    case DIPLOMAT_INVESTIGATE:
      return; /* These are not considered offences */
    case DIPLOMAT_ANY_ACTION:
    case SPY_POISON:
    case SPY_SABOTAGE_UNIT:
    case DIPLOMAT_SABOTAGE:
      /* You can only do these when you are at war, so we should never
       * get inside this "if" */
      fc_assert_ret(FALSE);
    }
    player_diplstate_get(victim_player, offender)->has_reason_to_cancel = 2;
    call_incident(INCIDENT_DIPLOMAT, offender, victim_player);
    send_player_all_c(offender, NULL);
    send_player_all_c(victim_player, NULL);
  }

  return;
}

/**************************************************************************
 return number of diplomats on this square.  AJS 20000130
**************************************************************************/
int count_diplomats_on_tile(struct tile *ptile)
{
  int count = 0;

  unit_list_iterate((ptile)->units, punit) {
    if (unit_has_type_flag(punit, UTYF_DIPLOMAT)) {
      count++;
    }
  } unit_list_iterate_end;

  return count;
}
