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
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
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
#include "support.h"

#include "civserver.h"
#include "console.h"
#include "stdinhand.h"

#include "sernet.h"

struct connection connections[MAX_NUM_CONNECTIONS];

#ifdef GENERATING_MAC      /* mac network globals */
TEndpointInfo serv_info;
EndpointRef serv_ep;
#else
static int sock;
extern int port;
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
  static int year;
#ifdef SOCKET_ZERO_ISNT_STDIN
  char buf[BUF_SIZE+1];
  char *bufptr = buf;
#endif
  
  if(year!=game.year) {
    if (server_state == RUN_GAME_STATE) year=game.year;
  }
  if (!game.timeout)
    game.turn_start = time(NULL);
  
  while(1) {
    con_prompt_on();		/* accepting new input */
    
    if(force_end_of_sniff) {
      force_end_of_sniff=0;
      con_prompt_off();
      return 2;
    }
    
    tv.tv_sec=1;
    tv.tv_usec=0;
    
    FD_ZERO(&readfs);
    FD_SET(0, &readfs);	
    FD_SET(sock, &readfs);
    max_desc=sock;
    
    for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
      if(connections[i].used) {
	FD_SET(connections[i].sock, &readfs);
        max_desc=MAX(connections[i].sock, max_desc);
      }
    }
    con_prompt_off();		/* output doesn't generate a new prompt */
    
    if(select(max_desc+1, &readfs, NULL, NULL, &tv)==0) { /* timeout */
      send_server_info_to_metaserver(0,0);
      if((game.timeout) 
	&& (time(NULL)>game.turn_start + game.timeout)
	&& (server_state == RUN_GAME_STATE)){
	con_prompt_off();
	return 0;
      }
#ifdef SOCKET_ZERO_ISNT_STDIN
    if (feof(stdin))
#endif
      continue;
    }
    if (!game.timeout)
      game.turn_start = time(NULL);
  
    if(FD_ISSET(sock, &readfs)) {	     /* new players connects */
      freelog(LOG_VERBOSE, "got new connection");
      if(server_accept_connection(sock)==-1)
	freelog(LOG_NORMAL, "failed accepting connection");
    }
#ifndef SOCKET_ZERO_ISNT_STDIN
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
#else
    else if(!feof(stdin)) {    /* input from server operator */
      /* fetch chars until \n or run out of space in buffer */
      while ((*bufptr=fgetc(stdin)) != EOF) {
          if (*bufptr == '\n') *bufptr = '\0';
          if (*bufptr == '\0') {
              bufptr = buf;
              con_prompt_enter(); /* will need a new prompt, regardless */
              handle_stdin_input((struct player *)NULL, buf);
              break;
          }
          if ((bufptr-buf) <= BUF_SIZE) bufptr++; /* prevent overrun */
      }
  }
#endif
    else {                             /* input from a player */
      for(i=0; i<MAX_NUM_CONNECTIONS; i++)
	if(connections[i].used && FD_ISSET(connections[i].sock, &readfs)) {
	  if(read_socket_data(connections[i].sock, 
			      &connections[i].buffer)>=0) {
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
     && (time(NULL)>game.turn_start + game.timeout))
    return 0;
  return 1;
}
  
  
static void nonblock(int sockfd)
{
#ifdef NONBLOCKING_SOCKETS
#ifdef HAVE_FCNTL
  int f_set;

  if ((f_set=fcntl(sockfd, F_GETFL)) == -1) {
    freelog(LOG_FATAL, "fcntl F_GETFL failed: %s", mystrerror(errno));
  }

  f_set |= O_NONBLOCK;

  if (fcntl(sockfd, F_SETFL, f_set) == -1) {
    freelog(LOG_FATAL, "fcntl F_SETFL failed: %s", mystrerror(errno));
  }
#else
#ifdef HAVE_IOCTL
  long value=1;

  if (ioctl(sockfd, FIONBIO, (char*)&value) == -1) {
  	freelog(LOG_FATAL, "ioctl failed: %s", mystrerror(errno));
  }
#endif
#endif
#endif
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
  int i;

  fromlen = sizeof(fromend);

  if ((new_sock=accept(sockfd, (struct sockaddr *) &fromend, &fromlen)) == -1) {
    freelog(LOG_VERBOSE, "accept failed: %s", mystrerror(errno));
    return -1;
  }

  nonblock(new_sock);

  from=gethostbyaddr((char*)&fromend.sin_addr, sizeof(fromend.sin_addr),
		     AF_INET);

  for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
    if(!connections[i].used) {
      connections[i].used=1;
      connections[i].sock=new_sock;
      connections[i].player=NULL;
      connections[i].buffer.ndata=0;
      connections[i].first_packet=1;
      connections[i].byte_swap=0;
      connections[i].access_level=default_access_level;

      if (from)
	sz_strlcpy(connections[i].addr, from->h_name);
      else
	sz_strlcpy(connections[i].addr, inet_ntoa(fromend.sin_addr));

      freelog(LOG_VERBOSE, "connection from %s", connections[i].addr);
      return 0;
    }
  }

  freelog(LOG_FATAL, "maximum number of connections reached");
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

  /* broken pipes are ignored. */
#ifdef HAVE_SIGPIPE
  signal (SIGPIPE, SIG_IGN);
#endif

  if((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    freelog(LOG_FATAL, "socket failed: %s", mystrerror(errno));
    exit(1);
  }

  opt=1; 
  if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
		(char*)&opt, sizeof(opt)) == -1) {
    freelog(LOG_FATAL, "SO_REUSEADDR failed: %s", mystrerror(errno));
  }

  memset(&src, 0, sizeof(src));
  src.sin_family = AF_INET;
  src.sin_addr.s_addr = htonl(INADDR_ANY);
  src.sin_port = htons(port);


  if(bind(sock, (struct sockaddr *) &src, sizeof (src)) == -1) {
    freelog(LOG_FATAL, "bind failed: %s", mystrerror(errno));
    exit(1);
  }

  if(listen(sock, MAX_NUM_CONNECTIONS) == -1) {
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
