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

#include "fcintl.h"
#include "mem.h"
#include "packets.h"
#include "support.h"

#include "climisc.h"
#include "clinet.h"

#include "gui_main.h"
#include "gui_stuff.h"

#include "chatline.h"

struct genlist	history_list;
int		history_pos;

/**************************************************************************
...
**************************************************************************/
void inputline_return(GtkWidget *w, gpointer data)
{
  char *theinput;

  theinput = gtk_entry_get_text(GTK_ENTRY(w));
  
  if (*theinput) {
    send_chat(theinput);

    if (genlist_size(&history_list) >= MAX_CHATLINE_HISTORY) {
      void *data;

      data=genlist_get(&history_list, -1);
      genlist_unlink(&history_list, data);
      free(data);
    }

    genlist_insert(&history_list, mystrdup(theinput), 0);
    history_pos=-1;
  }

  gtk_entry_set_text(GTK_ENTRY(w), "");
}

/**************************************************************************
  Appends the string to the chat output window.  The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void real_append_output_window(const char *astring, int conn_id)
{
  bool scroll;
  GtkAdjustment *slider =
    gtk_range_get_adjustment(GTK_RANGE(text_scrollbar));

  /* scroll forward only if slider is near the bottom */
  scroll = ((slider->value + slider->page_size) >=
	    (slider->upper - slider->step_increment));

  gtk_text_freeze(GTK_TEXT(main_message_area));
  gtk_text_insert(GTK_TEXT(main_message_area), NULL, NULL, NULL, "\n", -1);
  gtk_text_insert(GTK_TEXT(main_message_area), NULL, NULL, NULL, astring,
		  -1);
  gtk_text_thaw(GTK_TEXT(main_message_area));

  if (scroll) {
    gtk_range_default_vmotion(GTK_RANGE(text_scrollbar), 0, 10000);
  }
}

/**************************************************************************
 I have no idea what module this belongs in -- Syela
 I've decided to put output_window routines in chatline.c, because
 the are somewhat related and append_output_window is already here.  --dwp
**************************************************************************/
void log_output_window(void)
{
  write_chatline_content(gtk_editable_get_chars
			 (GTK_EDITABLE(main_message_area), 0, -1));
}
/**************************************************************************
...
**************************************************************************/
void clear_output_window(void)
{
  set_output_window_text(_("Cleared output window."));
}

/**************************************************************************
...
**************************************************************************/
void set_output_window_text(const char *text)
{
  gtk_text_freeze(GTK_TEXT(main_message_area));
  gtk_editable_delete_text(GTK_EDITABLE(main_message_area), 0, -1);
  gtk_text_insert(GTK_TEXT(main_message_area), NULL, NULL, NULL, text, -1);
  gtk_text_thaw(GTK_TEXT(main_message_area));
}
