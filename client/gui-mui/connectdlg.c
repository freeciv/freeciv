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
#include <string.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"
#include "version.h"

#include "civclient.h"
#include "connectdlg.h"
#include "chatline.h"
#include "clinet.h"
#include "gui_main.h"
#include "muistuff.h"

extern char server_host[512];
extern char user_name[512];
extern int server_port;

static Object *connect_wnd;
static Object *connect_name_string;
static Object *connect_host_string;
static Object *connect_port_string;
static Object *connect_connect_button;
static Object *connect_quit_button;
static Object *connect_meta_listview;

/**************************************************************************
 Callback for the Connect Button
**************************************************************************/
static void connect_connect(void)
{
  char errbuf [512];

  sz_strlcpy(user_name, getstring(connect_name_string));
  sz_strlcpy(server_host, getstring(connect_host_string));
  server_port = xget(connect_port_string, MUIA_String_Integer);

  if(connect_to_server(user_name, server_host, server_port,
		       errbuf, sizeof(errbuf))!=-1)
  {
    set(connect_wnd,MUIA_Window_Open,FALSE);
  } else
  {
    append_output_window(errbuf);
  }
}

/**************************************************************************
 Callback if the user select the Meta Page
**************************************************************************/
static void connect_meta_page(void)
{
  char errbuf[512];
  struct server_list *slist;

  set(app,MUIA_Application_Sleep,TRUE);

  if((slist = create_server_list(errbuf, sizeof(errbuf))))
  {
    DoMethod(connect_meta_listview, MUIM_NList_Clear);
    server_list_iterate(*slist,pserver)
      DoMethod(connect_meta_listview,MUIM_NList_InsertSingle, pserver, MUIV_NList_Insert_Bottom);
    server_list_iterate_end

    delete_server_list(slist);
  } else
  {
    append_output_window(errbuf);
  }

  set(app,MUIA_Application_Sleep,FALSE);
}

/**************************************************************************
 Callback if a new entry in the meta listview is selected
**************************************************************************/
static void connect_meta_active(void)
{
  struct server *pserver;
  DoMethod(connect_meta_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pserver);
  if(pserver)
  {
    setstring(connect_host_string,pserver->name);
    setstring(connect_port_string,pserver->port);
 } 
}

/**************************************************************************
 Constructor of a new entry in the meta listview
**************************************************************************/
HOOKPROTONHNO(connect_meta_construct, struct server *, struct server *entry)
{
  struct server *newentry = (struct server*)AllocVec(sizeof(*newentry),0);
  if(newentry)
  {
    newentry->name = mystrdup(entry->name);
    newentry->port = mystrdup(entry->port);
    newentry->version = mystrdup(entry->version);
    newentry->status = mystrdup(entry->status);
    newentry->players = mystrdup(entry->players);
    newentry->metastring = mystrdup(entry->metastring);
  }
  return newentry;
}

/**************************************************************************
 Destructor of a entry in the meta listview
**************************************************************************/
HOOKPROTONHNO(connect_meta_destruct, void, struct server *entry)
{
  free(entry->name);
  free(entry->port);
  free(entry->version);
  free(entry->status);
  free(entry->players);
  free(entry->metastring);
  FreeVec(entry);
}

/**************************************************************************
 Display function for the meta listview
**************************************************************************/
HOOKPROTONH(connect_meta_display, void, char **array, struct server *entry)
{
  if(entry)
  {
    *array++ = entry->name;
    *array++ = entry->port;
    *array++ = entry->version;
    *array++ = entry->status;
    *array++ = entry->players;
    *array = entry->metastring;
  } else
  {
    *array++ = _("Server Name");
    *array++ = _("Port");
    *array++ = _("Version");
    *array++ = _("Status");
    *array++ = _("Players");
    *array = _("Comment");
  }
}

/**************************************************************************
 Display the connect window
**************************************************************************/
void gui_server_connect(void)
{
  Object *page_group;
  static STRPTR pages[3];
  
  pages[0] = _("Server Selection");
  pages[1] = _("Metaserver");
  pages[2] = 0;

  if(!connect_wnd)
  {
    static struct Hook const_hook;
    static struct Hook dest_hook;
    static struct Hook display_hook;

    const_hook.h_Entry = (HOOKFUNC)connect_meta_construct;
    dest_hook.h_Entry = (HOOKFUNC)connect_meta_destruct;
    display_hook.h_Entry = (HOOKFUNC)connect_meta_display;

    connect_wnd = WindowObject,
	MUIA_Window_Title, _("Connect to Freeciv Server"),
	MUIA_Window_ID, MAKE_ID('C','O','N','N'),

	WindowContents, VGroup,
	    Child, page_group =RegisterGroup(pages),
		MUIA_CycleChain,1,
		MUIA_Register_Frame,TRUE,

		Child, ColGroup(2),
		    Child, MakeLabel(_("_Name")),
		    Child, connect_name_string = MakeString(_("_Name"),64),

		    Child, MakeLabel(_("_Host")),
		    Child, connect_host_string = MakeString(_("_Host"),64),

		    Child, MakeLabel(_("_Port")),
		    Child, connect_port_string = MakeInteger(_("_Port")),
										End,

		    Child, VGroup,
		    Child, connect_meta_listview = NListviewObject,
			MUIA_NListview_NList,NListObject,
			    MUIA_NList_Format,",,,,,",
			    MUIA_NList_Title,TRUE,
			    MUIA_NList_ConstructHook, &const_hook,
			    MUIA_NList_DestructHook, &dest_hook,
			    MUIA_NList_DisplayHook, &display_hook,
			    End,
			End,
		    End,
		End,

	    Child, HGroup,
		Child, connect_connect_button = MakeButton(_("_Connect")),
		Child, connect_quit_button = MakeButton(_("_Quit")),
		End,
	    End,
	End;

    if(connect_wnd)
    {
      DoMethod(connect_quit_button, MUIM_Notify, MUIA_Pressed, FALSE, app,2,MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
      DoMethod(connect_connect_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, connect_connect);
      DoMethod(page_group, MUIM_Notify, MUIA_Group_ActivePage, 1,app, 3, MUIM_CallHook, &civstandard_hook, connect_meta_page);
      DoMethod(connect_meta_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, app, 3, MUIM_CallHook, &civstandard_hook, connect_meta_active);

      DoMethod(app, OM_ADDMEMBER, connect_wnd);
    }
  }

  if(connect_wnd)
  {
    set(connect_name_string, MUIA_String_Contents, user_name);
    set(connect_host_string, MUIA_String_Contents, server_host);
    set(connect_port_string, MUIA_String_Integer, server_port);

    set(connect_wnd, MUIA_Window_Open, TRUE);
  }
}

/**************************************************************************
  Make an attempt to autoconnect to the server.  If the server isn't
  there yet, arrange for this routine to be called again in about
  AUTOCONNECT_INTERVAL milliseconds.  If anything else goes wrong, log
  a fatal error.
**************************************************************************/
static int try_to_autoconnect(void)
{
// Implement me
  return 0;
#if 0
  char errbuf[512];
  static int count = 0;

  count++;

  /* abort if after 10 seconds the server couldn't be reached */

  if (AUTOCONNECT_INTERVAL * count >= 10000) {
    freelog(LOG_FATAL,
	    _("Failed to contact server \"%s\" at port "
	      "%d as \"%s\" after %d attempts"),
	    server_host, server_port, connect_name, count);
    
  }

  switch (try_to_connect(connect_name, errbuf, sizeof(errbuf))) {
  case 0:			/* Success! */
    return;
  case ECONNREFUSED:		/* Server not available (yet) - wait & retry */
     /*PORTME*/ schedule_timer
	(AUTOCONNECT_INTERVAL, try_to_autoconnect, NULL);
  default:			/* All other errors are fatal */
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, connect_name, errbuf);
     /*PORTME*/ exit_application(error code);
  }
#endif
}

/**************************************************************************
  Start trying to autoconnect to civserver.
  Calls get_server_address(), then arranges for
  autoconnect_callback(), which calls try_to_connect(), to be called
  roughly every AUTOCONNECT_INTERVAL milliseconds, until success,
  fatal error or user intervention.
**************************************************************************/
void server_autoconnect()
{
  char buf[512];
  int outcome;

  my_snprintf(buf, sizeof(buf),
	      _("Auto-connecting to server \"%s\" at port %d "
		"as \"%s\" every %d.%d second(s) for %d times"),
	      server_host, server_port, user_name,
	      AUTOCONNECT_INTERVAL / 1000,AUTOCONNECT_INTERVAL % 1000, 
	      MAX_AUTOCONNECT_ATTEMPTS);
  append_output_window(buf);

  outcome = get_server_address(server_host, server_port, buf, sizeof(buf));
  if (outcome < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, user_name, buf);
    exit(EXIT_FAILURE);
  }
  try_to_autoconnect();
}

