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
#include <string.h>
#include <assert.h>

#include <gtk/gtk.h>

#include "city.h"
#include "game.h"
#include "genlist.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "tech.h"
#include "unit.h"
#include "version.h"

#include "climisc.h"
#include "colors.h"
#include "graphics.h"
#include "gui_stuff.h"

#include "helpdlg.h"


#define	HELP_UNIT_FONT		"-*-times-bold-r-*-*-14-*-*-*-*-*-*-*"
#define	HELP_TEXT_FONT		"-*-fixed-medium-r-*-*-14-*-*-*-*-*-iso8859-*"


char *help_type_names[] = { "", "TEXT", "UNIT", "IMPROVEMENT",
                           "WONDER", "TECH", 0 };
#define MAX_LAST (MAX(MAX(A_LAST,B_LAST),U_LAST))

void select_help_item_string(char *item, enum help_page_type htype);


extern GtkWidget *toplevel;


struct help_item {
  char *topic, *text;
  enum help_page_type type;
};
static struct genlist help_nodes;

#define help_list_iterate(helplist, pitem) { \
  struct genlist_iterator myiter; \
  struct help_item *pitem; \
  for( genlist_iterator_init(&myiter, &helplist, 0); \
       (pitem=ITERATOR_PTR(myiter)); \
       ITERATOR_NEXT(myiter) ) {
#define help_list_iterate_end }}

char *topic_list[1024];

char long_buffer[64000];

/*
 * Globals.
 */
GtkWidget *		help_dialog_shell;
GtkWidget *		help_clist;
GtkWidget *		help_clist_scrolled;
GtkWidget *		help_frame;
GtkWidget *		help_text;
GtkWidget *		help_text_scrolled;
GtkWidget *		help_vbox;
GtkWidget *		unit_tile;
GtkWidget *		help_box;
GtkWidget *		help_itable;
GtkWidget *		help_wtable;
GtkWidget *		help_utable;
GtkWidget *		help_tree;
GtkWidget *		help_tree_scrolled;
GtkWidget *		help_ilabel	[6];
GtkWidget *		help_wlabel	[6];
GtkWidget *		help_ulabel	[4][5];

char *help_ilabel_name[6] =
{ "Cost:", "", "Upkeep:", "", "Requirement:", "" };

char *help_wlabel_name[6] =
{ "Cost:", "", "Requirement:", "", "Obsolete by:", "" };

char *help_ulabel_name[4][5] =
{
    { "Cost:",		"", "",	"Attack:",	"" },
    { "Defense:",	"", "",	"Move:"	,	"" },
    { "FirePower:",	"", "",	"Hitpoints:",	"" },
    { "Requirement:",	"", "",	"Obsolete by:",	"" }
};


void create_help_dialog(void);
void help_update_dialog(struct help_item *pitem);
void create_help_page(enum help_page_type type);


/****************************************************************
...
*****************************************************************/
struct help_item *find_help_item_position(int pos)
{
  return genlist_get(&help_nodes, pos);
}

/****************************************************************
...
*****************************************************************/
void set_title_topic(char *topic)
{
  if(!strcmp(topic, "Freeciv") || !strcmp(topic, "About"))
    gtk_frame_set_label(GTK_FRAME(help_frame), FREECIV_NAME_VERSION);
  else
    gtk_frame_set_label(GTK_FRAME(help_frame), topic);
  return;
}

/****************************************************************
...
*****************************************************************/
static struct help_item *new_help_item(int type)
{
  struct help_item *pitem;
  
  pitem = fc_malloc(sizeof(struct help_item));
  pitem->topic = NULL;
  pitem->text = NULL;
  pitem->type = type;
  return pitem;
}

/* for genlist_sort(); sort by topic via strcmp */
static int help_item_compar(const void *a, const void *b)
{
  const struct help_item *ha, *hb;
  ha = (const struct help_item*) *(const void**)a;
  hb = (const struct help_item*) *(const void**)b;
  return strcmp(ha->topic, hb->topic);
}

void boot_help_texts(void)
{
  static int booted=0;
  
  FILE *fs;
  char *dfname;
  char buf[512], *p;
  char expect[32], name[MAX_LENGTH_NAME+2];
  char seen[MAX_LAST], *pname;
  int len;
  struct help_item *pitem = NULL;
  enum help_page_type current_type = HELP_TEXT;
  struct genlist category_nodes;
  int i, filter_this;

  if(help_dialog_shell) {
    /* need to do something like this or bad things happen */
    gtk_widget_destroy(help_dialog_shell);
    help_dialog_shell=0;
  }
  
  if(!booted) {
    freelog(LOG_VERBOSE, "Booting help texts");
    genlist_init(&help_nodes);
  } else {
    /* free memory allocated last time booted */
    help_list_iterate(help_nodes, ptmp) {
      free(ptmp->topic);
      free(ptmp->text);
      free(ptmp);
    }
    help_list_iterate_end;
    genlist_unlink_all(&help_nodes);
    freelog(LOG_VERBOSE, "Rebooting help texts");
  }    
  
  dfname = datafilename("helpdata.txt");
  if (dfname == NULL) {
    freelog(LOG_NORMAL, "Could not find readable helpdata.txt in data path");
    freelog(LOG_NORMAL, "The data path may be set via"
	                " the environment variable FREECIV_PATH");
    freelog(LOG_NORMAL, "Current data path is: \"%s\"", datafilename(NULL));
    freelog(LOG_NORMAL, "Did not read help texts");
    return;
  }
  fs = fopen(dfname, "r");
  if (fs == NULL) {
    /* this is now unlikely to happen */
    freelog(LOG_NORMAL, "failed reading help-texts");
    return;
  }

  while(1) {
    fgets(buf, 512, fs);
    buf[strlen(buf)-1]='\0';
    if(!strncmp(buf, "%%", 2))
      continue;
    len=strlen(buf);

    if (len>0 && buf[0] == '@') {
      if(current_type==HELP_TEXT) {
	current_type = -1;
	for(i=2; help_type_names[i]; i++) {
	  sprintf(expect, "START_%sS", help_type_names[i]);
	  if(strcmp(expect,buf+1)==0) {
	    current_type = i;
	    break;
	  }
	}
	if (current_type==-1) {
	  freelog(LOG_NORMAL, "bad help category \"%s\"", buf+1);
	  current_type = HELP_TEXT;
	} else {
	  genlist_init(&category_nodes);
	  for(i=0; i<MAX_LAST; i++) {
	    seen[i] = (booted?0:1); /* on initial boot data tables are empty */
	  }
	  freelog(LOG_DEBUG, "Help category %s",
		  help_type_names[current_type]);
	}
      } else {
	sprintf(expect, "END_%sS", help_type_names[current_type]);
	if(strcmp(expect,buf+1)!=0) {
	  freelog(LOG_FATAL, "bad end to help category \"%s\"", buf+1);
	  exit(1);
	}
	/* add defaults for those not seen: */
	if(current_type==HELP_UNIT) {
	  for(i=0; i<U_LAST; i++) {
	    if(!seen[i] && unit_type_exists(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", unit_name(i));
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_TECH) {
	  for(i=1; i<A_LAST; i++) {                 /* skip A_NONE */
	    if(!seen[i] && tech_exists(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", advances[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_IMPROVEMENT) {
	  for(i=0; i<B_LAST; i++) {
	    if(!seen[i] && improvement_exists(i) && !is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", improvement_types[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_WONDER) {
	  for(i=0; i<B_LAST; i++) {
	    if(!seen[i] && improvement_exists(i) && is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", improvement_types[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else {
	  freelog(LOG_FATAL, "Bad current_type %d", current_type);
	  exit(1);
	}
	genlist_sort(&category_nodes, help_item_compar);
	help_list_iterate(category_nodes, ptmp) {
	  genlist_insert(&help_nodes, ptmp, -1);
	}
	help_list_iterate_end;
	genlist_unlink_all(&category_nodes);
	current_type = HELP_TEXT;
      }
      continue;
    }
    
    p=strchr(buf, '#');
    if(!p) {
      break;
    }
    pname = p;
    while(*(++pname)==' ')
      ;

    /* i==-1 is text; filter_this==1 is to be left out, but we have to
     * read in the help text so we're at the right place
     */
    i = -1;
    filter_this = 0;
    switch(current_type) {
    case HELP_UNIT:
      i = find_unit_type_by_name(pname);
      if(!unit_type_exists(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering unit type %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_TECH:
      i = find_tech_by_name(pname);
      if(!tech_exists(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering tech %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_IMPROVEMENT:
      i = find_improvement_by_name(pname);
      if(!improvement_exists(i) || is_wonder(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering city improvement %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_WONDER:
      i = find_improvement_by_name(pname);
      if(!improvement_exists(i) || !is_wonder(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering wonder %s from help", pname);
	filter_this = 1;
      }
      break;
    default:
      /* nothing */
      {} /* placate Solaris cc/xmkmf/makedepend */
    }
    if(i>=0) {
      seen[i] = 1;
    }

    if(!filter_this) {
      pitem = new_help_item(current_type);
      pitem->topic = mystrdup(p+1);
    }

    long_buffer[0]='\0';
    while(1) {
      fgets(buf, 512, fs);
      buf[strlen(buf)-1]='\0';
      if(!strncmp(buf, "%%", 2))
	continue;
      if(!strcmp(buf, "---"))
	break;
      if(!filter_this) {
	strcat(long_buffer, buf);
	strcat(long_buffer, "\n");
      }
    } 

    if(!filter_this) {
      pitem->text=mystrdup(long_buffer);
      if(current_type == HELP_TEXT) {
	genlist_insert(&help_nodes, pitem, -1);
      } else {
	genlist_insert(&category_nodes, pitem, -1);
      }
    }
  }

  if(current_type != HELP_TEXT) {
    freelog(LOG_FATAL, "Didn't finish help category %s",
	 help_type_names[current_type]);
    exit(1);
  }
  
  fclose(fs);
  booted = 1;
}

/****************************************************************
...
*****************************************************************/
void popup_help_dialog_typed(char *item, enum help_page_type htype)
{
/*
  Position x, y;
  Dimension width, height;
*/
  if(!help_dialog_shell) {
    create_help_dialog();
/*
    XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
    XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
		      &x, &y);
    XtVaSetValues(help_dialog_shell, XtNx, x, XtNy, y, NULL);
*/
  }
  gtk_widget_show(help_dialog_shell);

  select_help_item_string(item, htype);
}


/****************************************************************
...
*****************************************************************/
void popup_help_dialog_string(char *item)
{
  popup_help_dialog_typed(item, HELP_ANY);
}



/**************************************************************************
...
**************************************************************************/
void create_tech_tree(GtkCTree *ctree, int tech, int levels,
				GtkCTreeNode *parent)
{
  GtkCTreeNode *l;
  gchar        *text[1];
  int	        bg;
  char          label [MAX_LENGTH_NAME+3];
  gboolean      leaf;
  
  text[0] = label;

  if ( advances[tech].req[0] == A_LAST && advances[tech].req[1] == A_LAST )
  {
    strcpy(label, "Removed");
    bg = COLOR_STD_RED;
    l = gtk_ctree_insert_node(ctree, parent, NULL, text, 10,
				NULL, NULL, NULL, NULL, TRUE, TRUE);
    gtk_ctree_node_set_background(ctree, l, colors_standard[bg]);
    return;
  }

  switch (get_invention(game.player_ptr, tech))
  {
  case TECH_UNKNOWN:	      bg = COLOR_STD_RED;	      break;
  case TECH_KNOWN:	      bg = COLOR_STD_GROUND;	      break;
  case TECH_REACHABLE:        bg = COLOR_STD_YELLOW;	      break;
  default:		      bg = COLOR_STD_WHITE;	      break;
  }

  sprintf(label, "%s:%d", advances[tech].name,
        		   tech_goal_turns(game.player_ptr, tech));

  leaf = (advances[tech].req[0] == A_NONE && advances[tech].req[1] == A_NONE);

  l = gtk_ctree_insert_node(ctree, parent, NULL, text, 10,
				 NULL, NULL, NULL, NULL, leaf, TRUE);
  gtk_ctree_node_set_background(ctree, l, colors_standard[bg]);

  if ( --levels <= 0 )
      return;

  if ( advances[tech].req[0] != A_NONE )
    create_tech_tree(ctree, advances[tech].req[0], levels, l);
  if ( advances[tech].req[1] != A_NONE )
    create_tech_tree(ctree, advances[tech].req[1], levels, l);
  return;
}

/**************************************************************************
...
**************************************************************************/
void help_hyperlink_callback(GtkWidget *w, enum help_page_type type)
{
  char *s;

  s=GTK_LABEL(GTK_BUTTON(w)->child)->label;

  if (strcmp (s, "(Never)") && strcmp (s, "None"))
    select_help_item_string(s, type);
}

GtkWidget *help_hyperlink_new(GtkWidget *label, enum help_page_type type)
{
  GtkWidget *button;

  button = gtk_button_new();
  gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
  gtk_container_add(GTK_CONTAINER(button), label);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
  	  GTK_SIGNAL_FUNC(help_hyperlink_callback), (gpointer)type);

  return button;
}

GtkWidget *help_slink_new(gchar *txt, enum help_page_type type)
{
  GtkWidget *button;

  button = gtk_button_new_with_label(txt);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
  	  GTK_SIGNAL_FUNC(help_hyperlink_callback), (gpointer)type);

  return button;
}

void selected_topic(GtkCList *clist, gint row, gint column,
							GdkEventButton *event)
{
  struct help_item *p = NULL;

  help_list_iterate(help_nodes, pitem) 
    if ((row--)==0)
    {
      p=pitem;
      break;
    }
  help_list_iterate_end;

  if (!p)
      return;

  help_update_dialog(p);
  return;
}

/**************************************************************************
...
**************************************************************************/
void create_help_dialog(void)
{
  GtkWidget *hbox;
  GtkWidget *button;
  GtkStyle  *style;
  char      *row       [1];
  int	     i, j;


  help_dialog_shell = gtk_dialog_new();

  gtk_signal_connect( GTK_OBJECT( help_dialog_shell ), "destroy",
  	  GTK_SIGNAL_FUNC( gtk_widget_destroyed ), &help_dialog_shell );

  gtk_window_set_title( GTK_WINDOW( help_dialog_shell ), "FreeCiv Help Browser" );
  gtk_container_border_width( GTK_CONTAINER( help_dialog_shell ), 5 );

  hbox = gtk_hbox_new( FALSE, 5 );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(help_dialog_shell)->vbox ),
  	  hbox, TRUE, TRUE, 0 );

  help_clist = gtk_clist_new(1);
  gtk_clist_set_column_width(GTK_CLIST(help_clist), 0, GTK_CLIST(help_clist)->clist_window_width);
  help_clist_scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(help_clist_scrolled),
  			  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(help_clist_scrolled, 180, 350);
  gtk_container_add(GTK_CONTAINER(help_clist_scrolled), help_clist);
  gtk_box_pack_start( GTK_BOX(hbox), help_clist_scrolled, TRUE, TRUE, 0 );

  gtk_signal_connect(GTK_OBJECT(help_clist), "select_row",
  		      GTK_SIGNAL_FUNC(selected_topic), NULL);

  help_list_iterate(help_nodes, pitem) 
    row[0]=pitem->topic;
    i = gtk_clist_append (GTK_CLIST (help_clist), row);
  help_list_iterate_end;

  help_frame = gtk_frame_new( "" );
  gtk_box_pack_start( GTK_BOX( hbox ), help_frame, TRUE, TRUE, 0 );
  gtk_widget_set_usize( help_frame, 520, 350 );

  help_box = gtk_vbox_new( FALSE, 5 );
  gtk_container_add( GTK_CONTAINER( help_frame ), help_box );


  unit_tile = gtk_pixmap_new( create_overlay_unit( 0 ), NULL );
  gtk_box_pack_start( GTK_BOX( help_box ), unit_tile, FALSE, FALSE, 0 );



  style = gtk_style_new();
  gdk_font_unref (style->font);
  style->font = gdk_font_load( HELP_UNIT_FONT );
  gdk_font_ref (style->font);
  style->fg[GTK_STATE_NORMAL] = *colors_standard[COLOR_STD_RED];


  help_itable = gtk_table_new(6, 1, FALSE);
  gtk_box_pack_start(GTK_BOX(help_box), help_itable, FALSE, FALSE, 0);

  for (i=0; i<6; i++) {
    help_ilabel[i] = gtk_label_new(help_ilabel_name[i]);
    gtk_widget_set_style(help_ilabel[i], style);

    if (i==5) {
      button = help_hyperlink_new(help_ilabel[i], HELP_TECH);
      gtk_table_attach_defaults(GTK_TABLE(help_itable), button, i, i+1, 0, 1);
    }
    else
    gtk_table_attach_defaults(GTK_TABLE(help_itable),
					help_ilabel[i], i, i+1, 0, 1);
  }

  help_wtable = gtk_table_new(6, 1, FALSE);
  gtk_box_pack_start(GTK_BOX(help_box), help_wtable, FALSE, FALSE, 0);

  for (i=0; i<6; i++) {
    help_wlabel[i] = gtk_label_new(help_wlabel_name[i]);
    gtk_widget_set_style(help_wlabel[i], style);

    if (i==3 || i==5) {
      button = help_hyperlink_new(help_wlabel[i], HELP_TECH);
      gtk_table_attach_defaults(GTK_TABLE(help_wtable), button, i, i+1, 0, 1);
    }
    else
    gtk_table_attach_defaults(GTK_TABLE(help_wtable),
					help_wlabel[i], i, i+1, 0, 1);
  }


  help_utable = gtk_table_new(5, 4, FALSE);
  gtk_box_pack_start(GTK_BOX(help_box), help_utable, FALSE, FALSE, 0);

  for (i=0; i<5; i++)
    for (j=0; j<4; j++)
    {
      help_ulabel[j][i] = gtk_label_new(help_ulabel_name[j][i]);
      gtk_widget_set_style(help_ulabel[j][i], style);

      if (j==3 && (i==1 || i==4))
      {
	if (i==1)
          button = help_hyperlink_new(help_ulabel[j][i], HELP_TECH);
	else
          button = help_hyperlink_new(help_ulabel[j][i], HELP_UNIT);

        gtk_table_attach_defaults(GTK_TABLE(help_utable),
					    button, i, i+1, j, j+1);
      }
      else
      gtk_table_attach_defaults(GTK_TABLE(help_utable),
					  help_ulabel[j][i], i, i+1, j, j+1);
    }

  help_vbox=gtk_vbox_new(FALSE, 1);
  gtk_box_pack_start( GTK_BOX(help_box), help_vbox, FALSE, FALSE, 0 );

  help_text_scrolled = gtk_scrolled_window_new( NULL, NULL );
  help_text = gtk_text_new( NULL, NULL );
  gtk_container_add(GTK_CONTAINER (help_text_scrolled), help_text);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(help_text_scrolled),
  			  GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_text_set_editable( GTK_TEXT( help_text ), FALSE );
  gtk_box_pack_start( GTK_BOX( help_box ), help_text_scrolled, TRUE, TRUE, 0 );

  style = gtk_style_new();
  gdk_font_unref (style->font);
  style->font = gdk_font_load( HELP_TEXT_FONT );
  gdk_font_ref (style->font);
  gtk_widget_set_style( help_text, style );
  gtk_widget_realize( help_text );

  help_tree = gtk_ctree_new(1,0);
  gtk_clist_set_column_width(GTK_CLIST(help_tree), 0, GTK_CLIST(help_tree)->clist_window_width);
  gtk_ctree_set_line_style(GTK_CTREE(help_tree),GTK_CTREE_LINES_DOTTED);
  gtk_clist_set_column_justification(GTK_CLIST(help_tree),0,1);
  gtk_ctree_set_expander_style(GTK_CTREE(help_tree),
  				  GTK_CTREE_EXPANDER_CIRCULAR);

  help_tree_scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(help_tree_scrolled), help_tree);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(help_tree_scrolled),
  			  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );

  gtk_box_pack_start( GTK_BOX( help_box ), help_tree_scrolled, TRUE, TRUE, 0 );

  button = gtk_button_new_with_label( "Close" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(help_dialog_shell)->action_area ),
      button, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( button );

  gtk_signal_connect_object( GTK_OBJECT( button ), "clicked",
  	  GTK_SIGNAL_FUNC( gtk_widget_destroy ), GTK_OBJECT( help_dialog_shell ) );

  gtk_widget_show_all( GTK_DIALOG(help_dialog_shell)->vbox );
  gtk_widget_hide_all( help_box );

  create_help_page(HELP_TEXT);
  return;
}



/**************************************************************************
...
**************************************************************************/
void create_help_page(enum help_page_type type)
{
}



static void help_cathedral_techs_append(char *buf)
{
  int t;
  
  t=game.rtech.cathedral_minus;
  if(tech_exists(t)) 
    sprintf(buf+strlen(buf),
	   "The discovery of %s will reduce this by 1.\n",
	   advances[t].name);
  t=game.rtech.cathedral_plus;
  if(tech_exists(t)) 
    sprintf(buf+strlen(buf),
	   "The discovery of %s will increase this by 1.\n",
	   advances[t].name);
}

/**************************************************************************
...
**************************************************************************/
void help_update_improvement(struct help_item *pitem, char *title, int which)
{
  char *buf = &long_buffer[0];
  int t;
  
  create_help_page(HELP_IMPROVEMENT);
  
  if (which<B_LAST) {
    struct improvement_type *imp = &improvement_types[which];
    sprintf(buf, "%d", imp->build_cost);
    gtk_set_label(help_ilabel[1], buf);
    sprintf(buf, "%d", imp->shield_upkeep);
    gtk_set_label(help_ilabel[3], buf);
    if (imp->tech_requirement == A_LAST) {
      gtk_set_label(help_ilabel[5], "(Never)");
    } else {
      gtk_set_label(help_ilabel[5], advances[imp->tech_requirement].name);
    }
/*    create_tech_tree(help_improvement_tree, 0, imp->tech_requirement, 3);*/
  }
  else {
    gtk_set_label(help_ilabel[1], "0");
    gtk_set_label(help_ilabel[3], "0");
    gtk_set_label(help_ilabel[5], "(Never)");
/*    create_tech_tree(help_improvement_tree, 0, A_LAST, 3);*/
  }
  gtk_widget_show_all(help_itable);

  buf[0] = '\0';
  if(which==B_AQUEDUCT) {
    sprintf(buf+strlen(buf), "Allows a city to grow larger than size %d.",
	   game.aqueduct_size);
    if(improvement_exists(B_SEWER)) {
      char *s = improvement_types[B_SEWER].name;
      sprintf(buf+strlen(buf), "  (A%s %s is also\n"
	     "required for a city to grow larger than size %d.)",
	     n_if_vowel(*s), s, game.sewer_size);
    }
    strcat(buf,"\n");
  }
  if(which==B_SEWER) {
    sprintf(buf+strlen(buf), "Allows a city to grow larger than size %d.\n",
	   game.sewer_size);
  }
  strcat(buf, pitem->text);
  if(which==B_CATHEDRAL) {
    help_cathedral_techs_append(buf);
  }
  if(which==B_COLOSSEUM) {
    t=game.rtech.colosseum_plus;
    if(tech_exists(t)) {
      int n = strlen(buf);
      if(n && buf[n-1] == '\n') buf[n-1] = ' ';
      sprintf(buf+n, "The discovery of %s will increase this by 1.\n",
	     advances[t].name);
    }
  }
  if(which==B_BARRACKS
     && tech_exists(improvement_types[B_BARRACKS].obsolete_by)
     && tech_exists(improvement_types[B_BARRACKS2].obsolete_by)) {
    sprintf(buf+strlen(buf),
	   "\nNote that discovering %s or %s will obsolete\n"
	   "any existing %s.\n",
	   advances[improvement_types[B_BARRACKS].obsolete_by].name,
	   advances[improvement_types[B_BARRACKS2].obsolete_by].name,
	   improvement_types[B_BARRACKS].name);
  }
  if(which==B_BARRACKS2
     && tech_exists(improvement_types[B_BARRACKS2].obsolete_by)) {
    sprintf(buf+strlen(buf),
	   "\nThe discovery of %s will make %s obsolete.\n",
	   advances[improvement_types[B_BARRACKS2].obsolete_by].name,
	   improvement_types[B_BARRACKS2].name);
  }
  gtk_text_freeze(GTK_TEXT(help_text));
  gtk_text_insert(GTK_TEXT(help_text), NULL, NULL, NULL, buf, -1);
  gtk_text_thaw(GTK_TEXT(help_text));
  gtk_widget_show(help_text);
  gtk_widget_show(help_text_scrolled);
}
  
/**************************************************************************
...
**************************************************************************/
void help_update_wonder(struct help_item *pitem, char *title, int which)
{
  char *buf = &long_buffer[0];
  
  create_help_page(HELP_WONDER);

  if (which<B_LAST) {
    struct improvement_type *imp = &improvement_types[which];
    sprintf(buf, "%d", imp->build_cost);
    gtk_set_label(help_wlabel[1], buf);
    if (imp->tech_requirement == A_LAST) {
      gtk_set_label(help_wlabel[3], "(Never)");
    } else {
      gtk_set_label(help_wlabel[3], advances[imp->tech_requirement].name);
    }
    gtk_set_label(help_wlabel[5], advances[imp->obsolete_by].name);
/*    create_tech_tree(help_improvement_tree, 0, imp->tech_requirement, 3);*/
  }
  else {
    /* can't find wonder */
    gtk_set_label(help_wlabel[1], "0");
    gtk_set_label(help_wlabel[3], "(Never)");
    gtk_set_label(help_wlabel[5], "None");
/*    create_tech_tree(help_improvement_tree, 0, A_LAST, 3); */
  }
  gtk_widget_show_all(help_wtable);

  buf[0] = '\0';
  if(which==B_MANHATTEN && num_role_units(F_NUCLEAR)>0) {
    int u, t;
    u = get_role_unit(F_NUCLEAR, 0);
    assert(u<U_LAST);
    t = get_unit_type(u)->tech_requirement;
    assert(t<A_LAST);
    sprintf(buf+strlen(buf),
	   "Allows all players with knowledge of %s to build %s units.\n",
	   advances[t].name, get_unit_type(u)->name);
  }
  strcat(buf, pitem->text);
  if(which==B_MICHELANGELO) {
    help_cathedral_techs_append(buf);
  }
  gtk_text_freeze(GTK_TEXT(help_text));
  gtk_text_insert(GTK_TEXT(help_text), NULL, NULL, NULL, buf, -1);
  gtk_text_thaw(GTK_TEXT(help_text));
  gtk_widget_show(help_text);
  gtk_widget_show(help_text_scrolled);
}

/**************************************************************************
...
**************************************************************************/
void help_update_unit_type(struct help_item *pitem, char *title, int i)
{
  char *buf = &long_buffer[0];
  
  create_help_page(HELP_UNIT);

  if (i<U_LAST) {
    struct unit_type *utype = get_unit_type(i);
    sprintf(buf, "%d", utype->build_cost);
    gtk_set_label(help_ulabel[0][1], buf);
    sprintf(buf, "%d", utype->attack_strength);
    gtk_set_label(help_ulabel[0][4], buf);
    sprintf(buf, "%d", utype->defense_strength);
    gtk_set_label(help_ulabel[1][1], buf);
    sprintf(buf, "%d", utype->move_rate/3);
    gtk_set_label(help_ulabel[1][4], buf);
    sprintf(buf, "%d", utype->firepower);
    gtk_set_label(help_ulabel[2][1], buf);
    sprintf(buf, "%d", utype->hp);
    gtk_set_label(help_ulabel[2][4], buf);
    sprintf(buf, "%d", utype->vision_range);
/*    xaw_set_label(help_unit_visrange_data, buf); -- FIXME */
    if(utype->tech_requirement==A_LAST) {
      gtk_set_label(help_ulabel[3][1], "(Never)");
    } else {
      gtk_set_label(help_ulabel[3][1], advances[utype->tech_requirement].name);
    }
/*    create_tech_tree(help_improvement_tree, 0, utype->tech_requirement, 3);*/
    if(utype->obsoleted_by==-1) {
      gtk_set_label(help_ulabel[3][4], "None");
    } else {
      gtk_set_label(help_ulabel[3][4], get_unit_type(utype->obsoleted_by)->name);
    }
    /* add text for transport_capacity, fuel, and flags: */
    buf[0] = '\0';
    sprintf(buf+strlen(buf), "* Vision range of %d square%s.\n",
	utype->vision_range, (utype->vision_range==1)?"":"s"); /* -- REMOVE ME */
    if (utype->transport_capacity>0) {
      if (unit_flag(i, F_SUBMARINE)) {
	sprintf(buf+strlen(buf), "* Can carry and refuel %d missile units.\n",
		utype->transport_capacity);
      } else if (unit_flag(i, F_CARRIER)) {
	sprintf(buf+strlen(buf), "* Can carry and refuel %d air units.\n",
		utype->transport_capacity);
      } else {
	sprintf(buf+strlen(buf), "* Can carry %d ground units across water.\n",
		utype->transport_capacity);
      }
    }
    if (unit_flag(i, F_CARAVAN)) {
      sprintf(buf+strlen(buf), "* Can establish trade routes and help build wonders.\n");
    }
    if (unit_flag(i, F_SETTLERS)) {
      sprintf(buf+strlen(buf), "* Can perform settler actions.\n");
    }
    if (unit_flag(i, F_DIPLOMAT)) {
      if (unit_flag(i, F_SPY)) 
	sprintf(buf+strlen(buf), "* Can perform diplomatic actions, plus special spy abilities.\n");
      else 
	sprintf(buf+strlen(buf), "* Can perform diplomatic actions.\n");
    }
    if (unit_flag(i, F_FIGHTER)) {
      sprintf(buf+strlen(buf), "* Can attack enemy air units.\n");
    }
    if (unit_flag(i, F_MARINES)) {
      sprintf(buf+strlen(buf), "* Can attack from aboard sea units: against enemy cities and\n  onto land squares.");
    }
    if (unit_flag(i, F_PIKEMEN)) {
      sprintf(buf+strlen(buf), "* Gets double defense against units specified as 'mounted'.\n");
    }
    if (unit_flag(i, F_HORSE)) {
      sprintf(buf+strlen(buf), "* Counts as 'mounted' against certain defenders.\n");
    }
    if (unit_flag(i, F_MISSILE)) {
      sprintf(buf+strlen(buf), "* A missile unit: gets used up in making an attack.\n");
    } else if(unit_flag(i, F_ONEATTACK)) {
      sprintf(buf+strlen(buf), "* Making an attack ends this unit's turn.\n");
    }
    if (unit_flag(i, F_NUCLEAR)) {
      sprintf(buf+strlen(buf), "* This unit's attack causes a nuclear explosion!\n");
    }
    if (unit_flag(i, F_IGWALL)) {
      sprintf(buf+strlen(buf), "* Ignores the effects of city walls.\n");
    }
    if (unit_flag(i, F_AEGIS)) {
      sprintf(buf+strlen(buf), "* Gets quintuple defence against missiles and aircraft.\n");
    }
    if (unit_flag(i, F_IGTER)) {
      sprintf(buf+strlen(buf), "* Ignores terrain effects (treats all squares as roads).\n");
    }
    if (unit_flag(i, F_IGZOC)) {
      sprintf(buf+strlen(buf), "* Ignores zones of control.\n");
    }
    if (unit_flag(i, F_NONMIL)) {
      sprintf(buf+strlen(buf), "* A non-military unit (no shield upkeep).\n");
    }
    if (unit_flag(i, F_TRIREME)) {
      sprintf(buf+strlen(buf), "* Must end turn in a city or next to land, or has a 50%% risk of\n  being lost at sea.");
    }
    if (utype->fuel>0) {
      sprintf(buf+strlen(buf), "* Must end ");
      if (utype->fuel==2) {
	sprintf(buf+strlen(buf), "second ");
      } else if (utype->fuel==3) {
	sprintf(buf+strlen(buf), "third ");
      } else if (utype->fuel>=4) {
	sprintf(buf+strlen(buf), "%dth ", utype->fuel);
      }
      sprintf(buf+strlen(buf), "turn in a city, or on a Carrier");
      if (unit_flag(i, F_MISSILE) &&
	  num_role_units(F_SUBMARINE)>0 &&
	  get_unit_type(get_role_unit(F_SUBMARINE,0))->transport_capacity) {
	sprintf(buf+strlen(buf), " or Submarine");
      }
      sprintf(buf+strlen(buf), ",\n  or will run out of fuel and be lost.\n");
    }
    if (strlen(buf)) {
      sprintf(buf+strlen(buf), "\n");
    } 
    strcpy(buf+strlen(buf), pitem->text);

    gtk_text_freeze(GTK_TEXT(help_text));
    gtk_text_insert(GTK_TEXT(help_text), NULL, NULL, NULL, buf, -1);
    gtk_text_thaw(GTK_TEXT(help_text));
    gtk_widget_show(help_text);
    gtk_widget_show(help_text_scrolled);

    gtk_pixmap_set(GTK_PIXMAP(unit_tile), create_overlay_unit(i), NULL);
    gtk_widget_show(unit_tile);
  }
  else {
    gtk_set_label(help_ulabel[0][1], "0");
    gtk_set_label(help_ulabel[0][4], "0");
    gtk_set_label(help_ulabel[1][1], "0");
    gtk_set_label(help_ulabel[1][4], "0");
    gtk_set_label(help_ulabel[2][1], "0");
    gtk_set_label(help_ulabel[2][4], "0");
/*    xaw_set_label(help_unit_visrange_data, "0 "); -- FIXME */

    gtk_set_label(help_ulabel[3][1], "(Never)");
/*    create_tech_tree(help_improvement_tree, 0, A_LAST, 3);*/
    gtk_set_label(help_ulabel[3][4], "None");

    gtk_text_freeze(GTK_TEXT(help_text));
    gtk_text_insert(GTK_TEXT(help_text), NULL, NULL, NULL, pitem->text, -1);
    gtk_text_thaw(GTK_TEXT(help_text));
    gtk_widget_show(help_text);
    gtk_widget_show(help_text_scrolled);
  }
  gtk_widget_show_all(help_utable);
}

/**************************************************************************
...
**************************************************************************/
void help_update_tech(struct help_item *pitem, char *title, int i)
{
  int j;
  GtkWidget *w, *hbox;
  char buf[4096];

  create_help_page(HELP_TECH);

  if (i<A_LAST&&i!=A_NONE) {
    gtk_container_foreach(GTK_CONTAINER(help_vbox), (GtkCallback)gtk_widget_destroy, NULL);

    gtk_clist_freeze(GTK_CLIST(help_tree));
    gtk_clist_clear(GTK_CLIST(help_tree));
    create_tech_tree(GTK_CTREE(help_tree), i, 3, NULL);
    gtk_clist_thaw(GTK_CLIST(help_tree));
    gtk_widget_show_all(help_tree_scrolled);

    strcpy(buf, pitem->text);

    if(game.rtech.get_bonus_tech == i) {
      sprintf(buf+strlen(buf),
	     "The first player to research %s gets an immediate advance.\n",
	     advances[i].name);
    }
    if(game.rtech.boat_fast == i) 
      sprintf(buf+strlen(buf), "Gives sea units one extra move.\n");
    if(game.rtech.cathedral_plus == i) 
      sprintf(buf+strlen(buf), "Improves the effect of Cathedrals.\n");
    if(game.rtech.cathedral_minus == i) 
      sprintf(buf+strlen(buf), "Reduces the effect of Cathedrals.\n");
    if(game.rtech.colosseum_plus == i) 
      sprintf(buf+strlen(buf), "Improves the effect of Colosseums.\n");

    w = gtk_label_new(buf);
    gtk_misc_set_alignment(GTK_MISC(w), 0.0, 0.0);
    gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_LEFT);
    gtk_container_add(GTK_CONTAINER(help_vbox), w);

    for(j=0; j<B_LAST; ++j) {
      if(i!=improvement_types[j].tech_requirement) continue;
      hbox = gtk_hbox_new(FALSE, 0);
      gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
      w = gtk_label_new("Allows ");
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = help_slink_new(improvement_types[j].name,
			is_wonder(j)?HELP_WONDER:HELP_IMPROVEMENT);
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = gtk_label_new(".");
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
    }
    for(j=0; j<U_LAST; ++j) {
      if(i!=get_unit_type(j)->tech_requirement) continue;
      hbox = gtk_hbox_new(FALSE, 0);
      gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
      w = gtk_label_new("Allows ");
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = help_slink_new(get_unit_type(j)->name, HELP_UNIT);
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = gtk_label_new(".");
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
    }

    for(j=0; j<A_LAST; ++j) {
      if(i==advances[j].req[0]) {
	if(advances[j].req[1]==A_NONE) {
          hbox = gtk_hbox_new(FALSE, 0);
          gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
          w = gtk_label_new("Allows ");
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = help_slink_new(advances[j].name, HELP_TECH);
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = gtk_label_new(".");
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
	}
	else {
          hbox = gtk_hbox_new(FALSE, 0);
          gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
          w = gtk_label_new("Allows ");
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = help_slink_new(advances[j].name, HELP_TECH);
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = gtk_label_new(" (with ");
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = help_slink_new(advances[advances[j].req[1]].name, HELP_TECH);
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = gtk_label_new(").");
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
	}
      }
      if(i==advances[j].req[1]) {
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
        w = gtk_label_new("Allows ");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = help_slink_new(advances[j].name, HELP_TECH);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = gtk_label_new(" (with ");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = help_slink_new(advances[advances[j].req[0]].name, HELP_TECH);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = gtk_label_new(").");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      }
    }
    gtk_widget_show_all(help_vbox);
  }
}

/**************************************************************************
...
**************************************************************************/
void help_update_dialog(struct help_item *pitem)
{
  int i;
  char *top;

  /* figure out what kind of item is required for pitem ingo */

  for(top=pitem->topic; *top==' '; ++top);

  gtk_widget_hide_all(help_box);
  gtk_text_freeze(GTK_TEXT(help_text));
  gtk_editable_delete_text(GTK_EDITABLE(help_text), 0, -1);
  gtk_text_thaw(GTK_TEXT(help_text));

  switch(pitem->type) {
  case HELP_IMPROVEMENT:
    i = find_improvement_by_name(top);
    if(i!=B_LAST && is_wonder(i)) i = B_LAST;
    help_update_improvement(pitem, top, i);
    break;
  case HELP_WONDER:
    i = find_improvement_by_name(top);
    if(i!=B_LAST && !is_wonder(i)) i = B_LAST;
    help_update_wonder(pitem, top, i);
    break;
  case HELP_UNIT:
    help_update_unit_type(pitem, top, find_unit_type_by_name(top));
    break;
  case HELP_TECH:
    help_update_tech(pitem, top, find_tech_by_name(top));
    break;
  case HELP_TEXT:
  default:
    /* it was a pure text item */ 
    create_help_page(HELP_TEXT);

    gtk_text_freeze(GTK_TEXT(help_text));
    gtk_text_insert(GTK_TEXT(help_text), NULL, NULL, NULL, pitem->text, -1);
    gtk_text_thaw(GTK_TEXT(help_text));
    gtk_widget_show(help_text);
    gtk_widget_show(help_text_scrolled);
    break;
  }
  set_title_topic(pitem->topic);

  gtk_widget_show(help_box);
}






/**************************************************************************
...
**************************************************************************/
void select_help_item(int item)
{
  gtk_clist_moveto(GTK_CLIST (help_clist), item, 0, 0, 0);
  gtk_clist_select_row(GTK_CLIST (help_clist), item, 0);
}

void select_help_item_string(char *item, enum help_page_type htype)
{
  int idx;
  struct help_item *pitem = NULL;
  static struct help_item vitem; /* v = virtual */
  static char vtopic[128];
  static char vtext[256];

  idx = 0;
  help_list_iterate(help_nodes, ptmp) {
    char *p=ptmp->topic;
    while(*p==' ')
      ++p;
    if(strcmp(item, p)==0 && (htype==HELP_ANY || htype==ptmp->type)) {
      pitem = ptmp;
      break;
    }
    ++idx;
  }
  help_list_iterate_end;
  
  if(!pitem) {
    idx=0;
    vitem.topic = vtopic;
    strncpy(vtopic, item, sizeof(vtopic));
    vitem.text = vtext;
    if(htype==HELP_ANY || htype==HELP_TEXT) {
      sprintf(vtext, "Sorry, no help topic for %s.\n", vitem.topic);
      vitem.type = HELP_TEXT;
    } else {
      sprintf(vtext, "Sorry, no help topic for %s.\n"
	      "This page was auto-generated.\n\n",
	      vitem.topic);
      vitem.type = htype;
    }
    pitem = &vitem;
  }

  select_help_item(idx);
  help_update_dialog(pitem);
}
