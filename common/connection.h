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
#ifndef FC__CONNECTION_H
#define FC__CONNECTION_H

/**************************************************************************
  The connection struct and related stuff.
  Includes cmdlevel stuff, which is connection-based.
***************************************************************************/

#include "shared.h"		/* MAX_LEN_ADDR */

struct player;

#define MAX_LEN_PACKET   4096
#define MAX_LEN_CAPSTR    512

/**************************************************************************
  Command access levels for client-side use; at present, they are only
  used to control access to server commands typed at the client chatline.
***************************************************************************/
enum cmdlevel_id {    /* access levels for users to issue commands        */
  ALLOW_NONE = 0,     /* user may issue no commands at all                */
  ALLOW_INFO,         /* user may issue informational commands            */
  ALLOW_CTRL,         /* user may issue commands that affect game & users */
  ALLOW_HACK,         /* user may issue *all* commands - dangerous!       */

  ALLOW_NUM,          /* the number of levels                             */
  ALLOW_UNRECOGNIZED  /* used as a failure return code                    */
};
/*  the set command is a special case:                                    */
/*    - ALLOW_CTRL is required for SSET_TO_CLIENT options                 */
/*    - ALLOW_HACK is required for SSET_TO_SERVER options                 */

/***************************************************************************
  On the distinction between nations(formerly races), players, and users,
  see freeciv_hackers_guide.txt
***************************************************************************/

/***********************************************************
  This is a buffer where the data is first collected,
  whenever it arrives to the client/server.
***********************************************************/
struct socket_packet_buffer {
  int ndata;
  int do_buffer_sends;
  unsigned char data[10*MAX_LEN_PACKET];
};

/***********************************************************
  The connection struct represents a single client or server
  at the other end of a network connection.
***********************************************************/
struct connection {
  int sock, used;
  int established;		/* have negotiated initial packets */
  int first_packet;		/* check byte order on first packet */
  int byte_swap;		/* connection uses non-network byte order */
  struct player *player;	/* NULL for connections not yet associated
				   with a specific player */
  int observer;			/* connection is "observer", not controller;
 				   may be observing specific player, or all
 				   (implementation incomplete) */
  struct socket_packet_buffer *buffer;
  struct socket_packet_buffer *send_buffer;
  char name[MAX_LEN_NAME];
  char addr[MAX_LEN_ADDR];
  char capability[MAX_LEN_CAPSTR];
  /* "capability" gives the capability string of the executable (be it
   * a client or server) at the other end of the connection.
   */
  enum cmdlevel_id access_level;
  /* Used in the server, "access_level" stores the access granted
   * to the client corresponding to this connection.
   */
};


const char *cmdlevel_name(enum cmdlevel_id lvl);
enum cmdlevel_id cmdlevel_named(const char *token);


typedef void CLOSE_FUN (struct connection *pc);
void close_socket_set_callback(CLOSE_FUN *fun);

int read_socket_data(int sock, struct socket_packet_buffer *buffer);
int send_connection_data(struct connection *pc, unsigned char *data, int len);

void connection_do_buffer(struct connection *pc);
void connection_do_unbuffer(struct connection *pc);

struct socket_packet_buffer *new_socket_packet_buffer(void);
  
#endif  /* FC__CONNECTION_H */
