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
#ifndef FC__TAIPLAYER_H
#define FC__TAIPLAYER_H

/* utility */
#include "fcthread.h"

/* common */
#include "player.h"

/* ai/threaded */
#include "taimsg.h"

struct player;

struct tai_msgs
{
  fc_thread_cond thr_cond;
  fc_mutex mutex;
  struct taimsg_list *msglist;
};

struct tai_plr
{
  struct tai_msgs msgs;
  bool thread_running;
  fc_thread ait;
};

struct ai_type *tai_get_self(void);
void tai_set_self(struct ai_type *ai);

void tai_player_alloc(struct player *pplayer);
void tai_player_free(struct player *pplayer);
void tai_control_gained(struct player *pplayer);
void tai_control_lost(struct player *pplayer);

void tai_send_msg(struct player *pplayer, struct tai_msg *msg);

static inline struct tai_plr *tai_player_data(const struct player *pplayer)
{
  return (struct tai_plr *)player_ai_data(pplayer, tai_get_self());
}

#endif /* FC__TAIPLAYER_H */
