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

#include <time.h>	/* time_t */
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/**************************************************************************
  The connection struct and related stuff.
  Includes cmdlevel stuff, which is connection-based.
***************************************************************************/

#include "shared.h"		/* MAX_LEN_ADDR */

struct player;

#define MAX_LEN_PACKET   4096
#define MAX_LEN_CAPSTR    512
#define MAX_LEN_BUFFER   (MAX_LEN_PACKET * 128)

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

/* get 'struct conn_list' and related functions: */
/* do this with forward definition of struct connection, so that
 * connection struct can contain a struct conn_list */
struct connection;
#define SPECLIST_TAG conn
#define SPECLIST_TYPE struct connection
#include "speclist.h"

#define conn_list_iterate(connlist, pconn) \
    TYPED_LIST_ITERATE(struct connection, connlist, pconn)
#define conn_list_iterate_end  LIST_ITERATE_END

/***********************************************************
  This is a buffer where the data is first collected,
  whenever it arrives to the client/server.
***********************************************************/
struct socket_packet_buffer {
  int ndata;
  int do_buffer_sends;
  int nsize;
  unsigned char *data;
};

/***********************************************************
  The connection struct represents a single client or server
  at the other end of a network connection.
***********************************************************/
struct connection {
  int id;			/* used for server/client communication */
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
  time_t last_write;
  int ponged;		        /* have received a PACKET_CONN_PONG? */

  struct conn_list self;	/* list with this connection as single element */
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
  struct map_position *route;
  int route_length;
  /* These are used when recieving goto routes; they are send split, and in
     the time where the route is partially recieved it is stored here. */

  int delayed_disconnect;
  /* Something has occured that means the connection should be closed, but
     the closing has been postponed. */

  void (*notify_of_writable_data) (struct connection * pc,
				   int data_available_and_socket_full);
  struct {
    /* 
     * Increases for every packet send to the server.
     */
    int last_request_id_used;

    /* 
     * Increases for every received PACKET_PROCESSING_FINISHED packet.
     */
    int last_processed_request_id_seen;

    /* 
     * Holds the id of the request which caused this packet. Can be
     * zero.
     */
    int request_id_of_currently_handled_packet;

    /*
     * Holds the request id of the last pong packet the client
     * sent. Can be zero.
     */
    int request_id_of_last_pong;
  } client;

  struct {
    /* 
     * Holds the id of the request which is processed now. Can be
     * zero.
     */
    int currently_processed_request_id;

    /* 
     * Will increase for every received packet.
     */
    int last_request_id_seen;
  } server;

  /*
   * Called before an incomming packet is processed. The packet_type
   * argument should really be a "enum packet_type". However due
   * circular dependency this is impossible.
   */
  void (*incomming_packet_notify) (struct connection * pc,
				   int packet_type, int size);

  /*
   * Called before a packet is sent. The packet_type argument should
   * really be a "enum packet_type". However due circular dependency
   * this is impossible.
   */
  void (*outgoing_packet_notify) (struct connection * pc,
				  int packet_type, int size);
};


const char *cmdlevel_name(enum cmdlevel_id lvl);
enum cmdlevel_id cmdlevel_named(const char *token);


typedef void (*CLOSE_FUN) (struct connection *pc);
void close_socket_set_callback(CLOSE_FUN fun);

int read_socket_data(int sock, struct socket_packet_buffer *buffer);
void flush_connection_send_buffer_all(struct connection *pc);
void flush_connection_send_buffer_packets(struct connection *pc);
void send_connection_data(struct connection *pc, unsigned char *data, int len);

void connection_do_buffer(struct connection *pc);
void connection_do_unbuffer(struct connection *pc);

void conn_list_do_buffer(struct conn_list *dest);
void conn_list_do_unbuffer(struct conn_list *dest);

struct connection *find_conn_by_name(const char *name);
struct connection *find_conn_by_name_prefix(const char *name,
					    enum m_pre_result *result);
struct connection *find_conn_by_id(int id);

struct socket_packet_buffer *new_socket_packet_buffer(void);
void free_socket_packet_buffer(struct socket_packet_buffer *buf);

const char *conn_description(const struct connection *pconn);

int get_next_request_id(int old_request_id);

extern const char blank_addr_str[];

extern int delayed_disconnect;

#endif  /* FC__CONNECTION_H */
