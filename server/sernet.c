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
#include <time.h>
#include <errno.h>
#include <signal.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
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

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include "log.h"
#include "packets.h"
#include "shared.h"

#include "civserver.h"
#include "console.h"
#include "stdinhand.h"

#include "sernet.h"

#define MY_FD_ZERO(p) memset((void *)(p), 0, sizeof(*(p)))

struct connection connections[MAX_NUM_CONNECTIONS];

#ifdef GENERATING_MAC      /* mac network globals */
TEndpointInfo serv_info;
EndpointRef serv_ep;
#else
static int sock;
extern int port;
extern int errno;
/* Is it necessary for us to declare errno on any platform?
   It should already be declared in errno.h (included above),
   and may really be a macro or some other magic.  If it is
   necessary, we should do some configure magic and only
   declare it when necessary.  --dwp
*/
#endif
extern int force_end_of_sniff;


/*****************************************************************************/
void close_connection(struct connection *pconn)
{
  close(pconn->sock);
  pconn->used=0;
  pconn->access_level=ALLOW_NONE;
}

/*****************************************************************************/
void close_connections_and_socket(void)
{
  int i;
  
  for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
    if(connections[i].used) {
      close_connection(&connections[i]);
    }
  }
  close(sock);
}

/*****************************************************************************
Get and handle:
- new connections,
- input from connections,
- input from server operator in stdin
Returns:
  0 if went past end-of-turn timeout
  2 if force_end_of_sniff found to be set
  1 otherwise (got and processed something?)
This function also handles prompt printing, via the con_prompt_*
functions.  That is, other functions should not need to do so.  --dwp
*****************************************************************************/
int sniff_packets(void)
{
  int i;
  int max_desc;
  fd_set readfs;
  struct timeval tv;
  static time_t time_at_turn_end;
  static int year;
  
  if(year!=game.year) {
    time_at_turn_end = time(NULL) + game.timeout;
    if (server_state == RUN_GAME_STATE) year=game.year;
  }
  
  while(1) {
    con_prompt_on();		/* accepting new input */
    
    if(force_end_of_sniff) {
      force_end_of_sniff=0;
      con_prompt_off();
      return 2;
    }
    
    tv.tv_sec=1; tv.tv_usec=0;
    
    MY_FD_ZERO(&readfs);
    FD_SET(0, &readfs);	
    FD_SET(sock, &readfs);
    max_desc=sock;
    
    for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
      if(connections[i].used) {
	FD_SET(connections[i].sock, &readfs);
      }
      max_desc=MAX(connections[i].sock, max_desc);
    }
    con_prompt_off();		/* output doesn't generate a new prompt */
    
    if(select(max_desc+1, &readfs, NULL, NULL, &tv)==0) { /* timeout */
      send_server_info_to_metaserver(0,0);
      if((game.timeout) 
	&& (time(NULL)>time_at_turn_end)
	&& (server_state == RUN_GAME_STATE)){
	con_prompt_off();
	return 0;
      }
      continue;
    }
  
    if(FD_ISSET(sock, &readfs)) {	     /* new players connects */
      freelog(LOG_VERBOSE, "got new connection");
      if(server_accept_connection(sock)==-1)
	freelog(LOG_NORMAL, "failed accepting connection");
    }
    else if(FD_ISSET(0, &readfs)) {    /* input from server operator */
      int didget;
      char buf[BUF_SIZE+1];
      
      if((didget=read(0, buf, BUF_SIZE))==-1) {
	freelog(LOG_FATAL, "read from stdin failed");
	exit(1);
      }
      *(buf+didget)='\0';
      con_prompt_enter();	/* will need a new prompt, regardless */
      handle_stdin_input((struct player *)NULL, buf);
    }
    else {                             /* input from a player */
      for(i=0; i<MAX_NUM_CONNECTIONS; i++)
	if(connections[i].used && FD_ISSET(connections[i].sock, &readfs)) {
	  if(read_socket_data(connections[i].sock, 
			      &connections[i].buffer)>0) {
	    char *packet;
	    int type;
	    while((packet=get_packet_from_connection(&connections[i], &type)))
	      handle_packet_input(&connections[i], packet, type);
	  }
	  else {
	    lost_connection_to_player(&connections[i]);
	    close_connection(&connections[i]);
	  }
	}
    }
    break;
  }
  con_prompt_off();
  
  if((game.timeout) 
    && (time(NULL)>time_at_turn_end)
    && (game.timeout)) return 0;
  return 1;
}
  
  
/********************************************************************
 server accepts connections from client
********************************************************************/
int server_accept_connection(int sockfd)
{
  int fromlen;
  int new_sock;
  struct sockaddr_in fromend;
  struct hostent *from;

  fromlen = sizeof fromend;

  new_sock = accept(sockfd, (struct sockaddr *) &fromend, &fromlen);

  from=gethostbyaddr((char*)&fromend.sin_addr, sizeof(sizeof(fromend.sin_addr)), 
		     AF_INET);

  if(new_sock!=-1) {
    int i;
    for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
      if(!connections[i].used) {
	connections[i].used=1;
	connections[i].sock=new_sock;
	connections[i].player=NULL;
	connections[i].buffer.ndata=0;
	connections[i].first_packet=1;
	connections[i].byte_swap=0;
	connections[i].access_level=default_access_level;

	if(from) {
	  strncpy(connections[i].addr, from->h_name, MAX_LEN_ADDR);
	  connections[i].addr[MAX_LEN_ADDR-1]='\0';
	}
	else {
	   strcpy(connections[i].addr, "unknown");
	}
	freelog(LOG_VERBOSE, "connection from %s", connections[i].addr);
	return 0;
      }
    }
    freelog(LOG_FATAL, "maximum number of connections reached");
    return -1;
  }

  return -1;
}



/********************************************************************
 open server socket to be used to accept client connections
********************************************************************/
int server_open_socket(void)
{
  /* setup socket address */
  struct sockaddr_in src;
  int opt;

  src.sin_addr.s_addr = INADDR_ANY;
  src.sin_family = AF_INET;
  src.sin_port = htons(port);


  /* broken pipes are ignored. */
#ifdef HAVE_SIGPIPE
  signal (SIGPIPE, SIG_IGN);
#endif

  if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    freelog(LOG_FATAL, "socket failed: %s", mystrerror(errno));
    exit(1);
  }

  opt=1; 
  if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
		(char*)&opt, sizeof(opt))) {
/*		(const char*)&opt, sizeof(opt))) {      gave me warnings -- Syela */
    freelog(LOG_FATAL, "setsockopt failed: %s", mystrerror(errno));
  }

  if(bind(sock, (struct sockaddr *) &src, sizeof (src)) < 0) {
    freelog(LOG_FATAL, "bind failed: %s", mystrerror(errno));
    exit(1);
  }

  if(listen(sock, MAX_NUM_CONNECTIONS) < 0) {
    freelog(LOG_FATAL, "listen failed: %s", mystrerror(errno));
    exit(1);
  }

  return 0;
}


/********************************************************************
...
********************************************************************/
void init_connections(void)
{
  int i;
  for(i=0; i<MAX_NUM_CONNECTIONS; i++) { 
    connections[i].used=0;
    connections[i].buffer.ndata=0;
  }
}
