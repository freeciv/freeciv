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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "support.h" /* bool */

#ifdef HAVE_PTHREAD
#include <pthread.h>

#define fc_thread      pthread_t
#define fc_mutex       pthread_mutex_t
#define fc_thread_cond pthread_cond_t

#elif defined (HAVE_WINTHREADS)

#include <windows.h>
#define fc_thread      HANDLE *
#define fc_mutex       HANDLE *

#ifndef HAVE_THREAD_COND
#define fc_thread_cond char
#else  /* HAVE_THREAD_COND */
#warning HAVE_THREAD_COND defined but we have no real Windows implementation
#endif /* HAVE_THREAD_COND */

#else /* No pthreads nor winthreads */

/* Dummy */
/* These must be real types with size instead of 'void' */
#define fc_thread      char
#define fc_mutex       char
#define fc_thread_cond char

#endif /* HAVE_PTHREAD */

int fc_thread_start(fc_thread *thread, void (*function) (void *arg), void *arg);
void fc_thread_wait(fc_thread *thread);

void fc_init_mutex(fc_mutex *mutex);
void fc_destroy_mutex(fc_mutex *mutex);
void fc_allocate_mutex(fc_mutex *mutex);
void fc_release_mutex(fc_mutex *mutex);

void fc_thread_cond_init(fc_thread_cond *cond);
void fc_thread_cond_destroy(fc_thread_cond *cond);
void fc_thread_cond_wait(fc_thread_cond *cond, fc_mutex *mutex);
void fc_thread_cond_signal(fc_thread_cond *cond);

bool has_thread_cond_impl(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__THREAD_H */
