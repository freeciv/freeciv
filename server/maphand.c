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

#include "cityhand.h"
#include "cityturn.h"
#include "mapgen.h"		/* assign_continent_numbers */
#include "plrhand.h"           /* notify_player */
#include "sernet.h"
#include "unittools.h"
#include "unithand.h"

#include "maphand.h"

static void player_tile_init(struct player_tile *ptile);
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

extern enum server_states server_state;
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

  k = map.xsize * map.ysize;
  while(effect && k--) {
    x = myrand(map.xsize);
    y = myrand(map.ysize);
    if (map_get_terrain(x, y) != T_OCEAN) {
      if (is_terrain_ecologically_wet(x, y)) {
	switch (map_get_terrain(x, y)) {
	case T_FOREST:
	  effect--;
	  map_set_terrain(x, y, T_JUNGLE);
	  reset_move_costs(x, y);
	  send_tile_info(0, x, y);
	  break;
	case T_DESERT:
	case T_PLAINS:
	case T_GRASSLAND:
	  effect--;
	  map_set_terrain(x, y, T_SWAMP);
	  map_clear_special(x, y, S_FARMLAND);
	  map_clear_special(x, y, S_IRRIGATION);
	  reset_move_costs(x, y);
	  send_tile_info(0, x, y);
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
	  map_set_terrain(x, y, T_DESERT);
	  reset_move_costs(x, y);
	  send_tile_info(0, x, y);
	  break;
	default:
	  break;
	}
      }
      unit_list_iterate(map_get_tile(x, y)->units, punit) {
	/* Because of the way can_unit_do_activity() works we do
	   fortified as a special case. (since you can never go
	   straight to ACTIVITY_FORTIFY can_unit_do_activity()
	   returns 0.) */
	if (punit->activity == ACTIVITY_FORTIFIED) {
	  punit->activity = ACTIVITY_IDLE;
	  if (can_unit_do_activity(punit, ACTIVITY_FORTIFYING)) {
	    punit->activity = ACTIVITY_FORTIFIED;
	  } /* else let it remain on idle. */
	} else if (!can_unit_do_activity(punit, punit->activity)
		   && !punit->connecting) {
	  handle_unit_activity_request(punit, ACTIVITY_IDLE);
	}
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

  k =map.xsize * map.ysize;
  while(effect && k--) {
    x = myrand(map.xsize);
    y = myrand(map.ysize);
    if (map_get_terrain(x, y) != T_OCEAN) {
      switch (map_get_terrain(x, y)) {
      case T_JUNGLE:
      case T_SWAMP:
      case T_PLAINS:
      case T_GRASSLAND:
	effect--;
	map_set_terrain(x, y,
			is_terrain_ecologically_wet(x, y) ? T_DESERT : T_TUNDRA);
	reset_move_costs(x, y);
	send_tile_info(0, x, y);
	break;
      case T_TUNDRA:
	effect--;
	map_set_terrain(x, y, T_ARCTIC);
	reset_move_costs(x, y);
	send_tile_info(0, x, y);
	break;
      default:
	break;
      }
      unit_list_iterate(map_get_tile(x, y)->units, punit) {
	if (!can_unit_do_activity(punit, punit->activity)
	    && !punit->connecting)
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
  int y, x;

  if (dest==NULL) dest = &game.game_connections;
  
  conn_list_iterate(*dest, pconn) {
    if (pconn->player) {
      set_unknown_tiles_to_unsent(pconn->player);
    }
  } conn_list_iterate_end;

  /* send whole map row by row to each player to balance the
     load of the send buffers better */
  for (y=0; y<map.ysize; y++) {
    conn_list_do_buffer(dest);
    conn_list_iterate(*dest, pconn) {
      struct player *pplayer = pconn->player;

      if (!pplayer && !pconn->observer) {	/* no map needed */
        continue;
      }

      if (pplayer) {
	for (x=0; x<map.xsize; x++) {
	  if (map_get_known(x, y, pplayer)) {
	    send_NODRAW_tiles(pplayer, &pconn->self, x, y, 0);
	    send_tile_info_always(pplayer, &pconn->self, x, y);
	  }
	}
      } else {
	for (x=0; x<map.xsize; x++) {
	  send_tile_info_always(pplayer, &pconn->self, x, y);
	}
      }
    } conn_list_iterate_end;
    conn_list_do_unbuffer(dest);
  }
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
    if(pplayer==NULL || map_get_known_and_seen(x, y, pplayer->player_no)) {
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
    if (map_get_seen(x, y, pplayer->player_no)) { /* known and seen */
      update_tile_knowledge(pplayer,x,y); /* visible; update info */
      info.known = TILE_KNOWN;
    } else { /* known but not seen */
      info.known = TILE_KNOWN_FOGGED;
    }
    plrtile = map_get_player_tile(x, y, pplayer->player_no);
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
     can use the tile.
     It is slightly suboptimal to do this here as a city may be sent to
     the client once for each tile that is revieled, but it shouldn't make
     too big a difference. It would require a largish recoding to make sure
     only to send a city once when multiple squares were unveiled at once.
  */
  if (!old_known) {
    map_city_radius_iterate(x, y, x1, y1) {
      pcity = map_get_city(x1, y1);
      if (pcity && city_owner(pcity) == pplayer) {
	city_check_workers(pcity, 1);
	send_city_info(pplayer, pcity);
      }
    } map_city_radius_iterate_end;
  }
}

/**************************************************************************
  Add an extra point of visibility to a square centered at x,y with
  sidelength 1+2*len, ie length 1 is normal sightrange for a unit.
  pplayer may not be NULL.
**************************************************************************/
void unfog_area(struct player *pplayer, int x, int y, int len)
{
  int playerid = pplayer->player_no;
  /* Did the tile just become visible?
     - send info about units and cities and the tile itself */
  buffer_shared_vision(pplayer);
  square_iterate(x, y, len, abs_x, abs_y) {
    /* the player himself */
    shared_vision_change_seen(abs_x, abs_y, pplayer, +1);
    if (map_get_seen(abs_x, abs_y, playerid) == 1
	|| !map_get_known(abs_x, abs_y, pplayer))
      really_unfog_area(pplayer, abs_x, abs_y);

    /* players (s)he gives shared vision */
    players_iterate(pplayer2) {
      int playerid2 = pplayer2->player_no;
      if (!(pplayer->really_gives_vision & (1<<playerid2)))
	continue;
      if (map_get_seen(abs_x, abs_y, playerid2) == 1 ||
	  !map_get_known(abs_x, abs_y, pplayer2))
	really_unfog_area(pplayer2, abs_x, abs_y);
    } players_iterate_end;
  } square_iterate_end;
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
  int playerid = pplayer->player_no;
  
  freelog(LOG_DEBUG, "Fogging %i,%i. Previous fog: %i.",
	  x, y, map_get_seen(x, y, playerid));
 
  assert(map_get_seen(x, y, playerid) == 0);
  update_player_tile_last_seen(pplayer, x, y);
  send_tile_info_always(pplayer, &pplayer->connections, x, y);
}

/**************************************************************************
  Remove a point of visibility from a square centered at x,y with
  sidelength 1+2*len, ie length 1 is normal sightrange for a unit.
**************************************************************************/
void fog_area(struct player *pplayer, int x, int y, int len)
{
  int playerid = pplayer->player_no;

  buffer_shared_vision(pplayer);
  square_iterate(x, y, len, abs_x, abs_y) {
    /* the player himself */
    shared_vision_change_seen(abs_x, abs_y, pplayer, -1);
    if (map_get_seen(abs_x, abs_y, playerid) == 0)
      really_fog_area(pplayer, abs_x, abs_y);

    /* players (s)he gives shared vision */
    players_iterate(pplayer2) {
      int playerid2 = pplayer2->player_no;
      if (!(pplayer->really_gives_vision & (1<<playerid2)))
	continue;
      if (map_get_seen(abs_x, abs_y, playerid2) == 0)
	really_fog_area(pplayer2, abs_x, abs_y);
    } players_iterate_end;
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
  int playerid = pplayer->player_no;
  map_change_seen(x, y, playerid, change);
  map_change_own_seen(x, y, playerid, change);

  players_iterate(pplayer2) {
    int playerid2 = pplayer2->player_no;
    if (pplayer->really_gives_vision & (1<<playerid2))
      map_change_seen(x, y, playerid2, change);
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
    if (map_get_known(x_itr, y_itr, pplayer))
      unfog_area(pplayer, x_itr, y_itr, 0);
    else {
      map_change_seen(x_itr, y_itr, pplayer->player_no, +1);
      map_change_own_seen(x_itr, y_itr, pplayer->player_no, +1);

      players_iterate(pplayer2) {
	int playerid2 = pplayer2->player_no;
	if (!(pplayer->really_gives_vision & (1<<playerid2)))
	  continue;
	map_change_seen(x_itr, y_itr, playerid2, +1);
	if (map_get_known(x_itr, y_itr, pplayer2))
	  really_unfog_area(pplayer2, x_itr, y_itr);
      } players_iterate_end;
    }
  } map_city_radius_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
There doesn't have to be a city.
**************************************************************************/
void map_fog_pseudo_city_area(struct player *pplayer, int x,int y)
{
  freelog(LOG_DEBUG, "Fogging city area at %i,%i", x, y);

  buffer_shared_vision(pplayer);
  map_city_radius_iterate(x, y, x_itr, y_itr) {
    if (map_get_known(x_itr, y_itr, pplayer))
      fog_area(pplayer, x_itr, y_itr, 0);
    else {
      map_change_seen(x_itr, y_itr, pplayer->player_no, -1);
      map_change_own_seen(x_itr, y_itr, pplayer->player_no, -1);

      players_iterate(pplayer2) {
	int playerid2 = pplayer2->player_no;
	if (!(pplayer->really_gives_vision & (1<<playerid2)))
	  continue;
	map_change_seen(x_itr, y_itr, playerid2, -1);
	if (map_get_known(x_itr, y_itr, pplayer2))
	  really_fog_area(pplayer2, x_itr, y_itr);
      } players_iterate_end;
    }
  } map_city_radius_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
For removing a unit. The actual removal is done in server_remove_unit
**************************************************************************/
void remove_unit_sight_points(struct unit *punit)
{
  int x = punit->x,y = punit->y,range = get_unit_type(punit->type)->vision_range;
  struct player *pplayer = &game.players[punit->owner];

  freelog(LOG_DEBUG, "Removing unit sight points at  %i,%i",punit->x,punit->y);

  fog_area(pplayer, x, y, range);
}

/**************************************************************************
Shows area even if still fogged. If the tile is not "seen" units are not
shown
**************************************************************************/
static void really_show_area(struct player *pplayer, int x, int y)
{
  int playerid = pplayer->player_no;
  struct city *pcity;

  freelog(LOG_DEBUG, "Showing %i,%i", x, y);

  send_NODRAW_tiles(pplayer, &pplayer->connections, x, y, 0);
  if (!map_get_known_and_seen(x, y, playerid)) {
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

    if (map_get_seen(x, y, playerid)) {
      unit_list_iterate(map_get_tile(x, y)->units, punit)
	send_unit_info(pplayer, punit);
      unit_list_iterate_end;
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
      if (pplayer->really_gives_vision & (1<<pplayer2->player_no))
	really_show_area(pplayer2, abs_x, abs_y);
    } players_iterate_end;
  } square_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/***************************************************************
...
***************************************************************/
int map_get_known(int x, int y, struct player *pplayer)
{
  return (int) (((map.tiles+map_adjust_x(x)+
	   map_adjust_y(y)*map.xsize)->known)&(1u<<pplayer->player_no));
}

/***************************************************************
...
***************************************************************/
int map_get_known_and_seen(int x, int y, int playerid)
{
  int offset = map_adjust_x(x)+map_adjust_y(y)*map.xsize;
  return ((map.tiles+offset)->known)&(1u<<playerid) &&
    (get_player(playerid)->private_map+offset)->seen;
}

/***************************************************************
Watch out - this can be true even if the tile is not known.
***************************************************************/
int map_get_seen(int x, int y, int playerid)
{
  return map_get_player_tile(x, y, playerid)->seen;
}

/***************************************************************
...
***************************************************************/
void map_change_seen(int x, int y, int playerid, int change)
{
  map_get_player_tile(x, y, playerid)->seen += change;
  freelog(LOG_DEBUG, "%d,%d, p: %d, change %d, result %d\n", x, y, playerid, change,
	 map_get_player_tile(x, y, playerid)->seen);
}

/***************************************************************
Watch out - this can be true even if the tile is not known.
***************************************************************/
int map_get_own_seen(int x, int y, int playerid)
{
  return map_get_player_tile(x, y, playerid)->own_seen;
}

/***************************************************************
...
***************************************************************/
void map_change_own_seen(int x, int y, int playerid, int change)
{
  map_get_player_tile(x, y, playerid)->own_seen += change;
}

/***************************************************************
...
***************************************************************/
void map_set_known(int x, int y, struct player *pplayer)
{
  (map.tiles+map_adjust_x(x)+
   map_adjust_y(y)*map.xsize)->known|=(1u<<pplayer->player_no);
}

/***************************************************************
Not used
***************************************************************/
void map_clear_known(int x, int y, struct player *pplayer)
{
  (map.tiles+map_adjust_x(x)+
   map_adjust_y(y)*map.xsize)->known&=~(1u<<pplayer->player_no);
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
  (map.tiles+map_adjust_x(x)+ map_adjust_y(y)*map.xsize)->sent
    |= (1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
static void map_clear_sent(int x, int y, struct player *pplayer)
{
  (map.tiles+map_adjust_x(x)+ map_adjust_y(y)*map.xsize)->sent
    &= ~(1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
static int map_get_sent(int x, int y, struct player *pplayer)
{
  return (int) (((map.tiles+map_adjust_x(x)+
	   map_adjust_y(y)*map.xsize)->sent)&(1u<<pplayer->player_no));
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
    player_tile_init(map_get_player_tile(x, y, pplayer->player_no));
  } whole_map_iterate_end;
}

/***************************************************************
We need to use use fogofwar_old here, so the player's tiles get
in the same state as th other players' tiles.
***************************************************************/
static void player_tile_init(struct player_tile *plrtile)
{
  plrtile->terrain = T_UNKNOWN;
  plrtile->special = S_NO_SPECIAL;
  plrtile->city = NULL;
  if (game.fogofwar_old)
    plrtile->seen = 0;
  else
    plrtile->seen = 1;
  plrtile->last_updated = GAME_START_YEAR;
  plrtile->own_seen = plrtile->seen;
}
 
/***************************************************************
...
***************************************************************/
struct player_tile *map_get_player_tile(int x, int y, int playerid)
{
  if(y<0 || y>=map.ysize) {
    freelog(LOG_ERROR, "Trying to get nonexistant tile at %i,%i", x,y);
    return get_player(playerid)->private_map
      + map_adjust_x(x)+map_adjust_y(y)*map.xsize;
  } else
    return get_player(playerid)->private_map
      + map_adjust_x(x)+y*map.xsize;
}
 
/***************************************************************
...
***************************************************************/
void update_tile_knowledge(struct player *pplayer,int x, int y)
{
  struct tile *ptile = map_get_tile(x,y);
  struct player_tile *plrtile = map_get_player_tile(x, y, pplayer->player_no);

  plrtile->terrain = ptile->terrain;
  plrtile->special = ptile->special;
}

/***************************************************************
...
***************************************************************/
void update_player_tile_last_seen(struct player *pplayer, int x, int y)
{
  map_get_player_tile(x, y, pplayer->player_no)->last_updated = game.year;
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
  if (!map_get_known_and_seen(x, y, pdest->player_no)) {
    /* I can just hear people scream as they try to comprehend this if :).
       Let me try in words:
       1) if the tile is seen by pdest the info is sent to pfrom
       OR
       2) if the tile is known by pfrom AND (he has more resent info
          OR it is not known by pdest) */
    if (map_get_known_and_seen(x, y, pfrom->player_no)
	|| (map_get_known(x,y,pfrom)
	    && (((map_get_player_tile(x, y, pfrom->player_no)->last_updated
		 > map_get_player_tile(x, y, pdest->player_no)->last_updated))
	        || !map_get_known(x, y, pdest)))) {
      from_tile = map_get_player_tile(x, y, pfrom->player_no);
      dest_tile = map_get_player_tile(x, y, pdest->player_no);
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
	  int change = map_get_own_seen(x, y, pplayer->player_no);
	  if (change) {
	    map_change_seen(x, y, pplayer2->player_no, change);
	    if (map_get_seen(x, y, pplayer2->player_no) == change)
	      really_unfog_area(pplayer2, x, y);
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
	  int change = map_get_own_seen(x, y, pplayer->player_no);
	  if (change > 0) {
	    map_change_seen(x, y, pplayer2->player_no, -change);
	    if (map_get_seen(x, y, pplayer2->player_no) == 0)
	      really_fog_area(pplayer2, x, y);
	  }
	}
      } whole_map_iterate_end;
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
