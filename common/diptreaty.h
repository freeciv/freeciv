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
#ifndef FC__DIPTREATY_H
#define FC__DIPTREATY_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "requirements.h"
#include "support.h"            /* bool type */

/* Used in the network protocol */
#define SPECENUM_NAME clause_type
#define SPECENUM_VALUE0 CLAUSE_ADVANCE
#define SPECENUM_VALUE0NAME "Advance"
#define SPECENUM_VALUE1 CLAUSE_GOLD
#define SPECENUM_VALUE1NAME "Gold"
#define SPECENUM_VALUE2 CLAUSE_MAP
#define SPECENUM_VALUE2NAME "Map"
#define SPECENUM_VALUE3 CLAUSE_SEAMAP
#define SPECENUM_VALUE3NAME "Seamap"
#define SPECENUM_VALUE4 CLAUSE_CITY
#define SPECENUM_VALUE4NAME "City"
#define SPECENUM_VALUE5 CLAUSE_CEASEFIRE
#define SPECENUM_VALUE5NAME "Ceasefire"
#define SPECENUM_VALUE6 CLAUSE_PEACE
#define SPECENUM_VALUE6NAME "Peace"
#define SPECENUM_VALUE7 CLAUSE_ALLIANCE
#define SPECENUM_VALUE7NAME "Alliance"
#define SPECENUM_VALUE8 CLAUSE_VISION
#define SPECENUM_VALUE8NAME "Vision"
#define SPECENUM_VALUE9 CLAUSE_EMBASSY
#define SPECENUM_VALUE9NAME "Embassy"
#define SPECENUM_VALUE10 CLAUSE_SHARED_TILES
#define SPECENUM_VALUE10NAME "SharedTiles"
#define SPECENUM_COUNT CLAUSE_COUNT
#include "specenum_gen.h"

#define is_pact_clause(x)                                                   \
  ((x == CLAUSE_CEASEFIRE) || (x == CLAUSE_PEACE) || (x == CLAUSE_ALLIANCE))

struct clause_info
{
  enum clause_type type;
  bool enabled;
  struct requirement_vector giver_reqs;
  struct requirement_vector receiver_reqs;
  struct requirement_vector either_reqs;
};

/* For when we need to iterate over treaties */
struct Clause;
#define SPECLIST_TAG clause
#define SPECLIST_TYPE struct Clause
#include "speclist.h"

#define clause_list_iterate(clauselist, pclause) \
    TYPED_LIST_ITERATE(struct Clause, clauselist, pclause)
#define clause_list_iterate_end  LIST_ITERATE_END

struct Clause {
  enum clause_type type;
  struct player *from;
  int value;
};

struct treaty {
  struct player *plr0, *plr1;
  bool accept0, accept1;
  struct clause_list *clauses;
};

bool diplomacy_possible(const struct player *pplayer,
                        const struct player *aplayer);
bool could_meet_with_player(const struct player *pplayer,
                            const struct player *aplayer);
bool could_intel_with_player(const struct player *pplayer,
                             const struct player *aplayer);

void init_treaty(struct treaty *ptreaty,
                 struct player *plr0, struct player *plr1);
bool add_clause(struct treaty *ptreaty, struct player *pfrom,
                enum clause_type type, int val,
                struct player *client_player);
bool remove_clause(struct treaty *ptreaty, struct player *pfrom,
                   enum clause_type type, int val);
void clear_treaty(struct treaty *ptreaty);

void clause_infos_init(void);
void clause_infos_free(void);
struct clause_info *clause_info_get(enum clause_type type);

bool clause_enabled(enum clause_type type);

#define SPECLIST_TAG treaty
#define SPECLIST_TYPE struct treaty
#include "speclist.h"

#define treaty_list_iterate(list, p) \
    TYPED_LIST_ITERATE(struct treaty, list, p)
#define treaty_list_iterate_end  LIST_ITERATE_END

void treaties_init(void);
void treaties_free(void);
void free_treaties(void);

struct treaty *find_treaty(struct player *plr0, struct player *plr1);

void treaty_add(struct treaty *ptreaty);
void treaty_remove(struct treaty *ptreaty);

typedef void (*treaty_cb)(struct treaty *, void *data);
void treaties_iterate(treaty_cb cb, void *data);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__DIPTREATY_H */
