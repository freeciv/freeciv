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
#ifndef FC__GENLIST_H
#define FC__GENLIST_H

struct genlist_link {
  struct genlist_link *next, *prev; 
  void *dataptr;
};


struct genlist {
  int nelements;
  struct genlist_link null_link;
  struct genlist_link *head_link;
  struct genlist_link *tail_link;
};


struct genlist_iterator {
  struct genlist *list;
  struct genlist_link *link;
};


int genlist_size(struct genlist *pgenlist);
void *genlist_get(struct genlist *pgenlist, int inx);
void genlist_init(struct genlist *pgenlist);
void genlist_unlink_all(struct genlist *pgenlist);
void genlist_insert(struct genlist *pgenlist, void *data, int pos);
struct genlist_link *
find_genlist_position(struct genlist *pgenlist, int pos);
void genlist_unlink(struct genlist *pgenlist, void *punlink);

void genlist_sort(struct genlist *pgenlist,
		  int (*compar)(const void *, const void *));

void genlist_iterator_init(struct genlist_iterator *iter,
				struct genlist *pgenlist, int pos);

#define ITERATOR_PTR(X)  ((X).link->dataptr)
#define ITERATOR_NEXT(X) ((X).link=(X).link->next)
#define ITERATOR_PREV(X) ((X).link=(X).link->prev)


/* This is to iterate for a type defined like:
     struct unit_list { struct genlist list; };
   where the pointers in the list are really pointers to "atype".
   Eg, see speclist.h, which is what this is really for.
*/
#define TYPED_LIST_ITERATE(atype, typed_list, var) {       \
  struct genlist_iterator myiter;                          \
  atype *var;                                              \
  genlist_iterator_init(&myiter, &(typed_list).list, 0);   \
  for(; ITERATOR_PTR(myiter);) {                           \
    var=(atype *)ITERATOR_PTR(myiter);                     \
    ITERATOR_NEXT(myiter);

/* Balance for above: */ 
#define LIST_ITERATE_END  }}


/* Same, but iterate backwards: */
#define TYPED_LIST_ITERATE_REV(atype, typed_list, var) {   \
  struct genlist_iterator myiter;                          \
  atype *var;                                              \
  genlist_iterator_init(&myiter, &(typed_list).list, -1);  \
  for(; ITERATOR_PTR(myiter);) {                           \
    var=(atype *)ITERATOR_PTR(myiter);                     \
    ITERATOR_PREV(myiter);
 
/* Balance for above: */ 
#define LIST_ITERATE_REV_END  }}


#endif  /* FC__GENLIST_H */
