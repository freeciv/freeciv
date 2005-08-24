/**********************************************************************
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
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

#include <errno.h>
#include <stdlib.h>

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
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
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#include "capstr.h"
#include "dataio.h"
#include "fcintl.h"
#include "game.h"
#include "hash.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "packets.h"
#include "registry.h"
#include "support.h"
#include "version.h"

#include "servers.h"

#include "gui_main_g.h"

static int socklan;
static struct server_list *lan_servers;

/**************************************************************************
 The server sends a stream in a registry 'ini' type format.
 Read it using secfile functions and fill the server_list structs.
**************************************************************************/
static struct server_list *parse_metaserver_data(fz_FILE *f)
{
  struct server_list *server_list;
  struct section_file the_file, *file = &the_file;
  int nservers, i, j;

  server_list = server_list_new();

  /* This call closes f. */
  if (!section_file_load_from_stream(file, f)) {
    return server_list;
  }

  nservers = secfile_lookup_int_default(file, 0, "main.nservers");

  for (i = 0; i < nservers; i++) {
    char *host, *port, *version, *state, *message, *nplayers;
    int n;
    struct server *pserver = (struct server*)fc_malloc(sizeof(struct server));

    host = secfile_lookup_str_default(file, "", "server%d.host", i);
    pserver->host = mystrdup(host);

    port = secfile_lookup_str_default(file, "", "server%d.port", i);
    pserver->port = mystrdup(port);

    version = secfile_lookup_str_default(file, "", "server%d.version", i);
    pserver->version = mystrdup(version);

    state = secfile_lookup_str_default(file, "", "server%d.state", i);
    pserver->state = mystrdup(state);

    message = secfile_lookup_str_default(file, "", "server%d.message", i);
    pserver->message = mystrdup(message);

    nplayers = secfile_lookup_str_default(file, "0", "server%d.nplayers", i);
    pserver->nplayers = mystrdup(nplayers);
    n = atoi(nplayers);

    if (n > 0) {
      pserver->players = fc_malloc(n * sizeof(*pserver->players));
    } else {
      pserver->players = NULL;
    }
      
    for (j = 0; j < n; j++) {
      char *name, *nation, *type, *host;

      name = secfile_lookup_str_default(file, "", 
                                        "server%d.player%d.name", i, j);
      pserver->players[j].name = mystrdup(name);

      type = secfile_lookup_str_default(file, "",
                                        "server%d.player%d.type", i, j);
      pserver->players[j].type = mystrdup(type);

      host = secfile_lookup_str_default(file, "", 
                                        "server%d.player%d.host", i, j);
      pserver->players[j].host = mystrdup(host);

      nation = secfile_lookup_str_default(file, "",
                                          "server%d.player%d.nation", i, j);
      pserver->players[j].nation = mystrdup(nation);
    }

    server_list_append(server_list, pserver);
  }

  section_file_free(file);
  return server_list;
}

#ifdef WIN32_NATIVE
/*****************************************************************
   Returns an uname like string for windows
*****************************************************************/
static char *win_uname()
{
  static char uname_buf[256];
  char cpuname[16];
  char *osname;
  SYSTEM_INFO sysinfo;
  OSVERSIONINFO osvi;

  osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
  GetVersionEx(&osvi);

  switch (osvi.dwPlatformId) {
  case VER_PLATFORM_WIN32s:
    osname = "Win32s";
    break;

  case VER_PLATFORM_WIN32_WINDOWS:
    osname = "Win32";

    if (osvi.dwMajorVersion == 4) {
      switch (osvi.dwMinorVersion) {
      case  0: osname = "Win95";    break;
      case 10: osname = "Win98";    break;
      case 90: osname = "WinME";    break;
      default:			    break;
      }
    }
    break;

  case VER_PLATFORM_WIN32_NT:
    osname = "WinNT";

    if (osvi.dwMajorVersion == 5) {
      switch (osvi.dwMinorVersion) {
      case 0: osname = "Win2000";   break;
      case 1: osname = "WinXP";	    break;
      default:			    break;
      }
    }
    break;

  default:
    osname = osvi.szCSDVersion;
    break;
  }

  GetSystemInfo(&sysinfo); 
  switch (sysinfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_INTEL:
      {
	unsigned int ptype;
	if (sysinfo.wProcessorLevel < 3) /* Shouldn't happen. */
	  ptype = 3;
	else if (sysinfo.wProcessorLevel > 9) /* P4 */
	  ptype = 6;
	else
	  ptype = sysinfo.wProcessorLevel;
	
	my_snprintf(cpuname, sizeof(cpuname), "i%d86", ptype);
      }
      break;

    case PROCESSOR_ARCHITECTURE_MIPS:
      sz_strlcpy(cpuname, "mips");
      break;

    case PROCESSOR_ARCHITECTURE_ALPHA:
      sz_strlcpy(cpuname, "alpha");
      break;

    case PROCESSOR_ARCHITECTURE_PPC:
      sz_strlcpy(cpuname, "ppc");
      break;
#if 0
    case PROCESSOR_ARCHITECTURE_IA64:
      sz_strlcpy(cpuname, "ia64");
      break;
#endif
    default:
      sz_strlcpy(cpuname, "unknown");
      break;
  }
  my_snprintf(uname_buf, sizeof(uname_buf),
	      "%s %ld.%ld [%s]", osname, osvi.dwMajorVersion, osvi.dwMinorVersion,
	      cpuname);
  return uname_buf;
}
#endif

/**************************************************************************
 Create the list of servers from the metaserver
 The result must be free'd with delete_server_list() when no
 longer used
**************************************************************************/
struct server_list *create_server_list(char *errbuf, int n_errbuf)
{
  union my_sockaddr addr;
  int s;
  fz_FILE *f;
  const char *urlpath;
  char metaname[MAX_LEN_ADDR];
  int metaport;
  const char *capstr;
  char str[MAX_LEN_PACKET];
  char machine_string[128];
#ifdef HAVE_UNAME
  struct utsname un;
#endif 

  urlpath = my_lookup_httpd(metaname, &metaport, METALIST_ADDR);//metaserver);
  if (!urlpath) {
    (void) mystrlcpy(errbuf, _("Invalid $http_proxy or metaserver value, must "
                              "start with 'http://'"), n_errbuf);
    return NULL;
  }

  if (!net_lookup_service(metaname, metaport, &addr)) {
    (void) mystrlcpy(errbuf, _("Failed looking up metaserver's host"), n_errbuf);
    return NULL;
  }
  
  if((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    (void) mystrlcpy(errbuf, mystrerror(), n_errbuf);
    return NULL;
  }
  
  if(connect(s, (struct sockaddr *) &addr.sockaddr, sizeof(addr)) == -1) {
    (void) mystrlcpy(errbuf, mystrerror(), n_errbuf);
    my_closesocket(s);
    return NULL;
  }

#ifdef HAVE_UNAME
  uname(&un);
  my_snprintf(machine_string,sizeof(machine_string),
              "%s %s [%s]",
              un.sysname,
              un.release,
              un.machine);
#else /* ! HAVE_UNAME */
  /* Fill in here if you are making a binary without sys/utsname.h and know
     the OS name, release number, and machine architechture */
#ifdef WIN32_NATIVE
  sz_strlcpy(machine_string,win_uname());
#else
  my_snprintf(machine_string,sizeof(machine_string),
              "unknown unknown [unknown]");
#endif
#endif /* HAVE_UNAME */

  capstr = my_url_encode(our_capability);

  my_snprintf(str, sizeof(str),
    "POST %s HTTP/1.1\r\n"
    "Host: %s:%d\r\n"
    "User-Agent: Freeciv/%s %s %s\r\n"
    "Connection: close\r\n"
    "Content-Type: application/x-www-form-urlencoded; charset=\"utf-8\"\r\n"
    "Content-Length: %lu\r\n"
    "\r\n"
    "client_cap=%s\r\n",
    urlpath,
    metaname, metaport,
    VERSION_STRING, client_string, machine_string,
    (unsigned long) (strlen("client_cap=") + strlen(capstr)),
    capstr);

  f = my_querysocket(s, str, strlen(str));

  if (f == NULL) {
    /* TRANS: This means a network error when trying to connect to
     * the metaserver.  The message will be shown directly to the user. */
    (void) mystrlcpy(errbuf, _("Failed querying socket"), n_errbuf);
    return NULL;
  }

  /* skip HTTP headers */
  while (fz_fgets(str, sizeof(str), f) && strcmp(str, "\r\n") != 0) {
    /* nothing */
  }

  /* XXX: TODO check for magic Content-Type: text/x-ini -vasc */

  /* parse HTTP message body */
  return parse_metaserver_data(f);
}

/**************************************************************************
 Frees everything associated with a server list including
 the server list itself (so the server_list is no longer
 valid after calling this function)
**************************************************************************/
void delete_server_list(struct server_list *server_list)
{
  server_list_iterate(server_list, ptmp) {
    int i;
    int n = atoi(ptmp->nplayers);

    free(ptmp->host);
    free(ptmp->port);
    free(ptmp->version);
    free(ptmp->state);
    free(ptmp->message);

    if (ptmp->players) {
      for (i = 0; i < n; i++) {
        free(ptmp->players[i].name);
        free(ptmp->players[i].type);
        free(ptmp->players[i].host);
        free(ptmp->players[i].nation);
      }
      free(ptmp->players);
    }
    free(ptmp->nplayers);

    free(ptmp);
  } server_list_iterate_end;

  server_list_unlink_all(server_list);
  server_list_free(server_list);
}

/**************************************************************************
  Broadcast an UDP package to all servers on LAN, requesting information 
  about the server. The packet is send to all Freeciv servers in the same
  multicast group as the client.
**************************************************************************/
int begin_lanserver_scan(void)
{
  union my_sockaddr addr;
  struct data_out dout;
  int sock, opt = 1;
  unsigned char buffer[MAX_LEN_PACKET];
  struct ip_mreq mreq;
  const char *group;
  unsigned char ttl;
  size_t size;

  /* Create a socket for broadcasting to servers. */
  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    freelog(LOG_ERROR, "socket failed: %s", mystrerror());
    return 0;
  }

  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                 (char *)&opt, sizeof(opt)) == -1) {
    freelog(LOG_ERROR, "SO_REUSEADDR failed: %s", mystrerror());
  }

  /* Set the UDP Multicast group IP address. */
  group = get_multicast_group();
  memset(&addr, 0, sizeof(addr));
  addr.sockaddr_in.sin_family = AF_INET;
  addr.sockaddr_in.sin_addr.s_addr = inet_addr(get_multicast_group());
  addr.sockaddr_in.sin_port = htons(SERVER_LAN_PORT);

  /* Set the Time-to-Live field for the packet  */
  ttl = SERVER_LAN_TTL;
  if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, 
                 sizeof(ttl))) {
    freelog(LOG_ERROR, "setsockopt failed: %s", mystrerror());
    return 0;
  }

  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, 
                 sizeof(opt))) {
    freelog(LOG_ERROR, "setsockopt failed: %s", mystrerror());
    return 0;
  }

  dio_output_init(&dout, buffer, sizeof(buffer));
  dio_put_uint8(&dout, SERVER_LAN_VERSION);
  size = dio_output_used(&dout);
 

  if (sendto(sock, buffer, size, 0, &addr.sockaddr,
      sizeof(addr)) < 0) {
    freelog(LOG_ERROR, "sendto failed: %s", mystrerror());
    return 0;
  } else {
    freelog(LOG_DEBUG, ("Sending request for server announcement on LAN."));
  }

  my_closesocket(sock);

  /* Create a socket for listening for server packets. */
  if ((socklan = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    freelog(LOG_ERROR, "socket failed: %s", mystrerror());
    return 0;
  }

  my_nonblock(socklan);

  if (setsockopt(socklan, SOL_SOCKET, SO_REUSEADDR,
                 (char *)&opt, sizeof(opt)) == -1) {
    freelog(LOG_ERROR, "SO_REUSEADDR failed: %s", mystrerror());
  }
                                                                               
  memset(&addr, 0, sizeof(addr));
  addr.sockaddr_in.sin_family = AF_INET;
  addr.sockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY); 
  addr.sockaddr_in.sin_port = htons(SERVER_LAN_PORT + 1);

  if (bind(socklan, &addr.sockaddr, sizeof(addr)) < 0) {
    freelog(LOG_ERROR, "bind failed: %s", mystrerror());
    return 0;
  }

  mreq.imr_multiaddr.s_addr = inet_addr(group);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(socklan, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                 (const char*)&mreq, sizeof(mreq)) < 0) {
    freelog(LOG_ERROR, "setsockopt failed: %s", mystrerror());
    return 0;
  }

  lan_servers = server_list_new();

  return 1;
}

/**************************************************************************
  Listens for UDP packets broadcasted from a server that responded
  to the request-packet sent from the client. 
**************************************************************************/
struct server_list *get_lan_server_list(void) {

# if defined(__VMS) && !defined(_DECC_V4_SOURCE)
  size_t fromlen;
# else
  unsigned int fromlen;
# endif
  union my_sockaddr fromend;
  struct hostent *from;
  char msgbuf[128];
  int type;
  struct data_in din;
  char servername[512];
  char port[256];
  char version[256];
  char status[256];
  char players[256];
  char message[1024];
  fd_set readfs, exceptfs;
  struct timeval tv;

  FD_ZERO(&readfs);
  FD_ZERO(&exceptfs);
  FD_SET(socklan, &exceptfs);
  FD_SET(socklan, &readfs);
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  while (select(socklan + 1, &readfs, NULL, &exceptfs, &tv) == -1) {
    if (errno != EINTR) {
      freelog(LOG_ERROR, "select failed: %s", mystrerror());
      return lan_servers;
    }
    /* EINTR can happen sometimes, especially when compiling with -pg.
     * Generally we just want to run select again. */
  }

  if (!FD_ISSET(socklan, &readfs)) {
    return lan_servers;
  }

  dio_input_init(&din, msgbuf, sizeof(msgbuf));
  fromlen = sizeof(fromend);

  /* Try to receive a packet from a server. */ 
  if (0 < recvfrom(socklan, msgbuf, sizeof(msgbuf), 0,
                   &fromend.sockaddr, &fromlen)) {
    struct server *pserver;

    dio_get_uint8(&din, &type);
    if (type != SERVER_LAN_VERSION) {
      return lan_servers;
    }
    dio_get_string(&din, servername, sizeof(servername));
    dio_get_string(&din, port, sizeof(port));
    dio_get_string(&din, version, sizeof(version));
    dio_get_string(&din, status, sizeof(status));
    dio_get_string(&din, players, sizeof(players));
    dio_get_string(&din, message, sizeof(message));

    if (!mystrcasecmp("none", servername)) {
      from = gethostbyaddr((char *) &fromend.sockaddr_in.sin_addr,
			   sizeof(fromend.sockaddr_in.sin_addr), AF_INET);
      sz_strlcpy(servername, inet_ntoa(fromend.sockaddr_in.sin_addr));
    }

    /* UDP can send duplicate or delayed packets. */
    server_list_iterate(lan_servers, aserver) {
      if (!mystrcasecmp(aserver->host, servername) 
          && !mystrcasecmp(aserver->port, port)) {
        return lan_servers;
      } 
    } server_list_iterate_end;

    freelog(LOG_DEBUG,
            ("Received a valid announcement from a server on the LAN."));
    
    pserver =  (struct server*)fc_malloc(sizeof(struct server));
    pserver->host = mystrdup(servername);
    pserver->port = mystrdup(port);
    pserver->version = mystrdup(version);
    pserver->state = mystrdup(status);
    pserver->nplayers = mystrdup(players);
    pserver->message = mystrdup(message);
    pserver->players = NULL;

    server_list_prepend(lan_servers, pserver);
  } else {
    return lan_servers;
  }                                       

  return lan_servers;
}

/**************************************************************************
  Closes the socket listening on the lan and frees the list of LAN servers.
**************************************************************************/
void finish_lanserver_scan(void) 
{
  my_closesocket(socklan);
  delete_server_list(lan_servers);
}
