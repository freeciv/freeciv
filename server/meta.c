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

/*
 * This bit sends "I'm here" packages to the metaserver.
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>
#include <meta.h>
#include <packets.h>

#ifndef INADDR_NONE
#define INADDR_NONE     0xffffffff
#endif

extern int errno;
static int			sockfd,n,in_size;
static struct sockaddr_in	cli_addr,serv_addr;

int send_to_metaserver(char *desc, char *info)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
  
  if(sockfd==0)
    return 0;

  cptr=put_int16(buffer+2,  PACKET_UDP_PCKT);
  cptr=put_string(cptr, desc);
  cptr=put_string(cptr, info);
  put_int16(buffer, cptr-buffer);
  
  n=sendto(sockfd, (const void*)buffer, cptr-buffer,0, 
	   (struct sockaddr *) &serv_addr, sizeof(serv_addr) );
  
  return 1;
}

void server_close_udp(void)
{
  close(sockfd);
}

void server_open_udp(void)
{
  char servername[]=METASERVER_ADDR;
  struct hostent *hp;
  u_int bin;
      
  /*
   * Fill in the structure "serv_addr" with the address of the
   * server that we want to connect with, checks if server address 
   * is valid, both decimal-dotted and name.
   */
  in_size = sizeof(inet_addr(servername));
  bin = inet_addr(servername);
  if ((hp = gethostbyaddr((char *) &bin,in_size,AF_INET)) ==NULL)
    {
      if((hp = gethostbyname(servername)) ==NULL) {
	perror("Metaserver: address error");
	printf("Not reporting to the metaserver in this game\n");
	/* printf("Use option --nometa to always enforce this\n"); */
	fflush(stdout);
	return;
      }
    }
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_port        = htons(METASERVER_PORT);
  memcpy(&serv_addr.sin_addr, hp->h_addr, hp->h_length); 
  
  /*
   * Open a UDP socket (an Internet datagram socket).
   */
  
  if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("metaserver: can't open datagram socket");
    printf("Not reporting to the metaserver in this game\n");
    /* printf("Use option --nometa to always enforce this\n> "); */
    fflush(stdout);
    return;
  }
  /*
   * Bind any local address for us.
   */
  
  memset( &cli_addr, 0, sizeof(cli_addr));	/* zero out */
  cli_addr.sin_family      = AF_INET;
  cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  cli_addr.sin_port        = htons(0);
  if(bind(sockfd, (struct sockaddr *) &cli_addr, sizeof(cli_addr)) < 0) {
    perror("metaserver: can't bind local address");
    printf("Not reporting to the metaserver in this game\n");
    /* printf("Use option --nometa to always enforce this\n> "); */
    fflush(stdout);
    return;
  }

}



