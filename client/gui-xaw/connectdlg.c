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

/* The following includes are (only) required for get_meta_list() */
#include <errno.h>
#include <ctype.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif


#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/AsciiText.h>  
#include <X11/Xaw/List.h>

#include "mem.h"		/* required by get_meta_list() */
#include "shared.h"		/* required by get_meta_list() */
#include "version.h"

#include "chatline.h"
#include "clinet.h"
#include "gui_stuff.h"

#include "connectdlg.h"

/* extern AppResources appResources; */
extern Widget toplevel;
extern char name[];
extern char server_host[];
extern int  server_port;

Widget iname, ihost, iport;
Widget connw, metaw, quitw;

void server_address_ok_callback(Widget w, XtPointer client_data, 
				XtPointer call_data);
void quit_callback(Widget w, XtPointer client_data, XtPointer call_data);
void connect_callback(Widget w, XtPointer client_data, XtPointer call_data);
void connect_meta_callback(Widget w, XtPointer client_data, XtPointer call_data);


/* Meta Server */
Widget meta_dialog_shell=0;
char *server_list[64]={NULL};

void create_meta_dialog(void);
int  update_meta_dialog(Widget meta_list);
void meta_list_callback(Widget w, XtPointer client_data, XtPointer call_data);
void meta_list_destroy(Widget w, XtPointer client_data, XtPointer call_data);
void meta_update_callback(Widget w, XtPointer client_data, XtPointer call_data);
void meta_close_callback(Widget w, XtPointer client_data, XtPointer call_data);

static int get_meta_list(char **list, char *errbuf);

void gui_server_connect(void)
{
  Widget shell, form, label, label2;
  char buf[512];
  
  XtTranslations textfieldtranslations;
  
  XtSetSensitive(toplevel, FALSE);
  
  shell=XtCreatePopupShell("connectdialog", transientShellWidgetClass,
			   toplevel, NULL, 0);
  
  form=XtVaCreateManagedWidget("cform", formWidgetClass, shell, NULL);

  label=XtVaCreateManagedWidget("cheadline", labelWidgetClass, form, NULL);   

  XtVaCreateManagedWidget("cnamel", labelWidgetClass, form, NULL);   
  iname=XtVaCreateManagedWidget("cnamei", asciiTextWidgetClass, form, 
			  XtNstring, name, NULL);

  XtVaCreateManagedWidget("chostl", labelWidgetClass, form, NULL);   
  ihost=XtVaCreateManagedWidget("chosti", asciiTextWidgetClass, form, 
			  XtNstring, server_host, NULL);

  sprintf(buf, "%d", server_port);
  
  XtVaCreateManagedWidget("cportl", labelWidgetClass, form, NULL);   
  iport=XtVaCreateManagedWidget("cporti", asciiTextWidgetClass, form, 
			  XtNstring, buf, NULL);

  connw=XtVaCreateManagedWidget("cconnectc", commandWidgetClass, form, NULL);   
  metaw=XtVaCreateManagedWidget("cmetac", commandWidgetClass, form, NULL);
  quitw=XtVaCreateManagedWidget("cquitc", commandWidgetClass, form, NULL); 

  if (IS_BETA_VERSION)
    label2=XtVaCreateManagedWidget("cbetaline", labelWidgetClass, form, NULL);

  XtAddCallback(connw, XtNcallback, connect_callback, NULL);
  XtAddCallback(quitw, XtNcallback, quit_callback, NULL);
  XtAddCallback(metaw, XtNcallback, connect_meta_callback, NULL);

  XtPopup(shell, XtGrabNone);
  xaw_set_relative_position(toplevel, shell, 50, 50);

  textfieldtranslations = 
    XtParseTranslationTable("<Key>Return: connect-dialog-returnkey()");
  XtOverrideTranslations(form, textfieldtranslations);
  XtOverrideTranslations(iname, textfieldtranslations);
  XtOverrideTranslations(ihost, textfieldtranslations);
  XtOverrideTranslations(iport, textfieldtranslations);

  XtSetKeyboardFocus(toplevel, shell);
}

/****************************************************************
...
*****************************************************************/
void connect_dialog_returnkey(Widget w, XEvent *event, String *params,
			    Cardinal *num_params)
{
  x_simulate_button_click(connw);
}
  
  
  
void quit_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data) 
{
  exit(0);
}


  
    
void connect_callback(Widget w, XtPointer client_data, 
		      XtPointer call_data) 
{
  XtPointer dp;
  char errbuf[512];
  
  XtVaGetValues(iname, XtNstring, &dp, NULL);
  strcpy(name, (char*)dp);
  XtVaGetValues(ihost, XtNstring, &dp, NULL);
  strcpy(server_host, (char*)dp);
  XtVaGetValues(iport, XtNstring, &dp, NULL);
  sscanf((char*)dp, "%d", &server_port);
  
  if(connect_to_server(name, server_host, server_port, errbuf)!=-1) {
    XtDestroyWidget(XtParent(XtParent(w)));
    if(meta_dialog_shell) {
      XtDestroyWidget(meta_dialog_shell);
      meta_dialog_shell=0;
    }
    XtSetSensitive(toplevel, True);
    return;
  }
  
  append_output_window(errbuf);
}


void connect_meta_callback(Widget w, XtPointer client_data,
                           XtPointer call_data)
{
  if(meta_dialog_shell) return;  /* Metaserver window already poped up */
  create_meta_dialog();
}

void create_meta_dialog(void)
{
  Widget shell, form, label, list, update, close;
  Dimension width;

  shell=XtCreatePopupShell("metadialog", transientShellWidgetClass,
			   toplevel, NULL, 0);
  meta_dialog_shell=shell;

  form=XtVaCreateManagedWidget("metaform", formWidgetClass, shell, NULL);

  label=XtVaCreateManagedWidget("legend", labelWidgetClass, form, NULL);
  list=XtVaCreateManagedWidget("metalist", listWidgetClass, form, NULL);
  update=XtVaCreateManagedWidget("update", commandWidgetClass, form, NULL);
  close=XtVaCreateManagedWidget("closecommand", commandWidgetClass, form, NULL);

  if(!update_meta_dialog(list))  {
    XtDestroyWidget(shell);
    meta_dialog_shell=0;
    return;
  }

  XtAddCallback(list, XtNcallback, meta_list_callback, NULL);
  XtAddCallback(list, XtNdestroyCallback, meta_list_destroy, NULL);
  XtAddCallback(update, XtNcallback, meta_update_callback, (XtPointer)list);
  XtAddCallback(close, XtNcallback, meta_close_callback, NULL);

  /* XtRealizeWidget(shell); */

  XtVaGetValues(list, XtNwidth, &width, NULL);
  XtVaSetValues(label, XtNwidth, width, NULL);

  XtPopup(shell, XtGrabNone);
  xaw_set_relative_position(toplevel, shell, 5, 20);

  XtSetKeyboardFocus(toplevel, shell);
}

int update_meta_dialog(Widget meta_list)
{
  char errbuf[128];

  if(get_meta_list(server_list,errbuf)!=-1)  {
    XawListChange(meta_list,server_list,0,0,True);
    return 1;
  } else {
    append_output_window(errbuf);
    return 0;
  }
}

void meta_update_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  update_meta_dialog((Widget)client_data);
}

void meta_close_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  XtDestroyWidget(meta_dialog_shell);
  meta_dialog_shell=0;
}

void meta_list_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(w);
  char name[64], port[16];

  sscanf(ret->string,"%s %s\n",name,port);
  XtVaSetValues(ihost, XtNstring, name, NULL);
  XtVaSetValues(iport, XtNstring, port, NULL);
}

void meta_list_destroy(Widget w, XtPointer client_data, XtPointer call_data)
{
  int i;

  for(i=0;server_list[i]!=NULL;i++)  {
    free(server_list[i]);
    server_list[i]=NULL;
  }
}



/**************************************************************************
  Get the list of servers from the metaserver
  Should be moved to clinet.c somewhen
**************************************************************************/
static int get_meta_list(char **list, char *errbuf)
{
  struct sockaddr_in addr;
  struct hostent *ph;
  int s;
  FILE *f;
  char *proxy_url = (char *)NULL;
  char urlbuf[512];
  char *urlpath;
  char *server;
  int port;
  char str[512];

  if ((proxy_url = getenv("http_proxy"))) {
    if (strncmp(proxy_url,"http://",strlen("http://"))) {
      strcpy(errbuf, "Invalid $http_proxy value, must start with 'http://'");
      return -1;
    }
    strncpy(urlbuf,proxy_url,511);
  } else {
    if (strncmp(METALIST_ADDR,"http://",strlen("http://"))) {
      strcpy(errbuf, "Invalid metaserver URL, must start with 'http://'");
      return -1;
    }
    strncpy(urlbuf,METALIST_ADDR,511);
  }
  server = &urlbuf[strlen("http://")];

  {
    char *s;
    if ((s = strchr(server,':'))) {
      port = atoi(&s[1]);
      if (!port) {
        port = 80;
      }
      s[0] = '\0';
      ++s;
      while (isdigit(s[0])) {++s;}
    } else {
      port = 80;
      if (!(s = strchr(server,'/'))) {
        s = &server[strlen(server)];
      }
    }  /* s now points past the host[:port] part */

    if (s[0] == '/') {
      s[0] = '\0';
      ++s;
    } else if (s[0]) {
      strcpy(errbuf, "Invalid $http_proxy value, cannot find separating '/'");
      /* which is obligatory if more characters follow */
      return -1;
    }
    urlpath = s;
  }

  if ((ph = gethostbyname(server)) == NULL) {
    strcpy(errbuf, "Failed looking up host");
    return -1;
  } else {
    addr.sin_family = ph->h_addrtype;
    memcpy((char *) &addr.sin_addr, ph->h_addr, ph->h_length);
  }
  
  addr.sin_port = htons(port);
  
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
  fprintf(f,"GET %s%s%s HTTP/1.0\r\n\r\n",
    proxy_url ? "" : "/",
    urlpath,
    proxy_url ? METALIST_ADDR : "");
  fflush(f);

#define NEXT_FIELD p=strstr(p,"<TD>"); if(p==NULL) continue; p+=4;
#define END_FIELD  p=strstr(p,"</TD>"); if(p==NULL) continue; *p++='\0';
#define GET_FIELD(x) NEXT_FIELD (x)=p; END_FIELD

  while( fgets(str,512,f)!=NULL)  {
    if(!strncmp(str,"<TR BGCOLOR",11))  {
      char *name,*port,*version,*status,*players,*metastring;
      char *p;
      char line[256];

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
      *list=mystrdup(line);
      list++;
    }
  }
  fclose(f);
  *list=NULL;

  return 0;
}
