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

#include <genlist.h>
#include <mem.h>


/************************************************************************
  ...
************************************************************************/
void genlist_init(struct genlist *pgenlist)
{
  pgenlist->nelements=0;
  pgenlist->head_link=&pgenlist->null_link;
  pgenlist->tail_link=&pgenlist->null_link;
  pgenlist->null_link.next=&pgenlist->null_link;
  pgenlist->null_link.prev=&pgenlist->null_link;
  pgenlist->null_link.dataptr=0;
}


/************************************************************************
  ...
************************************************************************/
int genlist_size(struct genlist *pgenlist)
{
  return pgenlist->nelements;
}

/************************************************************************
  ...
************************************************************************/
void *genlist_get(struct genlist *pgenlist, int idx)
{
  struct genlist_link *link=find_genlist_position(pgenlist, idx);

  return link->dataptr;
}



/************************************************************************
  ...
************************************************************************/
void genlist_unlink_all(struct genlist *pgenlist)
{
  if(pgenlist->nelements) {
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
  ...
************************************************************************/
void genlist_unlink(struct genlist *pgenlist, void *punlink)
{
  if(pgenlist->nelements) {
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
  ...
************************************************************************/
void genlist_insert(struct genlist *pgenlist, void *data, int pos)
{
  if(!pgenlist->nelements) { /*list is empty, ignore pos */
    
    struct genlist_link *plink=fc_malloc(sizeof(struct genlist_link));

    plink->dataptr=data;
    plink->next=&pgenlist->null_link;
    plink->prev=&pgenlist->null_link;

    pgenlist->head_link=plink;
    pgenlist->tail_link=plink;

  }
  else {
    struct genlist_link *plink=fc_malloc(sizeof(struct genlist_link));
    plink->dataptr=data;


    if(pos==0) {
      plink->next=pgenlist->head_link;
      plink->prev=&pgenlist->null_link;
      pgenlist->head_link->prev=plink;
      pgenlist->head_link=plink;
    }
    else if(pos==-1) {
      plink->next=&pgenlist->null_link;
      plink->prev=pgenlist->tail_link;
      pgenlist->tail_link->next=plink;
      pgenlist->tail_link=plink;
    }
    else {
      struct genlist_link *pre_insert_link; 
      pre_insert_link=find_genlist_position(pgenlist, pos);
      pre_insert_link->next->prev=plink;
    }
  }

  pgenlist->nelements++;
}

/************************************************************************
  ...
************************************************************************/
void genlist_iterator_init(struct genlist_iterator *iter,
				struct genlist *pgenlist, int pos)
{
  iter->list=pgenlist;
  iter->link=find_genlist_position(pgenlist, pos);
}





/************************************************************************
  ...
************************************************************************/
struct genlist_link *
find_genlist_position(struct genlist *pgenlist, int pos)
{
  struct genlist_link *plink;


  if(pos==0)
    return pgenlist->head_link;
  else if(pos==-1)
    return pgenlist->tail_link;
  else if(pos>=pgenlist->nelements)
    return &pgenlist->null_link;

  if(pos<pgenlist->nelements/2)   /* fastest to do forward search */
    for(plink=pgenlist->head_link; pos; pos--)
      plink=plink->next;
 
  else                           /* fastest to do backward search */
    for(plink=pgenlist->tail_link,pos=pgenlist->nelements-pos-1; pos; pos--)
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
    sortbuf = 0;
    n_alloc = 0;
    return;
  }

  n = genlist_size(pgenlist);
  if(n <= 1) {
    return;
  }
  if(n > n_alloc) {
    n_alloc = n+10;
    sortbuf = fc_realloc(sortbuf, n_alloc*sizeof(void*));
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

