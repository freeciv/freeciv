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

/********************************************************************** 
This module contains various general - mostly highlevel - functions
used throughout the client.
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "astring.h"
#include "diptreaty.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "city.h"
#include "packets.h"
#include "support.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "cityrep_g.h"
#include "control.h"
#include "mapview_g.h"

#include "climisc.h"

extern struct connection aconnection;

/**************************************************************************
...
**************************************************************************/
void client_remove_player(int plrno)
{
  struct player *pplayer=&game.players[plrno];
  unit_list_iterate(pplayer->units, punit) 
    client_remove_unit(punit->id);
  unit_list_iterate_end;
  city_list_iterate(pplayer->cities, pcity) 
    client_remove_city(pcity);
  city_list_iterate_end;
  game_renumber_players(plrno);
}

/**************************************************************************
...
**************************************************************************/
void client_remove_unit(int unit_id)
{
  struct unit *punit;
  struct city *pcity;

  freelog(LOG_DEBUG, "client_remove_unit %d", unit_id);
  
  if((punit=find_unit_by_id(unit_id))) {
    int x=punit->x;
    int y=punit->y;
    int hc=punit->homecity;

    freelog(LOG_DEBUG, "removing unit %d, %s %s (%d %d) hcity %d",
	   unit_id, get_nation_name(get_player(punit->owner)->nation),
	   unit_name(punit->type), punit->x, punit->y, hc);
    
    if(punit==get_unit_in_focus()) {
      set_unit_focus_no_center(0);
      game_remove_unit(unit_id);
      advance_unit_focus();
    }
    else
      game_remove_unit(unit_id);

    if((pcity=map_get_city(x, y)))
      refresh_city_dialog(pcity);

    if (pcity) {
      freelog(LOG_DEBUG, "map city %s, %s, (%d %d)",  pcity->name,
	   get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y);
    }
    
    if((pcity=player_find_city_by_id(game.player_ptr, hc)))
      refresh_city_dialog(pcity);

    if (pcity) {
      freelog(LOG_DEBUG, "home city %s, %s, (%d %d)", pcity->name,
	   get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y);
    }
    
    refresh_tile_mapcanvas(x, y, 1);
  }
  
}


/**************************************************************************
...
**************************************************************************/
void client_remove_city(struct city *pcity)
{
  int x=pcity->x;
  int y=pcity->y;

  freelog(LOG_DEBUG, "removing city %s, %s, (%d %d)", pcity->name,
	  get_nation_name(city_owner(pcity)->nation), x, y);
  popdown_city_dialog(pcity);
  game_remove_city(pcity);
  city_report_dialog_update();
  refresh_tile_mapcanvas(x, y, 1);
}

#define MAX_NUM_CONT 32767   /* max portable value in signed short */

/* Static data used to keep track of continent numbers in client:
 * maximum value used, and array of recycled values which can be used.
 * (Could used signed short, but int is easier and storage here will
 * be fairly small.)
 */
static int max_cont_used = 0;
static struct athing recyc_conts;   /* .n is number available */
static int recyc_init = 0;	    /* for first init of recyc_conts */
static int *recyc_ptr = NULL;	    /* set to recyc_conts.ptr (void* vs int*) */

/**************************************************************************
  Initialise above data, or re-initialize (eg, new map).
**************************************************************************/
void climap_init_continents(void)
{
  if (!recyc_init) {
    /* initialize with size first time: */
    ath_init(&recyc_conts, sizeof(int));
    recyc_init = 1;
  }
  ath_free(&recyc_conts);
  recyc_ptr = NULL;
  max_cont_used = 0;
}

/**************************************************************************
  Recycle a continent number.
  (Ie, number is no longer used, and may be re-used later.)
**************************************************************************/
static void recycle_continent_num(int cont)
{
  assert(cont>0 && cont<=max_cont_used);          /* sanity */
  ath_minnum(&recyc_conts, recyc_conts.n+1);
  recyc_ptr = recyc_conts.ptr;
  recyc_ptr[recyc_conts.n-1] = cont;
  freelog(LOG_DEBUG, "clicont: recycling %d (max %d recyc %d)",
	  cont, max_cont_used, recyc_conts.n);
}

/**************************************************************************
  Obtain an unused continent number: a recycled number if available,
  or increase the maximum.
**************************************************************************/
static int new_continent_num(void)
{
  int ret;
  
  if (recyc_conts.n>0) {
    ret = recyc_ptr[--recyc_conts.n];
  } else {
    assert(max_cont_used<MAX_NUM_CONT);
    ret = ++max_cont_used;
  }
  freelog(LOG_DEBUG, "clicont: new %d (max %d, recyc %d)",
	  ret, max_cont_used, recyc_conts.n);
  return ret;
}

/**************************************************************************
  Recursively renumber the client continent at (x,y) with continent
  number 'new'.  Ie, renumber (x,y) tile and recursive adjacent
  known land tiles with the same previous continent ('old').
**************************************************************************/
static void climap_renumber_continent(int x, int y, int newnumber)
{
  int i, j, old;

  if (y<0 || y>=map.ysize) return;
  x = map_adjust_x(x);

  old = map_get_continent(x,y);

  /* some sanity checks: */
  assert(tile_is_known(x,y) >= TILE_KNOWN_FOGGED);
  assert(map_get_terrain(x,y) != T_OCEAN);
  assert(old>0 && old<=max_cont_used);
  
  map_set_continent(x,y,newnumber);
  for(i=x-1; i<=x+1; i++) {
    for(j=y-1; j<=y+1; j++) {
      if(!(i==x && j==y) && j>=0 && j<map.ysize
	 && tile_is_known(i,j)>=TILE_KNOWN_FOGGED
	 && map_get_terrain(i,j)!=T_OCEAN
	 && map_get_continent(i,j)==old) {
	climap_renumber_continent(i,j,newnumber);
      }
    }
  }
}

/**************************************************************************
  Update continent numbers when (x,y) becomes known (if (x,y) land).
  Check neighbouring known land tiles: the first continent number
  found becomes the continent value of this tile.  Any other continents
  found are numbered to this continent (ie, continents are merged)
  and previous continent values are recycled.  If no neighbours are
  numbered, use a new number. 
**************************************************************************/
void climap_update_continents(int x, int y)
{
  struct tile *ptile = map_get_tile(x,y);
  int i, j, con, this_con;

  if(ptile->terrain == T_OCEAN) return;
  this_con = -1;
  for(i=x-1; i<=x+1; i++) {
    for(j=y-1; j<=y+1; j++) {
      if(!(i==x && j==y) && j>=0 && j<map.ysize
	 && tile_is_known(i,j)>=TILE_KNOWN_FOGGED
	 && map_get_terrain(i,j)!=T_OCEAN) {
	con = map_get_continent(i,j);
	if(con>0) {
	  if(this_con==-1) {
	    ptile->continent = this_con = con;
	  } else if(con != this_con) {
	    freelog(LOG_DEBUG, "renumbering client continent %d to %d at (%d %d)",
		    con, this_con, x, y);
	    climap_renumber_continent(i, j, this_con);
	    recycle_continent_num(con);
	  }
	}
      }
    }
  }
  if(this_con==-1) {
    ptile->continent = new_continent_num();
    freelog(LOG_DEBUG, "new client continent %d at (%d %d)",
	    ptile->continent, x, y);
  }
}

/**************************************************************************
Change all cities building X to building Y, if possible.
X and Y could be improvements or units; improvements are
specified by the id, units are specified by unit id + B_LAST
**************************************************************************/
void client_change_all(int x, int y)
{
  int fr_id, to_id, fr_is_unit, to_is_unit;
  struct packet_city_request packet;

  if (x < B_LAST)
    {
      fr_id = x;
      fr_is_unit = FALSE;
    }
  else
    {
      fr_id = x - B_LAST;
      fr_is_unit = TRUE;
    }

  if (y < B_LAST)
    {
      to_id = y;
      to_is_unit = FALSE;
    }
  else
    {
      to_id = y - B_LAST;
      to_is_unit = TRUE;
    }

  city_list_iterate (game.player_ptr->cities, pcity) {
    if (((fr_is_unit &&
	  (pcity->is_building_unit) &&
	  (pcity->currently_building == fr_id)) ||
	 (!fr_is_unit &&
	  !(pcity->is_building_unit) &&
	  (pcity->currently_building == fr_id))) &&
	((to_is_unit &&
	  can_build_unit (pcity, to_id)) ||
	 (!to_is_unit &&
	  can_build_improvement (pcity, to_id))))
      {
	packet.city_id = pcity->id;
	packet.build_id = to_id;
	packet.is_build_id_unit_id = to_is_unit;
	packet.name[0] = '\0';
	packet.worklist.name[0] = '\0';
	send_packet_city_request (&aconnection, &packet,
				  PACKET_CITY_CHANGE);
      }
  }
  city_list_iterate_end;
}

/**************************************************************************
Copy a string that describes the given clause into the return buffer.
**************************************************************************/
void client_diplomacy_clause_string(char *buf, int bufsiz,
				    struct Clause *pclause)
{
  switch(pclause->type) {
  case CLAUSE_ADVANCE:
    my_snprintf(buf, bufsiz, _("The %s give %s"),
		get_nation_name_plural(pclause->from->nation),
		advances[pclause->value].name);
    break;
  case CLAUSE_CITY:
    my_snprintf(buf, bufsiz, _("The %s give %s"),
		get_nation_name_plural(pclause->from->nation),
		find_city_by_id(pclause->value)->name);
    break;
  case CLAUSE_GOLD:
    my_snprintf(buf, bufsiz, _("The %s give %d gold"),
		get_nation_name_plural(pclause->from->nation),
		pclause->value);
    break;
  case CLAUSE_MAP:
    my_snprintf(buf, bufsiz, _("The %s give their worldmap"),
		get_nation_name_plural(pclause->from->nation));
    break;
  case CLAUSE_SEAMAP:
    my_snprintf(buf, bufsiz, _("The %s give their seamap"),
		get_nation_name_plural(pclause->from->nation));
    break;
  case CLAUSE_CEASEFIRE:
    my_snprintf(buf, bufsiz, _("The parties agree on a cease-fire"));
    break;
  case CLAUSE_PEACE:
    my_snprintf(buf, bufsiz, _("The parties agree on a peace"));
    break;
  case CLAUSE_ALLIANCE:
    my_snprintf(buf, bufsiz, _("The parties create an alliance"));
    break;
  default:
    if (bufsiz > 0) *buf = '\0';
    break;
  }
}
