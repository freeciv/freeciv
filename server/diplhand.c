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

#include "diptreaty.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "unit.h"

#include "script.h"

#include "citytools.h"
#include "cityturn.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "techtools.h"
#include "unittools.h"

#include "advdiplomacy.h"

#include "diplhand.h"

#define SPECLIST_TAG treaty
#define SPECLIST_TYPE struct Treaty
#include "speclist.h"

#define treaty_list_iterate(list, p) \
    TYPED_LIST_ITERATE(struct Treaty, list, p)
#define treaty_list_iterate_end  LIST_ITERATE_END

static struct treaty_list *treaties = NULL;

/* FIXME: Should this be put in a ruleset somewhere? */
#define TURNS_LEFT 16

/**************************************************************************
...
**************************************************************************/
void diplhand_init()
{
  treaties = treaty_list_new();
}

/**************************************************************************
...
**************************************************************************/
void diplhand_free()
{
  treaty_list_unlink_all(treaties);
  treaty_list_free(treaties);
  treaties = NULL;
}

/**************************************************************************
...
**************************************************************************/
struct Treaty *find_treaty(struct player *plr0, struct player *plr1)
{
  treaty_list_iterate(treaties, ptreaty) {
    if ((ptreaty->plr0 == plr0 && ptreaty->plr1 == plr1) ||
	(ptreaty->plr0 == plr1 && ptreaty->plr1 == plr0)) {
      return ptreaty;
    }
  } treaty_list_iterate_end;

  return NULL;
}

/**************************************************************************
pplayer clicked the accept button. If he accepted the treaty we check the
clauses. If both players have now accepted the treaty we execute the agreed
clauses.
**************************************************************************/
void handle_diplomacy_accept_treaty_req(struct player *pplayer,
					int counterpart)
{
  struct Treaty *ptreaty;
  struct player *pother;
  bool *player_accept, *other_accept;
  enum dipl_reason diplcheck;

  if (!is_valid_player_id(counterpart) || pplayer->player_no == counterpart) {
    return;
  }

  pother = get_player(counterpart);
  ptreaty = find_treaty(pplayer, pother);

  if (!ptreaty) {
    return;
  }

  if (ptreaty->plr0 == pplayer) {
    player_accept = &ptreaty->accept0;
    other_accept = &ptreaty->accept1;
  } else {
    player_accept = &ptreaty->accept1;
    other_accept = &ptreaty->accept0;
  }

  if (!*player_accept) {	/* Tries to accept. */

    /* Check that player who accepts can keep what (s)he promises. */

    clause_list_iterate(ptreaty->clauses, pclause) {
      struct city *pcity = NULL;

      if (pclause->from == pplayer) {
	switch(pclause->type) {
	case CLAUSE_EMBASSY:
          if (player_has_embassy(pother, pplayer)) {
            freelog(LOG_ERROR, "%s tried to give embassy to %s, who already "
                    "has an embassy", pplayer->name, pother->name);
            return;
          }
          break;
	case CLAUSE_ADVANCE:
          if (!tech_is_available(pother, pclause->value)) {
	    /* It is impossible to give a technology to a civilization that
	     * can never possess it (the client should enforce this). */
	    freelog(LOG_ERROR, "Treaty: The %s can't have tech %s",
                    get_nation_name_plural(pother->nation),
		    get_tech_name(pplayer, pclause->value));
	    notify_player(pplayer, NULL, E_DIPLOMACY,
                          _("The %s can't accept %s."),
                          get_nation_name_plural(pother->nation),
			  get_tech_name(pplayer, pclause->value));
	    return;
          }
	  if (get_invention(pplayer, pclause->value) != TECH_KNOWN) {
	    freelog(LOG_ERROR,
                    "The %s don't know tech %s, but try to give it to the %s.",
		    get_nation_name_plural(pplayer->nation),
		    get_tech_name(pplayer, pclause->value),
		    get_nation_name_plural(pother->nation));
	    notify_player(pplayer, NULL, E_DIPLOMACY,
			  _("You don't have tech %s, you can't accept treaty."),
			  get_tech_name(pplayer, pclause->value));
	    return;
	  }
	  break;
	case CLAUSE_CITY:
	  pcity = find_city_by_id(pclause->value);
	  if (!pcity) { /* Can't find out cityname any more. */
	    notify_player(pplayer, NULL, E_DIPLOMACY,
			  _("City you are trying to give no longer exists, "
			    "you can't accept treaty."));
	    return;
	  }
	  if (pcity->owner != pplayer) {
	    notify_player(pplayer, NULL, E_DIPLOMACY,
			  _("You are not owner of %s, you can't accept treaty."),
			  pcity->name);
	    return;
	  }
	  if (is_capital(pcity)) {
	    notify_player(pplayer, NULL, E_DIPLOMACY,
			  _("Your capital (%s) is requested, "
			    "you can't accept treaty."),
			  pcity->name);
	    return;
	  }
	  break;
	case CLAUSE_CEASEFIRE:
          diplcheck = pplayer_can_make_treaty(pplayer, pother, DS_CEASEFIRE);
          if (diplcheck != DIPL_OK) {
            return;
          }
          break;
	case CLAUSE_PEACE:
          diplcheck = pplayer_can_make_treaty(pplayer, pother, DS_PEACE);
          if (diplcheck != DIPL_OK) {
            return;
          }
          break;
	case CLAUSE_ALLIANCE:
          diplcheck = pplayer_can_make_treaty(pplayer, pother, DS_ALLIANCE);
          if (diplcheck != DIPL_OK) {
            return;
          }
          break;
	case CLAUSE_GOLD:
	  if (pplayer->economic.gold < pclause->value) {
	    notify_player(pplayer, NULL, E_DIPLOMACY,
			  _("You don't have enough gold, "
			    "you can't accept treaty."));
	    return;
	  }
	  break;
	default:
	  ; /* nothing */
	}
      }
    } clause_list_iterate_end;
  }

  *player_accept = ! *player_accept;

  dlsend_packet_diplomacy_accept_treaty(pplayer->connections,
					pother->player_no, *player_accept,
					*other_accept);
  dlsend_packet_diplomacy_accept_treaty(pother->connections,
					pplayer->player_no, *other_accept,
					*player_accept);

  if (ptreaty->accept0 && ptreaty->accept1) {
    int nclauses = clause_list_size(ptreaty->clauses);

    dlsend_packet_diplomacy_cancel_meeting(pplayer->connections,
					   pother->player_no,
					   pplayer->player_no);
    dlsend_packet_diplomacy_cancel_meeting(pother->connections,
					   pplayer->player_no,
 					   pplayer->player_no);

    notify_player(pplayer, NULL, E_DIPLOMACY,
		  PL_("A treaty containing %d clause was agreed upon.",
		      "A treaty containing %d clauses was agreed upon.",
		      nclauses),
		  nclauses);
    notify_player(pother, NULL, E_DIPLOMACY,
		  PL_("A treaty containing %d clause was agreed upon.",
		      "A treaty containing %d clauses was agreed upon.",
		      nclauses),
		  nclauses);

    /* Check that one who accepted treaty earlier still have everything
       (s)he promised to give. */

    clause_list_iterate(ptreaty->clauses, pclause) {
      struct city *pcity;
      if (pclause->from == pother) {
	switch (pclause->type) {
	case CLAUSE_CITY:
	  pcity = find_city_by_id(pclause->value);
	  if (!pcity) { /* Can't find out cityname any more. */
	    notify_player(pplayer, NULL, E_DIPLOMACY,
			  _("One of the cities %s is giving away is destroyed! "
			    "Treaty canceled!"),
			  get_nation_name_plural(pother->nation));
	    notify_player(pother, NULL, E_DIPLOMACY,
			  _("One of the cities %s is giving away is destroyed! "
			    "Treaty canceled!"),
			  get_nation_name_plural(pother->nation));
	    goto cleanup;
	  }
	  if (pcity->owner != pother) {
	    notify_player(pplayer, NULL, E_DIPLOMACY,
			  _("The %s no longer control %s! "
			    "Treaty canceled!"),
			  get_nation_name_plural(pother->nation),
			  pcity->name);
	    notify_player(pother, NULL, E_DIPLOMACY,
			  _("The %s no longer control %s! "
			    "Treaty canceled!"),
			  get_nation_name_plural(pother->nation),
			  pcity->name);
	    goto cleanup;
	  }
	  if (is_capital(pcity)) {
	    notify_player(pother, NULL, E_DIPLOMACY,
			  _("Your capital (%s) is requested, "
			    "you can't accept treaty."), pcity->name);
	    goto cleanup;
	  }

	  break;
	case CLAUSE_ALLIANCE:
          /* We need to recheck this way since things might have
           * changed. */
          diplcheck = pplayer_can_make_treaty(pplayer, pother, DS_ALLIANCE);
          if (diplcheck != DIPL_OK) {
            goto cleanup;
          }
          break;
  case CLAUSE_PEACE:
          diplcheck = pplayer_can_make_treaty(pplayer, pother, DS_PEACE);
          if (diplcheck != DIPL_OK) {
            goto cleanup;
          }
          break;
  case CLAUSE_CEASEFIRE:
          diplcheck = pplayer_can_make_treaty(pplayer, pother, DS_CEASEFIRE);
          if (diplcheck != DIPL_OK) {
            goto cleanup;
          }
          break;
	case CLAUSE_GOLD:
	  if (pother->economic.gold < pclause->value) {
	    notify_player(pplayer, NULL, E_DIPLOMACY,
			  _("The %s don't have the promised amount "
			    "of gold! Treaty canceled!"),
			  get_nation_name_plural(pother->nation));
	    notify_player(pother, NULL, E_DIPLOMACY,
			  _("The %s don't have the promised amount "
			    "of gold! Treaty canceled!"),
			  get_nation_name_plural(pother->nation));
	    goto cleanup;
	  }
	  break;
	default:
	  ; /* nothing */
	}
      }
    } clause_list_iterate_end;

    if (pplayer->ai.control) {
      ai_treaty_accepted(pplayer, pother, ptreaty);
    }
    if (pother->ai.control) {
      ai_treaty_accepted(pother, pplayer, ptreaty);
    }

    clause_list_iterate(ptreaty->clauses, pclause) {
      struct player *pgiver = pclause->from;
      struct player *pdest = (pplayer == pgiver) ? pother : pplayer;
      enum diplstate_type old_diplstate = 
        pgiver->diplstates[pdest->player_no].type;

      switch (pclause->type) {
      case CLAUSE_EMBASSY:
        establish_embassy(pdest, pgiver); /* sic */
        notify_player(pgiver, NULL, E_TREATY_SHARED_VISION,
                         _("You gave an embassy to %s."),
                         pdest->name);
        notify_player(pdest, NULL, E_TREATY_SHARED_VISION,
                         _("%s allowed you to create an embassy!"),
                         pgiver->name);
        break;
      case CLAUSE_ADVANCE:
        /* It is possible that two players open the diplomacy dialog
         * and try to give us the same tech at the same time. This
         * should be handled discreetly instead of giving a core dump. */
        if (get_invention(pdest, pclause->value) == TECH_KNOWN) {
	  freelog(LOG_VERBOSE,
                  "The %s already know tech %s, that %s want to give them.",
		  get_nation_name_plural(pdest->nation),
		  get_tech_name(pplayer, pclause->value),
		  get_nation_name_plural(pgiver->nation));
          break;
        }
	notify_player(pdest, NULL, E_TECH_GAIN,
			 _("You are taught the knowledge of %s."),
			 get_tech_name(pdest, pclause->value));

	notify_embassies(pdest, pgiver, NULL, E_TECH_GAIN,
			 _("The %s have acquired %s from the %s."),
			 get_nation_name_plural(pdest->nation),
			 get_tech_name(pdest, pclause->value),
			 get_nation_name_plural(pgiver->nation));

	script_signal_emit("tech_researched", 3,
			   API_TYPE_TECH_TYPE, &advances[pclause->value],
			   API_TYPE_PLAYER, pdest,
			   API_TYPE_STRING, "traded");
	do_dipl_cost(pdest, pclause->value);

	found_new_tech(pdest, pclause->value, FALSE, TRUE);
	break;
      case CLAUSE_GOLD:
	notify_player(pdest, NULL, E_DIPLOMACY,
		      _("You get %d gold."), pclause->value);
	pgiver->economic.gold -= pclause->value;
	pdest->economic.gold += pclause->value * (100 - game.info.diplcost) / 100;
	break;
      case CLAUSE_MAP:
	give_map_from_player_to_player(pgiver, pdest);
	notify_player(pdest, NULL, E_DIPLOMACY,
		      _("You receive %s's worldmap."),
		      pgiver->name);
	break;
      case CLAUSE_SEAMAP:
	give_seamap_from_player_to_player(pgiver, pdest);
	notify_player(pdest, NULL, E_DIPLOMACY,
		      _("You receive %s's seamap."),
		      pgiver->name);
	break;
      case CLAUSE_CITY:
	{
	  struct city *pcity = find_city_by_id(pclause->value);

	  if (!pcity) {
	    freelog(LOG_NORMAL,
		    "Treaty city id %d not found - skipping clause.",
		    pclause->value);
	    break;
	  }

	  notify_player(pdest, pcity->tile, E_CITY_TRANSFER,
			   _("You receive city of %s from %s."),
			   pcity->name, pgiver->name);

	  notify_player(pgiver, pcity->tile, E_CITY_LOST,
			   _("You give city of %s to %s."),
			   pcity->name, pdest->name);

	  transfer_city(pdest, pcity, -1, TRUE, TRUE, FALSE);
	  break;
	}
      case CLAUSE_CEASEFIRE:
	pgiver->diplstates[pdest->player_no].type=DS_CEASEFIRE;
	pgiver->diplstates[pdest->player_no].turns_left = TURNS_LEFT;
	pdest->diplstates[pgiver->player_no].type=DS_CEASEFIRE;
	pdest->diplstates[pgiver->player_no].turns_left = TURNS_LEFT;
	notify_player(pgiver, NULL, E_TREATY_CEASEFIRE,
			 _("You agree on a cease-fire with %s."),
			 pdest->name);
	notify_player(pdest, NULL, E_TREATY_CEASEFIRE,
			 _("You agree on a cease-fire with %s."),
			 pgiver->name);
	if (old_diplstate == DS_ALLIANCE) {
	  update_players_after_alliance_breakup(pgiver, pdest);
	}
	check_city_workers(pplayer);
	check_city_workers(pother);
	break;
      case CLAUSE_PEACE:
	pgiver->diplstates[pdest->player_no].type = DS_ARMISTICE;
	pdest->diplstates[pgiver->player_no].type = DS_ARMISTICE;
	pgiver->diplstates[pdest->player_no].turns_left = TURNS_LEFT;
	pdest->diplstates[pgiver->player_no].turns_left = TURNS_LEFT;
	pgiver->diplstates[pdest->player_no].max_state = 
          MAX(DS_PEACE, pgiver->diplstates[pdest->player_no].max_state);
	pdest->diplstates[pgiver->player_no].max_state = 
          MAX(DS_PEACE, pdest->diplstates[pgiver->player_no].max_state);
	notify_player(pgiver, NULL, E_TREATY_PEACE,
		      PL_("You agree on an armistice with %s. In %d turn "
			  "it will turn into a peace treaty. Move your "
			  "units out of %s's territory.",
			  "You agree on an armistice with %s. In %d turns "
			  "it will turn into a peace treaty. Move your "
			  "units out of %s's territory.",
			  TURNS_LEFT),
		      pdest->name, TURNS_LEFT, pdest->name);
	notify_player(pdest, NULL, E_TREATY_PEACE,
		      PL_("You agree on an armistice with %s. In %d turn "
			  "it will turn into a peace treaty. Move your "
			  "units out of %s's territory.",
			  "You agree on an armistice with %s. In %d turns "
			  "it will turn into a peace treaty. Move your "
			  "units out of %s's territory.",
			  TURNS_LEFT),
		      pgiver->name, TURNS_LEFT, pgiver->name);
	if (old_diplstate == DS_ALLIANCE) {
	  update_players_after_alliance_breakup(pgiver, pdest);
	}
	check_city_workers(pplayer);
	check_city_workers(pother);
	break;
      case CLAUSE_ALLIANCE:
	pgiver->diplstates[pdest->player_no].type=DS_ALLIANCE;
	pdest->diplstates[pgiver->player_no].type=DS_ALLIANCE;
	pgiver->diplstates[pdest->player_no].max_state = 
          MAX(DS_ALLIANCE, pgiver->diplstates[pdest->player_no].max_state);
	pdest->diplstates[pgiver->player_no].max_state = 
          MAX(DS_ALLIANCE, pdest->diplstates[pgiver->player_no].max_state);
	notify_player(pgiver, NULL, E_TREATY_ALLIANCE,
			 _("You agree on an alliance with %s."),
			 pdest->name);
	notify_player(pdest, NULL, E_TREATY_ALLIANCE,
			 _("You agree on an alliance with %s."),
			 pgiver->name);

	check_city_workers(pplayer);
	check_city_workers(pother);
	break;
      case CLAUSE_VISION:
	give_shared_vision(pgiver, pdest);
	notify_player(pgiver, NULL, E_TREATY_SHARED_VISION,
			 _("You give shared vision to %s."),
			 pdest->name);
	notify_player(pdest, NULL, E_TREATY_SHARED_VISION,
			 _("%s gives you shared vision."),
			 pgiver->name);
	break;
      case CLAUSE_LAST:
        freelog(LOG_ERROR, "Received bad clause type");
        break;
      }

    } clause_list_iterate_end;
  cleanup:
    treaty_list_unlink(treaties, ptreaty);
    clear_treaty(ptreaty);
    free(ptreaty);
    send_player_info(pplayer, NULL);
    send_player_info(pother, NULL);
  }
}

/****************************************************************************
  Create an embassy. pplayer gets an embassy with aplayer.
****************************************************************************/
void establish_embassy(struct player *pplayer, struct player *aplayer)
{
  /* Establish the embassy. */
  BV_SET(pplayer->embassy, aplayer->player_no);
  send_player_info(pplayer, pplayer);
  send_player_info(pplayer, aplayer);  /* update player dialog with embassy */
  send_player_info(aplayer, pplayer);  /* INFO_EMBASSY level info */
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_remove_clause_req(struct player *pplayer,
					int counterpart, int giver,
					enum clause_type type, int value)
{
  struct Treaty *ptreaty;
  struct player *pgiver, *pother;

  if (!is_valid_player_id(counterpart) || pplayer->player_no == counterpart
      || !is_valid_player_id(giver)) {
    return;
  }

  pother = get_player(counterpart);
  pgiver = get_player(giver);

  if (pgiver != pplayer && pgiver != pother) {
    return;
  }
  
  ptreaty = find_treaty(pplayer, pother);

  if (ptreaty && remove_clause(ptreaty, pgiver, type, value)) {
    dlsend_packet_diplomacy_remove_clause(pplayer->connections,
					  pother->player_no, giver, type,
					  value);
    dlsend_packet_diplomacy_remove_clause(pother->connections,
					  pplayer->player_no, giver, type,
					  value);
    if (pplayer->ai.control) {
      ai_treaty_evaluate(pplayer, pother, ptreaty);
    }
    if (pother->ai.control) {
      ai_treaty_evaluate(pother, pplayer, ptreaty);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_create_clause_req(struct player *pplayer,
					int counterpart, int giver,
					enum clause_type type, int value)
{
  struct Treaty *ptreaty;
  struct player *pgiver, *pother;

  if (!is_valid_player_id(counterpart) || pplayer->player_no == counterpart
      || !is_valid_player_id(giver)) {
    return;
  }

  pother = get_player(counterpart);
  pgiver = get_player(giver);

  if (pgiver != pplayer && pgiver != pother) {
    return;
  }

  ptreaty = find_treaty(pplayer, pother);

  if (ptreaty && add_clause(ptreaty, pgiver, type, value)) {
    /* 
     * If we are trading cities, then it is possible that the
     * dest is unaware of it's existence.  We have 2 choices,
     * forbid it, or lighten that area.  If we assume that
     * the giver knows what they are doing, then 2. is the
     * most powerful option - I'll choose that for now.
     *                           - Kris Bubendorfer
     */
    if (type == CLAUSE_CITY) {
      struct city *pcity = find_city_by_id(value);

      if (pcity && !map_is_known_and_seen(pcity->tile, pother, V_MAIN))
	give_citymap_from_player_to_player(pcity, pplayer, pother);
    }

    dlsend_packet_diplomacy_create_clause(pplayer->connections,
					  pother->player_no, giver, type,
					  value);
    dlsend_packet_diplomacy_create_clause(pother->connections,
					  pplayer->player_no, giver, type,
					  value);
    if (pplayer->ai.control) {
      ai_treaty_evaluate(pplayer, pother, ptreaty);
    }
    if (pother->ai.control) {
      ai_treaty_evaluate(pother, pplayer, ptreaty);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void really_diplomacy_cancel_meeting(struct player *pplayer,
					    struct player *pother)
{
  struct Treaty *ptreaty = find_treaty(pplayer, pother);

  if (ptreaty) {
    dlsend_packet_diplomacy_cancel_meeting(pother->connections,
					   pplayer->player_no,
					   pplayer->player_no);
    notify_player(pother, NULL, E_DIPLOMACY,
		  _("%s canceled the meeting!"), 
		  pplayer->name);
    /* Need to send to pplayer too, for multi-connects: */
    dlsend_packet_diplomacy_cancel_meeting(pplayer->connections,
					   pother->player_no,
					   pplayer->player_no);
    notify_player(pplayer, NULL, E_DIPLOMACY,
		  _("Meeting with %s canceled."), 
		  pother->name);
    treaty_list_unlink(treaties, ptreaty);
    clear_treaty(ptreaty);
    free(ptreaty);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_cancel_meeting_req(struct player *pplayer,
					 int counterpart)
{
  if (!is_valid_player_id(counterpart) || pplayer->player_no == counterpart) {
    return;
  }

  really_diplomacy_cancel_meeting(pplayer, get_player(counterpart));
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_init_meeting_req(struct player *pplayer,
				       int counterpart)
{
  struct player *pother;

  if (!is_valid_player_id(counterpart) || pplayer->player_no == counterpart) {
    return;
  }

  pother = get_player(counterpart);

  if (find_treaty(pplayer, pother)) {
    return;
  }

  if (get_player_bonus(pplayer, EFT_NO_DIPLOMACY)
      || get_player_bonus(pother, EFT_NO_DIPLOMACY)) {
    notify_player(pplayer, NULL, E_DIPLOMACY,
		  _("Your diplomatic envoy was decapitated!"));
    return;
  }

  if (could_meet_with_player(pplayer, pother)) {
    struct Treaty *ptreaty;

    ptreaty = fc_malloc(sizeof(*ptreaty));
    init_treaty(ptreaty, pplayer, pother);
    treaty_list_prepend(treaties, ptreaty);

    dlsend_packet_diplomacy_init_meeting(pplayer->connections,
					 pother->player_no,
					 pplayer->player_no);
    dlsend_packet_diplomacy_init_meeting(pother->connections,
					 pplayer->player_no,
					 pplayer->player_no);
  }
}

/**************************************************************************
  Send information on any on-going diplomatic meetings for connection's
  player.  For re-connections.
**************************************************************************/
void send_diplomatic_meetings(struct connection *dest)
{
  struct player *pplayer = dest->player;

  if (!pplayer) {
    return;
  }
  players_iterate(other) {
    struct Treaty *ptreaty = find_treaty(pplayer, other);

    if (ptreaty) {
      assert(pplayer != other);
      dsend_packet_diplomacy_init_meeting(dest, other->player_no,
                                          pplayer->player_no);
      clause_list_iterate(ptreaty->clauses, pclause) {
        dsend_packet_diplomacy_create_clause(dest, 
                                             other->player_no,
                                             pclause->from->player_no,
                                             pclause->type,
                                             pclause->value);
      } clause_list_iterate_end;
      if (ptreaty->plr0 == pplayer) {
        dsend_packet_diplomacy_accept_treaty(dest, other->player_no,
                                             ptreaty->accept0, 
                                             ptreaty->accept1);
      } else {
        dsend_packet_diplomacy_accept_treaty(dest, other->player_no,
                                             ptreaty->accept1, 
                                             ptreaty->accept0);
      }
    }
  } players_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void cancel_all_meetings(struct player *pplayer)
{
  players_iterate(pplayer2) {
    if (find_treaty(pplayer, pplayer2)) {
      really_diplomacy_cancel_meeting(pplayer, pplayer2);
    }
  } players_iterate_end;
}

/**************************************************************************
  Reject all treaties currently being negotiated
**************************************************************************/
void reject_all_treaties(struct player *pplayer)
{
  struct Treaty* treaty;
  players_iterate(pplayer2) {
    treaty = find_treaty(pplayer, pplayer2);
    if (!treaty) {
      continue;
    }
    treaty->accept0 = FALSE;
    treaty->accept1 = FALSE;
    dlsend_packet_diplomacy_accept_treaty(pplayer->connections,
					  pplayer2->player_no,
					  FALSE,
					  FALSE);
    dlsend_packet_diplomacy_accept_treaty(pplayer2->connections,
                                          pplayer->player_no,
					  FALSE,
					  FALSE);
  } players_iterate_end;
}

