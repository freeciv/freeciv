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
#ifndef FC__CONNECTHAND_H
#define FC__CONNECTHAND_H

#include "shared.h"		/* bool type */

struct connection;
struct conn_list;
struct packet_login_request;
struct player;

bool handle_login_request(struct connection *pconn,
                          struct packet_login_request *req);

void lost_connection_to_client(struct connection *pconn);
void accept_new_player(char *name, struct connection *pconn);

void send_conn_info(struct conn_list *src, struct conn_list *dest);
void send_conn_info_remove(struct conn_list *src, struct conn_list *dest);

void associate_player_connection(struct player *pplayer,
                                 struct connection *pconn);
void unassociate_player_connection(struct player *pplayer,
                                   struct connection *pconn);

#endif /* FC__CONNECTHAND_H */
