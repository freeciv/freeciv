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
#include <windows.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
                      
#include <stdlib.h>
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

#include "goto.h" 
#include "mapctrl.h"
#include "gui_main.h"

struct city *city_workers_display = NULL;

HWND popit_popup=NULL;
/*************************************************************************

*************************************************************************/
void map_handle_move(int window_x, int window_y)
{
  int x, y, old_x, old_y;

  if ((hover_state == HOVER_GOTO || hover_state == HOVER_PATROL)
      && draw_goto_line) {
    get_map_xy(window_x, window_y, &x, &y);
    
    get_line_dest(&old_x, &old_y);
    if (old_x != x || old_y != y) {
      draw_line(x, y);
    }
  }
  
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
  char s[512];
  struct city *pcity;
  struct unit *punit;
  struct tile *ptile=map_get_tile(xtile, ytile);
  if (popit_popup!=NULL) {
    DestroyWindow(popit_popup);
    popit_popup=NULL;
  }
  
  if (tile_get_known(xtile,ytile)<TILE_KNOWN_FOGGED)
    return;
  
  popup=fcwin_create_layouted_window(popit_proc,NULL,WS_POPUP|WS_BORDER,
				     0,0,root_window,NULL,
				     cross_head);
  vbox=fcwin_vbox_new(popup,FALSE);
  
  my_snprintf(s, sizeof(s), _("Terrain: %s"),
	      map_get_tile_info_text(xtile, ytile));
  fcwin_box_add_static(vbox,s,0,SS_LEFT,FALSE,FALSE,0);
  my_snprintf(s, sizeof(s), _("Food/Prod/Trade: %s"),
	      map_get_tile_fpt_text(xtile, ytile));
  fcwin_box_add_static(vbox,s,0,SS_LEFT,FALSE,FALSE,0);
  
  if (tile_has_special(ptile, S_HUT)) {
    fcwin_box_add_static(vbox,_("Minor Tribe Village"),
			 0,SS_LEFT,FALSE,FALSE,0);
  }
  
  if((pcity=map_get_city(xtile, ytile))) {
    my_snprintf(s, sizeof(s), _("City: %s(%s)"), pcity->name,
		get_nation_name(city_owner(pcity)->nation));
    fcwin_box_add_static(vbox,s,0,SS_LEFT,FALSE,FALSE,0);
    if (city_got_citywalls(pcity)) {
      fcwin_box_add_static(vbox,s,0,SS_LEFT,FALSE,FALSE,0);
    }
  }
   
  if (get_tile_infrastructure_set(ptile)) {
    sz_strlcpy(s, _("Infrastructure: "));
    sz_strlcat(s, map_get_infrastructure_text(ptile->special));
    fcwin_box_add_static(vbox,s,0,SS_LEFT,FALSE,FALSE,0);
  }

  sz_strlcpy(s, _("Activity: "));
  if (concat_tile_activity_text(s, sizeof(s), xtile, ytile)) {
    fcwin_box_add_static(vbox,s,0,SS_LEFT,FALSE,FALSE,0);
  }

  if((punit=find_visible_unit(ptile)) && !pcity) {
    char cn[64];
    struct unit_type *ptype=unit_type(punit);
    cn[0]='\0';
    if(punit->owner==game.player_idx) {
      struct city *pcity;
      pcity=player_find_city_by_id(game.player_ptr, punit->homecity);
      if(pcity)
	my_snprintf(cn, sizeof(cn), "/%s", pcity->name);
    }
    my_snprintf(s, sizeof(s), _("Unit: %s(%s%s)"), ptype->name,
		get_nation_name(unit_owner(punit)->nation), cn);

    fcwin_box_add_static(vbox,s,0,SS_LEFT,FALSE,FALSE,0);
    if(punit->owner==game.player_idx)  {
      char uc[64] = "";
      if(unit_list_size(&ptile->units)>=2) {
	my_snprintf(uc, sizeof(uc), _("  (%d more)"),
		    unit_list_size(&ptile->units) - 1);
      }
      my_snprintf(s, sizeof(s), _("A:%d D:%d FP:%d HP:%d/%d%s%s"),
		  ptype->attack_strength, 
		  ptype->defense_strength, ptype->firepower, punit->hp, 
		  ptype->hp, punit->veteran?_(" V"):"", uc);
      
      if(punit->activity==ACTIVITY_GOTO || punit->connecting)  {
	cross_head->x = punit->goto_dest_x;
	cross_head->y = punit->goto_dest_y;
	cross_head++;
      }
    } else {
      my_snprintf(s, sizeof(s), _("A:%d D:%d FP:%d HP:%d0%%"),
		  ptype->attack_strength, 
		  ptype->defense_strength, ptype->firepower, 
		  (punit->hp*100/ptype->hp + 9)/10 );
    }
    fcwin_box_add_static(vbox,s,0,SS_LEFT,FALSE,FALSE,0);
  }
  
  cross_head->x = xtile;
  cross_head->y = ytile;
  cross_head++;
  
  cross_head->x = -1;
  for (i = 0; cross_list[i].x >= 0; i++) {
    put_cross_overlay_tile(cross_list[i].x,cross_list[i].y);
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
static void adjust_workers(int map_x, int map_y)
{
  int x, y, is_valid;
  struct city *pcity;
  struct packet_city_request packet;
  enum city_tile_type wrk;
  

  pcity = find_city_near_tile(map_x, map_y);
  if (!pcity) {
    return;
  }
  
  is_valid = map_to_city_map(&x, &y, pcity, map_x, map_y);
  assert(is_valid);
  
  packet.city_id=pcity->id;
  packet.worker_x=x;
  packet.worker_y=y;
  
  wrk = get_worker_city(pcity, x, y);
  if(wrk==C_TILE_WORKER)
    send_packet_city_request(&aconnection, &packet, 
			     PACKET_CITY_MAKE_SPECIALIST);
  else if(wrk==C_TILE_EMPTY)
    send_packet_city_request(&aconnection, &packet, 
			     PACKET_CITY_MAKE_WORKER);
  
  /* When the city info packet is received, update the workers on the map*/
  city_workers_display = pcity;
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
    if (get_client_state()!=CLIENT_GAME_RUNNING_STATE)
      break;
    SetFocus(root_window);
    get_map_xy(LOWORD(lParam),HIWORD(lParam),&xtile,&ytile);
    if (wParam&MK_SHIFT) {
      adjust_workers(xtile,ytile);
    } else if (wParam&MK_CONTROL){
      popit(LOWORD(lParam),HIWORD(lParam),xtile,ytile);
    } else {
      do_map_click(xtile,ytile);
      wakeup_sentried_units(xtile,ytile);
    }
    break;
  case WM_RBUTTONDOWN:
    if (get_client_state()==CLIENT_GAME_RUNNING_STATE) {
      get_map_xy(LOWORD(lParam),HIWORD(lParam),&xtile,&ytile);
      if (wParam&MK_CONTROL) {
	popit(LOWORD(lParam),HIWORD(lParam),xtile,ytile);	
      } else {
	center_tile_mapcanvas(xtile,ytile);
      }
    }
    break;
  case WM_SETFOCUS:
  case WM_ACTIVATE:
    SetFocus(root_window);
    break;
  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
    if (popit_popup!=NULL) {
      DestroyWindow(popit_popup);
      popit_popup=NULL;
      SetFocus(root_window);
    }
    break;
  case WM_MOUSEMOVE:
    if (get_client_state()==CLIENT_GAME_RUNNING_STATE) {
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
void init_mapwindow()
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
 if (is_isometric) {
   xtile=x/2-(map.xsize/2-(map_view_x+(map_view_width+map_view_height)/2));
 } else {
   xtile=x/2-(map.xsize/2-(map_view_x+map_view_width/2));
 }

 ytile=y/2; 

 if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
     return ;
 center_tile_mapcanvas(xtile,ytile); 

}

/**************************************************************************

**************************************************************************/
void indicator_handle_but(int i)
{
  int tax_end,lux_end,sci_end;
  int delta=10;
  struct packet_player_request packet;       
  lux_end= game.player_ptr->economic.luxury;
  sci_end= lux_end + game.player_ptr->economic.science;
  tax_end= 100; 
  
  packet.luxury= game.player_ptr->economic.luxury;
  packet.science= game.player_ptr->economic.science;
  packet.tax= game.player_ptr->economic.tax;
  
  i*= 10;
  if(i<lux_end){
    packet.luxury-= delta; packet.science+= delta;
  }else if(i<sci_end){
    packet.science-= delta; packet.tax+= delta;
  }else{
   packet.tax-= delta; packet.luxury+= delta;
  }
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_RATES);
  
}

/**************************************************************************

**************************************************************************/
static void name_new_city_callback(HWND w, void *data)
{
  size_t unit_id;
 
  if((unit_id=(size_t)data)) {
    struct packet_unit_request req;
    req.unit_id=unit_id;
    sz_strlcpy(req.name, input_dialog_get_input(w));
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
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
void center_on_unit()
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
void focus_to_next_unit(void)
{
  advance_unit_focus();
}
           
/**************************************************************************

**************************************************************************/
void create_line_at_mouse_pos(void)
{
        /* PORTME */
}
