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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/uio.h>
#include <sys/time.h>

#include <pwd.h>
#include <string.h>
#include <errno.h>

#ifdef AIX
#include <sys/select.h>
#endif

#include <signal.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <gtk/gtk.h>

#include <civclient.h>
#include <clinet.h>
#include <log.h>
#include <gmain.h>
#include <packets.h>
#include <game.h>

#include <main.h>

void get_net_input( gpointer data, gint fid, GdkInputCondition condition );

struct connection aconnection;

gint gdk_input_id;

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
  
  if(isdigit((int)*hostname)) {
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
  
  /* ignore broken pipes */
  signal (SIGPIPE, SIG_IGN);
  
  if((aconnection.sock = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
    strcpy(errbuf, strerror(errno));
    return -1;
  }
  
  if(connect(aconnection.sock, (struct sockaddr *) &src, sizeof (src)) < 0) {
    strcpy(errbuf, strerror(errno));
    return -1;
  }

  aconnection.buffer.ndata=0;

  gdk_input_id = gdk_input_add (aconnection.sock,
	GDK_INPUT_READ|GDK_INPUT_EXCEPTION, get_net_input, NULL );
  
  /* now send join_request package */

  strcpy(req.name, name);
  req.major_version=MAJOR_VERSION;
  req.minor_version=MINOR_VERSION;
  req.patch_version=PATCH_VERSION;
  send_packet_req_join_game(&aconnection, &req);

  return 0;
}

/**************************************************************************/
void get_net_input( gpointer data, gint fid, GdkInputCondition condition )
{
  if (read_socket_data(fid, &aconnection.buffer)>0) {  
    int type;
    char *packet;
    
    while((packet=get_packet_from_connection(&aconnection, &type))) {
      handle_packet_input(packet, type);
    }
  }
  else {
    append_output_window("Lost connection to server!");
    log_string(LOG_NORMAL, "lost connection to server");
    close(fid);
    gdk_input_remove(gdk_input_id);
    set_client_state(CLIENT_PRE_GAME_STATE);
  }
}

/**************************************************************************
...
**************************************************************************/
void close_server_connection(void)
{
  close(aconnection.sock);
}

