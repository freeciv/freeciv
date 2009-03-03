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
#include <time.h>

#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "mem.h"
#include "packets.h"
#include "support.h"

#include "client_main.h"
#include "climisc.h"
#include "gui_main.h"
#include "gui_stuff.h"

#include "chatline.h"
#include "pages.h"

struct genlist *history_list;
int history_pos;

/**************************************************************************
  Helper function to determine if a given client input line is intended as
  a "plain" public message. Note that messages prefixed with : are a
  special case (explicit public messages), and will return FALSE.
**************************************************************************/
static bool is_plain_public_message(const char *s)
{
  /* FIXME: These prefix definitions are duplicated in the server. */
  const char ALLIES_CHAT_PREFIX = '.';
  const char SERVER_COMMAND_PREFIX = '/';
  const char MESSAGE_PREFIX = ':';

  const char *p;

  /* If it is a server command or an explicit ally
   * message, then it is not a public message. */
  if (s[0] == SERVER_COMMAND_PREFIX || s[0] == ALLIES_CHAT_PREFIX) {
    return FALSE;
  }

  /* It might be a private message of the form
   *   'player name with spaces':the message
   * or with ". So skip past the player name part. */
  if (s[0] == '\'' || s[0] == '"') {
    p = strchr(s + 1, s[0]);
  } else {
    p = s;
  }

  /* Now we just need to check that it is not a private
   * message. If we encounter a space then the preceeding
   * text could not have been a user/player name (the
   * quote check above eliminated names with spaces) so
   * it must be a public message. Otherwise if we encounter
   * the message prefix : then the text parsed up until now
   * was a player/user name and the line is intended as
   * a private message (or explicit public message if the
   * first character is :). */
  while (p != NULL && *p != '\0') {
    if (my_isspace(*p)) {
      return TRUE;
    } else if (*p == MESSAGE_PREFIX) {
      return FALSE;
    }
    p++;
  }
  return TRUE;
}


/**************************************************************************
...
**************************************************************************/
void inputline_return(GtkEntry *w, gpointer data)
{
  const char *theinput;

  theinput = gtk_entry_get_text(w);
  
  if (*theinput) {
    if (client_state() == C_S_RUNNING && allied_chat_only
        && is_plain_public_message(theinput)) {
      char buf[MAX_LEN_MSG];
      my_snprintf(buf, sizeof(buf), ". %s", theinput);
      send_chat(buf);
    } else {
      send_chat(theinput);
    }

    if (genlist_size(history_list) >= MAX_CHATLINE_HISTORY) {
      void *data;

      data = genlist_get(history_list, -1);
      genlist_unlink(history_list, data);
      free(data);
    }

    genlist_prepend(history_list, mystrdup(theinput));
    history_pos=-1;
  }

  gtk_entry_set_text(w, "");
}

/**************************************************************************
  Scroll a textview so that the given mark is visible, but only if the
  scroll window containing the textview is very close to the bottom. The
  text mark 'scroll_target' should probably be the first character of the
  last line in the text buffer.
**************************************************************************/
static void scroll_if_necessary(GtkTextView *textview,
                                GtkTextMark *scroll_target)
{
  GtkWidget *sw;
  GtkAdjustment *vadj;
  gdouble val, max, upper, page_size;

  g_return_if_fail(textview != NULL);
  g_return_if_fail(scroll_target != NULL);

  sw = gtk_widget_get_parent(GTK_WIDGET(textview));
  g_return_if_fail(sw != NULL);
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(sw));

  vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
  val = gtk_adjustment_get_value(GTK_ADJUSTMENT(vadj));
  g_object_get(G_OBJECT(vadj), "upper", &upper,
               "page-size", &page_size, NULL);
  max = upper - page_size;
  if (max - val < 10.0) {
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(textview), scroll_target,
                                 0.0, TRUE, 1.0, 0.0);
  }
}

/**************************************************************************
  Appends the string to the chat output window.  The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void real_append_output_window(const char *astring, int conn_id)
{
  GtkTextBuffer *buf;
  GtkTextIter iter;
  GtkTextMark *mark;

  buf = message_buffer;
  gtk_text_buffer_get_end_iter(buf, &iter);
  gtk_text_buffer_insert(buf, &iter, "\n", -1);
  mark = gtk_text_buffer_create_mark(buf, NULL, &iter, TRUE);

  if (show_chat_message_time) {
    char timebuf[64];
    time_t now;
    struct tm *now_tm;

    now = time(NULL);
    now_tm = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "[%H:%M:%S] ", now_tm);
    gtk_text_buffer_insert(buf, &iter, timebuf, -1);
  }

  gtk_text_buffer_insert(buf, &iter, astring, -1);

  if (main_message_area) {
    scroll_if_necessary(GTK_TEXT_VIEW(main_message_area), mark);
  }
  if (start_message_area) {
    scroll_if_necessary(GTK_TEXT_VIEW(start_message_area), mark);
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

/**************************************************************************
  Scrolls the pregame and in-game chat windows all the way to the bottom.
**************************************************************************/
void chatline_scroll_to_bottom(void)
{
  GtkTextIter end;

  if (!message_buffer) {
    return;
  }
  gtk_text_buffer_get_end_iter(message_buffer, &end);

  if (main_message_area) {
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(main_message_area),
                                 &end, 0.0, TRUE, 1.0, 0.0);
  }
  if (start_message_area) {
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(start_message_area),
                                 &end, 0.0, TRUE, 1.0, 0.0);
  }
}
