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

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
 
#include "chatline.h"
#include "citydlg.h"
#include "clinet.h"
#include "colors.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "options.h"       

#include "messagewin.h"

#define ID_MESSAGEWIN_GOTO 500
#define ID_MESSAGEWIN_POPUP 501
#define ID_MESSAGEWIN_LIST 502

extern HWND root_window;
extern HINSTANCE freecivhinst;
static HWND meswin_dlg;
static struct fcwin_box *meswin_box;
static int max_list_width;
static int messages_total = 0; /* current total number of message lines */
static int messages_alloc = 0; /* number allocated for */
static char **string_ptrs = NULL;
static int *xpos = NULL;
static int *ypos = NULL;
static int *event = NULL;

/**************************************************************************

**************************************************************************/
LONG APIENTRY MsgdlgProc(HWND hWnd,
			 UINT message,
			 UINT wParam,
			 LONG lParam)
{
  switch(message)
    {
    case WM_CREATE:
      return 0;
    case WM_DESTROY:
      meswin_box=NULL;
      meswin_dlg=NULL;
      break;
    case WM_CLOSE:
      DestroyWindow(hWnd);
      break;
    case WM_GETMINMAXINFO:
      break;
    case WM_SIZE:
      break;
  
    case WM_COMMAND:
      switch(LOWORD(wParam))
	{
	case IDCANCEL:
	  DestroyWindow(hWnd);
	  break;
	case ID_MESSAGEWIN_LIST:
	  {
	    int id;
	    id=ListBox_GetCurSel(GetDlgItem(hWnd,ID_MESSAGEWIN_LIST));
	    if (id!=LB_ERR)
	      {
		EnableWindow(GetDlgItem(hWnd,ID_MESSAGEWIN_GOTO),TRUE);
		EnableWindow(GetDlgItem(hWnd,ID_MESSAGEWIN_POPUP),TRUE);
	      }
	  }
	  break;
	case ID_MESSAGEWIN_GOTO:
	  {
	    int id,row;
	    id=ListBox_GetCurSel(GetDlgItem(hWnd,ID_MESSAGEWIN_LIST));
	    if (id!=LB_ERR)
	      {
		row=ListBox_GetItemData(GetDlgItem(hWnd,ID_MESSAGEWIN_LIST),
					id);
		if(xpos[row] != 0 || ypos[row]!=0)
		    center_tile_mapcanvas(xpos[row], ypos[row]);
		  
	      }
	  }
	  break;
	case ID_MESSAGEWIN_POPUP:
	  {
	    int id,row;
	    struct city *pcity;
	    int x, y;   
	    id=ListBox_GetCurSel(GetDlgItem(hWnd,ID_MESSAGEWIN_LIST));
	    if (id!=LB_ERR)
	      {
		row=ListBox_GetItemData(GetDlgItem(hWnd,ID_MESSAGEWIN_LIST),
					id);
		x = xpos[row];
		y = ypos[row];
		if((x || y) && (pcity=map_get_city(x,y))
		   && (pcity->owner == game.player_idx)) {
		  if (center_when_popup_city) {
		    center_tile_mapcanvas(x,y);
		  }
		  popup_city_dialog(pcity, 0);
		}                                   
	      }
	  }
	  break;
	}
    default:
      return DefWindowProc(hWnd,message,wParam,lParam);
    }
  return 0;
}

/**************************************************************************

**************************************************************************/
static void list_minsize(LPPOINT minsize,void *data)
{
  HWND hWnd;
  hWnd=(HWND)data;
  minsize->x=max_list_width;
  minsize->y=24*ListBox_GetItemHeight(hWnd,0);
}

/**************************************************************************

**************************************************************************/

static void list_setsize(LPRECT newsize,void *data)
{
  HWND hWnd;
  hWnd=(HWND)data;
  MoveWindow(hWnd,newsize->left,newsize->top,
	     newsize->right-newsize->left,
	     newsize->bottom-newsize->top,
	     TRUE);
}

/**************************************************************************

**************************************************************************/
static void list_del(void *data)
{
  HWND hWnd;
  hWnd=(HWND)data;
  DestroyWindow(hWnd);
}
/**************************************************************************

**************************************************************************/
static void create_meswin_dialog(void)
{
  HWND meswin_list;
  struct fcwin_box *hbox;
  meswin_dlg=fcwin_create_layouted_window(MsgdlgProc,_("Messages"),
					  WS_OVERLAPPEDWINDOW,
					  CW_USEDEFAULT,
					  CW_USEDEFAULT,
					  root_window,
					  NULL,
					  REAL_CHILD,
					  NULL);
  meswin_list=CreateWindow("LISTBOX",NULL,
			   WS_CHILD | WS_VISIBLE | 
			   LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL,
			   0,0,0,0,
			   meswin_dlg,
			   (HMENU)ID_MESSAGEWIN_LIST,
			   freecivhinst,
			   NULL);
  meswin_box=fcwin_vbox_new(meswin_dlg,FALSE);
  fcwin_box_add_generic(meswin_box,
			list_minsize,
			list_setsize,
			list_del,
			meswin_list,TRUE,TRUE,5);
  hbox=fcwin_hbox_new(meswin_dlg,TRUE);
  fcwin_box_add_button(hbox,_("Close"),IDCANCEL,0,TRUE,TRUE,5);
  fcwin_box_add_button(hbox,_("Goto location"),ID_MESSAGEWIN_GOTO,0,
		       TRUE,TRUE,5);
  fcwin_box_add_button(hbox,_("Popup City"),ID_MESSAGEWIN_POPUP,0,
		       TRUE,TRUE,5);
  fcwin_box_add_box(meswin_box,hbox,FALSE,FALSE,5);
  real_update_meswin_dialog();
  fcwin_set_box(meswin_dlg,meswin_box);
  ShowWindow(meswin_dlg,SW_SHOWNORMAL);
}

/**************************************************************************

**************************************************************************/
void popup_meswin_dialog(void)
{
  int updated = 0;
 
  if(!meswin_dlg) {
    create_meswin_dialog();
    updated = 1;               /* create_ calls update_ */
  }
  
}

/****************************************************************
...
*****************************************************************/
bool is_meswin_open(void)
{
  return meswin_dlg != 0;
}

/**************************************************************************
...
**************************************************************************/


/**************************************************************************
 This makes sure that the next two elements in string_ptrs etc are
 allocated for.  Two = one to be able to grow, and one for the sentinel
 in string_ptrs.
 Note update_meswin_dialog should always be called soon after this since
 it contains pointers to the memory we're reallocing here.
**************************************************************************/
static void meswin_allocate(void)
{
  int i;
  
  if (messages_total+2 > messages_alloc) {
    messages_alloc = messages_total + 32;
    string_ptrs = fc_realloc(string_ptrs, messages_alloc*sizeof(char*));
    xpos = fc_realloc(xpos, messages_alloc*sizeof(int));
    ypos = fc_realloc(ypos, messages_alloc*sizeof(int));
    event = fc_realloc(event, messages_alloc*sizeof(int));
    for( i=messages_total; i<messages_alloc; i++ ) {
      string_ptrs[i] = NULL;
      xpos[i] = 0;
      ypos[i] = 0;
      event[i] = E_NOEVENT;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void real_clear_notify_window(void)
{
  int i;
  meswin_allocate();
  for (i = 0; i <messages_total; i++) {
    free(string_ptrs[i]);
    string_ptrs[i] = NULL;
    xpos[i] = 0;
    ypos[i] = 0;
    event[i] = E_NOEVENT;
  }
  string_ptrs[0]=0;
  messages_total = 0;
  if(meswin_dlg) {
    EnableWindow(GetDlgItem(meswin_dlg,ID_MESSAGEWIN_GOTO),FALSE);
    EnableWindow(GetDlgItem(meswin_dlg,ID_MESSAGEWIN_POPUP),FALSE);
  }
}

/**************************************************************************
...
**************************************************************************/
void real_add_notify_window(struct packet_generic_message *packet)
{
  char *s;
  int nspc;
  char *game_prefix1 = "Game: ";
  char *game_prefix2 = _("Game: ");
  int gp_len1 = strlen(game_prefix1);
  int gp_len2 = strlen(game_prefix2);

  meswin_allocate();
  s = fc_malloc(strlen(packet->message) + 50);
  if (strncmp(packet->message, game_prefix1, gp_len1) == 0) {
    strcpy(s, packet->message + gp_len1);
  } else if(strncmp(packet->message, game_prefix2, gp_len2) == 0) {
    strcpy(s, packet->message + gp_len2);
  } else {
    strcpy(s, packet->message);
  }

  nspc=50-strlen(s);
  if(nspc>0)
    strncat(s, "                                                  ", nspc);
  
  xpos[messages_total] = packet->x;
  ypos[messages_total] = packet->y;
  event[messages_total]= packet->event;
  string_ptrs[messages_total] = s;
  messages_total++;
  string_ptrs[messages_total] = 0;
}


/**************************************************************************

**************************************************************************/
void real_update_meswin_dialog(void)
{
  RECT rc;
  int id;
  int i;
  HWND hLst;
  max_list_width = 0;
  hLst = GetDlgItem(meswin_dlg, ID_MESSAGEWIN_LIST);
  ListBox_ResetContent(hLst);
  for (i = 0; i < messages_total; i++) {
    id = ListBox_AddString(hLst, string_ptrs[i]);
    ListBox_SetItemData(hLst, id, i);
    ListBox_GetItemRect(hLst, id, &rc);
    max_list_width = MAX(max_list_width, rc.right - rc.left);
  }
  max_list_width += 20;
}
