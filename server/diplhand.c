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

#include "citytools.h"
#include "cityturn.h"
#include "gamelog.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "unittools.h"

#include "advdiplomacy.h"

#include "diplhand.h"

#define SPECLIST_TAG treaty
#define SPECLIST_TYPE struct Treaty
#include "speclist.h"

#define SPECLIST_TAG treaty
#define SPECLIST_TYPE struct Treaty
#include "speclist_c.h"

#define treaty_list_iterate(list, p) \
    TYPED_LIST_ITERATE(struct Treaty, list, p)
#define treaty_list_iterate_end  LIST_ITERATE_END

static struct treaty_list treaties;
static bool did_init_treaties;

/**************************************************************************
Checks if the player numbers in the packet are sane.
**************************************************************************/
static bool check_packet(struct packet_diplomacy_info *packet, bool check_other)
{
  if (packet->plrno0 >= game.nplayers) {
    freelog(LOG_ERROR, "plrno0 is %d >= game.nplayers (%d).",
	    packet->plrno0, game.nplayers);
    return FALSE;
  }
  if (packet->plrno1 >= game.nplayers) {
    freelog(LOG_ERROR, "plrno1 is %d >= game.nplayers (%d).",
	    packet->plrno1, game.nplayers);
    return FALSE;
  }
  if (check_other && packet->plrno_from >= game.nplayers) {
    freelog(LOG_ERROR, "plrno_from is %d >= game.nplayers (%d).",
	    packet->plrno_from, game.nplayers);
    return FALSE;
  }

  if (packet->plrno_from != packet->plrno0
      && packet->plrno_from != packet->plrno1) {
    freelog(LOG_ERROR, "plrno_from(%d) != plrno0(%d) and plrno1(%d)",
	    packet->plrno_from, packet->plrno0, packet->plrno1);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
struct Treaty *find_treaty(struct player *plr0, struct player *plr1)
{
  if (!did_init_treaties) {
    treaty_list_init(&treaties);
    did_init_treaties = TRUE;
  }

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
void handle_diplomacy_accept_treaty(struct player *pplayer, 
				    struct packet_diplomacy_info *packet)
{
  struct Treaty *ptreaty;
  struct player *plr0, *plr1, *pgiver, *pdest, *other;
  bool *giver_accept;

  if (!check_packet(packet, TRUE))
    return;

  plr0 = &game.players[packet->plrno0];
  plr1 = &game.players[packet->plrno1];
  pgiver = &game.players[packet->plrno_from];
  ptreaty = find_treaty(plr0, plr1);

  if (!ptreaty)
    return;
  if (pgiver != pplayer)
    return;

  if (ptreaty->plr0 == pgiver) {
    giver_accept = &ptreaty->accept0;
  } else {
    giver_accept = &ptreaty->accept1;
  }

  other = (plr0==pplayer) ? plr1 : plr0;
  if (! *giver_accept) { /* Tries to accept. */

    /* Check that player who accepts can keep what (s)he promises. */

    clause_list_iterate(ptreaty->clauses, pclause) {
      struct city *pcity;

      if (pclause->from == pplayer) {
	switch(pclause->type) {
	case CLAUSE_ADVANCE:
          if (!tech_is_available(other, pclause->value)) {
	    /* It is impossible to give a technology to a civilization that
	     * can never possess it (the client should enforce this). */
	    freelog(LOG_ERROR, "Treaty: The %s can't have tech %s",
                    get_nation_name_plural(other->nation),
                    advances[pclause->value].name);
	    notify_player(pplayer,
                          _("Game: The %s can't accept %s."),
                          get_nation_name_plural(other->nation),
                          advances[pclause->value].name);
	    return;
          }
	  if (get_invention(pplayer, pclause->value) != TECH_KNOWN) {
	    freelog(LOG_ERROR,
                    "The %s don't know tech %s, but try to give it to the %s.",
		    get_nation_name_plural(pplayer->nation),
		    advances[pclause->value].name,
		    get_nation_name_plural(other->nation));
	    notify_player(pplayer,
			  _("Game: You don't have tech %s, you can't accept treaty."),
			  advances[pclause->value].name);
	    return;
	  }
	  break;
	case CLAUSE_CITY:
	  pcity = find_city_by_id(pclause->value);
	  if (!pcity) { /* Can't find out cityname any more. */
	    notify_player(pplayer,
			  _("City you are trying to give no longer exists, "
			    "you can't accept treaty."));
	    return;
	  }
	  if (pcity->owner != pplayer->player_no) {
	    notify_player(pplayer,
			  _("You are not owner of %s, you can't accept treaty."),
			  pcity->name);
	    return;
	  }
	  if (city_got_building(pcity,B_PALACE)) {
	    notify_player(pplayer,
			  _("Game: Your capital (%s) is requested, "
			    "you can't accept treaty."),
			  pcity->name);
	    return;
	  }
	  break;
	case CLAUSE_ALLIANCE:
          if (!pplayer_can_ally(pplayer, other)) {
	    notify_player(pplayer,
			  _("Game: You are at war with one of %s's "
			    "allies - an alliance with %s is impossible."),
			  other->name, other->name);
            return;
          }
          if (!pplayer_can_ally(other, pplayer)) {
	    notify_player(pplayer,
			  _("Game: %s is at war with one of your allies "
			    "- an alliance with %s is impossible."),
			  other->name, other->name);
            return;
          }
          break;
	case CLAUSE_GOLD:
	  if (pplayer->economic.gold < pclause->value) {
	    notify_player(pplayer,
			  _("Game: You don't have enough gold, "
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

  *giver_accept = ! *giver_accept;

  lsend_packet_diplomacy_info(&plr0->connections, 
			      PACKET_DIPLOMACY_ACCEPT_TREATY, 
			      packet);
  lsend_packet_diplomacy_info(&plr1->connections, 
			      PACKET_DIPLOMACY_ACCEPT_TREATY, 
			      packet);

  if (ptreaty->accept0 && ptreaty->accept1) {
    int nclauses = clause_list_size(&ptreaty->clauses);

    lsend_packet_diplomacy_info(&plr0->connections,
				PACKET_DIPLOMACY_CANCEL_MEETING, 
				packet);
    lsend_packet_diplomacy_info(&plr1->connections, 
				PACKET_DIPLOMACY_CANCEL_MEETING, 
				packet);

    notify_player(plr0,
		  PL_("Game: A treaty containing %d clause was agreed upon.",
		      "Game: A treaty containing %d clauses was agreed upon.",
		      nclauses),
		  nclauses);
    notify_player(plr1,
		  PL_("Game: A treaty containing %d clause was agreed upon.",
		      "Game: A treaty containing %d clauses was agreed upon.",
		      nclauses),
		  nclauses);
    gamelog(GAMELOG_TREATY, _("%s and %s agree to a treaty"),
	    get_nation_name_plural(plr0->nation),
	    get_nation_name_plural(plr1->nation));

    /* Check that one who accepted treaty earlier still have everything
       (s)he promised to give. */

    clause_list_iterate(ptreaty->clauses, pclause) {
      struct city *pcity;
      if (pclause->from == other) {
	switch (pclause->type) {
	case CLAUSE_CITY:
	  pcity = find_city_by_id(pclause->value);
	  if (!pcity) { /* Can't find out cityname any more. */
	    notify_player(plr0,
			  _("Game: One of the cities %s is giving away is destroyed! "
			    "Treaty canceled!"),
			  get_nation_name_plural(other->nation));
	    notify_player(plr1,
			  _("Game: One of the cities %s is giving away is destroyed! "
			    "Treaty canceled!"),
			  get_nation_name_plural(other->nation));
	    goto cleanup;
	  }
	  if (pcity->owner != other->player_no) {
	    notify_player(plr0,
			  _("Game: The %s no longer control %s! "
			    "Treaty canceled!"),
			  get_nation_name_plural(other->nation),
			  pcity->name);
	    notify_player(plr1,
			  _("Game: The %s no longer control %s! "
			    "Treaty canceled!"),
			  get_nation_name_plural(other->nation),
			  pcity->name);
	    goto cleanup;
	  }
	  break;
	case CLAUSE_ALLIANCE:
          /* We need to recheck this way since things might have
           * changed. */
          if (!pplayer_can_ally(other, pplayer)) {
	    notify_player(pplayer,
			  _("Game: %s is at war with one of your "
			    "allies - an alliance with %s is impossible."),
			  other->name, other->name);
	    notify_player(other,
			  _("Game: You are at war with one of %s's "
			    "allies - an alliance with %s is impossible."),
			  pplayer->name, pplayer->name);
	    goto cleanup;
          }
          break;
	case CLAUSE_GOLD:
	  if (other->economic.gold < pclause->value) {
	    notify_player(plr0,
			  _("Game: The %s don't have the promised amount "
			    "of gold! Treaty canceled!"),
			  get_nation_name_plural(other->nation));
	    notify_player(plr1,
			  _("Game: The %s don't have the promised amount "
			    "of gold! Treaty canceled!"),
			  get_nation_name_plural(other->nation));
	    goto cleanup;
	  }
	  break;
	default:
	  ; /* nothing */
	}
      }
    } clause_list_iterate_end;

    if (plr0->ai.control) {
      ai_treaty_accepted(plr0, plr1, ptreaty);
    }
    if (plr1->ai.control) {
      ai_treaty_accepted(plr1, plr0, ptreaty);
    }

    clause_list_iterate(ptreaty->clauses, pclause) {
      pgiver = pclause->from;
      pdest = (plr0==pgiver) ? plr1 : plr0;

      switch (pclause->type) {
      case CLAUSE_ADVANCE:
        /* It is possible that two players open the diplomacy dialog
         * and try to give us the same tech at the same time. This
         * should be handled discreetly instead of giving a core dump. */
        if (get_invention(pdest, pclause->value) == TECH_KNOWN) {
	  freelog(LOG_VERBOSE,
                  "The %s already know tech %s, that %s want to give them.",
		  get_nation_name_plural(pdest->nation),
		  advances[pclause->value].name,
		  get_nation_name_plural(pgiver->nation));
          break;
        }
	notify_player_ex(pdest, -1, -1, E_TECH_GAIN,
			 _("Game: You are taught the knowledge of %s."),
			 get_tech_name(pdest, pclause->value));

	notify_embassies(pdest, pgiver,
			 _("Game: The %s have acquired %s from the %s."),
			 get_nation_name_plural(pdest->nation),
			 get_tech_name(pdest, pclause->value),
			 get_nation_name_plural(pgiver->nation));

	gamelog(GAMELOG_TECH, _("%s acquire %s (Treaty) from %s"),
		get_nation_name_plural(pdest->nation),
		get_tech_name(pdest, pclause->value),
		get_nation_name_plural(pgiver->nation));

	do_dipl_cost(pdest);

	found_new_tech(pdest, pclause->value, FALSE, TRUE);
	break;
      case CLAUSE_GOLD:
	notify_player(pdest, _("Game: You get %d gold."), pclause->value);
	pgiver->economic.gold -= pclause->value;
	pdest->economic.gold += pclause->value;
	break;
      case CLAUSE_MAP:
	give_map_from_player_to_player(pgiver, pdest);
	notify_player(pdest, _("Game: You receive %s's worldmap."),
		      pgiver->name);
	break;
      case CLAUSE_SEAMAP:
	give_seamap_from_player_to_player(pgiver, pdest);
	notify_player(pdest, _("Game: You receive %s's seamap."),
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

	  notify_player_ex(pdest, pcity->x, pcity->y, E_CITY_TRANSFER,
			   _("Game: You receive city of %s from %s."),
			   pcity->name, pgiver->name);

	  notify_player_ex(pgiver, pcity->x, pcity->y, E_CITY_LOST,
			   _("Game: You give city of %s to %s."),
			   pcity->name, pdest->name);

	  transfer_city(pdest, pcity, -1, TRUE, TRUE, FALSE);
	  break;
	}
      case CLAUSE_CEASEFIRE:
	pgiver->diplstates[pdest->player_no].type=DS_CEASEFIRE;
	pgiver->diplstates[pdest->player_no].turns_left=16;
	pdest->diplstates[pgiver->player_no].type=DS_CEASEFIRE;
	pdest->diplstates[pgiver->player_no].turns_left=16;
	notify_player_ex(pgiver, -1, -1, E_TREATY_CEASEFIRE,
			 _("Game: You agree on a cease-fire with %s."),
			 pdest->name);
	notify_player_ex(pdest, -1, -1, E_TREATY_CEASEFIRE,
			 _("Game: You agree on a cease-fire with %s."),
			 pgiver->name);
	check_city_workers(plr0);
	check_city_workers(plr1);
	break;
      case CLAUSE_PEACE:
	pgiver->diplstates[pdest->player_no].type=DS_PEACE;
	pdest->diplstates[pgiver->player_no].type=DS_PEACE;
	notify_player_ex(pgiver, -1, -1, E_TREATY_PEACE,
			 _("Game: You agree on a peace treaty with %s."),
			 pdest->name);
	notify_player_ex(pdest, -1, -1, E_TREATY_PEACE,
			 _("Game: You agree on a peace treaty with %s."),
			 pgiver->name);
	check_city_workers(plr0);
	check_city_workers(plr1);
	break;
      case CLAUSE_ALLIANCE:
	pgiver->diplstates[pdest->player_no].type=DS_ALLIANCE;
	pdest->diplstates[pgiver->player_no].type=DS_ALLIANCE;
	notify_player_ex(pgiver, -1, -1, E_TREATY_ALLIANCE,
			 _("Game: You agree on an alliance with %s."),
			 pdest->name);
	notify_player_ex(pdest, -1, -1, E_TREATY_ALLIANCE,
			 _("Game: You agree on an alliance with %s."),
			 pgiver->name);
	check_city_workers(plr0);
	check_city_workers(plr1);
	break;
      case CLAUSE_VISION:
	give_shared_vision(pgiver, pdest);
	notify_player_ex(pgiver, -1, -1, E_TREATY_SHARED_VISION,
			 _("Game: You give shared vision to %s."),
			 pdest->name);
	notify_player_ex(pdest, -1, -1, E_TREATY_SHARED_VISION,
			 _("Game: %s gives you shared vision."),
			 pgiver->name);
	break;
      case CLAUSE_LAST:
        freelog(LOG_ERROR, "Received bad clause type");
        break;
      }

    } clause_list_iterate_end;
  cleanup:
    treaty_list_unlink(&treaties, ptreaty);
    free(ptreaty);
    send_player_info(plr0, NULL);
    send_player_info(plr1, NULL);
  }
}

/****************************************************************************
  Create an embassy. pplayer gets an embassy with aplayer.
****************************************************************************/
void establish_embassy(struct player *pplayer, struct player *aplayer)
{
  /* Establish the embassy. */
  pplayer->embassy |= (1 << aplayer->player_no);
  send_player_info(pplayer, pplayer);
  send_player_info(pplayer, aplayer);  /* update player dialog with embassy */
  send_player_info(aplayer, pplayer);  /* INFO_EMBASSY level info */
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_remove_clause(struct player *pplayer, 
				    struct packet_diplomacy_info *packet)
{
  struct Treaty *ptreaty;
  struct player *plr0, *plr1, *pgiver;

  if (!check_packet(packet, TRUE))
    return;

  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];
  pgiver=&game.players[packet->plrno_from];
  
  if((ptreaty=find_treaty(plr0, plr1))) {
    if(remove_clause(ptreaty, pgiver, packet->clause_type, packet->value)) {
      lsend_packet_diplomacy_info(&plr0->connections, 
				 PACKET_DIPLOMACY_REMOVE_CLAUSE, packet);
      lsend_packet_diplomacy_info(&plr1->connections, 
				 PACKET_DIPLOMACY_REMOVE_CLAUSE, packet);
      if (plr0->ai.control) {
        ai_treaty_evaluate(plr0, plr1, ptreaty);
      }
      if (plr1->ai.control) {
        ai_treaty_evaluate(plr1, plr0, ptreaty);
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_create_clause(struct player *pplayer, 
				    struct packet_diplomacy_info *packet)
{
  struct Treaty *ptreaty;
  struct player *plr0, *plr1, *pgiver;

  if (!check_packet(packet, TRUE))
    return;

  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];
  pgiver=&game.players[packet->plrno_from];

  if ((ptreaty=find_treaty(plr0, plr1))) {
    if (add_clause(ptreaty, pgiver, packet->clause_type, packet->value)) {
      /* 
       * If we are trading cities, then it is possible that the
       * dest is unaware of it's existence.  We have 2 choices,
       * forbid it, or lighten that area.  If we assume that
       * the giver knows what they are doing, then 2. is the
       * most powerful option - I'll choose that for now.
       *                           - Kris Bubendorfer
       */
      if (packet->clause_type == CLAUSE_CITY){
	struct city *pcity = find_city_by_id(packet->value);
	if (pcity && !map_is_known_and_seen(pcity->x, pcity->y, plr1))
	  give_citymap_from_player_to_player(pcity, plr0, plr1);
      }

      lsend_packet_diplomacy_info(&plr0->connections, 
				 PACKET_DIPLOMACY_CREATE_CLAUSE, 
				 packet);
      lsend_packet_diplomacy_info(&plr1->connections, 
				 PACKET_DIPLOMACY_CREATE_CLAUSE, 
				 packet);
      if (plr0->ai.control) {
        ai_treaty_evaluate(plr0, plr1, ptreaty);
      }
      if (plr1->ai.control) {
        ai_treaty_evaluate(plr1, plr0, ptreaty);
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void really_diplomacy_cancel_meeting(struct player *pplayer,
					    struct player *pplayer2)
{
  struct Treaty *ptreaty = find_treaty(pplayer, pplayer2);
  struct packet_diplomacy_info packet;

  if (ptreaty) {
    packet.plrno0 = pplayer->player_no;
    packet.plrno1 = pplayer2->player_no;
    packet.plrno_from = pplayer->player_no;

    lsend_packet_diplomacy_info(&pplayer2->connections, 
			       PACKET_DIPLOMACY_CANCEL_MEETING, 
			       &packet);
    notify_player(pplayer2, _("Game: %s canceled the meeting!"), 
		  pplayer->name);
    /* Need to send to pplayer too, for multi-connects: */
    lsend_packet_diplomacy_info(&pplayer->connections, 
			       PACKET_DIPLOMACY_CANCEL_MEETING, 
			       &packet);
    notify_player(pplayer, _("Game: Meeting with %s canceled."), 
		  pplayer2->name);
    treaty_list_unlink(&treaties, ptreaty);
    free(ptreaty);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_cancel_meeting(struct player *pplayer, 
				     struct packet_diplomacy_info *packet)
{
  struct player *plr0, *plr1, *theother;

  if (!check_packet(packet, FALSE))
    return;

  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];
  
  theother = (pplayer==plr0) ? plr1 : plr0;

  really_diplomacy_cancel_meeting(pplayer, theother);
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_init(struct player *pplayer, 
			   struct packet_diplomacy_info *packet)
{
  struct packet_diplomacy_info pa;
  struct player *plr0, *plr1;

  if (!check_packet(packet, FALSE))
    return;

  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];

  assert(plr0 != plr1);

  if (!find_treaty(plr0, plr1)) {
    if (is_barbarian(plr0) || is_barbarian(plr1)) {
      notify_player(plr0, _("Your diplomatic envoy was decapitated!"));
      return;
    }

    if (could_meet_with_player(plr0, plr1)) {
      struct Treaty *ptreaty;

      ptreaty=fc_malloc(sizeof(struct Treaty));
      init_treaty(ptreaty, plr0, plr1);
      treaty_list_insert(&treaties, ptreaty);

      pa.plrno0=plr0->player_no;
      pa.plrno1=plr1->player_no;

      /* Send at least INFO_MEETING level information */
      send_player_info(plr0, plr1);
      send_player_info(plr1, plr0);

      lsend_packet_diplomacy_info(&plr0->connections,
				  PACKET_DIPLOMACY_INIT_MEETING, &pa);

      pa.plrno0=plr1->player_no;
      pa.plrno1=plr0->player_no;
      lsend_packet_diplomacy_info(&plr1->connections,
				  PACKET_DIPLOMACY_INIT_MEETING, &pa);
    }
  }
}

/**************************************************************************
  Send information on any on-going diplomatic meetings for connection's
  player.  (For re-connection in multi-connect case.)
**************************************************************************/
void send_diplomatic_meetings(struct connection *dest)
{
  struct Treaty *ptreaty;
  struct player *pplayer = dest->player;

  if (!pplayer) {
    return;
  }
  players_iterate(other_player) {

    if ( (ptreaty=find_treaty(pplayer, other_player))) {
      struct packet_diplomacy_info packet;
      
      packet.plrno0 = pplayer->player_no;
      packet.plrno1 = other_player->player_no;
      
      send_packet_diplomacy_info(dest, PACKET_DIPLOMACY_INIT_MEETING, &packet);
      
      clause_list_iterate(ptreaty->clauses, pclause) {
	packet.clause_type = pclause->type;
	packet.plrno_from = pclause->from->player_no;
	packet.value = pclause->value;
  
	send_packet_diplomacy_info(dest, PACKET_DIPLOMACY_CREATE_CLAUSE,
				   &packet);
      } clause_list_iterate_end;
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
