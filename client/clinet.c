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

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include "capstr.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "support.h"
#include "version.h"

#include "chatline_g.h"
#include "civclient.h"
#include "dialogs_g.h"		/* popdown_races_dialog() */
#include "gui_main_g.h"		/* add_net_input(), remove_net_input() */
#include "packhand.h"

#include "clinet.h"

struct connection aconnection;

extern char metaserver[];

/**************************************************************************
  Close socket and cleanup.  This one doesn't print a message, so should
  do so before-hand if necessary.
**************************************************************************/
static void close_socket_nomessage(struct connection *pc)
{
  pc->used = 0;
  pc->established = 0;
  close(pc->sock);

  /* make sure not to use these accidently: */
  free(pc->buffer);
  free(pc->send_buffer);
  pc->buffer = NULL;
  pc->send_buffer = NULL;
  
  remove_net_input();
  popdown_races_dialog(); 
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
...
**************************************************************************/
int connect_to_server(char *name, char *hostname, int port,
		      char *errbuf, int n_errbuf)
{
  /* use name to find TCPIP address of server */
  struct sockaddr_in src;
  struct hostent *ph;
  long address;
  struct packet_req_join_game req;

  if(port==0)
    port=DEFAULT_SOCK_PORT;
  
  if(!hostname)
    hostname="localhost";
  
  if(isdigit((size_t)*hostname)) {
    if((address = inet_addr(hostname)) == -1) {
      mystrlcpy(errbuf, _("Invalid hostname"), n_errbuf);
      return -1;
    }
    src.sin_addr.s_addr = address;
    src.sin_family = AF_INET;
  }
  else if ((ph = gethostbyname(hostname)) == NULL) {
    mystrlcpy(errbuf, _("Failed looking up host"), n_errbuf);
    return -1;
  }
  else {
    src.sin_family = ph->h_addrtype;
    memcpy((char *) &src.sin_addr, ph->h_addr, ph->h_length);
  }
  
  src.sin_port = htons(port);

#ifdef HAVE_SIGPIPE
  /* ignore broken pipes */
  signal (SIGPIPE, SIG_IGN);
#endif
  
  if((aconnection.sock = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
    mystrlcpy(errbuf, mystrerror(errno), n_errbuf);
    return -1;
  }
  
  if(connect(aconnection.sock, (struct sockaddr *) &src, sizeof (src)) < 0) {
    mystrlcpy(errbuf, mystrerror(errno), n_errbuf);
    close(aconnection.sock);
    return -1;
  }

  if (aconnection.buffer) {
    /* didn't close cleanly previously? */
    freelog(LOG_NORMAL, "Unexpected buffers in connect_to_server()");
    /* get newly initialized ones instead */
    free(aconnection.buffer);
    free(aconnection.send_buffer);
  }
  aconnection.buffer = new_socket_packet_buffer();
  aconnection.send_buffer = new_socket_packet_buffer();
  
  aconnection.used = 1;

  /* gui-dependent details now in gui_main.c: */
  add_net_input(aconnection.sock);
  
  close_socket_set_callback(close_socket_callback);

  /* now send join_request package */

  mystrlcpy(req.short_name, name, MAX_LEN_USERNAME);
  req.major_version=MAJOR_VERSION;
  req.minor_version=MINOR_VERSION;
  req.patch_version=PATCH_VERSION;
  sz_strlcpy(req.version_label, VERSION_LABEL);
  sz_strlcpy(req.capability, our_capability);
  sz_strlcpy(req.name, name);

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
 This function is called when the client received a
 new input from the server
**************************************************************************/
void input_from_server(int fid)
{
  if(read_socket_data(fid, aconnection.buffer)>=0) {
    int type;
    char *packet;

    while((packet=get_packet_from_connection(&aconnection, &type))) {
      handle_packet_input(packet, type);
    }
  }
  else {
    close_socket_callback(&aconnection);
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
  struct hostent *ph;
  int s;
  FILE *f;
  char *proxy_url;
  char urlbuf[512];
  char *urlpath;
  char *server;
  int port;
  char str[512];

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

  if ((ph = gethostbyname(server)) == NULL) {
    mystrlcpy(errbuf, _("Failed looking up host"), n_errbuf);
    return NULL;
  } else {
    addr.sin_family = ph->h_addrtype;
    memcpy((char *) &addr.sin_addr, ph->h_addr, ph->h_length);
  }
  
  addr.sin_port = htons(port);
  
  if((s = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
    mystrlcpy(errbuf, mystrerror(errno), n_errbuf);
    return NULL;
  }
  
  if(connect(s, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    mystrlcpy(errbuf, mystrerror(errno), n_errbuf);
    close(s);
    return NULL;
  }

#ifdef HAVE_FDOPEN
  f=fdopen(s,"r+");
  fprintf(f,"GET %s%s%s HTTP/1.0\r\n\r\n",
    proxy_url ? "" : "/",
    urlpath,
    proxy_url ? metaserver : "");
  fflush(f);
#else
  {
    int i;

    f=tmpfile();
    send(s,"GET ",4,0);
    if(!proxy_url) send(s,"/",1,0);
    send(s,urlpath,strlen(urlpath),0);
    if(proxy_url) send(s,metaserver,strlen(metaserver),0);
    send(s," HTTP/1.0\r\n\r\n", sizeof(" HTTP/1.0\r\n\r\n"),0);

    while ((i = recv(s, str, sizeof(str), 0)) > 0)
      fwrite(str,1,i,f);
    fflush(f);

    close(s);

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

