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
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

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


#include "gui_main.h"

/* in civclient.c; FIXME hardcoded sizes */


extern HWND root_window;
extern HINSTANCE freecivhinst;
static HWND connect_dlg;
static HWND tab_childs[2];
static HWND server_listview;
static HWND tab_ctrl;
static int autoconnect_timer_id;


/**************************************************************************

**************************************************************************/
static void connect_callback()
{
  char errbuf[512];
  char portbuf[10];
  Edit_GetText(GetDlgItem(tab_childs[0],ID_CONNECTDLG_NAME),player_name,512);
  Edit_GetText(GetDlgItem(tab_childs[0],ID_CONNECTDLG_HOST),server_host,512);
  Edit_GetText(GetDlgItem(tab_childs[0],ID_CONNECTDLG_PORT),portbuf,10);
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
  LPNMHDR nmhdr;
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
      break;
    case WM_NOTIFY:
      nmhdr=(LPNMHDR)lParam;
      if (nmhdr->hwndFrom==tab_ctrl) {
	if (TabCtrl_GetCurSel(tab_ctrl)) {
	  ShowWindow(tab_childs[0],SW_HIDE);
	  ShowWindow(tab_childs[1],SW_SHOWNORMAL);
	} else {
	  ShowWindow(tab_childs[0],SW_SHOWNORMAL);
	  ShowWindow(tab_childs[1],SW_HIDE);
	}
      }
      break;
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

 *************************************************************************/
static int get_meta_list(HWND list,char *errbuf,int n_errbuf)
{
  int i;
  char *row[6];
  char  buf[6][64];
  struct server_list *server_list = create_server_list(errbuf, n_errbuf);
  if(!server_list) return -1;
  ListView_DeleteAllItems(list);
  for (i=0; i<6; i++)
    row[i]=buf[i];
  server_list_iterate(*server_list,pserver) {
    sz_strlcpy(buf[0], pserver->name);
    sz_strlcpy(buf[1], pserver->port);
    sz_strlcpy(buf[2], pserver->version);
    sz_strlcpy(buf[3], _(pserver->status));
    sz_strlcpy(buf[4], pserver->players);
    sz_strlcpy(buf[5], pserver->metastring);
    fcwin_listview_add_row(list,0,6,row);
    
  }
  server_list_iterate_end;
  
  delete_server_list(server_list);
  return 0;
}

/**************************************************************************

 *************************************************************************/
static void handle_row_click()
{
  int i,n;
  n=ListView_GetItemCount(server_listview);
  for(i=0;i<n;i++) {
    if (ListView_GetItemState(server_listview,i,LVIS_SELECTED)) {
      char portbuf[10];
      LV_ITEM lvi;
      lvi.iItem=i;
      lvi.iSubItem=0;
      lvi.mask=LVIF_TEXT;
      lvi.cchTextMax=512;
      lvi.pszText=server_host;
      ListView_GetItem(server_listview,&lvi);
      lvi.iItem=i;
      lvi.iSubItem=1;
      lvi.mask=LVIF_TEXT;
      lvi.cchTextMax=sizeof(portbuf);
      lvi.pszText=portbuf;
      ListView_GetItem(server_listview,&lvi);
      SetWindowText(GetDlgItem(tab_childs[0],ID_CONNECTDLG_HOST),server_host);
      SetWindowText(GetDlgItem(tab_childs[0],ID_CONNECTDLG_PORT),portbuf);
    }
  }
}

/**************************************************************************

 *************************************************************************/
static LONG CALLBACK tabs_page_proc(HWND dlg,UINT message,WPARAM wParam,LPARAM lParam)
{
  NM_LISTVIEW *nmlv;
  switch(message)
    {
    case WM_CREATE:
      break;
    case WM_COMMAND:
      if (LOWORD(wParam)==IDOK) {
	char errbuf[128];
	if (get_meta_list(server_listview,errbuf,sizeof(errbuf))==-1) {
	  append_output_window(errbuf);
	}
      }
      break;
    case WM_NOTIFY:
      nmlv=(NM_LISTVIEW *)lParam;
      if (nmlv->hdr.hwndFrom==server_listview) {
	handle_row_click();
	if (nmlv->hdr.code==NM_DBLCLK)
	  connect_callback();
      }
      break;
    default:
      return DefWindowProc(dlg,message,wParam,lParam);
    }
  return 0;
}

/**************************************************************************

**************************************************************************/
void
gui_server_connect(void)
{
  int i;
  char buf[20];
  char *titles_[2]= {N_("Freeciv Server Selection"),N_("Metaserver")};
  char *server_list_titles_[6]={N_("Server Name"), N_("Port"), N_("Version"),
				N_("Status"), N_("Players"), N_("Comment")};
  char *titles[2];
  WNDPROC wndprocs[2]={tabs_page_proc,tabs_page_proc};
  void *user_data[2]={NULL,NULL};
  struct fcwin_box *hbox;
  struct fcwin_box *vbox;
  struct fcwin_box *main_vbox;
  
  titles[0]=_(titles_[0]);
  titles[1]=_(titles_[1]);
  connect_dlg=fcwin_create_layouted_window(connectdlg_proc,
					   _("Connect to Freeciv Server"),
					   WS_OVERLAPPEDWINDOW,
					   CW_USEDEFAULT,CW_USEDEFAULT,
					   root_window,NULL,NULL);
  main_vbox=fcwin_vbox_new(connect_dlg,FALSE);
  tab_ctrl=fcwin_box_add_tab(main_vbox,wndprocs,tab_childs,
			     titles,user_data,2,
			     0,0,TRUE,TRUE,5);
  hbox=fcwin_hbox_new(tab_childs[0],FALSE);
  vbox=fcwin_vbox_new(tab_childs[0],FALSE);
  fcwin_box_add_static(vbox,_("Name:"),0,SS_CENTER,
		       TRUE,TRUE,5);
  fcwin_box_add_static(vbox,_("Host:"),0,SS_CENTER,
		       TRUE,TRUE,5);
  fcwin_box_add_static(vbox,_("Port:"),0,SS_CENTER,
		       TRUE,TRUE,5);
  fcwin_box_add_box(hbox,vbox,FALSE,FALSE,5);
  vbox=fcwin_vbox_new(tab_childs[0],FALSE);
  fcwin_box_add_edit(vbox,player_name,40,ID_CONNECTDLG_NAME,0,
		     TRUE,TRUE,10);
  fcwin_box_add_edit(vbox,server_host,40,ID_CONNECTDLG_HOST,0,
		     TRUE,TRUE,10);
  my_snprintf(buf, sizeof(buf), "%d", server_port);
  fcwin_box_add_edit(vbox,buf,8,ID_CONNECTDLG_PORT,0,TRUE,TRUE,15);
  fcwin_box_add_box(hbox,vbox,TRUE,TRUE,5);
  vbox=fcwin_vbox_new(tab_childs[0],FALSE);
  fcwin_box_add_box(vbox,hbox,TRUE,FALSE,0);
  fcwin_set_box(tab_childs[0],vbox);
  vbox=fcwin_vbox_new(tab_childs[1],FALSE);
  server_listview=fcwin_box_add_listview(vbox,5,0,LVS_REPORT | LVS_SINGLESEL,
					 TRUE,TRUE,5);
  fcwin_box_add_button(vbox,_("Update"),IDOK,0,FALSE,FALSE,5);
  fcwin_set_box(tab_childs[1],vbox);
  
  hbox=fcwin_hbox_new(connect_dlg,TRUE);
  fcwin_box_add_button(hbox,_("Connect"),ID_CONNECTDLG_CONNECT,
		       0,TRUE,TRUE,5);
  fcwin_box_add_button(hbox,_("Quit"),ID_CONNECTDLG_QUIT,
		       0,TRUE,TRUE,5);
  fcwin_box_add_box(main_vbox,hbox,FALSE,FALSE,5);
  for(i=0;i<ARRAY_SIZE(server_list_titles_);i++) {
    LV_COLUMN lvc;
    lvc.pszText=_(server_list_titles_[i]);
    lvc.mask=LVCF_TEXT;
    ListView_InsertColumn(server_listview,i,&lvc);
  }
  fcwin_set_box(connect_dlg,main_vbox);
  for(i=0;i<ARRAY_SIZE(server_list_titles_);i++) {
    ListView_SetColumnWidth(server_listview,i,LVSCW_AUTOSIZE_USEHEADER);
  }
  fcwin_redo_layout(connect_dlg);
  ShowWindow(tab_childs[0],SW_SHOWNORMAL);
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
    exit(EXIT_FAILURE);
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
    exit(EXIT_FAILURE);     
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
    exit(EXIT_FAILURE);
  }
  printf("server_autoconnect\n");
  if (try_to_autoconnect()) {
    autoconnect_timer_id=SetTimer(root_window,3,AUTOCONNECT_INTERVAL,
				  autoconnect_timer);
  }

}
