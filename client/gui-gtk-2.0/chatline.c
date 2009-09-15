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

/* utility */
#include "fcintl.h"
#include "mem.h"
#include "support.h"

/* common */
#include "featured_text.h"
#include "game.h"
#include "packets.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "control.h"
#include "mapview_common.h"

/* gui-gtk-2.0 */
#include "colors.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "pages.h"

#include "chatline.h"

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
  Make a text tag for the selected text.
**************************************************************************/
void inputline_make_tag(GtkEntry *w, enum text_tag_type type)
{
  char buf[MAX_LEN_MSG];
  GtkEditable *editable = GTK_EDITABLE(w);
  gint start_pos, end_pos;
  gchar *selection;

  if (!gtk_editable_get_selection_bounds(editable, &start_pos, &end_pos)) {
    /* Let's say the selection starts and ends at the current position. */
    start_pos = end_pos = gtk_editable_get_position(editable);
  }

  selection = gtk_editable_get_chars(editable, start_pos, end_pos);
  if (0 != featured_text_apply_tag(selection, buf, sizeof(buf),
                                   type, 0, OFFSET_UNSET)) {
    /* Replace the selection. */
    gtk_editable_delete_text(editable, start_pos, end_pos);
    end_pos = start_pos;
    gtk_editable_insert_text(editable, buf, -1, &end_pos);
    gtk_editable_select_region(editable, start_pos, end_pos);
  }
  g_free(selection);
}

/**************************************************************************
  Make a chat link at the current position or make the current selection
  clickable.
**************************************************************************/
void inputline_make_chat_link(GtkEntry *w, struct tile *ptile, bool unit)
{
  char buf[MAX_LEN_MSG];
  GtkEditable *editable = GTK_EDITABLE(w);
  gint start_pos, end_pos;
  gchar *chars;
  struct unit *punit;

  /* Get the target. */
  if (unit) {
    punit = find_visible_unit(ptile);
    if (!punit) {
      append_output_window(_("No visible unit on this tile."));
      return;
    }
  } else {
    punit = NULL;
  }

  if (gtk_editable_get_selection_bounds(editable, &start_pos, &end_pos)) {
    /* There is a selection, make it clickable. */
    gpointer target;
    enum text_link_type type;

    chars = gtk_editable_get_chars(editable, start_pos, end_pos);
    if (punit) {
      type = TLT_UNIT;
      target = punit;
    } else if (tile_city(ptile)) {
      type = TLT_CITY;
      target = tile_city(ptile);
    } else {
      type = TLT_TILE;
      target = ptile;
    }

    if (0 != featured_text_apply_tag(chars, buf, sizeof(buf), TTT_LINK,
                                     0, OFFSET_UNSET, type, target)) {
      /* Replace the selection. */
      gtk_editable_delete_text(editable, start_pos, end_pos);
      end_pos = start_pos;
      gtk_editable_insert_text(editable, buf, -1, &end_pos);
      gtk_widget_grab_focus(GTK_WIDGET(w));
      gtk_editable_select_region(editable, start_pos, end_pos);
    }
  } else {
    /* Just insert the link at the current position. */
    start_pos = gtk_editable_get_position(editable);
    end_pos = start_pos;
    chars = gtk_editable_get_chars(editable, MAX(start_pos - 1, 0),
                                   start_pos + 1);
    if (punit) {
      sz_strlcpy(buf, unit_link(punit));
    } else if (tile_city(ptile)) {
      sz_strlcpy(buf, city_link(tile_city(ptile)));
    } else {
      sz_strlcpy(buf, tile_link(ptile));
    }

    if (start_pos > 0 && strlen(chars) > 0 && chars[0] != ' ') {
      /* Maybe insert an extra space. */
      gtk_editable_insert_text(editable, " ", 1, &end_pos);
    }
    gtk_editable_insert_text(editable, buf, -1, &end_pos);
    if (chars[start_pos > 0 ? 1 : 0] != '\0'
        && chars[start_pos > 0 ? 1 : 0] != ' ') {
      /* Maybe insert an extra space. */
      gtk_editable_insert_text(editable, " ", 1, &end_pos);
    }
    gtk_widget_grab_focus(GTK_WIDGET(w));
    gtk_editable_set_position(editable, end_pos);
  }

  g_free(chars);
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
  Click a link.
**************************************************************************/
static gboolean event_after(GtkWidget *text_view, GdkEventButton *event)
{
  GtkTextIter start, end, iter;
  GtkTextBuffer *buffer;
  GSList *tags, *tagp;
  gint x, y;
  struct tile *ptile = NULL;
  char buf[64];

  if (event->type != GDK_BUTTON_RELEASE || event->button != 1) {
    return FALSE;
  }

  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

  /* We shouldn't follow a link if the user has selected something. */
  gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
  if (gtk_text_iter_get_offset(&start) != gtk_text_iter_get_offset(&end)) {
    return FALSE;
  }

  gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW (text_view), 
                                        GTK_TEXT_WINDOW_WIDGET,
                                        event->x, event->y, &x, &y);

  gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(text_view), &iter, x, y);

  if ((tags = gtk_text_iter_get_tags(&iter))) {
    for (tagp = tags; tagp; tagp = tagp->next) {
      GtkTextTag *tag = tagp->data;
      enum text_link_type type =
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tag), "type"));

      if (type != 0) {
        /* This is a link. */
        int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tag), "id"));
        ptile = NULL;

        /* Real type is type - 1.
         * See comment in apply_text_tag() for g_object_set_data(). */
        type--;

        switch (type) {
        case TLT_CITY:
          {
            struct city *pcity = game_find_city_by_number(id);

            if (pcity) {
              ptile = city_tile(pcity);
            } else {
              my_snprintf(buf, sizeof(buf), _("This city isn't known!"));
              append_output_window(buf);
            }
          }
          break;
        case TLT_TILE:
          ptile = index_to_tile(id);

          if (!ptile) {
            my_snprintf(buf, sizeof(buf),
                        _("This tile doesn't exist in this game!"));
            append_output_window(buf);
          }
          break;
        case TLT_UNIT:
          {
            struct unit *punit = game_find_unit_by_number(id);

            if (punit) {
              ptile = unit_tile(punit);
            } else {
              my_snprintf(buf, sizeof(buf), _("This unit isn't known!"));
              append_output_window(buf);
            }
          }
          break;
        }

        if (ptile) {
          center_tile_mapcanvas(ptile);
          link_mark_restore(type, id);
          gtk_widget_grab_focus(GTK_WIDGET(map_canvas));
        }
      }
    }
    g_slist_free(tags);
  }

  return FALSE;
}

/**************************************************************************
  Set the "hand" cursor when moving over a link.
**************************************************************************/
static void set_cursor_if_appropriate(GtkTextView *text_view, gint x, gint y)
{
  static gboolean hovering_over_link = FALSE;
  static GdkCursor *hand_cursor = NULL;
  static GdkCursor *regular_cursor = NULL;
  GSList *tags, *tagp;
  GtkTextIter iter;
  gboolean hovering = FALSE;

  /* Initialize the cursors. */
  if (!hand_cursor) {
    hand_cursor = gdk_cursor_new_for_display(
        gdk_screen_get_display(gdk_screen_get_default()), GDK_HAND2);
  }
  if (!regular_cursor) {
    regular_cursor = gdk_cursor_new_for_display(
        gdk_screen_get_display(gdk_screen_get_default()), GDK_XTERM);
  }

  gtk_text_view_get_iter_at_location(text_view, &iter, x, y);

  tags = gtk_text_iter_get_tags(&iter);
  for (tagp = tags; tagp; tagp = tagp->next) {
    enum text_link_type type =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tagp->data), "type"));

    if (type != 0) {
      hovering = TRUE;
      break;
    }
  }

  if (hovering != hovering_over_link) {
    hovering_over_link = hovering;

    if (hovering_over_link) {
      gdk_window_set_cursor(gtk_text_view_get_window(text_view,
                                                     GTK_TEXT_WINDOW_TEXT),
                            hand_cursor);
    } else {
      gdk_window_set_cursor(gtk_text_view_get_window(text_view,
                                                     GTK_TEXT_WINDOW_TEXT),
                            regular_cursor);
    }
  }

  if (tags) {
    g_slist_free(tags);
  }
}

/**************************************************************************
  Maybe are the mouse is moving over a link.
**************************************************************************/
static gboolean motion_notify_event(GtkWidget *text_view,
                                    GdkEventMotion *event)
{
  gint x, y;

  gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(text_view), 
                                        GTK_TEXT_WINDOW_WIDGET,
                                        event->x, event->y, &x, &y);
  set_cursor_if_appropriate(GTK_TEXT_VIEW(text_view), x, y);
  gdk_window_get_pointer(text_view->window, NULL, NULL, NULL);

  return FALSE;
}

/**************************************************************************
  Maybe are the mouse is moving over a link.
**************************************************************************/
static gboolean visibility_notify_event(GtkWidget *text_view,
                                        GdkEventVisibility *event)
{
  gint wx, wy, bx, by;

  gdk_window_get_pointer(text_view->window, &wx, &wy, NULL);
  gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW (text_view), 
                                        GTK_TEXT_WINDOW_WIDGET,
                                        wx, wy, &bx, &by);
  set_cursor_if_appropriate(GTK_TEXT_VIEW(text_view), bx, by);

  return FALSE;
}

/**************************************************************************
  Set the appropriate callbacks for the message buffer.
**************************************************************************/
void set_message_buffer_view_link_handlers(GtkWidget *view)
{
  g_signal_connect(view, "event-after",
		   G_CALLBACK(event_after), NULL);
  g_signal_connect(view, "motion-notify-event",
		   G_CALLBACK(motion_notify_event), NULL);
  g_signal_connect(view, "visibility-notify-event",
		   G_CALLBACK(visibility_notify_event), NULL);

}

/**************************************************************************
  Convert a struct text_tag to a GtkTextTag.
**************************************************************************/
static void apply_text_tag(const struct text_tag *ptag, GtkTextBuffer *buf,
                           offset_t text_start_offset, const char *text)
{
  static bool initalized = FALSE;
  GtkTextIter start, stop;

  if (!initalized) {
    gtk_text_buffer_create_tag(buf, "bold",
                               "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(buf, "italic",
                               "style", PANGO_STYLE_ITALIC, NULL);
    gtk_text_buffer_create_tag(buf, "strike",
                               "strikethrough", TRUE, NULL);
    gtk_text_buffer_create_tag(buf, "underline",
                               "underline", PANGO_UNDERLINE_SINGLE, NULL);
    initalized = TRUE;
  }

  /* Get the position. */
  /*
   * N.B.: text_tag_*_offset() value is in bytes, so we need to convert it
   * to utf8 character offset.
   */
  gtk_text_buffer_get_iter_at_offset(buf, &start, text_start_offset
                                     + g_utf8_pointer_to_offset(text,
                                     text + text_tag_start_offset(ptag)));
  if (text_tag_stop_offset(ptag) == OFFSET_UNSET) {
    gtk_text_buffer_get_end_iter(buf, &stop);
  } else {
    gtk_text_buffer_get_iter_at_offset(buf, &stop, text_start_offset
                                       + g_utf8_pointer_to_offset(text,
                                       text + text_tag_stop_offset(ptag)));
  }

  switch (text_tag_type(ptag)) {
  case TTT_BOLD:
    gtk_text_buffer_apply_tag_by_name(buf, "bold", &start, &stop);
    break;
  case TTT_ITALIC:
    gtk_text_buffer_apply_tag_by_name(buf, "italic", &start, &stop);
    break;
  case TTT_STRIKE:
    gtk_text_buffer_apply_tag_by_name(buf, "strike", &start, &stop);
    break;
  case TTT_UNDERLINE:
    gtk_text_buffer_apply_tag_by_name(buf, "underline", &start, &stop);
    break;
  case TTT_COLOR:
    {
      /* We have to make a new tag every time. */
      GtkTextTag *tag = NULL;
      GdkColor foreground;
      GdkColor background;

      if (gdk_color_parse(text_tag_color_foreground(ptag), &foreground)) {
        if (gdk_color_parse(text_tag_color_background(ptag),
                            &background)) {
          tag = gtk_text_buffer_create_tag(buf, NULL,
                                           "foreground-gdk", &foreground,
                                           "background-gdk", &background,
                                           NULL);
        } else {
          tag = gtk_text_buffer_create_tag(buf, NULL,
                                           "foreground-gdk", &foreground,
                                           NULL);
        }
      } else if (gdk_color_parse(text_tag_color_background(ptag),
                                 &background)) {
        tag = gtk_text_buffer_create_tag(buf, NULL,
                                         "background-gdk", &background,
                                         NULL);
      }

      if (!tag) {
        break; /* No color. */
      }
      gtk_text_buffer_apply_tag(buf, tag, &start, &stop);
      g_object_unref(G_OBJECT(tag));
    }
    break;
  case TTT_LINK:
    {
      struct color *pcolor = NULL;
      GtkTextTag *tag;

      switch (text_tag_link_type(ptag)) {
      case TLT_CITY:
        pcolor = get_color(tileset, COLOR_MAPVIEW_CITY_LINK);
        break;
      case TLT_TILE:
        pcolor = get_color(tileset, COLOR_MAPVIEW_TILE_LINK);
        break;
      case TLT_UNIT:
        pcolor = get_color(tileset, COLOR_MAPVIEW_UNIT_LINK);
        break;
      }

      if (!pcolor) {
        break; /* Not a valid link type case. */
      }

      tag = gtk_text_buffer_create_tag(buf, NULL, 
                                       "foreground-gdk", &pcolor->color, 
                                       "underline", PANGO_UNDERLINE_SINGLE,
                                       NULL);

      /* Type 0 is reserved for non-link tags.  So, add 1 to the
       * type value. */
      g_object_set_data(G_OBJECT(tag), "type",
                        GINT_TO_POINTER(text_tag_link_type(ptag) + 1));
      g_object_set_data(G_OBJECT(tag), "id",
                        GINT_TO_POINTER(text_tag_link_id(ptag)));
      gtk_text_buffer_apply_tag(buf, tag, &start, &stop);
      g_object_unref(G_OBJECT(tag));
      break;
    }
  }
}

/**************************************************************************
  Appends the string to the chat output window.  The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void real_append_output_window(const char *astring,
                               const struct text_tag_list *tags,
                               int conn_id)
{
  GtkTextBuffer *buf;
  GtkTextIter iter;
  GtkTextMark *mark;
  offset_t text_start_offset;

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

  text_start_offset = gtk_text_iter_get_offset(&iter);
  gtk_text_buffer_insert(buf, &iter, astring, -1);
  text_tag_list_iterate(tags, ptag) {
    apply_text_tag(ptag, buf, text_start_offset, astring);
  } text_tag_list_iterate_end;

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
