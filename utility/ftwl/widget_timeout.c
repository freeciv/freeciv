/********************************************************************** 
 Freeciv - Copyright (C) 2004 - The Freeciv Project
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
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <stdio.h>

#ifdef HAVE_GETTIMEOFDAY
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef HAVE_FTIME
# include <sys/timeb.h>
#endif

#include "widget_p.h"

#include "mem.h"

#ifndef HAVE_GETTIMEOFDAY
#ifdef HAVE_FTIME
static void gettimeofday(struct timeval *tv, void *dummy)
{
  struct timeb tp;
  ftime(&tp);
  tv->tv_usec = tp.millitm * 1000;
  tv->tv_sec = tp.time;
}
#endif
#endif

struct callback {
  int id;
  struct timeval time;
  void (*callback) (void *data);
  void *data;
};

#define SPECLIST_TAG callback
#define SPECLIST_TYPE struct callback
#include "speclist.h"

#define callback_list_iterate(list, item) \
    TYPED_LIST_ITERATE(struct callback, list, item)
#define callback_list_iterate_end  LIST_ITERATE_END

static struct callback_list callback_list;
static bool callback_list_list_has_been_initialised = FALSE;
static int id_counter = 1;

/*************************************************************************
  ...
*************************************************************************/
static void ensure_init(void)
{
  if (!callback_list_list_has_been_initialised) {
    callback_list_init(&callback_list);
    callback_list_list_has_been_initialised = TRUE;
  }
}

/*************************************************************************
  ...
*************************************************************************/
int sw_add_timeout(int msec, void (*callback) (void *data), void *data)
{
  struct callback *p = fc_malloc(sizeof(*p));

  //printf("add_timeout(%dms)\n", msec);
  gettimeofday(&p->time, NULL);

  p->time.tv_sec += msec / 1000;
  msec %= 1000;

  p->time.tv_usec += msec * 1000;
  while (p->time.tv_usec > 1000 * 1000) {
    p->time.tv_usec -= 1000 * 1000;
    p->time.tv_sec++;
  }

  p->callback = callback;
  p->data = data;
  p->id=id_counter;
  id_counter++;

  ensure_init();
  callback_list_insert(&callback_list, p);
  return p->id;
}

/*************************************************************************
  ...
*************************************************************************/
void sw_remove_timeout(int id)
{
  assert(id > 0);
  callback_list_iterate(callback_list, callback) {
    if (callback->id == id) {
      callback_list_unlink(&callback_list, callback);
      free(callback);
    }
  } callback_list_iterate_end;
}

/*************************************************************************
  ...
*************************************************************************/
void handle_callbacks(void)
{
  bool one_called = FALSE;
  struct timeval now;

  gettimeofday(&now, NULL);

  /*printf("now =        %ld.%ld\n",now.tv_sec,now.tv_usec);*/

  ensure_init();

  for (;;) {
    bool change = FALSE;
    callback_list_iterate(callback_list, callback) {
      /*printf("  callback   %ld.%ld\n", callback->time.tv_sec,
         callback->time.tv_usec); */
      if (timercmp(&callback->time, &now, <)) {
	/*printf("  call\n"); */
	callback_list_unlink(&callback_list, callback);
	callback->callback(callback->data);
	free(callback);
	one_called = TRUE;
	change = TRUE;
	break;
      }
    } callback_list_iterate_end;
    if (!change) {
      break;
    }
  }

  if (one_called) {
    sw_paint_all();
  }
}

/*************************************************************************
  ...
*************************************************************************/
void get_select_timeout(struct timeval *timeout)
{
  struct callback *earliest = NULL;

  ensure_init();

  callback_list_iterate(callback_list, callback) {
    if (!earliest || timercmp(&callback->time, &earliest->time, <)) {
      earliest = callback;
    }
  }
  callback_list_iterate_end;
  if (!earliest) {
    timeout->tv_sec = 100000;
    timeout->tv_usec = 0;
  } else {
    struct timeval now;

    gettimeofday(&now, NULL);

    if (!timercmp(&now, &earliest->time, <)) {
      timeout->tv_sec = 0;
      timeout->tv_usec = 0;
    } else {
      timeout->tv_sec = earliest->time.tv_sec - now.tv_sec;
      timeout->tv_usec = earliest->time.tv_usec - now.tv_usec;
      if (timeout->tv_usec < 0) {
	timeout->tv_usec += 1000 * 1000;
	timeout->tv_sec--;
      }
    }
    assert(timeout->tv_sec >= 0 && timeout->tv_usec >= 0);
  }
}
