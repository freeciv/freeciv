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
#ifndef FC__META_H
#define FC__META_H

/*
 * Definitions for UDP.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "version.h"

#define DEFAULT_META_SERVER_NO_SEND	1
#define DEFAULT_META_SERVER_PORT	12245
#define DEFAULT_META_SERVER_ADDR	"meta.freeciv.org"
#define METASERVER_UPDATE_INTERVAL	(3*60)
#if IS_BETA_VERSION
#  define DEFAULT_META_SERVER_INFO_STRING "unstable pre-" NEXT_STABLE_VERSION ": beware"
#else
#  if IS_DEVEL_VERSION
#    define DEFAULT_META_SERVER_INFO_STRING "development version: beware"
#  else
#    define DEFAULT_META_SERVER_INFO_STRING "(default)"
#  endif
#endif

extern int server_is_open;

void meta_addr_split(void);
char *meta_addr_port(void);

/* Returns true if able to send */
int send_to_metaserver(char *desc, char *info);

void server_close_udp(void);
void server_open_udp(void);

#define PACKET_UDP_PCKT 2

#endif /* FC__META_H */
