/**********************************************************************
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

#define SPECENUM_NAME mk_eval_result
#define SPECENUM_VALUE0 MKE_FALSE
#define SPECENUM_VALUE0NAME "Certain_False"
#define SPECENUM_VALUE1 MKE_UNCERTAIN
#define SPECENUM_VALUE1NAME "Uncertain"
#define SPECENUM_VALUE2 MKE_TRUE
#define SPECENUM_VALUE2NAME "Certain_True"
#include "specenum_gen.h"

enum mk_eval_result mke_and(enum mk_eval_result one,
                            enum mk_eval_result two);

enum mk_eval_result
mke_eval_req(const struct player *pow_player,
             const struct player *target_player,
             const struct player *other_player,
             const struct city *target_city,
             const struct impr_type *target_building,
             const struct tile *target_tile,
             const struct unit *target_unit,
             const struct output_type *target_output,
             const struct specialist *target_specialist,
             const struct requirement *req);

enum mk_eval_result
mke_eval_reqs(const struct player *pow_player,
              const struct player *target_player,
              const struct player *other_player,
              const struct city *target_city,
              const struct impr_type *target_building,
              const struct tile *target_tile,
              const struct unit *target_unit,
              const struct output_type *target_output,
              const struct specialist *target_specialist,
              const struct requirement_vector *reqs);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC_META_KNOWLEDGE_H */
