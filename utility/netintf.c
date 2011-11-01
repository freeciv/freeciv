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

/********************************************************************** 
  Common network interface.
**********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif 
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif
#ifdef WIN32_NATIVE
#include <windows.h>	/* GetTempPath */
#endif

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

#include "netintf.h"

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

#ifdef HAVE_GETADDRINFO
#ifdef AI_NUMERICSERV
#define FC_AI_NUMERICSERV AI_NUMERICSERV
#else  /* AI_NUMERICSERV */
#define FC_AI_NUMERICSERV 0
#endif /* AI_NUMERICSERV */
#endif /* HAVE_GETADDRINFO */

#ifdef HAVE_WINSOCK
/***************************************************************
  Set errno variable on Winsock error
***************************************************************/
static void set_socket_errno(void)
{
  int err = WSAGetLastError();

  switch(err) {
    /* these have mappings to symbolic errno names in netintf.h */ 
    case WSAEINTR:
    case WSAEWOULDBLOCK:
    case WSAECONNRESET:
    case WSAECONNREFUSED:
    case WSAETIMEDOUT:
      errno = err;
      return;
    default:
      log_error("Missing errno mapping for Winsock error #%d.", err);
      /* TRANS: No full stop after the URL, could cause confusion. */
      log_error(_("Please report this message at %s"), BUG_URL);

      errno = 0;
  }
}
#endif /* HAVE_WINSOCK */

/***************************************************************
  Connect a socket to an address
***************************************************************/
int fc_connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen)
{
  int result;
  
  result = connect(sockfd, serv_addr, addrlen);
  
#ifdef HAVE_WINSOCK
  if (result == -1) {
    set_socket_errno();
  }
#endif /* HAVE_WINSOCK */

  return result;
}

/***************************************************************
  Wait for a number of sockets to change status
**************************************************************/
int fc_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
              struct timeval *timeout)
{
  int result;
  
  result = select(n, readfds, writefds, exceptfds, timeout);
  
#ifdef HAVE_WINSOCK
  if (result == -1) {
    set_socket_errno();
  }
#endif /* HAVE_WINSOCK */

  return result;       
}

/***************************************************************
  Read from a socket.
***************************************************************/
int fc_readsocket(int sock, void *buf, size_t size)
{
  int result;
  
#ifdef HAVE_WINSOCK
  result = recv(sock, buf, size, 0);
  if (result == -1) {
    set_socket_errno();
  }
#else  /* HAVE_WINSOCK */
  result = read(sock, buf, size);
#endif /* HAVE_WINSOCK */

  return result;
}

/***************************************************************
  Write to a socket.
***************************************************************/
int fc_writesocket(int sock, const void *buf, size_t size)
{
  int result;
        
#ifdef HAVE_WINSOCK
  result = send(sock, buf, size, 0);
  if (result == -1) {
    set_socket_errno();
  }
#else  /* HAVE_WINSOCK */
#  ifdef MSG_NOSIGNAL
  result = send(sock, buf, size, MSG_NOSIGNAL);
#  else  /* MSG_NOSIGNAL */
  result = write(sock, buf, size);
#  endif /* MSG_NOSIGNAL */
#endif /* HAVE_WINSOCK */

  return result;
}

/***************************************************************
  Close a socket.
***************************************************************/
void fc_closesocket(int sock)
{
#ifdef HAVE_WINSOCK
  closesocket(sock);
#else
  close(sock);
#endif
}

/***************************************************************
  Initialize network stuff.
***************************************************************/
void fc_init_network(void)
{
#ifdef HAVE_WINSOCK
  WSADATA wsa;

  if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
    log_error("no usable WINSOCK.DLL: %s", fc_strerror(fc_get_errno()));
  }
#endif /* HAVE_WINSOCK */

  /* broken pipes are ignored. */
#ifdef HAVE_SIGPIPE
  (void) signal(SIGPIPE, SIG_IGN);
#endif
}

/***************************************************************
  Shutdown network stuff.
***************************************************************/
void fc_shutdown_network(void)
{
#ifdef HAVE_WINSOCK
  WSACleanup();
#endif
}

/***************************************************************
  Set socket to non-blocking.
***************************************************************/
void fc_nonblock(int sockfd)
{
#ifdef NONBLOCKING_SOCKETS
#ifdef HAVE_WINSOCK
  unsigned long b = 1;
  ioctlsocket(sockfd, FIONBIO, &b);
#else  /* HAVE_WINSOCK */
#ifdef HAVE_FCNTL
  int f_set;

  if ((f_set = fcntl(sockfd, F_GETFL)) == -1) {
    log_error("fcntl F_GETFL failed: %s", fc_strerror(fc_get_errno()));
  }

  f_set |= O_NONBLOCK;

  if (fcntl(sockfd, F_SETFL, f_set) == -1) {
    log_error("fcntl F_SETFL failed: %s", fc_strerror(fc_get_errno()));
  }
#else  /* HAVE_FCNTL */
#ifdef HAVE_IOCTL
  long value=1;

  if (ioctl(sockfd, FIONBIO, (char*)&value) == -1) {
    log_error("ioctl failed: %s", fc_strerror(fc_get_errno()));
  }
#endif /* HAVE_IOCTL */
#endif /* HAVE_FCNTL */
#endif /* HAVE_WINSOCK */
#else  /* NONBLOCKING_SOCKETS */
  log_debug("NONBLOCKING_SOCKETS not available");
#endif /* NONBLOCKING_SOCKETS */
}

/***************************************************************************
  Write information about socaddr to debug log.
***************************************************************************/
void sockaddr_debug(union fc_sockaddr *addr)
{
#ifdef IPV6_SUPPORT
  char buf[INET6_ADDRSTRLEN] = "Unknown";

  if (addr->saddr.sa_family == AF_INET6) { 
    inet_ntop(AF_INET6, &addr->saddr_in6.sin6_addr, buf, INET6_ADDRSTRLEN);
    log_debug("Host: %s, Port: %d (IPv6)",
              buf, ntohs(addr->saddr_in6.sin6_port));
    return;
  } else if (addr->saddr.sa_family == AF_INET) {
    inet_ntop(AF_INET, &addr->saddr_in4.sin_addr, buf, INET_ADDRSTRLEN);
    log_debug("Host: %s, Port: %d (IPv4)",
              buf, ntohs(addr->saddr_in4.sin_port));
    return;
  }
#else  /* IPv6 support */
  if (addr->saddr.sa_family == AF_INET) {
    char *buf;

    buf = inet_ntoa(addr->saddr_in4.sin_addr);

    log_debug("Host: %s, Port: %d",
	      buf, ntohs(addr->saddr_in4.sin_port));

    return;
  }
#endif /* IPv6 support */

  log_error("Unsupported address family in sockaddr_debug()");
}

/***************************************************************************
  Gets size of address to fc_sockaddr. IPv6/IPv4 must be selected before
  calling this.
***************************************************************************/
int sockaddr_size(union fc_sockaddr *addr)
{
#ifdef IPV6_SUPPORT
  if (addr->saddr.sa_family == AF_INET6) {
    return sizeof(addr->saddr_in6);
  } else
#endif /* IPV6_SUPPORT */
  if (addr->saddr.sa_family == AF_INET) {
    return sizeof(addr->saddr_in4);
  } else {
    fc_assert(FALSE);

    log_error("Unsupported address family in socaddr_size()");

    return 0;
  }
}

/***************************************************************************
  Returns wether address is IPv6 address.
***************************************************************************/
bool sockaddr_ipv6(union fc_sockaddr *addr)
{
#ifdef IPV6_SUPPORT
  if (addr->saddr.sa_family == AF_INET6) {
    return TRUE;
  }
#endif /* IPv6 support */

  return FALSE;
}

#ifdef HAVE_GETADDRINFO
/***************************************************************************
  Look up the service at hostname:port using getaddrinfo().
***************************************************************************/
static struct fc_sockaddr_list *net_lookup_getaddrinfo(const char *name,
						       int port,
						       enum fc_addr_family family)
{
  struct addrinfo hints;
  struct addrinfo *res;
  int err;
  char servname[8];
  int gafam;
  struct fc_sockaddr_list *addrs = fc_sockaddr_list_new();

  switch (family) {
    case FC_ADDR_IPV4:
      gafam = AF_INET;
      break;
    case FC_ADDR_IPV6:
      gafam = AF_INET6;
      break;
    case FC_ADDR_ANY:
      gafam = AF_UNSPEC;
      break;
    default:
      fc_assert(FALSE);

      return addrs;
  }

  /* Convert port to string for getaddrinfo() */
  fc_snprintf(servname, sizeof(servname), "%d", port);

  /* Use getaddrinfo() to lookup IPv6 addresses */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = gafam;
  hints.ai_socktype = SOCK_DGRAM; /* any type that uses sin6_port */
  hints.ai_flags = AI_PASSIVE | FC_AI_NUMERICSERV;
  err = getaddrinfo(name, servname, &hints, &res);

  if (err == 0) {
    struct addrinfo *current = res;

    while (current != NULL) {
      union fc_sockaddr *caddr = fc_malloc(sizeof(caddr));

      memcpy(caddr, current->ai_addr, MIN(sizeof(caddr), current->ai_addrlen));

      fc_sockaddr_list_append(addrs, caddr);

      current = current->ai_next;
    }
  }

  return addrs;
}
#endif /* HAVE_GETADDRINFO */

/***************************************************************************
  Look up the service at hostname:port.
***************************************************************************/
struct fc_sockaddr_list *net_lookup_service(const char *name, int port,
					    enum fc_addr_family family)
{
  /* IPv6-enabled Freeciv always has HAVE_GETADDRINFO, IPv4-only Freeciv not
   * necessarily */
#ifdef HAVE_GETADDRINFO
  return net_lookup_getaddrinfo(name, port, family);
#else  /* HAVE_GETADDRINFO */

  struct sockaddr_in *sock4;
  struct hostent *hp;
  struct fc_sockaddr_list *addrs = fc_sockaddr_list_new();
  union fc_sockaddr *result = fc_malloc(sizeof(result));

  sock4 = &result->saddr_in4;

  fc_assert(family != FC_ADDR_IPV6);

  result->saddr.sa_family = AF_INET;
  sock4->sin_port = htons(port);

  if (!name) {
    sock4->sin_addr.s_addr = htonl(INADDR_ANY);
    fc_sockaddr_list_append(addrs, result);

    return addrs;
  }

#if defined(HAVE_INET_ATON)
  if (inet_aton(name, &sock4->sin_addr) != 0) {
    fc_sockaddr_list_append(addrs, result);

    return addrs;
  }
#else  /* HAVE_INET_ATON */
  if ((sock4->sin_addr.s_addr = inet_addr(name)) != INADDR_NONE) {
    fc_sockaddr_list_append(addrs, result);

    return addrs;
  }
#endif /* HAVE_INET_ATON */
  hp = gethostbyname(name);
  if (!hp || hp->h_addrtype != AF_INET) {
    FC_FREE(result);

    return addrs;
  }

  memcpy(&sock4->sin_addr, hp->h_addr, hp->h_length);
  fc_sockaddr_list_append(addrs, result);

  return addrs;

#endif /* !HAVE_GETADDRINFO */

}

/*************************************************************************
  Writes buf to socket and returns the response in an fz_FILE.
  Use only on blocking sockets.
*************************************************************************/
fz_FILE *fc_querysocket(int sock, void *buf, size_t size)
{
  FILE *fp;

#ifdef HAVE_FDOPEN
  fp = fdopen(sock, "r+b");
  if (fwrite(buf, 1, size, fp) != size) {
    log_error("socket %d: write error", sock);
  }
  fflush(fp);

  /* we don't use fc_closesocket on sock here since when fp is closed
   * sock will also be closed. fdopen doesn't dup the socket descriptor. */
#else  /* HAVE_FDOPEN */
  {
    char tmp[4096];
    int n;

#ifdef WIN32_NATIVE
    /* tmpfile() in mingw attempts to make a temp file in the root directory
     * of the current drive, which we may not have write access to. */
    {
      char filename[MAX_PATH];

      GetTempPath(sizeof(filename), filename);
      sz_strlcat(filename, "fctmp");

      fp = fc_fopen(filename, "w+b");
    }
#else  /* WIN32_NATIVE */

    fp = tmpfile();

#endif /* WIN32_NATIVE */

    if (fp == NULL) {
      return NULL;
    }

    fc_writesocket(sock, buf, size);

    while ((n = fc_readsocket(sock, tmp, sizeof(tmp))) > 0) {
      if (fwrite(tmp, 1, n, fp) != n) {
        log_error("socket %d: write error", sock);
      }
    }
    fflush(fp);

    fc_closesocket(sock);

    rewind(fp);
  }
#endif /* HAVE_FDOPEN */

  return fz_from_stream(fp);
}

/*************************************************************************
  Returns a valid httpd server and port, plus the path to the resource
  at the url location.
*************************************************************************/
const char *fc_lookup_httpd(char *server, int *port, const char *url)
{
  const char *purl, *str, *ppath, *pport;
  const char *str2;
  int chars_between = 0;

  if ((purl = getenv("http_proxy")) && purl[0] != '\0') {
    if (strncmp(purl, "http://", strlen("http://")) != 0) {
      return NULL;
    }
    str = purl;
  } else {
    purl = NULL;
    if (strncmp(url, "http://", strlen("http://")) != 0) {
      return NULL;
    }
    str = url;
  }

  str += strlen("http://");

  if (*str == '[') {
    /* Literal IPv6 address (RFC 2732) */
    str++;
    str2 = strchr(str, ']') + 1;
    if (!str2) {
      str2 = str + strlen(str);
    }
    chars_between = 1;
  } else {
    str2 = str;
  }

  pport = strchr(str2, ':');
  ppath = strchr(str2, '/');

  /* snarf server. */
  server[0] = '\0';

  if (pport) {
    strncat(server, str, MIN(MAX_LEN_ADDR, pport-str-chars_between));
  } else {
    if (ppath) {
      strncat(server, str, MIN(MAX_LEN_ADDR, ppath-str-chars_between));
    } else {
      strncat(server, str, MAX_LEN_ADDR);
    }
  }

  /* snarf port. */
  if (NULL == pport || !str_to_int(pport + 1, port)) {
    *port = 80;
  }

  /* snarf path. */
  if (!ppath) {
    ppath = "/";
  }

  return (purl ? url : ppath);
}

/*************************************************************************
  Returns TRUE if ch is an unreserved ASCII character.
*************************************************************************/
static bool is_url_safe(unsigned ch)
{
  const char *unreserved = "-_.!~*'|";

  if ((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9')) {
    return TRUE;
  } else {
    return (strchr(unreserved, ch) != NULL);
  }
}

/***************************************************************
  URL-encode a string as per RFC 2396.
  Should work for all ASCII based charsets: including UTF-8.
***************************************************************/
const char *fc_url_encode(const char *txt)
{
  static char buf[2048];
  unsigned ch;
  char *ptr;

  /* in a worst case scenario every character needs "% HEX HEX" encoding. */
  if (sizeof(buf) <= (3*strlen(txt))) {
    return "";
  }
  
  for (ptr = buf; *txt != '\0'; txt++) {
    ch = (unsigned char) *txt;

    if (is_url_safe(ch)) {
      *ptr++ = *txt;
    } else if (ch == ' ') {
      *ptr++ = '+';
    } else {
      sprintf(ptr, "%%%2.2X", ch);
      ptr += 3;
    }
  }
  *ptr++ = '\0';

  return buf;
}

/************************************************************************** 
  Finds the next (lowest) free port.
**************************************************************************/ 
int find_next_free_port(int starting_port, enum fc_addr_family family)
{
  int port;
  int s;
  int gafamily;
  bool found = FALSE;

#ifndef IPV6_SUPPORT
  fc_assert(family == FC_ADDR_IPV4 || family == FC_ADDR_ANY);
#endif

  switch (family) {
   case FC_ADDR_IPV4:
     gafamily = AF_INET;
     break;
   case FC_ADDR_IPV6:
     gafamily = AF_INET6;
     break;
   case FC_ADDR_ANY:
     gafamily = AF_UNSPEC;
     break;
   default:
     fc_assert(FALSE);

     return -1;
  }

  s = socket(gafamily, SOCK_STREAM, 0);

  for (port = starting_port; !found ; port++) {
    /* HAVE_GETADDRINFO implies IPv6 support */
#ifdef HAVE_GETADDRINFO
    struct addrinfo hints;
    int err;
    char servname[8];
    struct addrinfo *res;

    fc_snprintf(servname, sizeof(servname), "%d", port);

    hints.ai_family = gafamily;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE | FC_AI_NUMERICSERV;

    err = getaddrinfo(NULL, servname, &hints, &res);
    if (!err) {
      struct addrinfo *current = res;

      while (current != NULL && !found) {
        if (bind(s, current->ai_addr, current->ai_addrlen) == 0) {
          found = TRUE;
        }
        current = current->ai_next;
      }

      freeaddrinfo(res);
    }
#else /* HAVE_GETADDRINFO */
    union fc_sockaddr tmp;
    struct sockaddr_in *sock4;

    sock4 = &tmp.saddr_in4;
    memset(&tmp, 0, sizeof(tmp));
    sock4->sin_family = AF_INET;
    sock4->sin_port = htons(port);
    sock4->sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, &tmp.saddr, sockaddr_size(&tmp)) == 0) {
      found = TRUE;
    }
#endif /* HAVE_GETADDRINFO */
  }

  fc_closesocket(s);
  
  return port;
}
