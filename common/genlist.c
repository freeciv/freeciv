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
#include <stdlib.h>

#include "mem.h"
#include "genlist.h"


/************************************************************************
  Initialize a genlist.
  This should be called before the genlist is used in any other way.
************************************************************************/
void genlist_init(struct genlist *pgenlist)
{
  pgenlist->nelements=0;
  pgenlist->head_link=&pgenlist->null_link;
  pgenlist->tail_link=&pgenlist->null_link;
  pgenlist->null_link.next=&pgenlist->null_link;
  pgenlist->null_link.prev=&pgenlist->null_link;
  pgenlist->null_link.dataptr = NULL;
}


/************************************************************************
  Returns the number of elements stored in the genlist.
************************************************************************/
int genlist_size(struct genlist *pgenlist)
{
  return pgenlist->nelements;
}


/************************************************************************
  Returns the user-data pointer stored in the genlist at the position
  given by 'idx'.  For idx out of range (including an empty list),
  returns 0 (the dataptr of null_link).
  Recall 'idx' can be -1 meaning the last element.
************************************************************************/
void *genlist_get(struct genlist *pgenlist, int idx)
{
  struct genlist_link *link=find_genlist_position(pgenlist, idx);

  return link->dataptr;
}


/************************************************************************
  Frees all the internal data used by the genlist (but doesn't touch
  the user-data).  At the end the state of the genlist will be the
  same as when genlist_init() is called on a new genlist.
************************************************************************/
void genlist_unlink_all(struct genlist *pgenlist)
{
  if(pgenlist->nelements > 0) {
    struct genlist_link *plink=pgenlist->head_link, *plink2;

    do {
      plink2=plink->next;
      free(plink);
    } while((plink=plink2)!=&pgenlist->null_link);

    pgenlist->head_link=&pgenlist->null_link;
    pgenlist->tail_link=&pgenlist->null_link;

    pgenlist->nelements=0;
  }
}


/************************************************************************
  Remove an element of the genlist with the specified user-data pointer
  given by 'punlink'.  If there is no such element, does nothing.
  If there are multiple such elements, removes the first one.
************************************************************************/
void genlist_unlink(struct genlist *pgenlist, void *punlink)
{
  if(pgenlist->nelements > 0) {
    struct genlist_link *plink=pgenlist->head_link;
    
    while(plink!=&pgenlist->null_link && plink->dataptr!=punlink)
       plink=plink->next;
    
    if(plink->dataptr==punlink) {
      if(pgenlist->head_link==plink)
	 pgenlist->head_link=plink->next;
      else
	 plink->prev->next=plink->next;

      if(pgenlist->tail_link==plink)
	 pgenlist->tail_link=plink->prev;
      else
	 plink->next->prev=plink->prev;
      free(plink);
      pgenlist->nelements--;
    }
  }
}


/************************************************************************
  Insert a new element in the list, at position 'pos', with the specified
  user-data pointer 'data'.  Existing elements at >= pos are moved one
  space to the "right".  Recall 'pos' can be -1 meaning add at the
  end of the list.  For an empty list pos has no effect.
  A bad 'pos' value for a non-empty list is treated as -1 (is this
  a good idea?)
************************************************************************/
void genlist_insert(struct genlist *pgenlist, void *data, int pos)
{
  if(pgenlist->nelements == 0) { /*list is empty, ignore pos */
    
    struct genlist_link *plink=(struct genlist_link *)
      	      	      	      	  fc_malloc(sizeof(struct genlist_link));

    plink->dataptr=data;
    plink->next=&pgenlist->null_link;
    plink->prev=&pgenlist->null_link;

    pgenlist->head_link=plink;
    pgenlist->tail_link=plink;

  }
  else {
    struct genlist_link *plink=(struct genlist_link *)
      	      	      	      	  fc_malloc(sizeof(struct genlist_link));
    plink->dataptr=data;

    if(pos==0) {
      plink->next=pgenlist->head_link;
      plink->prev=&pgenlist->null_link;
      pgenlist->head_link->prev=plink;
      pgenlist->head_link=plink;
    }
    else if(pos<=-1 || pos>=pgenlist->nelements) {
      plink->next=&pgenlist->null_link;
      plink->prev=pgenlist->tail_link;
      pgenlist->tail_link->next=plink;
      pgenlist->tail_link=plink;
    }
    else {
      struct genlist_link *left, *right;     /* left and right of new element */
      right = find_genlist_position(pgenlist, pos);
      left = right->prev;
      plink->next = right;
      plink->prev = left;
      right->prev = plink;
      left->next = plink;
    }
  }

  pgenlist->nelements++;
}

/************************************************************************
  Initialize a genlist_iterator, for specified genlist and position
  of initial element.  If pos is out of range the link will be null_link
  (which will generally be interpreted as the iterator being finished).
  Recall 'pos' can be -1 meaning the last element.
************************************************************************/
void genlist_iterator_init(struct genlist_iterator *iter,
				struct genlist *pgenlist, int pos)
{
  iter->list=pgenlist;
  iter->link=find_genlist_position(pgenlist, pos);
}


/************************************************************************
  Returns a pointer to the genlist link structure at the specified
  position.  Recall 'pos' -1 refers to the last position.
  For pos out of range returns the null_link.
  Traverses list either forwards or backwards for best efficiency.
************************************************************************/
struct genlist_link *
find_genlist_position(struct genlist *pgenlist, int pos)
{
  struct genlist_link *plink;

  if(pos==0)
    return pgenlist->head_link;
  else if(pos==-1)
    return pgenlist->tail_link;
  else if(pos<-1 || pos>=pgenlist->nelements)
    return &pgenlist->null_link;

  if(pos<pgenlist->nelements/2)   /* fastest to do forward search */
    for(plink=pgenlist->head_link; pos != 0; pos--)
      plink=plink->next;
 
  else                           /* fastest to do backward search */
    for(plink=pgenlist->tail_link,pos=pgenlist->nelements-pos-1; pos != 0; pos--)
      plink=plink->prev;
 
  return plink;
}


/************************************************************************
 Sort the elements of a genlist.
 
 The comparison function should be a function useable by qsort; note
 that the const void * arguments to compar should really be "pointers to
 void*", where the void* being pointed to are the genlist dataptrs.
 That is, there are two levels of indirection.
 To do the sort we first construct an array of pointers corresponding
 the the genlist dataptrs, then sort those and put them back into
 the genlist.  This function will be called many times, so we use
 a static pointed to realloc-ed memory.  Calling this function with
 compar==NULL will free the memory and do nothing else.
************************************************************************/
void genlist_sort(struct genlist *pgenlist,
		  int (*compar)(const void *, const void *))
{
  static void **sortbuf = 0;
  static int n_alloc = 0;
  
  struct genlist_iterator myiter;
  int i, n;

  if(compar==NULL) {
    free(sortbuf);
    sortbuf = NULL;
    n_alloc = 0;
    return;
  }

  n = genlist_size(pgenlist);
  if(n <= 1) {
    return;
  }
  if(n > n_alloc) {
    n_alloc = n+10;
    sortbuf = (void **)fc_realloc(sortbuf, n_alloc*sizeof(void*));
  }
  
  genlist_iterator_init(&myiter, pgenlist, 0);
  for(i=0; i<n; i++, ITERATOR_NEXT(myiter)) {
    sortbuf[i] = ITERATOR_PTR(myiter);
  }
  
  qsort(sortbuf, n, sizeof(void*), compar);
  
  genlist_iterator_init(&myiter, pgenlist, 0);
  for(i=0; i<n; i++, ITERATOR_NEXT(myiter)) {
     ITERATOR_PTR(myiter) = sortbuf[i];
  }
}
