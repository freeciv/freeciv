/***********************************************************************
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
#include <fc_config.h>
#endif

/* utility */
#include "log.h"

/* common */
#include "ai.h"
#include "city.h"
#include "unit.h"

#include "taiplayer.h"

static struct ai_type *self = NULL;

/**************************************************************************
  Set pointer to ai type of the threaded ai.
**************************************************************************/
void tai_set_self(struct ai_type *ai)
{
  self = ai;
}

/**************************************************************************
  Get pointer to ai type of the threaded ai.
**************************************************************************/
struct ai_type *tai_get_self(void)
{
  return self;
}

/**************************************************************************
  This is main function of ai thread.
**************************************************************************/
static void tai_thread_start(void *arg)
{
  struct player *pplayer = (struct player *) arg;
  struct tai_plr *data = tai_player_data(pplayer);
  bool finished = FALSE;

  log_debug("New AI thread launched");

  /* Just wait until we are signaled to shutdown */
  fc_allocate_mutex(&data->msgs.mutex);
  while (!finished) {
    fc_thread_cond_wait(&data->msgs.thr_cond, &data->msgs.mutex);
    data = tai_player_data(pplayer);

    taimsg_list_allocate_mutex(data->msgs.msglist);
    while(taimsg_list_size(data->msgs.msglist) > 0) {
      struct tai_msg *msg;

      msg = taimsg_list_get(data->msgs.msglist, 0);
      taimsg_list_remove(data->msgs.msglist, msg);
      taimsg_list_release_mutex(data->msgs.msglist);

      switch(msg->type) {
       case TAI_MSG_THR_EXIT:
         finished = TRUE;
         break;
       default:
         log_error("Illegal message type %d for threaded ai!",
                   msg->type);
         break;
      }

      FC_FREE(msg);

      taimsg_list_allocate_mutex(data->msgs.msglist);
    }
    taimsg_list_release_mutex(data->msgs.msglist);
  }
  fc_release_mutex(&data->msgs.mutex);

  log_debug("AI thread exiting");
}

/**************************************************************************
  Initialize player for use with threaded AI.
**************************************************************************/
void tai_player_alloc(struct player *pplayer)
{
  struct tai_plr *player_data = fc_calloc(1, sizeof(struct tai_plr));

  player_set_ai_data(pplayer, tai_get_self(), player_data);

  player_data->thread_running = FALSE;

  player_data->msgs.msglist = taimsg_list_new();
}

/**************************************************************************
  Free player from use with default AI.
**************************************************************************/
void tai_player_free(struct player *pplayer)
{
  struct tai_plr *player_data = tai_player_data(pplayer);

  if (player_data != NULL) {
    tai_control_lost(pplayer);
    fc_thread_cond_destroy(&player_data->msgs.thr_cond);
    fc_destroy_mutex(&player_data->msgs.mutex);
    taimsg_list_destroy(player_data->msgs.msglist);
    player_set_ai_data(pplayer, tai_get_self(), NULL);
    FC_FREE(player_data);
  }
}

/**************************************************************************
  We actually control the player
**************************************************************************/
void tai_control_gained(struct player *pplayer)
{
  struct tai_plr *player_data = tai_player_data(pplayer);

  player_data->thread_running = TRUE;

  fc_thread_cond_init(&player_data->msgs.thr_cond);
  fc_init_mutex(&player_data->msgs.mutex);
  fc_thread_start(&player_data->ait, tai_thread_start, pplayer);
}

/**************************************************************************
  We no longer control the player
**************************************************************************/
void tai_control_lost(struct player *pplayer)
{
  struct tai_plr *player_data = tai_player_data(pplayer);

  if (player_data->thread_running) {
    struct tai_msg *thrq_msg = fc_malloc(sizeof(*thrq_msg));

    thrq_msg->type = TAI_MSG_THR_EXIT;
    thrq_msg->data = NULL;
    tai_send_msg(pplayer, thrq_msg);

    fc_thread_wait(&player_data->ait);
    player_data->thread_running = FALSE;
  }
}

/**************************************************************************
  Send message to thread.
**************************************************************************/
void tai_send_msg(struct player *pplayer, struct tai_msg *msg)
{
  struct tai_plr *player_data = tai_player_data(pplayer);

  fc_allocate_mutex(&player_data->msgs.mutex);
  taimsg_list_append(player_data->msgs.msglist, msg);
  fc_thread_cond_signal(&player_data->msgs.thr_cond);
  fc_release_mutex(&player_data->msgs.mutex);
}
