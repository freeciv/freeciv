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

#ifndef FC__NETINTF_H
#define FC__NETINTF_H

/********************************************************************** 
  Common network interface.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include "shared.h"		/* bool type */

#ifdef FD_ZERO
#define MY_FD_ZERO FD_ZERO
#else
#define MY_FD_ZERO(p) memset((void *)(p), 0, sizeof(*(p)))
#endif

int my_readsocket(int sock, void *buf , size_t size);
int my_writesocket(int sock, const void *buf, size_t size); 
void my_closesocket(int sock);
void my_init_network(void);         
void my_shutdown_network(void);

void my_nonblock(int sockfd);
bool fc_lookup_host(const char *hostname, struct sockaddr_in *sock);

#endif  /* FC__NETINTF_H */
