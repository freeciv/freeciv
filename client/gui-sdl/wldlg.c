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
                          wldlg.c  -  description
                             -------------------
    begin                : Wed Sep 18 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "gui_mem.h"

#include "city.h"
#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"


#include "packets.h"
#include "worklist.h"
#include "support.h"
#include "climisc.h"
#include "clinet.h"

#include "wldlg.h"
#include "citydlg.h"

#define WORKLIST_ADVANCED_TARGETS  	1
#define WORKLIST_CURRENT_TARGETS   	0
#define COLUMNS				4
#define BUFFER_SIZE			100
#define NUM_TARGET_TYPES		3

/*
 * The Worklist Report dialog shows all the global worklists that the
 * player has defined.  There can be at most MAX_NUM_WORKLISTS global
 * worklists.
 */
struct worklist_report {
  /*GtkWidget *list, *shell, *btn_edit, *btn_rename, *btn_delete; */
  struct player *pplr;
  char worklist_names[MAX_NUM_WORKLISTS][MAX_LEN_NAME];
  char *worklist_names_ptrs[MAX_NUM_WORKLISTS + 1];
  struct worklist *worklist_ptr[MAX_NUM_WORKLISTS];
  int wl_idx;
};

/**************************************************************************
  If the worklist report is open, force its contents to be updated.
**************************************************************************/
void update_worklist_report_dialog(void)
{
  freelog(LOG_DEBUG, "WLDLG: update_worklist_report_dialog : PORT ME");
}

/**************************************************************************
  ...
**************************************************************************/
void popup_worklists_report(struct player *pPlayer)
{
  freelog(LOG_DEBUG, "WLDLG: popup_worklists_report : PORT ME");
}
