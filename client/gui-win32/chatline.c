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
#include "tilespec.h"

#include <stdio.h>

#include "gui_main.h"
#include <windows.h>
#include <windowsx.h>


/**************************************************************************

**************************************************************************/
void handle_chatline()
{
  struct packet_generic_message apacket;
  GetWindowText(hchatline,apacket.message,MAX_LEN_MSG-MAX_LEN_USERNAME+1);
  if (strchr(apacket.message,'\n')) {
    char *crpos;
    if ((crpos=strchr(apacket.message,'\r')))
      crpos[0]=0;
    if ((crpos=strchr(apacket.message,'\n')))
      crpos[0]=0;
  
    send_packet_generic_message(&aconnection, PACKET_CHAT_MSG,&apacket);
    SetWindowText(hchatline,"");
  }
}

/**************************************************************************

**************************************************************************/
void append_output_window_real(char *astring)
{
  int len;
  len=Edit_GetTextLength(logoutput_win);
  Edit_SetSel(logoutput_win,len,len);
  Edit_ReplaceSel(logoutput_win,astring);
  Edit_ScrollCaret(logoutput_win);
}

/**************************************************************************

**************************************************************************/
void real_append_output_window(const char *astring) 
     /* We need to add \r to lineends */ 
{

  char *str;
  char *str2;
  char buf[512];
  str=astring;
  while((str2=strchr(str,'\n')))
    {
      strncpy(buf,str,str2-str);
      buf[str2-str]=0;
      strcat(buf,"\r\n");
      append_output_window_real(buf);
      str=str2+1;
    }
  append_output_window_real(str);
  append_output_window_real("\r\n");
}

/**************************************************************************
...
**************************************************************************/
void
log_output_window(void)
{
  int len;
  char *theoutput;
 
  len=GetWindowTextLength(logoutput_win)+1;
  theoutput=fc_malloc(len);
  GetWindowText(logoutput_win,theoutput,len);
  write_chatline_content(theoutput);
  free(theoutput);
}

/**************************************************************************
...
**************************************************************************/
void
clear_output_window(void)
{
  SetWindowText(logoutput_win,"");
  append_output_window(_("Cleared output window."));
}
