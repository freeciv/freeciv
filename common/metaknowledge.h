/***********************************************************************
 Freeciv - Copyright (C) 1996-2013 - Freeciv Development Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC_META_KNOWLEDGE_H
#define FC_META_KNOWLEDGE_H

/* common */
#include "requirements.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum fc_tristate
mke_eval_req(const struct player *pov_player,
             const struct req_context *context,
             const struct req_context *other_context,
             const struct requirement *req,
             const enum   req_problem_type prob_type);

enum fc_tristate
mke_eval_reqs(const struct player *pov_player,
              const struct req_context *context,
              const struct req_context *other_context,
              const struct requirement_vector *reqs,
              const enum   req_problem_type prob_type);


bool can_see_techs_of_target(const struct player *pov_player,
                             const struct player *target_player);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC_META_KNOWLEDGE_H */
