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

#include <stdio.h>
#include <string.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

#include "mem.h"
#include "version.h"

#include "connectdlg.h"
#include "chatline.h"
#include "clinet.h"

/* in civclient.c; FIXME hardcoded sizes */
extern char name[];
extern char server_host[];
extern int  server_port;

/* MUI Imports */
#include "muistuff.h"

IMPORT Object *app;

Object *connect_wnd;
Object *connect_name_string;
Object *connect_host_string;
Object *connect_port_string;
Object *connect_connect_button;
Object *connect_quit_button;
Object *connect_meta_listview;

/**************************************************************************
 Callback for the Connect Button
**************************************************************************/
static void connect_connect(void)
{
  char errbuf [512];

	strcpy(name, getstring(connect_name_string));
	strcpy(server_host, getstring(connect_host_string));
	server_port = xget(connect_port_string, MUIA_String_Integer);
  
  if(connect_to_server(name, server_host, server_port,
		       errbuf, sizeof(errbuf))!=-1)
  {
  	set(connect_wnd,MUIA_Window_Open,FALSE);
	}
  else
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
	}	else
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
__asm __saveds static struct server *connect_meta_construct( register __a2 APTR pool, register __a1 struct server *entry)
{
	struct server *newentry = (struct server*)AllocVec(sizeof(*newentry),0);
	if(newentry)
	{
		newentry->name = strdup(entry->name);
		newentry->port = strdup(entry->port);
		newentry->version = strdup(entry->version);
		newentry->status = strdup(entry->status);
		newentry->players = strdup(entry->players);
		newentry->metastring = strdup(entry->metastring);
	}
	return newentry;
}

/**************************************************************************
 Destructor of a entry in the meta listview
**************************************************************************/
__asm __saveds static void connect_meta_destruct( register __a2 APTR pool, register __a1 struct server *entry)
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
__asm __saveds static void connect_meta_display(register __a2 char **array, register __a1 struct server *entry)
{
	if(entry)
	{
		*array++ = entry->name;
		*array++ = entry->port;
		*array++ = entry->version;
		*array++ = entry->status;
		*array++ = entry->players;
		*array++ = entry->metastring;
	}	else
	{
		*array++ = "Server Name";
		*array++ = "Port";
		*array++ = "Version";
		*array++ = "Status";
		*array++ = "Players";
		*array = "Comment";
	}
}

/**************************************************************************
 Display the connect window
**************************************************************************/
void gui_server_connect(void)
{
	Object *page_group;
	STATIC STRPTR pages[] = {"Server Selection", "Metaserver",NULL};

	if(!connect_wnd)
	{
		STATIC struct Hook const_hook;
		STATIC struct Hook dest_hook;
		STATIC struct Hook display_hook;

		const_hook.h_Entry = (HOOKFUNC)connect_meta_construct;
		dest_hook.h_Entry = (HOOKFUNC)connect_meta_destruct;
		display_hook.h_Entry = (HOOKFUNC)connect_meta_display;

		connect_wnd = WindowObject,
				MUIA_Window_Title, "Connect to Freeciv Server",
				MUIA_Window_ID, 'CONN',

				WindowContents, VGroup,
						Child, page_group =RegisterGroup(pages),
								MUIA_CycleChain,1,
								MUIA_Register_Frame,TRUE,

								Child, ColGroup(2),
										Child, MakeLabel("_Name"),
										Child, connect_name_string = MakeString("_Name",64),

										Child, MakeLabel("_Host"),
										Child, connect_host_string = MakeString("_Host",64),

										Child, MakeLabel("_Port"),
										Child, connect_port_string = MakeInteger("_Port"),
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
								Child, connect_connect_button = MakeButton("_Connect"),
								Child, connect_quit_button = MakeButton("_Quit"),
								End,
						End,
				End;

		if(connect_wnd)
		{
			DoMethod(connect_quit_button, MUIM_Notify, MUIA_Pressed, FALSE, app,2,MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
			DoMethod(connect_connect_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &standart_hook, connect_connect);
			DoMethod(page_group, MUIM_Notify, MUIA_Group_ActivePage, 1,app, 3, MUIM_CallHook, &standart_hook, connect_meta_page);
			DoMethod(connect_meta_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, app, 3, MUIM_CallHook, &standart_hook, connect_meta_active);

			DoMethod(app, OM_ADDMEMBER, connect_wnd);
		}
	}

	if(connect_wnd)
	{
		set(connect_name_string, MUIA_String_Contents, name);
		set(connect_host_string, MUIA_String_Contents, server_host);
		set(connect_port_string, MUIA_String_Integer, server_port);

		set(connect_wnd, MUIA_Window_Open, TRUE);
	}
}
