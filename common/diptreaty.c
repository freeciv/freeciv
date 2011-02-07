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
#include <fc_config.h>
#endif

/* utility */
#include "log.h"
#include "mem.h"

/* common */
#include "game.h"
#include "player.h"

#include "diptreaty.h"

/****************************************************************************
  Returns TRUE iff pplayer could do diplomancy in the game at all.
****************************************************************************/
bool diplomacy_possible(const struct player *pplayer1,
                        const struct player *pplayer2)
{
  switch (game.info.diplomacy) {
  case DIPLO_FOR_ALL:
    return TRUE;
  case DIPLO_FOR_HUMANS:
    return (!pplayer1->ai_controlled && !pplayer2->ai_controlled);
  case DIPLO_FOR_AIS:
    return (pplayer1->ai_controlled && pplayer2->ai_controlled);
  case DIPLO_FOR_TEAMS:
    return players_on_same_team(pplayer1, pplayer2);
  case DIPLO_DISABLED:
    return FALSE;
  }
  log_error("%s(): Unsupported diplomacy variant %d.",
            __FUNCTION__, game.info.diplomacy);
  return FALSE;
}

/**************************************************************************
  Returns TRUE iff pplayer could do diplomatic meetings with aplayer.
**************************************************************************/
bool could_meet_with_player(const struct player *pplayer,
			    const struct player *aplayer)
{
  return (pplayer->is_alive
          && aplayer->is_alive
          && pplayer != aplayer
          && diplomacy_possible(pplayer,aplayer)
          && (player_has_embassy(aplayer, pplayer) 
              || player_has_embassy(pplayer, aplayer)
              || player_diplstate_get(pplayer, aplayer)->contact_turns_left
                 > 0
              || player_diplstate_get(aplayer, pplayer)->contact_turns_left
                 > 0));
}

/**************************************************************************
  Returns TRUE iff pplayer could do diplomatic meetings with aplayer.
**************************************************************************/
bool could_intel_with_player(const struct player *pplayer,
			     const struct player *aplayer)
{
  return (pplayer->is_alive
          && aplayer->is_alive
          && pplayer != aplayer
          && (player_diplstate_get(pplayer, aplayer)->contact_turns_left > 0
              || player_diplstate_get(aplayer, pplayer)->contact_turns_left
                 > 0
              || player_has_embassy(pplayer, aplayer)));
}

/****************************************************************
  Initialize treaty structure between two players.
*****************************************************************/
void init_treaty(struct Treaty *ptreaty, 
		 struct player *plr0, struct player *plr1)
{
  ptreaty->plr0=plr0;
  ptreaty->plr1=plr1;
  ptreaty->accept0 = FALSE;
  ptreaty->accept1 = FALSE;
  ptreaty->clauses = clause_list_new();
}

/****************************************************************
  Free the clauses of a treaty.
*****************************************************************/
void clear_treaty(struct Treaty *ptreaty)
{
  clause_list_iterate(ptreaty->clauses, pclause) {
    free(pclause);
  } clause_list_iterate_end;
  clause_list_destroy(ptreaty->clauses);
}

/****************************************************************
  Remove clause from treaty
*****************************************************************/
bool remove_clause(struct Treaty *ptreaty, struct player *pfrom, 
		  enum clause_type type, int val)
{
  clause_list_iterate(ptreaty->clauses, pclause) {
    if(pclause->type==type && pclause->from==pfrom &&
       pclause->value==val) {
      clause_list_remove(ptreaty->clauses, pclause);
      free(pclause);

      ptreaty->accept0 = FALSE;
      ptreaty->accept1 = FALSE;

      return TRUE;
    }
  } clause_list_iterate_end;

  return FALSE;
}


/****************************************************************
  Add clause to treaty.
*****************************************************************/
bool add_clause(struct Treaty *ptreaty, struct player *pfrom, 
		enum clause_type type, int val)
{
  struct player *pto = (pfrom == ptreaty->plr0
                        ? ptreaty->plr1 : ptreaty->plr0);
  struct Clause *pclause;
  enum diplstate_type ds
    = player_diplstate_get(ptreaty->plr0, ptreaty->plr1)->type;

  if (type < 0 || type >= CLAUSE_LAST) {
    log_error("Illegal clause type encountered.");
    return FALSE;
  }

  if (type == CLAUSE_ADVANCE && !valid_advance_by_number(val)) {
    log_error("Illegal tech value %i in clause.", val);
    return FALSE;
  }
  
  if (is_pact_clause(type)
      && ((ds == DS_PEACE && type == CLAUSE_PEACE)
          || (ds == DS_ARMISTICE && type == CLAUSE_PEACE)
          || (ds == DS_ALLIANCE && type == CLAUSE_ALLIANCE)
          || (ds == DS_CEASEFIRE && type == CLAUSE_CEASEFIRE))) {
    /* we already have this diplomatic state */
    log_error("Illegal treaty suggested between %s and %s - they "
              "already have this treaty level.",
              nation_rule_name(nation_of_player(ptreaty->plr0)), 
              nation_rule_name(nation_of_player(ptreaty->plr1)));
    return FALSE;
  }

  if (type == CLAUSE_EMBASSY && player_has_real_embassy(pto, pfrom)) {
    /* we already have embassy */
    log_error("Illegal embassy clause: %s already have embassy with %s.",
              nation_rule_name(nation_of_player(pto)),
              nation_rule_name(nation_of_player(pfrom)));
    return FALSE;
  }

  if (!game.info.trading_gold && type == CLAUSE_GOLD) {
    return FALSE;
  }
  if (!game.info.trading_tech && type == CLAUSE_ADVANCE) {
    return FALSE;
  }
  if (!game.info.trading_city && type == CLAUSE_CITY) {
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
   
  pclause = fc_malloc(sizeof(*pclause));

  pclause->type=type;
  pclause->from=pfrom;
  pclause->value=val;
  
  clause_list_append(ptreaty->clauses, pclause);

  ptreaty->accept0 = FALSE;
  ptreaty->accept1 = FALSE;

  return TRUE;
}
