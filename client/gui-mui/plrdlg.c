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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/graphics.h>

#include "game.h"
#include "packets.h"
#include "player.h"
#include "support.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "inteldlg.h"
#include "spaceshipdlg.h"

#include "plrdlg.h"

/* Amiga Client stuff */
#include "muistuff.h"

IMPORT Object *app;

STATIC Object *player_wnd;
STATIC Object *player_players_listview;
STATIC Object *player_close_button;
STATIC Object *player_intelligence_button;
STATIC Object *player_meet_button;
STATIC Object *player_war_button;
STATIC Object *player_spaceship_button;
STATIC struct Hook player_players_disphook;

static int delay_plrdlg_update=0;

void create_players_dialog(void);

/****************************************************************
 GUI Independend
*****************************************************************/
void request_diplomacy_init_meeting(int plrno0, int plrno1)
{
  struct packet_diplomacy_info pa;

  pa.plrno0 = plrno0;
  pa.plrno1 = plrno1;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_INIT_MEETING,
			     &pa);
}


/******************************************************************
 Turn off updating of player dialog
*******************************************************************/
void plrdlg_update_delay_on(void)
{
  delay_plrdlg_update=1;
}

/******************************************************************
 Turn on updating of player dialog
*******************************************************************/
void plrdlg_update_delay_off(void)
{
  delay_plrdlg_update=0;
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_players_dialog(void)
{
  if (!player_wnd)
    create_players_dialog();
  if (player_wnd)
  {
    update_players_dialog();
    set(player_wnd, MUIA_Window_Open, TRUE);
  }
}


/****************************************************************
 Display function for the players listview
*****************************************************************/
HOOKPROTONH(players_render, void, char **array, APTR msg)
{
  ULONG playerno = (ULONG) msg;

  if (playerno)
  {
    int i = playerno - 100;
    static char idlebuf[32];
    static char statebuf[32];
    static char namebuf[32];
    static char dsbuf[32];
    static char repbuf[32];
    const struct player_diplstate *pds;

    if (game.players[i].nturns_idle > 3)
      sprintf(idlebuf, "(idle %d turns)", game.players[i].nturns_idle - 1);
    else
      idlebuf[0] = '\0';

    if (game.players[i].is_alive)
    {
      if (game.players[i].is_connected)
      {
	if (game.players[i].turn_done)
	  strcpy(statebuf, "done");
	else
	  strcpy(statebuf, "moving");
      }
      else
	statebuf[0] = '\0';
    }
    else
      strcpy(statebuf, "R.I.P");

    if (game.players[i].ai.control)
      sprintf(namebuf, "*%-15s", game.players[i].name);
    else
      sprintf(namebuf, "%-16s", game.players[i].name);



    /* text for diplstate type and turns -- not applicable if this is me */
    if (i == game.player_idx) {
      strcpy(dsbuf, "-");
    } else {
      pds = player_get_diplstate(game.player_idx, i);
      if (pds->type == DS_CEASEFIRE) {
	my_snprintf(dsbuf, sizeof(dsbuf), "%s (%d)",
		    diplstate_text(pds->type), pds->turns_left);
      } else {
	my_snprintf(dsbuf, sizeof(dsbuf), "%s",
		    diplstate_text(pds->type));
      }
    }

    /* text for reputation */
    my_snprintf(repbuf, sizeof(repbuf),
		reputation_text(game.players[i].reputation));

    *array++ = namebuf;
    *array++ = get_nation_name(game.players[i].nation);
    *array++ = get_embassy_status(game.player_ptr, &game.players[i]);
    *array++ = dsbuf;
    *array++ = repbuf;
    *array++ = statebuf;
    *array++ = (char*)player_addr_hack(&game.players[i]);  /* Fixme */
    *array = idlebuf;
  }
  else
  {
    *array++ = "Name";
    *array++ = "Nation";
    *array++ = "Embassy";
    *array++ = "Dipl.State";
    *array++ = "Reputation";
    *array++ = "State";
    *array++ = "Host";
    *array = "Idle";
  }
}

/****************************************************************
...
*****************************************************************/
static void players_active(void)
{
  LONG playerno;
  DoMethod(player_players_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &playerno);

  if (playerno)
  {
    struct player *pplayer;
    playerno -= 100;

    pplayer = &game.players[playerno];

    if (pplayer->spaceship.state != SSHIP_NONE)
      set(player_spaceship_button, MUIA_Disabled, FALSE);
    else
      set(player_spaceship_button, MUIA_Disabled, TRUE);

    switch(player_get_diplstate(game.player_idx, playerno)->type)
    {
      case DS_WAR:
      case DS_NO_CONTACT:
	   set(player_war_button, MUIA_Disabled, TRUE);
           break;

      default:
	   set(player_war_button, MUIA_Disabled, game.player_idx == playerno);
           break;
    }

    if (pplayer->is_alive && player_has_embassy(game.player_ptr, pplayer))
    {
      if (pplayer->is_connected)
	set(player_meet_button, MUIA_Disabled, FALSE);
      else
	set(player_meet_button, MUIA_Disabled, TRUE);
      set(player_intelligence_button, MUIA_Disabled, FALSE);
      return;
    }
  }

  set(player_meet_button, MUIA_Disabled, TRUE);
  set(player_intelligence_button, MUIA_Disabled, TRUE);
  set(player_war_button, MUIA_Disabled, TRUE);
}

/**************************************************************************
 Callback for the Intelligence button
**************************************************************************/
static void players_intelligence(void)
{
  LONG playerno;
  DoMethod(player_players_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &playerno);

  if (playerno)
  {
    playerno -= 100;

    if (player_has_embassy(game.player_ptr, &game.players[playerno]))
      popup_intel_dialog(&game.players[playerno]);
  }
}

/****************************************************************
 Callback for the Meet button
*****************************************************************/
static void players_meet(void)
{
  LONG playerno;
  DoMethod(player_players_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &playerno);

  if (playerno)
  {
    playerno -= 100;

    if (player_has_embassy(game.player_ptr, &game.players[playerno]))
    {
      request_diplomacy_init_meeting(game.player_idx, playerno);
    }
  }
  else
  {
    append_output_window("Game: You need an embassy to establish a diplomatic meeting.");
  }
}

/****************************************************************
 Callback for the war button
*****************************************************************/
static void players_war(void)
{
  LONG playerno;
  DoMethod(player_players_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &playerno);

  if (playerno)
  {
    playerno -= 100;

    if (player_has_embassy(game.player_ptr, &game.players[playerno]))
    {
      struct packet_generic_integer pa;
      pa.value = playerno;
      send_packet_generic_integer(&aconnection, PACKET_PLAYER_CANCEL_PACT,
				  &pa);
    }
  }
}

/****************************************************************
 Callback for the spaceship button
*****************************************************************/
static void players_spaceship(void)
{
  LONG playerno;
  DoMethod(player_players_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &playerno);

  if(playerno)
  {
    popup_spaceship_dialog(&game.players[playerno-100]);
  }
}

/****************************************************************
...
*****************************************************************/
void create_players_dialog(void)
{
  player_players_disphook.h_Entry = (HOOKFUNC) players_render;

  player_wnd = WindowObject,
    MUIA_Window_Title, "Players",
    WindowContents, VGroup,
	Child, player_players_listview = NListviewObject,
	MUIA_NListview_NList, NListObject,
	    MUIA_NList_DisplayHook, &player_players_disphook,
	    MUIA_NList_Title, TRUE,
	    MUIA_NList_Format, ",,,,,,,",
	    End,
	End,
	Child, HGroup,
	    Child, player_close_button = MakeButton("_Close"),
	    Child, player_intelligence_button = MakeButton("_Intelligence"),
	    Child, player_meet_button = MakeButton("_Meet"),
	    Child, player_war_button = MakeButton("_Cancel Treaty"),
	    Child, player_spaceship_button = MakeButton("_Spaceship"),
	    End,
	End,
    End;

  if (player_wnd)
  {
    set(player_intelligence_button, MUIA_Disabled, TRUE);
    set(player_meet_button, MUIA_Disabled, TRUE);
    set(player_spaceship_button, MUIA_Disabled, TRUE);
    DoMethod(player_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, player_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(player_close_button, MUIM_Notify, MUIA_Pressed, FALSE, player_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(player_intelligence_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, players_intelligence);
    DoMethod(player_meet_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, players_meet);
    DoMethod(player_war_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, players_war);
    DoMethod(player_spaceship_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, players_spaceship);
    DoMethod(player_players_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, app, 3, MUIM_CallHook, &civstandard_hook, players_active);
    DoMethod(app, OM_ADDMEMBER, player_wnd);
  }
}


/**************************************************************************
...
**************************************************************************/
void update_players_dialog(void)
{
  int i;

  if (!player_wnd || delay_plrdlg_update)
    return;

  set(player_players_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(player_players_listview, MUIM_NList_Clear);
  for (i = 0; i < game.nplayers; i++)
  {
    if(is_barbarian(&game.players[i]))
      continue;

    DoMethod(player_players_listview, MUIM_NList_InsertSingle, i + 100, MUIV_NList_Insert_Bottom);
  }
  set(player_players_listview, MUIA_NList_Quiet, FALSE);
}

