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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "mem.h"

#include "options.h"
#include "gui_main.h"

#include "gui_stuff.h"

/**************************************************************************
...
**************************************************************************/
void gtk_expose_now(GtkWidget *w)
{
  gtk_widget_queue_draw(w);
}

/**************************************************************************
...
**************************************************************************/
void gtk_set_relative_position(GtkWidget *ref, GtkWidget *w, int px, int py)
{
  gint x, y, width, height;

  gtk_window_get_position(GTK_WINDOW(ref), &x, &y);
  gtk_window_get_size(GTK_WINDOW(ref), &width, &height);

  x += px*width/100;
  y += py*height/100;

  gtk_window_move(GTK_WINDOW(w), x, y);
}

/**************************************************************************
...
**************************************************************************/
GtkWidget *gtk_stockbutton_new(const gchar *stock, const gchar *label_text)
{
  GtkWidget *label;
  GtkWidget *image;
  GtkWidget *hbox;
  GtkWidget *align;
  GtkWidget *button;
  
  button = gtk_button_new();

  label = gtk_label_new_with_mnemonic(label_text);
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);

  image = gtk_image_new_from_stock(stock, GTK_ICON_SIZE_BUTTON);
  hbox = gtk_hbox_new(FALSE, 2);

  align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);

  gtk_box_pack_start(GTK_BOX (hbox), image, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX (hbox), label, FALSE, FALSE, 0);

  gtk_container_add(GTK_CONTAINER(button), align);
  gtk_container_add(GTK_CONTAINER(align), hbox);
  gtk_widget_show_all(align);
  return button;
}

/**************************************************************************
  Returns gettext-converted list of n strings.  The individual strings
  in the list are as returned by gettext().  In case of no NLS, the strings
  will be the original strings, so caller should ensure that the originals
  persist for as long as required.  (For no NLS, still allocate the
  list, for consistency.)

  (This is not directly gui/gtk related, but it fits in here
  because so far it is used for doing i18n for gtk titles...)
**************************************************************************/
void intl_slist(int n, const char **s, bool *done)
{
  int i;

  if (!*done) {
    for(i=0; i<n; i++) {
      s[i] = _(s[i]);
    }

    *done = TRUE;
  }
}

/****************************************************************
...
*****************************************************************/
void itree_begin(GtkTreeModel *model, ITree *it)
{
  it->model = model;
  it->end = !gtk_tree_model_get_iter_first(it->model, &it->it);
}

/****************************************************************
...
*****************************************************************/
gboolean itree_end(ITree *it)
{
  return it->end;
}

/****************************************************************
...
*****************************************************************/
void itree_next(ITree *it)
{
  it->end = !gtk_tree_model_iter_next(it->model, &it->it);
}

/****************************************************************
...
*****************************************************************/
void itree_set(ITree *it, ...)
{
  va_list ap;
  
  va_start(ap, it);
  gtk_tree_store_set_valist(GTK_TREE_STORE(it->model), &it->it, ap);
  va_end(ap);
}

/****************************************************************
...
*****************************************************************/
void itree_get(ITree *it, ...)
{
  va_list ap;
  
  va_start(ap, it);
  gtk_tree_model_get_valist(it->model, &it->it, ap);
  va_end(ap);
}

/****************************************************************
...
*****************************************************************/
void tstore_append(GtkTreeStore *store, ITree *it, ITree *parent)
{
  it->model = GTK_TREE_MODEL(store);
  if (parent)
    gtk_tree_store_append(GTK_TREE_STORE(it->model), &it->it, &parent->it);
  else
    gtk_tree_store_append(GTK_TREE_STORE(it->model), &it->it, NULL);
  it->end = FALSE;
}

/****************************************************************
...
*****************************************************************/
void tstore_remove(ITree *it)
{
  gtk_tree_store_remove(GTK_TREE_STORE(it->model), &it->it);
}

/****************************************************************
...
*****************************************************************/
gboolean itree_is_selected(GtkTreeSelection *selection, ITree *it)
{
  return gtk_tree_selection_iter_is_selected(selection, &it->it);
}

/****************************************************************
...
*****************************************************************/
void itree_select(GtkTreeSelection *selection, ITree *it)
{
  gtk_tree_selection_select_iter(selection, &it->it);
}

/****************************************************************
...
*****************************************************************/
void itree_unselect(GtkTreeSelection *selection, ITree *it)
{
  gtk_tree_selection_unselect_iter(selection, &it->it);
}

/**************************************************************************
  Return the selected row in a GtkTreeSelection.
  If no row is selected return -1.
**************************************************************************/
gint gtk_tree_selection_get_row(GtkTreeSelection *selection)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  gint row = -1;

  if (gtk_tree_selection_get_selected(selection, &model, &it)) {
    GtkTreePath *path;
    gint *idx;

    path = gtk_tree_model_get_path(model, &it);
    idx = gtk_tree_path_get_indices(path);
    row = idx[0];
    gtk_tree_path_free(path);
  }
  return row;
}

/**************************************************************************
...
**************************************************************************/
void gtk_tree_view_focus(GtkTreeView *view)
{
  GtkTreePath *path;

  if ((path = gtk_tree_path_new_first())) {
    gtk_tree_view_set_cursor(view, path, NULL, FALSE);
    gtk_tree_path_free(path);
    gtk_widget_grab_focus(GTK_WIDGET(view));
  }
}

/**************************************************************************
...
**************************************************************************/
static void close_callback(GtkDialog *dialog, gpointer data)
{
  gtk_widget_destroy(GTK_WIDGET(dialog));
}

/**********************************************************************
  This function handles new windows which are subwindows to the
  toplevel window. It must be called on every dialog in the game,
  so fullscreen windows are handled properly by the window manager.
***********************************************************************/
void setup_dialog(GtkWidget *shell, GtkWidget *parent)
{
  if (dialogs_on_top || fullscreen_mode) {
    gtk_window_set_transient_for(GTK_WINDOW(shell),
                                 GTK_WINDOW(parent));
    gtk_window_set_type_hint(GTK_WINDOW(shell),
                             GDK_WINDOW_TYPE_HINT_UTILITY);
  } else {
    gtk_window_set_type_hint(GTK_WINDOW(shell),
                             GDK_WINDOW_TYPE_HINT_NORMAL);
  }

  /* Close dialog window on Escape keypress. */
  if (GTK_IS_DIALOG(shell)) {
    g_signal_connect_after(shell, "close", G_CALLBACK(close_callback), shell);
  }
}

