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

#include <windows.h>
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
                      
#include "capability.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "support.h"
#include "unit.h"
 
#include "chatline.h"
#include "citydlg.h"
#include "civclient.h"
#include "climap.h"
#include "climisc.h"
#include "clinet.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "inputdlg.h"
#include "mapview.h"
#include "menu.h"
#include "tilespec.h" 
#include "text.h"

#include "goto.h" 
#include "mapctrl.h"
#include "gui_main.h"

HWND popit_popup=NULL;
static bool popit_is_orders;
/*************************************************************************

*************************************************************************/
void map_handle_move(int window_x, int window_y)
{
  update_line(window_x, window_y);
}

/*************************************************************************

*************************************************************************/
static LONG CALLBACK popit_proc(HWND hwnd,UINT message,
				WPARAM wParam,LPARAM lParam)
{
  struct map_position *cross_list;
  switch(message)
    {
    case WM_CREATE:
    case WM_SIZE:
    case WM_GETMINMAXINFO:
      break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
      DestroyWindow(hwnd);
      break;
    case WM_DESTROY:
      cross_list = fcwin_get_user_data(hwnd);
      if (cross_list) {
	  while (cross_list->x >= 0) {
	    refresh_tile_mapcanvas(cross_list->x, cross_list->y, TRUE);
	    cross_list++;
	  }
      }
      if (popit_is_orders) {
	update_map_canvas_visible();
      }
      popit_popup = NULL;
      break;
    default:
      return DefWindowProc(hwnd,message,wParam,lParam);
    }
  return 0;
}

/*************************************************************************

*************************************************************************/
static void popit(int x, int y, int xtile, int ytile)
{
  HWND popup;
  POINT pt;
  RECT rc;
  struct fcwin_box *vbox;
  static struct map_position cross_list[2+1];
  struct map_position *cross_head = cross_list;
  int i;
  struct unit *punit;
  
  if (popit_popup!=NULL) {
    DestroyWindow(popit_popup);
    popit_popup=NULL;
  }
  
  if (tile_get_known(xtile,ytile)<TILE_KNOWN_FOGGED)
    return;
  
  popup=fcwin_create_layouted_window(popit_proc,NULL,WS_POPUP|WS_BORDER,
				     0,0,root_window,NULL,
				     FAKE_CHILD,
				     cross_head);
  vbox=fcwin_vbox_new(popup,FALSE);

  fcwin_box_add_static(vbox, popup_info_text(xtile, ytile), 0, SS_LEFT,
		       FALSE, FALSE, 0);

  punit = find_visible_unit(map_get_tile(xtile, ytile));

  popit_is_orders = show_unit_orders(punit);

  if (punit && is_goto_dest_set(punit)) {
    cross_head->x = goto_dest_x(punit);
    cross_head->y = goto_dest_y(punit);
    cross_head++;
  }
  cross_head->x = xtile;
  cross_head->y = ytile;
  cross_head++;

  cross_head->x = -1;
  for (i = 0; cross_list[i].x >= 0; i++) {
    put_cross_overlay_tile(cross_list[i].x, cross_list[i].y);
  }

  fcwin_set_box(popup,vbox);

  GetWindowRect(popup,&rc);
  pt.x=x;
  pt.y=y;
  ClientToScreen(map_window,&pt);
  MoveWindow(popup,pt.x+16,pt.y-rc.bottom+rc.top,
	     rc.right-rc.left,rc.bottom-rc.top,FALSE);
  ShowWindow(popup,SW_SHOWNORMAL);
  popit_popup=popup;
} 

/**************************************************************************

**************************************************************************/
static LONG CALLBACK map_wnd_proc(HWND hwnd,UINT message,WPARAM wParam, LPARAM lParam)
{
  HDC hdc;
  PAINTSTRUCT ps;
  int xtile;
  int ytile;
  switch(message) {
  case WM_CREATE:
    break;
  case WM_LBUTTONDOWN:
    if (!can_client_change_view()) {
      break;
    }
    SetFocus(root_window);
    if (wParam&MK_SHIFT) {
      adjust_workers_button_pressed(LOWORD(lParam), HIWORD(lParam));
      wakeup_button_pressed(LOWORD(lParam), HIWORD(lParam));
    } else if (wParam & MK_CONTROL
	       && canvas_to_map_pos(&xtile, &ytile,
				    LOWORD(lParam), HIWORD(lParam))) {
      popit(LOWORD(lParam),HIWORD(lParam),xtile,ytile);
    } else {
      action_button_pressed(LOWORD(lParam), HIWORD(lParam), SELECT_POPUP);
    }
    break;
  case WM_MBUTTONDOWN:
    if (can_client_change_view()
        && canvas_to_map_pos(&xtile, &ytile, LOWORD(lParam), HIWORD(lParam))) {
      popit(LOWORD(lParam), HIWORD(lParam), xtile, ytile);
    }
    break;
  case WM_RBUTTONDOWN:
    if (can_client_change_view()) {
      if (wParam&MK_CONTROL) {
        if (canvas_to_map_pos(&xtile, &ytile,
			      LOWORD(lParam), HIWORD(lParam))) {
          popit(LOWORD(lParam), HIWORD(lParam), xtile, ytile);
        }
      } else {
	recenter_button_pressed(LOWORD(lParam), HIWORD(lParam));
      }
    }
    break;
  case WM_SETFOCUS:
  case WM_ACTIVATE:
    SetFocus(root_window);
    break;
  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
    if (popit_popup!=NULL) {
      DestroyWindow(popit_popup);
      popit_popup=NULL;
      SetFocus(root_window);
    }
    break;
  case WM_MOUSEMOVE:
    if (can_client_change_view()) {
      map_handle_move(LOWORD(lParam),HIWORD(lParam));
    }
    break;
  case WM_PAINT:
    hdc=BeginPaint(hwnd,(LPPAINTSTRUCT)&ps);
    map_expose(hdc); 
    EndPaint(hwnd,(LPPAINTSTRUCT)&ps);
    break;
  default:
    return DefWindowProc(hwnd,message,wParam,lParam);
  }
  return 0;
}

/**************************************************************************

**************************************************************************/
void init_mapwindow(void)
{
  WNDCLASS *wndclass;
  wndclass=fc_malloc(sizeof(WNDCLASS));
  wndclass->style=0;
  wndclass->cbClsExtra=0;
  wndclass->cbWndExtra=0;
  wndclass->lpfnWndProc=(WNDPROC) map_wnd_proc;
  wndclass->hIcon=NULL;
  wndclass->hCursor=LoadCursor(NULL,IDC_ARROW);
  wndclass->hInstance=freecivhinst;
  wndclass->hbrBackground=GetStockObject(BLACK_BRUSH);
  wndclass->lpszClassName="freecivmapwindow";
  wndclass->lpszMenuName=(LPSTR)NULL;
  if (!RegisterClass(wndclass))
    exit(EXIT_FAILURE);
}

/**************************************************************************

**************************************************************************/
void overview_handle_rbut(int x, int y)
{
 int xtile, ytile;        

 if (!can_client_change_view()) {
   return;
 }

 overview_to_map_pos(&xtile, &ytile, x, y);

 center_tile_mapcanvas(xtile,ytile); 

}

/**************************************************************************

**************************************************************************/
void indicator_handle_but(int i)
{
  int delta = 10;
  int lux_end = game.player_ptr->economic.luxury;
  int sci_end = lux_end + game.player_ptr->economic.science;
#if 0 /* Unneeded. */
  int tax_end = 100; 
#endif
  int luxury = game.player_ptr->economic.luxury;
  int science = game.player_ptr->economic.science;
  int tax = game.player_ptr->economic.tax;
  
  i *= 10;
  if (i < lux_end) {
    luxury -= delta;
    science += delta;
  } else if (i < sci_end) {
    science -= delta;
    tax += delta;
  } else {
    tax -= delta;
    luxury += delta;
  }

  dsend_packet_player_rates(&aconnection, tax, luxury, science);
}

/**************************************************************************

**************************************************************************/
static void name_new_city_callback(HWND w, void *data)
{
  size_t unit_id;
 
  if ((unit_id = (size_t)data)) {
    dsend_packet_unit_build_city(&aconnection, unit_id, 
				 input_dialog_get_input(w));
  }
  input_dialog_destroy(w);
}
 
/**************************************************************************
 Popup dialog where the user choose the name of the new city
 punit = (settler) unit which builds the city
 suggestname = suggetion of the new city's name
**************************************************************************/     
void
popup_newcity_dialog(struct unit *punit, char *suggestname)
{
  input_dialog_create(NULL, /*"shellnewcityname"*/_("Build New City"),
                        _("What should we call our new city?"),
                        suggestname,
                        name_new_city_callback, (void *)punit->id,
                        name_new_city_callback, (void *)0);  
}

/**************************************************************************

**************************************************************************/
void center_on_unit(void)
{
   request_center_focus_unit(); 
}

/**************************************************************************

**************************************************************************/
void set_turn_done_button_state(bool state)
{
  EnableWindow(turndone_button,state);
}

/**************************************************************************

**************************************************************************/
void create_line_at_mouse_pos(void)
{
        /* PORTME */
}

/**************************************************************************
 The Area Selection rectangle. Called by center_tile_mapcanvas() and
 when the mouse pointer moves.
**************************************************************************/
void update_rect_at_mouse_pos(void)
{
  /* PORTME */
}
