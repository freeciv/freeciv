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
  
/**************************************************************************
  Sent msg/command from imput dlg to server
**************************************************************************/
static int inputline_return_callback(struct GUI *pWidget)
{
  char *theinput = NULL;

  if (!pWidget->string16->text) {
    return -1;
  }

  theinput = convert_to_chars(pWidget->string16->text);

  if (*theinput) {
    send_chat(theinput);

    real_append_output_window(theinput);
    FREE(theinput);
  }
  
  return -1;
}

/**************************************************************************
  This function is main chat/command client input.
**************************************************************************/
void popup_input_line(void)
{
  int w = 400;
  int h = 30;
  struct GUI *pInput_Edit;
    
  pInput_Edit = create_edit_from_unichars(NULL, NULL, NULL, 0, 18, w, 0);
  lock_buffer(pInput_Edit->dst);/* always on top */
  
  pInput_Edit->size.x = (Main.screen->w - w) / 2;
  
  if (h > pInput_Edit->size.h) {
    pInput_Edit->size.h = h;
  }

  pInput_Edit->size.y = Main.screen->h - 2 * pInput_Edit->size.h;
  
  
  if(edit(pInput_Edit)) {
    inputline_return_callback(pInput_Edit);
  }
  
  sdl_dirty_rect(pInput_Edit->size);
  FREEWIDGET(pInput_Edit);
  remove_locked_buffer();
  
  flush_dirty();
}

/**************************************************************************
  Appends the string to the chat output window.
  Curretn it is wraper to message subsystem.
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
