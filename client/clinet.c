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
#include "game.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "version.h"

#include "chatline_g.h"
#include "civclient.h"
#include "dialogs_g.h"		/* popdown_races_dialog() */
#include "gui_main_g.h"		/* add_net_input(), remove_net_input() */
#include "packhand.h"

#include "clinet.h"

struct connection aconnection;

/**************************************************************************
...
**************************************************************************/
int connect_to_server(char *name, char *hostname, int port, char *errbuf)
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
      strcpy(errbuf, "Invalid hostname");
      return -1;
    }
    src.sin_addr.s_addr = address;
    src.sin_family = AF_INET;
  }
  else if ((ph = gethostbyname(hostname)) == NULL) {
    strcpy(errbuf, "Failed looking up host");
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
    strcpy(errbuf, mystrerror(errno));
    return -1;
  }
  
  if(connect(aconnection.sock, (struct sockaddr *) &src, sizeof (src)) < 0) {
    strcpy(errbuf, mystrerror(errno));
    close(aconnection.sock);
    return -1;
  }

  aconnection.buffer.ndata=0;

  /* gui-dependent details now in gui_main.c: */
  add_net_input(aconnection.sock);
  

  /* now send join_request package */

  strncpy(req.short_name, name, MAX_LEN_USERNAME);
  req.short_name[MAX_LEN_USERNAME-1] = '\0';
  req.major_version=MAJOR_VERSION;
  req.minor_version=MINOR_VERSION;
  req.patch_version=PATCH_VERSION;
  strcpy(req.capability, our_capability);
  strcpy(req.name, name);

  send_packet_req_join_game(&aconnection, &req);
  
  return 0;
}

/**************************************************************************
...
**************************************************************************/
void disconnect_from_server(void)
{
  append_output_window("Disconnecting from server.");
  close(aconnection.sock);
  remove_net_input();
  set_client_state(CLIENT_PRE_GAME_STATE);
}  

/**************************************************************************
 This function is called when the client received a
 new input from the server
**************************************************************************/
void input_from_server(int fid)
{
  if(read_socket_data(fid, &aconnection.buffer)>0) {
    int type;
    char *packet;

    while((packet=get_packet_from_connection(&aconnection, &type))) {
      handle_packet_input(packet, type);
    }
  }
  else {
    append_output_window("Lost connection to server!");
    freelog(LOG_NORMAL, "lost connection to server");
    close(fid);
    remove_net_input();
    popdown_races_dialog(); 
    set_client_state(CLIENT_PRE_GAME_STATE);
  }
}

