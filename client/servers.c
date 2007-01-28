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

#include "fcintl.h"
#include "hash.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "rand.h" /* myrand() */
#include "registry.h"
#include "support.h"

#include "capstr.h"
#include "dataio.h"
#include "game.h"
#include "packets.h"
#include "version.h"

#include "civclient.h"
#include "servers.h"

#include "gui_main_g.h"

struct server_scan {
  enum server_scan_type type;
  ServerScanErrorFunc error_func;

  struct server_list *servers;
  int sock;

  /* Only used for metaserver */
  struct {
    enum {
      META_CONNECTING,
      META_WAITING,
      META_DONE
    } state;
    char name[MAX_LEN_ADDR];
    int port;
    const char *urlpath;
    FILE *fp; /* temp file */
  } meta;
};

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

/*****************************************************************
  Returns an uname like string.
*****************************************************************/
static void my_uname(char *buf, size_t len)
{
#ifdef HAVE_UNAME
  {
    struct utsname un;

    uname(&un);
    my_snprintf(buf, len,
		"%s %s [%s]",
		un.sysname,
		un.release,
		un.machine);
  }
#else /* ! HAVE_UNAME */
  /* Fill in here if you are making a binary without sys/utsname.h and know
     the OS name, release number, and machine architechture */
#ifdef WIN32_NATIVE
  {
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
    my_snprintf(buf, len,
		"%s %ld.%ld [%s]",
		osname, osvi.dwMajorVersion, osvi.dwMinorVersion,
		cpuname);
  }
#else
  my_snprintf(buf, len,
              "unknown unknown [unknown]");
#endif
#endif /* HAVE_UNAME */
}

/****************************************************************************
  Send the request string to the metaserver.
****************************************************************************/
static void meta_send_request(struct server_scan *scan)
{
  const char *capstr;
  char str[MAX_LEN_PACKET];
  char machine_string[128];

  my_uname(machine_string, sizeof(machine_string));

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
    scan->meta.urlpath,
    scan->meta.name, scan->meta.port,
    VERSION_STRING, client_string, machine_string,
    (unsigned long) (strlen("client_cap=") + strlen(capstr)),
    capstr);

  if (my_writesocket(scan->sock, str, strlen(str)) != strlen(str)) {
    /* Even with non-blocking this shouldn't fail. */
    (scan->error_func)(scan, mystrerror());
    return;
  }

  scan->meta.state = META_WAITING;
}

/****************************************************************************
  Read the request string (or part of it) from the metaserver.
****************************************************************************/
static void meta_read_response(struct server_scan *scan)
{
  char buf[4096];
  int result;

  if (!scan->meta.fp) {
#ifdef WIN32_NATIVE
    char filename[MAX_PATH];

    GetTempPath(sizeof(filename), filename);
    cat_snprintf(filename, sizeof(filename), "fctmp%d", myrand(1000));

    scan->meta.fp = fopen(filename, "w+b");
#else
    scan->meta.fp = tmpfile();
#endif

    if (!scan->meta.fp) {
      (scan->error_func)(scan, _("Could not open temp file."));
    }
  }

  while (1) {
    result = my_readsocket(scan->sock, buf, sizeof(buf));

    if (result < 0) {
      if (errno == EAGAIN || errno == EINTR) {
	/* Keep waiting. */
	return;
      }
      (scan->error_func)(scan, mystrerror());
      return;
    } else if (result == 0) {
      fz_FILE *f;
      char str[4096];

      /* We're done! */
      rewind(scan->meta.fp);

      f = fz_from_stream(scan->meta.fp);
      assert(f != NULL);


      /* skip HTTP headers */
      /* XXX: TODO check for magic Content-Type: text/x-ini -vasc */
      while (fz_fgets(str, sizeof(str), f) && strcmp(str, "\r\n") != 0) {
	/* nothing */
      }

      /* XXX: TODO check for magic Content-Type: text/x-ini -vasc */

      /* parse HTTP message body */
      scan->servers = parse_metaserver_data(f);
      scan->meta.state = META_DONE;
      return;
    } else {
      if (fwrite(buf, 1, result, scan->meta.fp) != result) {
	(scan->error_func)(scan, mystrerror());
      }
    }
  }
}

/****************************************************************************
  Begin a metaserver scan for servers.  This just initiates the connection
  to the metaserver; later get_meta_server_list should be called whenever
  the socket has data pending to read and parse it.

  Returns FALSE on error (in which case errbuf will contain an error
  message).
****************************************************************************/
static bool begin_metaserver_scan(struct server_scan *scan)
{
  union my_sockaddr addr;
  int s;

  scan->meta.urlpath = my_lookup_httpd(scan->meta.name, &scan->meta.port,
				       metaserver);
  if (!scan->meta.urlpath) {
    (scan->error_func)(scan,
		       _("Invalid $http_proxy or metaserver value, must "
			 "start with 'http://'"));
    return FALSE;
  }

  if (!net_lookup_service(scan->meta.name, scan->meta.port, &addr)) {
    (scan->error_func)(scan, _("Failed looking up metaserver's host"));
    return FALSE;
  }
  
  if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    (scan->error_func)(scan, mystrerror());
    return FALSE;
  }

  my_nonblock(s);
  
  if (connect(s, (struct sockaddr *) &addr.sockaddr, sizeof(addr)) == -1) {
    if (
#ifdef HAVE_WINSOCK
	errno == WSAEINPROGRESS
#else
	errno == EINPROGRESS
#endif
	) {
      /* With non-blocking sockets this is the expected result. */
      scan->meta.state = META_CONNECTING;
      scan->sock = s;
    } else {
      my_closesocket(s);
      (scan->error_func)(scan, mystrerror());
      return FALSE;
    }
  } else {
    /* Instant connection?  Whoa. */
    scan->sock = s;
    scan->meta.state = META_CONNECTING;
    meta_send_request(scan);
  }

  return TRUE;
}

/**************************************************************************
 Create the list of servers from the metaserver
 The result must be free'd with delete_server_list() when no
 longer used
**************************************************************************/
static struct server_list *get_metaserver_list(struct server_scan *scan)
{
  struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
  fd_set sockset;

  if (scan->sock < 0) {
    return NULL;
  }

  FD_ZERO(&sockset);
  FD_SET(scan->sock, &sockset);

  switch (scan->meta.state) {
  case META_CONNECTING:
    if (select(scan->sock + 1, NULL, &sockset, NULL, &tv) < 0) {
      (scan->error_func)(scan, mystrerror());
    } else if (FD_ISSET(scan->sock, &sockset)) {
      meta_send_request(scan);
    } else {
      /* Keep waiting. */
    }
    return NULL;
  case META_WAITING:
    if (select(scan->sock + 1, &sockset, NULL, NULL, &tv) < 0) {
      (scan->error_func)(scan, mystrerror());
    } else if (FD_ISSET(scan->sock, &sockset)) {
      meta_read_response(scan);
      return scan->servers;
    } else {
      /* Keep waiting. */
    }
    return NULL;
  case META_DONE:
    /* We already have a server list but we don't return it since it hasn't
     * changed. */
    return NULL;
  }

  assert(0);
  return NULL;
}

/**************************************************************************
 Frees everything associated with a server list including
 the server list itself (so the server_list is no longer
 valid after calling this function)
**************************************************************************/
static void delete_server_list(struct server_list *server_list)
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
static bool begin_lanserver_scan(struct server_scan *scan)
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
    return FALSE;
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
    return FALSE;
  }

  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, 
                 sizeof(opt))) {
    freelog(LOG_ERROR, "setsockopt failed: %s", mystrerror());
    return FALSE;
  }

  dio_output_init(&dout, buffer, sizeof(buffer));
  dio_put_uint8(&dout, SERVER_LAN_VERSION);
  size = dio_output_used(&dout);
 

  if (sendto(sock, buffer, size, 0, &addr.sockaddr,
      sizeof(addr)) < 0) {
    /* This can happen when there's no network connection - it should
     * give an in-game message. */
    freelog(LOG_ERROR, "sendto failed: %s", mystrerror());
    return FALSE;
  } else {
    freelog(LOG_DEBUG, ("Sending request for server announcement on LAN."));
  }

  my_closesocket(sock);

  /* Create a socket for listening for server packets. */
  if ((scan->sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    (scan->error_func)(scan, mystrerror());
    return FALSE;
  }

  my_nonblock(scan->sock);

  if (setsockopt(scan->sock, SOL_SOCKET, SO_REUSEADDR,
                 (char *)&opt, sizeof(opt)) == -1) {
    freelog(LOG_ERROR, "SO_REUSEADDR failed: %s", mystrerror());
  }
                                                                               
  memset(&addr, 0, sizeof(addr));
  addr.sockaddr_in.sin_family = AF_INET;
  addr.sockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY); 
  addr.sockaddr_in.sin_port = htons(SERVER_LAN_PORT + 1);

  if (bind(scan->sock, &addr.sockaddr, sizeof(addr)) < 0) {
    (scan->error_func)(scan, mystrerror());
    return FALSE;
  }

  mreq.imr_multiaddr.s_addr = inet_addr(group);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(scan->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                 (const char*)&mreq, sizeof(mreq)) < 0) {
    (scan->error_func)(scan, mystrerror());
    return FALSE;
  }

  scan->servers = server_list_new();

  return TRUE;
}

/**************************************************************************
  Listens for UDP packets broadcasted from a server that responded
  to the request-packet sent from the client. 
**************************************************************************/
static struct server_list *get_lan_server_list(struct server_scan *scan)
{
#ifdef HAVE_SOCKLEN_T
  socklen_t fromlen;
# else
  int fromlen;
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
  bool found_new = FALSE;

  while (1) {
    struct server *pserver;
    bool duplicate = FALSE;

    dio_input_init(&din, msgbuf, sizeof(msgbuf));
    fromlen = sizeof(fromend);

    /* Try to receive a packet from a server.  No select loop is needed;
     * we just keep on reading until recvfrom returns -1. */
    if (recvfrom(scan->sock, msgbuf, sizeof(msgbuf), 0,
		 &fromend.sockaddr, &fromlen) < 0) {
      break;
    }

    dio_get_uint8(&din, &type);
    if (type != SERVER_LAN_VERSION) {
      continue;
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
    server_list_iterate(scan->servers, aserver) {
      if (!mystrcasecmp(aserver->host, servername) 
          && !mystrcasecmp(aserver->port, port)) {
	duplicate = TRUE;
	break;
      } 
    } server_list_iterate_end;
    if (duplicate) {
      continue;
    }

    freelog(LOG_DEBUG,
            ("Received a valid announcement from a server on the LAN."));
    
    pserver = fc_malloc(sizeof(*pserver));
    pserver->host = mystrdup(servername);
    pserver->port = mystrdup(port);
    pserver->version = mystrdup(version);
    pserver->state = mystrdup(status);
    pserver->nplayers = mystrdup(players);
    pserver->message = mystrdup(message);
    pserver->players = NULL;
    found_new = TRUE;

    server_list_prepend(scan->servers, pserver);
  }

  return found_new ? scan->servers : NULL;
}

/****************************************************************************
  Creates a new server scan, and starts scanning.

  Depending on 'type' the scan will look for either local or internet
  games.

  error_func provides a callback to be used in case of error; this
  callback probably should call server_scan_finish.
****************************************************************************/
struct server_scan *server_scan_begin(enum server_scan_type type,
				      ServerScanErrorFunc error_func)
{
  struct server_scan *scan = fc_calloc(sizeof(*scan), 1);

  scan->type = type;
  scan->error_func = error_func;

  scan->sock = -1;

  switch (type) {
  case SERVER_SCAN_GLOBAL:
    if (begin_metaserver_scan(scan)) {
      return scan;
    } else {
      return NULL;
    }
  case SERVER_SCAN_LOCAL:
    if (begin_lanserver_scan(scan)) {
      return scan;
    } else {
      return NULL;
    }
  case SERVER_SCAN_LAST:
    break;
  }

  assert(0);
  return NULL;
}

/****************************************************************************
  A simple query function to determine the type of a server scan (previously
  allocated in server_scan_begin).
****************************************************************************/
enum server_scan_type server_scan_get_type(const struct server_scan *scan)
{
  return scan->type;
}

/****************************************************************************
  A function to query servers of the server scan.  This will check any
  pending network data and update the server list.  It then returns
  a server_list if any new servers are found, or NULL if the list has not
  changed.

  Note that unless the list has changed NULL will be returned.  Since
  polling is likely to be used with the server scans, callers should poll
  this function often (every 100 ms) but only need to take further action
  when a non-NULL value is returned.
****************************************************************************/
struct server_list *server_scan_get_servers(struct server_scan *scan)
{
  switch (scan->type) {
  case SERVER_SCAN_GLOBAL:
    return get_metaserver_list(scan);
  case SERVER_SCAN_LOCAL:
   return get_lan_server_list(scan);
  case SERVER_SCAN_LAST:
    break;
  }

  assert(0);
  return NULL;
}

/**************************************************************************
  Closes the socket listening on the scan and frees the list of servers.
**************************************************************************/
void server_scan_finish(struct server_scan *scan)
{
  if (!scan) {
    return;
  }
  if (scan->sock >= 0) {
    my_closesocket(scan->sock);
    scan->sock = -1;
  }

  if (scan->servers) {
    delete_server_list(scan->servers);
    scan->servers = NULL;
  }

  /* FIXME: do we need to close scan->meta.fp or free scan->meta.urlpath? */
}
