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
#ifndef FC__OBLIG_REQS_H
#define FC__OBLIG_REQS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Custom data types for obligatory hard action requirements. */

/* A contradiction to an obligatory hard requirement. */
struct action_enabler_contradiction {
  /* A requirement that contradicts the obligatory hard requirement. */
  struct requirement req;

  /* Is the obligatory hard requirement in the action enabler's target
   * requirement vector? If FALSE it is in its actor requirement vector. */
  bool is_target;
};

/* An obligatory hard action requirement */
struct obligatory_req {
  /* The requirement is fulfilled if the action enabler doesn't contradict
   * one of the contradictions listed here. */
  struct ae_contra_or *contras;

  /* The error message to show when the hard obligatory requirement is
   * missing. Must be there. */
  const char *error_msg;
};

#define SPECVEC_TAG obligatory_req
#define SPECVEC_TYPE struct obligatory_req
#include "specvec.h"
#define obligatory_req_vector_iterate(obreq_vec, pobreq) \
  TYPED_VECTOR_ITERATE(struct obligatory_req, obreq_vec, pobreq)
#define obligatory_req_vector_iterate_end VECTOR_ITERATE_END

/* One or more alternative obligatory hard requirement contradictions. */
struct ae_contra_or {
  int alternatives;
  /* The obligatory hard requirement is fulfilled if a contradiction exists
   * that doesn't contradict the action enabler. */
  struct action_enabler_contradiction *alternative;

  /* How many obligatory reqs use this */
  int users;
};

void oblig_hard_reqs_init(void);
void oblig_hard_reqs_free(void);

void hard_code_oblig_hard_reqs(void);
void hard_code_oblig_hard_reqs_ruleset(void);

struct obligatory_req_vector *oblig_hard_reqs_get(enum action_result res);
struct obligatory_req_vector *oblig_hard_reqs_get_sub(enum action_sub_result res);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* OBLIG_REQS_H */
