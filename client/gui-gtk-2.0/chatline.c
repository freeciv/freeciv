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

#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "mem.h"
#include "packets.h"
#include "support.h"

#include "climisc.h"
#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"

#include "chatline.h"
#include "pages.h"

struct genlist	history_list;
int		history_pos;


/**************************************************************************
...
**************************************************************************/
void inputline_return(GtkEntry *w, gpointer data)
{
  const char *theinput;

  theinput = gtk_entry_get_text(w);
  
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

  gtk_entry_set_text(w, "");
}

/**************************************************************************
  Appends the string to the chat output window.  The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void real_append_output_window(const char *astring, int conn_id)
{
  GtkWidget *sw;
  GtkAdjustment *slider;
  bool scroll;

  GtkTextBuffer *buf;
  GtkTextIter i;
  GtkTextMark *mark;


  buf = message_buffer;
  gtk_text_buffer_get_end_iter(buf, &i);
  gtk_text_buffer_insert(buf, &i, "\n", -1);
  gtk_text_buffer_insert(buf, &i, astring, -1);

  /* have to use a mark, or this won't work properly */
  gtk_text_buffer_get_end_iter(buf, &i);
  mark = gtk_text_buffer_create_mark(buf, NULL, &i, FALSE);


  sw = gtk_widget_get_parent(GTK_WIDGET(main_message_area));
  slider = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));

  /* scroll forward only if slider is near the bottom */
  scroll = ((slider->value + slider->page_size) >=
      (slider->upper - slider->step_increment));
  if (scroll) {
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(main_message_area),
	mark);
  }

  sw = gtk_widget_get_parent(GTK_WIDGET(start_message_area));
  slider = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));

  /* scroll forward only if slider is near the bottom */
  scroll = ((slider->value + slider->page_size) >=
      (slider->upper - slider->step_increment));
  if (scroll) {
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(start_message_area),
	mark);
  }


  gtk_text_buffer_delete_mark(buf, mark);


  append_network_statusbar(astring, FALSE);
}

/**************************************************************************
 I have no idea what module this belongs in -- Syela
 I've decided to put output_window routines in chatline.c, because
 the are somewhat related and append_output_window is already here.  --dwp
**************************************************************************/
void log_output_window(void)
{
  GtkTextIter start, end;
  gchar *txt;

  gtk_text_buffer_get_bounds(message_buffer, &start, &end);
  txt = gtk_text_buffer_get_text(message_buffer, &start, &end, TRUE);

  write_chatline_content(txt);
  g_free(txt);
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
  gtk_text_buffer_set_text(message_buffer, text, -1);
}
