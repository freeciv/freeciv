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
#include <stdio.h>
#include <stdlib.h>

#include "genlist.h"
#include "log.h"
#include "mem.h"
#include "player.h"

#include "diptreaty.h"

/****************************************************************
...
*****************************************************************/
void init_treaty(struct Treaty *ptreaty, 
		 struct player *plr0, struct player *plr1)
{
  ptreaty->plr0=plr0;
  ptreaty->plr1=plr1;
  ptreaty->accept0=0;
  ptreaty->accept1=0;
  genlist_init(&ptreaty->clauses);
}


/****************************************************************
...
*****************************************************************/
int remove_clause(struct Treaty *ptreaty, struct player *pfrom, 
		  enum clause_type type, int val)
{
  struct genlist_iterator myiter;
  
  genlist_iterator_init(&myiter, &ptreaty->clauses, 0);
  
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);
    if(pclause->type==type && pclause->from==pfrom &&
       pclause->value==val) {
      genlist_unlink(&ptreaty->clauses, pclause);
      free(pclause);

      ptreaty->accept0=0;
      ptreaty->accept1=0;

      return 1;
    }
  }

  return 0;
}


/****************************************************************
...
*****************************************************************/
int add_clause(struct Treaty *ptreaty, struct player *pfrom, 
		enum clause_type type, int val)
{
  struct Clause *pclause;
  struct genlist_iterator myiter;

  if (type == CLAUSE_ADVANCE && !tech_exists(val)) {
    freelog(LOG_ERROR, "Illegal tech value %i in clause.", val);
    return 0;
  }
  
  genlist_iterator_init(&myiter, &ptreaty->clauses, 0);
  
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);
    if(pclause->type==type
       && pclause->from==pfrom
       && pclause->value==val) {
      /* same clause already there */
      return 0;
    }
    if(is_pact_clause(type) &&
       is_pact_clause(pclause->type)) {
      /* pact clause already there */
      ptreaty->accept0=0;
      ptreaty->accept1=0;
      pclause->type=type;
      return 1;
    }
    if (type == CLAUSE_GOLD && pclause->type==CLAUSE_GOLD &&
        pclause->from==pfrom) {
      /* gold clause there, different value */
      ptreaty->accept0=0;
      ptreaty->accept1=0;
      pclause->value=val;
      return 1;
    }
  }
   
  pclause=fc_malloc(sizeof(struct Clause));

  pclause->type=type;
  pclause->from=pfrom;
  pclause->value=val;
  
  genlist_insert(&ptreaty->clauses, pclause, -1);

  ptreaty->accept0=0;
  ptreaty->accept1=0;

  return 1;
}
