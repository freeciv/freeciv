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
#include "log.h"
#include "shared.h"
#include "support.h"

#include "connection.h"
#include "game.h"
#include "government.h"
#include "packets.h"

#include "chatline_common.h"
#include "civclient.h"
#include "editor.h"
#include "mapview_common.h"
#include "tilespec.h"

#include "canvas.h"
#include "dialogs_g.h"
#include "editgui.h"
#include "editprop.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "plrdlg.h"


#define BWIDTH 40
#define BHEIGHT 40

enum tool_value_selector_columns {
  TVS_COL_IMAGE = 0,
  TVS_COL_ID,
  TVS_COL_NAME,
  
  TVS_NUM_COLS
};

enum player_pov_combo_columns {
  PPV_COL_FLAG = 0,
  PPV_COL_NAME,

  PPV_NUM_COLS
};

enum tool_applied_player_columns {
  TAP_COL_FLAG = 0,
  TAP_COL_NAME,
  TAP_COL_PLAYER_NO,

  TAP_NUM_COLS
};

enum spin_button_types {
  SPIN_BUTTON_SIZE,
  SPIN_BUTTON_COUNT
};

struct tool_value_selector {
  struct editbar *editbar_parent;

  GtkWidget *dialog;

  GtkListStore *store;
  GtkWidget *view;
};

static struct tool_value_selector *
create_tool_value_selector(struct editbar *eb_parent,
                           enum editor_tool_type ett);
static void editinfobox_refresh(struct editinfobox *ei);

static struct editbar *editor_toolbar;
static struct editinfobox *editor_infobox;

/****************************************************************************
  Refresh the buttons in the given editbar according to the current
  editor state.
****************************************************************************/
static void refresh_tool_buttons(struct editbar *eb)
{
  enum editor_tool_type ett;
  bool erase;
  GtkWidget *tb = NULL;

  if (eb == NULL) {
    return;
  }

  erase = editor_get_erase_mode();
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(eb->erase_button), erase);

  ett = editor_get_tool();
  if (!(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)) {
    return;
  }

  tb = eb->tool_buttons[ett];
  if (tb != NULL) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tb), TRUE);
  }
}

/****************************************************************************
  Callback for erase-mode button toggle.
****************************************************************************/
static void editbar_erase_button_toggled(GtkToggleButton *tb,
                                         gpointer userdata)
{
  gboolean active = gtk_toggle_button_get_active(tb);

  editor_set_erase_mode(active);
  editinfobox_refresh(editgui_get_editinfobox());
}

/****************************************************************************
  Callback to handle toggling of any of the tool buttons.
****************************************************************************/
static void editbar_tool_button_toggled(GtkToggleButton *tb,
                                        gpointer userdata)
{
  gboolean active;
  enum editor_tool_type ett;

  active = gtk_toggle_button_get_active(tb);
  ett = GPOINTER_TO_INT(userdata);

  if (active) {
    editor_set_tool(ett);
    editinfobox_refresh(editgui_get_editinfobox());
  }
}

/****************************************************************************
  Refresh the player point-of-view indicator based on the client and
  editor state.
****************************************************************************/
static void refresh_player_pov_indicator(struct editbar *eb)
{
  GtkListStore *store;
  GdkPixbuf *flag;
  GtkTreeIter iter;
  GtkComboBox *combo;
  gint index = -1;

  const struct player *me = client_player();
  const bool am_global_observer = client_is_global_observer();

  if (eb == NULL) {
    return;
  }

  if (eb->player_pov_store == NULL) {
    return;
  }

  store = eb->player_pov_store;

  gtk_list_store_clear(store);

  players_iterate(pplayer) {
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       PPV_COL_NAME, player_name(pplayer), -1);

    if (pplayer->nation != NO_NATION_SELECTED) {
      flag = get_flag(pplayer->nation);

      if (flag != NULL) {
        gtk_list_store_set(store, &iter, PPV_COL_FLAG, flag, -1);
        g_object_unref(flag);
      }
    }
  } players_iterate_end;

  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter,
                     PPV_COL_NAME, _("Global Observer"), -1);

  if (am_global_observer) {
    index = player_count();
  } else if (me != NULL) {
    index = player_number(me);
  }

  combo = GTK_COMBO_BOX(eb->player_pov_combobox);
  gtk_combo_box_set_active(combo, index);
}

/****************************************************************************
  Callback to handle selection of a player/global observer in the
  player pov indicator.
****************************************************************************/
static void editbar_player_pov_combobox_changed(GtkComboBox *combo,
                                                gpointer user_data)
{
  struct editbar *eb = user_data;
  gint index;
  struct player *pplayer;

  const struct player *me = client_player();
  const bool am_global_observer = client_is_global_observer();

  if (eb == NULL || eb->widget == NULL
      || !GTK_WIDGET_VISIBLE(eb->widget)) {
    return;
  }

  index = gtk_combo_box_get_active(combo);

  if ((am_global_observer && index == player_count())
      || (me != NULL && player_number(me) == index)) {
    return;
  }

  /* Ugh... hard-coded server command strings. :( */
  if (index == player_count()) {
    send_chat("/observe");
  } else if (NULL != (pplayer = valid_player_by_number(index))) {
    char buf[64];
    my_snprintf(buf, sizeof(buf), "/take '%s'", pplayer->name);
    send_chat(buf);
  }
}

/****************************************************************************
  Run the tool value selection dailog and return the value ID selected.
  Returns -1 if cancelled.
****************************************************************************/
static int tool_value_selector_run(struct tool_value_selector *tvs)
{
  gint res;
  GtkWidget *dialog;
  GtkTreeSelection *sel;
  GtkTreeIter iter;
  GtkTreeModel *model;
  int id;

  if (tvs == NULL) {
    return -1;
  }

  dialog = tvs->dialog;
  res = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_hide(dialog);

  if (res != GTK_RESPONSE_ACCEPT) {
    return -1;
  }

  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tvs->view));
  if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
    return -1;
  }

  gtk_tree_model_get(model, &iter, TVS_COL_ID, &id, -1);

  return id;
}

/****************************************************************************
  Run the tool value selector for the given tool type. Sets the editor state
  and refreshes the editor GUI depending on the user's choices.
  
  Returns FALSE if running the dialog is not possible.
****************************************************************************/
static bool editgui_run_tool_selection(enum editor_tool_type ett)
{
  struct editbar *eb;
  struct tool_value_selector *tvs;
  int res = -1;

  eb = editgui_get_editbar();
  if (eb == NULL || !(0 <= ett && ett < NUM_EDITOR_TOOL_TYPES)) {
    return FALSE;
  }

  if (!editor_tool_has_value(ett)) {
    return FALSE;
  }

  tvs = eb->tool_selectors[ett];
  if (!tvs) {
    return FALSE;
  }

  res = tool_value_selector_run(tvs);

  if (res >= 0) {
    editor_tool_set_value(ett, res);
    editinfobox_refresh(editgui_get_editinfobox());
  }

  return TRUE;
}

/****************************************************************************
  Handle a mouse click on any of the tool buttons.
****************************************************************************/
static gboolean editbar_tool_button_mouse_click(GtkWidget *w,
                                                GdkEventButton *ev,
                                                gpointer userdata)
{
  int ett;

  if (ev->button != 3) {
    return FALSE; /* Propagate event further. */
  }

  ett = GPOINTER_TO_INT(userdata);
  return editgui_run_tool_selection(ett);
}

/****************************************************************************
  Create a tool button for the given editor toolbar. The 'radio_parent'
  argument is another radio button in the same radio button group, or
  NULL if this is to be the first button in a group.
****************************************************************************/
static GtkWidget *create_editbar_radiobutton(struct editbar *eb,
                                             enum editor_tool_type ett,
                                             GtkRadioButton *radio_parent,
                                             GtkWidget *box)
{
  GdkPixbuf *pixbuf;
  GtkWidget *image, *button;
  struct sprite *sprite;

  if (radio_parent == NULL) {
    button = gtk_radio_button_new(NULL);
  } else {
    button = gtk_radio_button_new_from_widget(radio_parent);
  }

  sprite = editor_tool_get_sprite(ett);
  assert(sprite != NULL);
  pixbuf = sprite_get_pixbuf(sprite);
  image = gtk_image_new_from_pixbuf(pixbuf);

  gtk_container_add(GTK_CONTAINER(button), image);
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
  gtk_tooltips_set_tip(eb->tooltips, button,
                       editor_tool_get_tooltip(ett), "");
  gtk_widget_set_size_request(button, BWIDTH, BHEIGHT);
  gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
  gtk_button_set_focus_on_click(GTK_BUTTON(button), FALSE);

  g_signal_connect(button, "toggled",
      G_CALLBACK(editbar_tool_button_toggled), GINT_TO_POINTER(ett));
  g_signal_connect(button, "button_press_event",
      G_CALLBACK(editbar_tool_button_mouse_click), GINT_TO_POINTER(ett));

  gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
  eb->tool_buttons[ett] = button;

  if (editor_tool_has_value(ett)) {
    eb->tool_selectors[ett] = create_tool_value_selector(eb, ett);
  }

  return button;
}

/****************************************************************************
  Handle a click on the player properties button in the editor toolbar.
****************************************************************************/
static void editbar_player_properties_button_clicked(GtkButton *b,
                                                     gpointer userdata)
{
  struct property_editor *pe;

  pe = editprop_get_property_editor();
  property_editor_reload_players(pe);
  property_editor_popup(pe);
}

/****************************************************************************
  Create and return an editor toolbar.
****************************************************************************/
static struct editbar *editbar_create(void)
{
  struct editbar *eb;
  GtkWidget *hbox, *button, *combo, *image, *separator, *vbox;
  GtkRadioButton *radio_parent;
  GtkListStore *store;
  GtkCellRenderer *cell;
  GdkPixbuf *pixbuf;
  const struct editor_sprites *sprites;
  
  eb = fc_calloc(1, sizeof(struct editbar));

  hbox = gtk_hbox_new(FALSE, 4);
  eb->widget = hbox;
  eb->tooltips = gtk_tooltips_new();

  sprites = get_editor_sprites(tileset);

  /* Erase tool. */
  button = gtk_toggle_button_new();
  pixbuf = sprite_get_pixbuf(sprites->erase);
  image = gtk_image_new_from_pixbuf(pixbuf);
  gtk_container_add(GTK_CONTAINER(button), image);
  gtk_tooltips_set_tip(eb->tooltips, button,
                       _("Set Erase Mode"), "");
  gtk_widget_set_size_request(button, BWIDTH, BHEIGHT);
  gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
  gtk_button_set_focus_on_click(GTK_BUTTON(button), FALSE);
  g_signal_connect(button, "toggled",
      G_CALLBACK(editbar_erase_button_toggled), NULL);
  gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
  eb->erase_button = button;

  separator = gtk_vseparator_new();
  gtk_box_pack_start(GTK_BOX(hbox), separator, FALSE, FALSE, 0);

  button = create_editbar_radiobutton(eb, ETT_TERRAIN,
                                      NULL, hbox);
  radio_parent = GTK_RADIO_BUTTON(button);

  create_editbar_radiobutton(eb, ETT_TERRAIN_RESOURCE,
                             radio_parent, hbox);
  create_editbar_radiobutton(eb, ETT_TERRAIN_SPECIAL,
                             radio_parent, hbox);
  create_editbar_radiobutton(eb, ETT_MILITARY_BASE,
                             radio_parent, hbox);
  create_editbar_radiobutton(eb, ETT_UNIT,
                             radio_parent, hbox);
  create_editbar_radiobutton(eb, ETT_CITY,
                             radio_parent, hbox);
  create_editbar_radiobutton(eb, ETT_VISION,
                             radio_parent, hbox);
  create_editbar_radiobutton(eb, ETT_TERRITORY,
                             radio_parent, hbox);
  create_editbar_radiobutton(eb, ETT_STARTPOS,
                             radio_parent, hbox);

  separator = gtk_vseparator_new();
  gtk_box_pack_start(GTK_BOX(hbox), separator, FALSE, FALSE, 0);

  /* Player POV indicator. */
  store = gtk_list_store_new(PPV_NUM_COLS,
                             GDK_TYPE_PIXBUF,
                             G_TYPE_STRING);
  eb->player_pov_store = store;

  combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));

  cell = gtk_cell_renderer_pixbuf_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, FALSE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo),
                                cell, "pixbuf", PPV_COL_FLAG);

  cell = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo),
                                cell, "text", PPV_COL_NAME);

  gtk_tooltips_set_tip(eb->tooltips, combo,
                       _("Switch Point of View"), "");
  gtk_widget_set_size_request(combo, 140, -1);
  g_signal_connect(combo, "changed",
                   G_CALLBACK(editbar_player_pov_combobox_changed), eb);
  vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), combo, TRUE, FALSE, 0);
  eb->player_pov_combobox = combo;

  /* Player properties button. */
  button = gtk_button_new();
  pixbuf = sprite_get_pixbuf(sprites->properties);
  image = gtk_image_new_from_pixbuf(pixbuf);
  gtk_container_add(GTK_CONTAINER(button), image);
  gtk_tooltips_set_tip(eb->tooltips, button,
                       _("Modify Player"), "");
  gtk_widget_set_size_request(button, BWIDTH, BHEIGHT);
  gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
  gtk_button_set_focus_on_click(GTK_BUTTON(button), FALSE);
  g_signal_connect(button, "clicked",
      G_CALLBACK(editbar_player_properties_button_clicked), eb);
  gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
  eb->player_properties_button = button;

  return eb;
}

/****************************************************************************
  Refresh the tool value selector in the given toolbar for the given tool
  type with data from the editor state.
****************************************************************************/
static void refresh_tool_value_selector(struct editbar *eb,
                                        enum editor_tool_type ett)
{
  GtkTreeSelection *sel;
  GtkTreeIter iter;
  GtkTreeModel *model;
  int value, store_value;
  struct tool_value_selector *tvs;

  if (!editor_is_active() || !eb || !editor_tool_has_value(ett)) {
    return;
  }

  tvs = eb->tool_selectors[ett];

  if (!tvs) {
    return;
  }

  value = editor_tool_get_value(ett);
  model = GTK_TREE_MODEL(tvs->store);
  if (!gtk_tree_model_get_iter_first(model, &iter)) {
    return;
  }

  do {
    gtk_tree_model_get(model, &iter, TVS_COL_ID, &store_value, -1);
    if (value == store_value) {
      sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tvs->view));
      gtk_tree_selection_select_iter(sel, &iter);
      break;
    }
  } while (gtk_tree_model_iter_next(model, &iter));
}

/****************************************************************************
  Refresh all tool value selectors in the given toolbar according to the
  current editor state.
****************************************************************************/
static void refresh_all_tool_value_selectors(struct editbar *eb)
{
  int ett;

  if (!eb) {
    return;
  }

  for (ett = 0; ett < NUM_EDITOR_TOOL_TYPES; ett++) {
    if (editor_tool_has_value(ett)) {
      refresh_tool_value_selector(eb, ett);
    }
  }
}

/****************************************************************************
  Refresh the given toolbar according to the current editor state.
****************************************************************************/
static void editbar_refresh(struct editbar *eb)
{
  if (eb == NULL || eb->widget == NULL) {
    return;
  }

  if (!editor_is_active()) {
    gtk_widget_hide_all(eb->widget);
    return;
  }

  refresh_tool_buttons(eb);
  refresh_all_tool_value_selectors(eb);
  refresh_player_pov_indicator(eb);

  gtk_widget_show_all(eb->widget);
}

/****************************************************************************
  Create a pixbuf containing a representative image for the given base
  type, to be used as an icon in the GUI.

  May return NULL on error.

  NB: You must call g_object_unref on the non-NULL return value when you
  no longer need it.
****************************************************************************/
static GdkPixbuf *create_military_base_pixbuf(const struct base_type *pbase)
{
  struct drawn_sprite sprs[80];
  int count, w, h, canvas_x, canvas_y;
  GdkPixbuf *pixbuf;
  struct canvas canvas;

  w = tileset_tile_width(tileset);
  h = tileset_tile_height(tileset);

  pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
  if (pixbuf == NULL) {
    return NULL;
  }
  gdk_pixbuf_fill(pixbuf, 0x00000000);

  canvas.type = CANVAS_PIXBUF;
  canvas.v.pixbuf = pixbuf;
  canvas_x = 0;
  canvas_y = 0;

  count = fill_basic_base_sprite_array(tileset, sprs, pbase);
  put_drawn_sprites(&canvas, canvas_x, canvas_y, count, sprs, FALSE);

  return pixbuf;
}

/****************************************************************************
  Create a pixbuf containing a representative image for the given terrain
  type, to be used as an icon in the GUI.

  May return NULL on error.

  NB: You must call g_object_unref on the non-NULL return value when you
  no longer need it.
****************************************************************************/
static GdkPixbuf *create_terrain_pixbuf(struct terrain *pterrain)
{
  struct drawn_sprite sprs[80];
  int count, w, h, canvas_x, canvas_y, i;
  GdkPixbuf *pixbuf;
  struct canvas canvas;

  w = tileset_tile_width(tileset);
  h = tileset_tile_height(tileset);

  pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
  if (pixbuf == NULL) {
    return NULL;
  }
  gdk_pixbuf_fill(pixbuf, 0x00000000);

  canvas.type = CANVAS_PIXBUF;
  canvas.v.pixbuf = pixbuf;
  canvas_x = 0;
  canvas_y = 0;

  for (i = 0; i < 3; i++) {
    count = fill_basic_terrain_layer_sprite_array(tileset, sprs,
                                                  i, pterrain);
    put_drawn_sprites(&canvas, canvas_x, canvas_y, count, sprs, FALSE);
  }

  return pixbuf;
}

/****************************************************************************
  Reload all tool value data from the tileset for the given toolbar.
****************************************************************************/
static void editbar_reload_tileset(struct editbar *eb)
{
  GtkTreeIter iter;
  GtkListStore *store;
  GdkPixbuf *pixbuf;
  struct sprite *sprite;
  struct tool_value_selector *tvs;

  if (eb == NULL || tileset == NULL) {
    return;
  }


  /* Reload terrains. */

  tvs = eb->tool_selectors[ETT_TERRAIN];
  store = tvs->store;
  gtk_list_store_clear(store);

  terrain_type_iterate(pterrain) {
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       TVS_COL_ID, terrain_number(pterrain),
                       TVS_COL_NAME, terrain_name_translation(pterrain),
                       -1);
    pixbuf = create_terrain_pixbuf(pterrain);
    if (pixbuf != NULL) {
      gtk_list_store_set(store, &iter, TVS_COL_IMAGE, pixbuf, -1);
      g_object_unref(pixbuf);
    }
  } terrain_type_iterate_end;


  /* Reload terrain resources. */

  tvs = eb->tool_selectors[ETT_TERRAIN_RESOURCE];
  store = tvs->store;
  gtk_list_store_clear(store);

  resource_type_iterate(presource) {
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       TVS_COL_ID, resource_number(presource),
                       TVS_COL_NAME, resource_name_translation(presource),
                       -1);
    sprite = get_resource_sprite(tileset, presource);
    if (sprite == NULL) {
      continue;
    }
    pixbuf = sprite_get_pixbuf(sprite);
    if (pixbuf == NULL) {
      continue;
    }

    gtk_list_store_set(store, &iter, TVS_COL_IMAGE, pixbuf, -1);
  } resource_type_iterate_end;


  /* Reload terrain specials. */

  tvs = eb->tool_selectors[ETT_TERRAIN_SPECIAL];
  store = tvs->store;
  gtk_list_store_clear(store);

  tile_special_type_iterate(special) {
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       TVS_COL_ID, special,
                       TVS_COL_NAME, special_name_translation(special),
                       -1);
    sprite = get_basic_special_sprite(tileset, special);
    if (sprite == NULL) {
      continue;
    }
    pixbuf = sprite_get_pixbuf(sprite);
    if (pixbuf == NULL) {
      continue;
    }

    gtk_list_store_set(store, &iter, TVS_COL_IMAGE, pixbuf, -1);
  } tile_special_type_iterate_end;


  /* Reload military bases. */

  tvs = eb->tool_selectors[ETT_MILITARY_BASE];
  store = tvs->store;
  gtk_list_store_clear(store);

  base_type_iterate(pbase) {
    int id;

    id = base_number(pbase);

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       TVS_COL_ID, id,
                       TVS_COL_NAME, base_name_translation(pbase),
                       -1);
    pixbuf = create_military_base_pixbuf(pbase);
    if (pixbuf != NULL) {
      gtk_list_store_set(store, &iter, TVS_COL_IMAGE, pixbuf, -1);
      g_object_unref(pixbuf);
    }
  } base_type_iterate_end;


  /* Reload unit types. */

  tvs = eb->tool_selectors[ETT_UNIT];
  store = tvs->store;
  gtk_list_store_clear(store);

  unit_type_iterate(putype) {
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       TVS_COL_ID, utype_number(putype),
                       TVS_COL_NAME, utype_name_translation(putype),
                       -1);
    sprite = get_unittype_sprite(tileset, putype);
    if (sprite == NULL) {
      continue;
    }
    pixbuf = sprite_get_pixbuf(sprite);
    if (pixbuf == NULL) {
      continue;
    }

    gtk_list_store_set(store, &iter, TVS_COL_IMAGE, pixbuf, -1);
  } unit_type_iterate_end;
}

/****************************************************************************
  Convert gdk modifier values to editor modifier values.
****************************************************************************/
static int convert_modifiers(int gdk_event_state)
{
  int modifiers = EKM_NONE;

  if (gdk_event_state & GDK_SHIFT_MASK) {
    modifiers |= EKM_SHIFT;
  }
  if (gdk_event_state & GDK_CONTROL_MASK) {
    modifiers |= EKM_CTRL;
  }
  if (gdk_event_state & GDK_MOD1_MASK) {
    modifiers |= EKM_ALT;
  }

  return modifiers;
}

/****************************************************************************
  Convert gdk mouse button values to editor mouse button values.
****************************************************************************/
static int convert_mouse_button(int gdk_mouse_button)
{
  switch (gdk_mouse_button) {
  case 1:
    return MOUSE_BUTTON_LEFT;
    break;
  case 2:
    return MOUSE_BUTTON_MIDDLE;
    break;
  case 3:
    return MOUSE_BUTTON_RIGHT;
    break;
  default:
    break;
  }

  return MOUSE_BUTTON_OTHER;
}

/****************************************************************************
  Pass on the gdk mouse event to the editor's handler.
****************************************************************************/
gboolean handle_edit_mouse_button_press(GdkEventButton *ev)
{
  if (ev->type != GDK_BUTTON_PRESS) {
    return TRUE;
  }

  editor_mouse_button_press(ev->x, ev->y,
                            convert_mouse_button(ev->button),
                            convert_modifiers(ev->state));

  return TRUE;
}

/****************************************************************************
  Pass on the gdk mouse event to the editor's handler.
****************************************************************************/
gboolean handle_edit_mouse_button_release(GdkEventButton *ev)
{
  if (ev->type != GDK_BUTTON_RELEASE) {
    return TRUE;
  }

  editor_mouse_button_release(ev->x, ev->y,
                              convert_mouse_button(ev->button),
                              convert_modifiers(ev->state));
  return TRUE;
}

/****************************************************************************
  Pass on the gdk mouse event to the editor's handler.
****************************************************************************/
gboolean handle_edit_mouse_move(GdkEventMotion *ev)
{
  editor_mouse_move(ev->x, ev->y, convert_modifiers(ev->state));
  return TRUE;
}

/****************************************************************************
  Handle a double-click on the tool value list.
****************************************************************************/
static void tool_value_selector_treeview_row_activated(GtkTreeView *view,
                                                       GtkTreePath *path,
                                                       GtkTreeViewColumn *col,
                                                       gpointer user_data)
{
  struct tool_value_selector *tvs = user_data;

  gtk_dialog_response(GTK_DIALOG(tvs->dialog), GTK_RESPONSE_ACCEPT);
}

/****************************************************************************
  Create a tool value selection dialog for the given toolbar.
****************************************************************************/
static struct tool_value_selector *
create_tool_value_selector(struct editbar *eb,
                           enum editor_tool_type ett)
{
  struct tool_value_selector *tvs;
  GtkWidget *vbox, *view, *scrollwin;
  GtkCellRenderer *cell;
  GtkListStore *store;
  GtkTreeViewColumn *col;
  GtkTreeSelection *sel;

  tvs = fc_calloc(1, sizeof(struct tool_value_selector));

  tvs->editbar_parent = eb;

  tvs->dialog = gtk_dialog_new_with_buttons(_("Select Tool Value"),
      GTK_WINDOW(toplevel), GTK_DIALOG_MODAL,
      GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
      GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
      NULL);
  vbox = GTK_DIALOG(tvs->dialog)->vbox;

  store = gtk_list_store_new(TVS_NUM_COLS,
                             GDK_TYPE_PIXBUF,
                             G_TYPE_INT,
                             G_TYPE_STRING);
  tvs->store = store;

  scrollwin = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrollwin),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(vbox), scrollwin, TRUE, TRUE, 0);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(tvs->store));
  gtk_widget_set_size_request(view, -1, 10 * tileset_tile_height(tileset));
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);
  gtk_tree_view_set_search_column(GTK_TREE_VIEW(view), TVS_COL_NAME);
  g_signal_connect(view, "row-activated",
                   G_CALLBACK(tool_value_selector_treeview_row_activated), tvs);

  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);

  cell = gtk_cell_renderer_pixbuf_new();
  col = gtk_tree_view_column_new_with_attributes(editor_tool_get_name(ett),
                                                 cell, "pixbuf",
                                                 TVS_COL_IMAGE, NULL);
  gtk_tree_view_column_set_resizable(col, FALSE);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_reorderable(col, FALSE);
  gtk_tree_view_column_set_clickable(col, FALSE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  cell = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes("ID", cell,
                                                 "text", TVS_COL_ID, NULL);
  gtk_tree_view_column_set_resizable(col, FALSE);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_reorderable(col, FALSE);
  gtk_tree_view_column_set_clickable(col, FALSE);
  gtk_tree_view_column_set_sort_column_id(col, TVS_COL_ID);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  cell = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes("Name", cell,
                                                 "text", TVS_COL_NAME, NULL);
  gtk_tree_view_column_set_resizable(col, FALSE);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_reorderable(col, FALSE);
  gtk_tree_view_column_set_clickable(col, FALSE);
  gtk_tree_view_column_set_sort_column_id(col, TVS_COL_NAME);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  gtk_container_add(GTK_CONTAINER(scrollwin), view);
  tvs->view = view;

  /* Show everything but the window itself. */
  gtk_widget_show_all(vbox);

  return tvs;
}

/****************************************************************************
  Handle a mouse click on the tool image area in the editor info box.
****************************************************************************/
static gboolean editinfobox_handle_tool_image_button_press(GtkWidget *evbox,
                                                           GdkEventButton *ev,
                                                           gpointer data)
{
  editgui_run_tool_selection(editor_get_tool());
  return TRUE;
}

/****************************************************************************
  Handle a mouse click on the mode image area in the editor info box.
****************************************************************************/
static gboolean editinfobox_handle_mode_image_button_press(GtkWidget *evbox,
                                                           GdkEventButton *ev,
                                                           gpointer data)
{
  editor_set_erase_mode(!editor_get_erase_mode());
  editgui_refresh();
  return TRUE;
}

/****************************************************************************
  Callback for spin button changes in the editor info box.
****************************************************************************/
static void editinfobox_spin_button_value_changed(GtkSpinButton *spinbutton,
                                                  gpointer userdata)
{
  struct editinfobox *ei;
  int which, value;
  
  ei = editgui_get_editinfobox();

  if (!ei) {
    return;
  }

  value = gtk_spin_button_get_value_as_int(spinbutton);
  which = GPOINTER_TO_INT(userdata);

  switch (which) {
  case SPIN_BUTTON_SIZE:
    editor_set_size(value);
    break;
  case SPIN_BUTTON_COUNT:
    editor_set_count(value);
    break;
  default:
    return;
    break;
  }

  editinfobox_refresh(ei);
}

/****************************************************************************
  Callback for changes in the applied player combobox in the editor
  info box.
****************************************************************************/
static void editinfobox_tool_applied_player_changed(GtkComboBox *combo,
                                                    gpointer userdata)
{
  struct editinfobox *ei = userdata;
  GtkTreeIter iter;
  GtkTreeModel *model;
  int player_no = -1;

  if (ei == NULL) {
    return;
  }

  if (!gtk_combo_box_get_active_iter(combo, &iter)) {
    return;
  }

  model = gtk_combo_box_get_model(combo);
  gtk_tree_model_get(model, &iter, TAP_COL_PLAYER_NO, &player_no, -1);

  editor_set_applied_player(player_no);
}

/****************************************************************************
  Create and return an editor info box widget bundle.
****************************************************************************/
static struct editinfobox *editinfobox_create(void)
{
  GtkWidget *label, *vbox, *frame, *hbox, *vbox2, *image, *evbox;
  GtkWidget *spin, *combo;
  GtkListStore *store;
  GtkCellRenderer *cell;
  struct editinfobox *ei;
  char buf[128];

  ei = fc_calloc(1, sizeof(*ei));
  ei->tooltips = gtk_tooltips_new();

  frame = gtk_frame_new(_("Editor Tool"));
  gtk_container_set_border_width(GTK_CONTAINER(frame), 4);
  ei->widget = frame;

  vbox = gtk_vbox_new(FALSE, 8);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  /* tool section */
  hbox = gtk_hbox_new(FALSE, 8);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  evbox = gtk_event_box_new();
  g_signal_connect(evbox, "button_press_event",
      G_CALLBACK(editinfobox_handle_tool_image_button_press), NULL);
  gtk_box_pack_start(GTK_BOX(hbox), evbox, FALSE, FALSE, 0);

  image = gtk_image_new();
  gtk_tooltips_set_tip(ei->tooltips, image,
                       _("Click to change value if applicable."), "");
  gtk_container_add(GTK_CONTAINER(evbox), image);
  ei->tool_image = image;

  vbox2 = gtk_vbox_new(FALSE, 4);
  gtk_box_pack_start(GTK_BOX(hbox), vbox2, FALSE, FALSE, 0);

  label = gtk_label_new(NULL);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);
  ei->tool_label = label;

  label = gtk_label_new(NULL);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);
  ei->tool_value_label = label;

  /* mode section */
  hbox = gtk_hbox_new(FALSE, 8);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  evbox = gtk_event_box_new();
  g_signal_connect(evbox, "button_press_event",
      G_CALLBACK(editinfobox_handle_mode_image_button_press), NULL);
  gtk_box_pack_start(GTK_BOX(hbox), evbox, FALSE, FALSE, 0);

  image = gtk_image_new();
  gtk_tooltips_set_tip(ei->tooltips, image,
                       _("Toggle erase/paint mode."), "");
  gtk_container_add(GTK_CONTAINER(evbox), image);
  ei->mode_image = image;

  vbox2 = gtk_vbox_new(FALSE, 4);
  gtk_box_pack_start(GTK_BOX(hbox), vbox2, FALSE, FALSE, 0);

  label = gtk_label_new(NULL);
  my_snprintf(buf, sizeof(buf), "<span weight=\"bold\">%s</span>",
              _("Mode"));
  gtk_label_set_markup(GTK_LABEL(label), buf);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);

  label = gtk_label_new(NULL);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);
  ei->mode_label = label;

  /* spinner section */
  hbox = gtk_hbox_new(FALSE, 8);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
  ei->size_hbox = hbox;
  spin = gtk_spin_button_new_with_range(1, 255, 1);
  g_signal_connect(spin, "value-changed",
                   G_CALLBACK(editinfobox_spin_button_value_changed),
                   GINT_TO_POINTER(SPIN_BUTTON_SIZE));
  gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 0);
  ei->size_spin_button = spin;
  label = gtk_label_new("Size");
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
  
  hbox = gtk_hbox_new(FALSE, 8);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
  ei->count_hbox = hbox;
  spin = gtk_spin_button_new_with_range(1, 255, 1);
  g_signal_connect(spin, "value-changed",
                   G_CALLBACK(editinfobox_spin_button_value_changed),
                   GINT_TO_POINTER(SPIN_BUTTON_COUNT));
  gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 0);
  ei->count_spin_button = spin;
  label = gtk_label_new("Count");
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

  /* combo section */
  store = gtk_list_store_new(TAP_NUM_COLS,
                             GDK_TYPE_PIXBUF,
                             G_TYPE_STRING,
                             G_TYPE_INT);
  ei->tool_applied_player_store = store;
  combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));

  cell = gtk_cell_renderer_pixbuf_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, FALSE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo),
                                cell, "pixbuf", TAP_COL_FLAG);

  cell = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo),
                                cell, "text", TAP_COL_NAME);

  gtk_tooltips_set_tip(ei->tooltips, combo,
                       _("Apply tool as this player."), "");
  gtk_widget_set_size_request(combo, 132, -1);
  g_signal_connect(combo, "changed",
                   G_CALLBACK(editinfobox_tool_applied_player_changed), ei);
  gtk_box_pack_start(GTK_BOX(vbox), combo, FALSE, FALSE, 0);
  ei->tool_applied_player_combobox = combo;

  /* We add a ref to the editinfobox widget so that it is
   * not destroyed when replaced by the unit info box when
   * we leave edit mode. See editinfobox_refresh(). */
  g_object_ref(ei->widget);

  /* The edit info box starts with no parent, so we have to
   * show its internal widgets manually. */
  gtk_widget_show_all(ei->widget);

  return ei;
}

/****************************************************************************
  Refresh the given editinfobox's applied player combobox according to the
  current editor state.
****************************************************************************/
static void refresh_tool_applied_player_combo(struct editinfobox *ei)
{
  enum editor_tool_type ett;
  GtkListStore *store;
  GtkWidget *combo;
  GtkTreeIter iter;
  GdkPixbuf *flag;
  int apno, index, i;

  if (!ei) {
    return;
  }

  ett = editor_get_tool();
  combo = ei->tool_applied_player_combobox;
  store = ei->tool_applied_player_store;

  if (!combo || !store) {
    return;
  }

  if (!editor_tool_has_applied_player(ett)) {
    gtk_widget_hide(combo);
    return;
  }

  gtk_list_store_clear(store);

  apno = editor_get_applied_player();
  index = -1;
  i = 0;

  players_iterate(pplayer) {
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       TAP_COL_PLAYER_NO, player_number(pplayer),
                       TAP_COL_NAME, player_name(pplayer), -1);

    if (pplayer->nation != NO_NATION_SELECTED) {
      flag = get_flag(pplayer->nation);

      if (flag != NULL) {
        gtk_list_store_set(store, &iter, TAP_COL_FLAG, flag, -1);
        g_object_unref(flag);
      }
    }
    if (apno == player_number(pplayer)) {
      index = i;
    }
    i++;
  } players_iterate_end;

  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
  gtk_widget_show(combo);
}

/****************************************************************************
  Return a pixbuf containing an image for the given editor tool sub-value,
  if one exists.

  NB: Can return NULL. Must call g_object_unref on non-NULL when done.
****************************************************************************/
static GdkPixbuf *get_tool_value_pixbuf(enum editor_tool_type ett,
                                        int value)
{
  struct sprite *sprite = NULL;
  GdkPixbuf *pixbuf = NULL;
  struct terrain *pterrain;
  struct resource *presource;
  struct unit_type *putype;
  struct base_type *pbase;
  const struct editor_sprites *sprites;

  sprites = get_editor_sprites(tileset);
  if (!sprites) {
    return NULL;
  }

  switch (ett) {
  case ETT_TERRAIN:
    pterrain = terrain_by_number(value);
    if (pterrain) {
      pixbuf = create_terrain_pixbuf(pterrain);
    }
    break;
  case ETT_TERRAIN_RESOURCE:
    presource = resource_by_number(value);
    if (presource) {
      sprite = get_resource_sprite(tileset, presource);
    }
    break;
  case ETT_TERRAIN_SPECIAL:
    sprite = get_basic_special_sprite(tileset, value);
    break;
  case ETT_MILITARY_BASE:
    pbase = base_by_number(value);
    pixbuf = create_military_base_pixbuf(pbase);
    break;
  case ETT_UNIT:
    putype = utype_by_number(value);
    if (putype) {
      sprite = get_unittype_sprite(tileset, putype);
    }
    break;
  case ETT_CITY:
    sprite = sprites->city;
    break;
  case ETT_VISION:
    sprite = sprites->vision;
    break;
  case ETT_TERRITORY:
    sprite = sprites->territory;
    break;
  case ETT_STARTPOS:
    sprite = sprites->startpos;
    break;
  default:
    break;
  }

  if (sprite) {
    pixbuf = sprite_get_pixbuf(sprite);
    if (pixbuf) {
      g_object_ref(pixbuf);
    }
    sprite = NULL;
  }
  
  return pixbuf;
}

/****************************************************************************
  Return a pixbuf containing an image suitable for use as an icon
  respresenting 'erase mode'.

  NB: Can return NULL. Must call g_object_unref on non-NULL when done.
****************************************************************************/
static GdkPixbuf *get_erase_pixbuf(void)
{
  struct sprite *sprite = NULL;
  GdkPixbuf *pixbuf = NULL;
  const struct editor_sprites *sprites;

  sprites = get_editor_sprites(tileset);
  if (!sprites) {
    return NULL;
  }

  sprite = sprites->erase;

  if (sprite) {
    pixbuf = sprite_get_pixbuf(sprite);
    if (pixbuf) {
      g_object_ref(pixbuf);
    }
    sprite = NULL;
  }

  return pixbuf;
}

/****************************************************************************
  Return a pixbuf representing a brush icon.

  NB: Can return NULL. Must call g_object_unref on non-NULL when done.
****************************************************************************/
static GdkPixbuf *get_brush_pixbuf(void)
{
  struct sprite *sprite = NULL;
  GdkPixbuf *pixbuf = NULL;
  const struct editor_sprites *sprites;

  sprites = get_editor_sprites(tileset);
  if (!sprites) {
    return NULL;
  }

  sprite = sprites->brush;

  if (sprite) {
    pixbuf = sprite_get_pixbuf(sprite);
    if (pixbuf) {
      g_object_ref(pixbuf);
    }
    sprite = NULL;
  }

  return pixbuf;
}

/****************************************************************************
  NB: Assumes that widget 'old' has enough references to not be destroyed
  when removed from its parent container, and that the parent container
  is a GtkBox (or is descended from it).
****************************************************************************/
static void replace_widget(GtkWidget *old, GtkWidget *new)
{
  GtkWidget *parent;

  parent = gtk_widget_get_parent(old);
  if (!parent) {
    return;
  }

  gtk_container_remove(GTK_CONTAINER(parent), old);
  gtk_box_pack_start(GTK_BOX(parent), new, FALSE, FALSE, 0);
}

/****************************************************************************
  Refresh the given editinfobox according to the current editor state.
****************************************************************************/
static void editinfobox_refresh(struct editinfobox *ei)
{
  GdkPixbuf *pixbuf = NULL;
  enum editor_tool_type ett;
  int value;
  bool erase, value_erase;
  char buf[256];

  if (ei == NULL) {
    return;
  }

  if (!editor_is_active()) {
    replace_widget(ei->widget, unit_info_box);
    return;
  }

  erase = editor_get_erase_mode();
  ett = editor_get_tool();
  value = editor_tool_get_value(ett);
  value_erase = editor_tool_has_value_erase(ett);

  if (erase) {
    if (value_erase) {
      gtk_label_set_text(GTK_LABEL(ei->mode_label),
                         _("Erase Value"));
    } else {
      gtk_label_set_text(GTK_LABEL(ei->mode_label),
                         _("Erase"));
    }
    pixbuf = get_erase_pixbuf();
  } else {
    gtk_label_set_text(GTK_LABEL(ei->mode_label),
                       _("Paint"));
    pixbuf = get_brush_pixbuf();
  }

  gtk_image_set_from_pixbuf(GTK_IMAGE(ei->mode_image), pixbuf);
  if (pixbuf) {
    g_object_unref(pixbuf);
    pixbuf = NULL;
  }

  pixbuf = get_tool_value_pixbuf(ett, value);

  gtk_image_set_from_pixbuf(GTK_IMAGE(ei->tool_image), pixbuf);
  if (pixbuf) {
    g_object_unref(pixbuf);
    pixbuf = NULL;
  }

  my_snprintf(buf, sizeof(buf), "<span weight=\"bold\">%s</span>",
              editor_tool_get_name(ett));
  gtk_label_set_markup(GTK_LABEL(ei->tool_label), buf);
  gtk_label_set_text(GTK_LABEL(ei->tool_value_label),
                     editor_tool_get_value_name(ett, value));

  if (editor_tool_has_size(ett)) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ei->size_spin_button),
                              editor_get_size());
    gtk_widget_show(ei->size_hbox);
  } else {
    gtk_widget_hide(ei->size_hbox);
  }

  if (editor_tool_has_count(ett)) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ei->count_spin_button),
                              editor_get_count());
    gtk_widget_show(ei->count_hbox);
  } else {
    gtk_widget_hide(ei->count_hbox);
  }

  refresh_tool_applied_player_combo(ei);

  replace_widget(unit_info_box, ei->widget);
}

/****************************************************************************
  Handle ctrl+<key> combinations.
****************************************************************************/
static gboolean handle_edit_key_press_with_ctrl(GdkEventKey *ev)
{
  return FALSE; /* Don't gobble. */
}

/****************************************************************************
  Handle shift+<key> combinations.
****************************************************************************/
static gboolean handle_edit_key_press_with_shift(GdkEventKey *ev)
{
  switch (ev->keyval) {
  case GDK_D:
    editor_toggle_erase_mode();
    break;
  case GDK_T:
    editgui_run_tool_selection(ETT_TERRAIN);
    break;
  case GDK_R:
    editgui_run_tool_selection(ETT_TERRAIN_RESOURCE);
    break;
  case GDK_S:
    editgui_run_tool_selection(ETT_TERRAIN_SPECIAL);
    break;
  case GDK_M:
    editgui_run_tool_selection(ETT_MILITARY_BASE);
    break;
  case GDK_U:
    editgui_run_tool_selection(ETT_UNIT);
    break;
  default:
    return TRUE; /* Gobble? */
    break;
  }

  editbar_refresh(editor_toolbar);

  return TRUE;
}

/****************************************************************************
  Handle any kind of key press event.
****************************************************************************/
gboolean handle_edit_key_press(GdkEventKey *ev)
{
  int ett = -1;

  if (ev->state & GDK_SHIFT_MASK) {
    return handle_edit_key_press_with_shift(ev);
  }

  if (ev->state & GDK_CONTROL_MASK) {
    return handle_edit_key_press_with_ctrl(ev);
  }

  switch (ev->keyval) {
  case GDK_t:
    ett = ETT_TERRAIN;
    break;
  case GDK_r:
    ett = ETT_TERRAIN_RESOURCE;
    break;
  case GDK_s:
    ett = ETT_TERRAIN_SPECIAL;
    break;
  case GDK_m:
    ett = ETT_MILITARY_BASE;
    break;
  case GDK_u:
    ett = ETT_UNIT;
    break;
  case GDK_c:
    ett = ETT_CITY;
    break;
  case GDK_v:
    ett = ETT_VISION;
    break;
  case GDK_b:
    ett = ETT_TERRITORY;
    break;
  case GDK_p:
    ett = ETT_STARTPOS;
    break;
  case GDK_plus:
  case GDK_equal:
  case GDK_KP_Add:
    if (editor_tool_has_size(editor_get_tool())) {
      editor_set_size(editor_get_size() + 1);
      editinfobox_refresh(editgui_get_editinfobox());
    } else if (editor_tool_has_count(editor_get_tool())) {
      editor_set_count(editor_get_count() + 1);
      editinfobox_refresh(editgui_get_editinfobox());
    }
    break;
  case GDK_minus:
  case GDK_underscore:
  case GDK_KP_Subtract:
    if (editor_tool_has_size(editor_get_tool())) {
      editor_set_size(editor_get_size() - 1);
      editinfobox_refresh(editgui_get_editinfobox());
    } else if (editor_tool_has_count(editor_get_tool())) {
      editor_set_count(editor_get_count() - 1);
      editinfobox_refresh(editgui_get_editinfobox());
    }
    break;
  case GDK_1:
  case GDK_2:
  case GDK_3:
  case GDK_4:
  case GDK_5:
  case GDK_6:
  case GDK_7:
  case GDK_8:
  case GDK_9:
    if (editor_tool_has_size(editor_get_tool())) {
      editor_set_size(ev->keyval - GDK_1 + 1);
      editinfobox_refresh(editgui_get_editinfobox());
    } else if (editor_tool_has_count(editor_get_tool())) {
      editor_set_count(ev->keyval - GDK_1 + 1);
      editinfobox_refresh(editgui_get_editinfobox());
    }
    break;
  case GDK_space:
    editor_apply_tool_to_selection();
    break;
  case GDK_Tab:
    editgui_run_tool_selection(editor_get_tool());
    break;
  case GDK_F1:
  case GDK_F2:
  case GDK_F3:
  case GDK_F4:
  case GDK_F5:
  case GDK_F6:
  case GDK_F7:
  case GDK_F8:
  case GDK_F9:
  case GDK_F10:
  case GDK_F11:
  case GDK_F12:
    return FALSE; /* Allow default handler. */
    break;
  default:
    return TRUE; /* Gobbled... */
    break;
  }

  if (ett != -1) {
    editor_set_tool(ett);
    editor_apply_tool_to_selection();
    editbar_refresh(editgui_get_editbar());
  }

  return TRUE;
}

/****************************************************************************
  Key release handler.
****************************************************************************/
gboolean handle_edit_key_release(GdkEventKey *ev)
{
  return FALSE;
}

/****************************************************************************
  Get the pointer for the editbar embedded in the client's GUI.
****************************************************************************/
struct editbar *editgui_get_editbar(void)
{
  return editor_toolbar;
}

/****************************************************************************
  Refresh all editor GUI widgets according to the current editor state.
****************************************************************************/
void editgui_refresh(void)
{
  if (editor_toolbar != NULL) {
    editbar_refresh(editor_toolbar);
  }
  if (editor_infobox != NULL) {
    editinfobox_refresh(editor_infobox);
  }
}

/****************************************************************************
  Create all editor GUI widgets.
****************************************************************************/
void editgui_create_widgets(void)
{
  if (editor_toolbar == NULL) {
    editor_toolbar = editbar_create();
  }
  if (editor_infobox == NULL) {
    editor_infobox = editinfobox_create();
  }
}

/****************************************************************************
  Return a pointer to the editor info box embedded in the client's GUI.
****************************************************************************/
struct editinfobox *editgui_get_editinfobox(void)
{
  return editor_infobox;
}

/****************************************************************************
  Update all editor widget internal data for the new tileset. Call this
  after a new tileset has finished loading.
****************************************************************************/
void editgui_tileset_changed(void)
{
  editbar_reload_tileset(editgui_get_editbar());
  editinfobox_refresh(editgui_get_editinfobox());
}

/****************************************************************************
  Popup the property editor on the given list of tiles.
****************************************************************************/
void editgui_popup_properties(const struct tile_list *tiles)
{
  struct property_editor *pe;

  if (!tiles || tile_list_size(tiles) < 1) {
    return;
  }

  pe = editprop_get_property_editor();
  property_editor_clear(pe);
  property_editor_load_tiles(pe, tiles);
  property_editor_popup(pe);
}

/****************************************************************************
  This is called to notify the editor GUI that some object (e.g. tile, unit,
  etc.) has changed (usually because the corresponding packet was received)
  and that widgets displaying the object should be updated.
  
  Currently this is used to notify the property editor that some object
  has been removed or some property value has changed at the server.
****************************************************************************/
void editgui_notify_object_changed(int objtype, int object_id, bool remove)
{
  struct property_editor *pe;

  pe = editprop_get_property_editor();
  if (!pe) {
    return;
  }

  property_editor_handle_object_changed(pe, objtype, object_id, remove);
}
