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

#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"

#include "chatline.h"

/**************************************************************************
...
**************************************************************************/
void inputline_return(GtkWidget *w, gpointer data)
{
  struct packet_generic_message apacket;
  char *theinput, *buffer;
  GList *cache;
  int *newindex;

  theinput = gtk_entry_get_text(GTK_ENTRY(w));
  /* get the cache */
  cache = gtk_object_get_data(GTK_OBJECT(w), "cache");
  
  if (GTK_WIDGET_IS_SENSITIVE(top_vbox) && *theinput) {
    mystrlcpy(apacket.message, theinput, MAX_LEN_MSG-MAX_LEN_USERNAME+1);
    send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);

    /* add to the cache */
    buffer = fc_malloc(strlen(theinput) + 1);
    newindex = gtk_object_get_data(GTK_OBJECT(w), "cache_current");
    strcpy(buffer, theinput);
    cache = g_list_prepend(cache, buffer);
    /* don't save more than 20 messages */
    if (g_list_length(cache) > 20) {
      char *data = g_list_last(cache)->data;
      cache = g_list_remove(cache, g_list_last(cache)->data);
      if (data)
	free(data);
    }
    /* save the list again (in case the pointer changed) */
    *newindex = -1;
    gtk_object_set_data(GTK_OBJECT(w), "cache", cache);
  }

  gtk_entry_set_text(GTK_ENTRY(w), "");
}

/**************************************************************************
...
**************************************************************************/
void append_output_window(char *astring)
{
  gtk_text_freeze(GTK_TEXT(main_message_area));
  gtk_text_insert(GTK_TEXT(main_message_area), NULL, NULL, NULL, "\n", -1);
  gtk_text_insert(GTK_TEXT(main_message_area), NULL, NULL, NULL, astring, -1);
  gtk_text_thaw(GTK_TEXT(main_message_area));

  /* move the scrollbar forward by a ridiculous amount */
  gtk_range_default_vmotion(GTK_RANGE(text_scrollbar), 0, 10000);
}

/**************************************************************************
 I have no idea what module this belongs in -- Syela
 I've decided to put output_window routines in chatline.c, because
 the are somewhat related and append_output_window is already here.  --dwp
**************************************************************************/
void log_output_window(void)
{ 
  char *theoutput;
  FILE *fp;
  
  append_output_window(_("Exporting output window to civgame.log ..."));
  theoutput = gtk_editable_get_chars(GTK_EDITABLE(main_message_area), 0, -1);
  fp = fopen("civgame.log", "w"); /* should allow choice of name? */
  fprintf(fp, "%s", theoutput);
  fclose(fp);
  append_output_window(_("Export complete."));
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
