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

#include <gtk/gtk.h>

#include "fcintl.h"
#include "mem.h"

#include "tile.h"

#include "tilespec.h"

#include "gui_main.h"

#include "editprop.h"

enum tile_properties_page_columns {
  TPP_COL_IMAGE = 0,
  TPP_COL_NAME,
  TPP_COL_ID,

  TPP_NUM_COLS
};

struct tile_properties_page {
  GtkWidget *widget;
  GtkListStore *tile_store;
  GtkWidget *tile_view;
};

struct unit_properties_page {
  GtkWidget *widget;
};

struct city_properties_page {
  GtkWidget *widget;
};

struct property_editor {
  GtkWidget *dialog;
  GtkWidget *notebook;

  struct tile_properties_page *tile_page;
  struct unit_properties_page *unit_page;
  struct city_properties_page *city_page;
};

static struct property_editor *editor_property_editor;

/****************************************************************************
  Create and add the tile property page to the property editor.
****************************************************************************/
static void add_tile_properties_page(struct property_editor *pe)
{
  struct tile_properties_page *tpp;
  GtkWidget *hbox, *label, *view, *scrollwin;
  GtkCellRenderer *cell;
  GtkListStore *store;
  GtkTreeViewColumn *col;
  GtkTreeSelection *sel;

  if (!pe || !pe->notebook) {
    return;
  }
  
  tpp = fc_calloc(1, sizeof(*tpp));
  
  hbox = gtk_hbox_new(FALSE, 0);
  tpp->widget = hbox;

  store = gtk_list_store_new(TPP_NUM_COLS,
                             GDK_TYPE_PIXBUF,
                             G_TYPE_STRING,
                             G_TYPE_INT);
  tpp->tile_store = store;

  scrollwin = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrollwin),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(hbox), scrollwin, TRUE, TRUE, 0);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);

  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);

  cell = gtk_cell_renderer_pixbuf_new();
  col = gtk_tree_view_column_new_with_attributes(_("Terrain"),
                                                 cell, "pixbuf",
                                                 TPP_COL_IMAGE, NULL);
  gtk_tree_view_column_set_resizable(col, FALSE);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_reorderable(col, FALSE);
  gtk_tree_view_column_set_clickable(col, FALSE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  cell = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes("Name", cell,
                                                 "text", TPP_COL_NAME, NULL);
  gtk_tree_view_column_set_resizable(col, FALSE);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_reorderable(col, FALSE);
  gtk_tree_view_column_set_clickable(col, FALSE);
  gtk_tree_view_column_set_sort_column_id(col, TPP_COL_NAME);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  cell = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes("ID", cell,
                                                 "text", TPP_COL_ID, NULL);
  gtk_tree_view_column_set_resizable(col, FALSE);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_reorderable(col, FALSE);
  gtk_tree_view_column_set_clickable(col, FALSE);
  gtk_tree_view_column_set_sort_column_id(col, TPP_COL_ID);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  gtk_container_add(GTK_CONTAINER(scrollwin), view);
  tpp->tile_view = view;

  label = gtk_label_new(_("Terrain"));
  gtk_notebook_append_page(GTK_NOTEBOOK(pe->notebook),
                           tpp->widget, label);
  pe->tile_page = tpp;
}

/****************************************************************************
  Create and add the unit property page to the property editor.
****************************************************************************/
static void add_unit_properties_page(struct property_editor *pe)
{
  struct unit_properties_page *upp;
  GtkWidget *hbox, *label;

  if (!pe || !pe->notebook) {
    return;
  }
  
  upp = fc_calloc(1, sizeof(*upp));
  
  hbox = gtk_hbox_new(FALSE, 0);
  upp->widget = hbox;

  label = gtk_label_new(_("Units"));
  gtk_notebook_append_page(GTK_NOTEBOOK(pe->notebook),
                           upp->widget, label);
  pe->unit_page = upp;
}

/****************************************************************************
  Create and add the city property page to the property editor.
****************************************************************************/
static void add_city_properties_page(struct property_editor *pe)
{
  struct city_properties_page *cpp;
  GtkWidget *hbox, *label;

  if (!pe || !pe->notebook) {
    return;
  }
  
  cpp = fc_calloc(1, sizeof(*cpp));
  
  hbox = gtk_hbox_new(FALSE, 0);
  cpp->widget = hbox;

  label = gtk_label_new(_("City"));
  gtk_notebook_append_page(GTK_NOTEBOOK(pe->notebook),
                           cpp->widget, label);
  pe->city_page = cpp;
}

/****************************************************************************
  Create and return the property editor widget bundle.
****************************************************************************/
static struct property_editor *create_property_editor(void)
{
  struct property_editor *pe;
  GtkWidget *dialog, *notebook, *vbox;

  pe = fc_calloc(1, sizeof(*pe));

  dialog = gtk_dialog_new_with_buttons(_("Property Editor"),
      GTK_WINDOW(toplevel), GTK_DIALOG_MODAL,
      GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
      NULL);
  gtk_widget_set_size_request(dialog, 400, 300);
  pe->dialog = dialog;

  vbox = GTK_DIALOG(dialog)->vbox;

  notebook = gtk_notebook_new();
  gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);
  pe->notebook = notebook;

  add_tile_properties_page(pe);
  add_unit_properties_page(pe);
  add_city_properties_page(pe);

  gtk_widget_show_all(notebook);

  return pe;
}

/****************************************************************************
  Get the property editor for the client's GUI.
****************************************************************************/
struct property_editor *editprop_get_property_editor(void)
{
  if (!editor_property_editor) {
    editor_property_editor = create_property_editor();
  }
  return editor_property_editor;
}

/****************************************************************************
  Create a pixbuf containing an image of the given tile. The image will
  only be of the layers containing terrains, resources and specials.

  May return NULL on error or bad input.
  NB: You must call g_object_unref on the non-NULL return value when you
  no longer need it.
****************************************************************************/
static GdkPixbuf *create_tile_pixbuf(struct tile *ptile)
{
  struct drawn_sprite sprs[80];
  struct sprite *sprite;
  int count, w, h, i, j, ox, oy, sw, sh;
  GdkPixbuf *src_pixbuf, *dest_pixbuf;
  const int const layers[] = {
    LAYER_BACKGROUND,
    LAYER_TERRAIN1,
    LAYER_TERRAIN2,
    LAYER_TERRAIN3,
    LAYER_WATER,
    LAYER_ROADS,
    LAYER_SPECIAL1,
    LAYER_SPECIAL2,
    LAYER_SPECIAL3
  };

  if (!ptile) {
    return NULL;
  }

  w = tileset_full_tile_width(tileset);
  h = tileset_full_tile_height(tileset);

  dest_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
  if (!dest_pixbuf) {
    return NULL;
  }

  gdk_pixbuf_fill(dest_pixbuf, 0x00000000);

  for (i = 0; i < ARRAY_SIZE(layers); i++) {
    count = fill_sprite_array(tileset, sprs, layers[i],
                              ptile, NULL, NULL, NULL, NULL, FALSE);
    for (j = 0; j < count; j++) {
      sprite = sprs[j].sprite;
      if (!sprite) {
        continue;
      }
      src_pixbuf = sprite_get_pixbuf(sprite);
      if (!src_pixbuf) {
        continue;
      }
      ox = sprs[j].offset_x;
      oy = sprs[j].offset_y;
      sw = sprite->width;
      sh = sprite->height;
      gdk_pixbuf_composite(src_pixbuf, dest_pixbuf, ox, oy, sw, sh,
                           ox, oy, 1.0, 1.0, GDK_INTERP_NEAREST, 255);
    }
  }

  return dest_pixbuf;
}

/****************************************************************************
  Fill the supplied buffer with a name for the given tile. The name is
  a function of the tile's terrain, resource, and specials.
****************************************************************************/
static void make_tile_name(char *buf, int buflen, const struct tile *ptile)
{
  struct terrain *pterrain;
  struct resource *presource;
  const char *name;

  if (buflen < 1) {
    return;
  }
  if (!ptile) {
    buf[0] = '\0';
    return;
  }

  pterrain = tile_terrain(ptile);
  presource = tile_resource(ptile);
  name = pterrain ? terrain_name_translation(pterrain) : _("Unknown");
  
  if (presource) {
    my_snprintf(buf, buflen, "%s (%s)", name,
                resource_name_translation(presource));
  } else {
    mystrlcpy(buf, name, buflen);
  }
}

/****************************************************************************
  Refresh the tile property page according to the supplied tile list.
****************************************************************************/
static void tile_properties_page_refresh(struct tile_properties_page *tpp,
                                         const struct tile_list *tiles)
{
  GtkListStore *store;
  GtkTreeIter iter;
  char buf[256];
  GdkPixbuf *pixbuf;

  if (!tpp || !tiles) {
    return;
  }

  store = tpp->tile_store;
  gtk_list_store_clear(store);

  tile_list_iterate(tiles, ptile) {
    if (!ptile) {
      continue;
    }
    gtk_list_store_append(store, &iter);

    make_tile_name(buf, sizeof(buf), ptile);
    gtk_list_store_set(store, &iter,
                       TPP_COL_NAME, buf,
                       TPP_COL_ID, tile_index(ptile),
                       -1);

    pixbuf = create_tile_pixbuf(ptile);
    if (pixbuf) {
      gtk_list_store_set(store, &iter, TPP_COL_IMAGE, pixbuf, -1);
      g_object_unref(pixbuf);
    }
  } tile_list_iterate_end;
}

/****************************************************************************
  Refresh the city property page according to the supplied tile list.
****************************************************************************/
static void city_properties_page_refresh(struct city_properties_page *cpp,
                                         const struct tile_list *tiles)
{
  if (!cpp || !tiles) {
    return;
  }

}

/****************************************************************************
  Refresh the unit property page according to the supplied tile list.
****************************************************************************/
static void unit_properties_page_refresh(struct unit_properties_page *upp,
                                         const struct tile_list *tiles)
{
  if (!upp || !tiles) {
    return;
  }

}

/****************************************************************************
  Refresh the given property editor according to the given list of tiles.
****************************************************************************/
void property_editor_refresh(struct property_editor *pe,
                             const struct tile_list *tiles)
{
  bool has_cities = FALSE, has_units = FALSE;

  if (!pe || !tiles) {
    return;
  }

  tile_properties_page_refresh(pe->tile_page, tiles);

  tile_list_iterate(tiles, ptile) {
    if (!ptile) {
      continue;
    }
    if (!has_cities && tile_city(ptile)) {
      has_cities = TRUE;
    }
    if (!has_units && ptile->units && unit_list_size(ptile->units) > 0) {
      has_units = TRUE;
    }
    if (has_cities && has_units) {
      break;
    }
  } tile_list_iterate_end;

  if (has_cities) {
    city_properties_page_refresh(pe->city_page, tiles);
    gtk_widget_show(pe->city_page->widget);
  } else {
    gtk_widget_hide(pe->city_page->widget);
  }

  if (has_units) {
    unit_properties_page_refresh(pe->unit_page, tiles);
    gtk_widget_show(pe->unit_page->widget);
  } else {
    gtk_widget_hide(pe->unit_page->widget);
  }
}

/****************************************************************************
  Run the given property editor as a modal dialog.
****************************************************************************/
void property_editor_run(struct property_editor *pe)
{
  int res;

  if (!pe || !pe->dialog) {
    return;
  }

  res = gtk_dialog_run(GTK_DIALOG(pe->dialog));
  gtk_widget_hide(pe->dialog);
}
