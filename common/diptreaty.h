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
		   CLAUSE_SEAMAP, CLAUSE_CITY};

struct Clause {
  enum clause_type type;
  struct player *from;
  int value;
};

struct Treaty {
  struct player *plr0, *plr1;
  int accept0, accept1;
  struct genlist clauses;
};

void init_treaty(struct Treaty *ptreaty, 
		 struct player *plr0, struct player *plr1);
int add_clause(struct Treaty *ptreaty, struct player *pfrom, 
	       enum clause_type type, int val);
int remove_clause(struct Treaty *ptreaty, struct player *pfrom, 
		  enum clause_type type, int val);

#endif /* FC__DIPTREATY_H */
