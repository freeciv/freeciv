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

#include <shared.h>

#define	METASERVER_PORT	12245
#define	METASERVER_ADDR	"platinum.daimi.aau.dk"
#define METASERVER_UPDATE_INTERVAL 3*60
#if IS_BETA_VERSION
#  define DEFAULT_META_SERVER_INFO_STRING "unstable pre-1.8: beware"
#else
#  define DEFAULT_META_SERVER_INFO_STRING "(default)"
#endif

/* Returns true if able to send */
int send_to_metaserver(char *desc, char *info);
void server_close_udp(void);
void server_open_udp(void);

#define PACKET_UDP_PCKT 2

#endif /* FC__META_H */
