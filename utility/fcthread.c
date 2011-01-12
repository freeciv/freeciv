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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// utility
#include "mem.h"
#include "support.h"

#include "fcthread.h"

#ifdef HAVE_PTHREAD

struct fc_thread_wrap_data {
  void *arg;
  void (*func)(void *arg);
};

/**********************************************************************
  Wrapper which fingerprint matches one required by pthread_create().
  Calls function which matches fingerprint required by fc_thread_start()
***********************************************************************/
static void *fc_thread_wrapper(void *arg)
{
  struct fc_thread_wrap_data *data = (struct fc_thread_wrap_data *) arg;

  data->func(data->arg);

  free(data);

  return NULL;
}

/**********************************************************************
  Create new thread
***********************************************************************/
int fc_thread_start(fc_thread *thread, void (*function) (void *arg),
                    void *arg)
{
  /* Freed by child thread once it's finished with data */
  struct fc_thread_wrap_data *data = fc_malloc(sizeof(data));

  data->arg = arg;
  data->func = function;

  return pthread_create(thread, NULL, &fc_thread_wrapper, data);
}

/**********************************************************************
  Wait for thread to finish
***********************************************************************/
void fc_thread_wait(fc_thread *thread)
{
  void **return_value = NULL;

  pthread_join(*thread, return_value);
}

/**********************************************************************
  Initialize mutex
***********************************************************************/
void fc_init_mutex(fc_mutex *mutex)
{
  pthread_mutex_init(mutex, NULL);
}

/**********************************************************************
  Destroy mutex
***********************************************************************/
void fc_destroy_mutex(fc_mutex *mutex)
{
  pthread_mutex_destroy(mutex);
}

/**********************************************************************
  Lock mutex
***********************************************************************/
void fc_allocate_mutex(fc_mutex *mutex)
{
  pthread_mutex_lock(mutex);
}

/**********************************************************************
  Release mutex
***********************************************************************/
void fc_release_mutex(fc_mutex *mutex)
{
  pthread_mutex_unlock(mutex);
}
#else /* HAVE_PTHREAD */

#ifdef HAVE_WINTHREADS

struct fc_thread_wrap_data {
  void *arg;
  void (*func)(void *arg);
};

/**********************************************************************
  Wrapper which fingerprint matches one required by CreateThread().
  Calls function which matches fingerprint required by fc_thread_start()
***********************************************************************/
static DWORD WINAPI fc_thread_wrapper(LPVOID arg)
{
  struct fc_thread_wrap_data *data = (struct fc_thread_wrap_data *) arg;

  data->func(data->arg);

  free(data);

  return 0;
}

/**********************************************************************
  Create new thread
***********************************************************************/
int fc_thread_start(fc_thread *thread, void (*function) (void *arg), void *arg)
{
  /* Freed by child thread once it's finished with data */
  struct fc_thread_wrap_data *data = fc_malloc(sizeof(data));

  data->arg = arg;
  data->func = function;

  *thread = CreateThread(NULL, 0, &fc_thread_wrapper, data, 0, NULL);

  if (*thread == NULL) {
    return 1;
  }

  return 0;
}

/**********************************************************************
  Wait for thread to finish
***********************************************************************/
void fc_thread_wait(fc_thread *thread)
{
  DWORD exit_code;

  GetExitCodeThread(*thread, &exit_code);

  while (exit_code == STILL_ACTIVE) {
    fc_usleep(1000);
    GetExitCodeThread(*thread, &exit_code);
  }

  CloseHandle(*thread);
}

/**********************************************************************
  Initialize mutex
***********************************************************************/
void fc_init_mutex(fc_mutex *mutex)
{
  *mutex = CreateMutex(NULL, FALSE, NULL);
}

/**********************************************************************
  Destroy mutex
***********************************************************************/
void fc_destroy_mutex(fc_mutex *mutex)
{
  CloseHandle(*mutex);
}

/**********************************************************************
  Lock mutex
***********************************************************************/
void fc_allocate_mutex(fc_mutex *mutex)
{
  WaitForSingleObject(*mutex, INFINITE);
}

/**********************************************************************
  Release mutex
***********************************************************************/
void fc_release_mutex(fc_mutex *mutex)
{
  ReleaseMutex(*mutex);
}
#endif /* HAVE_WINTHREADS */
#endif /* HAVE_PTHREAD */
