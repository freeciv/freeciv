#ifndef __DIPTREATY__H
#define __DIPTREATY__H

enum clause_type { CLAUSE_ADVANCE, CLAUSE_GOLD, CLAUSE_MAP, CLAUSE_SEAMAP };

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
#endif
