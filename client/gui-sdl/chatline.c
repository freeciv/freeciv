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

/**********************************************************************
                          chatline.c  -  description
                             -------------------
    begin                : Sun Jun 30 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "fcintl.h"

#include "gui_mem.h"

#include "packets.h"
#include "support.h"

#include "climisc.h"
#include "clinet.h"

#include "colors.h"
#include "graphics.h"
#include "unistring.h"
#include "gui_string.h"
#include "gui_id.h"
#include "gui_stuff.h"
#include "gui_zoom.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "messagewin_common.h"
#include "chatline.h"

#define PTSIZE_LOG_FONT 10

static struct GUI *pInput_Edit = NULL;
/**************************************************************************
  ...
**************************************************************************/
static int inputline_return_callback(struct GUI *pWidget)
{
  struct packet_generic_message apacket;
  char *theinput = NULL;

  if (!pWidget->string16->text) {
    return -1;
  }

  theinput = convert_to_chars(pWidget->string16->text);

  if (*theinput) {
    mystrlcpy(apacket.message, theinput,
	      MAX_LEN_MSG - MAX_LEN_USERNAME + 1);
    send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);

    real_append_output_window(theinput);
    FREE(theinput);
  }

  pWidget->string16->text = NULL;

  return -1;
}


/**************************************************************************
  ...
**************************************************************************/
void Init_Input_Edit(void)
{
  int w = 400;
  int h = 30;
  
  pInput_Edit = create_edit_from_unichars(NULL, NULL, 18, w, 0);
  pInput_Edit->size.x = (Main.screen->w - w) / 2;

  if (h > pInput_Edit->size.h) {
    pInput_Edit->size.h = h;
  }

  pInput_Edit->size.y = Main.screen->h - 2 * pInput_Edit->size.h;
  
  pInput_Edit->action = inputline_return_callback;

  add_to_gui_list(ID_CHATLINE_INPUT_EDIT, pInput_Edit);
}

 /* ======================================================= */

void popup_input_line(void)
{
  SDL_Rect dst = { pInput_Edit->size.x, pInput_Edit->size.y, 0, 0};
  
  refresh_widget_background(pInput_Edit, Main.gui);
  
  edit(pInput_Edit , Main.gui);
  
  SDL_BlitSurface(pInput_Edit->gfx, NULL, Main.gui, &dst);
  
  flush_rect(pInput_Edit->size);
    

  if (pInput_Edit->action) {
    pInput_Edit->action(pInput_Edit);
  }
}

void new_input_line_position(void)
{
  pInput_Edit->size.x = (Main.screen->w - pInput_Edit->size.w) / 2;
  pInput_Edit->size.y = Main.screen->h - 2 * pInput_Edit->size.h;
}

/**************************************************************************
  Appends the string to the chat output window.  The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void real_append_output_window(const char *astring)
{
  struct packet_generic_message packet;
  
  my_snprintf(packet.message , MAX_LEN_MSG, "%s" , astring);
  packet.x = -1;
  packet.y = -1;
  /*packet.event;*/
  
  add_notify_window(&packet);
  
}

/**************************************************************************
  Get the text of the output window, and call write_chatline_content() to
  log it.
**************************************************************************/
void log_output_window(void)
{
  /* TODO */
}

/**************************************************************************
  Clear all text from the output window.
**************************************************************************/
void clear_output_window(void)
{
  /* TODO */
}
