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
#include <winsock.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>

#include "fciconv.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "shared.h"
#include "support.h"
#include "version.h"

#include "chatline.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "colors.h"
#include "connectdlg.h"
#include "control.h"
#include "dialogs.h"
#include "gotodlg.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "helpdata.h"           /* boot_help_texts() */
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "optiondlg.h"
#include "options.h"
#include "spaceshipdlg.h"
#include "tilespec.h"

#include <stdio.h>

#include "gui_main.h"

const char *client_string = "gui-win32";

HINSTANCE freecivhinst;
HWND hchatline;
HWND root_window;
HWND logoutput_win;
HWND infolabel_win;
HWND unit_info_frame;
HWND unit_info_label;
HWND turndone_button;
HWND timeout_label;
HWND map_window;
HWND map_scroll_h;
HWND map_scroll_v;
HFONT font_8courier;
HFONT font_12courier;
HFONT font_12arial;
HACCEL freecivaccel;
int main_win_width;
int main_win_height;
int map_win_x;
int map_win_y;
int taxinfoline_y;
int indicator_y;
int overview_win_x;
int overview_win_y;
int overview_win_width;
int overview_win_height;
static int net_input=-1;


extern int seconds_to_turndone;   

bool better_fog = TRUE;

const static RECT textwin_size={0,1,0,100};

struct fcwin_box *main_win_box;
struct fcwin_box *output_box;

client_option gui_options[] = {
  GEN_BOOL_OPTION(better_fog,
		  N_("Better fog-of-war drawing"))
};
const int num_gui_options = ARRAY_SIZE(gui_options);

void socket_timer(void);

/**************************************************************************

**************************************************************************/
static void HandleRMouse(int x, int y)
{
  SetFocus(root_window);
}

/**************************************************************************

**************************************************************************/
static void HandleLMouse(int x, int y)
{
  SetFocus(root_window);
  if ((x>overview_win_x)&&(x<overview_win_x+overview_win_width)
	   &&(y>overview_win_y)&&(y<overview_win_y+overview_win_height))
    overview_handle_rbut(x-overview_win_x,y-overview_win_y);
  else if ((x<10*SMALL_TILE_WIDTH)&&(y>taxinfoline_y)
	   &&(y<taxinfoline_y+2*SMALL_TILE_HEIGHT))
    indicator_handle_but(x/SMALL_TILE_WIDTH);
}

/**************************************************************************

 **************************************************************************/
static void HandlePaint(HDC hdc)
{
  overview_expose(hdc);
}

/**************************************************************************

**************************************************************************/
void do_mainwin_layout(void)
{
  fcwin_redo_layout(root_window);
 
}

/**************************************************************************

**************************************************************************/
void set_overview_win_dim(int w, int h)
{
  overview_win_width=w;
  overview_win_height=h;
  
  do_mainwin_layout();
  
}

static void Handle_Hscroll(HWND hWnd, HWND hWndCtl, UINT code, int pos)
{
  int PosCur, PosMax, PosMin, id, xstep, ystep;
  get_mapview_scroll_step(&xstep, &ystep);
  PosCur=ScrollBar_GetPos(hWndCtl);
  ScrollBar_GetRange(hWndCtl,&PosMin,&PosMax);
  id=GetDlgCtrlID(hWndCtl);
  switch(code)
    {
    case SB_LINELEFT: 
      if (id==ID_MAPHSCROLL)
	PosCur -= xstep;
      if (id==ID_MAPVSCROLL)
	PosCur -= ystep;
      break;
    case SB_LINERIGHT: 
      if (id==ID_MAPHSCROLL)
	PosCur += xstep;
      if (id==ID_MAPVSCROLL)
	PosCur += ystep;
      break;
    case SB_PAGELEFT: PosCur-=(PosMax-PosMin+1)/10; break;
    case SB_PAGERIGHT: PosCur+=(PosMax-PosMin+1)/10; break;
    case SB_LEFT: PosCur=PosMin; break;
    case SB_RIGHT: PosCur=PosMax; break;
    case SB_THUMBTRACK: PosCur=pos; break;
    case SB_THUMBPOSITION:
    case SB_ENDSCROLL:
      if (id==ID_MAPHSCROLL)
	map_handle_hscroll(PosCur);
      if (id==ID_MAPVSCROLL)
	map_handle_vscroll(PosCur);
      break;
    }
  if (PosCur<PosMin) PosCur=PosMin;
  if (PosCur>PosMax) PosCur=PosMax;
  ScrollBar_SetPos(hWndCtl,PosCur,TRUE); 
};

/**************************************************************************

**************************************************************************/
static void box_fixedsize(POINT * minsize,void *data)
{
  RECT *rc=data;
  minsize->y=rc->bottom-rc->top;
  minsize->x=rc->right-rc->left;

}

/**************************************************************************

**************************************************************************/
static void taxinfoline_minsize(POINT * minsize,void *data)
{
  minsize->x=10*SMALL_TILE_WIDTH;
  minsize->y=1*SMALL_TILE_HEIGHT;
}

/**************************************************************************

**************************************************************************/
static void indicator_line_minsize(POINT *minsize, void *data)
{
  minsize->x=4*SMALL_TILE_WIDTH;
  minsize->y=1*SMALL_TILE_HEIGHT;
}
/**************************************************************************

**************************************************************************/
static void indicator_line_setsize(LPRECT newsize, void *data)
{
  indicator_y=newsize->top;
}

/**************************************************************************

**************************************************************************/
static void taxinfoline_setsize(LPRECT newsize,void *data)
{
  RECT rc;
  rc=*newsize;
  rc.bottom+=SMALL_TILE_HEIGHT;
  InvalidateRect(root_window,newsize,TRUE);
  taxinfoline_y=newsize->top;
		 
}

/**************************************************************************

**************************************************************************/
static void textwin_setsize(LPRECT newsize,void *data)
{
  MoveWindow(logoutput_win,newsize->left,newsize->top,
	     newsize->right-newsize->left,
	     newsize->bottom-newsize->top,TRUE);
}

/**************************************************************************

**************************************************************************/
static void overview_minsize(POINT * minsize,void *data)
{
  minsize->x=overview_win_width;
  minsize->y=overview_win_height;
  
}

/**************************************************************************

**************************************************************************/
static void overview_setsize(LPRECT newsize, void *data)
{
  if ((overview_win_x==newsize->left)&&(overview_win_y==newsize->top))
    return;
  overview_win_x=newsize->left;
  overview_win_y=newsize->top;
  InvalidateRect(root_window,newsize,TRUE);
}

/**************************************************************************

**************************************************************************/
static void map_minsize(POINT * minsize, void *data)
{
  minsize->x=NORMAL_TILE_WIDTH+15;
  minsize->y=NORMAL_TILE_HEIGHT+15;
}

/**************************************************************************

**************************************************************************/
static void map_setsize(LPRECT newsize, void *data)
{
  int mx,my,mw,mh;
  mx=newsize->left;
  my=newsize->top;
  mw=newsize->right-newsize->left-15;
  mh=newsize->bottom-newsize->top-15;
  if ((mx==map_win_x)&&(map_win_y==my)&&
      (map_win_width==mw)&&(map_win_height==mh))
    return;
  if ((mw<(2*NORMAL_TILE_WIDTH))||(mh<(2*NORMAL_TILE_HEIGHT))) {
    return;
  }
  map_win_x=mx;
  map_win_y=my;
  map_win_height=mh;
  map_win_width=mw;
  InvalidateRect(root_window,newsize,FALSE);
  MoveWindow(map_window,map_win_x,map_win_y,map_win_width,map_win_height,TRUE);
  MoveWindow(map_scroll_v,map_win_x+map_win_width,map_win_y,
	     15,map_win_height+map_win_y,TRUE);
  MoveWindow(map_scroll_h,map_win_x,map_win_y+map_win_height,
	     map_win_width,15,TRUE);
  
  map_canvas_resized(mw, mh);
}


/**************************************************************************

**************************************************************************/
static LONG APIENTRY FreecivWndProc(HWND hWnd, UINT message,
				    UINT wParam, LONG lParam)
{
  HDC hdc;
  PAINTSTRUCT ps;

  switch (message)
    {
    case WM_CREATE:
      break;
    case WM_GETMINMAXINFO:
      break;
    case WM_SIZE:
      {
	RECT rc;
	rc.left=0;
	rc.right=LOWORD(lParam);
	rc.top=0;
	rc.bottom=HIWORD(lParam);
	InvalidateRect(hWnd,&rc,TRUE);
      }
      break;
    case WM_PAINT:
      hdc=BeginPaint(hWnd,(LPPAINTSTRUCT)&ps);
      HandlePaint(hdc); 
      EndPaint(hWnd,(LPPAINTSTRUCT)&ps);
      return TRUE;
      break;
    case WM_COMMAND:       
      handle_menu(LOWORD(wParam));
      break;
    case WM_HSCROLL:
      HANDLE_WM_HSCROLL(hWnd,wParam,lParam,Handle_Hscroll); 
      break;
    case WM_VSCROLL:
      HANDLE_WM_VSCROLL(hWnd,wParam,lParam,Handle_Hscroll); 
      break;
    case WM_LBUTTONDOWN:
      HandleLMouse(LOWORD(lParam),HIWORD(lParam));
      break;
    case WM_RBUTTONDOWN:
      HandleRMouse(LOWORD(lParam),HIWORD(lParam));
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      break;
    default:
      return DefWindowProc(hWnd,message,wParam,lParam);
    }
  return (0);
}


/**************************************************************************

**************************************************************************/
static void create_main_window(void)
{
  HINSTANCE riched;
  struct fcwin_box *upper;
  struct fcwin_box *leftrow;
  struct fcwin_box *hbox;
  root_window=fcwin_create_layouted_window(FreecivWndProc,"Freeciv",
					   WS_OVERLAPPEDWINDOW,
					   CW_USEDEFAULT,
					   CW_USEDEFAULT,
					   (HWND) NULL,
					   NULL,
					   FAKE_CHILD,
					   NULL);
  main_win_box=fcwin_vbox_new(root_window,FALSE);
  upper=fcwin_hbox_new(root_window,FALSE);
  leftrow=fcwin_vbox_new(root_window,FALSE);
  fcwin_box_add_generic(leftrow,overview_minsize,overview_setsize,NULL,
			NULL,TRUE,FALSE,0);
  infolabel_win=fcwin_box_add_static_default(leftrow," \n \n \n \n \n",
					     0,SS_CENTER);
  fcwin_box_add_generic(leftrow,taxinfoline_minsize,
			taxinfoline_setsize,NULL,NULL,
			FALSE,FALSE,0);
  hbox=fcwin_hbox_new(root_window,FALSE);
  fcwin_box_add_generic(hbox,indicator_line_minsize,
			indicator_line_setsize,NULL,NULL,
			FALSE,FALSE,0);
  timeout_label=fcwin_box_add_static(hbox," ",0,SS_CENTER,TRUE,TRUE,0);
  fcwin_box_add_box(leftrow,hbox,FALSE,FALSE,0);
  turndone_button=fcwin_box_add_button_default(leftrow,_("Turn Done"),
					       ID_TURNDONE,0);
  hbox = fcwin_hbox_new(root_window, FALSE);
  unit_info_label = fcwin_box_add_static_default(hbox, " \n \n \n \n", 0,
						 SS_LEFT);
  unit_info_frame = fcwin_box_add_groupbox(leftrow, "", hbox, 0, FALSE,
					   FALSE, 0);
  fcwin_box_add_box(upper,leftrow,FALSE,FALSE,5);
  map_window=CreateWindow("freecivmapwindow",NULL,WS_CHILD | WS_VISIBLE,
			  0,0,20,20,root_window,
			  NULL,freecivhinst,NULL);
  fcwin_box_add_generic(upper,map_minsize,map_setsize,NULL,NULL,
			TRUE,TRUE,5);
  fcwin_box_add_box(main_win_box,upper,TRUE,TRUE,0);
  output_box=fcwin_hbox_new(root_window,FALSE);
  fcwin_box_add_generic(output_box,box_fixedsize,textwin_setsize,NULL,
			(void *)&textwin_size,TRUE,TRUE,0);
  fcwin_box_add_box(main_win_box,output_box,FALSE,FALSE,0);
  
  riched=LoadLibrary("RICHED32.DLL");
  logoutput_win=CreateWindowEx(WS_EX_CLIENTEDGE,
			       (riched != NULL) ? "RichEdit" : "EDIT",
			       " ",
			       WS_CHILD | ES_READONLY | WS_VISIBLE | 
			       WS_VSCROLL | ES_LEFT | ES_WANTRETURN |
			       ES_MULTILINE | ES_AUTOVSCROLL,
			       0, 0, 0, 0,
			       root_window,
			       (HMENU) ID_OUTPUTWINDOW,
			       freecivhinst,
			       NULL);
  if (riched != NULL) {
    SendMessage(logoutput_win, EM_EXLIMITTEXT, 0, 1<<20);
  }
  
  ShowWindow(logoutput_win,SW_SHOWNORMAL);
  map_scroll_v=CreateWindow("SCROLLBAR",NULL,
			    WS_CHILD | WS_VISIBLE | SBS_VERT,
			    0,0,0,0,
			    root_window,
			    (HMENU) ID_MAPVSCROLL,
			    freecivhinst,
			    NULL);
  map_scroll_h=CreateWindow("SCROLLBAR",NULL,
			    WS_CHILD | WS_VISIBLE | SBS_HORZ,
			    0,0,0,0,
			    root_window,
			    (HMENU) ID_MAPHSCROLL,
			    (HINSTANCE) GetWindowLong(root_window, GWL_HINSTANCE),
			    NULL);
  SendMessage(infolabel_win,
	      WM_SETFONT,(WPARAM) font_12arial,MAKELPARAM(TRUE,0)); 
  SendMessage(unit_info_frame,
	      WM_SETFONT,(WPARAM) font_12arial,MAKELPARAM(TRUE,0)); 
  SendMessage(unit_info_label,
	      WM_SETFONT,(WPARAM) font_12arial,MAKELPARAM(TRUE,0)); 
  append_output_window(_("Freeciv is free software and you are welcome to distribute copies of"
			 " it\nunder certain conditions; See the \"Copying\" item on the Help"
			 " menu.\nNow.. Go give'em hell!") );
  hchatline=fcwin_box_add_edit(main_win_box,"",40,
			       IDOK, 
			       ES_WANTRETURN | ES_AUTOVSCROLL | ES_MULTILINE,
			       FALSE,FALSE,10);
  
  fcwin_set_box(root_window,main_win_box);
  init_mapcanvas_and_overview();

  /* Need to set these so the version number shows up properly */
  overview_win_width = 160;
  overview_win_height = 100;

  init_color_system();
}

/**************************************************************************

**************************************************************************/
static VOID CALLBACK blink_timer(HWND hwnd, UINT uMsg, UINT idEvent,
				 DWORD dwTime)
{
  if (can_client_change_view()) {
    check_mapstore();
  }

  real_timer_callback();
}

/**************************************************************************

**************************************************************************/
void socket_timer ()
{
    struct timeval tv;
  fd_set civfdset;
  while (net_input>=0)
    {
 
      FD_ZERO(&civfdset);
      FD_SET(net_input,&civfdset);
      tv.tv_sec=0;
      tv.tv_usec=0;
      if (select(1,&civfdset,NULL,NULL,&tv))
        {
          if (FD_ISSET(net_input,&civfdset))
            {
              input_from_server(net_input);
            }
        }
      else
        {
          break;
        }
    }           
} 

/**************************************************************************
  This pops down every dialog
**************************************************************************/
void popdown_all_game_dialogs(void)
{
  RECT rc;
  fcwin_close_all_childs(root_window);
  GetClientRect(root_window, &rc);
  InvalidateRect(root_window, &rc, FALSE);
  UpdateWindow(root_window);
  InvalidateRect(root_window, &rc, FALSE);
}

/**************************************************************************

**************************************************************************/
void ui_init(void)
{
  init_character_encodings(NULL, TRUE);
}

/**************************************************************************

**************************************************************************/
void
ui_main(int argc, char *argv[])
{
  RECT rc;
  MSG msg;
  freecivhinst=GetModuleHandle(NULL); /* There is no WinMain! */
  bool quit = FALSE;
  init_layoutwindow();
  InitCommonControls();
  unitselect_init(freecivhinst);
  init_mapwindow();
  font_8courier=GetStockObject(ANSI_FIXED_FONT);
  font_12courier=font_8courier;
  font_12arial=GetStockObject(DEFAULT_GUI_FONT);
  create_main_window();
  GetWindowRect(root_window,&rc);
  MoveWindow(root_window,rc.left,rc.top,640,480,TRUE);
  SetMenu(root_window,create_mainmenu());
  ShowWindow(root_window,SW_SHOWNORMAL);
  UpdateWindow(root_window);
  tilespec_load_tiles();
  init_fog_bmp();
  load_cursors();

  freecivaccel=my_create_menu_acceltable();
 
  set_client_state(CLIENT_PRE_GAME_STATE);

  SetTimer(root_window, 2, TIMER_INTERVAL, blink_timer);

  while (!quit) {
    socket_timer();
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
	quit = TRUE;
      }
      if (!((msg.hwnd == root_window)
	  && (TranslateAccelerator(root_window, freecivaccel, &msg)))) {
	TranslateMessage(&msg);
	DispatchMessage(&msg);   
      }
    }
    Sleep(1);
  }
}


/**************************************************************************
 Update the connected users list at pregame state.
**************************************************************************/
void update_conn_list_dialog(void)
{
  /* PORTME */
}

/**************************************************************************

**************************************************************************/
void
sound_bell(void)
{
	/* PORTME */
}

/**************************************************************************

**************************************************************************/
void
add_net_input(int sock)
{
  net_input=sock;
}
/**************************************************************************

**************************************************************************/

void
remove_net_input(void)
{
  net_input=-1;
}

/**************************************************************************

**************************************************************************/
void
set_unit_icon(int idx, struct unit *punit)
{
	/* PORTME */
}

/**************************************************************************

**************************************************************************/
void
set_unit_icons_more_arrow(bool onoff)
{
	/* PORTME */
}
