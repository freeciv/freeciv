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
#include <time.h>

#include <unistd.h>

#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac network headers */
#include <OpenTransport.h>
#else /* Unix network headers */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif /* end network header selection */

#include "packets.h"
#include "console.h"
#include "meta.h"

#ifndef INADDR_NONE
#define INADDR_NONE     0xffffffff
#endif

#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac network globals */
TEndpointInfo meta_info;
EndpointRef meta_ep;
InetAddress serv_addr;
#else /* Unix network globals */
extern int errno;
static int			sockfd,n,in_size;
static struct sockaddr_in	cli_addr,serv_addr;
#endif /* end network global selector */

int send_to_metaserver(char *desc, char *info)
{
  unsigned char buffer[MAX_PACKET_SIZE], *cptr;
#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac alternate networking */
  struct TUnitData xmit;
  OSStatus err;
  xmit.udata.maxlen = MAX_PACKET_SIZE;
  xmit.udata.buf=&buffer;
#else  
  if(sockfd==0)
    return 0;
#endif
  cptr=put_int16(buffer+2,  PACKET_UDP_PCKT);
  cptr=put_string(cptr, desc);
  cptr=put_string(cptr, info);
  put_int16(buffer, cptr-buffer);
#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac networking */
  xmit.udata.len=strlen(buffer);
  err=OTSndUData(meta_ep, &xmit);
#else
  n=sendto(sockfd, buffer, cptr-buffer,0, 
	   (struct sockaddr *) &serv_addr, sizeof(serv_addr) );
#endif
  return 1;
}

void server_close_udp(void)
{
#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac networking */
  OTUnbind(meta_ep);
#else
  close(sockfd);
#endif
}

void server_open_udp(void)
{
  char servername[]=METASERVER_ADDR;
  int bad;
#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac networking */
  OSStatus err1;
  OSErr err2;
  MapperRef ref=OTOpenMapper(OTCreateConfiguration("dnr"), 0, &err1);
  TLookupRequest request, reply;
#else
  struct hostent *hp;
  u_int bin;
#endif
  
  /*
   * Fill in the structure "serv_addr" with the address of the
   * server that we want to connect with, checks if server address 
   * is valid, both decimal-dotted and name.
   */
#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac networking */
  request.udata.len=strlen(servername);
  request.udata.buf=&servername;
  err2=OTLookupName(ref, 1, &request, &reply);
  serv_adr=reply.udata.buf;
  bad = ((serv_adr.fHost == 0) || (err2 != 0) || (reply.respcount==0));
#else
  in_size = sizeof(inet_addr(servername));
  bin = inet_addr(servername);
  bad = (((hp = gethostbyaddr((char *) &bin,in_size,AF_INET)) == NULL)
	 && ((hp = gethostbyname(servername)) == NULL));
#endif
  if (bad) {
    perror("Metaserver: address error");
    con_puts(C_METAERROR,"Not reporting to the metaserver in this game.");
    con_flush();
    return;
  }
  /*
   * Open a UDP socket (an Internet datagram socket).
   */
#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac networking */
  meta_ep=OTOpenEndpoint(OTCreateConfiguration(kUDPName), 0, &meta_info, &err1);
  bad = (err1 != 0);
#else  
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_port        = htons(METASERVER_PORT);
  memcpy(&serv_addr.sin_addr, hp->h_addr, hp->h_length); 
  bad = ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0);
#endif
  if (bad) {
    perror("metaserver: can't open datagram socket");
    con_puts(C_METAERROR, "Not reporting to the metaserver in this game.");
    con_flush();
    return;
  }
  /*
   * Bind any local address for us.
   */
  
#if (defined(GENERATING68K) || defined(GENERATINGPPC)) /* mac networking */
  err=OTBind(meta_ep, NULL, NULL);
  bad = (err1 != 0);
#else
  memset( &cli_addr, 0, sizeof(cli_addr));	/* zero out */
  cli_addr.sin_family      = AF_INET;
  cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  cli_addr.sin_port        = htons(0);
  bad = (bind(sockfd, (struct sockaddr *) &cli_addr, sizeof(cli_addr)) < 0);
#endif
  if (bad) {
    perror("metaserver: can't bind local address");
    con_puts(C_METAERROR, "Not reporting to the metaserver in this game.");
    con_flush();
    return;
  }
}
