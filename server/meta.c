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

/*
The desc block should look like this:
1) GameName   - Freeciv
2) Version    - 1.5.0
3) State      - Running(Open for new players)
4) Host       - Empty
5) Port       - 5555
6) NoPlayers  - 3
7) InfoText   - Warfare - starts at 8:00 EMT

The info string should look like this:
  GameSpecific text block
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif 

#ifdef HAVE_OPENTRANSPORT
#include <OpenTransport.h>
#include <OpenTptInternet.h>
#endif

#include "fcintl.h"
#include "log.h"
#include "packets.h"
#include "support.h"

#include "console.h"

#include "meta.h"

#ifndef INADDR_NONE
#define INADDR_NONE     0xffffffff
#endif

int server_is_open=0;

#ifdef GENERATING_MAC    /* mac network globals */
TEndpointInfo meta_info;
EndpointRef meta_ep;
InetAddress serv_addr;
#else /* Unix network globals */
static int			sockfd=0;
static struct sockaddr_in	cli_addr,serv_addr;
#endif /* end network global selector */

extern char metaserver_addr[];
extern unsigned short int metaserver_port;


void meta_addr_split(void)
{
  char *metaserver_port_separator;
  int specified_port;

  if ((metaserver_port_separator = strchr(metaserver_addr,':'))) {
    metaserver_port_separator[0] = '\0';
    if ((specified_port=atoi(&metaserver_port_separator[1]))) {
      metaserver_port = (unsigned short int)specified_port;
    }
  }
}

char *meta_addr_port(void)
{
  static char retstr[300];

  if (metaserver_port == DEFAULT_META_SERVER_PORT) {
    sz_strlcpy(retstr, metaserver_addr);
  } else {
    my_snprintf(retstr, sizeof(retstr),
		"%s:%d", metaserver_addr, metaserver_port);
  }

  return retstr;
}

int send_to_metaserver(char *desc, char *info)
{
  unsigned char buffer[MAX_LEN_PACKET], *cptr;
#ifdef GENERATING_MAC       /* mac alternate networking */
  struct TUnitData xmit;
  OSStatus err;
  xmit.udata.maxlen = MAX_LEN_PACKET;
  xmit.udata.buf=buffer;
#else  
  if(sockfd<=0)
    return 0;
#endif
  cptr=put_uint16(buffer+2,  PACKET_UDP_PCKT);
  cptr=put_string(cptr, desc);
  cptr=put_string(cptr, info);
  put_uint16(buffer, cptr-buffer);
#ifdef GENERATING_MAC  /* mac networking */
  xmit.udata.len=strlen((const char *)buffer);
  err=OTSndUData(meta_ep, &xmit);
#else
  sendto(sockfd, buffer, cptr-buffer,0, 
	 (struct sockaddr *) &serv_addr, sizeof(serv_addr) );
#endif
  return 1;
}

void server_close_udp(void)
{
  server_is_open=0;

#ifdef GENERATING_MAC  /* mac networking */
  OTUnbind(meta_ep);
#else
  if(sockfd<=0)
    return;
  close(sockfd);
  sockfd=0;
#endif
}

void server_open_udp(void)
{
  char *servername=metaserver_addr;
  int bad;
#ifdef GENERATING_MAC  /* mac networking */
  OSStatus err1;
  OSErr err2;
  InetSvcRef ref=OTOpenInternetServices(kDefaultInternetServicesPath, 0, &err1);
  InetHostInfo hinfo;
#else
  int in_size;
  struct hostent *hp;
  u_int bin;
#endif
  
  /*
   * Fill in the structure "serv_addr" with the address of the
   * server that we want to connect with, checks if server address 
   * is valid, both decimal-dotted and name.
   */
#ifdef GENERATING_MAC  /* mac networking */
  if (err1 == 0)
  {
    err1=OTInetStringToAddress(ref, servername,&hinfo);
    serv_addr.fHost=hinfo.addrs[0];
    bad = ((serv_addr.fHost == 0) || (err1 != 0));
  } else
    bad=true;
#else
  in_size = sizeof(inet_addr(servername));
  bin = inet_addr(servername);
  bad = (((hp = gethostbyaddr((char *) &bin,in_size,AF_INET)) == NULL)
	 && ((hp = gethostbyname(servername)) == NULL));
#endif
  if (bad) {
    freelog(LOG_NORMAL, _("Metaserver: bad address: [%s]."),
	    servername);
    con_puts(C_METAERROR, _("Not reporting to the metaserver in this game."));
    con_flush();
    return;
  }
  /*
   * Open a UDP socket (an Internet datagram socket).
   */
#ifdef GENERATING_MAC  /* mac networking */
  meta_ep=OTOpenEndpoint(OTCreateConfiguration(kUDPName), 0, &meta_info, &err1);
  bad = (err1 != 0);
#else  
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_port        = htons(metaserver_port);
  memcpy(&serv_addr.sin_addr, hp->h_addr, hp->h_length); 
  bad = ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0);
#endif
  if (bad) {
    freelog(LOG_DEBUG, "Metaserver: can't open datagram socket: %s",
	    mystrerror(errno));
    con_puts(C_METAERROR, _("Not reporting to the metaserver in this game."));
    con_flush();
    return;
  }
  /*
   * Bind any local address for us.
   */
  
#ifdef GENERATING_MAC  /* mac networking */
  err1=OTBind(meta_ep, NULL, NULL);
  bad = (err1 != 0);
#else
  memset( &cli_addr, 0, sizeof(cli_addr));	/* zero out */
  cli_addr.sin_family      = AF_INET;
  cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  cli_addr.sin_port        = htons(0);
  bad = (bind(sockfd, (struct sockaddr *) &cli_addr, sizeof(cli_addr)) < 0);
#endif
  if (bad) {
    freelog(LOG_DEBUG, "Metaserver: can't bind local address: %s",
	    mystrerror(errno));
    con_puts(C_METAERROR, _("Not reporting to the metaserver in this game."));
    con_flush();
    return;
  }

  server_is_open=1;
}
