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
#include <config.h>
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

#include "log.h"
#include "shared.h"		/* TRUE, FALSE */
#include "support.h"

#include "netintf.h"

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif


/***************************************************************
  Read from a socket.
***************************************************************/
int my_readsocket(int sock, void *buf, size_t size)
{
#ifdef HAVE_WINSOCK
  return recv(sock, buf, size, 0);
#else
  return read(sock, buf, size);
#endif
}

/***************************************************************
  Write to a socket.
***************************************************************/
int my_writesocket(int sock, const void *buf, size_t size)
{
#ifdef HAVE_WINSOCK
  return send(sock, buf, size, 0);
#else
  return write(sock, buf, size);
#endif
}

/***************************************************************
  Close a socket.
***************************************************************/
void my_closesocket(int sock)
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
void my_init_network(void)
{
#ifdef HAVE_WINSOCK
  WSADATA wsa;

  if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
    freelog(LOG_ERROR, "no usable WINSOCK.DLL: %s", mystrerror());
  }
#endif

  /* broken pipes are ignored. */
#ifdef HAVE_SIGPIPE
  (void) signal(SIGPIPE, SIG_IGN);
#endif
}

/***************************************************************
  Shutdown network stuff.
***************************************************************/
void my_shutdown_network(void)
{
#ifdef HAVE_WINSOCK
  WSACleanup();
#endif
}

/***************************************************************
  Set socket to non-blocking.
***************************************************************/
void my_nonblock(int sockfd)
{
#ifdef NONBLOCKING_SOCKETS
#ifdef HAVE_FCNTL
  int f_set;

  if ((f_set=fcntl(sockfd, F_GETFL)) == -1) {
    freelog(LOG_ERROR, "fcntl F_GETFL failed: %s", mystrerror());
  }

  f_set |= O_NONBLOCK;

  if (fcntl(sockfd, F_SETFL, f_set) == -1) {
    freelog(LOG_ERROR, "fcntl F_SETFL failed: %s", mystrerror());
  }
#else
#ifdef HAVE_IOCTL
  long value=1;

  if (ioctl(sockfd, FIONBIO, (char*)&value) == -1) {
    freelog(LOG_ERROR, "ioctl failed: %s", mystrerror());
  }
#endif
#endif
#else
  freelog(LOG_DEBUG, "NONBLOCKING_SOCKETS not available");
#endif
}

/***************************************************************************
  Look up the service at hostname:port and fill in *sa.
***************************************************************************/
bool net_lookup_service(const char *name, int port, union my_sockaddr *addr)
{
  struct hostent *hp;
  struct sockaddr_in *sock = &addr->sockaddr_in;

  sock->sin_family = AF_INET;
  sock->sin_port = htons(port);

  if (!name) {
    sock->sin_addr.s_addr = htonl(INADDR_ANY);
    return TRUE;
  }

#ifdef HAVE_INET_ATON
  if (inet_aton(name, &sock->sin_addr) != 0) {
    return TRUE;
  }
#else
  if ((sock->sin_addr.s_addr = inet_addr(name)) != INADDR_NONE) {
    return TRUE;
  }
#endif
  hp = gethostbyname(name);
  if (!hp || hp->h_addrtype != AF_INET) {
    return FALSE;
  }

  memcpy(&sock->sin_addr, hp->h_addr, hp->h_length);
  return TRUE;
}

/*************************************************************************
  Writes buf to socket and returns the response in an fz_FILE.
  Use only on blocking sockets.
*************************************************************************/
fz_FILE *my_querysocket(int sock, void *buf, size_t size)
{
  FILE *fp;

#ifdef HAVE_FDOPEN
  fp = fdopen(sock, "r+b");
  if (fwrite(buf, 1, size, fp) != size) {
    die("socket %d: write error", sock);
  }
  fflush(fp);

  /* we don't use my_closesocket on sock here since when fp is closed
   * sock will also be closed. fdopen doesn't dup the socket descriptor. */
#else
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

      fp = fopen(filename, "w+b");
    }
#else

    fp = tmpfile();

#endif

    if (fp == NULL) {
      return NULL;
    }

    my_writesocket(sock, buf, size);

    while ((n = my_readsocket(sock, tmp, sizeof(tmp))) > 0) {
      if (fwrite(tmp, 1, n, fp) != n) {
	die("socket %d: write error", sock);
      }
    }
    fflush(fp);

    my_closesocket(sock);

    rewind(fp);
  }
#endif

  return fz_from_stream(fp);
}

/*************************************************************************
  Returns a valid httpd server and port, plus the path to the resource
  at the url location.
*************************************************************************/
const char *my_lookup_httpd(char *server, int *port, const char *url)
{
  const char *purl, *str, *ppath, *pport;

  if ((purl = getenv("http_proxy"))) {
    if (strncmp(purl, "http://", strlen("http://")) != 0) {
      return NULL;
    }
    str = purl;
  } else {
    if (strncmp(url, "http://", strlen("http://")) != 0) {
      return NULL;
    }
    str = url;
  }

  str += strlen("http://");
  
  pport = strchr(str, ':');
  ppath = strchr(str, '/');

  /* snarf server. */
  server[0] = '\0';

  if (pport) {
    strncat(server, str, MIN(MAX_LEN_ADDR, pport-str));
  } else {
    if (ppath) {
      strncat(server, str, MIN(MAX_LEN_ADDR, ppath-str));
    } else {
      strncat(server, str, MAX_LEN_ADDR);
    }
  }

  /* snarf port. */
  if (!pport || sscanf(pport+1, "%d", port) != 1) {
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
const char *my_url_encode(const char *txt)
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
int find_next_free_port(int starting_port)
{
  int port, s = socket(AF_INET, SOCK_STREAM, 0);

  for (port = starting_port;; port++) {
    union my_sockaddr tmp;
    struct sockaddr_in *sock = &tmp.sockaddr_in;

    memset(&tmp, 0, sizeof(tmp));

    sock->sin_family = AF_INET;
    sock->sin_port = htons(port);
    sock->sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, &tmp.sockaddr, sizeof(tmp.sockaddr)) == 0) {
      break;
    }
  }

  my_closesocket(s);
  
  return port;
}
