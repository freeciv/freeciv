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
#include "diptreaty.h"
#include "gamelog.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "unittools.h"

#include "diplhand.h"

static struct genlist treaties;
static int did_init_treaties;


/**************************************************************************
Checks if the player numbers in the packet are sane.
**************************************************************************/
static int check_packet(struct packet_diplomacy_info *packet, int check_other)
{
  if (packet->plrno0 >= game.nplayers) {
    freelog(LOG_ERROR, "plrno0 is %d >= game.nplayers (%d).",
	    packet->plrno0, game.nplayers);
    return 0;
  }
  if (packet->plrno1 >= game.nplayers) {
    freelog(LOG_ERROR, "plrno1 is %d >= game.nplayers (%d).",
	    packet->plrno1, game.nplayers);
    return 0;
  }
  if (check_other && packet->plrno_from >= game.nplayers) {
    freelog(LOG_ERROR, "plrno_from is %d >= game.nplayers (%d).",
	    packet->plrno_from, game.nplayers);
    return 0;
  }

  if (packet->plrno_from != packet->plrno0
      && packet->plrno_from != packet->plrno1) {
    freelog(LOG_ERROR, "plrno_from(%d) != plrno0(%d) and plrno1(%d)",
	    packet->plrno_from, packet->plrno0, packet->plrno1);
    return 0;
  }

  return 1;
}

/**************************************************************************
...
**************************************************************************/
struct Treaty *find_treaty(struct player *plr0, struct player *plr1)
{
  struct genlist_iterator myiter;
  
  if(!did_init_treaties) {
    genlist_init(&treaties);
    did_init_treaties=1;
  }

  genlist_iterator_init(&myiter, &treaties, 0);
  
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Treaty *ptreaty=(struct Treaty *)ITERATOR_PTR(myiter);
    if((ptreaty->plr0==plr0 && ptreaty->plr1==plr1) ||
       (ptreaty->plr0==plr1 && ptreaty->plr1==plr0))
      return ptreaty;
  }
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
  int *giver_accept;

  if (!check_packet(packet, 1))
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
    lsend_packet_diplomacy_info(&plr0->connections,
				PACKET_DIPLOMACY_CANCEL_MEETING, 
				packet);
    lsend_packet_diplomacy_info(&plr1->connections, 
				PACKET_DIPLOMACY_CANCEL_MEETING, 
				packet);

    notify_player(plr0,
		  _("Game: A treaty containing %d clauses was agreed upon."),
		  clause_list_size(&ptreaty->clauses));
    notify_player(plr1,
		  _("Game: A treaty containing %d clauses was agreed upon."),
		  clause_list_size(&ptreaty->clauses));
    gamelog(GAMELOG_TREATY, "%s and %s agree to a treaty",
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

    clause_list_iterate(ptreaty->clauses, pclause) {
      pgiver = pclause->from;
      pdest = (plr0==pgiver) ? plr1 : plr0;

      switch (pclause->type) {
      case CLAUSE_ADVANCE:
	notify_player(pdest, _("Game: You are taught the knowledge of %s."),
		      advances[pclause->value].name);

	notify_embassies(pdest,pgiver,
			 _("Game: The %s have aquired %s from the %s."),
			 get_nation_name_plural(pdest->nation),
			 advances[pclause->value].name,
			 get_nation_name_plural(pgiver->nation));

	gamelog(GAMELOG_TECH, "%s acquire %s (Treaty) from %s",
		get_nation_name_plural(pdest->nation),
		advances[pclause->value].name,
		get_nation_name_plural(pgiver->nation));

	do_dipl_cost(pdest);

	found_new_tech(pdest, pclause->value, 0, 1);
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
	  struct city *pnewcity = NULL;

	  if (!pcity) {
	    freelog(LOG_NORMAL,
		    "Treaty city id %d not found - skipping clause.",
		    pclause->value);
	    break;
	  }

	  notify_player(pdest, _("Game: You receive city of %s from %s."),
			pcity->name, pgiver->name);

	  notify_player(pgiver, _("Game: You give city of %s to %s."),
			pcity->name, pdest->name);

	  pnewcity = transfer_city(pdest, pcity, -1, 1, 1, 0);
	  if (!pnewcity) {
	    freelog(LOG_NORMAL, "Transfer city returned no city - skipping clause.");
	    break;
	  }
	  break;
	}
      case CLAUSE_CEASEFIRE:
	pgiver->diplstates[pdest->player_no].type=DS_CEASEFIRE;
	pgiver->diplstates[pdest->player_no].turns_left=16;
	pdest->diplstates[pgiver->player_no].type=DS_CEASEFIRE;
	pdest->diplstates[pgiver->player_no].turns_left=16;
	notify_player(pgiver, _("Game: You agree on a cease-fire with %s."),
		      pdest->name);
	notify_player(pdest, _("Game: You agree on a cease-fire with %s."),
		      pgiver->name);
	check_city_workers(plr0);
	check_city_workers(plr1);
	break;
      case CLAUSE_PEACE:
	pgiver->diplstates[pdest->player_no].type=DS_PEACE;
	pdest->diplstates[pgiver->player_no].type=DS_PEACE;
	notify_player(pgiver, _("Game: You agree on peace with %s."),
		      pdest->name);
	notify_player(pdest, _("Game: You agree on peace with %s."),
		      pgiver->name);    
	check_city_workers(plr0);
	check_city_workers(plr1);
	break;
      case CLAUSE_ALLIANCE:
	pgiver->diplstates[pdest->player_no].type=DS_ALLIANCE;
	pdest->diplstates[pgiver->player_no].type=DS_ALLIANCE;
	notify_player(pgiver, _("Game: You agree on an alliance with %s."),
		      pdest->name);
	notify_player(pdest, _("Game: You agree on an alliance with %s."),
		      pgiver->name);
	check_city_workers(plr0);
	check_city_workers(plr1);
	break;          
      case CLAUSE_VISION:
	give_shared_vision(pgiver, pdest);
	notify_player(pgiver, _("Game: You give shared vision to %s."),
		      pdest->name);
	notify_player(pdest, _("Game: %s gives you shared vision."),
		      pgiver->name);
	break;
      }

    } clause_list_iterate_end;
  cleanup:      
    genlist_unlink(&treaties, ptreaty);
    free(ptreaty);
    send_player_info(plr0, NULL);
    send_player_info(plr1, NULL);
  }
}


/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_remove_clause(struct player *pplayer, 
				    struct packet_diplomacy_info *packet)
{
  struct Treaty *ptreaty;
  struct player *plr0, *plr1, *pgiver;

  if (!check_packet(packet, 1))
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

  if (!check_packet(packet, 1))
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
	if (pcity && !map_get_known_and_seen(pcity->x, pcity->y, plr1))
	  give_citymap_from_player_to_player(pcity, plr0, plr1);
      }

      lsend_packet_diplomacy_info(&plr0->connections, 
				 PACKET_DIPLOMACY_CREATE_CLAUSE, 
				 packet);
      lsend_packet_diplomacy_info(&plr1->connections, 
				 PACKET_DIPLOMACY_CREATE_CLAUSE, 
				 packet);
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
    genlist_unlink(&treaties, ptreaty);
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

  if (!check_packet(packet, 0))
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

  if (!check_packet(packet, 0))
    return;

  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];

  if (!find_treaty(plr0, plr1)) {
    if (plr0->ai.control || plr1->ai.control) {
      notify_player(plr0, _("AI controlled players cannot participate in "
			    "diplomatic meetings."));
      return;
    }

    if (player_has_embassy(plr0, plr1) && plr0->is_connected && 
	plr0->is_alive && plr1->is_connected && plr1->is_alive) {
      struct Treaty *ptreaty;

      ptreaty=fc_malloc(sizeof(struct Treaty));
      init_treaty(ptreaty, plr0, plr1);
      genlist_insert(&treaties, ptreaty, 0);

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

  if (pplayer==NULL) {
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
