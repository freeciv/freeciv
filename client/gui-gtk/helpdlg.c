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
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "mem.h"
#include "shared.h"
#include "tech.h"
#include "unit.h"
#include "map.h"
#include "version.h"

#include "climisc.h"
#include "colors.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "helpdata.h"

#include "helpdlg.h"


#define	HELP_TEXT_FONT		"-*-fixed-medium-r-*-*-14-*-*-*-*-*-iso8859-*"


extern GtkWidget *toplevel;

extern char long_buffer[64000];	      /* helpdata.c */

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
GtkWidget *		help_ttable;
GtkWidget *		help_tree;
GtkWidget *		help_tree_scrolled;
GtkWidget *		help_ilabel	[6];
GtkWidget *		help_wlabel	[6];
GtkWidget *		help_ulabel	[4][5];
GtkWidget *		help_tlabel	[4][5];

char *help_ilabel_name[6] =
{ N_("Cost:"), "", N_("Upkeep:"), "", N_("Requirement:"), "" };

char *help_wlabel_name[6] =
{ N_("Cost:"), "", N_("Requirement:"), "", N_("Obsolete by:"), "" };

char *help_ulabel_name[4][5] =
{
    { N_("Cost:"),		"", "",	N_("Attack:"),	"" },
    { N_("Defense:"),	"", "",	N_("Move:")	,	"" },
    { N_("FirePower:"),	"", "",	N_("Hitpoints:"),	"" },
    { N_("Requirement:"),	"", "",	N_("Obsolete by:"),	"" }
};

char *help_tlabel_name[4][5] =
{
    { N_("Move/Defense:"),	"", "",	N_("Food/Res/Trade:"),	"" },
    { N_("Sp1 F/R/T:"),	"", "",	N_("Sp2 F/R/T:"),		"" },
    { N_("Road Rslt/Time:"),"", "",	N_("Irrig. Rslt/Time:"),	"" },
    { N_("Mine Rslt/Time:"),"", "",	N_("Trans. Rslt/Time:"),	"" }
};


static void create_help_dialog(void);
static void help_update_dialog(const struct help_item *pitem);
static void create_help_page(enum help_page_type type);

static void select_help_item(int item);
static void select_help_item_string(const char *item,
				    enum help_page_type htype);

/****************************************************************
...
*****************************************************************/
static void set_title_topic(char *topic)
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
void popdown_help_dialog(void)
{
  if(help_dialog_shell) {
    gtk_widget_destroy(help_dialog_shell);
    help_dialog_shell=0;
  }
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
static void create_tech_tree(GtkCTree *ctree, int tech, int levels,
			     GtkCTreeNode *parent)
{
  GtkCTreeNode *l;
  gchar        *text[1];
  int	        bg;
  char          label [MAX_LEN_NAME+3];
  gboolean      leaf;
  
  text[0] = label;

  if ( advances[tech].req[0] == A_LAST && advances[tech].req[1] == A_LAST )
  {
    strcpy(label, _("Removed"));
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
static void help_hyperlink_callback(GtkWidget *w, enum help_page_type type)
{
  char *s;

  s=GTK_LABEL(GTK_BUTTON(w)->child)->label;

  if (strcmp (s, "(Never)") && strcmp (s, "None"))
    select_help_item_string(s, type);
}

static GtkWidget *help_hyperlink_new(GtkWidget *label, enum help_page_type type)
{
  GtkWidget *button;

  button = gtk_button_new();
  gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
  gtk_container_add(GTK_CONTAINER(button), label);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
  	  GTK_SIGNAL_FUNC(help_hyperlink_callback), (gpointer)type);

  return button;
}

static GtkWidget *help_slink_new(gchar *txt, enum help_page_type type)
{
  GtkWidget *button;

  button = gtk_button_new_with_label(txt);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
  	  GTK_SIGNAL_FUNC(help_hyperlink_callback), (gpointer)type);

  return button;
}

static void selected_topic(GtkCList *clist, gint row, gint column,
			   GdkEventButton *event)
{
  const struct help_item *p = NULL;

  help_items_iterate(pitem) {
    if ((row--)==0)
    {
      p=pitem;
      break;
    }
  }
  help_items_iterate_end;

  if (!p)
      return;

  help_update_dialog(p);
  return;
}

/**************************************************************************
...
**************************************************************************/
static void create_help_dialog(void)
{
  GtkWidget *hbox;
  GtkWidget *button;
  GtkStyle  *style;
  char      *row       [1];
  int	     i, j;


  help_dialog_shell = gtk_dialog_new();

  gtk_signal_connect( GTK_OBJECT( help_dialog_shell ), "destroy",
  	  GTK_SIGNAL_FUNC( gtk_widget_destroyed ), &help_dialog_shell );

  gtk_window_set_title( GTK_WINDOW( help_dialog_shell ), _("Freeciv Help Browser") );
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

  help_items_iterate(pitem) 
    row[0]=pitem->topic;
    i = gtk_clist_append (GTK_CLIST (help_clist), row);
  help_items_iterate_end;

  help_frame = gtk_frame_new( "" );
  gtk_box_pack_start( GTK_BOX( hbox ), help_frame, TRUE, TRUE, 0 );
  gtk_widget_set_usize( help_frame, 520, 350 );

  help_box = gtk_vbox_new( FALSE, 5 );
  gtk_container_add( GTK_CONTAINER( help_frame ), help_box );


  unit_tile = gtk_pixmap_new( create_overlay_unit( 0 ), NULL );
  gtk_box_pack_start( GTK_BOX( help_box ), unit_tile, FALSE, FALSE, 0 );



  style = gtk_style_new( );
  style->fg[GTK_STATE_NORMAL] = *colors_standard[COLOR_STD_RED];


  help_itable = gtk_table_new(6, 1, FALSE);
  gtk_box_pack_start(GTK_BOX(help_box), help_itable, FALSE, FALSE, 0);

  for (i=0; i<6; i++) {
    help_ilabel[i] = gtk_label_new(_(help_ilabel_name[i]));
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
    help_wlabel[i] = gtk_label_new(_(help_wlabel_name[i]));
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
      help_ulabel[j][i] = gtk_label_new(_(help_ulabel_name[j][i]));
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


  help_ttable = gtk_table_new(5, 4, FALSE);
  gtk_box_pack_start(GTK_BOX(help_box), help_ttable, FALSE, FALSE, 0);

  for (i=0; i<5; i++)
    for (j=0; j<4; j++)
    {
      help_tlabel[j][i] = gtk_label_new(_(help_tlabel_name[j][i]));
      gtk_widget_set_style(help_tlabel[j][i], style);

      gtk_table_attach_defaults(GTK_TABLE(help_ttable),
					  help_tlabel[j][i], i, i+1, j, j+1);
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

  button = gtk_button_new_with_label( _("Close") );
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
static void create_help_page(enum help_page_type type)
{
}

/**************************************************************************
...
**************************************************************************/
static void help_update_improvement(const struct help_item *pitem,
				    char *title, int which)
{
  char *buf = &long_buffer[0];
  
  create_help_page(HELP_IMPROVEMENT);
  
  if (which<B_LAST) {
    struct improvement_type *imp = &improvement_types[which];
    sprintf(buf, "%d", imp->build_cost);
    gtk_set_label(help_ilabel[1], buf);
    sprintf(buf, "%d", imp->shield_upkeep);
    gtk_set_label(help_ilabel[3], buf);
    if (imp->tech_requirement == A_LAST) {
      gtk_set_label(help_ilabel[5], _("(Never)"));
    } else {
      gtk_set_label(help_ilabel[5], advances[imp->tech_requirement].name);
    }
/*    create_tech_tree(help_improvement_tree, 0, imp->tech_requirement, 3);*/
  }
  else {
    gtk_set_label(help_ilabel[1], "0");
    gtk_set_label(help_ilabel[3], "0");
    gtk_set_label(help_ilabel[5], _("(Never)"));
/*    create_tech_tree(help_improvement_tree, 0, game.num_tech_types, 3);*/
  }
  gtk_widget_show_all(help_itable);

  helptext_improvement(buf, which, pitem->text);
  
  gtk_text_freeze(GTK_TEXT(help_text));
  gtk_text_insert(GTK_TEXT(help_text), NULL, NULL, NULL, buf, -1);
  gtk_text_thaw(GTK_TEXT(help_text));
  gtk_widget_show(help_text);
  gtk_widget_show(help_text_scrolled);
}
  
/**************************************************************************
...
**************************************************************************/
static void help_update_wonder(const struct help_item *pitem,
			       char *title, int which)
{
  char *buf = &long_buffer[0];
  
  create_help_page(HELP_WONDER);

  if (which<B_LAST) {
    struct improvement_type *imp = &improvement_types[which];
    sprintf(buf, "%d", imp->build_cost);
    gtk_set_label(help_wlabel[1], buf);
    if (imp->tech_requirement == A_LAST) {
      gtk_set_label(help_wlabel[3], _("(Never)"));
    } else {
      gtk_set_label(help_wlabel[3], advances[imp->tech_requirement].name);
    }
    gtk_set_label(help_wlabel[5], advances[imp->obsolete_by].name);
/*    create_tech_tree(help_improvement_tree, 0, imp->tech_requirement, 3);*/
  }
  else {
    /* can't find wonder */
    gtk_set_label(help_wlabel[1], "0");
    gtk_set_label(help_wlabel[3], _("(Never)"));
    gtk_set_label(help_wlabel[5], _("None"));
/*    create_tech_tree(help_improvement_tree, 0, game.num_tech_types, 3); */
  }
  gtk_widget_show_all(help_wtable);

  helptext_wonder(buf, which, pitem->text);
  gtk_text_freeze(GTK_TEXT(help_text));
  gtk_text_insert(GTK_TEXT(help_text), NULL, NULL, NULL, buf, -1);
  gtk_text_thaw(GTK_TEXT(help_text));
  gtk_widget_show(help_text);
  gtk_widget_show(help_text_scrolled);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_unit_type(const struct help_item *pitem,
				  char *title, int i)
{
  char *buf = &long_buffer[0];
  
  create_help_page(HELP_UNIT);

  if (i<game.num_unit_types) {
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
      gtk_set_label(help_ulabel[3][1], _("(Never)"));
    } else {
      gtk_set_label(help_ulabel[3][1], advances[utype->tech_requirement].name);
    }
/*    create_tech_tree(help_improvement_tree, 0, utype->tech_requirement, 3);*/
    if(utype->obsoleted_by==-1) {
      gtk_set_label(help_ulabel[3][4], _("None"));
    } else {
      gtk_set_label(help_ulabel[3][4], get_unit_type(utype->obsoleted_by)->name);
    }

    /* No separate Vision widget for Gtk+ client (yet?) */
    sprintf(buf, _("* Vision range of %d square%s.\n"),
	utype->vision_range, (utype->vision_range==1)?"":"s"); /* -- REMOVE ME */

    helptext_unit(buf+strlen(buf), i, pitem->text);

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

    gtk_set_label(help_ulabel[3][1], _("(Never)"));
/*    create_tech_tree(help_improvement_tree, 0, A_LAST, 3);*/
    gtk_set_label(help_ulabel[3][4], _("None"));

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
static void help_update_tech(const struct help_item *pitem, char *title, int i)
{
  int j;
  GtkWidget *w, *hbox;
  char *buf = &long_buffer[0];

  create_help_page(HELP_TECH);

  if (i<game.num_tech_types&&i!=A_NONE) {
    gtk_container_foreach(GTK_CONTAINER(help_vbox), (GtkCallback)gtk_widget_destroy, NULL);

    gtk_clist_freeze(GTK_CLIST(help_tree));
    gtk_clist_clear(GTK_CLIST(help_tree));
    create_tech_tree(GTK_CTREE(help_tree), i, 3, NULL);
    gtk_clist_thaw(GTK_CLIST(help_tree));
    gtk_widget_show_all(help_tree_scrolled);

    helptext_tech(buf, i, pitem->text);
    if (advances[i].helptext) {
      if (strlen(buf)) strcat(buf, "\n");
      sprintf(buf+strlen(buf), "%s\n", _(advances[i].helptext));
    }
    wordwrap_string(buf, 68);

    w = gtk_label_new(buf);
    gtk_misc_set_alignment(GTK_MISC(w), 0.0, 0.0);
    gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_LEFT);
    gtk_container_add(GTK_CONTAINER(help_vbox), w);

    for(j=0; j<B_LAST; ++j) {
      if(i!=improvement_types[j].tech_requirement) continue;
      hbox = gtk_hbox_new(FALSE, 0);
      gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
      w = gtk_label_new(_("Allows "));
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = help_slink_new(improvement_types[j].name,
			is_wonder(j)?HELP_WONDER:HELP_IMPROVEMENT);
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = gtk_label_new(".");
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
    }
    for(j=0; j<game.num_unit_types; ++j) {
      if(i!=get_unit_type(j)->tech_requirement) continue;
      hbox = gtk_hbox_new(FALSE, 0);
      gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
      w = gtk_label_new(_("Allows "));
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = help_slink_new(get_unit_type(j)->name, HELP_UNIT);
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = gtk_label_new(".");
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
    }

    for(j=0; j<game.num_tech_types; ++j) {
      if(i==advances[j].req[0]) {
	if(advances[j].req[1]==A_NONE) {
          hbox = gtk_hbox_new(FALSE, 0);
          gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
          w = gtk_label_new(_("Allows "));
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = help_slink_new(advances[j].name, HELP_TECH);
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = gtk_label_new(".");
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
	}
	else {
          hbox = gtk_hbox_new(FALSE, 0);
          gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
          w = gtk_label_new(_("Allows "));
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = help_slink_new(advances[j].name, HELP_TECH);
          gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
          w = gtk_label_new(_(" (with "));
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
        w = gtk_label_new(_("Allows "));
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = help_slink_new(advances[j].name, HELP_TECH);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = gtk_label_new(_(" (with "));
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
static void help_update_terrain(const struct help_item *pitem,
				char *title, int i)
{
  char buf[1024];

  create_help_page(HELP_TERRAIN);

  if (i < T_COUNT)
    {
      sprintf (buf, "%d/%d.%d",
	       tile_types[i].movement_cost,
	       (int)(tile_types[i].defense_bonus/10), tile_types[i].defense_bonus%10);
      gtk_set_label (help_tlabel[0][1], buf);

      sprintf (buf, "%d/%d/%d",
	       tile_types[i].food,
	       tile_types[i].shield,
	       tile_types[i].trade);
      gtk_set_label (help_tlabel[0][4], buf);

      if (*(tile_types[i].special_1_name))
	{
	  sprintf (buf, "%s F/R/T:",
		   tile_types[i].special_1_name);
	  gtk_set_label (help_tlabel[1][0], buf);
	  sprintf (buf, "%d/%d/%d",
		   tile_types[i].food_special_1,
		   tile_types[i].shield_special_1,
		   tile_types[i].trade_special_1);
	  gtk_set_label (help_tlabel[1][1], buf);
	} else {
	  gtk_set_label (help_tlabel[1][0], "");
	  gtk_set_label (help_tlabel[1][1], "");
	}

      if (*(tile_types[i].special_2_name))
	{
	  sprintf (buf, "%s F/R/T:",
		   tile_types[i].special_2_name);
	  gtk_set_label (help_tlabel[1][3], buf);
	  sprintf (buf, "%d/%d/%d",
		   tile_types[i].food_special_2,
		   tile_types[i].shield_special_2,
		   tile_types[i].trade_special_2);
	  gtk_set_label (help_tlabel[1][4], buf);
	} else {
	  gtk_set_label (help_tlabel[1][3], "");
	  gtk_set_label (help_tlabel[1][4], "");
	}

      if (tile_types[i].road_trade_incr > 0)
	{
	  sprintf (buf, _("+%d Trade / %d"),
		   tile_types[i].road_trade_incr,
		   tile_types[i].road_time);
	}
      else if (tile_types[i].road_time > 0)
	{
	  sprintf (buf, _("no extra / %d"),
		   tile_types[i].road_time);
	}
      else
	{
	  strcpy (buf, _("n/a"));
	}
      gtk_set_label (help_tlabel[2][1], buf);

      strcpy (buf, _("n/a"));
      if (tile_types[i].irrigation_result == i)
	{
	  if (tile_types[i].irrigation_food_incr > 0)
	    {
	      sprintf (buf, _("+%d Food / %d"),
		       tile_types[i].irrigation_food_incr,
		       tile_types[i].irrigation_time);
	    }
	}
      else if (tile_types[i].irrigation_result != T_LAST)
	{
	  sprintf (buf, "%s / %d",
		   tile_types[tile_types[i].irrigation_result].terrain_name,
		   tile_types[i].irrigation_time);
	}
      gtk_set_label (help_tlabel[2][4], buf);

      strcpy (buf, _("n/a"));
      if (tile_types[i].mining_result == i)
	{
	  if (tile_types[i].mining_shield_incr > 0)
	    {
	      sprintf (buf, _("+%d Res. / %d"),
		       tile_types[i].mining_shield_incr,
		       tile_types[i].mining_time);
	    }
	}
      else if (tile_types[i].mining_result != T_LAST)
	{
	  sprintf (buf, "%s / %d",
		   tile_types[tile_types[i].mining_result].terrain_name,
		   tile_types[i].mining_time);
	}
      gtk_set_label (help_tlabel[3][1], buf);

      if (tile_types[i].transform_result != T_LAST)
	{
	  sprintf (buf, "%s / %d",
		   tile_types[tile_types[i].transform_result].terrain_name,
		   tile_types[i].transform_time);
	} else {
	  strcpy (buf, "n/a");
	}
      gtk_set_label (help_tlabel[3][4], buf);
    }

  gtk_text_freeze(GTK_TEXT(help_text));
  gtk_text_insert(GTK_TEXT(help_text), NULL, NULL, NULL, pitem->text, -1);
  gtk_text_thaw(GTK_TEXT(help_text));
  gtk_widget_show(help_text);
  gtk_widget_show(help_text_scrolled);

  gtk_widget_show_all(help_ttable);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_dialog(const struct help_item *pitem)
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
  case HELP_TERRAIN:
    help_update_terrain(pitem, top, get_terrain_by_name(top));
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
static void select_help_item(int item)
{
  gtk_clist_moveto(GTK_CLIST (help_clist), item, 0, 0, 0);
  gtk_clist_select_row(GTK_CLIST (help_clist), item, 0);
}

/****************************************************************
...
*****************************************************************/
static void select_help_item_string(const char *item,
				    enum help_page_type htype)
{
  const struct help_item *pitem;
  int idx;

  pitem = get_help_item_spec(item, htype, &idx);
  if(idx==-1) idx = 0;
  select_help_item(idx);
  help_update_dialog(pitem);
}
