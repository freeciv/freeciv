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
#ifndef __CLINET_H
#define __CLINET_H

#include <X11/Intrinsic.h>

#define DEFAULT_SOCK_PORT 5555

struct connection;

int connect_to_server(char *name, char *hostname, int port, char *errbuf);
void get_net_input(XtPointer client_data, int *fid, XtInputId *id);
void close_server_connection(void);
int client_open_connection(char *host, int port);
void connection_gethostbyaddr(struct connection *pconn, char *desthost);

extern struct connection aconnection;
#endif


