/**********************************************************************
 Freeciv - Copyright (C) 2003  - M.C. Kaufman
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifndef WIN32_NATIVE
#define HAS_FILE_LOCKING
#endif

#ifdef HAVE_FLOCK
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "log.h"
#include "support.h"

#include "lockfile.h"

#ifndef FC_LOCK_FILE
#define FC_LOCK_FILE "freeciv_user_database.lock"
#endif

#define USLEEP_TIME     50   /* us to sleep after finding a lock */
#define USLEEP_MAX      100  /* max times to sleep before declaring failure */

#ifdef HAS_FILE_LOCKING
static FILE *fp;
#endif

/* the file descriptors are here. */
#ifdef HAVE_FLOCK
  #define LOCK_CMD flock(fd, LOCK_EX|LOCK_NB)
  #define UNLOCK_CMD flock(fd, LOCK_UN)
#else
  #define LOCK_CMD fcntl(fd, F_SETLK, &fl)
  #define UNLOCK_CMD fcntl(fd, F_SETLK, &fl)
#endif

/**************************************************************************
 Attempt to create a lock so other servers can't access the database when 
 we are. Return TRUE for success, FALSE for failure.
 N.B. This is a simple locking mechanism: it for example does not check for
 stale lock files. This means that if the application crashes in between 
 lock creation and removal, you'll get a dangling lock file (this could 
 easily happen if you were to corrupt the database. The registry functions
 will bomb and quit the server). Perhaps this warrants a FIXME.
**************************************************************************/
bool create_lock(void)
{
#ifdef HAS_FILE_LOCKING
  int i, fd;
#ifndef HAVE_FLOCK
  struct flock fl = { F_WRLCK, SEEK_SET, 0, 0, 0 };

  fl.l_pid = getpid();
#endif

  /* open the file */
  if (!(fp = fopen(FC_LOCK_FILE, "w+"))) {
    return FALSE;
  }

  fd = fileno(fp);

  /* if we can't get a lock, then sleep a little and try again */
  for (i = 0; i < USLEEP_MAX; i++) {
    if (LOCK_CMD == -1) {
      myusleep(USLEEP_TIME);
    } else {
      return TRUE;
    }
  }
#else
  freelog(LOG_VERBOSE, "FILE_LOCKING not available"); 
  return TRUE;
#endif /* HAS_FILE_LOCKING */

  freelog(LOG_DEBUG, "Unable to create lock");
  remove(FC_LOCK_FILE);
  return FALSE;
}

/**************************************************************************
 Remove the lock file, but only if we locked in the first place.
**************************************************************************/
void remove_lock(void)
{
#ifdef HAS_FILE_LOCKING
  int fd = fileno(fp);
#ifndef HAVE_FLOCK
  struct flock fl = { F_UNLCK, SEEK_SET, 0, 0, 0 };

  fl.l_pid = getpid();
#endif

  if (UNLOCK_CMD == -1) {
    fclose(fp);
    remove(FC_LOCK_FILE);
    die("Your system couldn't remove a lock. your 'flock()' may be broken");
  }

  fclose(fp);
  remove(FC_LOCK_FILE);
#else
  freelog(LOG_VERBOSE, "FILE_LOCKING not available"); 
#endif /* HAS_FILE_LOCKING */
}
