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

#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "netintf.h"
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
#include "inputdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "optiondlg.h"
#include "options.h"
#include "spaceshipdlg.h"
#include "resources.h"
#include "tilespec.h"


#include "gui_main.h"


static HWND connect_dlg;
static HWND tab_childs[2];
static HWND server_listview;
static HWND tab_ctrl;
static int autoconnect_timer_id;
struct t_server_button {
  HWND button;
  char *button_string;
  char *command;
};

enum new_game_dlg_ids {
  ID_NAME=100,
  ID_EASY,
  ID_MEDIUM,
  ID_HARD,
  ID_AIFILL,
  ID_STARTGAME,
  ID_OK=IDOK,
  ID_CANCEL=IDCANCEL
};

static struct t_server_button server_buttons[]={{NULL,"Start Game","start"},
						{NULL,"Save Game","save"},
						{NULL,"End Game","quit"},
						{NULL,"Get Score","score"}};
static HANDLE server_process=INVALID_HANDLE_VALUE;
static HANDLE stdin_pipe[2];
static HANDLE stdout_pipe[2];
static HANDLE stderr_pipe[2];
static HWND server_window;
static HWND server_output;
static HWND server_commandline;
static HWND main_menu;
static char saved_games_dirname[MAX_PATH+1]=".";

static void new_game_callback(HWND w,void * data);
/**************************************************************************
  Tests if the client has started the server.
**************************************************************************/
static int is_server_running()
{
  return (server_process!=INVALID_HANDLE_VALUE);
}

/**************************************************************************
  Kills the server if the client has started it (atexit handler)
**************************************************************************/
static void kill_server()
{
  if (is_server_running()) {
    TerminateProcess(server_process,0);
    CloseHandle(server_process);
    server_process=INVALID_HANDLE_VALUE;
  }
}

/**************************************************************************
 Finds the lowest port which can be used for the 
 server (starting at the default 5555)
**************************************************************************/
static int min_free_port()
{
  int port, n, s;
  struct sockaddr_in tmp;

  s=socket(AF_INET, SOCK_STREAM, 0);
  n=INADDR_ANY;
  port=5554; /* make looping convinient */
  do {
    port++;
    memset(&tmp,0,sizeof(struct sockaddr_in));
    tmp.sin_family=AF_INET;
    tmp.sin_port=htons(port);
    memcpy(&tmp.sin_addr, &n, sizeof(long));
  } while( bind(s,(struct sockaddr*) &tmp, sizeof(struct sockaddr_in)) );
  my_closesocket(s);
  return(port);
}


/*************************************************************************

*************************************************************************/
static void remove_server_control_buttons()
{
  fcwin_box_freeitem(output_box,1);
  fcwin_redo_layout(root_window);
}

/*************************************************************************

*************************************************************************/
static void add_server_control_buttons()
{
  int i;
  char buf[64];
  struct fcwin_box *vbox;
  vbox=fcwin_vbox_new(root_window,FALSE);
  my_snprintf(buf,sizeof(buf),_("Port: %d"),server_port);
  fcwin_box_add_static(vbox,buf,0,SS_LEFT,TRUE,TRUE,0);
  for (i=0;i<ARRAY_SIZE(server_buttons);i++) {
    server_buttons[i].button=
      fcwin_box_add_button(vbox,server_buttons[i].button_string,
			   ID_SERVERBUTTON,0,FALSE,FALSE,0);
  }
  fcwin_box_add_box(output_box,vbox,FALSE,FALSE,0);
  fcwin_redo_layout(root_window);
   
}
 
/*************************************************************************
  Exit without killing the server
*************************************************************************/
void quit_client_only()
{
  CloseHandle(server_process);
  server_process=INVALID_HANDLE_VALUE;
  exit(0);
}
 
/*************************************************************************
 
*************************************************************************/
void show_server_window()
{
  ShowWindow(server_window,SW_SHOWNORMAL);
}

/**************************************************************************

**************************************************************************/
static void connect_callback()
{
  char errbuf[512];
  char portbuf[10];
  Edit_GetText(GetDlgItem(tab_childs[0],ID_CONNECTDLG_NAME),player_name,512);
  Edit_GetText(GetDlgItem(tab_childs[0],ID_CONNECTDLG_HOST),server_host,512);
  Edit_GetText(GetDlgItem(tab_childs[0],ID_CONNECTDLG_PORT),portbuf,10);
  sscanf(portbuf, "%d", &server_port);
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
static void
gui_server_connect_real(void)
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
  printf("Timer\n");
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
    printf("T2\n");
    autoconnect_timer_id=SetTimer(root_window,3,AUTOCONNECT_INTERVAL,
				  autoconnect_timer);
  }

}

/**************************************************************************
  Sends a server command to the stdin pipe
**************************************************************************/
static void send_server_commandline(char *s)
{
  DWORD wlen;
  WriteFile(stdin_pipe[1],s,strlen(s),&wlen,NULL);
  WriteFile(stdin_pipe[1],"\n",1,&wlen,NULL);
}

/**************************************************************************

**************************************************************************/
static void append_server_output(char *buf)
{
  int len;
  len=Edit_GetTextLength(server_output);
  if (len>(32767-strlen(buf))) {
    Edit_SetSel(server_output,0,len);
  } else {
    Edit_SetSel(server_output,len,len);
  }
  Edit_ReplaceSel(server_output,buf);
  if (len==Edit_GetTextLength(server_output)) {
    SetWindowText(server_output,buf);   
  }
}

/**************************************************************************

**************************************************************************/
static void check_server_output(HANDLE pipe)
{
  char buf[1024];
  DWORD rlen;
  if (!PeekNamedPipe(pipe,NULL,0,NULL,&rlen,NULL))
    return;
  if (rlen<=0)
    return;
  if (ReadFile(pipe,buf,rlen,&rlen,NULL)) {
    buf[rlen]=0;
    append_server_output(buf);
  }
  
}

/**************************************************************************

**************************************************************************/
void handle_pipe_and_process(void)
{
  if (!server_window)
    return;
  check_server_output(stdout_pipe[0]);
  check_server_output(stderr_pipe[0]);
  if (is_server_running()&&(WaitForSingleObject(server_process,0)==WAIT_OBJECT_0)) {
    CloseHandle(server_process);
    server_process=INVALID_HANDLE_VALUE;
    remove_server_control_buttons();
  }
}

/**************************************************************************

**************************************************************************/
static void save_game()
{
  OPENFILENAME ofn;
  char dirname[MAX_PATH+1];
  char savecmd[MAX_PATH+10];
  char szfile[MAX_PATH]="\0";
  strcpy(szfile,"");
  ofn.lStructSize=sizeof(OPENFILENAME);
  ofn.hwndOwner=root_window;
  ofn.hInstance=freecivhinst;
  ofn.lpstrFilter=NULL;
  ofn.lpstrCustomFilter=NULL;
  ofn.nMaxCustFilter=0;
  ofn.nFilterIndex=1;
  ofn.lpstrFile=szfile;
  ofn.nMaxFile=sizeof(szfile);
  ofn.lpstrFileTitle=NULL;
  ofn.nMaxFileTitle=0;
  ofn.lpstrInitialDir=NULL;
  ofn.lpstrTitle="Save Game";
  ofn.nFileOffset=0;
  ofn.nFileExtension=0;
  ofn.lpstrDefExt=NULL;
  ofn.lCustData=0;
  ofn.lpfnHook=NULL;
  ofn.lpTemplateName=NULL;
  ofn.Flags=OFN_EXPLORER;
  GetCurrentDirectory(MAX_PATH,dirname);
  SetCurrentDirectory(saved_games_dirname);
  if (GetSaveFileName(&ofn)) {
    GetCurrentDirectory(MAX_PATH,saved_games_dirname);
    my_snprintf(savecmd,sizeof(savecmd),
		"save %s",ofn.lpstrFile);
    send_server_commandline(savecmd);
  }
  SetCurrentDirectory(dirname);

}

/**************************************************************************

**************************************************************************/
void handle_server_buttons(HWND button)
{
  int i;
  for(i=0;i<ARRAY_SIZE(server_buttons);i++) {
    if (server_buttons[i].button==button) {
      if (strcmp(server_buttons[i].command,"save")==0) {
	save_game();
      } else {
	send_server_commandline(server_buttons[i].command);
      }
      break;
    }
  } 
}

/**************************************************************************

**************************************************************************/
static LONG CALLBACK server_window_proc(HWND dlg,UINT message,
					WPARAM wParam,LPARAM lParam)
{
  int i;
  HWND controlwnd;
  switch(message) {
  case WM_CLOSE:
    ShowWindow(dlg,SW_HIDE);
    break;
  case WM_DESTROY:
  case WM_SIZE:
  case WM_GETMINMAXINFO:
    break;
  case WM_COMMAND:
    controlwnd=(HWND)lParam;
    if (controlwnd==server_commandline) {
      char *crpos;
      char buf[512];
      GetWindowText(controlwnd,buf,sizeof(buf));
      if (strchr(buf,'\n')) {
	if ((crpos=strchr(buf,'\r')))
	  crpos[0]=0;
	if ((crpos=strchr(buf,'\n')))
	  crpos[0]=0;
	send_server_commandline(buf);
	SetWindowText(controlwnd,"");
      }
    } else {
      for(i=0;i<ARRAY_SIZE(server_buttons);i++) {
	if (server_buttons[i].button==controlwnd) {
	  send_server_commandline(server_buttons[i].command);
	  break;
	}
      }
    }
    
    break;
  default:
    return DefWindowProc(dlg,message,wParam,lParam);
  }
  return 0;
}


/**************************************************************************

**************************************************************************/
static void output_minsize(POINT *minsize, void *data)
{
  minsize->x=500;
  minsize->y=100;
}

/**************************************************************************

**************************************************************************/
static void output_setsize(RECT *rc, void *data)
{
  MoveWindow((HWND)data,rc->left,rc->top,rc->right-rc->left,
	     rc->bottom-rc->top,TRUE);
}

/**************************************************************************

**************************************************************************/
static void output_del(void *data)
{
  DestroyWindow((HWND)data);
}

/**************************************************************************
 Creates the server window (which shows stdout and stderr of the server)
**************************************************************************/
static void create_server_window()
{
  SECURITY_ATTRIBUTES sa;
  struct fcwin_box *vbox;
  memset(&sa,0,sizeof(sa));
  sa.nLength=sizeof(sa);
  sa.bInheritHandle=TRUE;
  
  if (!CreatePipe(&stdout_pipe[0],&stdout_pipe[1],&sa,1024)) {
    freelog(LOG_FATAL,_("Cannot create pipe"));
    exit(1);
  }
  if (!CreatePipe(&stderr_pipe[0],&stderr_pipe[1],&sa,1024)) {
    freelog(LOG_FATAL,_("Cannot create pipe"));
    exit(1);
  }
  if (!CreatePipe(&stdin_pipe[0],&stdin_pipe[1],&sa,1024)) {
    freelog(LOG_FATAL,_("Cannot create pipe"));
    exit(1);
  }
  server_window=fcwin_create_layouted_window(server_window_proc,
					     _("Game Control"),
					     WS_OVERLAPPEDWINDOW,
					     CW_USEDEFAULT,CW_USEDEFAULT,
					     root_window,NULL,NULL);
  vbox=fcwin_vbox_new(server_window,FALSE);
  server_output=CreateWindowEx(WS_EX_CLIENTEDGE,
			       "EDIT",
			       "",
			       WS_CHILD | ES_READONLY | WS_VISIBLE | 
			       WS_VSCROLL | ES_LEFT | ES_WANTRETURN |
			       ES_MULTILINE | ES_AUTOVSCROLL,
			       0, 0, 0, 0,
			       server_window,
			       NULL,
			       freecivhinst,
			       NULL);      
  fcwin_box_add_generic(vbox,output_minsize,output_setsize,output_del,
			server_output,
			TRUE,TRUE,5);
  fcwin_set_box(server_window,vbox);
  atexit(kill_server);
}

/**************************************************************************

**************************************************************************/
char getc_from_pipe()
{
  char tmp[2];
  DWORD rlen; 
  tmp[1]='\0';
  while(WaitForSingleObject(server_process,0)!=WAIT_OBJECT_0) {
    rlen=1;
    tmp[0]=' ';;
    if (ReadFile(stdout_pipe[0],tmp,rlen,&rlen,NULL)&&(rlen==1)) {
      append_server_output(tmp);
      return tmp[0];
    } else {
      Sleep(100);
    }
  }
  return '\0';  
}


/**************************************************************************

**************************************************************************/
void get_line_from_pipe(char *buf, int bufsize) {
  int i;
  i=0;
  do {
    buf[i]=getc_from_pipe();
    i++;
  } while((i<bufsize)&&(buf[i-1]!='\n')&&(buf[i-1]!=0));
  i--;
  buf[i]=0;
  if ((i>0)&&(buf[i-1]=='\r'))
    buf[i-1]=0;
}

/**************************************************************************

**************************************************************************/
bool wait_for_prompt()
{
  char tmp;
  do {
    tmp=getc_from_pipe();
  } while((tmp!='\0')&&(tmp!='>'));
  if (tmp=='>')
    return TRUE;
  append_output_window("Server exited unexpected");
  return FALSE;
}


/*****************************************************************************

*****************************************************************************/
static void start_server(char *cmdline)
{
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  if (!server_window)
    create_server_window();
  ZeroMemory(&si,sizeof(si));
  si.cb=sizeof(si);
  si.hStdOutput=stdout_pipe[1];
  si.hStdInput=stdin_pipe[0];
  si.hStdError=stderr_pipe[1];
  si.dwFlags=STARTF_USESTDHANDLES;
  
  if (!CreateProcess(NULL,cmdline,NULL,NULL,TRUE,
		     DETACHED_PROCESS|NORMAL_PRIORITY_CLASS,
		     NULL,NULL,&si,&pi))
    {
      MessageBox(NULL,"Cannot start the server","Freeciv",MB_OK|MB_ICONEXCLAMATION);
      exit(1);
    }
  CloseHandle(pi.hThread);
  server_process=pi.hProcess;
  show_server_window();
  if (!wait_for_prompt()) {
    gui_server_connect();
    return;
  }
  send_server_commandline("cmdlevel ctrl first");
  if (!wait_for_prompt())
    gui_server_connect();
}



/**************************************************************************
 Starts the server without arguments.
**************************************************************************/
void start_server_for_new_game()
{
  char cmdline[512];
  server_port=min_free_port();
  my_snprintf(cmdline,sizeof(cmdline),"civserver -p %d",server_port);
  start_server(cmdline);   
}

/*************************************************************************

*************************************************************************/
static LONG CALLBACK playername_proc(HWND win,UINT message,
				     WPARAM wParam,LPARAM lParam)
{
  switch(message) 
    {
    case WM_CREATE:
    case WM_CLOSE:
    case WM_DESTROY:
    case WM_GETMINMAXINFO:
    case WM_SIZE:
      break;
    case WM_COMMAND:
      if (LOWORD(wParam)==IDOK) {
	char buf[512];
	int i;
	char *name;
	GetWindowText((HWND)lParam,buf,sizeof(buf));
	name=buf;
	for(i=strlen(name);(i>0)&&(name[i]!='(');i--);
	name[i-1]=0;
	strcpy(player_name,name);
	server_autoconnect();
	add_server_control_buttons();
	if (Button_GetCheck(GetDlgItem(win,IDYES))==BST_CHECKED) {
	  struct packet_generic_message apacket;
	  mystrlcpy(apacket.message,"/start", MAX_LEN_MSG-MAX_LEN_USERNAME+1);
	  send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);
	}
	DestroyWindow(win);
      }
      break;
    default:
      return DefWindowProc(win,message,wParam,lParam);
    }
  return 0;
}

/*************************************************************************

*************************************************************************/
static void get_player_name()
{
  char buf[512];
  char tmp;
  struct fcwin_box *vbox;
  HWND win;
  send_server_commandline("list");
  do {
    tmp=getc_from_pipe();
  } while((tmp!='-')&&(tmp!='\0'));
  win=fcwin_create_layouted_window(playername_proc,_("Select connection"),
				   WS_OVERLAPPEDWINDOW,
				   0,0,root_window,
				   NULL,NULL);
  vbox=fcwin_vbox_new(win,FALSE);
  fcwin_box_add_static(vbox,_("Choose player to be"),0,SS_LEFT,TRUE,FALSE,5);
  fcwin_box_add_checkbox(vbox,_("Start game automatically?"),IDYES,0,TRUE,FALSE,5);
  get_line_from_pipe(buf,sizeof(buf));
  get_line_from_pipe(buf,sizeof(buf));
  while((buf[0]!=0)&&(buf[0]!='-')) { 
    if (strchr(buf,'(')==NULL) { /* No players, thats a scenarion file */
      fcwin_set_box(win,vbox);
      DestroyWindow(win);
      wait_for_prompt();
      new_game_callback(NULL,NULL);
      return;
    }
    fcwin_box_add_button(vbox,buf,IDOK,0,TRUE,FALSE,5);
    get_line_from_pipe(buf,sizeof(buf));
  }
  fcwin_set_box(win,vbox);
  ShowWindow(win,SW_SHOWNORMAL);
}

/*************************************************************************
 Starts the server with -f option to load a game
*************************************************************************/
static void start_server_load_game(char *filename)
{
  char cmdline[512];
  server_port=min_free_port();
  my_snprintf(cmdline,sizeof(cmdline),"civserver -p %d -f \"%s\"",
	      server_port,filename);
  start_server(cmdline);
  if (is_server_running())
    get_player_name();
}

/**************************************************************************

**************************************************************************/
static void load_game_callback(HWND w,void * data)
{
  char dirname[MAX_PATH+1];
  OPENFILENAME ofn;
  char filename[MAX_PATH+1];
  filename[0]='\0';
  destroy_message_dialog(w);
  ZeroMemory(&ofn,sizeof(ofn));
  ofn.lStructSize=sizeof(OPENFILENAME);
  ofn.hwndOwner=root_window;
  ofn.hInstance=(HINSTANCE)GetWindowLong(root_window,GWL_HINSTANCE);
  ofn.lpstrTitle="Load Game"; 
  ofn.lpstrFile=filename;
  ofn.nMaxFile=sizeof(filename);
  ofn.Flags=OFN_EXPLORER;
  GetCurrentDirectory(MAX_PATH,dirname);
  if (data!=NULL) {
    SetCurrentDirectory((char *)data);
  } else {
    SetCurrentDirectory(saved_games_dirname);
  }
  if (GetOpenFileName(&ofn)) {
    GetCurrentDirectory(MAX_PATH,saved_games_dirname);
    SetCurrentDirectory(dirname);
    start_server_load_game(ofn.lpstrFile);
  } else {
    SetCurrentDirectory(dirname);
    gui_server_connect();
  }
}

/**************************************************************************

**************************************************************************/
static void scroll_minsize(POINT *rcmin,void *data)
{
  rcmin->y=15;
  rcmin->x=100;
}

/**************************************************************************

**************************************************************************/
static void scroll_setsize(RECT *rc,void *data)
{
  MoveWindow((HWND)data,rc->left,rc->top,
             rc->right-rc->left,
             rc->bottom-rc->top,TRUE);
}

/**************************************************************************

**************************************************************************/
static void scroll_del(void *data)
{
  DestroyWindow((HWND)data);
}

/**************************************************************************

**************************************************************************/
static void handle_hscroll(HWND hWnd,HWND hWndCtl,UINT code,int pos) 
{
  int PosCur,PosMax,PosMin;
  char buf[10];
  PosCur=ScrollBar_GetPos(hWndCtl);
  ScrollBar_GetRange(hWndCtl,&PosMin,&PosMax);
  switch(code)
    {
    case SB_LINELEFT: PosCur--; break;
    case SB_LINERIGHT: PosCur++; break;
    case SB_PAGELEFT: PosCur-=(PosMax-PosMin+1)/10; break;
    case SB_PAGERIGHT: PosCur+=(PosMax-PosMin+1)/10; break;
    case SB_LEFT: PosCur=PosMin; break;
    case SB_RIGHT: PosCur=PosMax; break;
    case SB_THUMBTRACK: PosCur=pos; break; 
    default:
      return;
    }
  if (PosCur<PosMin) PosCur=PosMin;
  if (PosCur>PosMax) PosCur=PosMax;
  ScrollBar_SetPos(hWndCtl,PosCur,TRUE);
   
  my_snprintf(buf,sizeof(buf),"%d",PosCur);
  SetWindowText(GetNextSibling(hWndCtl),buf);
}

/**************************************************************************

**************************************************************************/
static void set_new_game_params(HWND win)
{
  int aifill;
  struct packet_generic_message apacket;
  if (!is_server_running())
    start_server_for_new_game();
  GetWindowText(GetDlgItem(win,ID_NAME),player_name,512);
  server_autoconnect();
  add_server_control_buttons();
  if (IsDlgButtonChecked(win,ID_EASY)) {
    mystrlcpy(apacket.message, "/easy", MAX_LEN_MSG-MAX_LEN_USERNAME+1);
  } else if (IsDlgButtonChecked(win,ID_MEDIUM)) {
    mystrlcpy(apacket.message, "/normal", MAX_LEN_MSG-MAX_LEN_USERNAME+1);
  } else {
    mystrlcpy(apacket.message, "/hard", MAX_LEN_MSG-MAX_LEN_USERNAME+1);   
  }
  send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);
 
  mystrlcpy(apacket.message, "/set autotoggle 1",
	    MAX_LEN_MSG-MAX_LEN_USERNAME+1);
  send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);
  
  aifill=ScrollBar_GetPos(GetDlgItem(win,ID_AIFILL));
  my_snprintf(apacket.message, MAX_LEN_MSG-MAX_LEN_USERNAME+1,
	      "/set aifill %d", aifill);
  send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);
   
  if (Button_GetCheck(GetDlgItem(win,ID_STARTGAME))==BST_CHECKED) {
    struct packet_generic_message apacket;
    mystrlcpy(apacket.message,"/start", MAX_LEN_MSG-MAX_LEN_USERNAME+1);
    send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);
  }
}

/**************************************************************************

**************************************************************************/
static CALLBACK LONG new_game_proc(HWND win, UINT message,
				   WPARAM wParam, LPARAM lParam)
{
  switch(message)
    {
    case WM_CREATE:
    case WM_DESTROY:
    case WM_SIZE:
    case WM_GETMINMAXINFO:
      break;
    case WM_CLOSE:
      DestroyWindow(win);
      gui_server_connect();
      break;
    case WM_HSCROLL:
      HANDLE_WM_HSCROLL(win,wParam,lParam,handle_hscroll);
      break;
    case WM_COMMAND:
      switch((enum new_game_dlg_ids)LOWORD(wParam))
	{
	case ID_CANCEL:
	  DestroyWindow(win);
	  gui_server_connect();
	  break;
	case ID_OK:
	  set_new_game_params(win);
	  DestroyWindow(win);
	  break;
	default:
	  break;
	}
      break;
    default:
      return DefWindowProc(win,message,wParam,lParam);
    }
  return 0;
}

/**************************************************************************

**************************************************************************/
static void new_game_callback(HWND w,void * data)
{
 
  HWND win;
  HWND scroll;
  struct fcwin_box *hbox;
  struct fcwin_box *vbox;
  if (w!=NULL)
    destroy_message_dialog(w);
  win=fcwin_create_layouted_window(new_game_proc,_("Start New Game"),
				   WS_OVERLAPPEDWINDOW,
				   10,10,
				   root_window,NULL,NULL);
  vbox=fcwin_vbox_new(win,FALSE);
  hbox=fcwin_hbox_new(win,FALSE);
  fcwin_box_add_static(hbox,_("Your name:"),0,SS_LEFT,FALSE,FALSE,5);
  fcwin_box_add_edit(hbox,player_name,30,ID_NAME,0,TRUE,TRUE,5);
  fcwin_box_add_box(vbox,hbox,FALSE,FALSE,5);
  hbox=fcwin_hbox_new(win,FALSE);
  fcwin_box_add_static(hbox,_("Difficulty:"),0,SS_LEFT,FALSE,FALSE,5);
  fcwin_box_add_radiobutton(hbox,_("easy"),ID_EASY,WS_GROUP,TRUE,TRUE,5);
  fcwin_box_add_radiobutton(hbox,_("medium"),ID_MEDIUM,0,TRUE,TRUE,5);
  fcwin_box_add_radiobutton(hbox,_("hard"),ID_HARD,0,TRUE,TRUE,5);
  fcwin_box_add_box(vbox,hbox,FALSE,FALSE,5);
  hbox=fcwin_hbox_new(win,FALSE);
  fcwin_box_add_static(hbox,_("Total players (fill with AIs)"),0,
		       WS_GROUP|SS_LEFT,FALSE,FALSE,5);
  CheckRadioButton(win,ID_EASY,ID_HARD,ID_EASY);
  scroll=CreateWindow("SCROLLBAR",NULL,
		      WS_CHILD | WS_VISIBLE | SBS_HORZ,
		      0,0,0,0,
		      win,
		      (HMENU)ID_AIFILL,
		      freecivhinst,NULL);
  fcwin_box_add_generic(hbox,scroll_minsize,scroll_setsize,scroll_del,
			scroll,TRUE,TRUE,15);
  ScrollBar_SetRange(scroll,1,30,TRUE);
  ScrollBar_SetPos(scroll,5,TRUE);
  fcwin_box_add_static(hbox,"  5",0,SS_RIGHT,FALSE,FALSE,15);
  fcwin_box_add_box(vbox,hbox,FALSE,FALSE,5);
  fcwin_box_add_checkbox(vbox,_("Start game automatically?"),
			 ID_STARTGAME,0,FALSE,FALSE,5);
  Button_SetCheck(GetDlgItem(win,ID_STARTGAME),BST_CHECKED);
  hbox=fcwin_hbox_new(win,TRUE);
  fcwin_box_add_button(hbox,_("OK"),ID_OK,0,TRUE,TRUE,5);
  fcwin_box_add_button(hbox,_("Cancel"),ID_CANCEL,0,TRUE,TRUE,5);
  fcwin_box_add_box(vbox,hbox,TRUE,FALSE,5);
  fcwin_set_box(win,vbox);
  ShowWindow(win,SW_SHOWNORMAL);
}


/**************************************************************************

**************************************************************************/
static void quit_game_callback(HWND w,void *data)
{
  exit(0);
}

/**************************************************************************
...
**************************************************************************/
static void join_game_callback(HWND w,void *data)
{ 
  if (w)
    destroy_message_dialog(w);
  gui_server_connect_real();
}

/**************************************************************************

**************************************************************************/
void gui_server_connect()
{
  kill_server();
  remove_server_control_buttons();
  if (server_window)
    ShowWindow(server_window,SW_HIDE);
  main_menu=popup_message_dialog(root_window,
                                 _("Start a game"),
                                 _("What do you wish to to?"),
                                 _("New Game"),new_game_callback,NULL,
                                 _("Load Game"),load_game_callback,NULL,
                                 _("Load Scenario"),load_game_callback,
				 "data/scenario",
				 _("Join Game"),join_game_callback,NULL,
                                 _("Quit Game"),quit_game_callback,NULL,
                                 NULL);
}

