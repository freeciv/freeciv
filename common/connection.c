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

#include "log.h"
#include "mem.h"
#include "netintf.h"

#include "connection.h"

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
static CLOSE_FUN *close_callback;

/**************************************************************************
  Register the close_callback:
**************************************************************************/
void close_socket_set_callback(CLOSE_FUN *fun)
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

  didget=read(sock, (char *)(buffer->data+buffer->ndata), 
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
			     struct socket_packet_buffer *buf)
{
  int start, nput, nblock;

  for (start=0; start<buf->ndata;) {
    fd_set writefs, exceptfs;
    struct timeval tv;

    MY_FD_ZERO(&writefs);
    MY_FD_ZERO(&exceptfs);
    FD_SET(pc->sock, &writefs);
    FD_SET(pc->sock, &exceptfs);

    tv.tv_sec = 2; tv.tv_usec = 0;

    if (select(pc->sock+1, NULL, &writefs, &exceptfs, &tv) <= 0) {
      buf->ndata -= start;
      memmove(buf->data, buf->data+start, buf->ndata);
      return -1;
    }

    if (FD_ISSET(pc->sock, &exceptfs)) {
      if (close_callback) {
	(*close_callback)(pc);
      }
      return -1;
    }

    if (FD_ISSET(pc->sock, &writefs)) {
      nblock=MIN(buf->ndata-start, 4096);
      if((nput=write(pc->sock, (const char *)buf->data+start, nblock)) == -1) {
#ifdef NONBLOCKING_SOCKETS
	if (errno == EWOULDBLOCK || errno == EAGAIN) {
	  continue;
	}
#endif
	if (close_callback) {
	  (*close_callback)(pc);
	}
	return -1;
      }
      start += nput;
    }
  }

  buf->ndata=0;
  return 0;
}


/**************************************************************************
  flush'em
**************************************************************************/
static void flush_connection_send_buffer(struct connection *pc)
{
  if(pc) {
    if(pc->send_buffer->ndata) {
      write_socket_data(pc, pc->send_buffer);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static int add_connection_data(struct connection *pc, unsigned char *data,
			       int len)
{
  if (pc) {
    if(10*MAX_LEN_PACKET-pc->send_buffer->ndata >= len) { /* room for more? */
      memcpy(pc->send_buffer->data + pc->send_buffer->ndata, data, len);
      pc->send_buffer->ndata += len;
      return 1;
    }
    return 0;
  }
  return 1;
}

/**************************************************************************
  write data to socket
**************************************************************************/
int send_connection_data(struct connection *pc, unsigned char *data, int len)
{
  if(pc) {
    if(pc->send_buffer->do_buffer_sends) {
      if (!add_connection_data(pc, data, len)) {
	flush_connection_send_buffer(pc);
	if (!add_connection_data(pc, data, len)) {
	  freelog(LOG_DEBUG, "send buffer filled, packet discarded");
	}
      }
    }
    else {
      flush_connection_send_buffer(pc);
      if (!add_connection_data(pc, data, len)) {
	freelog(LOG_DEBUG, "send buffer filled, packet discarded");
      }
      flush_connection_send_buffer(pc);
    }
  }
  return 0;
}

/**************************************************************************
  Turn on buffering, using a counter so that calls may be nested.
**************************************************************************/
void connection_do_buffer(struct connection *pc)
{
  if(pc)
    pc->send_buffer->do_buffer_sends++;
}

/**************************************************************************
  Turn off buffering if internal counter of number of times buffering
  was turned on falls to zero, to handle nested buffer/unbuffer pairs.
  When counter is zero, flush any pending data.
**************************************************************************/
void connection_do_unbuffer(struct connection *pc)
{
  if(pc) {
    pc->send_buffer->do_buffer_sends--;
    if(pc->send_buffer->do_buffer_sends == 0)
      flush_connection_send_buffer(pc);
  }
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
  return buf;
}
