/********************************************************************** 
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Team
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
#include <fc_config.h>
#endif

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Viewport.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "support.h"

/* common */
#include "game.h"

/* client */
#include "client_main.h"
#include "connectdlg_g.h"
#include "dialogs_g.h"

#include "chatline.h" /* for send_chat() */
#include "gui_main.h"
#include "gui_stuff.h" /* for xaw_set_relative_position() */

#include "pages.h"

static enum client_pages old_page;

static Widget start_page_shell;
static Widget start_page_form;
static Widget start_page_label;
static Widget start_page_viewport;
static Widget start_page_players_list;
static Widget start_page_cancel_command;
static Widget start_page_nation_command;
static Widget start_page_take_command;
static Widget start_page_start_command;
void start_page_cancel_callback(Widget w, XtPointer client_data,
				XtPointer call_data);
void start_page_nation_callback(Widget w, XtPointer client_data,
				XtPointer call_data);
void start_page_take_callback(Widget w, XtPointer client_data,
			       XtPointer call_data);
void start_page_start_callback(Widget w, XtPointer client_data,
			       XtPointer call_data);


/***************************************************************************
  Returns current client page
***************************************************************************/
enum client_pages get_current_client_page(void)
{
  return old_page;
}

/**************************************************************************
  Sets the "page" that the client should show.  See documentation in
  pages_g.h.
**************************************************************************/
void real_set_client_page(enum client_pages page)
{
  /* PORTME, PORTME, PORTME */
  switch (page) {
  case PAGE_MAIN:
    /* FIXME: call main/intro page rather than falling to network page */
    gui_server_connect();
    break;
  case PAGE_GAME:
    if (old_page == PAGE_START) {
      popdown_start_page();
    }
    break;
  case PAGE_START:
    popup_start_page();
    conn_list_dialog_update();
    break;
  case PAGE_SCENARIO:
  case PAGE_LOAD:
  case PAGE_NETWORK:
  case PAGE_GGZ:
    break;
  }

  old_page = page;
}

/****************************************************************************
  Set the list of available rulesets.  The default ruleset should be
  "default", and if the user changes this then set_ruleset() should be
  called.
****************************************************************************/
void gui_set_rulesets(int num_rulesets, char **rulesets)
{
  /* PORTME */
}

/**************************************************************************
                                  START PAGE
**************************************************************************/

/**************************************************************************
  Popup start page.
**************************************************************************/
void popup_start_page(void)
{
  if (!start_page_shell) {
    create_start_page();
  }

  xaw_set_relative_position(toplevel, start_page_shell, 5, 25);
  XtPopup(start_page_shell, XtGrabNone);
}

/**************************************************************************
  Close start page.
**************************************************************************/
void popdown_start_page(void)
{
  if (start_page_shell) {
    XtDestroyWidget(start_page_shell);
    start_page_shell = 0;
  }
}

/**************************************************************************
  Create start page.
**************************************************************************/
void create_start_page(void)
{
  start_page_shell =
    I_IN(I_T(XtCreatePopupShell("startpage",
				topLevelShellWidgetClass,
				toplevel, NULL, 0)));

  start_page_form = XtVaCreateManagedWidget("startpageform",
				       formWidgetClass,
				       start_page_shell, NULL);

  start_page_label = I_L(XtVaCreateManagedWidget("startpagelabel",
					labelWidgetClass,
					start_page_form, NULL));

  start_page_viewport =
    XtVaCreateManagedWidget("startpageviewport", viewportWidgetClass,
                            start_page_form, NULL);

  start_page_players_list =
    XtVaCreateManagedWidget("startpageplayerslist",
			    listWidgetClass,
			    start_page_viewport,
			    NULL);

  start_page_cancel_command =
    I_L(XtVaCreateManagedWidget("startpagecancelcommand",
				commandWidgetClass,
				start_page_form, NULL));

  start_page_nation_command =
    I_L(XtVaCreateManagedWidget("startpagenationcommand",
				commandWidgetClass,
				start_page_form,
				NULL));

  start_page_take_command =
    I_L(XtVaCreateManagedWidget("startpagetakecommand",
				commandWidgetClass,
				start_page_form,
				NULL));

  start_page_start_command =
    I_L(XtVaCreateManagedWidget("startpagestartcommand",
				commandWidgetClass,
				start_page_form,
				NULL));

/*
  XtAddCallback(start_page_players_list, XtNcallback,
		start_page_players_list_callback,
		NULL);
*/
  XtAddCallback(start_page_cancel_command, XtNcallback,
		start_page_cancel_callback,
		NULL);

  XtAddCallback(start_page_nation_command, XtNcallback,
		start_page_nation_callback,
		NULL);

  XtAddCallback(start_page_take_command, XtNcallback,
		start_page_take_callback,
		NULL);

  XtAddCallback(start_page_start_command, XtNcallback,
		start_page_start_callback,
		NULL);

  update_start_page();

  XtRealizeWidget(start_page_shell);
  
  XSetWMProtocols(display, XtWindow(start_page_shell),
		  &wm_delete_window, 1);
  XtOverrideTranslations(start_page_shell,
    XtParseTranslationTable("<Message>WM_PROTOCOLS: msg-close-start-page()"));
}

/**************************************************************************
  Update start page players list.
**************************************************************************/
void update_start_page(void)
{
  if (!start_page_shell) {
    return;
  }
  if ( C_S_PREPARING == client_state()) {
    bool is_ready;
    const char *nation, *leader;
    char name[MAX_LEN_NAME + 8];
    static char *namelist_ptrs[MAX_NUM_PLAYERS];
    static char namelist_text[MAX_NUM_PLAYERS][256];
    int j;
    Dimension width, height;

    j = 0;

    players_iterate(pplayer) {
      if (pplayer->ai_controlled && !pplayer->was_created
          && !pplayer->is_connected) {
        fc_snprintf(name, sizeof(name), _("<%s AI>"),
                    ai_level_name(pplayer->ai_common.skill_level));
      } else {
        sz_strlcpy(name, pplayer->username);
      }
      is_ready = pplayer->ai_controlled ? TRUE: pplayer->is_ready;
      if (pplayer->nation == NO_NATION_SELECTED) {
	nation = _("Random");
	leader = "";
      } else {
	nation = nation_adjective_for_player(pplayer);
	leader = player_name(pplayer);
      }

      fc_snprintf(namelist_text[j], sizeof(namelist_text[j]),
		  "%-16s %-5s %-16s %-16s %-4d ",
		  name,
		  is_ready ? " Yes  " : " No   ",
		  leader,
		  nation,
		  player_number(pplayer));

      namelist_ptrs[j]=namelist_text[j];
      j++;
    } players_iterate_end;
    conn_list_iterate(game.est_connections, pconn) {
      if (NULL != pconn->playing && !pconn->observer) {
	continue; /* Already listed above. */
      }
      sz_strlcpy(name, pconn->username);
      nation = "";
      leader = "";

      fc_snprintf(namelist_text[j], sizeof(namelist_text[j]),
		  "%-16s %-5s %-16s %-16s %-4d ",
		  name,
		  " No  ",
		  leader,
		  nation,
		  -1);

      namelist_ptrs[j]=namelist_text[j];
      j++;
    } conn_list_iterate_end;
    XawFormDoLayout(start_page_form, False);
    XawListChange(start_page_players_list, namelist_ptrs, j, 0, True);

    XtVaGetValues(start_page_players_list, XtNlongest, &width, NULL);
    XtVaGetValues(start_page_players_list, XtNheight, &height, NULL);
    XtVaSetValues(start_page_viewport, XtNwidth, width + 15, NULL); 
    XtVaSetValues(start_page_label, XtNwidth, width + 15, NULL); 
    XawFormDoLayout(start_page_form, True);
    if (height > 120) {
      height = 120;
    }
    XtVaSetValues(start_page_viewport, XtNheight, height, NULL); 
  }
}

/**************************************************************************
  Callback for start page "Cancel" button
**************************************************************************/
void start_page_cancel_callback(Widget w, XtPointer client_data,
				XtPointer call_data)
{
  popdown_start_page();
}

/**************************************************************************
  Called when "Pick Nation" is clicked.
**************************************************************************/
void start_page_nation_callback(Widget w, XtPointer client_data,
				XtPointer call_data)
{
  if (NULL != client.conn.playing) {
    popup_races_dialog(client.conn.playing);
  }
}

/**************************************************************************
  Callback for start page "Take" button
**************************************************************************/
void start_page_take_callback(Widget w, XtPointer client_data,
			       XtPointer call_data)
{
  XawListReturnStruct *ret;
  struct player *selected_plr;
  ret=XawListShowCurrent(start_page_players_list);
  if(ret->list_index!=XAW_LIST_NONE) {
    selected_plr=player_by_number(ret->list_index);
    if(NULL != selected_plr ) {
      send_chat_printf("/take \"%s\"", player_name(selected_plr));
      if(selected_plr != client_player() && selected_plr->ai_controlled) {
        send_chat("/away");
        send_chat_printf("/%s",ai_level_name(selected_plr->ai_common.skill_level));
      }
    }
  }
}

/**************************************************************************
  Callback for start page "Start" button
**************************************************************************/
void start_page_start_callback(Widget w, XtPointer client_data,
			       XtPointer call_data)
{
  /* popdown_start_page(); */
  send_chat("/start");
}

/**************************************************************************
  Callback for start page window (x) button
**************************************************************************/
void start_page_msg_close(Widget w)
{
  popdown_start_page();
}

