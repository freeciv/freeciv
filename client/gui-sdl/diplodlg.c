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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "genlist.h"
#include "government.h"
#include "map.h"

#include "gui_mem.h"

#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "diptreaty.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "mapview.h"

#include "diplodlg.h"

#define MAX_NUM_CLAUSES 64

struct Diplomacy_dialog {
  struct Treaty treaty;
#if 0
  GtkWidget *dip_dialog_shell;
  GtkWidget *dip_hbox, *dip_vbox0, *dip_vboxm, *dip_vbox1;

  GtkWidget *dip_frame0;
  GtkWidget *dip_labelm;
  GtkWidget *dip_frame1;

  GtkWidget *dip_map_menu0;
  GtkWidget *dip_map_menu1;
  GtkWidget *dip_tech_menu0;
  GtkWidget *dip_tech_menu1;
  GtkWidget *dip_city_menu0;
  GtkWidget *dip_city_menu1;
  GtkWidget *dip_gold_frame0;
  GtkWidget *dip_gold_frame1;
  GtkWidget *dip_gold_entry0;
  GtkWidget *dip_gold_entry1;
  GtkWidget *dip_pact_menu;
  GtkWidget *dip_vision_button0;
  GtkWidget *dip_vision_button1;

  GtkWidget *dip_label;
  GtkWidget *dip_clauselabel;
  GtkWidget *dip_clauselist;
  GtkWidget *dip_acceptthumb0;
  GtkWidget *dip_acceptthumb1;

  GtkWidget *dip_accept_command;
  GtkWidget *dip_close_command;

  GtkWidget *dip_erase_clause_command;
#endif
};

#if 0
static void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
#endif

/**************************************************************************
  Update a player's acceptance status of a treaty (traditionally shown
  with the thumbs-up/thumbs-down sprite).
**************************************************************************/
void handle_diplomacy_accept_treaty(struct packet_diplomacy_info *pa)
{
  freelog(LOG_DEBUG, "handle_diplomacy_accept_treaty : PORT ME");
}

/**************************************************************************
  Handle the start of a diplomacy meeting - usually by poping up a
  diplomacy dialog.
**************************************************************************/
void handle_diplomacy_init_meeting(struct packet_diplomacy_info *pa)
{
  freelog(LOG_DEBUG, "handle_diplomacy_init_meeting : PORT ME");
}

/**************************************************************************
  Update the diplomacy dialog when the meeting is canceled (the dialog
  should be closed).
**************************************************************************/
void handle_diplomacy_cancel_meeting(struct packet_diplomacy_info *pa)
{
  freelog(LOG_DEBUG, "handle_diplomacy_cancel_meeting : PORT ME");
}

/**************************************************************************
  Update the diplomacy dialog by adding a clause.
**************************************************************************/
void handle_diplomacy_create_clause(struct packet_diplomacy_info *pa)
{
  freelog(LOG_DEBUG, "handle_diplomacy_create_clause : PORT ME");
}

/**************************************************************************
  Update the diplomacy dialog by removing a clause.
**************************************************************************/
void handle_diplomacy_remove_clause(struct packet_diplomacy_info *pa)
{
  freelog(LOG_DEBUG, "handle_diplomacy_remove_clause : PORT ME");
}

#if 0
/**************************************************************************
  ...
**************************************************************************/
static void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  freelog(LOG_DEBUG, "close_diplomacy_dialog : PORT ME");
}
#endif

/**************************************************************************
  Close all open diplomacy dialogs, for when client disconnects from game.
**************************************************************************/
void close_all_diplomacy_dialogs(void)
{
  /* called by set_client_state(...) */
  freelog(LOG_DEBUG, "close_all_diplomacy_dialogs : PORT ME");
}
