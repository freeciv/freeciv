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
#include <string.h>
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

#include <gtk/gtk.h>

#include "mem.h"		/* required by get_meta_list() */
#include "shared.h"		/* required by get_meta_list() */
#include "version.h"

#include "chatline.h"
#include "clinet.h"
#include "colors.h"
#include "dialogs.h"
#include "gui_stuff.h"

#include "connectdlg.h"

extern char name[];
extern char server_host[];
extern int  server_port;

static GtkWidget *iname, *ihost, *iport;
static GtkWidget *connw, *quitw;

extern GtkWidget *toplevel;

static GtkWidget *dialog;

/* meta Server */
extern char metaserver[];
int  update_meta_dialog(GtkWidget *meta_list);
void meta_list_callback(GtkWidget *w, gint row, gint column);
void meta_update_callback(GtkWidget *w, gpointer data);

static int get_meta_list(GtkWidget *list, char *errbuf);

/**************************************************************************
...
**************************************************************************/
static void connect_callback(GtkWidget *w, gpointer data)
{
  char errbuf [512];

  strcpy(name, gtk_entry_get_text(GTK_ENTRY(iname)));
  strcpy(server_host, gtk_entry_get_text(GTK_ENTRY(ihost)));
  server_port=atoi(gtk_entry_get_text(GTK_ENTRY(iport)));
  
  if(connect_to_server(name, server_host, server_port, errbuf)!=-1) {
    gtk_widget_destroy(dialog);
    gtk_widget_set_sensitive(toplevel,TRUE);
  }
  else
    append_output_window(errbuf);
}

/**************************************************************************
...
**************************************************************************/
int update_meta_dialog(GtkWidget *meta_list)
{
  char errbuf[128];

  if(get_meta_list(meta_list,errbuf)!=-1)  {
    return 1;
  } else {
    append_output_window(errbuf);
    return 0;
  }
}

/**************************************************************************
...
**************************************************************************/
void meta_update_callback(GtkWidget *w, gpointer data)
{
  update_meta_dialog(GTK_WIDGET(data));
}

/**************************************************************************
...
**************************************************************************/
void meta_list_callback(GtkWidget *w, gint row, gint column)
{
  gchar *name, *port;

  gtk_clist_get_text(GTK_CLIST(w), row, 0, &name);
  gtk_entry_set_text(GTK_ENTRY(ihost), name);
  gtk_clist_get_text(GTK_CLIST(w), row, 1, &port);
  gtk_entry_set_text(GTK_ENTRY(iport), port);
}

/**************************************************************************
...
**************************************************************************/
static gint connect_deleted_callback(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  gtk_main_quit();
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
void gui_server_connect(void)
{
  GtkWidget *label, *table, *book, *scrolled, *list, *vbox, *update, *label2;
  char *titles[6]= {"Server Name", "Port", "Version", "Status", "Players",
		    "Comment"};
  char buf [256];
  int i;

  gtk_widget_set_sensitive(toplevel, FALSE);

  dialog=gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(dialog),"delete_event",
	GTK_SIGNAL_FUNC(connect_deleted_callback), NULL);
  
  gtk_window_set_title(GTK_WINDOW(dialog), " Connect to FreeCiv Server");

  book = gtk_notebook_new ();
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), book, TRUE, TRUE, 0);


  label=gtk_label_new("Freeciv Server Selection");

  vbox=gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  table = gtk_table_new (4, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 5);
  gtk_container_border_width (GTK_CONTAINER (table), 5);
  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, TRUE, 0);

  label=gtk_label_new("Name:");
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, 0, 0, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  iname=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(iname), name);
  gtk_table_attach_defaults (GTK_TABLE (table), iname, 1, 2, 0, 1);

  label=gtk_label_new("Host:");
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2, 0, 0, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  ihost=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(ihost), server_host);
  gtk_table_attach_defaults (GTK_TABLE (table), ihost, 1, 2, 1, 2);

  label=gtk_label_new("Port:");
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3, 0, 0, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  sprintf(buf, "%d", server_port);

  iport=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(iport), buf);
  gtk_table_attach_defaults (GTK_TABLE (table), iport, 1, 2, 2, 3);

  if (IS_BETA_VERSION)
  {
    GtkStyle *style;

    label2=gtk_label_new ("THIS IS A BETA RELEASE\n"
			  "Freeciv 1.8 will be released third\n"
			  "week of March at http://www.freeciv.org");

    style=gtk_style_copy (label2->style);
    style->fg[GTK_STATE_NORMAL]=*colors_standard[COLOR_STD_RED];
    gtk_widget_set_style (label2, style);
    gtk_table_attach_defaults (GTK_TABLE (table), label2, 0, 2, 3, 4);
  }


  label=gtk_label_new("Metaserver");

  vbox=gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  list=gtk_clist_new_with_titles(6, titles);
  gtk_clist_column_titles_passive(GTK_CLIST(list));

  for(i=0; i<6; i++)
    gtk_clist_set_column_auto_resize(GTK_CLIST(list), i, TRUE);

  scrolled=gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), list);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  update=gtk_button_new_with_label("Update");
  gtk_box_pack_start(GTK_BOX(vbox), update, FALSE, FALSE, 2);

  gtk_signal_connect(GTK_OBJECT(list), "select_row",
			GTK_SIGNAL_FUNC(meta_list_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(update), "clicked",
			GTK_SIGNAL_FUNC(meta_update_callback), (gpointer)list);

  connw=gtk_button_new_with_label("Connect");
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), connw,
	TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(connw, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(connw);

  quitw=gtk_button_new_with_label("Quit");
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), quitw,
	TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(quitw, GTK_CAN_DEFAULT);

  gtk_widget_grab_focus (iname);

  gtk_signal_connect(GTK_OBJECT(iname), "activate",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(ihost), "activate",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(iport), "activate",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(connw), "clicked",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(quitw), "clicked",
        	      GTK_SIGNAL_FUNC(gtk_main_quit), NULL);

  gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);
  gtk_widget_show_all(GTK_DIALOG(dialog)->action_area);

  gtk_widget_set_usize(dialog, 450, 200);
  gtk_set_relative_position(toplevel, dialog, 50, 50);
  gtk_widget_show(dialog);
}

/**************************************************************************
  Get the list of servers from the metaserver
  Should be moved to clinet.c somewhen
**************************************************************************/
static int get_meta_list(GtkWidget *list, char *errbuf)
{
  struct sockaddr_in addr;
  struct hostent *ph;
  int s, i;
  FILE *f;
  char *row[6];
  char  buf[6][64];
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
    if (strncmp(metaserver,"http://",strlen("http://"))) {
      strcpy(errbuf, "Invalid metaserver URL, must start with 'http://'");
      return -1;
    }
    strncpy(urlbuf,metaserver,511);
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
    proxy_url ? metaserver : "");
  fflush(f);

#define NEXT_FIELD p=strstr(p,"<TD>"); if(p==NULL) continue; p+=4;
#define END_FIELD  p=strstr(p,"</TD>"); if(p==NULL) continue; *p++='\0';
#define GET_FIELD(x) NEXT_FIELD (x)=p; END_FIELD

  gtk_clist_freeze(GTK_CLIST(list));
  gtk_clist_clear(GTK_CLIST(list));

  for (i=0; i<6; i++)
    row[i]=buf[i];

  while(fgets(str,512,f)!=NULL)  {
    if(!strncmp(str,"<TR BGCOLOR",11))  {
      char *name,*port,*version,*status,*players,*metastring;
      char *p;

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

      sprintf(row[0], "%.63s", name);
      sprintf(row[1], "%.63s", port);
      sprintf(row[2], "%.63s", version);
      sprintf(row[3], "%.63s", status);
      sprintf(row[4], "%.63s", players);
      sprintf(row[5], "%.63s", metastring);

      gtk_clist_append(GTK_CLIST(list), row);
    }
  }
  gtk_clist_thaw(GTK_CLIST(list));
  fclose(f);

  return 0;
}
