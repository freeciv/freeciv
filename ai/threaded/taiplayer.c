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

/* What level of operation we should abort because
 * of received messages. Lower is more critical;
 * TAI_ABORT_EXIT means that whole thread should exit,
 * TAI_ABORT_NONE means that we can continue what we were doing */
enum tai_abort_msg_class
{
  TAI_ABORT_EXIT,
  TAI_ABORT_PHASE_END,
  TAI_ABORT_NONE
};

static enum tai_abort_msg_class tai_check_messages(struct player *pplayer);

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
  fc_allocate_mutex(&data->msgs_to.mutex);
  while (!finished) {
    fc_thread_cond_wait(&data->msgs_to.thr_cond, &data->msgs_to.mutex);

    if (tai_check_messages(pplayer) <= TAI_ABORT_EXIT) {
      finished = TRUE;
    }
  }
  fc_release_mutex(&data->msgs_to.mutex);

  log_debug("AI thread exiting");
}

/**************************************************************************
  Handle messages from message queue.
**************************************************************************/
enum tai_abort_msg_class tai_check_messages(struct player *pplayer)
{
  enum tai_abort_msg_class ret_abort= TAI_ABORT_NONE;
  struct tai_plr *data;

  data = tai_player_data(pplayer);

  taimsg_list_allocate_mutex(data->msgs_to.msglist);
  while(taimsg_list_size(data->msgs_to.msglist) > 0) {
    struct tai_msg *msg;
    enum tai_abort_msg_class new_abort = TAI_ABORT_NONE;

    msg = taimsg_list_get(data->msgs_to.msglist, 0);
    taimsg_list_remove(data->msgs_to.msglist, msg);
    taimsg_list_release_mutex(data->msgs_to.msglist);

    log_debug("Plr thr \"%s\" got %s", player_name(pplayer),
              taimsgtype_name(msg->type));

    switch(msg->type) {
    case TAI_MSG_FIRST_ACTIVITIES:
      /* Not implemented */
      break;
    case TAI_MSG_PHASE_FINISHED:
      new_abort = TAI_ABORT_PHASE_END;
      break;
    case TAI_MSG_THR_EXIT:
      new_abort = TAI_ABORT_EXIT;
      break;
    default:
      log_error("Illegal message type %s (%d) for threaded ai!",
                taimsgtype_name(msg->type), msg->type);
      break;
    }

    if (new_abort < ret_abort) {
      ret_abort = new_abort;
    }

    FC_FREE(msg);

    taimsg_list_allocate_mutex(data->msgs_to.msglist);
  }
  taimsg_list_release_mutex(data->msgs_to.msglist);

  return ret_abort;
}

/**************************************************************************
  Initialize player for use with threaded AI.
**************************************************************************/
void tai_player_alloc(struct player *pplayer)
{
  struct tai_plr *player_data = fc_calloc(1, sizeof(struct tai_plr));

  player_set_ai_data(pplayer, tai_get_self(), player_data);

  player_data->thread_running = FALSE;

  player_data->msgs_to.msglist = taimsg_list_new();
  player_data->reqs_from.reqlist = taireq_list_new();
}

/**************************************************************************
  Free player from use with threaded AI.
**************************************************************************/
void tai_player_free(struct player *pplayer)
{
  struct tai_plr *player_data = tai_player_data(pplayer);

  if (player_data != NULL) {
    tai_control_lost(pplayer);
    fc_thread_cond_destroy(&player_data->msgs_to.thr_cond);
    fc_destroy_mutex(&player_data->msgs_to.mutex);
    taimsg_list_destroy(player_data->msgs_to.msglist);
    taireq_list_destroy(player_data->reqs_from.reqlist);
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

  fc_thread_cond_init(&player_data->msgs_to.thr_cond);
  fc_init_mutex(&player_data->msgs_to.mutex);
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

    tai_send_msg(TAI_MSG_THR_EXIT, pplayer, NULL);

    fc_thread_wait(&player_data->ait);
    player_data->thread_running = FALSE;
  }
}

/**************************************************************************
  Check for messages sent by player thread
**************************************************************************/
void tai_refresh(struct player *pplayer)
{
  struct tai_plr *player_data = tai_player_data(pplayer);

  if (player_data->thread_running) {
    taireq_list_allocate_mutex(player_data->reqs_from.reqlist);
    while(taireq_list_size(player_data->reqs_from.reqlist) > 0) {
       struct tai_req *req;

       req = taireq_list_get(player_data->reqs_from.reqlist, 0);
       taireq_list_remove(player_data->reqs_from.reqlist, req);

       taireq_list_release_mutex(player_data->reqs_from.reqlist);

       log_debug("Plr thr \"%s\" sent %s", player_name(pplayer),
                 taireqtype_name(req->type));

       FC_FREE(req);

       taireq_list_allocate_mutex(player_data->reqs_from.reqlist);
     }
    taireq_list_release_mutex(player_data->reqs_from.reqlist);
  }
}

/**************************************************************************
  Send message to thread.
**************************************************************************/
void tai_msg_to_thr(struct player *pplayer, struct tai_msg *msg)
{
  struct tai_plr *player_data = tai_player_data(pplayer);

  fc_allocate_mutex(&player_data->msgs_to.mutex);
  taimsg_list_append(player_data->msgs_to.msglist, msg);
  fc_thread_cond_signal(&player_data->msgs_to.thr_cond);
  fc_release_mutex(&player_data->msgs_to.mutex);
}

/**************************************************************************
  Thread sends message.
**************************************************************************/
void tai_req_from_thr(struct player *pplayer, struct tai_req *req)
{
  struct tai_plr *player_data = tai_player_data(pplayer);

  taireq_list_allocate_mutex(player_data->reqs_from.reqlist);
  taireq_list_append(player_data->reqs_from.reqlist, req);
  taireq_list_release_mutex(player_data->reqs_from.reqlist);
}
