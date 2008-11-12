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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fciconv.h"
#include "fcintl.h"
#include "mem.h"

#include "connection.h"
#include "game.h"
#include "player.h"

#include "console.h"
#include "plrhand.h"
#include "stdinhand.h"
#include "voting.h"

struct vote_cast {
  enum vote_type vote_cast;
  int conn_id;
};

#define SPECLIST_TAG vote_cast
#define SPECLIST_TYPE struct vote_cast
#include "speclist.h"
#define vote_cast_list_iterate(alist, pvc) \
    TYPED_LIST_ITERATE(struct vote_cast, alist, pvc)
#define vote_cast_list_iterate_end  LIST_ITERATE_END

struct vote {
  int caller_id;     /* caller connection id */
  char command[MAX_LEN_CONSOLE_LINE]; /* [0] == \0 if none in action */
  struct vote_cast_list *votes_cast;
  int vote_no;
  int yes, no, abstain;
};

struct vote_list *vote_list = 0;
int vote_number_sequence = 0;


static void check_vote(struct vote *pvote);
static void remove_vote(struct vote *pvote);
static void vote_free(struct vote *pvote);
static struct vote_cast *find_vote_cast(struct vote *pvote, int conn_id);
static struct vote_cast *vote_cast_new(struct vote *pvote);
static void remove_vote_cast(struct vote *pvote, struct vote_cast *pvc);


/**************************************************************************
  Initialize voting related data structures.
**************************************************************************/
void voting_init(void)
{
  if (!vote_list) {
    vote_list = vote_list_new();
    vote_number_sequence = 0;
  }
}

/**************************************************************************
  Cannot vote if:
    * is not connected
    * access level < info
    * isn't a living player
**************************************************************************/
bool connection_can_vote(struct connection *pconn)
{
  if (pconn != NULL && pconn->playing != NULL && !pconn->observer
      && pconn->playing->is_alive && pconn->access_level >= ALLOW_INFO) {
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
  Returns the total number of users that are allowed to vote.
**************************************************************************/
int voting_get_voter_count(void)
{
  int num_voters = 0;

  conn_list_iterate(game.est_connections, pconn) {
    if (connection_can_vote(pconn)) {
      num_voters++;
    }
  } conn_list_iterate_end;

  return num_voters;
}

/**************************************************************************
  Check if we satisfy the criteria for resolving a vote, and resolve it
  if these critera are indeed met. Updates yes and no variables in voting 
  struct as well.

  Criteria:
    Accepted immediately if: > 50% of votes for
    Rejected immediately if: >= 50% of votes against
**************************************************************************/
static void check_vote(struct vote *pvote)
{
  struct connection *pconn = NULL;
  int num_cast = 0, num_voters = 0, voting_base;
  bool resolve = FALSE, passed = FALSE;
  char cmdline[MAX_LEN_CONSOLE_LINE];

  pvote->yes = 0;
  pvote->no = 0;
  pvote->abstain = 0;

  num_voters = voting_get_voter_count();

  vote_cast_list_iterate(pvote->votes_cast, pvc) {
    if (!(pconn = find_conn_by_id(pvc->conn_id))
        || !connection_can_vote(pconn)) {
      continue;
    }
    num_cast++;

    switch (pvc->vote_cast) {
    case VOTE_YES:
      pvote->yes++;
      break;
    case VOTE_NO:
      pvote->no++;
      break;
    case VOTE_ABSTAIN:
      pvote->abstain++;
      break;
    default:
      break;
    }
  } vote_cast_list_iterate_end;

  voting_base = num_voters - pvote->abstain;

  /* Check if we should resolve the vote */
  if ((voting_base > 0 && (pvote->yes > voting_base / 2
                          || pvote->no >= (voting_base + 1) / 2))
      || num_cast >= num_voters) {
    /* Yep, resolve this one */
    resolve = TRUE;
  }

  if (!resolve) {
    return;
  }

  if (pvote->yes > voting_base / 2) {
    passed = TRUE;
  }

  if (passed) {
    notify_conn(NULL, NULL, E_SETTING,
                _("Vote \"%s\" is passed %d to %d with %d "
                  "abstentions and %d that did not vote."),
                pvote->command, pvote->yes, pvote->no,
                pvote->abstain, num_voters - num_cast);
    sz_strlcpy(cmdline, pvote->command);
  } else {
    notify_conn(NULL, NULL, E_SETTING,
                _("Vote \"%s\" failed with %d against, %d for, "	  
                  "%d abstentions, and %d that did not vote."),
                pvote->command, pvote->no, pvote->yes, 
                pvote->abstain, num_voters - num_cast);
  }

  remove_vote(pvote);

  if (passed) {
    handle_stdin_input(NULL, cmdline, FALSE);
  }
}

/**************************************************************************
  Remove the given vote.
**************************************************************************/
static void remove_vote(struct vote *pvote)
{
  if (!pvote) {
    return;
  }

  if (vote_list) {
    vote_list_unlink(vote_list, pvote);
  }

  vote_free(pvote);
}

/**************************************************************************
  Free the memory used by the vote structure.
**************************************************************************/
static void vote_free(struct vote *pvote)
{
  if (!pvote) {
    return;
  }

  if (pvote->votes_cast) {
    vote_cast_list_iterate(pvote->votes_cast, pvc) {
      free(pvc);
    } vote_cast_list_iterate_end;
    vote_cast_list_free(pvote->votes_cast);
    pvote->votes_cast = NULL;
  }
  free(pvote);
}

/**************************************************************************
  Check votes at end of turn.
**************************************************************************/
void voting_turn(void)
{
  vote_list_iterate(vote_list, pvote) {
    check_vote(pvote);
  } vote_list_iterate_end;
}

/**************************************************************************
  Free vote related data structures.
**************************************************************************/
void voting_free(void)
{
  clear_all_votes();
  vote_list_free(vote_list);
  vote_list = NULL;
}

/**************************************************************************
  Remove all votes.
**************************************************************************/
void clear_all_votes(void)
{
  if (!vote_list) {
    return;
  }

  vote_list_iterate(vote_list, pvote) {
    vote_free(pvote);
  } vote_list_iterate_end;
  vote_list_clear(vote_list);
}

/**************************************************************************
  Returns the vote associated to the given vote number.
**************************************************************************/
struct vote *voting_get_vote_by_no(int vote_no)
{
  if (!vote_list) {
    return NULL;
  }

  vote_list_iterate(vote_list, pvote) {
    if (pvote->vote_no == vote_no) {
      return pvote;
    }
  } vote_list_iterate_end;

  return NULL;
}

/**************************************************************************
  Find the vote made by the given user.
**************************************************************************/
struct vote *voting_get_vote_by_caller(const struct connection *caller)
{
  if (vote_list == NULL || caller == NULL) {
    return NULL;
  }

  vote_list_iterate(vote_list, pvote) {
    if (pvote->caller_id == caller->id) {
      return pvote;
    }
  } vote_list_iterate_end;

  return NULL;
}

/**************************************************************************
  Register a vote of type 'type' on the vote 'pvote' made by the user
  'pconn'.
**************************************************************************/
void connection_vote(struct connection *pconn,
                     struct vote *pvote,
                     enum vote_type type)
{
  assert(vote_list != NULL);

  struct vote_cast *pvc;

  if (!connection_can_vote(pconn)) {
    return;
  }

  /* Try to find a previous vote */
  if ((pvc = find_vote_cast(pvote, pconn->id))) {
    pvc->vote_cast = type;
  } else if ((pvc = vote_cast_new(pvote))) {
    pvc->vote_cast = type;
    pvc->conn_id = pconn->id;
  } else {
    /* Must never happen */
    assert(0);
  }
  check_vote(pvote);
}

/**************************************************************************
  Cancel the votes of a lost or a detached connection...
**************************************************************************/
void cancel_connection_votes(struct connection *pconn)
{
  if (!pconn || !vote_list) {
    return;
  }

  remove_vote(voting_get_vote_by_caller(pconn));

  vote_list_iterate(vote_list, pvote) {
    remove_vote_cast(pvote, find_vote_cast(pvote, pconn->id));
  } vote_list_iterate_end;
}

/**************************************************************************
  Create a new vote.
**************************************************************************/
struct vote *vote_new(struct connection *caller,
                             const char *full_command)
{
  struct vote *pvote;

  assert(vote_list != NULL);

  if (!connection_can_vote(caller)) {
    return NULL;
  }

  /* Cancel previous vote */
  remove_vote(voting_get_vote_by_caller(caller));

  /* Make a new vote */
  pvote = fc_calloc(1, sizeof(*pvote));
  pvote->caller_id = caller->id;

  sz_strlcpy(pvote->command, full_command);

  pvote->votes_cast = vote_cast_list_new();
  pvote->vote_no = ++vote_number_sequence;

  vote_list_append(vote_list, pvote);

  return pvote;
}

/**************************************************************************
  Find the vote cast for the user id conn_id in a vote.
**************************************************************************/
static struct vote_cast *find_vote_cast(struct vote *pvote, int conn_id)
{
  assert(vote_list != NULL);

  vote_cast_list_iterate(pvote->votes_cast, pvc) {
    if (pvc->conn_id == conn_id) {
      return pvc;
    }
  } vote_cast_list_iterate_end;

  return NULL;
}
/**************************************************************************
  Return a new vote cast.
**************************************************************************/
static struct vote_cast *vote_cast_new(struct vote *pvote)
{
  assert(vote_list != NULL);

  struct vote_cast *pvc = fc_malloc(sizeof(struct vote_cast));

  pvc->conn_id = -1;
  pvc->vote_cast = VOTE_ABSTAIN;

  vote_cast_list_append(pvote->votes_cast, pvc);

  return pvc;
}

/**************************************************************************
  Remove a vote cast.
**************************************************************************/
static void remove_vote_cast(struct vote *pvote, struct vote_cast *pvc)
{
  assert(vote_list != NULL);

  if (!pvc) {
    return;
  }

  vote_cast_list_unlink(pvote->votes_cast, pvc);
  free(pvc);
  check_vote(pvote);            /* Maybe can pass */
}

/**************************************************************************
  Return the number of currently active votes.
**************************************************************************/
int voting_get_vote_count(void)
{
  if (!vote_list) {
    return 0;
  }
  return vote_list_size(vote_list);
}

/**************************************************************************
  Return the command line that will be executed if this vote passes.
**************************************************************************/
const char *vote_cmdline(const struct vote *pvote)
{
  if (!pvote) {
    return NULL;
  }
  return pvote->command;
}

/**************************************************************************
  Return the number of votes cast of the given type.
**************************************************************************/
int vote_cast_count(const struct vote *pvote, enum vote_type vt)
{
  if (!pvote) {
    return 0;
  }
  switch (vt) {
  case VOTE_YES:
    return pvote->yes;
    break;
  case VOTE_NO:
    return pvote->no;
    break;
  case VOTE_ABSTAIN:
    return pvote->abstain;
    break;
  default:
    break;
  }
  return 0;
}

/**************************************************************************
  Return the unique identifier number for this vote.
**************************************************************************/
int vote_number(const struct vote *pvote)
{
  return pvote->vote_no;
}

/**************************************************************************
  Return a const pointer to the global vote list.
**************************************************************************/
const struct vote_list *voting_get_vote_list(void)
{
  return vote_list;
}

/**************************************************************************
  Returns the last vote that was made, or NULL if none. We use the last
  vote according to 'vote_number_sequence' rather than the last vote in
  'vote_list' so that users do not mistakenly vote for the wrong vote if
  the list is changed while they are voting.
**************************************************************************/
struct vote *voting_get_last_vote(void)
{
  return voting_get_vote_by_no(vote_number_sequence);
}
