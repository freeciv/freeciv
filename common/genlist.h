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

/********************************************************************** 
  MODULE: genlist

  A "genlist" is a generic doubly-linked list.  That is:
    generic:        stores (void*) pointers to arbitrary user data;
    doubly-linked:  can be efficiently traversed both "forwards"
                    and "backwards".
		    
  The list data structures are allocated dynamically, and list
  elements can be added or removed at arbitrary positions.
  
  Positions in the list are specified starting from 0, up to n-1
  for a list with n elements.  The position -1 can be used to
  refer to the last element (that is, the same as n-1, or n when
  adding a new element), but other negative numbers are not
  meaningful.

  There are two memory management issues:
  
  - The user-data pointed to by the genlist elements; these are
    entirely the user's responsibility, and the genlist code just
    treats these as opaque data, not doing any allocation or freeing.
    
  - The data structures used internally by the genlist to store
    data for the links etc.  These are allocated by genlist_insert(),
    and freed by genlist_unlink() and genlist_unlink_all().  That is,
    it is the responsibility of the user to call the unlink functions
    as necessary to avoid memory leaks.

  This module also contains a "genlist_iterator" type and associated
  macros to assist in traversing genlists.  A trap to beware of with
  iterators is modifying the list while the iterator is active, in
  particular removing the next element pointed to by the iterator
  (see further comments below).

  See also the speclist module.
***********************************************************************/

/* A single element of a genlist, storing the pointer to user
   data, and pointers to the next and previous elements:
*/
struct genlist_link {
  struct genlist_link *next, *prev; 
  void *dataptr;
};


/* A genlist, storing the number of elements (for quick retrieval and
   testing for empty lists), and pointers to the first and last elements
   of the list.  The address of the null_link is used to "terminate"
   the ends of the list, and head_link and tail_link initially point
   to null_link.  null_link always has dataptr==0.  (This is used to
   conveniently deal with "out of range" results etc, though I'm not
   sure if it really gives a big advantage compared to a scheme just
   using NULL as a terminator/flag value  --dwp)
*/
struct genlist {
  int nelements;
  struct genlist_link null_link;
  struct genlist_link *head_link;
  struct genlist_link *tail_link;
};


/* Used to iterate genlists: a pointer to the list we are iterating
   (although I don't think this is ever used?), and a pointer to the
   current position.  You can use the ITERATOR_* macros below to
   access the data of the current link and traverse the list.
   Be very careful if the genlist is modified while the iteration
   is in progress.  The standard macros (based on TYPED_LIST_ITERATE
   below) allow removing the "current" element, as the iterator
   is advances at the start of the loop, but if the "next" element
   is removed (including as a side effect of removing the current
   element), expect core dumps.  See server/unitfunc.c:wipe_unit_safe()
   as an example of protecting against this.
*/
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
