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

/********************************************************************** 
  Common network interface.
**********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include "log.h"
#include "support.h"

#include "netintf.h"

/***************************************************************
...
***************************************************************/
void my_nonblock(int sockfd)
{
#ifdef NONBLOCKING_SOCKETS
#ifdef HAVE_FCNTL
  int f_set;

  if ((f_set=fcntl(sockfd, F_GETFL)) == -1) {
    freelog(LOG_ERROR, "fcntl F_GETFL failed: %s", mystrerror(errno));
  }

  f_set |= O_NONBLOCK;

  if (fcntl(sockfd, F_SETFL, f_set) == -1) {
    freelog(LOG_ERROR, "fcntl F_SETFL failed: %s", mystrerror(errno));
  }
#else
#ifdef HAVE_IOCTL
  long value=1;

  if (ioctl(sockfd, FIONBIO, (char*)&value) == -1) {
    freelog(LOG_ERROR, "ioctl failed: %s", mystrerror(errno));
  }
#endif
#endif
#else
  freelog(LOG_DEBUG, "NONBLOCKING_SOCKETS not available");
#endif
}
