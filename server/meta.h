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


#ifndef _META_H
#define _META_H

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
void server_close_udp();
void server_open_udp();

#define PACKET_UDP_PCKT 2
#endif
