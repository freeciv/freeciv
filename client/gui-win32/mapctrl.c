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
#include "clinet.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "graphics.h"
#include "inputdlg.h"
#include "mapview.h"
#include "menu.h"
#include "tilespec.h" 

#include "goto.h" 
#include "mapctrl.h"
#include "gui_main.h"

struct city *city_workers_display = NULL;

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
    get_map_xy(LOWORD(lParam),HIWORD(lParam),&xtile,&ytile);
    do_map_click(xtile,ytile);
    wakeup_sentried_units(xtile,ytile);
    break;
  case WM_RBUTTONDOWN:
    get_map_xy(LOWORD(lParam),HIWORD(lParam),&xtile,&ytile);  
    center_tile_mapcanvas(xtile,ytile);
    break;
  case WM_MOUSEMOVE:
    map_handle_move(LOWORD(lParam),HIWORD(lParam));
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
    exit(1);
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
void
set_turn_done_button_state( int state )
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
