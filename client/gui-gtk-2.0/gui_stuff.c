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
  gint x=0, y=0;
  
  if (!GTK_WIDGET_REALIZED (ref))
    gtk_widget_realize (ref);
  if (!GTK_WIDGET_NO_WINDOW (ref))
    gdk_window_get_root_origin (ref->window, &x, &y);

  x=x+px*ref->requisition.width/100;
  y=y+py*ref->requisition.height/100;

  if (!GTK_WIDGET_REALIZED (w))
    gtk_widget_realize (w);
  if (GTK_IS_WINDOW (w))
    gtk_widget_set_uposition (w, x, y);
}


/**************************************************************************
...
**************************************************************************/
void gtk_set_bitmap(GtkWidget *w, GdkPixmap *pm)
{
  if (!GTK_IS_PIXMAP(w))
    return;
  gtk_pixmap_set(GTK_PIXMAP(w), pm, NULL);
}


/**************************************************************************
...
**************************************************************************/
void gtk_set_label(GtkWidget *w, char *text)
{
  char *str;

  if (!GTK_IS_LABEL(w))
    return;
  gtk_label_get(GTK_LABEL(w), &str);
  if(strcmp(str, text) != 0) {
    gtk_label_set_text(GTK_LABEL(w), text);
    gtk_expose_now(w);
  }
}


/**************************************************************************
...
**************************************************************************/
GtkWidget *gtk_accelbutton_new(const gchar *label, GtkAccelGroup *accel)
{
  return gtk_button_new_with_mnemonic(label);
}

/**************************************************************************
  Returns gettext-converted list of n strings.  Allocates the space
  for the returned list, but the individual strings in the list are
  as returned by gettext().  In case of no NLS, the strings will be
  the original strings, so caller should ensure that the originals
  persist for as long as required.  (For no NLS, still allocate the
  list, for consistency.)

  (This is not directly gui/gtk related, but it fits in here
  because so far it is used for doing i18n for gtk titles...)
**************************************************************************/
char **intl_slist(int n, char **s)
{
  char **ret = fc_malloc(n * sizeof(char*));
  int i;

  for(i=0; i<n; i++) {
    ret[i] = _(s[i]);
  }
  return ret;
}

/****************************************************************
...
*****************************************************************/
void itree_begin(GtkTreeStore *store, ITree *it)
{
  it->model = GTK_TREE_MODEL(store);
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
