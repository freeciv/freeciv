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

#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

#include "capability.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "support.h"
#include "unit.h"

#include "civclient.h"
#include "clinet.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "inputdlg.h"
#include "mapview.h"

#include "mapctrl.h"

/* Amiga client stuff */

#include "muistuff.h"
#include "mapclass.h"

IMPORT Object *app;
IMPORT Object *main_turndone_button;

/* Update the workers for a city on the map, when the update is received */
struct city *city_workers_display = NULL;
/* Color to use to display the workers */
int city_workers_color=COLOR_STD_WHITE;

/****************************************************************
...
*****************************************************************/
static void newcity_city_hook(struct input_dialog_data *data)
{
  struct unit *punit = (struct unit *) data->data;

  if(punit)
  {
    struct packet_unit_request packet;

    packet.unit_id = punit->id;
    sz_strlcpy(packet.name, input_dialog_get_input(data->name));
    send_packet_unit_request(&aconnection, &packet, PACKET_UNIT_BUILD_CITY);
  }

  input_dialog_destroy(data->wnd);
}

/**************************************************************************
 Popup dialog where the user choose the name of the new city
 punit = (settler) unit which builds the city
 suggestname = suggetion of the new city's name
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, char *suggestname)
{
  input_dialog_create(app,
    _("What should we call our new city?"), _("City_name"), suggestname,
    (void*)newcity_city_hook, punit,
    (void*)newcity_city_hook, 0);
}

/**************************************************************************
 Activate or deactivate the turn done button.
 Should probably some where else.
**************************************************************************/
void set_turn_done_button_state(int state)
{
  set(main_turndone_button, MUIA_Disabled, !state);
}

/**************************************************************************
 Callback if the user clicks on the map
**************************************************************************/
int main_map_click(struct Map_Click **click)
{
  int xtile = (*click)->x;
  int ytile = (*click)->y;

  do_map_click(xtile, ytile);
  return 0;
}
