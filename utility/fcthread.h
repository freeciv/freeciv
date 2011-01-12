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

#ifndef FC__THREAD_H
#define FC__THREAD_H

#ifdef HAVE_PTHREAD
#include <pthread.h>

#define fc_thread pthread_t
#define fc_mutex  pthread_mutex_t
#else /* HAVE_PTHREAD */
#ifdef HAVE_WINTHREADS

#include <windows.h>

#define fc_thread HANDLE *
#define fc_mutex HANDLE *
#else /* HAVE_WINTHREADS */
/* Dummy */
#define fc_thread void
#define fc_mutex void
#endif /* HAVE_WINTHREADS */
#endif /* HAVE_PTHREAD */

int fc_thread_start(fc_thread *thread, void (*function) (void *arg), void *arg);
void fc_thread_wait(fc_thread *thread);

void fc_init_mutex(fc_mutex *mutex);
void fc_destroy_mutex(fc_mutex *mutex);
void fc_allocate_mutex(fc_mutex *mutex);
void fc_release_mutex(fc_mutex *mutex);

#endif /* FC__THREAD_H */
