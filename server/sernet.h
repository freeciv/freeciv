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
#ifndef FC__SERNET_H
#define FC__SERNET_H

struct connection;

#define DEFAULT_SOCK_PORT 5555
#define BUF_SIZE 512

#define SERVER_LAN_PORT 4555
#define SERVER_LAN_TTL 1
#define SERVER_LAN_VERSION 1

int server_open_socket(void);
void flush_packets(void);
int sniff_packets(void);
void close_connections_and_socket(void);
void init_connections(void);
void close_connection(struct connection *pconn);
void handle_conn_pong(struct connection *pconn);

#endif  /* FC__SERNET_H */
