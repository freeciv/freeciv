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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/uio.h>
#include <sys/time.h>

#include <pwd.h>
#include <string.h>
#include <errno.h>

#ifdef AIX
#include <sys/select.h>
#endif

#include <signal.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <civclient.h>
#include <clinet.h>
#include <log.h>
#include <chatline.h>
#include <packets.h>
#include <xmain.h>
#include <chatline.h>
#include <game.h>
#include <packhand.h>
extern Widget toplevel, main_form, menu_form, below_menu_form, left_column_form;

struct connection aconnection;
int server_has_autoattack=0;
extern int errno;
extern XtInputId x_input_id;
extern XtAppContext app_context;

/**************************************************************************
...
**************************************************************************/
int connect_to_server(char *name, char *hostname, int port, char *errbuf)
{
  /* use name to find TCPIP address of server */
  struct sockaddr_in src;
  struct hostent *ph;
  long address;
  struct packet_req_join_game req;

  if(port==0)
    port=DEFAULT_SOCK_PORT;
  
  if(!hostname)
    hostname="localhost";
  
  if(isdigit((int)*hostname)) {
    if((address = inet_addr(hostname)) == -1) {
      strcpy(errbuf, "Invalid hostname");
      return -1;
    }
    src.sin_addr.s_addr = address;
    src.sin_family = AF_INET;
  }
  else if ((ph = gethostbyname(hostname)) == NULL) {
    strcpy(errbuf, "Failed looking up host");
    return -1;
  }
  else {
    src.sin_family = ph->h_addrtype;
    memcpy((char *) &src.sin_addr, ph->h_addr, ph->h_length);
  }
  
  src.sin_port = htons(port);
  
  /* ignore broken pipes */
  signal (SIGPIPE, SIG_IGN);
  
  if((aconnection.sock = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
    strcpy(errbuf, mystrerror(errno));
    return -1;
  }
  
  if(connect(aconnection.sock, (struct sockaddr *) &src, sizeof (src)) < 0) {
    strcpy(errbuf, mystrerror(errno));
    close(aconnection.sock);
    return -1;
  }

  aconnection.buffer.ndata=0;

  x_input_id=XtAppAddInput(app_context, aconnection.sock, 
			   (XtPointer) XtInputReadMask,
			   (XtInputCallbackProc) get_net_input, NULL);

  
  /* now send join_request package */

  strcpy(req.name, name);
  req.major_version=MAJOR_VERSION;
  req.minor_version=MINOR_VERSION;
  req.patch_version=PATCH_VERSION;
  strcpy(req.capability, our_capability);

  send_packet_req_join_game(&aconnection, &req);
  
  return 0;
}

void disconnect_from_server(void)
{
  append_output_window("Disconnecting from server.");
  close(aconnection.sock);
  remove_net_input();
  set_client_state(CLIENT_PRE_GAME_STATE);
}  

/**************************************************************************
...
**************************************************************************/
void get_net_input(XtPointer client_data, int *fid, XtInputId *id)
{
  if(read_socket_data(*fid, &aconnection.buffer)>0) {
    int type;
    char *packet;

    while((packet=get_packet_from_connection(&aconnection, &type))) {
      handle_packet_input(packet, type);
    }
  }
  else {
    append_output_window("Lost connection to server!");
    freelog(LOG_NORMAL, "lost connection to server");
    close(*fid);
    remove_net_input();
    popdown_races_dialog(); 
    set_client_state(CLIENT_PRE_GAME_STATE);
  }
}





/**************************************************************************
...
**************************************************************************/
void close_server_connection(void)
{
  close(aconnection.sock);
}


/**************************************************************************
  Get the list of servers from the metaserver
**************************************************************************/
int get_meta_list(char *server, char **list, char *errbuf)
{
  struct sockaddr_in addr;
  struct hostent *ph;
  int s;
  FILE *f;
  char str[512],*p;
  char line[256];
  char *name,*port,*version,*status,*players,*metastring;

  if ((ph = gethostbyname(server)) == NULL) {
    strcpy(errbuf, "Failed looking up host");
    return -1;
  } else {
    addr.sin_family = ph->h_addrtype;
    memcpy((char *) &addr.sin_addr, ph->h_addr, ph->h_length);
  }
  
  addr.sin_port = htons(80);
  
  if((s = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
    strcpy(errbuf, mystrerror(errno));
    return -1;
  }
  
  if(connect(s, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    strcpy(errbuf, mystrerror(errno));
    close(s);
    return -1;
  }

  f=fdopen(s,"r+");
  fputs("GET /~lancelot/freeciv.html\r\n\r\n",f); fflush(f);

#define NEXT_FIELD p=strstr(p,"<TD>"); if(p==NULL) continue; p+=4;
#define END_FIELD  p=strstr(p,"</TD>"); if(p==NULL) continue; *p++='\0';
#define GET_FIELD(x) NEXT_FIELD (x)=p; END_FIELD

  while( fgets(str,512,f)!=NULL)  {
    if(!strncmp(str,"<TR BGCOLOR",11))  {
      p=strstr(str,"<a"); if(p==NULL) continue;
      p=strchr(p,'>');    if(p==NULL) continue;
      name=++p;
      p=strstr(p,"</a>"); if(p==NULL) continue;
      *p++='\0';

      GET_FIELD(port);
      GET_FIELD(version);
      GET_FIELD(status);
      GET_FIELD(players);
      GET_FIELD(metastring);

      sprintf(line,"%-35s %-5s %-7s %-9s %2s   %s",
              name,port,version,status,players,metastring);
      if(*list) free(*list);
      *list=malloc(strlen(line)+1); strcpy(*list,line); list++;
    }
  }
  fclose(f);
  *list=NULL;

  return 0;
}
