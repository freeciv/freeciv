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

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_WINSOCK
# include <winsock.h>
#endif

#include "ioz.h"
#include "shared.h"		/* bool type */

#ifdef FD_ZERO
#define MY_FD_ZERO FD_ZERO
#else
#define MY_FD_ZERO(p) memset((void *)(p), 0, sizeof(*(p)))
#endif

union my_sockaddr {
  struct sockaddr sockaddr;
  struct sockaddr_in sockaddr_in;
};

int my_readsocket(int sock, void *buf, size_t size);
int my_writesocket(int sock, const void *buf, size_t size); 
void my_closesocket(int sock);
void my_init_network(void);         
void my_shutdown_network(void);

void my_nonblock(int sockfd);
bool net_lookup_service(const char *name, int port, 
                        union my_sockaddr *addr);
fz_FILE *my_querysocket(int sock, void *buf, size_t size);
int find_next_free_port(int starting_port);

const char *my_lookup_httpd(char *server, int *port, const char *url);
const char *my_url_encode(const char *txt);

#endif  /* FC__NETINTF_H */
