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

#include "fcthread.h"

#ifdef HAVE_PTHREAD
/**********************************************************************
  Create new thread
***********************************************************************/
int fc_thread_start(fc_thread *thread, void *(*function) (void *arg), void *arg)
{
  return pthread_create(thread, NULL, function, arg);
}

/**********************************************************************
  Wait for thread to finish
***********************************************************************/
void *fc_thread_wait(fc_thread *thread)
{
  void **return_value = NULL;

  pthread_join(*thread, return_value);
  return *return_value;
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
#endif /* HAVE_PTHREAD */
