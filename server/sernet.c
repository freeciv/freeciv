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

#include <sys/signal.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <string.h>

#if defined(AIX) || defined(__EMX__)
#include <sys/select.h>
#endif

#include <signal.h>

#include <sys/socket.h>
#include <netdb.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <log.h>
#include <sernet.h>
#include <shared.h>
#include <civserver.h>
#include <stdinhand.h>
#include <packets.h>

#define MY_FD_ZERO(p) memset((void *)(p), 0, sizeof(*(p)))

struct connection connections[MAX_CONNECTIONS];
int no_active_connections;
int sock;
extern int errno;
extern int port;
extern int force_end_of_sniff;

extern enum server_states server_state;



/*****************************************************************************/
void close_connection(struct connection *pconn)
{
  close(pconn->sock);
  pconn->used=0;
}

/*****************************************************************************/
void close_connections_and_socket(void)
{
  int i;
  for(i=0; i<MAX_CONNECTIONS; i++)
    if(connections[i].used) {
      close(connections[i].sock);
      connections[i].used=0;
      close(sock);
    }
}

/*****************************************************************************/
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
    year=game.year;
  }
  
  while(1) {
    if(force_end_of_sniff) {
      force_end_of_sniff=0;
      return 2;
    }
    
    tv.tv_sec=1; tv.tv_usec=0;
    
    MY_FD_ZERO(&readfs);
    FD_SET(0, &readfs);	
    FD_SET(sock, &readfs);
    max_desc=sock;
    
    for(i=0; i<MAX_CONNECTIONS; i++) {
      if(connections[i].used) {
	FD_SET(connections[i].sock, &readfs);
      }
      max_desc=MAX(connections[i].sock, max_desc);
    }
    
    if(select(max_desc+1, &readfs, NULL, NULL, &tv)==0) { /* timeout */
      send_server_info_to_metaserver(0);
      if(game.timeout && time(NULL)>time_at_turn_end) {
	return 0;
      }
      continue;
    }
  
    if(FD_ISSET(sock, &readfs)) {	     /* new players connects */
      flog(LOG_DEBUG, "got new connection");
      if(server_accept_connection(sock)==-1)
	flog(LOG_NORMAL, "failed accepting connection");
    }
    else if(FD_ISSET(0, &readfs)) {    /* input from server operator */
      int didget;
      char buf[BUF_SIZE+1];
      
      if((didget=read(0, buf, BUF_SIZE))==-1) {
	flog(LOG_FATAL, "read from stdin failed");
	exit(1);
      }
      *(buf+didget)='\0';
      handle_stdin_input(buf);
    }
    else {                             /* input from a player */
      for(i=0; i<MAX_CONNECTIONS; i++)
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
	    connections[i].used=0;
	    close(connections[i].sock);
	    
	  }
	}
    }
    break;
  }
  if(game.timeout && time(NULL)>time_at_turn_end) return 0;
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
    for(i=0; i<MAX_CONNECTIONS; i++)
      if(!connections[i].used) {
	connections[i].used=1;
	connections[i].sock=new_sock;
	connections[i].player=NULL;
	connections[i].buffer.ndata=0;

	if(from) {
	  strncpy(connections[i].addr, from->h_name, ADDR_LENGTH);
	  connections[i].addr[ADDR_LENGTH-1]='\0';
	}
	else
	   strcpy(connections[i].addr, "unknown");

	return 0;
      }
    flog(LOG_FATAL, "maximum number of connections reached");
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
  signal (SIGPIPE, SIG_IGN);
  
  if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    flog(LOG_FATAL, "socket failed: %s", strerror(errno));
    exit(1);
  }

  opt=1; 
  if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
		(char*)&opt, sizeof(opt))) {
/*		(const char*)&opt, sizeof(opt))) {      gave me warnings -- Syela */
    flog(LOG_FATAL, "setsockopt failed: %s", strerror(errno));
  }

  if(bind(sock, (struct sockaddr *) &src, sizeof (src)) < 0) {
    flog(LOG_FATAL, "bind failed: %s", strerror(errno));
    exit(1);
  }

  if(listen(sock, MAX_CONNECTIONS) < 0) {
    flog(LOG_FATAL, "listen failed: %s", strerror(errno));
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
  for(i=0; i<MAX_CONNECTIONS; i++) { 
    connections[i].used=0;
    connections[i].buffer.ndata=0;
  }
  no_active_connections=0;
}
