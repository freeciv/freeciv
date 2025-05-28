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

#ifndef FC__THREAD_H
#define FC__THREAD_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* gen_headers */
#include "freeciv_config.h"

/* utility */
#include "support.h" /* bool */

#ifdef FREECIV_HAVE_TINYCTHR
#include "fc_tinycthread.h"
#define FREECIV_C11_THR
#else
#ifdef FREECIV_HAVE_C11_THREADS
#include <threads.h>
#define FREECIV_C11_THR
#endif /* FREECIV_HAVE_C11_THREADS */
#endif /* FREECIV_HAVE_TINYCTHR */

#ifdef FREECIV_C11_THR

#define fc_thread      thrd_t
#define fc_mutex       mtx_t
#define fc_thread_cond cnd_t
#define fc_thread_id   thrd_t

#elif defined(FREECIV_HAVE_PTHREAD)

#include <pthread.h>

#define fc_thread      pthread_t
#define fc_mutex       pthread_mutex_t
#define fc_thread_cond pthread_cond_t
#define fc_thread_id   pthread_t

#elif defined (FREECIV_HAVE_WINTHREADS)

#include <windows.h>
#define fc_thread      HANDLE *
#define fc_mutex       HANDLE *
#define fc_thread_id   DWORD

#ifndef FREECIV_HAVE_THREAD_COND
#define fc_thread_cond char
#else  /* FREECIV_HAVE_THREAD_COND */
#warning FREECIV_HAVE_THREAD_COND defined but we have no real Windows implementation
#endif /* FREECIV_HAVE_THREAD_COND */

#else /* No pthreads nor winthreads */

#error "No working thread implementation"

#endif /* FREECIV_HAVE_PTHREAD */

int fc_thread_start(fc_thread *thread, void (*function) (void *arg), void *arg);
void fc_thread_wait(fc_thread *thread);
fc_thread_id fc_thread_self(void);
bool fc_threads_equal(fc_thread_id thr1, fc_thread_id thr2);

void fc_mutex_init(fc_mutex *mutex);
void fc_mutex_destroy(fc_mutex *mutex);
void fc_mutex_allocate(fc_mutex *mutex);
void fc_mutex_release(fc_mutex *mutex);

void fc_thread_cond_init(fc_thread_cond *cond);
void fc_thread_cond_destroy(fc_thread_cond *cond);
void fc_thread_cond_wait(fc_thread_cond *cond, fc_mutex *mutex);
void fc_thread_cond_signal(fc_thread_cond *cond);

bool has_thread_cond_impl(void);

typedef void (at_thread_exit_cb)(void);

bool register_at_thread_exit_callback(at_thread_exit_cb *cb);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__THREAD_H */
