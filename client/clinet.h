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
#define METALIST_ADDR "http://meta.freeciv.org/metaserver/"

/* In autoconnect mode, try to connect to once a second */
#define AUTOCONNECT_INTERVAL		500

/* In autoconnect mode, try to connect 100 times */
#define MAX_AUTOCONNECT_ATTEMPTS	100

struct connection;

int connect_to_server(char *name, char *hostname, int port,
		      char *errbuf, int errbufsize);
int get_server_address(char *hostname, int port, char *errbuf, int errbufsize);
int try_to_connect(char *user_name, char *errbuf, int errbufsize);
void input_from_server(int fd);
void input_from_server_till_request_got_processed(int fd,
						  int expected_request_id);
void disconnect_from_server(void);
void add_non_user_request(int request_id);
int test_non_user_request_and_remove(int request_id);

extern struct connection aconnection;
/* this is the client's connection to the server */

struct server
{
  char *name;
  char *port;
  char *version;
  char *status;
  char *players;
  char *metastring;
};

#define SPECLIST_TAG server
#define SPECLIST_TYPE struct server
#include "speclist.h"

#define server_list_iterate(serverlist, pserver) \
  TYPED_LIST_ITERATE(struct server, serverlist, pserver)
#define server_list_iterate_end  LIST_ITERATE_END

struct server_list *create_server_list(char *errbuf, int n_errbuf);
void delete_server_list(struct server_list *server_list);

#endif  /* FC__CLINET_H */
