/********************************************************************** 
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
#ifndef FC__VOTE_H
#define FC__VOTE_H

enum vote_type {
  VOTE_ABSTAIN = 0,
  VOTE_YES,
  VOTE_NO,

  NUM_VOTE_TYPES
};

struct connection;
struct vote;

struct vote *vote_new(struct connection *caller,
                      const char *full_command);
int vote_number(const struct vote *pvote);
int vote_cast_count(const struct vote *pvote, enum vote_type vt);
const char *vote_cmdline(const struct vote *pvote);

#define SPECLIST_TAG vote
#define SPECLIST_TYPE struct vote
#include "speclist.h"
#define vote_list_iterate(alist, pvote) \
      TYPED_LIST_ITERATE(struct vote, alist, pvote)
#define vote_list_iterate_end  LIST_ITERATE_END

void voting_init(void);
void voting_turn(void);
void voting_free(void);
int voting_get_voter_count(void);
int voting_get_vote_count(void);
struct vote *voting_get_last_vote(void);
struct vote *voting_get_vote_by_no(int vote_no);
struct vote *voting_get_vote_by_caller(const struct connection *caller);
const struct vote_list *voting_get_vote_list(void);

bool connection_can_vote(struct connection *pconn);
void connection_vote(struct connection *pconn, struct vote *pvote,
                     enum vote_type type);

void clear_all_votes(void);
void cancel_connection_votes(struct connection *pconn);


#endif /* FC__VOTE_H */
