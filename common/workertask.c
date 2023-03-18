/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "log.h"

/* common */
#include "city.h"
#include "extras.h"
#include "unit.h"

#include "workertask.h"

/************************************************************************//**
  Initialize empty worker_task.
****************************************************************************/
void worker_task_init(struct worker_task *ptask)
{
  ptask->ptile = NULL;
  ptask->want = 0;
}

/************************************************************************//**
  Returns TRUE iff the specified worker_task is sane.
****************************************************************************/
bool worker_task_is_sane(struct worker_task *ptask)
{
  if (ptask == NULL) {
    return FALSE;
  }

  if (ptask->ptile == NULL) {
    return FALSE;
  }

  if (ptask->act >= ACTIVITY_LAST) {
    return FALSE;
  }

  if (activity_requires_target(ptask->act)) {
    if (ptask->tgt == NULL) {
      return FALSE;
    }
    if (!is_extra_caused_by(ptask->tgt,
                            activity_to_extra_cause(ptask->act))
        && !is_extra_removed_by(ptask->tgt,
                                activity_to_extra_rmcause(ptask->act))) {
      return FALSE;
    }
  }

  return TRUE;
}
