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

#include <assert.h>
#include <string.h>

#include "fcintl.h"
#include "map.h"
#include "mem.h"

#include "citydlg_g.h"
#include "mapview_g.h"
#include "messagewin_g.h"
#include "options.h"

#include "messagewin_common.h"

static int frozen_level = 0;
static bool change = FALSE;
static struct message *messages = NULL;
static int messages_total = 0;
static int messages_alloc = 0;

/******************************************************************
 Turn off updating of message window
*******************************************************************/
void meswin_freeze(void)
{
  frozen_level++;
}

/******************************************************************
 Turn on updating of message window
*******************************************************************/
void meswin_thaw(void)
{
  frozen_level--;
  assert(frozen_level >= 0);
  if (frozen_level == 0) {
    update_meswin_dialog();
  }
}

/******************************************************************
 Turn on updating of message window
*******************************************************************/
void meswin_force_thaw(void)
{
  frozen_level = 1;
  meswin_thaw();
}

/**************************************************************************
...
**************************************************************************/
void update_meswin_dialog(void)
{
  if (frozen_level > 0 || !change) {
    return;
  }

  if (!is_meswin_open() && messages_total > 0 &&
      (!game.player_ptr->ai.control || ai_popup_windows)) {
    popup_meswin_dialog();
    change = FALSE;
    return;
  }

  if (is_meswin_open()) {
    real_update_meswin_dialog();
    change = FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
void clear_notify_window(void)
{
  int i;

  change = TRUE;
  for (i = 0; i < messages_total; i++) {
    free(messages[i].descr);
    messages[i].descr = NULL;
  }
  messages_total = 0;
  update_meswin_dialog();
}

/**************************************************************************
...
**************************************************************************/
void add_notify_window(char *message, struct tile *ptile,
		       enum event_type event)
{
  const size_t min_msg_len = 50;
  const char *game_prefix1 = "Game: ";
  const char *game_prefix2 = _("Game: ");
  size_t gp_len1 = strlen(game_prefix1);
  size_t gp_len2 = strlen(game_prefix2);
  char *s = fc_malloc(MAX(strlen(message), min_msg_len) + 1);
  int i, nspc;

  change = TRUE;

  if (messages_total + 2 > messages_alloc) {
    messages_alloc = messages_total + 32;
    messages = fc_realloc(messages, messages_alloc * sizeof(*messages));
  }

  if (strncmp(message, game_prefix1, gp_len1) == 0) {
    strcpy(s, message + gp_len1);
  } else if (strncmp(message, game_prefix2, gp_len2) == 0) {
    strcpy(s, message + gp_len2);
  } else {
    strcpy(s, message);
  }

  nspc = min_msg_len - strlen(s);
  if (nspc > 0) {
    strncat(s, "                                                  ", nspc);
  }

  messages[messages_total].tile = ptile;
  messages[messages_total].event = event;
  messages[messages_total].descr = s;
  messages[messages_total].location_ok = (ptile != NULL);
  messages[messages_total].visited = FALSE;
  messages_total++;

  /* 
   * Update the city_ok fields of all messages since the city may have
   * changed owner.
   */
  for (i = 0; i < messages_total; i++) {
    if (messages[i].location_ok) {
      struct city *pcity = map_get_city(messages[i].tile);

      messages[i].city_ok = (pcity && city_owner(pcity) == game.player_ptr);
    } else {
      messages[i].city_ok = FALSE;
    }
  }

  update_meswin_dialog();
}

/**************************************************************************
 Returns the pointer to a message.
**************************************************************************/
struct message *get_message(int message_index)
{
  assert(message_index >= 0 && message_index < messages_total);
  return &messages[message_index];
}

/**************************************************************************
 Returns the number of message in the window.
**************************************************************************/
int get_num_messages(void)
{
  return messages_total;
}

/**************************************************************************
 Sets the visited-state of a message
**************************************************************************/
void set_message_visited_state(int message_index, bool state)
{
  messages[message_index].visited = state;
}

/**************************************************************************
 Called from messagewin.c if the user clicks on the popup-city button.
**************************************************************************/
void meswin_popup_city(int message_index)
{
  assert(message_index < messages_total);

  if (messages[message_index].city_ok) {
    struct tile *ptile = messages[message_index].tile;
    struct city *pcity = map_get_city(ptile);

    if (center_when_popup_city) {
      center_tile_mapcanvas(ptile);
    }

    if (pcity && city_owner(pcity) == game.player_ptr) {
      /* If the event was the city being destroyed, pcity will be NULL
       * and we'd better not try to pop it up.  It's also possible that
       * events will happen on enemy cities; we generally don't want to pop
       * those dialogs up either (although it's hard to be certain).
       *
       * In both cases, it would be better if the popup button weren't
       * highlighted at all - this is left up to the GUI. */
      popup_city_dialog(pcity, FALSE);
    }
  }
}

/**************************************************************************
 Called from messagewin.c if the user clicks on the goto button.
**************************************************************************/
void meswin_goto(int message_index)
{
  assert(message_index < messages_total);

  if (messages[message_index].location_ok) {
    center_tile_mapcanvas(messages[message_index].tile);
  }
}

/**************************************************************************
 Called from messagewin.c if the user double clicks on a message.
**************************************************************************/
void meswin_double_click(int message_index)
{
  assert(message_index < messages_total);

  if (messages[message_index].city_ok
      && is_city_event(messages[message_index].event)) {
    meswin_popup_city(message_index);
  } else if (messages[message_index].location_ok) {
    meswin_goto(message_index);
  }
}
