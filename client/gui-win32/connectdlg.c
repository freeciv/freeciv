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
#include "resources.h"
#include "tilespec.h"

#include <stdio.h>

#include "gui_main.h"

#include <windowsx.h>
/* in civclient.c; FIXME hardcoded sizes */

extern HWND root_window;
extern HINSTANCE freecivhinst;
HWND connect_dlg;
static int autoconnect_timer_id;

/**************************************************************************

**************************************************************************/
static void connect_callback()
{
  char errbuf[512];
  char portbuf[10];
  Edit_GetText(GetDlgItem(connect_dlg,ID_CONNECTDLG_NAME),player_name,512);
  Edit_GetText(GetDlgItem(connect_dlg,ID_CONNECTDLG_HOST),server_host,512);
  Edit_GetText(GetDlgItem(connect_dlg,ID_CONNECTDLG_PORT),portbuf,10);
  server_port=atoi(portbuf);
  if (connect_to_server(player_name,server_host,server_port,
			errbuf,sizeof(errbuf))!=-1)
    {
      DestroyWindow(connect_dlg);
    }
  else
    {
      printf("xx\n");
      append_output_window(errbuf);
    }
}

/**************************************************************************

**************************************************************************/
static LONG CALLBACK connectdlg_proc(HWND hWnd,
				     UINT message,
				     WPARAM wParam,
				     LPARAM lParam)  
{
  switch(message)
    {
    case WM_CREATE:
      break;
    case WM_CLOSE:
      PostQuitMessage(0);
      break;
    case WM_DESTROY:
      break;
    case WM_COMMAND:
      switch (LOWORD(wParam))
	{
	case ID_CONNECTDLG_QUIT:
	  PostQuitMessage(0);
	  break;
	case ID_CONNECTDLG_CONNECT:
	  connect_callback();
	  break;
	}
    case WM_SIZE:
      break;
    case WM_GETMINMAXINFO:
      break;
    default:
      return DefWindowProc(hWnd,message,wParam,lParam); 
    }
  return FALSE;  
}

/**************************************************************************

**************************************************************************/
void
gui_server_connect(void)
{
  char buf[20];
  struct fcwin_box *hbox;
  struct fcwin_box *vbox;
  connect_dlg=fcwin_create_layouted_window(connectdlg_proc,
					   _("Connect to Freeciv Server"),
					   WS_OVERLAPPEDWINDOW,
					   CW_USEDEFAULT,CW_USEDEFAULT,
					   root_window,NULL,NULL);
  hbox=fcwin_hbox_new(connect_dlg,FALSE);
  vbox=fcwin_vbox_new(connect_dlg,FALSE);
  fcwin_box_add_static(vbox,_("Name:"),0,SS_CENTER,
		       TRUE,TRUE,5);
  fcwin_box_add_static(vbox,_("Host:"),0,SS_CENTER,
		       TRUE,TRUE,5);
  fcwin_box_add_static(vbox,_("Port:"),0,SS_CENTER,
		       TRUE,TRUE,5);
  fcwin_box_add_box(hbox,vbox,FALSE,FALSE,5);

  vbox=fcwin_vbox_new(connect_dlg,FALSE);
  fcwin_box_add_edit(vbox,player_name,40,ID_CONNECTDLG_NAME,0,
		     TRUE,TRUE,10);
  fcwin_box_add_edit(vbox,server_host,40,ID_CONNECTDLG_HOST,0,
		     TRUE,TRUE,10);
  my_snprintf(buf, sizeof(buf), "%d", server_port);
  fcwin_box_add_edit(vbox,buf,8,ID_CONNECTDLG_PORT,0,TRUE,TRUE,15);
  fcwin_box_add_box(hbox,vbox,TRUE,TRUE,5);
  vbox=fcwin_vbox_new(connect_dlg,FALSE);
  fcwin_box_add_box(vbox,hbox,TRUE,FALSE,5);
  hbox=fcwin_hbox_new(connect_dlg,TRUE);
  fcwin_box_add_button(hbox,_("Connect"),ID_CONNECTDLG_CONNECT,
		       0,TRUE,TRUE,5);
  fcwin_box_add_button(hbox,_("Quit"),ID_CONNECTDLG_QUIT,
		       0,TRUE,TRUE,5);
  fcwin_box_add_box(vbox,hbox,FALSE,FALSE,5);
  fcwin_set_box(connect_dlg,vbox);
  ShowWindow(connect_dlg,SW_SHOWNORMAL);
}

/**************************************************************************
  Make an attempt to autoconnect to the server.
  (server_autoconnect() gets GTK to call this function every so often.)
**************************************************************************/
static int try_to_autoconnect()
{
  char errbuf[512];
  static int count = 0;

  count++;

  if (count >= MAX_AUTOCONNECT_ATTEMPTS) {
    freelog(LOG_FATAL,
            _("Failed to contact server \"%s\" at port "
              "%d as \"%s\" after %d attempts"),
            server_host, server_port, player_name, count);
    exit(1);
  }

  switch (try_to_connect(player_name, errbuf, sizeof(errbuf))) {
  case 0:                       /* Success! */
    return FALSE;               /* Do not call this
                                   function again */
#if 0
  case ECONNREFUSED:            /* Server not available (yet) */
    return TRUE;                /* Keep calling this function */
#endif
  default:                      /* All other errors are fatal */
    freelog(LOG_FATAL,
            _("Error contacting server \"%s\" at port %d "
              "as \"%s\":\n %s\n"),
            server_host, server_port, player_name, errbuf);
    exit(1);     
  }
}

/**************************************************************************

**************************************************************************/
static void CALLBACK autoconnect_timer(HWND  hwnd,UINT uMsg,
				       UINT idEvent,DWORD  dwTime)  
{
  if (!try_to_autoconnect())
    KillTimer(NULL,autoconnect_timer_id);
}

/**************************************************************************
  Start trying to autoconnect to civserver.  Calls
  get_server_address(), then arranges for try_to_autoconnect(), which
  calls try_to_connect(), to be called roughly every
  AUTOCONNECT_INTERVAL milliseconds, until success, fatal error or
  user intervention.  
**************************************************************************/
void server_autoconnect()
{
  char buf[512];

  my_snprintf(buf, sizeof(buf),
              _("Auto-connecting to server \"%s\" at port %d "
                "as \"%s\" every %d.%d second(s) for %d times"),
              server_host, server_port, player_name,
              AUTOCONNECT_INTERVAL / 1000,AUTOCONNECT_INTERVAL % 1000, 
              MAX_AUTOCONNECT_ATTEMPTS);
  append_output_window(buf);
  if (get_server_address(server_host, server_port, buf, sizeof(buf)) < 0) {
    freelog(LOG_FATAL,
            _("Error contacting server \"%s\" at port %d "
              "as \"%s\":\n %s\n"),
            server_host, server_port, player_name, buf);
    exit(1);
  }
  printf("server_autoconnect\n");
  if (try_to_autoconnect()) {
    autoconnect_timer_id=SetTimer(root_window,3,AUTOCONNECT_INTERVAL,
				  autoconnect_timer);
  }

}
