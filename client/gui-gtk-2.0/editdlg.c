/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "shared.h"
#include "support.h"

#include "game.h"
#include "government.h"
#include "packets.h"

#include "editor.h"

#include "editdlg.h"
#include "gui_main.h"
#include "gui_stuff.h"

#define TABLE_WIDTH 3
#define TOOL_WIDTH 5

typedef struct {
  const char *name;
  int paint;
} paint_item;

static paint_item *terrains;

static paint_item specials[] = {
  { N_("clear all"), S_LAST },
  { NULL, S_ROAD },
  { NULL, S_IRRIGATION },
  { NULL, S_RAILROAD },
  { NULL, S_MINE },
  { NULL, S_POLLUTION },
  { NULL, S_HUT },
  { NULL, S_FORTRESS },
  { NULL, S_RIVER },
  { NULL, S_FARMLAND },
  { NULL, S_AIRBASE },
  { NULL, S_FALLOUT }
};

static char *tool_names[ETOOL_LAST] = {
  N_("Paint"), N_("Unit"), N_("City"), N_("Player"), N_("Delete")
};

#define SPECIALS_NUM ARRAY_SIZE(specials)

static GtkWidget *toolwin;
static GtkWidget *notebook;

static GList *tool_group;
static GList *map_group;

/****************************************************************************
  handle the toggle buttons' toggle events
****************************************************************************/
static void tool_toggled(GtkWidget *widget, int tool)
{
  editor_set_selected_tool_type(tool);
  /* switch pages if necessary */
  gtk_notebook_set_page(GTK_NOTEBOOK(notebook), tool);
}

/****************************************************************************
  callback for button that is in a group. we want to untoggle all the
  other buttons in the group
****************************************************************************/
static void toggle_group_callback(GtkWidget *w, gpointer data)
{
  int i;
  GList *group = (GList *)data;

  /* untoggle all the other buttons in the group toggle this one */
  for (i = 0 ; i < g_list_length(group); i++) {
    GtkWidget *button = (GtkWidget *)g_list_nth_data(group, i);
    int id = (int)g_object_get_data(G_OBJECT(button), "sigid");

    g_signal_handlers_block_by_func(button,
                                    G_CALLBACK(toggle_group_callback), data);
    g_signal_handler_block(button, id);
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(button), (button == w));
    g_signal_handlers_unblock_by_func(button,
                                      G_CALLBACK(toggle_group_callback), data);
    g_signal_handler_unblock(button, id);
  }
}

/****************************************************************************
  fill the tools[]-array with the missing values: pixmaps, buttons, ids
****************************************************************************/
static void create_tools(GtkWidget *win, GtkWidget *parent)
{
  GtkWidget *button, *table;
  int i, sig;

  table = gtk_table_new(TOOL_WIDTH, 2, FALSE);
  gtk_box_pack_start(GTK_BOX(parent), table, FALSE, TRUE, 0);

  for (i = 0; i < ETOOL_LAST; i++) {
    button = gtk_toggle_button_new_with_label(tool_names[i]);

    /* must do this here. we can't call tool_toggled on palette creation */
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(button),
                                (i == 0) ? TRUE : FALSE);

    sig = g_signal_connect(button, "toggled",
                           G_CALLBACK(tool_toggled), GINT_TO_POINTER(i));

    tool_group = g_list_append(tool_group, button);

    /* add this group and the signal id to widget internal data */
    g_signal_connect(button, "toggled", G_CALLBACK(toggle_group_callback),
                     (gpointer)tool_group);
    g_object_set_data(G_OBJECT(button), "sigid", GINT_TO_POINTER(sig));

    /* do the rest for both types of buttons */
    gtk_table_attach(GTK_TABLE(table), button,
                     i % TOOL_WIDTH, i % TOOL_WIDTH + 1, i / TOOL_WIDTH,
                     i / TOOL_WIDTH + 1, GTK_FILL, GTK_FILL, 1, 1);

  }

  gtk_widget_show_all(table);
}

/****************************************************************************
  select a paint type and depress the particular button
****************************************************************************/
static void set_selected_paint(GtkWidget *w, gpointer data)
{
  int id = GPOINTER_TO_INT(data);
  enum editor_paint_type paint
    = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(w), "paint"));

  editor_set_selected_paint_type(paint);

  assert(paint >= 0 && paint < EPAINT_LAST);
  switch(paint){
  case EPAINT_TERRAIN:
    editor_set_selected_terrain(get_terrain(terrains[id].paint));
    break;
  case EPAINT_SPECIAL:
    editor_set_selected_special(specials[id].paint);
    break;
  case EPAINT_LAST:
    break;
  }
}

/****************************************************************************
  FIXME: this is for demonstration purposes only (and not demonstration of
          coding goodness to be sure!)
****************************************************************************/
static void unit_callback(GtkSpinButton *spinbutton, gpointer data)
{
  struct unit *punit = editor_get_selected_unit();
  int param = GPOINTER_TO_INT(data);

  switch (param) {
  case 0:
    punit->owner = get_player(gtk_spin_button_get_value_as_int(spinbutton));
    break;
  case 1:
    punit->type = get_unit_type(gtk_spin_button_get_value_as_int(spinbutton));
    break;
  case 2:
    punit->moves_left = gtk_spin_button_get_value_as_int(spinbutton);
    break;
  case 3:
    punit->activity = gtk_spin_button_get_value_as_int(spinbutton);
    break;
  case 4:
    punit->activity_target = gtk_spin_button_get_value_as_int(spinbutton);
    break;
  case 5:
    punit->activity_count = gtk_spin_button_get_value_as_int(spinbutton);
    break;
  }
}

/****************************************************************************
 FIXME: this is for demonstration purposes only (and not demonstration of
          coding goodness to be sure!)
****************************************************************************/
static void city_callback(GtkSpinButton *spinbutton, gpointer data)
{
  struct city *pcity = editor_get_selected_city();
  int param = GPOINTER_TO_INT(data);

  switch (param) {
  case 0:
    pcity->owner = get_player(gtk_spin_button_get_value_as_int(spinbutton));
    break;
  case 1:
    pcity->size = gtk_spin_button_get_value_as_int(spinbutton);
    break;
  case 2:
    pcity->food_stock = gtk_spin_button_get_value_as_int(spinbutton);
    break;
  case 3:
    pcity->shield_stock = gtk_spin_button_get_value_as_int(spinbutton);
    break;
  case 4:
    pcity->pollution = gtk_spin_button_get_value_as_int(spinbutton);
    break;
  }
}

#if 0
/****************************************************************************
 FIXME: this is for demonstration purposes only (and not demonstration of
          coding goodness to be sure!)
****************************************************************************/
static void player_callback(GtkSpinButton *spinbutton, gpointer data)
{
}
#endif

/****************************************************************************
  Create the palette for the editor terrain/special paint tool.
****************************************************************************/
static GtkWidget *create_map_palette(void)
{
  GtkWidget *button, *vbox;
  GtkWidget *table = gtk_table_new(12, TABLE_WIDTH, TRUE);
  int i, j, sig; 
  int magic[3] = { 0, 5, 11 }; /* magic numbers to make the table look good */
  int types_num[] = { game.control.terrain_count, SPECIALS_NUM };
  paint_item *ptype[EPAINT_LAST] = { NULL, specials };
  
  terrains = fc_realloc(terrains,
			game.control.terrain_count * sizeof(*terrains));
  ptype[0] = terrains;
  
  vbox = gtk_vbox_new(TRUE, 5);
  
  for(i = 0; i < EPAINT_LAST; i++) {
    for(j = 0; j < types_num[i]; j++) {
      paint_item *item = &ptype[i][j];

      switch(i) {
      case EPAINT_TERRAIN:
        item->paint = j;
        item->name = get_terrain(item->paint)->name;
        break;
      case EPAINT_SPECIAL:
        if (!item->name) {
	  item->name = get_special_name(item->paint);
	}
        break;
      }

      button = gtk_toggle_button_new_with_label(item->name);

      gtk_table_attach(GTK_TABLE(table), button,
                       j % TABLE_WIDTH, j % TABLE_WIDTH + 1,
                       j / TABLE_WIDTH + magic[i],  
                       j / TABLE_WIDTH + magic[i] + 1, 
                       GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0);

      gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(button),
                                  (i == 0 && j == 0) ? TRUE : FALSE);
      sig = g_signal_connect(button, "toggled",
                           G_CALLBACK(set_selected_paint), GINT_TO_POINTER(j));
      gtk_object_set_data(GTK_OBJECT(button), "paint", GINT_TO_POINTER(i));

      /* add this button to a group */
      map_group = g_list_append(map_group, button);

      /* add this group and the signal id to widget internal data */
      g_signal_connect(button, "toggled", G_CALLBACK(toggle_group_callback),
                       (gpointer)map_group);
      g_object_set_data(G_OBJECT(button), "sigid", GINT_TO_POINTER(sig));
    }
  }
  
  editor_set_selected_terrain(get_terrain(terrains[0].paint));
  gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 5);
  gtk_widget_show_all(vbox);
  
  return vbox;
}

/****************************************************************************
  Create the tab for the unit editor tool.
****************************************************************************/
static GtkWidget *create_units_palette(void)
{
#define NUM_PARAMS 6
  GtkWidget *hbox, *vbox, *label, *sb;
  GtkAdjustment *adj;
  int i;
  struct unit *punit = editor_get_selected_unit();

  const char *names[NUM_PARAMS] = { _("Owner"),
				    _("Type"),
                                    _("Moves Left"),
				    _("Activity"),
                                    _("Activity Target"),
				    _("Activity Count") };
  int inits[NUM_PARAMS][3] = {
    {punit->owner->player_no, 0, game.info.nplayers - 1},
    {punit->type->index, 0, game.control.num_unit_types - 1},
    {punit->moves_left, 0, 200},
    {punit->activity, 0, ACTIVITY_LAST},
    {punit->activity_target, 0, S_LAST},
    {punit->activity_count, 0, 200}
  };

  vbox = gtk_vbox_new(FALSE, 5);

  for (i = 0; i < NUM_PARAMS; i++) {
    adj = (GtkAdjustment *)gtk_adjustment_new(inits[i][0], inits[i][1], 
                                              inits[i][2], 1.0, 5.0, 5.0);
    hbox = gtk_hbox_new(FALSE, 5);
    sb = gtk_spin_button_new(adj, 1, 0);
    label = gtk_label_new(names[i]);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), sb, TRUE, TRUE, 0);

    g_signal_connect(sb, "value-changed", G_CALLBACK(unit_callback),
                     GINT_TO_POINTER(i));
  }

  return vbox;
#undef NUM_PARAMS
}

/****************************************************************************
  Create the tab for the city editor tool.
****************************************************************************/
static GtkWidget *create_city_palette(void)
{
#define NUM_PARAMS 5
  GtkWidget *hbox, *vbox, *label, *sb;
  GtkAdjustment *adj;
  int i;
  struct city *pcity = editor_get_selected_city();

  const char *names[NUM_PARAMS] = { _("Owner"), _("Size"),
                                    _("Food"), _("Shields"),
                                    _("Pollution") };
  int inits[NUM_PARAMS][3] = {
    {pcity->owner->player_no, 0, game.info.nplayers - 1},
    {pcity->size, 1, 63},
    {pcity->food_stock, 0, 10000},
    {pcity->shield_stock, 0, 10000},
    {pcity->pollution, 0, 2000}
  };

  vbox = gtk_vbox_new(FALSE, 5);

  for (i = 0; i < NUM_PARAMS; i++) {
    adj = (GtkAdjustment *)gtk_adjustment_new(inits[i][0], inits[i][1], 
                                              inits[i][2], 1.0, 5.0, 5.0);
    hbox = gtk_hbox_new(FALSE, 5);
    sb = gtk_spin_button_new(adj, 1, 0);
    label = gtk_label_new(names[i]);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), sb, TRUE, TRUE, 0);

    g_signal_connect(sb, "value-changed", G_CALLBACK(city_callback),
                     GINT_TO_POINTER(i));
  }

  return vbox;
#undef NUM_PARAMS
}

#if 0
/****************************************************************************
  Create the tab for the player editor tool.
****************************************************************************/
static GtkWidget *create_player_palette(void)
{
  GtkWidget *vbox;

  vbox = gtk_vbox_new(FALSE, 5);

  return vbox;
}
#endif

/****************************************************************************
  Create the tools dialog.

  FIXME: This absolutely has to be called anew each time we connect to a
  new server, since the ruleset may be different and data needs to be reset.
****************************************************************************/
static void create_toolsdlg(void)
{
  GtkWidget *vbox;

  if (toolwin) {
    return;
  }

  editor_init_tools();

  toolwin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  setup_dialog(toolwin, toplevel);
  gtk_window_set_title(GTK_WINDOW(toolwin), _("Editing Tools"));
  gtk_container_set_border_width(GTK_CONTAINER(toolwin), 5);
  gtk_window_set_policy(GTK_WINDOW(toolwin), FALSE, FALSE, FALSE);
  g_signal_connect(toolwin, "delete_event",
		   G_CALLBACK(editdlg_hide_tools), NULL);
  g_signal_connect(toolwin, "destroy",
		   GTK_SIGNAL_FUNC(gtk_widget_destroyed),
		   &toolwin);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(toolwin), vbox);

  create_tools(toolwin, vbox);
  notebook = gtk_notebook_new();
  gtk_box_pack_start(GTK_BOX(vbox), gtk_hseparator_new(), FALSE, FALSE, 2);

  gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
  gtk_box_pack_start(GTK_BOX(vbox), notebook, FALSE, FALSE, 0);

  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			   create_map_palette(), NULL);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			   create_units_palette(), NULL);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			   create_city_palette(), NULL);
#if 0
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			   create_player_palette(), NULL);
#endif

  gtk_widget_show_all(toolwin);
}

/****************************************************************************
  show the editor toolbox window
****************************************************************************/
void editdlg_show_tools(void)
{
  if (!toolwin) {
    create_toolsdlg();
  }
  gtk_widget_show(toolwin);
}

/****************************************************************************
  hide the editor toolbox window

  FIXME: When disconnecting from a server it's not enough to hide it; we
  must destroy it so that it gets remade on reconnect.
****************************************************************************/
void editdlg_hide_tools(void)
{
  if (toolwin) {
    gtk_widget_hide(toolwin);
  }
}
