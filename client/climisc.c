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

This module contains various general - mostly highlevel - functions
used throughout the client.
***********************************************************************/

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

#include <game.h>
#include <citydlg.h>
#include <mapview.h>
#include <climisc.h>
#include <mapctrl.h>
#include <map.h>
#include <chatline.h>
#include <log.h>

char *tile_set_dir=NULL;

#ifndef DEBUG
#define DEBUG 0
#endif 

/**************************************************************************
  Search for the requested xpm file
**************************************************************************/
char *tilefilename(char *name)
{
  char *datadir=datafilename("");
  static char filename[256];
  struct stat foo;

  /* first search tileset directory */
  if(tile_set_dir)  {
    strcpy(filename,datadir);
    strcat(filename,tile_set_dir);
    if(filename[strlen(filename)-1]!='/') strcat(filename,"/");
    strcat(filename,name);
    if(!stat(filename,&foo)) return filename;
  }

  /* wasn't in tileset directory or no tileset specified, use default */
  sprintf(filename,"%sdefault/%s",datadir,name);
  return filename;
}


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
    client_remove_city(pcity->id);
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

  if (DEBUG) flog(LOG_DEBUG, "client_remove_unit %d", unit_id);
  
  if((punit=game_find_unit_by_id(unit_id))) {
    int x=punit->x;
    int y=punit->y;
    int hc=punit->homecity;

    if (DEBUG) {
      flog(LOG_DEBUG, "removing unit %d, %s %s (%d %d) hcity %d",
	   unit_id, get_race_name(get_player(punit->owner)->race),
	   unit_name(punit->type), punit->x, punit->y, hc);
    }
    
    if(punit==get_unit_in_focus()) {
      set_unit_focus_no_center(0);
      game_remove_unit(unit_id);
      advance_unit_focus();
    }
    else
      game_remove_unit(unit_id);

    if((pcity=map_get_city(x, y)))
      refresh_city_dialog(pcity);

    if (pcity && DEBUG) {
      flog(LOG_DEBUG, "map city %s, %s, (%d %d)",  pcity->name,
	   get_race_name(city_owner(pcity)->race), pcity->x, pcity->y);
    }
    
    if((pcity=city_list_find_id(&game.player_ptr->cities, hc)))
      refresh_city_dialog(pcity);

    if (pcity && DEBUG) {
      flog(LOG_DEBUG, "home city %s, %s, (%d %d)", pcity->name,
	   get_race_name(city_owner(pcity)->race), pcity->x, pcity->y);
    }
    
    refresh_tile_mapcanvas(x, y, 1);
  }
  
}


/**************************************************************************
...
**************************************************************************/
void client_remove_city(int city_id)
{
  struct city *pcity;
  
  if (DEBUG) flog(LOG_DEBUG, "client_remove_city %d", city_id);
  
  if((pcity=game_find_city_by_id(city_id))) {
    int x=pcity->x;
    int y=pcity->y;
    popdown_city_dialog(pcity);
    if (DEBUG) {
      flog(LOG_DEBUG, "removing city %s, %s, (%d %d)", pcity->name,
	   get_race_name(city_owner(pcity)->race), pcity->x, pcity->y);
    }
    game_remove_city(pcity);
    refresh_tile_mapcanvas(x, y, 1);
  }
}

struct city *find_city_by_id(int id)
{ /* no idea where this belongs either */
  return(game_find_city_by_id(id));
}
