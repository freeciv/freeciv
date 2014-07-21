/**********************************************************************
 Freeciv - Copyright (C) 2002 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/***********************************************************************
  Implementation of a priority queue aka heap.

  Currently only one value-type is supported.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "log.h"                /* fc_assert. */
#include "mem.h"

#include "pqueue.h"


struct pq_cell {
  pq_data_t data;
  int priority;
};

struct pqueue {
  int size;			/* number of occupied cells */
  int avail;			/* total number of cells */
  int step;			/* additional memory allocation step */
  struct pq_cell *cells; /* array containing data and priorities. */
};

/**********************************************************************
  Initialize the queue.
 
  initial_size is the numer of queue items for which memory should be
  preallocated, that is, the initial size of the item array the queue
  uses. If you insert more than n items to the queue, another n items
  will be allocated automatically.
***********************************************************************/
struct pqueue *pq_create(int initial_size)
{
  struct pqueue *q = fc_malloc(sizeof(struct pqueue));

  q->cells = fc_malloc(sizeof(*q->cells) * initial_size);
  q->avail = initial_size;
  q->step = initial_size;
  q->size = 1;
  return q;
}

/********************************************************************
  Destructor for queue structure.
********************************************************************/
void pq_destroy(struct pqueue *q)
{
  free(q->cells);
  free(q);
}

/********************************************************************
  Insert an item into the queue.
*********************************************************************/
void pq_insert(struct pqueue *q, pq_data_t datum, int datum_priority)
{
  int i, j;

  /* allocate more memory if necessary */
  if (q->size >= q->avail) {
    int newsize = q->size + q->step;

    q->cells = fc_realloc(q->cells, sizeof(*q->cells) * newsize);
    q->avail = newsize;
  }

  /* insert item */
  i = q->size++;
  while (i > 1 && (j = i / 2) && q->cells[j].priority < datum_priority) {
    q->cells[i] = q->cells[j];
    i = j;
  }
  q->cells[i].data = datum;
  q->cells[i].priority = datum_priority;
}

/***************************************************************************
  Set a better priority for datum. Insert if datum is not present yet.
****************************************************************************/
void pq_replace(struct pqueue *q, const pq_data_t datum, int datum_priority)
{
  int i, j;

  /* Lookup for datum... */
  for (i = q->size - 1; i >= 1; i--) {
    if (q->cells[i].data == datum) {
      break;
    }
  }

  if (i == 0) {
    /* Not found, insert. */
    pq_insert(q, datum, datum_priority);
  } else if (q->cells[i].priority < datum_priority) {
    /* Found, percolate-up. */
    while ((j = i / 2) && q->cells[j].priority < datum_priority) {
      q->cells[i] = q->cells[j];
      i = j;
    }
    q->cells[i].data = datum;
    q->cells[i].priority = datum_priority;
  }
}

/*******************************************************************
  Remove the highest-ranking item from the queue and store it in
  dest. dest maybe NULL.
 
  Return value:
     TRUE   The value of the item that has been removed.
     FALSE  No item could be removed, because the queue was empty.
*******************************************************************/
bool pq_remove(struct pqueue * q, pq_data_t *dest)
{
  struct pq_cell tmp;
  struct pq_cell *pcelli, *pcellj;
  pq_data_t top;
  int i, j, s;

  if (q->size == 1) {
    return FALSE;
  }

  fc_assert_ret_val(q->size <= q->avail, FALSE);
  top = q->cells[1].data;
  q->size--;
  tmp = q->cells[q->size];
  s = q->size / 2;
  i = 1;
  pcelli = q->cells + 1;
  while (i <= s) {
    j = 2 * i;
    pcellj = q->cells + j;
    if (j < q->size && pcellj->priority < q->cells[j + 1].priority) {
      j++;
      pcellj++;
    }
    if (pcellj->priority <= tmp.priority) {
      break;
    }
    *pcelli = *pcellj;
    i = j;
    pcelli = pcellj;
  }
  *pcelli = tmp;
  if (dest) {
    *dest = top;
  }
  return TRUE;
}

/*********************************************************************
  Store the highest-ranking item in dest without removing it
 
  Return values:
     TRUE   dest was set.
     FALSE  The queue is empty.
**********************************************************************/
bool pq_peek(struct pqueue *q, pq_data_t * dest)
{
  if (q->size == 1) {
    return FALSE;
  }

  *dest = q->cells[1].data;
  return TRUE;
}

/****************************************************************************
  Set the highest priority of the queue in 'datum_priority'. Return FALSE
  iff the queue is empty.
****************************************************************************/
bool pq_priority(const struct pqueue *q, int *datum_priority)
{
  if (q->size == 1) {
    return FALSE;
  }

  *datum_priority = q->cells[1].priority;
  return TRUE;
}
