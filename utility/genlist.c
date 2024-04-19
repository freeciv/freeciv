/***********************************************************************
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

#include <stdlib.h>

/* utility */
#include "fcthread.h"
#include "log.h"
#include "mem.h"
#include "shared.h"  /* array_shuffle() */

#include "genlist.h"

/************************************************************************//**
  Create a new empty genlist.
****************************************************************************/
struct genlist *genlist_new(void)
{
  return genlist_new_full(nullptr);
}

/************************************************************************//**
  Create a new empty genlist with a free data function.
****************************************************************************/
struct genlist *genlist_new_full(genlist_free_fn_t free_data_func)
{
  struct genlist *pgenlist = fc_calloc(1, sizeof(*pgenlist));

#ifdef ZERO_VARIABLES_FOR_SEARCHING
  pgenlist->nelements = 0;
  pgenlist->head_link = nullptr;
  pgenlist->tail_link = nullptr;
#endif /* ZERO_VARIABLES_FOR_SEARCHING */
  fc_mutex_init(&pgenlist->mutex);
  pgenlist->free_data_func = free_data_func;

  return pgenlist;
}

/************************************************************************//**
  Destroys the genlist.
****************************************************************************/
void genlist_destroy(struct genlist *pgenlist)
{
  if (pgenlist == nullptr) {
    return;
  }

  genlist_clear(pgenlist);
  fc_mutex_destroy(&pgenlist->mutex);
  free(pgenlist);
}

/************************************************************************//**
  Create a new link.
****************************************************************************/
static void genlist_link_new(struct genlist *pgenlist, void *dataptr,
                             struct genlist_link *prev,
                             struct genlist_link *next)
{
  struct genlist_link *plink = fc_malloc(sizeof(*plink));

  plink->dataptr = dataptr;
  plink->prev = prev;
  if (prev != nullptr) {
    prev->next = plink;
  } else {
    pgenlist->head_link = plink;
  }
  plink->next = next;
  if (next != nullptr) {
    next->prev = plink;
  } else {
    pgenlist->tail_link = plink;
  }
  pgenlist->nelements++;
}

/************************************************************************//**
  Free a link.
****************************************************************************/
static void genlist_link_destroy(struct genlist *pgenlist,
                                 struct genlist_link *plink)
{
  if (pgenlist->head_link == plink) {
    pgenlist->head_link = plink->next;
  } else {
    plink->prev->next = plink->next;
  }

  if (pgenlist->tail_link == plink) {
    pgenlist->tail_link = plink->prev;
  } else {
    plink->next->prev = plink->prev;
  }

  pgenlist->nelements--;

  /* NB: Detach the link before calling the free function for avoiding
   * re-entrant code. */
  if (pgenlist->free_data_func != nullptr) {
    pgenlist->free_data_func(plink->dataptr);
  }
  free(plink);
}

/************************************************************************//**
  Returns a pointer to the genlist link structure at the specified
  position.  Recall 'pos' -1 refers to the last position.
  For pos out of range returns nullptr.
  Traverses list either forwards or backwards for best efficiency.
****************************************************************************/
static struct genlist_link *
genlist_link_at_pos(const struct genlist *pgenlist, int pos)
{
  struct genlist_link *plink;

  if (pos == 0) {
    return pgenlist->head_link;
  } else if (pos == -1) {
    return pgenlist->tail_link;
  } else if (pos < -1 || pos >= pgenlist->nelements) {
    return nullptr;
  }

  if (pos < pgenlist->nelements / 2) {  /* Fastest to do forward search */
    for (plink = pgenlist->head_link; pos != 0; pos--) {
      plink = plink->next;
    }
  } else {                              /* Fastest to do backward search */
    for (plink = pgenlist->tail_link, pos = pgenlist->nelements-pos - 1;
         pos != 0; pos--) {
      plink = plink->prev;
    }
  }

  return plink;
}

/************************************************************************//**
  Returns a new genlist that's a copy of the existing one.
****************************************************************************/
struct genlist *genlist_copy(const struct genlist *pgenlist)
{
  return genlist_copy_full(pgenlist, nullptr, pgenlist->free_data_func);
}

/************************************************************************//**
  Returns a new genlist that's a copy of the existing one.
****************************************************************************/
struct genlist *genlist_copy_full(const struct genlist *pgenlist,
                                  genlist_copy_fn_t copy_data_func,
                                  genlist_free_fn_t free_data_func)
{
  struct genlist *pcopy = genlist_new_full(free_data_func);

  if (pgenlist) {
    struct genlist_link *plink;

    if (copy_data_func != nullptr) {
      for (plink = pgenlist->head_link; plink; plink = plink->next) {
        genlist_link_new(pcopy, copy_data_func(plink->dataptr),
                         pcopy->tail_link, nullptr);
      }
    } else {
      for (plink = pgenlist->head_link; plink; plink = plink->next) {
        genlist_link_new(pcopy, plink->dataptr, pcopy->tail_link, nullptr);
      }
    }
  }

  return pcopy;
}

/************************************************************************//**
  Returns the number of elements stored in the genlist.
****************************************************************************/
int genlist_size(const struct genlist *pgenlist)
{
  return pgenlist->nelements;
}

/************************************************************************//**
  Returns the link in the genlist at the position given by 'idx'. For idx
  out of range (including an empty list), returns nullptr.
  Recall 'idx' can be -1 meaning the last element.
****************************************************************************/
struct genlist_link *genlist_link_get(const struct genlist *pgenlist, int idx)
{
  return genlist_link_at_pos(pgenlist, idx);
}

/************************************************************************//**
  Returns the tail link of the genlist.
****************************************************************************/
struct genlist_link *genlist_tail(const struct genlist *pgenlist)
{
  return (pgenlist != nullptr ? pgenlist->tail_link : nullptr);
}

/************************************************************************//**
  Returns the user-data pointer stored in the genlist at the position
  given by 'idx'.  For idx out of range (including an empty list),
  returns nullptr.
  Recall 'idx' can be -1 meaning the last element.
****************************************************************************/
void *genlist_get(const struct genlist *pgenlist, int idx)
{
  return genlist_link_data(genlist_link_get(pgenlist, idx));
}

/************************************************************************//**
  Returns the user-data pointer stored in the first element of the genlist.
****************************************************************************/
void *genlist_front(const struct genlist *pgenlist)
{
  return genlist_link_data(genlist_head(pgenlist));
}

/************************************************************************//**
  Returns the user-data pointer stored in the last element of the genlist.
****************************************************************************/
void *genlist_back(const struct genlist *pgenlist)
{
  return genlist_link_data(genlist_tail(pgenlist));
}

/************************************************************************//**
  Frees all the internal data used by the genlist (but doesn't touch
  the user-data).  At the end the state of the genlist will be the
  same as when genlist_init() is called on a new genlist.
****************************************************************************/
void genlist_clear(struct genlist *pgenlist)
{
  if (0 < pgenlist->nelements) {
    genlist_free_fn_t free_data_func = pgenlist->free_data_func;
    struct genlist_link *plink = pgenlist->head_link, *plink2;

    pgenlist->head_link = nullptr;
    pgenlist->tail_link = nullptr;

    pgenlist->nelements = 0;

    if (free_data_func != nullptr) {
      do {
        plink2 = plink->next;
        free_data_func(plink->dataptr);
        free(plink);
      } while ((plink = plink2) != nullptr);
    } else {
      do {
        plink2 = plink->next;
        free(plink);
      } while ((plink = plink2) != nullptr);
    }
  }
}

/************************************************************************//**
  Remove all duplicates of element from every consecutive group of equal
  elements in the list.
****************************************************************************/
void genlist_unique(struct genlist *pgenlist)
{
  genlist_unique_full(pgenlist, nullptr);
}

/************************************************************************//**
  Remove all duplicates of element from every consecutive group of equal
  elements in the list (equality is assumed from if the compare function
  return TRUE).
****************************************************************************/
void genlist_unique_full(struct genlist *pgenlist,
                         genlist_comp_fn_t comp_data_func)
{
  if (2 <= pgenlist->nelements) {
    struct genlist_link *plink = pgenlist->head_link, *plink2;

    if (comp_data_func != nullptr) {
      do {
        plink2 = plink->next;
        if (plink2 != nullptr && comp_data_func(plink->dataptr,
                                                plink2->dataptr)) {
          /* Remove this element. */
          genlist_link_destroy(pgenlist, plink);
        }
      } while ((plink = plink2) != nullptr);
    } else {
      do {
        plink2 = plink->next;
        if (plink2 != nullptr && plink->dataptr == plink2->dataptr) {
          /* Remove this element. */
          genlist_link_destroy(pgenlist, plink);
        }
      } while ((plink = plink2) != nullptr);
    }
  }
}

/************************************************************************//**
  Remove an element of the genlist with the specified user-data pointer
  given by 'punlink'. If there is no such element, does nothing.
  If there are multiple such elements, removes the first one. Return
  TRUE if one element has been removed.
  See also, genlist_remove_all(), genlist_remove_if() and
  genlist_remove_all_if().
****************************************************************************/
bool genlist_remove(struct genlist *pgenlist, const void *punlink)
{
  struct genlist_link *plink;

  for (plink = pgenlist->head_link; plink != nullptr; plink = plink->next) {
    if (plink->dataptr == punlink) {
      genlist_link_destroy(pgenlist, plink);
      return TRUE;
    }
  }

  return FALSE;
}

/************************************************************************//**
  Remove all elements of the genlist with the specified user-data pointer
  given by 'punlink'. Return the number of removed elements.
  See also, genlist_remove(), genlist_remove_if() and
  genlist_remove_all_if().
****************************************************************************/
int genlist_remove_all(struct genlist *pgenlist, const void *punlink)
{
  struct genlist_link *plink;
  int count = 0;

  for (plink = pgenlist->head_link; plink != nullptr;) {
    if (plink->dataptr == punlink) {
      struct genlist_link *pnext = plink->next;

      genlist_link_destroy(pgenlist, plink);
      count++;
      plink = pnext;
    } else {
      plink = plink->next;
    }
  }

  return count;
}

/************************************************************************//**
  Remove the first element of the genlist which fit the function (the
  function return TRUE). Return TRUE if one element has been removed.
  See also, genlist_remove(), genlist_remove_all(), and
  genlist_remove_all_if().
****************************************************************************/
bool genlist_remove_if(struct genlist *pgenlist,
                       genlist_cond_fn_t cond_data_func)
{
  if (cond_data_func != nullptr) {
    struct genlist_link *plink = pgenlist->head_link;

    for (; plink != nullptr; plink = plink->next) {
      if (cond_data_func(plink->dataptr)) {
        genlist_link_destroy(pgenlist, plink);
        return TRUE;
      }
    }
  }

  return FALSE;
}

/************************************************************************//**
  Remove all elements of the genlist which fit the function (the
  function return TRUE). Return the number of removed elements.
  See also, genlist_remove(), genlist_remove_all(), and
  genlist_remove_if().
****************************************************************************/
int genlist_remove_all_if(struct genlist *pgenlist,
                          genlist_cond_fn_t cond_data_func)
{
  if (cond_data_func != nullptr) {
    struct genlist_link *plink = pgenlist->head_link;
    int count = 0;

    while (plink != nullptr) {
      if (cond_data_func(plink->dataptr)) {
        struct genlist_link *pnext = plink->next;

        genlist_link_destroy(pgenlist, plink);
        count++;
        plink = pnext;
      } else {
        plink = plink->next;
      }
    }
    return count;
  }

  return 0;
}

/************************************************************************//**
  Remove the element pointed to plink.

  NB: After calling this function 'plink' is no more usable. You should
  have saved the next or previous link before.
****************************************************************************/
void genlist_erase(struct genlist *pgenlist, struct genlist_link *plink)
{
  if (plink != nullptr) {
    genlist_link_destroy(pgenlist, plink);
  }
}

/************************************************************************//**
  Remove the first element of the genlist.
****************************************************************************/
void genlist_pop_front(struct genlist *pgenlist)
{
  if (pgenlist->head_link != nullptr) {
    genlist_link_destroy(pgenlist, pgenlist->head_link);
  }
}

/************************************************************************//**
  Remove the last element of the genlist.
****************************************************************************/
void genlist_pop_back(struct genlist *pgenlist)
{
  if (pgenlist->tail_link != nullptr) {
    genlist_link_destroy(pgenlist, pgenlist->tail_link);
  }
}

/************************************************************************//**
  Insert a new element in the list, at position 'pos', with the specified
  user-data pointer 'data'.  Existing elements at >= pos are moved one
  space to the "right".  Recall 'pos' can be -1 meaning add at the
  end of the list.  For an empty list pos has no effect.
  A bad 'pos' value for a non-empty list is treated as -1 (is this
  a good idea?)
****************************************************************************/
void genlist_insert(struct genlist *pgenlist, void *data, int pos)
{
  if (0 == pgenlist->nelements) {
    /* List is empty, ignore pos. */
    genlist_link_new(pgenlist, data, nullptr, nullptr);
  } else if (0 == pos) {
    /* Prepend. */
    genlist_link_new(pgenlist, data, nullptr, pgenlist->head_link);
  } else if (-1 >= pos || pos >= pgenlist->nelements) {
    /* Append. */
    genlist_link_new(pgenlist, data, pgenlist->tail_link, nullptr);
  } else {
    /* Insert before plink. */
    struct genlist_link *plink = genlist_link_at_pos(pgenlist, pos);

    fc_assert_ret(plink != nullptr);

    genlist_link_new(pgenlist, data, plink->prev, plink);
  }
}

/************************************************************************//**
  Insert an item after the link. If plink is nullptr, prepend to the list.
****************************************************************************/
void genlist_insert_after(struct genlist *pgenlist, void *data,
                          struct genlist_link *plink)
{
  genlist_link_new(pgenlist, data, plink,
                   plink != nullptr ? plink->next : pgenlist->head_link);
}

/************************************************************************//**
  Insert an item before the link.
****************************************************************************/
void genlist_insert_before(struct genlist *pgenlist, void *data,
                          struct genlist_link *plink)
{
  genlist_link_new(pgenlist, data,
                   plink != nullptr ? plink->prev : pgenlist->tail_link, plink);
}

/************************************************************************//**
  Insert an item at the start of the list.
****************************************************************************/
void genlist_prepend(struct genlist *pgenlist, void *data)
{
  genlist_link_new(pgenlist, data, nullptr, pgenlist->head_link);
}

/************************************************************************//**
  Insert an item at the end of the list.
****************************************************************************/
void genlist_append(struct genlist *pgenlist, void *data)
{
  genlist_link_new(pgenlist, data, pgenlist->tail_link, nullptr);
}

/************************************************************************//**
  Return the link where data is equal to 'data'. Returns nullptr if not found.

  This is an O(n) operation.  Hence, "search".
****************************************************************************/
struct genlist_link *genlist_search(const struct genlist *pgenlist,
                                    const void *data)
{
  struct genlist_link *plink;

  for (plink = pgenlist->head_link; plink; plink = plink->next) {
    if (plink->dataptr == data) {
      return plink;
    }
  }

  return nullptr;
}

/************************************************************************//**
  Return the link which fit the conditional function. Returns nullptr if no
  match.
****************************************************************************/
struct genlist_link *genlist_search_if(const struct genlist *pgenlist,
                                       genlist_cond_fn_t cond_data_func)
{
  if (cond_data_func != nullptr) {
    struct genlist_link *plink = pgenlist->head_link;

    for (; plink != nullptr; plink = plink->next) {
      if (cond_data_func(plink->dataptr)) {
        return plink;
      }
    }
  }

  return nullptr;
}

/************************************************************************//**
  Sort the elements of a genlist.

  The comparison function should be a function usable by qsort; note
  that the const void * arguments to compar should really be "pointers to
  void*", where the void* being pointed to are the genlist dataptrs.
  That is, there are two levels of indirection.
  To do the sort we first construct an array of pointers corresponding
  to the genlist dataptrs, then sort those and put them back into
  the genlist.
****************************************************************************/
void genlist_sort(struct genlist *pgenlist,
                  int (*compar) (const void *, const void *))
{
  const size_t n = genlist_size(pgenlist);
  void **sortbuf;
  struct genlist_link *myiter;
  unsigned int i;

  if (n <= 1) {
    return;
  }

  sortbuf = fc_malloc(n * sizeof(void *));
  myiter = genlist_head(pgenlist);
  for (i = 0; i < n; i++, myiter = myiter->next) {
    sortbuf[i] = myiter->dataptr;
  }

  qsort(sortbuf, n, sizeof(*sortbuf), compar);

  myiter = genlist_head(pgenlist);
  for (i = 0; i < n; i++, myiter = myiter->next) {
    myiter->dataptr = sortbuf[i];
  }
  FC_FREE(sortbuf);
}

/************************************************************************//**
  Randomize the elements of a genlist using the Fisher-Yates shuffle.

  see: genlist_sort() and shared.c:array_shuffle()
****************************************************************************/
void genlist_shuffle(struct genlist *pgenlist)
{
  const int n = genlist_size(pgenlist);

  if (n > 1) {
    void *sortbuf[n];
    struct genlist_link *myiter;
    int i, shuffle[n];

    myiter = genlist_head(pgenlist);
    for (i = 0; i < n; i++, myiter = myiter->next) {
      sortbuf[i] = myiter->dataptr;
      /* Also create the shuffle list */
      shuffle[i] = i;
    }

    /* Randomize it */
    array_shuffle(shuffle, n);

    /* Create the shuffled list */
    myiter = genlist_head(pgenlist);
    for (i = 0; i < n; i++, myiter = myiter->next) {
      myiter->dataptr = sortbuf[shuffle[i]];
    }
  }
}

/************************************************************************//**
  Reverse the order of the elements in the genlist.
****************************************************************************/
void genlist_reverse(struct genlist *pgenlist)
{
  struct genlist_link *head, *tail;
  int counter;

  head = pgenlist->head_link;
  tail = pgenlist->tail_link;
  for (counter = pgenlist->nelements / 2; 0 < counter; counter--) {
    /* Swap. */
    void *temp = head->dataptr;

    head->dataptr = tail->dataptr;
    tail->dataptr = temp;

    head = head->next;
    tail = tail->prev;
  }
}

/************************************************************************//**
  Allocates list mutex
****************************************************************************/
void genlist_allocate_mutex(struct genlist *pgenlist)
{
  fc_mutex_allocate(&pgenlist->mutex);
}

/************************************************************************//**
  Releases list mutex
****************************************************************************/
void genlist_release_mutex(struct genlist *pgenlist)
{
  fc_mutex_release(&pgenlist->mutex);
}
