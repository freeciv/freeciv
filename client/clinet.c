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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#include "capstr.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "packets.h"
#include "support.h"
#include "version.h"

#include "chatline_g.h"
#include "civclient.h"
#include "dialogs_g.h"		/* popdown_races_dialog() */
#include "gui_main_g.h"		/* add_net_input(), remove_net_input() */
#include "messagewin_g.h"
#include "packhand.h"
#include "plrdlg_g.h"
#include "repodlgs_g.h"

#include "clinet.h"

struct connection aconnection;
static struct sockaddr_in server_addr;

/**************************************************************************
  Close socket and cleanup.  This one doesn't print a message, so should
  do so before-hand if necessary.
**************************************************************************/
static void close_socket_nomessage(struct connection *pc)
{
  pc->used = 0;
  pc->established = 0;
  my_closesocket(pc->sock);

  /* make sure not to use these accidently: */
  free_socket_packet_buffer(pc->buffer);
  free_socket_packet_buffer(pc->send_buffer);
  pc->buffer = NULL;
  pc->send_buffer = NULL;

  remove_net_input();
  popdown_races_dialog(); 

  plrdlg_update_delay_off();
  report_update_delay_off();
  meswin_update_delay_off();
  
  set_client_state(CLIENT_PRE_GAME_STATE);
}

/**************************************************************************
...
**************************************************************************/
static void close_socket_callback(struct connection *pc)
{
  append_output_window(_("Lost connection to server!"));
  freelog(LOG_NORMAL, "lost connection to server");
  close_socket_nomessage(pc);
}

/**************************************************************************
  Connect to a civserver instance -- or at least try to.  On success,
  return 0; on failure, put an error message in ERRBUF and return -1.
**************************************************************************/
int connect_to_server(char *name, char *hostname, int port,
		      char *errbuf, int errbufsize)
{
  if (get_server_address(hostname, port, errbuf, errbufsize) != 0) {
    return -1;
  }

  if (try_to_connect(name, errbuf, errbufsize) != 0) {
    return -1;
  }
  return 0;
}

/**************************************************************************
  Get ready to [try to] connect to a server:
   - translate HOSTNAME and PORT (with defaults of "localhost" and
     DEFAULT_SOCK_PORT respectively) to a raw IP address and port number, and
     store them in the `server_addr' variable
   - return 0 on success
     or put an error message in ERRBUF and return -1 on failure
**************************************************************************/
int get_server_address(char *hostname, int port, char *errbuf,
		       int errbufsize)
{
  if (port == 0)
    port = DEFAULT_SOCK_PORT;

  /* use name to find TCP/IP address of server */
  if (!hostname)
    hostname = "localhost";

  if (!fc_lookup_host(hostname, &server_addr)) {
    mystrlcpy(errbuf, _("Failed looking up host"), errbufsize);
    return -1;
  }

  server_addr.sin_port = htons(port);

#ifdef HAVE_SIGPIPE
  /* ignore broken pipes */
  signal(SIGPIPE, SIG_IGN);
#endif

  return 0;
}

/**************************************************************************
  Try to connect to a server (get_server_address() must be called first!):
   - try to create a TCP socket and connect it to `server_addr'
   - if successful:
	  - start monitoring the socket for packets from the server
	  - send a "join game request" packet to the server
      and - return 0
   - if unable to create the connection, close the socket, put an error
     message in ERRBUF and return the Unix error code (ie., errno, which
     will be non-zero).
**************************************************************************/
int try_to_connect(char *user_name, char *errbuf, int errbufsize)
{
  struct packet_req_join_game req;

  if ((aconnection.sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    mystrlcpy(errbuf, mystrerror(errno), errbufsize);
    return -1;
  }

  if (connect
      (aconnection.sock, (struct sockaddr *) &server_addr,
       sizeof(server_addr)) < 0) {
    mystrlcpy(errbuf, mystrerror(errno), errbufsize);
    my_closesocket(aconnection.sock);
    aconnection.sock = -1;
    return errno;
  }

  if (aconnection.buffer) {
    /* didn't close cleanly previously? */
    freelog(LOG_ERROR, "Unexpected buffers in try_to_connect()");
    /* get newly initialized ones instead */
    free_socket_packet_buffer(aconnection.buffer);
    free_socket_packet_buffer(aconnection.send_buffer);
  }

  aconnection.buffer = new_socket_packet_buffer();
  aconnection.send_buffer = new_socket_packet_buffer();
  aconnection.last_write = 0;
  aconnection.client.last_request_id_used = 0;
  aconnection.client.last_processed_request_id_seen = 0;
  aconnection.client.request_id_of_last_pong = 0;
  aconnection.incomming_packet_notify = notify_about_incomming_packet;
  aconnection.outgoing_packet_notify = notify_about_outgoing_packet;
  aconnection.used = 1;

  /* call gui-dependent stuff in gui_main.c */
  add_net_input(aconnection.sock);

  /* now send join_request package */

  mystrlcpy(req.short_name, user_name, MAX_LEN_USERNAME);
  req.major_version = MAJOR_VERSION;
  req.minor_version = MINOR_VERSION;
  req.patch_version = PATCH_VERSION;
  sz_strlcpy(req.version_label, VERSION_LABEL);
  sz_strlcpy(req.capability, our_capability);
  sz_strlcpy(req.name, user_name);
  
  send_packet_req_join_game(&aconnection, &req);

  return 0;
}

/**************************************************************************
...
**************************************************************************/
void disconnect_from_server(void)
{
  append_output_window(_("Disconnecting from server."));
  close_socket_nomessage(&aconnection);
}  

/**************************************************************************
A wrapper around read_socket_data() which also handles the case the
socket becomes writeable and there is still data which should be sent
to the server.

Returns:
    -1  :  an error occured - you should close the socket
    >0  :  number of bytes read
    =0  :  no data read, would block
**************************************************************************/
static int read_from_connection(struct connection *pc)
{
  for (;;) {
    fd_set readfs, writefs, exceptfs;
    int socket_fd = pc->sock;
    int have_data_for_server = (pc->used && pc->send_buffer
				&& pc->send_buffer->ndata);
    int n;
    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    MY_FD_ZERO(&readfs);
    FD_SET(socket_fd, &readfs);

    MY_FD_ZERO(&exceptfs);
    FD_SET(socket_fd, &exceptfs);

    if (have_data_for_server) {
      MY_FD_ZERO(&writefs);
      FD_SET(socket_fd, &writefs);
      n = select(socket_fd + 1, &readfs, &writefs, &exceptfs, &tv);
    } else {
      n = select(socket_fd + 1, &readfs, NULL, &exceptfs, &tv);
    }

    /* the socket is neither readable, writeable nor got an
       exception */
    if (n == 0) {
      return 0;
    }

    if (n == -1) {
      if (errno == EINTR) {
	freelog(LOG_DEBUG, "select() returned EINTR");
	continue;
      }

      freelog(LOG_NORMAL, "error in select() return=%d errno=%d (%s)",
	      n, errno, strerror(errno));
      return -1;
    }

    if (FD_ISSET(socket_fd, &exceptfs)) {
      return -1;
    }

    if (have_data_for_server && FD_ISSET(socket_fd, &writefs)) {
      flush_connection_send_buffer_all(pc);
      if (pc->notify_of_writable_data) {
	pc->notify_of_writable_data(pc, pc->send_buffer
				    && pc->send_buffer->ndata);
      }
    }

    if (FD_ISSET(socket_fd, &readfs)) {
      return read_socket_data(socket_fd, pc->buffer);
    }
  }
}

/**************************************************************************
 This function is called when the client received a new input from the
 server.
**************************************************************************/
void input_from_server(int fid)
{
  if (read_from_connection(&aconnection) >= 0) {
    int type, result;
    char *packet;

    while (1) {
      packet = get_packet_from_connection(&aconnection, &type, &result);
      if (result) {
	handle_packet_input(packet, type);
      } else {
	break;
      }
    }
  } else {
    close_socket_callback(&aconnection);
  }
}

/**************************************************************************
 This function will sniff at the given fd, get the packet and call
 handle_packet_input. It will return if there is a network error or if
 the PACKET_PROCESSING_FINISHED packet for the given request is
 received.
**************************************************************************/
void input_from_server_till_request_got_processed(int fd, 
						  int expected_request_id)
{
  assert(expected_request_id);

  freelog(LOG_DEBUG,
	  "input_from_server_till_request_got_processed("
	  "expected_request_id=%d)", expected_request_id);

  while (1) {
    if (read_from_connection(&aconnection) >= 0) {
      int type, result;
      char *packet;

      while (1) {
	packet = get_packet_from_connection(&aconnection, &type, &result);
	if (!result) {
	  break;
	}

	handle_packet_input(packet, type);

	if (type == PACKET_PROCESSING_FINISHED
	    && aconnection.client.last_processed_request_id_seen >=
	    expected_request_id) {
	  freelog(LOG_DEBUG, "ifstrgp: got it; returning");
	  return;
	}
      }
    } else {
      close_socket_callback(&aconnection);
    }
  }
}

#define SPECLIST_TAG server
#define SPECLIST_TYPE struct server
#include "speclist_c.h"

/**************************************************************************
 Create the list of servers from the metaserver
 The result must be free'd with delete_server_list() when no
 longer used
**************************************************************************/
struct server_list *create_server_list(char *errbuf, int n_errbuf)
{
  struct server_list *server_list;
  struct sockaddr_in addr;
  int s;
  FILE *f;
  char *proxy_url;
  char urlbuf[512];
  char *urlpath;
  char *server;
  int port;
  char str[512];
  char machine_string[128];
#ifdef HAVE_UNAME
  struct utsname un;
#endif 

  if ((proxy_url = getenv("http_proxy"))) {
    if (strncmp(proxy_url,"http://",strlen("http://"))) {
      mystrlcpy(errbuf,
		_("Invalid $http_proxy value, must start with 'http://'"),
		n_errbuf);
      return NULL;
    }
    sz_strlcpy(urlbuf, proxy_url);
  } else {
    if (strncmp(metaserver,"http://",strlen("http://"))) {
      mystrlcpy(errbuf, _("Invalid metaserver URL, must start with 'http://'"),
		n_errbuf);
      return NULL;
    }
    sz_strlcpy(urlbuf, metaserver);
  }
  server = &urlbuf[strlen("http://")];

  {
    char *s;
    if ((s = strchr(server,':'))) {
      port = atoi(&s[1]);
      if (!port) {
        port = 80;
      }
      s[0] = '\0';
      ++s;
      while (isdigit(s[0])) {++s;}
    } else {
      port = 80;
      if (!(s = strchr(server,'/'))) {
        s = &server[strlen(server)];
      }
    }  /* s now points past the host[:port] part */

    if (s[0] == '/') {
      s[0] = '\0';
      ++s;
    } else if (s[0]) {
      mystrlcpy(errbuf,
		_("Invalid $http_proxy value, cannot find separating '/'"),
		n_errbuf);
      /* which is obligatory if more characters follow */
      return NULL;
    }
    urlpath = s;
  }

  if (!fc_lookup_host(server, &addr)) {
    mystrlcpy(errbuf, _("Failed looking up host"), n_errbuf);
    return NULL;
  }
  
  addr.sin_port = htons(port);
  
  if((s = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
    mystrlcpy(errbuf, mystrerror(errno), n_errbuf);
    return NULL;
  }
  
  if(connect(s, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    mystrlcpy(errbuf, mystrerror(errno), n_errbuf);
    my_closesocket(s);
    return NULL;
  }

#ifdef HAVE_UNAME
  uname(&un);
  my_snprintf(machine_string,sizeof(machine_string),
              "%s %s [%s]",
              un.sysname,
              un.release,
              un.machine);
#else /* ! HAVE_UNAME */
  /* Fill in here if you are making a binary without sys/utsname.h and know
     the OS name, release number, and machine architechture */
  my_snprintf(machine_string,sizeof(machine_string),
              "unknown unknown [unknown]");
#endif /* HAVE_UNAME */

  my_snprintf(str,sizeof(str),
              "GET %s%s%s HTTP/1.0\r\nUser-Agent: Freeciv/%s %s %s\r\n\r\n",
              proxy_url ? "" : "/",
              urlpath,
              proxy_url ? metaserver : "",
              VERSION_STRING,
              client_string,
              machine_string);

#ifdef HAVE_FDOPEN
  f=fdopen(s,"r+");
  fwrite(str,1,strlen(str),f);
  fflush(f);
#else
  {
    int i;

    f=tmpfile();
    my_writesocket(s,str,strlen(str));
    
    while ((i = my_readsocket(s, str, sizeof(str))) > 0)
      fwrite(str,1,i,f);
    fflush(f);

    my_closesocket(s);

    fseek(f,0,SEEK_SET);
  }
#endif

#define NEXT_FIELD p=strstr(p,"<TD>"); if(p==NULL) continue; p+=4;
#define END_FIELD  p=strstr(p,"</TD>"); if(p==NULL) continue; *p++='\0';
#define GET_FIELD(x) NEXT_FIELD (x)=p; END_FIELD

  server_list = fc_malloc(sizeof(struct server_list));
  server_list_init(server_list);

  while(fgets(str, 512, f) != NULL) {
    if((0 == strncmp(str, "<TR BGCOLOR",11)) && strchr(str, '\n')) {
      char *name,*port,*version,*status,*players,*metastring;
      char *p;
      struct server *pserver = (struct server*)fc_malloc(sizeof(struct server));

      p=strstr(str,"<a"); if(p==NULL) continue;
      p=strchr(p,'>');    if(p==NULL) continue;
      name=++p;
      p=strstr(p,"</a>"); if(p==NULL) continue;
      *p++='\0';

      GET_FIELD(port);
      GET_FIELD(version);
      GET_FIELD(status);
      GET_FIELD(players);
      GET_FIELD(metastring);

      pserver->name = mystrdup(name);
      pserver->port = mystrdup(port);
      pserver->version = mystrdup(version);
      pserver->status = mystrdup(status);
      pserver->players = mystrdup(players);
      pserver->metastring = mystrdup(metastring);

      server_list_insert(server_list, pserver);
    }
  }
  fclose(f);

  return server_list;
}

/**************************************************************************
 Frees everything associated with a server list including
 the server list itself (so the server_list is no longer
 valid after calling this function)
**************************************************************************/
void delete_server_list(struct server_list *server_list)
{
  server_list_iterate(*server_list, ptmp)
    free(ptmp->name);
    free(ptmp->port);
    free(ptmp->version);
    free(ptmp->status);
    free(ptmp->players);
    free(ptmp->metastring);
    free(ptmp);
  server_list_iterate_end;

  server_list_unlink_all(server_list);
	free(server_list);
}

