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

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "rand.h"
#include "support.h"

#include "citytools.h"
#include "cityturn.h"
#include "mapgen.h"		/* assign_continent_numbers */
#include "plrhand.h"           /* notify_player */
#include "sernet.h"
#include "srv_main.h"
#include "unittools.h"
#include "unithand.h"

#include "maphand.h"

static void player_tile_init(int x, int y, struct player *pplayer);
static void give_tile_info_from_player_to_player(struct player *pfrom,
						 struct player *pdest,
						 int x, int y);
static void send_tile_info_always(struct player *pplayer,
				  struct conn_list *dest, int x, int y);
static void send_NODRAW_tiles(struct player *pplayer,
			      struct conn_list *dest, int x, int y, int len);
static int map_get_sent(int x, int y, struct player *pplayer);
static void map_set_sent(int x, int y, struct player *pplayer);
static void map_clear_sent(int x, int y, struct player *pplayer);
static void set_unknown_tiles_to_unsent(struct player *pplayer);
static void shared_vision_change_seen(int x, int y, struct player *pplayer, int change);
static int map_get_seen(int x, int y, struct player *pplayer);

/**************************************************************************
Used only in global_warming() and nuclear_winter() below.
**************************************************************************/
static int is_terrain_ecologically_wet(int x, int y)
{
  return (map_get_terrain(x, y) == T_RIVER
	  || map_get_special(x, y) & S_RIVER
	  || is_terrain_near_tile(x, y, T_OCEAN)
	  || is_terrain_near_tile(x, y, T_RIVER)
	  || is_special_near_tile(x, y, S_RIVER));
}

/**************************************************************************
...
**************************************************************************/
void global_warming(int effect)
{
  int x, y, k;

  freelog(LOG_NORMAL, "Global warming: %d", game.heating);

  k = map_num_tiles();
  while(effect && k--) {
    rand_map_pos(&x, &y);
    if (map_get_terrain(x, y) != T_OCEAN) {
      if (is_terrain_ecologically_wet(x, y)) {
	switch (map_get_terrain(x, y)) {
	case T_FOREST:
	  effect--;
	  change_terrain(x, y, T_JUNGLE);
	  send_tile_info(NULL, x, y);
	  break;
	case T_DESERT:
	case T_PLAINS:
	case T_GRASSLAND:
	  effect--;
	  change_terrain(x, y, T_SWAMP);
	  send_tile_info(NULL, x, y);
	  break;
	default:
	  break;
	}
      } else {
	switch (map_get_terrain(x, y)) {
	case T_PLAINS:
	case T_GRASSLAND:
	case T_FOREST:
	  effect--;
	  change_terrain(x, y, T_DESERT);
	  send_tile_info(NULL, x, y);
	  break;
	default:
	  break;
	}
      }
      unit_list_iterate(map_get_tile(x, y)->units, punit) {
	if (!can_unit_continue_current_activity(punit))
	  handle_unit_activity_request(punit, ACTIVITY_IDLE);
      } unit_list_iterate_end;
    }
  }

  notify_player_ex(0, -1, -1, E_GLOBAL_ECO,
		   _("Game: Global warming has occurred!"));
  notify_player(0, _("Game: Coastlines have been flooded and vast "
		     "ranges of grassland have become deserts."));
}

/**************************************************************************
...
**************************************************************************/
void nuclear_winter(int effect)
{
  int x, y, k;

  freelog(LOG_NORMAL, "Nuclear winter: %d", game.cooling);

  k = map_num_tiles();
  while(effect && k--) {
    rand_map_pos(&x, &y);
    if (map_get_terrain(x, y) != T_OCEAN) {
      switch (map_get_terrain(x, y)) {
      case T_JUNGLE:
      case T_SWAMP:
      case T_PLAINS:
      case T_GRASSLAND:
	effect--;
	change_terrain(x, y,
		       is_terrain_ecologically_wet(x, y) ? T_DESERT : T_TUNDRA);
	send_tile_info(NULL, x, y);
	break;
      case T_TUNDRA:
	effect--;
	change_terrain(x, y, T_ARCTIC);
	send_tile_info(NULL, x, y);
	break;
      default:
	break;
      }
      unit_list_iterate(map_get_tile(x, y)->units, punit) {
	if (!can_unit_continue_current_activity(punit))
	  handle_unit_activity_request(punit, ACTIVITY_IDLE);
      } unit_list_iterate_end;
    }
  }

  notify_player_ex(0, -1, -1, E_GLOBAL_ECO,
		   _("Game: Nuclear winter has occurred!"));
  notify_player(0, _("Game: Wetlands have dried up and vast "
		     "ranges of grassland have become tundra."));
}

/***************************************************************
To be called when a player gains the Railroad tech for the first
time.  Sends a message, and then upgrade all city squares to
railroads.  "discovery" just affects the message: set to
   1 if the tech is a "discovery",
   0 if otherwise aquired (conquer/trade/GLib).        --dwp
***************************************************************/
void upgrade_city_rails(struct player *pplayer, int discovery)
{
  if (!(terrain_control.may_road)) {
    return;
  }

  conn_list_do_buffer(&pplayer->connections);

  if (discovery) {
    notify_player(pplayer,
		  _("Game: New hope sweeps like fire through the country as "
		    "the discovery of railroad is announced.\n"
		    "      Workers spontaneously gather and upgrade all "
		    "cities with railroads."));
  } else {
    notify_player(pplayer,
		  _("Game: The people are pleased to hear that your "
		    "scientists finally know about railroads.\n"
		    "      Workers spontaneously gather and upgrade all "
		    "cities with railroads."));
  }
  
  city_list_iterate(pplayer->cities, pcity) {
    map_set_special(pcity->x, pcity->y, S_RAILROAD);
    send_tile_info(0, pcity->x, pcity->y);
  }
  city_list_iterate_end;

  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
static void buffer_shared_vision(struct player *pplayer)
{
  players_iterate(pplayer2) {
    if (pplayer->really_gives_vision & (1<<pplayer2->player_no))
      conn_list_do_buffer(&pplayer2->connections);
  } players_iterate_end;
  conn_list_do_buffer(&pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
static void unbuffer_shared_vision(struct player *pplayer)
{
  players_iterate(pplayer2) {
    if (pplayer->really_gives_vision & (1<<pplayer2->player_no))
      conn_list_do_unbuffer(&pplayer2->connections);
  } players_iterate_end;
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
void give_map_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  buffer_shared_vision(pdest);
  whole_map_iterate(x, y) {
    give_tile_info_from_player_to_player(pfrom, pdest, x, y);
  } whole_map_iterate_end;
  unbuffer_shared_vision(pdest);
}

/**************************************************************************
...
**************************************************************************/
void give_seamap_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  buffer_shared_vision(pdest);
  whole_map_iterate(x, y) {
    if (map_get_terrain(x, y) == T_OCEAN)
      give_tile_info_from_player_to_player(pfrom, pdest, x, y);
  } whole_map_iterate_end;
  unbuffer_shared_vision(pdest);
}

/**************************************************************************
...
**************************************************************************/
void give_citymap_from_player_to_player(struct city *pcity,
					struct player *pfrom, struct player *pdest)
{
  buffer_shared_vision(pdest);
  map_city_radius_iterate(pcity->x, pcity->y, x, y) {
    give_tile_info_from_player_to_player(pfrom, pdest, x, y);
  } map_city_radius_iterate_end;
  unbuffer_shared_vision(pdest);
}

/**************************************************************************
  Send all tiles known to specified clients.
  If dest is NULL means game.game_connections.
  
  Note for multiple connections this may change "sent" multiple times
  for single player.  This is ok, because "sent" data is just optimised
  calculations, so it will be correct before this, for each connection
  during this, and at end.
**************************************************************************/
void send_all_known_tiles(struct conn_list *dest)
{
  int tiles_sent;

  if (dest==NULL) dest = &game.game_connections;
  
  conn_list_iterate(*dest, pconn) {
    if (pconn->player) {
      set_unknown_tiles_to_unsent(pconn->player);
    }
  } conn_list_iterate_end;

  /* send whole map piece by piece to each player to balance the load
     of the send buffers better */
  tiles_sent = 0;
  conn_list_do_buffer(dest);

  whole_map_iterate(x, y) {
    tiles_sent++;
    if ((tiles_sent % map.xsize) == 0) {
      conn_list_do_unbuffer(dest);
      flush_packets();
      conn_list_do_buffer(dest);
    }

    conn_list_iterate(*dest, pconn) {
      struct player *pplayer = pconn->player;

      if (!pplayer && !pconn->observer) {	/* no map needed */
        continue;
      }

      if (!pplayer) {
	send_tile_info_always(pplayer, &pconn->self, x, y);
      } else if (map_get_known(x, y, pplayer)) {
	send_NODRAW_tiles(pplayer, &pconn->self, x, y, 0);
	send_tile_info_always(pplayer, &pconn->self, x, y);
      }
    } conn_list_iterate_end;
  } whole_map_iterate_end;

  conn_list_do_unbuffer(dest);
  flush_packets();
}

/**************************************************************************
  Send tile information to all the clients in dest which know and see
  the tile.  Also updates player knowledge.  If dest is NULL, sends to
  all clients (game.game_connections) which know and see tile.
**************************************************************************/
void send_tile_info(struct conn_list *dest, int x, int y)
{
  struct packet_tile_info info;
  struct tile *ptile;

  if (dest==NULL) dest = &game.game_connections;

  ptile = map_get_tile(x, y);

  info.x = x;
  info.y = y;

  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    if (pplayer==NULL && !pconn->observer) {
      continue;
    }
    if (pplayer == NULL || map_get_known_and_seen(x, y, pplayer)) {
      info.known = TILE_KNOWN;
      info.type = ptile->terrain;
      info.special = ptile->special;
      if (pplayer) {
	update_tile_knowledge(pplayer,x,y);
      }
      send_packet_tile_info(pconn, &info);
    }
  }
  conn_list_iterate_end;
}

/**************************************************************************
  Send the tile information, as viewed by pplayer, to all specified
  connections.   The tile info is sent even if pplayer doesn't see or
  know the tile (setting appropriate info.known), as required for
  client drawing requirements in some cases (see freeciv_hackers_guide.txt).
  Also updates pplayer knowledge if known and seen, else used old.
  pplayer==NULL means send "real" data, for observers
**************************************************************************/
static void send_tile_info_always(struct player *pplayer, struct conn_list *dest,
			   int x, int y)
{
  struct packet_tile_info info;
  struct tile *ptile;
  struct player_tile *plrtile;

  ptile = map_get_tile(x, y);

  info.x = x;
  info.y = y;

  if (pplayer==NULL) {	/* observer sees all */
    info.known=TILE_KNOWN;
    info.type = ptile->terrain;
    info.special = ptile->special;
  }
  else if (map_get_known(x, y, pplayer)) {
    if (map_get_seen(x, y, pplayer)) { /* known and seen */
      update_tile_knowledge(pplayer,x,y); /* visible; update info */
      info.known = TILE_KNOWN;
    } else { /* known but not seen */
      info.known = TILE_KNOWN_FOGGED;
    }
    plrtile = map_get_player_tile(x, y, pplayer);
    info.type = plrtile->terrain;
    info.special = plrtile->special;
  } else { /* unknown (the client needs these sometimes to draw correctly) */
    info.known = TILE_UNKNOWN;
    info.type = ptile->terrain;
    info.special = ptile->special;
  }
  lsend_packet_tile_info(dest, &info);
}

/**************************************************************************
...
**************************************************************************/
static int map_get_pending_seen(struct player *pplayer, int x, int y)
{
  return map_get_player_tile(x, y, pplayer)->pending_seen;
}

/**************************************************************************
...
**************************************************************************/
static void map_set_pending_seen(struct player *pplayer, int x, int y, int newv)
{
  map_get_player_tile(x, y, pplayer)->pending_seen = newv;
}

/**************************************************************************
...
**************************************************************************/
static void increment_pending_seen(struct player *pplayer, int x, int y)
{
  map_get_player_tile(x, y, pplayer)->pending_seen += 1;
}

/**************************************************************************
...
**************************************************************************/
static void decrement_pending_seen(struct player *pplayer, int x, int y)
{
  struct player_tile *plr_tile = map_get_player_tile(x, y, pplayer);
  assert(plr_tile->pending_seen != 0);
  map_get_player_tile(x, y, pplayer)->pending_seen -= 1;
}

/**************************************************************************
...
**************************************************************************/
static void reveal_pending_seen(struct player *pplayer, int x, int y, int len)
{
  square_iterate(x, y, len, x_itr, y_itr) {
    int pseen = map_get_pending_seen(pplayer, x_itr, y_itr);
    map_set_pending_seen(pplayer, x_itr, y_itr, 0);
    while (pseen > 0) {
      unfog_area(pplayer, x_itr, y_itr, 0);
      pseen--;
    }
  } square_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void really_unfog_area(struct player *pplayer, int x, int y)
{
  struct city *pcity;
  int old_known = map_get_known(x, y, pplayer);

  freelog(LOG_DEBUG, "really unfogging %d,%d\n", x, y);
  send_NODRAW_tiles(pplayer, &pplayer->connections, x, y, 0);

  map_set_known(x, y, pplayer);

  /* discover units */
  unit_list_iterate(map_get_tile(x, y)->units, punit)
    send_unit_info(pplayer, punit);
  unit_list_iterate_end;

  /* discover cities */ 
  reality_check_city(pplayer, x, y);
  if ((pcity=map_get_city(x, y)))
    send_city_info(pplayer, pcity);

  /* send info about the tile itself */
  send_tile_info_always(pplayer, &pplayer->connections, x, y);

  /* If the tile was not known before we need to refresh the cities that
     can use the tile. */
  if (!old_known) {
    map_city_radius_iterate(x, y, x1, y1) {
      pcity = map_get_city(x1, y1);
      if (pcity && city_owner(pcity) == pplayer) {
	update_city_tile_status_map(pcity, x, y);
      }
    } map_city_radius_iterate_end;
    sync_cities();
  }
}

/**************************************************************************
  Add an extra point of visibility to a square centered at x,y with
  sidelength 1+2*len, ie length 1 is normal sightrange for a unit.
  pplayer may not be NULL.
**************************************************************************/
void unfog_area(struct player *pplayer, int x, int y, int len)
{
  /* Did the tile just become visible?
     - send info about units and cities and the tile itself */
  buffer_shared_vision(pplayer);
  square_iterate(x, y, len, abs_x, abs_y) {
    /* the player himself */
    shared_vision_change_seen(abs_x, abs_y, pplayer, +1);
    if (map_get_seen(abs_x, abs_y, pplayer) == 1
	|| !map_get_known(abs_x, abs_y, pplayer))
      really_unfog_area(pplayer, abs_x, abs_y);

    /* players (s)he gives shared vision */
    players_iterate(pplayer2) {
      int playerid2 = pplayer2->player_no;
      if (!(pplayer->really_gives_vision & (1<<playerid2)))
	continue;

      if (map_get_seen(abs_x, abs_y, pplayer2) == 1
	  || !map_get_known(abs_x, abs_y, pplayer2)) {
	really_unfog_area(pplayer2, abs_x, abs_y);
      }
      reveal_pending_seen(pplayer2, abs_x, abs_y, 0);
    } players_iterate_end;
  } square_iterate_end;

  reveal_pending_seen(pplayer, x, y, len);
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
  Send KNOWN_NODRAW tiles as required by pplayer, to specified connections.
  We send only the unknown tiles around the square with length len.
  pplayer must not be NULL.
**************************************************************************/
static void send_NODRAW_tiles(struct player *pplayer, struct conn_list *dest,
			      int x, int y, int len)
{
  conn_list_do_buffer(dest);
  square_iterate(x, y, len+1, abs_x, abs_y) {
    if (!map_get_sent(abs_x, abs_y, pplayer)) {
      send_tile_info_always(pplayer, dest, abs_x, abs_y);
      map_set_sent(abs_x, abs_y, pplayer);
    }
  } square_iterate_end;
  conn_list_do_unbuffer(dest);
}

/**************************************************************************
...
**************************************************************************/
static void really_fog_area(struct player *pplayer, int x, int y)
{
  freelog(LOG_DEBUG, "Fogging %i,%i. Previous fog: %i.",
	  x, y, map_get_seen(x, y, pplayer));
 
  assert(map_get_seen(x, y, pplayer) == 0);
  update_player_tile_last_seen(pplayer, x, y);
  send_tile_info_always(pplayer, &pplayer->connections, x, y);
}

/**************************************************************************
  Remove a point of visibility from a square centered at x,y with
  sidelength 1+2*len, ie length 1 is normal sightrange for a unit.
**************************************************************************/
void fog_area(struct player *pplayer, int x, int y, int len)
{
  buffer_shared_vision(pplayer);
  square_iterate(x, y, len, abs_x, abs_y) {
    if (map_get_known(abs_x, abs_y, pplayer)) {
      /* the player himself */
      shared_vision_change_seen(abs_x, abs_y, pplayer, -1);
      if (map_get_seen(abs_x, abs_y, pplayer) == 0)
	really_fog_area(pplayer, abs_x, abs_y);

      /* players (s)he gives shared vision */
      players_iterate(pplayer2) {
	int playerid2 = pplayer2->player_no;
	if (!(pplayer->really_gives_vision & (1<<playerid2)))
	  continue;
	if (map_get_seen(abs_x, abs_y, pplayer2) == 0)
	  really_fog_area(pplayer2, abs_x, abs_y);
      } players_iterate_end;
    } else {
      decrement_pending_seen(pplayer, abs_x, abs_y);
    }
  } square_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
  Send basic map information: map size, and is_earth.
**************************************************************************/
void send_map_info(struct conn_list *dest)
{
  struct packet_map_info minfo;

  minfo.xsize=map.xsize;
  minfo.ysize=map.ysize;
  minfo.is_earth=map.is_earth;
 
  lsend_packet_map_info(dest, &minfo);
}

/**************************************************************************
...
**************************************************************************/
void map_fog_city_area(struct city *pcity)
{
  if (!pcity) {
    freelog(LOG_ERROR, "Attempting to fog non-existent city");
    return;
  }

  map_fog_pseudo_city_area(city_owner(pcity), pcity->x, pcity->y);
}

/**************************************************************************
...
**************************************************************************/
void map_unfog_city_area(struct city *pcity)
{
  if (!pcity) {
    freelog(LOG_ERROR, "Attempting to unfog non-existent city");
    return;
  }

  map_unfog_pseudo_city_area(city_owner(pcity), pcity->x, pcity->y);
}

/**************************************************************************
...
**************************************************************************/
static void shared_vision_change_seen(int x, int y, struct player *pplayer, int change)
{
  map_change_seen(x, y, pplayer, change);
  map_change_own_seen(x, y, pplayer, change);

  players_iterate(pplayer2) {
    int playerid2 = pplayer2->player_no;
    if (pplayer->really_gives_vision & (1<<playerid2))
      map_change_seen(x, y, pplayer2, change);
  } players_iterate_end;
}

/**************************************************************************
There doesn't have to be a city.
**************************************************************************/
void map_unfog_pseudo_city_area(struct player *pplayer, int x, int y)
{
  freelog(LOG_DEBUG, "Unfogging city area at %i,%i", x, y);

  buffer_shared_vision(pplayer);
  map_city_radius_iterate(x, y, x_itr, y_itr) {
    if (map_get_known(x_itr, y_itr, pplayer)) {
      unfog_area(pplayer, x_itr, y_itr, 0);
    } else {
      increment_pending_seen(pplayer, x_itr, y_itr);
    }
  } map_city_radius_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
There doesn't have to be a city.
**************************************************************************/
void map_fog_pseudo_city_area(struct player *pplayer, int x, int y)
{
  freelog(LOG_DEBUG, "Fogging city area at %i,%i", x, y);

  buffer_shared_vision(pplayer);
  map_city_radius_iterate(x, y, x_itr, y_itr) {
    if (map_get_known(x_itr, y_itr, pplayer)) {
      fog_area(pplayer, x_itr, y_itr, 0);
    } else {
      decrement_pending_seen(pplayer, x_itr, y_itr);
    }
  } map_city_radius_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
For removing a unit. The actual removal is done in server_remove_unit
**************************************************************************/
void remove_unit_sight_points(struct unit *punit)
{
  int x = punit->x, y = punit->y;
  struct player *pplayer = unit_owner(punit);

  freelog(LOG_DEBUG, "Removing unit sight points at  %i,%i", punit->x,
	  punit->y);

  if (map_get_special(punit->x, punit->y) & S_FORTRESS
      && unit_profits_of_watchtower(punit))
    fog_area(pplayer, x, y, get_watchtower_vision(punit));
  else
    fog_area(pplayer, x, y, unit_type(punit)->vision_range);
}

/**************************************************************************
Shows area even if still fogged. If the tile is not "seen" units are not
shown
**************************************************************************/
static void really_show_area(struct player *pplayer, int x, int y)
{
  struct city *pcity;
  int old_known = map_get_known(x, y, pplayer);

  freelog(LOG_DEBUG, "Showing %i,%i", x, y);

  send_NODRAW_tiles(pplayer, &pplayer->connections, x, y, 0);
  if (!map_get_known_and_seen(x, y, pplayer)) {
    map_set_known(x, y, pplayer);

    /* as the tile may be fogged send_tile_info won't always do this for us */
    update_tile_knowledge(pplayer, x, y);
    update_player_tile_last_seen(pplayer, x, y);

    send_tile_info_always(pplayer, &pplayer->connections, x, y);

    /* remove old cities that exist no more */
    reality_check_city(pplayer, x, y);
    if ((pcity = map_get_city(x, y))) {
      /* as the tile may be fogged send_city_info won't do this for us */
      update_dumb_city(pplayer, pcity);
      send_city_info(pplayer, pcity);
    }

    if (map_get_seen(x, y, pplayer)) {
      unit_list_iterate(map_get_tile(x, y)->units, punit)
	send_unit_info(pplayer, punit);
      unit_list_iterate_end;
    }

    /* If the tile was not known before we need to refresh the cities that
       can use the tile. */
    if (!old_known) {
      map_city_radius_iterate(x, y, x1, y1) {
	pcity = map_get_city(x1, y1);
	if (pcity && city_owner(pcity) == pplayer) {
	  update_city_tile_status_map(pcity, x, y);
	}
      } map_city_radius_iterate_end;
      sync_cities();
    }
  }
}

/**************************************************************************
Shows area, ie send terrain etc., even if still fogged, sans units and cities.
**************************************************************************/
void show_area(struct player *pplayer, int x, int y, int len)
{
  buffer_shared_vision(pplayer);
  square_iterate(x, y, len, abs_x, abs_y) {
    /* the player himself */
    really_show_area(pplayer, abs_x, abs_y);

    /* players (s)he gives shared vision */
    players_iterate(pplayer2) {
      if (pplayer->really_gives_vision & (1<<pplayer2->player_no)) {
	really_show_area(pplayer2, abs_x, abs_y);
	reveal_pending_seen(pplayer2, abs_x, abs_y, 0);
      }
    } players_iterate_end;
  } square_iterate_end;

  reveal_pending_seen(pplayer, x, y, len);
  unbuffer_shared_vision(pplayer);
}

/***************************************************************
...
***************************************************************/
int map_get_known(int x, int y, struct player *pplayer)
{
  return map_get_tile(x, y)->known & (1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
int map_get_known_and_seen(int x, int y, struct player *pplayer)
{
  int offset = map_inx(x, y);

  return ((map.tiles + offset)->known) & (1u << (pplayer->player_no))
      && (pplayer->private_map + offset)->seen;
}

/***************************************************************
Watch out - this can be true even if the tile is not known.
***************************************************************/
static int map_get_seen(int x, int y, struct player *pplayer)
{
  return map_get_player_tile(x, y, pplayer)->seen;
}

/***************************************************************
...
***************************************************************/
void map_change_seen(int x, int y, struct player *pplayer, int change)
{
  map_get_player_tile(x, y, pplayer)->seen += change;
  freelog(LOG_DEBUG, "%d,%d, p: %d, change %d, result %d\n", x, y,
	  pplayer->player_no, change, map_get_player_tile(x, y,
							 pplayer)->seen);
}

/***************************************************************
...
***************************************************************/
int map_get_own_seen(int x, int y, struct player *pplayer)
{
  int own_seen = map_get_player_tile(x, y, pplayer)->own_seen;
  if (own_seen)
    assert(map_get_known(x, y, pplayer));
  return own_seen;
}

/***************************************************************
...
***************************************************************/
void map_change_own_seen(int x, int y, struct player *pplayer, int change)
{
  map_get_player_tile(x, y, pplayer)->own_seen += change;
}

/***************************************************************
...
***************************************************************/
void map_set_known(int x, int y, struct player *pplayer)
{
  map_get_tile(x, y)->known |= (1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
void map_clear_known(int x, int y, struct player *pplayer)
{
  map_get_tile(x, y)->known &= ~(1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
void map_know_all(struct player *pplayer)
{
  whole_map_iterate(x, y) {
    show_area(pplayer, x, y, 0);
  } whole_map_iterate_end;
}

/***************************************************************
...
***************************************************************/
void map_know_and_see_all(struct player *pplayer)
{
  whole_map_iterate(x, y) {
    unfog_area(pplayer, x, y, 0);
  } whole_map_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void show_map_to_all(void)
{
  players_iterate(pplayer) {
    map_know_and_see_all(pplayer);
  } players_iterate_end;
}

/***************************************************************
...
***************************************************************/
static void map_set_sent(int x, int y, struct player *pplayer)
{
  map_get_tile(x, y)->sent |= (1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
static void map_clear_sent(int x, int y, struct player *pplayer)
{
  map_get_tile(x, y)->sent &= ~(1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
static int map_get_sent(int x, int y, struct player *pplayer)
{
  return map_get_tile(x, y)->sent & (1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
static void set_unknown_tiles_to_unsent(struct player *pplayer)
{
  whole_map_iterate(x, y) {
    map_clear_sent(x, y, pplayer);
  } whole_map_iterate_end;
}

/***************************************************************
  Allocate space for map, and initialise the tiles.
  Uses current map.xsize and map.ysize.
****************************************************************/
void player_map_allocate(struct player *pplayer)
{
  pplayer->private_map =
    fc_malloc(map.xsize*map.ysize*sizeof(struct player_tile));
  whole_map_iterate(x, y) {
    player_tile_init(x, y, pplayer);
  } whole_map_iterate_end;
}

/***************************************************************
We need to use use fogofwar_old here, so the player's tiles get
in the same state as the other players' tiles.
***************************************************************/
static void player_tile_init(int x, int y, struct player *pplayer)
{
  struct player_tile *plrtile =
    map_get_player_tile(x, y, pplayer);

  plrtile->terrain = T_UNKNOWN;
  plrtile->special = S_NO_SPECIAL;
  plrtile->city = NULL;

  plrtile->seen = 0;
  plrtile->pending_seen = 0;
  if (!game.fogofwar_old) {
    if (map_get_known(x, y, pplayer)) {
      plrtile->seen = 1;
    } else {
      plrtile->pending_seen = 1;
    }
  }

  plrtile->last_updated = GAME_START_YEAR;
  plrtile->own_seen = plrtile->seen;
}
 
/***************************************************************
...
***************************************************************/
struct player_tile *map_get_player_tile(int x, int y,
					struct player *pplayer)
{
  if (!is_real_tile(x, y)) {
    freelog(LOG_ERROR, "Trying to get nonexistant tile at %i,%i", x, y);
  }
  nearest_real_pos(&x, &y);
  return pplayer->private_map + map_inx(x, y);
}
 
/***************************************************************
...
***************************************************************/
void update_tile_knowledge(struct player *pplayer, int x, int y)
{
  struct tile *ptile = map_get_tile(x, y);
  struct player_tile *plrtile = map_get_player_tile(x, y, pplayer);

  plrtile->terrain = ptile->terrain;
  plrtile->special = ptile->special;
}

/***************************************************************
...
***************************************************************/
void update_player_tile_last_seen(struct player *pplayer, int x, int y)
{
  map_get_player_tile(x, y, pplayer)->last_updated = game.year;
}

/***************************************************************
...
***************************************************************/
static void really_give_tile_info_from_player_to_player(struct player *pfrom,
							struct player *pdest,
							int x, int y)
{
  struct dumb_city *from_city, *dest_city;
  struct player_tile *from_tile, *dest_tile;
  if (!map_get_known_and_seen(x, y, pdest)) {
    /* I can just hear people scream as they try to comprehend this if :).
       Let me try in words:
       1) if the tile is seen by pdest the info is sent to pfrom
       OR
       2) if the tile is known by pfrom AND (he has more resent info
          OR it is not known by pdest) */
    if (map_get_known_and_seen(x, y, pfrom)
	|| (map_get_known(x,y,pfrom)
	    && (((map_get_player_tile(x, y, pfrom)->last_updated
		 > map_get_player_tile(x, y, pdest)->last_updated))
	        || !map_get_known(x, y, pdest)))) {
      from_tile = map_get_player_tile(x, y, pfrom);
      dest_tile = map_get_player_tile(x, y, pdest);
      /* Update and send tile knowledge */
      map_set_known(x, y, pdest);
      dest_tile->terrain = from_tile->terrain;
      dest_tile->special = from_tile->special;
      dest_tile->last_updated = from_tile->last_updated;
      send_NODRAW_tiles(pdest, &pdest->connections, x, y, 0);
      send_tile_info_always(pdest, &pdest->connections, x, y);
	
      /* update and send city knowledge */
      /* remove outdated cities */
      if (dest_tile->city) {
	if (!from_tile->city) {
	  /* As the city was gone on the newer from_tile
	     it will be removed by this function */
	  reality_check_city(pdest, x, y);
	} else /* We have a dest_city. update */
	  if (from_tile->city->id != dest_tile->city->id)
	    /* As the city was gone on the newer from_tile
	       it will be removed by this function */
	    reality_check_city(pdest, x, y);
      }
      /* Set and send new city info */
      if (from_tile->city && !dest_tile->city) {
	dest_tile->city = fc_malloc(sizeof(struct dumb_city));
      }
      if ((from_city = from_tile->city) && (dest_city = dest_tile->city)) {
	dest_city->id = from_city->id;
	sz_strlcpy(dest_city->name, from_city->name);
	dest_city->size = from_city->size;
	dest_city->has_walls = from_city->has_walls;
	dest_city->owner = from_city->owner;
	send_city_info_at_tile(pdest, &pdest->connections, NULL, x, y);
      }

      reveal_pending_seen(pdest, x, y, 0);

      map_city_radius_iterate(x, y, x1, y1) {
	struct city *pcity = map_get_city(x1, y1);
	if (pcity && city_owner(pcity) == pdest) {
	  update_city_tile_status_map(pcity, x, y);
	}
      } map_city_radius_iterate_end;
      sync_cities();
    }
  }
}

/***************************************************************
...
***************************************************************/
static void give_tile_info_from_player_to_player(struct player *pfrom,
						 struct player *pdest,
						 int x, int y)
{
  really_give_tile_info_from_player_to_player(pfrom, pdest, x, y);

  players_iterate(pplayer2) {
    int playerid2 = pplayer2->player_no;
    if (!(pdest->really_gives_vision & (1<<playerid2)))
      continue;
    really_give_tile_info_from_player_to_player(pfrom, pplayer2, x, y);
  } players_iterate_end;
}

/***************************************************************
This updates all players' really_gives_vision field.
If p1 gives p2 shared vision and p2 gives p3 shared vision p1
should also give p3 shared vision.
***************************************************************/
static void create_vision_dependencies(void)
{
  int added;

  players_iterate(pplayer) {
    pplayer->really_gives_vision = pplayer->gives_shared_vision;
  } players_iterate_end;

  /* In words: This terminates when it has run a round without adding
     a dependency. One loop only propagates dependencies one level deep,
     which is why we keep doing it as long as changes occur. */
  do {
    added = 0;
    players_iterate(pplayer) {
      players_iterate(pplayer2) {
	if (pplayer->really_gives_vision & (1<<pplayer2->player_no)
	    && pplayer != pplayer2) {
	  players_iterate(pplayer3) {
	    if (pplayer2->really_gives_vision & (1<<pplayer3->player_no)
		&& !(pplayer->really_gives_vision & (1<<pplayer3->player_no))
		&& pplayer != pplayer3) {
	      pplayer->really_gives_vision |= (1<<pplayer3->player_no);
	      added++;
	    }
	  } players_iterate_end;
	}
      } players_iterate_end;
    } players_iterate_end;
  } while (added > 0);
}

/***************************************************************
...
***************************************************************/
void give_shared_vision(struct player *pfrom, struct player *pto)
{
  int save_vision[MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS];
  if (pfrom == pto) return;
  if (pfrom->gives_shared_vision & (1<<pto->player_no)) {
    freelog(LOG_ERROR, "Trying to give shared vision from %s to %s, "
	    "but that vision is already given!",
	    pfrom->name, pto->name);
    return;
  }

  players_iterate(pplayer) {
    save_vision[pplayer->player_no] = pplayer->really_gives_vision;
  } players_iterate_end;

  pfrom->gives_shared_vision |= 1<<pto->player_no;
  create_vision_dependencies();
  freelog(LOG_DEBUG, "giving shared vision from %s to %s\n",
	  pfrom->username, pto->username);

  players_iterate(pplayer) {
    buffer_shared_vision(pplayer);
    players_iterate(pplayer2) {
      if (pplayer->really_gives_vision & (1<<pplayer2->player_no)
	  && !(save_vision[pplayer->player_no] & (1<<pplayer2->player_no))) {
	freelog(LOG_DEBUG, "really giving shared vision from %s to %s\n",
	       pplayer->username, pplayer2->username);
	whole_map_iterate(x, y) {
	  int change = map_get_own_seen(x, y, pplayer);
	  if (change) {
	    map_change_seen(x, y, pplayer2, change);
	    if (map_get_seen(x, y, pplayer2) == change) {
	      really_unfog_area(pplayer2, x, y);
	      reveal_pending_seen(pplayer2, x, y, 0);
	    }
	  }
	} whole_map_iterate_end;

	/* squares that are not seen, but which pfrom may have more recent
	   knowledge of */
	give_map_from_player_to_player(pplayer, pplayer2);
      }
    } players_iterate_end;
    unbuffer_shared_vision(pplayer);
  } players_iterate_end;

  if (server_state == RUN_GAME_STATE)
    send_player_info(pfrom, 0);
}

/***************************************************************
...
***************************************************************/
void remove_shared_vision(struct player *pfrom, struct player *pto)
{
  int save_vision[MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS];
  assert(pfrom != pto);
  if (!(pfrom->gives_shared_vision & (1<<pto->player_no))) {
    freelog(LOG_ERROR, "Tried removing the shared vision from %s to %s, "
	    "but it did not exist in the first place!",
	    pfrom->name, pto->name);
    return;
  }

  players_iterate(pplayer) {
    save_vision[pplayer->player_no] = pplayer->really_gives_vision;
  } players_iterate_end;

  freelog(LOG_DEBUG, "removing shared vision from %s to %s\n",
	 pfrom->username, pto->username);

  pfrom->gives_shared_vision &= ~(1<<pto->player_no);
  create_vision_dependencies();

  players_iterate(pplayer) {
    buffer_shared_vision(pplayer);
    players_iterate(pplayer2) {
      if (!(pplayer->really_gives_vision & (1<<pplayer2->player_no))
	  && save_vision[pplayer->player_no] & (1<<pplayer2->player_no)) {
	freelog(LOG_DEBUG, "really removing shared vision from %s to %s\n",
	       pplayer->username, pplayer2->username);
	whole_map_iterate(x, y) {
	  int change = map_get_own_seen(x, y, pplayer);
	  if (change > 0) {
	    map_change_seen(x, y, pplayer2, -change);
	    if (map_get_seen(x, y, pplayer2) == 0)
	      really_fog_area(pplayer2, x, y);
	  }
	} whole_map_iterate_end;
      }
    } players_iterate_end;
    unbuffer_shared_vision(pplayer);
  } players_iterate_end;

  if (server_state == RUN_GAME_STATE)
    send_player_info(pfrom, 0);
}

/***************************************************************
...
***************************************************************/
void handle_player_remove_vision(struct player *pplayer,
				 struct packet_generic_integer *packet)
{
  struct player *pplayer2 = get_player(packet->value);
  if (pplayer == pplayer2) return;
  if (!pplayer2->is_alive) return;
  if (!(pplayer->gives_shared_vision & (1<<packet->value))) return;

  remove_shared_vision(pplayer, pplayer2);
  notify_player(pplayer2, _("%s no longer gives us shared vision!"),
		pplayer->name);
}

/*************************************************************************
...
*************************************************************************/
static void enable_fog_of_war_player(struct player *pplayer)
{
  whole_map_iterate(x, y) {
    if (map_get_known(x, y, pplayer)) {
      fog_area(pplayer, x, y, 0);
    } else {
      decrement_pending_seen(pplayer, x, y);
    }
  } whole_map_iterate_end;
}

/*************************************************************************
...
*************************************************************************/
void enable_fog_of_war(void)
{
  players_iterate(pplayer) {
    enable_fog_of_war_player(pplayer);
  } players_iterate_end;
}

/*************************************************************************
...
*************************************************************************/
static void disable_fog_of_war_player(struct player *pplayer)
{
  whole_map_iterate(x, y) {
    if (map_get_known(x, y, pplayer)) {
      unfog_area(pplayer, x, y, 0);
    } else {
      increment_pending_seen(pplayer, x, y);
    }
  } whole_map_iterate_end;
}

/*************************************************************************
...
*************************************************************************/
void disable_fog_of_war(void)
{
  players_iterate(pplayer) {
    disable_fog_of_war_player(pplayer);
  } players_iterate_end;
}

/***************************************************************
Can pplayer conclude (at least by circumstantial evidence) that
(x,y) is on coastline?  Remember, coastline ocean tiles have a 
small stripe of land in them, even if the actual continent is 
not seen.
***************************************************************/
int is_coast_seen(int x, int y, struct player *pplayer)
{
  int ai_always_see_map = !ai_handicap(pplayer, H_MAP);

  square_iterate(x, y, 1, x1, y1) {
    if (map_get_terrain(x1, y1) == T_OCEAN) {
      continue;
    }
    /* Found land next to (x,y).  Can we see it? */
    if (ai_always_see_map || map_get_known(x1, y1, pplayer)) {
      /* Yes, we can see it */
      return TRUE;
    }
    /* No, we cannot see it, but maybe we can see the strip of land
     * on a tile next to it? */
    adjc_dir_iterate(x1, y1, x2, y2, dir) {
      if (!DIR_IS_CARDINAL(dir)) {
        /* We cannot see strip of land on diagonal tiles */
        continue;
      }
      if (map_get_known(x2, y2, pplayer)) {
        /* Yes, we can see (x2, y2) and it will display
         * the strip of land.  We can conclude that (x1, y1) is land
         * and therefore (x,y) is right next to (or on) the land */
        return TRUE;
      }
    } adjc_dir_iterate_end; /* around x1,y1 */
  } square_iterate_end; /* around x,y */

  return FALSE;
}
