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
#ifndef FC__CLINET_H
#define FC__CLINET_H

#define DEFAULT_SOCK_PORT 5555
#define METALIST_ADDR "http://www.daimi.aau.dk/~lancelot/freeciv.html"

struct connection;

int connect_to_server(char *name, char *hostname, int port, char *errbuf);
void input_from_server(int fd);
void disconnect_from_server(void);

extern struct connection aconnection;
/* this is the client's connection to the server */

#endif  /* FC__CLINET_H */
