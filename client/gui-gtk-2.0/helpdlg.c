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
#include "gtkpixcomm.h"

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "government.h"
#include "mem.h"
#include "shared.h"
#include "tech.h"
#include "unit.h"
#include "map.h"
#include "support.h"
#include "version.h"

#include "climisc.h"
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdata.h"
#include "tilespec.h"

#include "helpdlg.h"

#define TECH_TREE_DEPTH         20
#define TECH_TREE_EXPANDED_DEPTH 2

/*
 * Globals.
 */
static GtkWidget *help_dialog_shell;
static GtkWidget *help_clist;
static GtkWidget *help_view_scrolled;

static GtkWidget *help_view;

static GtkWidget *help_frame;
static GtkWidget *help_text;
static GtkWidget *help_text_scrolled;
static GtkWidget *help_vbox;
static GtkWidget *unit_tile;
static GtkWidget *help_box;
static GtkWidget *help_itable;
static GtkWidget *help_wtable;
static GtkWidget *help_utable;
static GtkWidget *help_ttable;
static GtkWidget *help_tree;
static GtkWidget *help_tree_scrolled;
static GtkWidget *help_tree_expand;
static GtkWidget *help_tree_expand_unknown;
static GtkWidget *help_tree_collapse;
static GtkWidget *help_tree_reset;
static GtkWidget *help_tree_buttons_hbox;
static GtkWidget *help_ilabel[6];
static GtkWidget *help_wlabel[6];
static GtkWidget *help_ulabel[5][5];
static GtkWidget *help_tlabel[4][5];

typedef struct help_tree_node {
  int tech;
  int turns_to_tech;
} help_tndata;

static char *help_ilabel_name[6] =
{ N_("Cost:"), NULL, N_("Upkeep:"), NULL, N_("Requirement:"), NULL };

static char *help_wlabel_name[6] =
{ N_("Cost:"), NULL, N_("Requirement:"), NULL, N_("Obsolete by:"), NULL };

static char *help_ulabel_name[5][5] =
{
    { N_("Cost:"),		NULL, NULL, N_("Attack:"),	NULL },
    { N_("Defense:"),		NULL, NULL, N_("Move:")	,	NULL },
    { N_("FirePower:"),		NULL, NULL, N_("Hitpoints:"),	NULL },
    { N_("Basic Upkeep:"),	NULL, NULL, N_("Vision:"),	NULL },
    { N_("Requirement:"),	NULL, NULL, N_("Obsolete by:"),	NULL }
};

static char *help_tlabel_name[4][5] =
{
    { N_("Move/Defense:"),	NULL, NULL, N_("Food/Res/Trade:"),	NULL },
    { N_("Sp1 F/R/T:"),		NULL, NULL, N_("Sp2 F/R/T:"),		NULL },
    { N_("Road Rslt/Time:"),	NULL, NULL, N_("Irrig. Rslt/Time:"),	NULL },
    { N_("Mine Rslt/Time:"),	NULL, NULL, N_("Trans. Rslt/Time:"),	NULL }
};


static void create_help_dialog(void);
static void help_update_dialog(const struct help_item *pitem);
static void create_help_page(enum help_page_type type);

static void select_help_item_string(const char *item,
				    enum help_page_type htype);

/****************************************************************
...
*****************************************************************/
static void set_title_topic(char *topic)
{
  if (strcmp(topic, "Freeciv") == 0 || strcmp(topic, "About") == 0
      || strcmp(topic, _("About")) == 0) {
    gtk_frame_set_label(GTK_FRAME(help_frame), freeciv_name_version());
  } else {
    gtk_frame_set_label(GTK_FRAME(help_frame), topic);
  }
  return;
}

/****************************************************************
...
*****************************************************************/
void popdown_help_dialog(void)
{
  if(help_dialog_shell) {
    gtk_widget_destroy(help_dialog_shell);
    help_dialog_shell = NULL;
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
Not sure if this should call _(item) as it does, or whether all
callers of this function should do so themselves... --dwp
*****************************************************************/
void popup_help_dialog_string(char *item)
{
  popup_help_dialog_typed(_(item), HELP_ANY);
}

/****************************************************************
Called when destroying a node in the help_tree
*****************************************************************/
static void destroy_help_tndata(gpointer data) {
  if (data)
    free(data);
}

/****************************************************************
Called when creating a node in the help_tree to ensure that no
duplicate subtrees are generated.
*****************************************************************/
static int help_tndata_compare(gpointer d1, gpointer d2) {
  help_tndata *a = (help_tndata *)d1;
  help_tndata *b = (help_tndata *)d2;
  if (!a)
    return (b != NULL);
  return (a->tech != b->tech);
}      

/**************************************************************************
Called by help_update_tech and itself
Creates a node in the given tree for the given tech, and creates child
nodes for any children it has up to levels deep. These are then expanded
if they are less than expanded_levels deep. Avoids generating redundant
subtrees, so that if Literacy occurs twice in a tech tree, only the first
will have children. Color codes the node based on when it will be
discovered: red >2 turns, yellow 1 turn, green 0 turns (discovered).
**************************************************************************/
static void create_tech_tree(GtkCTree *ctree, int tech, int levels,
			     int expanded_levels, GtkCTreeNode *parent)
{
  GtkCTreeNode *l;
  gchar        *text[1];
  int	        bg;
  char          label [MAX_LEN_NAME+3];
  gboolean      leaf;
  help_tndata  *d = fc_malloc(sizeof(help_tndata));
  bool           original;

  text[0] = label;
  if ( advances[tech].req[0] == A_LAST && advances[tech].req[1] == A_LAST )
  {
    sz_strlcpy(label, _("Removed"));
    bg = COLOR_STD_RED;
    l = gtk_ctree_insert_node(ctree, parent, NULL, text, 10,
				NULL, NULL, NULL, NULL, TRUE, TRUE);
    d->tech = tech;
    d->turns_to_tech = -1;
    gtk_ctree_node_set_row_data_full(ctree, l, d, destroy_help_tndata);
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
  d->tech = tech;
  d->turns_to_tech = num_unknown_techs_for_goal(game.player_ptr, tech);
  l = gtk_ctree_find_by_row_data_custom(ctree, NULL, d, (GCompareFunc)help_tndata_compare);
  /* l is the original in the tree. */
  original = (l == NULL);

  my_snprintf(label, sizeof(label), "%s:%d", advances[tech].name,
	      d->turns_to_tech);

  leaf = (advances[tech].req[0] == A_NONE && advances[tech].req[1] == A_NONE);

  l = gtk_ctree_insert_node(ctree, parent, NULL, text, 10,
				 NULL, NULL, NULL, NULL, leaf, TRUE);
  gtk_ctree_node_set_row_data_full(ctree, l, d, destroy_help_tndata);
  gtk_ctree_node_set_background(ctree, l, colors_standard[bg]);

  if (d->turns_to_tech <= 1) {
    expanded_levels = 0;
  }

  if (expanded_levels > 0) {
    gtk_ctree_expand(ctree, l);
    expanded_levels--;
  } else {
    gtk_ctree_collapse(ctree, l);
  }

  if ( --levels <= 0 )
      return;

  if (original) {
    /* only add children to orginals */
    if ( advances[tech].req[0] != A_NONE )
      create_tech_tree(ctree, advances[tech].req[0], levels, expanded_levels, l);
    if ( advances[tech].req[1] != A_NONE )
      create_tech_tree(ctree, advances[tech].req[1], levels, expanded_levels, l);
  }
  return;
}

/**************************************************************************
Selects the help page for the tech in the tree that was double clicked.
As a ctree eats the GDK_2BUTTON_PRESS events that would normally go through
to the clist parent (it turns them into "ctree_expand" or "ctree_collapse"
events), we have to register a "button_press_event" callback instead.
**************************************************************************/
static int help_tech_tree_mouse_callback(GtkWidget *w, GdkEventButton *ev)
{
  GtkCTree *ctree = GTK_CTREE(w);
  GtkCList *clist = GTK_CLIST(w);
  GtkCTreeNode *node;
  help_tndata *d;
  int row;
  int col;
  /* check we have an event for the ctree */
  if (!ev || (ev->window != clist->clist_window))
    return FALSE;
  /* check there is a row where the user clicked */
  if (gtk_clist_get_selection_info(clist, ev->x, ev->y, &row, &col) == FALSE)
    return FALSE;
  /* grab the double click */
  if (ev->type == GDK_2BUTTON_PRESS) {
    node = gtk_ctree_node_nth(ctree, row);
    d = (help_tndata *)gtk_ctree_node_get_row_data(ctree, node);
    select_help_item_string(advances[d->tech].name, HELP_TECH);
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
Called when "Expand All" button is clicked
**************************************************************************/
static void help_tech_tree_expand_callback(GtkWidget *w, gpointer data)
{
  GtkCTree *ctree = GTK_CTREE(data);
  gtk_ctree_expand_recursive(ctree, gtk_ctree_node_nth(ctree, 0));
}

/**************************************************************************
Called when "Collapse All" button is clicked
**************************************************************************/
static void help_tech_tree_collapse_callback(GtkWidget *w, gpointer data)
{
  GtkCTree *ctree = GTK_CTREE(data);
  gtk_ctree_collapse_recursive(ctree, gtk_ctree_node_nth(ctree, 0));
}

/**************************************************************************
Called by "Reset Tree" and "Expand Unknown" button callbacks
**************************************************************************/
static void help_tech_tree_node_expand(GtkCTree *ctree, GtkCTreeNode *node,
				       gpointer data)
{
  help_tndata *d = (help_tndata *)gtk_ctree_node_get_row_data(ctree, node);
  if (d->turns_to_tech > 1)
    gtk_ctree_expand(ctree, node);
}

/**************************************************************************
Called when "Reset tree" button is clicked
**************************************************************************/
static void help_tech_tree_reset_callback(GtkWidget *w, gpointer data)
{
  GtkCTree *ctree = GTK_CTREE(data);
  /* Collapse the whole tree */
  gtk_ctree_collapse_recursive(ctree, gtk_ctree_node_nth(ctree, 0));
  /* Expand the deserving */
  gtk_ctree_pre_recursive_to_depth(ctree, gtk_ctree_node_nth(ctree, 0), 
				   TECH_TREE_EXPANDED_DEPTH,
				   help_tech_tree_node_expand, NULL);
}

/**************************************************************************
Called when "Expand Unknown" button is clicked
**************************************************************************/
static void help_tech_tree_expand_unknown_callback(GtkWidget *w, gpointer data)
{
  GtkCTree *ctree = GTK_CTREE(data);
  /* Collapse the whole tree */
  gtk_ctree_collapse_recursive(ctree, gtk_ctree_node_nth(ctree, 0));
  /* Expand the deserving */
  gtk_ctree_pre_recursive(ctree, gtk_ctree_node_nth(ctree, 0), 
			  help_tech_tree_node_expand, NULL);
}

/**************************************************************************
...
**************************************************************************/
static void help_hyperlink_callback(GtkWidget *w)
{
  const char *s;
  enum help_page_type type;

  s=gtk_label_get_text(GTK_LABEL(w));
  type=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "page_type"));

  /* May be able to skip, or may need to modify, advances[A_NONE].name
     below, depending on which i18n is done elsewhere.
  */
  if (strcmp(s, _("(Never)")) != 0 && strcmp(s, _("None")) != 0
      && strcmp(s, advances[A_NONE].name) != 0)
    select_help_item_string(s, type);
}

static GtkWidget *help_hyperlink_new(GtkWidget *label, enum help_page_type type)
{
  GtkWidget *button;

  button = gtk_button_new();
  gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
  gtk_container_add(GTK_CONTAINER(button), label);
  g_signal_connect_swapped(button, "clicked",
			   G_CALLBACK(help_hyperlink_callback), label);
  g_object_set_data(G_OBJECT(label), "page_type", GUINT_TO_POINTER(type));
  return button;
}

static GtkWidget *help_slink_new(gchar *txt, enum help_page_type type)
{
  GtkWidget *button, *label;

  button = gtk_button_new();
  label = gtk_label_new(txt);
  gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
  gtk_container_add(GTK_CONTAINER(button), label);
  g_signal_connect_swapped(button, "clicked",
			   G_CALLBACK(help_hyperlink_callback), label);
  g_object_set_data(G_OBJECT(label), "page_type", GUINT_TO_POINTER(type));
  return button;
}

static void selected_topic(GtkTreeSelection *selection, GtkTreeModel *model)
{
  GtkTreeIter it;
  const struct help_item *p = NULL;

  if (!gtk_tree_selection_get_selected(selection, NULL, &it))
    return;

  gtk_tree_model_get(model, &it, 1, &p, -1);
  help_update_dialog(p);
}

/**************************************************************************
...
**************************************************************************/
static void create_help_dialog(void)
{
  GtkWidget *hbox;
  GtkWidget *button;
  int	     i, j;
  GtkCellRenderer   *renderer;
  GtkTreeViewColumn *col;
  GArray            *array;
  GtkTreeStore      *store;
  GtkTreeSelection  *selection;

  help_dialog_shell = gtk_dialog_new();
  gtk_widget_set_name(help_dialog_shell, "Freeciv");

  g_signal_connect(help_dialog_shell, "destroy",
		   G_CALLBACK(gtk_widget_destroyed), &help_dialog_shell);

  gtk_window_set_title( GTK_WINDOW( help_dialog_shell ), _("Freeciv Help Browser") );
  gtk_container_border_width( GTK_CONTAINER( help_dialog_shell ), 5 );

  hbox = gtk_hbox_new( FALSE, 5 );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(help_dialog_shell)->vbox ),
  	  hbox, TRUE, TRUE, 0 );

  /* build tree store. */
  store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);

  array = g_array_new(FALSE, FALSE, sizeof(GtkTreeIter));
  help_items_iterate(pitem) {
    GtkTreeIter *it, *parent;
    const char *s;
    int depth;

    for (s = pitem->topic; *s == ' '; s++) {
      /* nothing */
    }
    depth = s - pitem->topic;

    array = g_array_set_size(array, depth+1);

    if (depth > 0) {
      parent = &g_array_index(array, GtkTreeIter, depth-1);
    } else {
      parent = NULL;
    }

    it = &g_array_index(array, GtkTreeIter, depth);
    gtk_tree_store_append(store, it, parent);

    gtk_tree_store_set(store, it, 0, pitem->topic, 1, pitem, -1);
  } help_items_iterate_end;


  /* create tree view. */
  help_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(help_view));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(help_view), FALSE);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(help_view));
  g_signal_connect(selection, "changed", G_CALLBACK(selected_topic), store);

  renderer = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, renderer, "text",0,NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(help_view), col);

  help_clist = gtk_clist_new(1);
  gtk_clist_set_column_width(GTK_CLIST(help_clist), 0, GTK_CLIST(help_clist)->clist_window_width);
  help_view_scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(help_view_scrolled),
  			  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(help_view_scrolled, 180, -1);
  gtk_container_add(GTK_CONTAINER(help_view_scrolled), help_view);

  gtk_box_pack_start(GTK_BOX(hbox), help_view_scrolled, FALSE, FALSE, 0);

  help_frame = gtk_frame_new( "" );
  gtk_box_pack_start( GTK_BOX( hbox ), help_frame, TRUE, TRUE, 0 );
  gtk_widget_set_usize(help_frame, 580, 350);

  help_box = gtk_vbox_new( FALSE, 5 );
  gtk_container_add( GTK_CONTAINER( help_frame ), help_box );


  unit_tile = gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH,
  					   UNIT_TILE_HEIGHT);
  gtk_box_pack_start( GTK_BOX( help_box ), unit_tile, FALSE, FALSE, 0 );

  help_itable = gtk_table_new(1, 6, FALSE);
  gtk_box_pack_start(GTK_BOX(help_box), help_itable, FALSE, FALSE, 0);

  for (i=0; i<6; i++) {
    help_ilabel[i] =
	gtk_label_new(help_ilabel_name[i] ? _(help_ilabel_name[i]) : "");
    gtk_widget_set_name(help_ilabel[i], "help label");

    if (i==5) {
      button = help_hyperlink_new(help_ilabel[i], HELP_TECH);
      gtk_table_attach_defaults(GTK_TABLE(help_itable), button, i, i+1, 0, 1);
    }
    else
    gtk_table_attach_defaults(GTK_TABLE(help_itable),
					help_ilabel[i], i, i+1, 0, 1);
  }

  help_wtable = gtk_table_new(1, 6, FALSE);
  gtk_box_pack_start(GTK_BOX(help_box), help_wtable, FALSE, FALSE, 0);

  for (i=0; i<6; i++) {
    help_wlabel[i] =
	gtk_label_new(help_wlabel_name[i] ? _(help_wlabel_name[i]) : "");
    gtk_widget_set_name(help_wlabel[i], "help label");

    if (i==3 || i==5) {
      button = help_hyperlink_new(help_wlabel[i], HELP_TECH);
      gtk_table_attach_defaults(GTK_TABLE(help_wtable), button, i, i+1, 0, 1);
    }
    else
    gtk_table_attach_defaults(GTK_TABLE(help_wtable),
					help_wlabel[i], i, i+1, 0, 1);
  }


  help_utable = gtk_table_new(5, 5, FALSE);
  gtk_box_pack_start(GTK_BOX(help_box), help_utable, FALSE, FALSE, 0);

  for (i=0; i<5; i++)
    for (j=0; j<5; j++)
    {
      help_ulabel[j][i] =
	  gtk_label_new(help_ulabel_name[j][i] ? _(help_ulabel_name[j][i]) : "");
      gtk_widget_set_name(help_ulabel[j][i], "help label");

      if (j==4 && (i==1 || i==4))
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


  help_ttable = gtk_table_new(5, 5, FALSE);
  gtk_box_pack_start(GTK_BOX(help_box), help_ttable, FALSE, FALSE, 0);

  for (i=0; i<5; i++) {
    for (j=0; j<4; j++) {
      help_tlabel[j][i] =
	  gtk_label_new(help_tlabel_name[j][i] ? _(help_tlabel_name[j][i]) : "");
      gtk_widget_set_name(help_tlabel[j][i], "help label");

      gtk_table_attach_defaults(GTK_TABLE(help_ttable),
					  help_tlabel[j][i], i, i+1, j, j+1);
    }
  }


  help_vbox=gtk_vbox_new(FALSE, 1);
  gtk_box_pack_start( GTK_BOX(help_box), help_vbox, FALSE, FALSE, 0 );

  help_text_scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(help_text_scrolled),
  			  GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  help_text = gtk_text_view_new();
  gtk_container_add(GTK_CONTAINER(help_text_scrolled), help_text);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(help_text), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(help_text), GTK_WRAP_NONE);
  gtk_box_pack_start(GTK_BOX(help_box), help_text_scrolled, TRUE, TRUE, 0);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(help_text), 5);
  gtk_widget_set_name(help_text, "help text");

  help_tree = gtk_ctree_new(1,0);
  gtk_clist_set_column_width(GTK_CLIST(help_tree), 0, GTK_CLIST(help_tree)->clist_window_width);
  gtk_ctree_set_line_style(GTK_CTREE(help_tree),GTK_CTREE_LINES_DOTTED);
  gtk_clist_set_column_justification(GTK_CLIST(help_tree),0,1);
  gtk_ctree_set_expander_style(GTK_CTREE(help_tree),
  				  GTK_CTREE_EXPANDER_CIRCULAR);

  g_signal_connect(help_tree, "button_press_event",
		   G_CALLBACK(help_tech_tree_mouse_callback), NULL);
  help_tree_scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(help_tree_scrolled), help_tree);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(help_tree_scrolled),
  			  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );

  gtk_box_pack_start( GTK_BOX( help_box ), help_tree_scrolled, TRUE, TRUE, 0 );

  help_tree_expand = gtk_button_new_with_label(_("Expand All"));
  help_tree_collapse = gtk_button_new_with_label(_("Collapse All"));
  help_tree_reset = gtk_button_new_with_label(_("Reset Tree"));
  help_tree_expand_unknown = gtk_button_new_with_label(_("Expand Unknown"));
  g_signal_connect(help_tree_expand, "clicked",
		   G_CALLBACK(help_tech_tree_expand_callback), help_tree);
  g_signal_connect(help_tree_collapse, "clicked",
		   G_CALLBACK(help_tech_tree_collapse_callback), help_tree);
  g_signal_connect(help_tree_reset, "clicked",
		   G_CALLBACK(help_tech_tree_reset_callback), help_tree);
  g_signal_connect(help_tree_expand_unknown, "clicked",
		   G_CALLBACK(help_tech_tree_expand_unknown_callback),
		   help_tree);
  help_tree_buttons_hbox = gtk_hbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(help_tree_buttons_hbox), help_tree_expand,
		     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(help_tree_buttons_hbox), help_tree_collapse,
		     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(help_tree_buttons_hbox), help_tree_reset,
		     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(help_tree_buttons_hbox),
		     help_tree_expand_unknown, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(help_box), help_tree_buttons_hbox,
		     FALSE, FALSE, 0);

  button = gtk_button_new_with_label( _("Close") );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(help_dialog_shell)->action_area ),
      button, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( button );

  g_signal_connect_swapped(button, "clicked",
			   G_CALLBACK(popdown_help_dialog), help_dialog_shell);

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
  GtkTextBuffer *b;
  
  create_help_page(HELP_IMPROVEMENT);
  
  if (which<game.num_impr_types) {
    struct impr_type *imp = &improvement_types[which];
    sprintf(buf, "%d", imp->build_cost);
    gtk_set_label(help_ilabel[1], buf);
    sprintf(buf, "%d", imp->upkeep);
    gtk_set_label(help_ilabel[3], buf);
    if (imp->tech_req == A_LAST) {
      gtk_set_label(help_ilabel[5], _("(Never)"));
    } else {
      gtk_set_label(help_ilabel[5], advances[imp->tech_req].name);
    }
/*    create_tech_tree(help_improvement_tree, 0, imp->tech_req, 3);*/
  }
  else {
    gtk_set_label(help_ilabel[1], "0");
    gtk_set_label(help_ilabel[3], "0");
    gtk_set_label(help_ilabel[5], _("(Never)"));
/*    create_tech_tree(help_improvement_tree, 0, game.num_tech_types, 3);*/
  }
  gtk_widget_show_all(help_itable);

  helptext_improvement(buf, which, pitem->text);
  b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(help_text));
  gtk_text_buffer_set_text(b, buf, -1);
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
  GtkTextBuffer *b;

  create_help_page(HELP_WONDER);

  if (which<game.num_impr_types) {
    struct impr_type *imp = &improvement_types[which];
    sprintf(buf, "%d", imp->build_cost);
    gtk_set_label(help_wlabel[1], buf);
    if (imp->tech_req == A_LAST) {
      gtk_set_label(help_wlabel[3], _("(Never)"));
    } else {
      gtk_set_label(help_wlabel[3], advances[imp->tech_req].name);
    }
    gtk_set_label(help_wlabel[5], advances[imp->obsolete_by].name);
/*    create_tech_tree(help_improvement_tree, 0, imp->tech_req, 3);*/
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
  b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(help_text));
  gtk_text_buffer_set_text(b, buf, -1);
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
  GtkTextBuffer *b;

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
    gtk_set_label(help_ulabel[3][1], helptext_unit_upkeep_str(i));
    sprintf(buf, "%d", utype->vision_range);
    gtk_set_label(help_ulabel[3][4], buf);
    if(utype->tech_requirement==A_LAST) {
      gtk_set_label(help_ulabel[4][1], _("(Never)"));
    } else {
      gtk_set_label(help_ulabel[4][1], advances[utype->tech_requirement].name);
    }
/*    create_tech_tree(help_improvement_tree, 0, utype->tech_requirement, 3);*/
    if(utype->obsoleted_by==-1) {
      gtk_set_label(help_ulabel[4][4], _("None"));
    } else {
      gtk_set_label(help_ulabel[4][4], get_unit_type(utype->obsoleted_by)->name);
    }

    helptext_unit(buf, i, pitem->text);

    b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(help_text));
    gtk_text_buffer_set_text(b, buf, -1);
    gtk_widget_show(help_text);
    gtk_widget_show(help_text_scrolled);

    create_overlay_unit(unit_tile, i);
    gtk_widget_show(unit_tile);
  }
  else {
    gtk_set_label(help_ulabel[0][1], "0");
    gtk_set_label(help_ulabel[0][4], "0");
    gtk_set_label(help_ulabel[1][1], "0");
    gtk_set_label(help_ulabel[1][4], "0");
    gtk_set_label(help_ulabel[2][1], "0");
    gtk_set_label(help_ulabel[2][4], "0");
    gtk_set_label(help_ulabel[3][1], "0");
    gtk_set_label(help_ulabel[3][4], "0");

    gtk_set_label(help_ulabel[4][1], _("(Never)"));
/*    create_tech_tree(help_improvement_tree, 0, A_LAST, 3);*/
    gtk_set_label(help_ulabel[4][4], _("None"));

    b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(help_text));
    gtk_text_buffer_set_text(b, buf, -1);
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

  if (!is_future_tech(i)) {
    gtk_container_foreach(GTK_CONTAINER(help_vbox), (GtkCallback)gtk_widget_destroy, NULL);

    gtk_clist_freeze(GTK_CLIST(help_tree));
    gtk_clist_clear(GTK_CLIST(help_tree));
    create_tech_tree(GTK_CTREE(help_tree), i, TECH_TREE_DEPTH,
		     TECH_TREE_EXPANDED_DEPTH, NULL);
    gtk_clist_thaw(GTK_CLIST(help_tree));
    gtk_widget_show_all(help_tree_scrolled);
    gtk_widget_show_all(help_tree_buttons_hbox);

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

    impr_type_iterate(j) {
      if(i==improvement_types[j].tech_req) {
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
      if(i==improvement_types[j].obsolete_by) {
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
        w = gtk_label_new(_("Obsoletes "));
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = help_slink_new(improvement_types[j].name,
			  is_wonder(j)?HELP_WONDER:HELP_IMPROVEMENT);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = gtk_label_new(".");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      }
    } impr_type_iterate_end;

    unit_type_iterate(j) {
      if(i!=get_unit_type(j)->tech_requirement) continue;
      hbox = gtk_hbox_new(FALSE, 0);
      gtk_container_add(GTK_CONTAINER(help_vbox), hbox);
      w = gtk_label_new(_("Allows "));
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = help_slink_new(get_unit_type(j)->name, HELP_UNIT);
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
      w = gtk_label_new(".");
      gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
    } unit_type_iterate_end;

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
          w = gtk_label_new(Q_("?techhelp:)."));
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
        w = gtk_label_new(Q_("?techhelp:)."));
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
  char *buf = &long_buffer[0];
  GtkTextBuffer *b;

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
	  sprintf (buf, _("%s F/R/T:"),
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
	  sprintf (buf, _("%s F/R/T:"),
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

  helptext_terrain(buf, i, pitem->text);
  
  b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(help_text));
  gtk_text_buffer_set_text(b, buf, -1);
  gtk_widget_show(help_text);
  gtk_widget_show(help_text_scrolled);

  gtk_widget_show_all(help_ttable);
}

/**************************************************************************
  This is currently just a text page, with special text:
**************************************************************************/
static void help_update_government(const struct help_item *pitem,
				   char *title, struct government *gov)
{
  char *buf = &long_buffer[0];
  GtkTextBuffer *b;

  if (!gov) {
    strcat(buf, pitem->text);
  } else {
    helptext_government(buf, gov-governments, pitem->text);
  }
  create_help_page(HELP_TEXT);
  b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(help_text));
  gtk_text_buffer_set_text(b, buf, -1);
  gtk_widget_show(help_text);
  gtk_widget_show(help_text_scrolled);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_dialog(const struct help_item *pitem)
{
  int i;
  char *top;
  GtkTextBuffer *b;

  /* figure out what kind of item is required for pitem ingo */

  for(top=pitem->topic; *top==' '; ++top);

  gtk_widget_hide_all(help_box);
  b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(help_text));
  gtk_text_buffer_set_text(b, "", -1);

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
  case HELP_GOVERNMENT:
    help_update_government(pitem, top, find_government_by_name(top));
    break;
  case HELP_TEXT:
  default:
    /* it was a pure text item */ 
    create_help_page(HELP_TEXT);

    b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(help_text));
    gtk_text_buffer_set_text(b, pitem->text, -1);
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
gboolean select_item_iteration(GtkTreeModel *model, GtkTreePath *path,
			       GtkTreeIter *it, gpointer data)
{
  const struct help_item *pitem = data, *pitem2;

  gtk_tree_model_get(model, it, 1, &pitem2, -1);

  if (pitem == pitem2) {
    GtkTreeSelection *selection;
    GtkTreeIter       parent, step;
    GtkTreePath      *ppath;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(help_view));

    step = *it;
    while (gtk_tree_store_iter_depth(GTK_TREE_STORE(model), &step) > 0) {
      gtk_tree_model_iter_parent(model, &parent, &step);
      step = parent;
    }

    ppath = gtk_tree_model_get_path(model, &step);
    gtk_tree_view_expand_row(GTK_TREE_VIEW(help_view), ppath, TRUE);
    gtk_tree_path_free(ppath);

    gtk_tree_selection_select_iter(selection, it);

#ifdef UNUSED
    /* XXX: would be nice if it actually worked everytime! */
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(help_view), path, NULL,
				 TRUE, 0.0, 0.0);
#endif
    return TRUE;
  } else {
    return FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
static void select_help_item(const struct help_item *pitem)
{
  if (pitem) {
    GtkTreeModel *model;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(help_view));
    gtk_tree_model_foreach(model, select_item_iteration, (gpointer)pitem);
  }
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
  select_help_item(pitem);
  help_update_dialog(pitem);
}
