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
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif 

#include "log.h"
#include "support.h"

#include "netintf.h"

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

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

/***************************************************************************
  Look up the given host and fill in *sock.  Note that the caller
  should fill in the port number (sock->sin_port).
***************************************************************************/
int fc_lookup_host(const char *hostname, struct sockaddr_in *sock)
{
  struct hostent *hp;

  sock->sin_family = AF_INET;

#ifdef HAVE_INET_ATON
  if (inet_aton(hostname, &sock->sin_addr)) {
    return 1;
  }
#else
  if ((sock->sin_addr.s_addr = inet_addr(hostname)) != INADDR_NONE) {
    return 1;
  }
#endif
  hp = gethostbyname(hostname);
  if (hp == NULL || hp->h_addrtype != AF_INET) {
    return 0;
  }

  memcpy(&sock->sin_addr, hp->h_addr, hp->h_length);
  return 1;
}
