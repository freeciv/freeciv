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

#include "game.h"
#include "log.h"
#include "mem.h"
#include "player.h"

#include "diptreaty.h"

/**************************************************************************
  Returns TRUE iff pplayer could do diplomancy in the game at all.
  These values are set by player in stdinhand.c.
**************************************************************************/
bool diplomacy_possible(struct player *pplayer, struct player *aplayer)
{
  return  (game.diplomacy == 0      /* Unlimited diplomacy */
	   || (game.diplomacy == 1  /* Human diplomacy only */
	       && !pplayer->ai.control 
	       && !aplayer->ai.control)
	   || (game.diplomacy == 2  /* AI diplomacy only */
	       && pplayer->ai.control
	       && aplayer->ai.control)
	   || (game.diplomacy == 3  /* Team diplomacy only */
	       && players_on_same_team(pplayer, aplayer)));
}

/**************************************************************************
  Returns TRUE iff pplayer could do diplomatic meetings with aplayer.
**************************************************************************/
bool could_meet_with_player(struct player *pplayer, struct player *aplayer)
{
  return (pplayer->is_alive
          && aplayer->is_alive
          && pplayer != aplayer
          && diplomacy_possible(pplayer,aplayer)
          && (player_has_embassy(aplayer, pplayer) 
              || player_has_embassy(pplayer, aplayer)
              || pplayer->diplstates[aplayer->player_no].contact_turns_left > 0
              || aplayer->diplstates[pplayer->player_no].contact_turns_left > 0)
          && (aplayer->is_connected || aplayer->ai.control)
          && (pplayer->is_connected || pplayer->ai.control));
}

/**************************************************************************
  Returns TRUE iff pplayer could do diplomatic meetings with aplayer.
**************************************************************************/
bool could_intel_with_player(struct player *pplayer, struct player *aplayer)
{
  return (pplayer->is_alive
          && aplayer->is_alive
          && pplayer != aplayer
          && (pplayer->diplstates[aplayer->player_no].contact_turns_left > 0
              || aplayer->diplstates[pplayer->player_no].contact_turns_left > 0
              || player_has_embassy(pplayer, aplayer)));
}

/****************************************************************
...
*****************************************************************/
void init_treaty(struct Treaty *ptreaty, 
		 struct player *plr0, struct player *plr1)
{
  ptreaty->plr0=plr0;
  ptreaty->plr1=plr1;
  ptreaty->accept0 = FALSE;
  ptreaty->accept1 = FALSE;
  clause_list_init(&ptreaty->clauses);
}

/****************************************************************
  Free the clauses of a treaty.
*****************************************************************/
void clear_treaty(struct Treaty *ptreaty)
{
  clause_list_iterate(ptreaty->clauses, pclause) {
    free(pclause);
  } clause_list_iterate_end;
  clause_list_unlink_all(&ptreaty->clauses);
}

/****************************************************************
...
*****************************************************************/
bool remove_clause(struct Treaty *ptreaty, struct player *pfrom, 
		  enum clause_type type, int val)
{
  clause_list_iterate(ptreaty->clauses, pclause) {
    if(pclause->type==type && pclause->from==pfrom &&
       pclause->value==val) {
      clause_list_unlink(&ptreaty->clauses, pclause);
      free(pclause);

      ptreaty->accept0 = FALSE;
      ptreaty->accept1 = FALSE;

      return TRUE;
    }
  } clause_list_iterate_end;

  return FALSE;
}


/****************************************************************
...
*****************************************************************/
bool add_clause(struct Treaty *ptreaty, struct player *pfrom, 
		enum clause_type type, int val)
{
  struct Clause *pclause;
  enum diplstate_type ds = 
                     pplayer_get_diplstate(ptreaty->plr0, ptreaty->plr1)->type;

  if (type < 0 || type >= CLAUSE_LAST) {
    freelog(LOG_ERROR, "Illegal clause type encountered.");
    return FALSE;
  }

  if (type == CLAUSE_ADVANCE && !tech_exists(val)) {
    freelog(LOG_ERROR, "Illegal tech value %i in clause.", val);
    return FALSE;
  }
  
  if (is_pact_clause(type)
      && ((ds == DS_PEACE && type == CLAUSE_PEACE)
          || (ds == DS_ALLIANCE && type == CLAUSE_ALLIANCE)
          || (ds == DS_CEASEFIRE && type == CLAUSE_CEASEFIRE))) {
    /* we already have this diplomatic state */
    freelog(LOG_ERROR, "Illegal treaty suggested between %s and %s - they "
                       "already have this treaty level.", ptreaty->plr0->name, 
                       ptreaty->plr1->name);
    return FALSE;
  }

  clause_list_iterate(ptreaty->clauses, pclause) {
    if(pclause->type==type
       && pclause->from==pfrom
       && pclause->value==val) {
      /* same clause already there */
      return FALSE;
    }
    if(is_pact_clause(type) &&
       is_pact_clause(pclause->type)) {
      /* pact clause already there */
      ptreaty->accept0 = FALSE;
      ptreaty->accept1 = FALSE;
      pclause->type=type;
      return TRUE;
    }
    if (type == CLAUSE_GOLD && pclause->type==CLAUSE_GOLD &&
        pclause->from==pfrom) {
      /* gold clause there, different value */
      ptreaty->accept0 = FALSE;
      ptreaty->accept1 = FALSE;
      pclause->value=val;
      return TRUE;
    }
  } clause_list_iterate_end;
   
  pclause=(struct Clause *)fc_malloc(sizeof(struct Clause));

  pclause->type=type;
  pclause->from=pfrom;
  pclause->value=val;
  
  clause_list_insert_back(&ptreaty->clauses, pclause);

  ptreaty->accept0 = FALSE;
  ptreaty->accept1 = FALSE;

  return TRUE;
}
