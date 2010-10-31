/********************************************************************** 
 Freeciv - Copyright (C) 2002 - R. Falke
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

#include <string.h>

/* utility */
#include "fcintl.h"
#include "mem.h"

/* common */
#include "featured_text.h"
#include "map.h"

/* include */
#include "citydlg_g.h"
#include "mapview_g.h"
#include "messagewin_g.h"

/* client */
#include "client_main.h"
#include "messagewin_common.h"
#include "options.h"
#include "update_queue.h"

static struct message *messages = NULL;
static int messages_total = 0;
static int messages_alloc = 0;

/****************************************************************************
  Update the message dialog if needed.
****************************************************************************/
static void meswin_dialog_update(void)
{
  if (!can_client_change_view()) {
    return;
  }

  if (meswin_dialog_is_open()) {
    update_queue_add(UQ_CALLBACK(real_meswin_dialog_update), NULL);
  } else if (0 < messages_total
             && (!client_has_player()
                 || !client.conn.playing->ai_controlled)) {
    meswin_dialog_popup(FALSE);
  }
}

/****************************************************************************
  Clear all messages.
****************************************************************************/
void meswin_clear(void)
{
  int i;

  for (i = 0; i < messages_total; i++) {
    free(messages[i].descr);
    messages[i].descr = NULL;

    text_tag_list_destroy(messages[i].tags);
    messages[i].tags = NULL;
  }
  messages_total = 0;
  meswin_dialog_update();
}

/****************************************************************************
  Add a message.
****************************************************************************/
void meswin_add(const char *message, const struct text_tag_list *tags,
                struct tile *ptile, enum event_type event)
{
  const size_t min_msg_len = 50;
  size_t msg_len = strlen(message);
  char *s = fc_malloc(MAX(msg_len, min_msg_len) + 1);
  int i, nspc;

  if (messages_total + 2 > messages_alloc) {
    messages_alloc = messages_total + 32;
    messages = fc_realloc(messages, messages_alloc * sizeof(*messages));
  }

  strcpy(s, message);

  nspc = min_msg_len - strlen(s);
  if (nspc > 0) {
    strncat(s, "                                                  ", nspc);
  }

  messages[messages_total].tile = ptile;
  messages[messages_total].event = event;
  messages[messages_total].descr = s;
  messages[messages_total].tags = text_tag_list_copy(tags);
  messages[messages_total].location_ok = (ptile != NULL);
  messages[messages_total].visited = FALSE;
  messages_total++;

  /* Update the city_ok fields of all messages since the city may have
   * changed owner. */
  for (i = 0; i < messages_total; i++) {
    if (messages[i].location_ok) {
      struct city *pcity = tile_city(messages[i].tile);

      messages[i].city_ok =
          (pcity
           && (!client_has_player()
               || can_player_see_city_internals(client_player(), pcity)));
    } else {
      messages[i].city_ok = FALSE;
    }
  }

  meswin_dialog_update();
}

/****************************************************************************
  Returns the pointer to a message.  Returns NULL on error.
****************************************************************************/
const struct message *meswin_get_message(int message_index)
{
  if (message_index >= 0 && message_index < messages_total) {
    return &messages[message_index];
  } else {
    /* Can happen in turn change... */
    return NULL;
  }
}

/****************************************************************************
  Returns the number of message in the window.
****************************************************************************/
int meswin_get_num_messages(void)
{
  return messages_total;
}

/****************************************************************************
  Sets the visited-state of a message
****************************************************************************/
void meswin_set_visited_state(int message_index, bool state)
{
  fc_assert_ret(0 <= message_index && message_index < messages_total);

  messages[message_index].visited = state;
}

/****************************************************************************
  Called from messagewin.c if the user clicks on the popup-city button.
****************************************************************************/
void meswin_popup_city(int message_index)
{
  fc_assert_ret(0 <= message_index && message_index < messages_total);

  if (messages[message_index].city_ok) {
    struct tile *ptile = messages[message_index].tile;
    struct city *pcity = tile_city(ptile);

    if (center_when_popup_city) {
      center_tile_mapcanvas(ptile);
    }

    if (pcity && can_player_see_units_in_city(client.conn.playing, pcity)) {
      /* If the event was the city being destroyed, pcity will be NULL
       * and we'd better not try to pop it up.  It's also possible that
       * events will happen on enemy cities; we generally don't want to pop
       * those dialogs up either (although it's hard to be certain).
       *
       * In both cases, it would be better if the popup button weren't
       * highlighted at all - this is left up to the GUI. */
      popup_city_dialog(pcity);
    }
  }
}

/**************************************************************************
  Called from messagewin.c if the user clicks on the goto button.
**************************************************************************/
void meswin_goto(int message_index)
{
  fc_assert_ret(0 <= message_index && message_index < messages_total);

  if (messages[message_index].location_ok) {
    center_tile_mapcanvas(messages[message_index].tile);
  }
}

/****************************************************************************
  Called from messagewin.c if the user double clicks on a message.
****************************************************************************/
void meswin_double_click(int message_index)
{
  fc_assert_ret(0 <= message_index && message_index < messages_total);

  if (messages[message_index].city_ok
      && is_city_event(messages[message_index].event)) {
    meswin_popup_city(message_index);
  } else if (messages[message_index].location_ok) {
    meswin_goto(message_index);
  }
}
