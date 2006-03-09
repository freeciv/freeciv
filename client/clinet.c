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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#include "agents.h"
#include "attribute.h"
#include "chatline_g.h"
#include "civclient.h"
#include "climisc.h"
#include "connectdlg_common.h"
#include "connectdlg_g.h"
#include "dialogs_g.h"		/* popdown_races_dialog() */
#include "gui_main_g.h"		/* add_net_input(), remove_net_input() */
#include "mapview_common.h"	/* unqueue_mapview_update */
#include "menu_g.h"
#include "messagewin_g.h"
#include "options.h"
#include "packhand.h"
#include "pages_g.h"
#include "plrdlg_g.h"
#include "repodlgs_g.h"

#include "clinet.h"

struct connection aconnection;
static int socklan;
static struct server_list *lan_servers;
static union my_sockaddr server_addr;

/*************************************************************************
  Close socket and cleanup.  This one doesn't print a message, so should
  do so before-hand if necessary.
**************************************************************************/
static void close_socket_nomessage(struct connection *pc)
{
  connection_common_close(pc);
  remove_net_input();
  popdown_races_dialog(); 
  close_connection_dialog();
  set_client_page(PAGE_MAIN);

  reports_force_thaw();
  
  set_client_state(CLIENT_PRE_GAME_STATE);
  agents_disconnect();
  update_menus();
  client_remove_all_cli_conn();
}

/**************************************************************************
...
**************************************************************************/
static void close_socket_callback(struct connection *pc)
{
  close_socket_nomessage(pc);
  /* If we lost connection to the internal server - kill him */
  client_kill_server(TRUE);
  append_output_window(_("Lost connection to server!"));
  freelog(LOG_NORMAL, "lost connection to server");
}

/**************************************************************************
  Connect to a civserver instance -- or at least try to.  On success,
  return 0; on failure, put an error message in ERRBUF and return -1.
**************************************************************************/
int connect_to_server(const char *username, const char *hostname, int port,
		      char *errbuf, int errbufsize)
{
  if (get_server_address(hostname, port, errbuf, errbufsize) != 0) {
    return -1;
  }

  if (try_to_connect(username, errbuf, errbufsize) != 0) {
    return -1;
  }
  return 0;
}

/**************************************************************************
  Get ready to [try to] connect to a server:
   - translate HOSTNAME and PORT (with defaults of "localhost" and
     DEFAULT_SOCK_PORT respectively) to a raw IP address and port number, and
     store them in the `server_addr' variable
   - return 0 on success
     or put an error message in ERRBUF and return -1 on failure
**************************************************************************/
int get_server_address(const char *hostname, int port, char *errbuf,
		       int errbufsize)
{
  if (port == 0)
    port = DEFAULT_SOCK_PORT;

  /* use name to find TCP/IP address of server */
  if (!hostname)
    hostname = "localhost";

  if (!net_lookup_service(hostname, port, &server_addr)) {
    (void) mystrlcpy(errbuf, _("Failed looking up host."), errbufsize);
    return -1;
  }

  return 0;
}

/**************************************************************************
  Try to connect to a server (get_server_address() must be called first!):
   - try to create a TCP socket and connect it to `server_addr'
   - if successful:
	  - start monitoring the socket for packets from the server
	  - send a "login request" packet to the server
      and - return 0
   - if unable to create the connection, close the socket, put an error
     message in ERRBUF and return the Unix error code (ie., errno, which
     will be non-zero).
**************************************************************************/
int try_to_connect(const char *username, char *errbuf, int errbufsize)
{
  struct packet_server_join_req req;

  close_socket_set_callback(close_socket_callback);

  /* connection in progress? wait. */
  if (aconnection.used) {
    (void) mystrlcpy(errbuf, _("Connection in progress."), errbufsize);
    return -1;
  }
  
  if ((aconnection.sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    (void) mystrlcpy(errbuf, mystrerror(), errbufsize);
    return -1;
  }

  if (connect(aconnection.sock, &server_addr.sockaddr,
      sizeof(server_addr)) == -1) {
    (void) mystrlcpy(errbuf, mystrerror(), errbufsize);
    my_closesocket(aconnection.sock);
    aconnection.sock = -1;
#ifdef WIN32_NATIVE
    return -1;
#else
    return errno;
#endif
  }

  connection_common_init(&aconnection);
  aconnection.client.last_request_id_used = 0;
  aconnection.client.last_processed_request_id_seen = 0;
  aconnection.client.request_id_of_currently_handled_packet = 0;
  aconnection.incoming_packet_notify = notify_about_incoming_packet;
  aconnection.outgoing_packet_notify = notify_about_outgoing_packet;

  /* call gui-dependent stuff in gui_main.c */
  add_net_input(aconnection.sock);

  /* now send join_request package */

  req.major_version = MAJOR_VERSION;
  req.minor_version = MINOR_VERSION;
  req.patch_version = PATCH_VERSION;
  sz_strlcpy(req.version_label, VERSION_LABEL);
  sz_strlcpy(req.capability, our_capability);
  sz_strlcpy(req.username, username);
  
  send_packet_server_join_req(&aconnection, &req);

  return 0;
}

/**************************************************************************
...
**************************************************************************/
void disconnect_from_server(void)
{
  const bool force = !aconnection.used;

  attribute_flush();
  /* If it's internal server - kill him 
   * We assume that we are always connected to the internal server  */
  if (!force) {
    client_kill_server(FALSE);
  }
  close_socket_nomessage(&aconnection);
  if (force) {
    client_kill_server(TRUE);
  }
  append_output_window(_("Disconnected from server."));
}  

/**************************************************************************
A wrapper around read_socket_data() which also handles the case the
socket becomes writeable and there is still data which should be sent
to the server.

Returns:
    -1  :  an error occurred - you should close the socket
    >0  :  number of bytes read
    =0  :  no data read, would block
**************************************************************************/
static int read_from_connection(struct connection *pc, bool block)
{
  for (;;) {
    fd_set readfs, writefs, exceptfs;
    int socket_fd = pc->sock;
    bool have_data_for_server = (pc->used && pc->send_buffer
				&& pc->send_buffer->ndata > 0);
    int n;
    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    MY_FD_ZERO(&readfs);
    FD_SET(socket_fd, &readfs);

    MY_FD_ZERO(&exceptfs);
    FD_SET(socket_fd, &exceptfs);

    if (have_data_for_server) {
      MY_FD_ZERO(&writefs);
      FD_SET(socket_fd, &writefs);
      n =
	  select(socket_fd + 1, &readfs, &writefs, &exceptfs,
		 block ? NULL : &tv);
    } else {
      n =
	  select(socket_fd + 1, &readfs, NULL, &exceptfs,
		 block ? NULL : &tv);
    }

    /* the socket is neither readable, writeable nor got an
       exception */
    if (n == 0) {
      return 0;
    }

    if (n == -1) {
      if (errno == EINTR) {
	/* EINTR can happen sometimes, especially when compiling with -pg.
	 * Generally we just want to run select again. */
	freelog(LOG_DEBUG, "select() returned EINTR");
	continue;
      }

      freelog(LOG_NORMAL, "error in select() return=%d errno=%d (%s)",
	      n, errno, mystrerror());
      return -1;
    }

    if (FD_ISSET(socket_fd, &exceptfs)) {
      return -1;
    }

    if (have_data_for_server && FD_ISSET(socket_fd, &writefs)) {
      flush_connection_send_buffer_all(pc);
    }

    if (FD_ISSET(socket_fd, &readfs)) {
      return read_socket_data(socket_fd, pc->buffer);
    }
  }
}

/**************************************************************************
 This function is called when the client received a new input from the
 server.
**************************************************************************/
void input_from_server(int fd)
{
  assert(fd == aconnection.sock);

  if (read_from_connection(&aconnection, FALSE) >= 0) {
    enum packet_type type;

    while (TRUE) {
      bool result;
      void *packet = get_packet_from_connection(&aconnection,
						&type, &result);

      if (result) {
	assert(packet != NULL);
	handle_packet_input(packet, type);
	free(packet);
      } else {
	assert(packet == NULL);
	break;
      }
    }
  } else {
    close_socket_callback(&aconnection);
  }

  unqueue_mapview_updates();
}

/**************************************************************************
 This function will sniff at the given fd, get the packet and call
 handle_packet_input. It will return if there is a network error or if
 the PACKET_PROCESSING_FINISHED packet for the given request is
 received.
**************************************************************************/
void input_from_server_till_request_got_processed(int fd, 
						  int expected_request_id)
{
  assert(expected_request_id);
  assert(fd == aconnection.sock);

  freelog(LOG_DEBUG,
	  "input_from_server_till_request_got_processed("
	  "expected_request_id=%d)", expected_request_id);

  while (TRUE) {
    if (read_from_connection(&aconnection, TRUE) >= 0) {
      enum packet_type type;

      while (TRUE) {
	bool result;
	void *packet = get_packet_from_connection(&aconnection,
						  &type, &result);
	if (!result) {
	  assert(packet == NULL);
	  break;
	}

	assert(packet != NULL);
	handle_packet_input(packet, type);
	free(packet);

	if (type == PACKET_PROCESSING_FINISHED) {
	  freelog(LOG_DEBUG, "ifstrgp: expect=%d, seen=%d",
		  expected_request_id,
		  aconnection.client.last_processed_request_id_seen);
	  if (aconnection.client.last_processed_request_id_seen >=
	      expected_request_id) {
	    freelog(LOG_DEBUG, "ifstrgp: got it; returning");
	    goto out;
	  }
	}
      }
    } else {
      close_socket_callback(&aconnection);
      break;
    }
  }

out:
  unqueue_mapview_updates();
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
 The server sends a stream in a registry 'ini' type format.
 Read it using secfile functions and fill the server_list structs.
**************************************************************************/
static struct server_list *parse_metaserver_data(fz_FILE *f)
{
  struct server_list *server_list;
  struct section_file the_file, *file = &the_file;
  int nservers, i, j;

  server_list = fc_malloc(sizeof(struct server_list));
  server_list_init(server_list);

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

    server_list_insert_back(server_list, pserver);
  }

  section_file_free(file);
  return server_list;
}

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

  urlpath = my_lookup_httpd(metaname, &metaport, metaserver);
  if (!urlpath) {
    (void) mystrlcpy(errbuf, _("Invalid $http_proxy or metaserver value, must "
                              "start with 'http://'"), n_errbuf);
    return NULL;
  }

  if (!net_lookup_service(metaname, metaport, &addr)) {
    (void) mystrlcpy(errbuf, _("Failed looking up host"), n_errbuf);
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
  server_list_iterate(*server_list, ptmp) {
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
  free(server_list);
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

  lan_servers = fc_malloc(sizeof(struct server_list));
  server_list_init(lan_servers);

  return 1;
}

/**************************************************************************
  Listens for UDP packets broadcasted from a server that responded
  to the request-packet sent from the client. 
**************************************************************************/
struct server_list *get_lan_server_list(void)
{
#ifdef HAVE_SOCKLEN_T
  socklen_t fromlen;
#else
  int fromlen;
#endif
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

 again:

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
      goto again;
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
    server_list_iterate(*lan_servers, aserver) {
      if (!mystrcasecmp(aserver->host, servername) 
          && !mystrcasecmp(aserver->port, port)) {
	goto again;
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

    server_list_insert(lan_servers, pserver);

    goto again;
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
