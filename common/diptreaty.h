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
#ifndef FC__DIPTREATY_H
#define FC__DIPTREATY_H

#include "genlist.h"

enum clause_type { CLAUSE_ADVANCE, CLAUSE_GOLD, CLAUSE_MAP,
		   CLAUSE_SEAMAP, CLAUSE_CITY, 
		   CLAUSE_CEASEFIRE, CLAUSE_PEACE, CLAUSE_ALLIANCE,
		   CLAUSE_VISION};

#define is_pact_clause(x) \
  ((x == CLAUSE_CEASEFIRE) || (x == CLAUSE_PEACE) || (x == CLAUSE_ALLIANCE))

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

struct Treaty {
  struct player *plr0, *plr1;
  int accept0, accept1;
  struct clause_list clauses;
};

void init_treaty(struct Treaty *ptreaty, 
		 struct player *plr0, struct player *plr1);
int add_clause(struct Treaty *ptreaty, struct player *pfrom, 
	       enum clause_type type, int val);
int remove_clause(struct Treaty *ptreaty, struct player *pfrom, 
		  enum clause_type type, int val);

#endif /* FC__DIPTREATY_H */
