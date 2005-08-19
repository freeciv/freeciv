/**********************************************************************
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__SERVERS_H
#define FC__SERVERS_H

#define METALIST_ADDR "http://meta.freeciv.org/metaserver.phtml"

#define SERVER_LAN_PORT 4555
#define SERVER_LAN_TTL 1
#define SERVER_LAN_VERSION 1

struct server
{
  char *host;
  char *port;
  char *capability;
  char *patches;
  char *version;
  char *state;
  char *topic;
  char *message;

  struct players
  {
    char *name;
    char *type;
    char *host;
    char *nation;
  } *players;

  char *nplayers;
};

#define SPECLIST_TAG server
#define SPECLIST_TYPE struct server
#include "speclist.h"

#define server_list_iterate(serverlist, pserver) \
  TYPED_LIST_ITERATE(struct server, serverlist, pserver)
#define server_list_iterate_end  LIST_ITERATE_END

struct server_list *create_server_list(char *errbuf, int n_errbuf);
void delete_server_list(struct server_list *server_list);
int begin_lanserver_scan(void);
struct server_list *get_lan_server_list(void); 
void finish_lanserver_scan(void);

#endif /* FC__SERVERS_H */
