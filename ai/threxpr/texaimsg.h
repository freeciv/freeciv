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
#ifndef FC__TEXAIMSG_H
#define FC__TEXAIMSG_H

#define SPECENUM_NAME texaimsgtype
#define SPECENUM_VALUE0 TEXAI_MSG_THR_EXIT
#define SPECENUM_VALUE0NAME "Exit"
#define SPECENUM_VALUE1 TEXAI_MSG_FIRST_ACTIVITIES
#define SPECENUM_VALUE1NAME "FirstActivities"
#define SPECENUM_VALUE2 TEXAI_MSG_PHASE_FINISHED
#define SPECENUM_VALUE2NAME "PhaseFinished"
#define SPECENUM_VALUE3 TEXAI_MSG_TILE_INFO
#define SPECENUM_VALUE3NAME "TileInfo"
#define SPECENUM_VALUE4 TEXAI_MSG_GAME_START
#define SPECENUM_VALUE4NAME "GameStart"
#define SPECENUM_VALUE5 TEXAI_MSG_GAME_END
#define SPECENUM_VALUE5NAME "GameEnd"
#include "specenum_gen.h"

#define SPECENUM_NAME texaireqtype
#define SPECENUM_VALUE0 TEXAI_REQ_WORKER_TASK
#define SPECENUM_VALUE0NAME "WorkerTask"
#define SPECENUM_VALUE1 TEXAI_REQ_TURN_DONE
#define SPECENUM_VALUE1NAME "TurnDone"
#include "specenum_gen.h"

struct texai_msg
{
  enum texaimsgtype type;
  struct player *plr;
  void *data;
};

struct texai_req
{
  enum texaireqtype type;
  struct player *plr;
  void *data;
};

#define SPECLIST_TAG texaimsg
#define SPECLIST_TYPE struct texai_msg
#include "speclist.h"

#define SPECLIST_TAG texaireq
#define SPECLIST_TYPE struct texai_req
#include "speclist.h"

void texai_send_msg(enum texaimsgtype type, struct player *pplayer,
                    void *data);
void texai_send_req(enum texaireqtype type, struct player *pplayer,
                    void *data);

void texai_first_activities(struct ai_type *ait, struct player *pplayer);
void texai_phase_finished(struct ai_type *ait, struct player *pplayer);

#endif /* FC__TEXAIMSG_H */
