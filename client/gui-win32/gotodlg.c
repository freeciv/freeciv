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
#include <windows.h>
#include <windowsx.h>

#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "packets.h"
#include "player.h"
#include "support.h"
#include "unit.h"

#include "clinet.h"
#include "civclient.h"
#include "control.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"


#include "gotodlg.h"

#define ID_GOTO 100
#define ID_AIRLIFT 101
#define ID_LIST 102
#define ID_ALL 103

static HWND goto_dialog;
static HWND goto_list;

static struct tile *original_tile;

static void update_goto_dialog(HWND list);
static struct city *get_selected_city(void);
static int show_all_cities;

/****************************************************************

*****************************************************************/
static LONG CALLBACK goto_dialog_proc(HWND dlg,UINT message,
				      WPARAM wParam,LPARAM lParam)
{
  struct city *pdestcity;
  switch(message)
    {
    case WM_CREATE:
    case WM_GETMINMAXINFO:
    case WM_SIZE:
      break;
    case WM_DESTROY:
      goto_dialog=NULL;
      break;
    case WM_CLOSE:
      center_tile_mapcanvas(original_tile); 
      DestroyWindow(dlg);
      break;
    case WM_COMMAND:
      switch(LOWORD(wParam))
	{
	case ID_LIST:
	  if((pdestcity=get_selected_city())) {
	    struct unit *punit=get_unit_in_focus();
	    center_tile_mapcanvas(pdestcity->tile);
	    if(punit && unit_can_airlift_to(punit, pdestcity)) {
	      EnableWindow(GetDlgItem(dlg,ID_AIRLIFT),TRUE);
	    } else {
	      EnableWindow(GetDlgItem(dlg,ID_AIRLIFT),FALSE);
	    }
	    break;
	  }
	  break;
	case ID_ALL:
	  show_all_cities=show_all_cities?0:1;
	  update_goto_dialog(GetDlgItem(dlg,ID_LIST));
	  break;
	case ID_GOTO:
	  {
	    pdestcity=get_selected_city();
	    if (pdestcity) {
	      struct unit *punit=get_unit_in_focus();
	      if (punit) {
		send_goto_unit(punit, pdestcity->tile);
		DestroyWindow(dlg);
	      }
	    }
	  }
	  break;
	case ID_AIRLIFT:
	  {
	    pdestcity=get_selected_city();
	    if (pdestcity) {
	      struct unit *punit=get_unit_in_focus();
	      if (punit) {
		request_unit_airlift(punit, pdestcity);
		DestroyWindow(dlg);
	      }
	    }
	 
	  }
	  break;
	case IDCANCEL:
	  center_tile_mapcanvas(original_tile);
	  DestroyWindow(dlg);
	  break;
	  
	}
      break;
    default:
      return DefWindowProc(dlg,message,wParam,lParam);
    }
  return 0;
}
/****************************************************************
...
*****************************************************************/
void
popup_goto_dialog(void)
{
  struct fcwin_box *hbox;
  struct fcwin_box *vbox;
  if (goto_dialog)
    return;
  if (!can_client_change_view()) {
    return;
  }
  if (get_unit_in_focus()==0)
    return;

  original_tile = get_center_tile_mapcanvas();
  
  goto_dialog=fcwin_create_layouted_window(goto_dialog_proc,
					   _("Goto/Airlift Unit"),
					   WS_OVERLAPPEDWINDOW,
					   CW_USEDEFAULT,CW_USEDEFAULT,
					   root_window,NULL,
					   REAL_CHILD,
					   NULL);
  vbox=fcwin_vbox_new(goto_dialog,FALSE);
  hbox=fcwin_hbox_new(goto_dialog,TRUE);
  fcwin_box_add_static(vbox,_("Goto/Airlift Unit"),
		       0,SS_LEFT,FALSE,FALSE,5);
  goto_list=fcwin_box_add_list(vbox,10,ID_LIST,
			       WS_VSCROLL | LBS_STANDARD,TRUE,TRUE,5);
  fcwin_box_add_button(hbox,_("Goto"),ID_GOTO,0,TRUE,TRUE,15);

  fcwin_box_add_button(hbox,_("Airlift"),ID_AIRLIFT,0,TRUE,TRUE,15);
  EnableWindow(GetDlgItem(goto_dialog,ID_AIRLIFT),FALSE);
  fcwin_box_add_button(hbox,_("All Cities"),ID_ALL,
		       0,TRUE,TRUE,15);
  fcwin_box_add_button(hbox,_("Cancel"),IDCANCEL,0,TRUE,TRUE,15);
  fcwin_box_add_box(vbox,hbox,FALSE,FALSE,5);
  fcwin_set_box(goto_dialog,vbox);
  update_goto_dialog(goto_list);
  ShowWindow(goto_dialog,SW_SHOWNORMAL);
}

/**************************************************************************
...
**************************************************************************/
static void update_goto_dialog(HWND list)
{
  int    i, j;
  char   name[MAX_LEN_NAME+3];

  ListBox_ResetContent(list);
  Button_SetState(GetDlgItem(goto_dialog,ID_ALL),show_all_cities);

  for(i=0; i<game.nplayers; i++) {
    if(!show_all_cities && i!=game.player_idx) continue;
    city_list_iterate(game.players[i].cities, pcity) {
      sz_strlcpy(name, pcity->name);
      /* FIXME: should use unit_can_airlift_to(). */
      if (pcity->airlift) {
        sz_strlcat(name, "(A)");
      }
      j=ListBox_AddString(list,name);
      ListBox_SetItemData(list,j,pcity->id);
    }
    city_list_iterate_end;
  }
}

/**************************************************************************
...
**************************************************************************/
static struct city *get_selected_city(void)
{
  int selection;  
  if ((selection=ListBox_GetCurSel(goto_list))==LB_ERR)
    return 0;
  return find_city_by_id(ListBox_GetItemData(goto_list,selection));

}
