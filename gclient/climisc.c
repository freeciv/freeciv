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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <gtk/gtk.h>

#include <game.h>
/*#include <citydlg.h>*/
#include <mapview.h>
#include <climisc.h>
#include <mapctrl.h>
#include <map.h>
#include <main.h>

extern GtkText *main_message_area;

/***************************************************************************
...
***************************************************************************/
char *datafilename(char *filename)
{
  static char* datadir=0;
  static char  realfile[512];
  if(!datadir) {
    if((datadir = getenv("FREECIV_DATADIR"))) {
      int i;
      for(i=strlen(datadir)-1; i>=0 && isspace((int)datadir[i]); i--)
	datadir[i] = '\0';
      if(datadir[i] == '/')
	datadir[i] = '\0';
    } else {
      datadir = "data";		/* correct if not 'data' is the default */
    };
  };
  sprintf(realfile,"%s/%s",datadir,filename);
  return(realfile);
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
    
  if((punit=game_find_unit_by_id(unit_id))) {
    int x=punit->x;
    int y=punit->y;
    int hc=punit->homecity;
    
    if(punit==get_unit_in_focus()) {
      set_unit_focus_no_center(0);
      game_remove_unit(unit_id);
      advance_unit_focus();
    }
    else
      game_remove_unit(unit_id);
/*
    if((pcity=map_get_city(x, y)))
      refresh_city_dialog(pcity);
    if((pcity=city_list_find_id(&game.player_ptr->cities, hc)))
      refresh_city_dialog(pcity);
*/
    refresh_tile_mapcanvas(x, y, 1);
  }
  
}


/**************************************************************************
...
**************************************************************************/
void client_remove_city(int city_id)
{
  struct city *pcity;
  
  if((pcity=game_find_city_by_id(city_id))) {
    int x=pcity->x;
    int y=pcity->y;
/*
    popdown_city_dialog(pcity);
*/
    game_remove_city(city_id);
    refresh_tile_mapcanvas(x, y, 1);
  }
}

void log_output_window( GtkWidget *widget, gpointer data )
{ /* I have no idea what module this belongs in -- Syela */
  char *theoutput;
  FILE *flog;
  
  append_output_window( "Exporting output window to civgame.log ..." );

  theoutput = gtk_editable_get_chars( GTK_EDITABLE( main_message_area ), 0, -1 );

  flog = fopen("civgame.log", "w"); /* should allow choice of name? */

  fprintf(flog, "%s", theoutput);

  fclose(flog);
  append_output_window("Export complete.");
}
