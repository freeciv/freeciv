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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>
#include <errno.h>

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

#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#include "fcintl.h"
#include "game.h"		/* game.all_connections */
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "support.h"		/* mystr(n)casecmp */

#include "connection.h"

/* get 'struct conn_list' functions: */
#define SPECLIST_TAG conn
#define SPECLIST_TYPE struct connection
#include "speclist_c.h"

/* String used for connection.addr and related cases to indicate
 * blank/unknown/not-applicable address:
 */
const char blank_addr_str[] = "---.---.---.---";

/* This is only used by the server.
   If it is set the disconnection of conns is posponed. This is sometimes
   neccesary as removing a random connection while we are iterating through
   a connection list might corrupt the list. */
int delayed_disconnect = FALSE;
  
/**************************************************************************
  Command access levels for client-side use; at present, they are only
  used to control access to server commands typed at the client chatline.
**************************************************************************/
static const char *levelnames[] = {
  "none",
  "info",
  "ctrl",
  "hack"
};

/**************************************************************************
  Get name associated with given level.  These names are used verbatim in
  commands, so should not be translated here.
**************************************************************************/
const char *cmdlevel_name(enum cmdlevel_id lvl)
{
  assert (lvl >= 0 && lvl < ALLOW_NUM);
  return levelnames[lvl];
}

/**************************************************************************
  Lookup level assocated with token, or ALLOW_UNRECOGNISED if no match.
  Only as many characters as in token need match, so token may be
  abbreviated -- current level names are unique at first character.
  Empty token will match first, i.e. level 'none'.
**************************************************************************/
enum cmdlevel_id cmdlevel_named(const char *token)
{
  enum cmdlevel_id i;
  int len = strlen(token);

  for (i = 0; i < ALLOW_NUM; ++i) {
    if (strncmp(levelnames[i], token, len) == 0) {
      return i;
    }
  }

  return ALLOW_UNRECOGNIZED;
}


/**************************************************************************
  This callback is used when an error occurs trying to write to the
  connection.  The effect of the callback should be to close the
  connection.  This is here so that the server and client can take
  appropriate (different) actions: server lost a client, client lost
  connection to server.
**************************************************************************/
static CLOSE_FUN close_callback;

/**************************************************************************
  Register the close_callback:
**************************************************************************/
void close_socket_set_callback(CLOSE_FUN fun)
{
  close_callback = fun;
}


/**************************************************************************
  Read data from socket, and check if a packet is ready.
  Returns:
    -1  :  an error occured - you should close the socket
    >0  :  number of bytes read
    =0  :  non-blocking sockets only; no data read, would block
**************************************************************************/
int read_socket_data(int sock, struct socket_packet_buffer *buffer)
{
  int didget;

  didget=my_readsocket(sock, (char *)(buffer->data+buffer->ndata),
		       MAX_LEN_PACKET-buffer->ndata);

  if (didget > 0) {
    buffer->ndata+=didget;
    freelog(LOG_DEBUG, "didget:%d", didget);
    return didget;
  }
  else if (didget == 0) {
    freelog(LOG_DEBUG, "EOF on socket read");
    return -1;
  }
#ifdef NONBLOCKING_SOCKETS
  else if (errno == EWOULDBLOCK || errno == EAGAIN) {
    freelog(LOG_DEBUG, "EGAIN on socket read");
    return 0;
  }
#endif
  return -1;
}

/**************************************************************************
  write wrapper function -vasc
**************************************************************************/
static int write_socket_data(struct connection *pc,
			     struct socket_packet_buffer *buf, int limit)
{
  int start, nput, nblock;

  if (pc->delayed_disconnect) {
    if (delayed_disconnect) {
      return 0;
    } else {
      if (close_callback != NULL) {
	(*close_callback)(pc);
      }
      return -1;
    }
  }

  for (start=0; buf->ndata-start>limit;) {
    fd_set writefs, exceptfs;
    struct timeval tv;

    MY_FD_ZERO(&writefs);
    MY_FD_ZERO(&exceptfs);
    FD_SET(pc->sock, &writefs);
    FD_SET(pc->sock, &exceptfs);

    tv.tv_sec = 0; tv.tv_usec = 0;

    if (select(pc->sock+1, NULL, &writefs, &exceptfs, &tv) <= 0) {
      break;
    }

    if (FD_ISSET(pc->sock, &exceptfs)) {
      if (delayed_disconnect) {
	pc->delayed_disconnect = TRUE;
	return 0;
      } else {
	if (close_callback != NULL) {
	  (*close_callback)(pc);
	}
	return -1;
      }
    }

    if (FD_ISSET(pc->sock, &writefs)) {
      nblock=MIN(buf->ndata-start, MAX_LEN_PACKET);
      if((nput=my_writesocket(pc->sock, 
			      (const char *)buf->data+start, nblock)) == -1) {
#ifdef NONBLOCKING_SOCKETS
	if (errno == EWOULDBLOCK || errno == EAGAIN) {
	  break;
	}
#endif
	if (delayed_disconnect) {
	  pc->delayed_disconnect = TRUE;
	  return 0;
	} else {
	  if (close_callback != NULL) {
	    (*close_callback)(pc);
	  }
	  return -1;
	}
      }
      start += nput;
    }
  }

  if (start > 0) {
    buf->ndata -= start;
    memmove(buf->data, buf->data+start, buf->ndata);
    time(&pc->last_write);
  }
  return 0;
}


/**************************************************************************
  flush'em
**************************************************************************/
void flush_connection_send_buffer_all(struct connection *pc)
{
  if (pc != NULL && pc->used && pc->send_buffer->ndata) {
    write_socket_data(pc, pc->send_buffer, 0);
    if (pc->notify_of_writable_data != NULL) {
      pc->notify_of_writable_data(pc, pc->send_buffer != NULL
				  && pc->send_buffer->ndata);
    }
  }
}

/**************************************************************************
  flush'em
**************************************************************************/
void flush_connection_send_buffer_packets(struct connection *pc)
{
  if (pc != NULL && pc->used && pc->send_buffer->ndata >= MAX_LEN_PACKET) {
    write_socket_data(pc, pc->send_buffer, MAX_LEN_PACKET-1);
    if (pc->notify_of_writable_data != NULL) {
      pc->notify_of_writable_data(pc, pc->send_buffer != NULL
				  && pc->send_buffer->ndata);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static int add_connection_data(struct connection *pc, unsigned char *data,
			       int len)
{
  if (pc != NULL && pc->delayed_disconnect) {
    if (delayed_disconnect) {
      return TRUE;
    } else {
      if (close_callback != NULL) {
	(*close_callback)(pc);
      }
      return FALSE;
    }
  }

  if (pc != NULL && pc->used) {
    struct socket_packet_buffer *buf;

    buf = pc->send_buffer;

    /* room for more? */
    if(buf->nsize - buf->ndata < len) {
      buf->nsize += MAX_LEN_PACKET;

      /* added this check so we don't gobble up too much mem */
      if (buf->nsize > MAX_LEN_BUFFER) {
	if (delayed_disconnect) {
	  pc->delayed_disconnect = TRUE;
	  return TRUE;
	} else {
	  if (close_callback != NULL) {
	    (*close_callback)(pc);
	  }
	  return FALSE;
	}
      } else {
	buf->data = fc_realloc(buf->data, buf->nsize);
      }
    }
    memcpy(buf->data + buf->ndata, data, len);
    buf->ndata += len;
    return TRUE;
  }
  return TRUE;
}

/**************************************************************************
  write data to socket
**************************************************************************/
void send_connection_data(struct connection *pc, unsigned char *data, int len)
{
  if (pc != NULL && pc->used) {
    if(pc->send_buffer->do_buffer_sends) {
      flush_connection_send_buffer_packets(pc);
      if (!add_connection_data(pc, data, len)) {
	freelog(LOG_ERROR, "cut connection %s due to huge send buffer (1)",
		conn_description(pc));
      }
      flush_connection_send_buffer_packets(pc);
    }
    else {
      flush_connection_send_buffer_all(pc);
      if (!add_connection_data(pc, data, len)) {
	freelog(LOG_ERROR, "cut connection %s due to huge send buffer (2)",
		conn_description(pc));
      }
      flush_connection_send_buffer_all(pc);
    }
  }
}

/**************************************************************************
  Turn on buffering, using a counter so that calls may be nested.
**************************************************************************/
void connection_do_buffer(struct connection *pc)
{
  if (pc != NULL && pc->used) {
    pc->send_buffer->do_buffer_sends++;
  }
}

/**************************************************************************
  Turn off buffering if internal counter of number of times buffering
  was turned on falls to zero, to handle nested buffer/unbuffer pairs.
  When counter is zero, flush any pending data.
**************************************************************************/
void connection_do_unbuffer(struct connection *pc)
{
  if (pc != NULL && pc->used) {
    pc->send_buffer->do_buffer_sends--;
    if (pc->send_buffer->do_buffer_sends < 0) {
      freelog(LOG_ERROR, "Too many calls to unbuffer %s!", pc->name);
      pc->send_buffer->do_buffer_sends = 0;
    }
    if(pc->send_buffer->do_buffer_sends == 0)
      flush_connection_send_buffer_all(pc);
  }
}

/**************************************************************************
  Convenience functions to buffer/unbuffer a list of connections:
**************************************************************************/
void conn_list_do_buffer(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn)
    connection_do_buffer(pconn);
  conn_list_iterate_end;
}
void conn_list_do_unbuffer(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn)
    connection_do_unbuffer(pconn);
  conn_list_iterate_end;
}

/***************************************************************
  Find connection by exact name, from game.all_connections,
  case-insensitve.  Returns NULL if not found.
***************************************************************/
struct connection *find_conn_by_name(const char *name)
{
  conn_list_iterate(game.all_connections, pconn) {
    if (mystrcasecmp(name, pconn->name)==0) {
      return pconn;
    }
  }
  conn_list_iterate_end;
  return NULL;
}

/***************************************************************
  Like find_conn_by_name(), but allow unambigous prefix
  (ie abbreviation).
  Returns NULL if could not match, or if ambiguous or other
  problem, and fills *result with characterisation of
  match/non-match (see shared.[ch])
***************************************************************/
static const char *connection_accessor(int i) {
  return conn_list_get(&game.all_connections, i)->name;
}
struct connection *find_conn_by_name_prefix(const char *name,
					    enum m_pre_result *result)
{
  int ind;

  *result = match_prefix(connection_accessor,
			 conn_list_size(&game.all_connections),
			 MAX_LEN_NAME-1, mystrncasecmp, name, &ind);
  
  if (*result < M_PRE_AMBIGUOUS) {
    return conn_list_get(&game.all_connections, ind);
  } else {
    return NULL;
  }
}

/***************************************************************
  Find connection by id, from game.all_connections.
  Returns NULL if not found.
  Number of connections will always be relatively small given
  current implementation, so linear search should be fine.
***************************************************************/
struct connection *find_conn_by_id(int id)
{
  conn_list_iterate(game.all_connections, pconn) {
    if (pconn->id == id) {
      return pconn;
    }
  }
  conn_list_iterate_end;
  return NULL;
}

/**************************************************************************
  Return malloced struct, appropriately initialized.
**************************************************************************/
struct socket_packet_buffer *new_socket_packet_buffer(void)
{
  struct socket_packet_buffer *buf;

  buf = fc_malloc(sizeof(*buf));
  buf->ndata = 0;
  buf->do_buffer_sends = 0;
  buf->nsize = 10*MAX_LEN_PACKET;
  buf->data = fc_malloc(buf->nsize);
  return buf;
}

/**************************************************************************
  Free malloced struct
**************************************************************************/
void free_socket_packet_buffer(struct socket_packet_buffer *buf)
{
  if (buf != NULL) {
    if (buf->data != NULL) {
      free(buf->data);
    }
    free(buf);
  }
}

/**************************************************************************
  Return pointer to static string containing a description for this
  connection, based on pconn->name, pconn->addr, and (if applicable)
  pconn->player->name.  (Also pconn->established and pconn->observer.)

  Note that if pconn is client's aconnection (connection to server),
  pconn->name and pconn->addr contain empty string, and pconn->player
  is NULL: in this case return string "server".
**************************************************************************/
const char *conn_description(const struct connection *pconn)
{
  static char buffer[MAX_LEN_NAME*2 + MAX_LEN_ADDR + 128];

  buffer[0] = '\0';

  if (*pconn->name) {
    my_snprintf(buffer, sizeof(buffer), _("%s from %s"),
		pconn->name, pconn->addr); 
  } else {
    sz_strlcpy(buffer, "server");
  }
  if (!pconn->established) {
    sz_strlcat(buffer, _(" (connection incomplete)"));
    return buffer;
  }
  if (pconn->player != NULL) {
    cat_snprintf(buffer, sizeof(buffer), _(" (player %s)"),
		 pconn->player->name);
  }
  if (pconn->observer) {
    sz_strlcat(buffer, _(" (observer)"));
  }
  return buffer;
}

/**************************************************************************
  Get next request id. Takes wrapping of the 16 bit wide unsigned int
  into account.
**************************************************************************/
int get_next_request_id(int old_request_id)
{
  int result = old_request_id + 1;

  if ((result & 0xffff) == 0) {
    freelog(LOG_NORMAL,
	    "INFORMATION: request_id has wrapped around; "
	    "setting from %d to 2", result);
    result = 2;
  }
  assert(result);
  return result;
}
