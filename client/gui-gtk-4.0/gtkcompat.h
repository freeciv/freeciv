/***********************************************************************
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__GTKCOMPAT_H
#define FC__GTKCOMPAT_H

#if GTK_CHECK_VERSION(4,0,0)
#define GTKCOMPAT_GTK4_FINAL
#endif


#if !GTK_CHECK_VERSION(3,98,3)
/* Compatibility mode */

#define GDK_ALT_MASK GDK_MOD1_MASK

void gtk_scrolled_window_set_has_frame(GtkScrolledWindow *wnd, bool shadow);
void gtk_button_set_has_frame(GtkButton *btn, bool shadow);

#endif  /* GTK version < 3.98.3 */


#if !GTK_CHECK_VERSION(3,98,4)
/* Compatibility mode */

/* Can't be simple macro, as in some places we need the address of the function. */
void gtk_window_destroy(GtkWindow *wnd);

#define gtk_box_append(_box_, _child_) gtk_container_add(GTK_CONTAINER(_box_), _child_)
#define gtk_box_remove(_box_, _child_) gtk_container_remove(GTK_CONTAINER(_box_), _child_)
#define gtk_grid_remove(_grid_, _child_)                \
  gtk_container_remove(GTK_CONTAINER(_grid_), _child_)
#define gtk_frame_set_child(_frame_, _child_) \
  gtk_container_add(GTK_CONTAINER(_frame_), _child_)
#define gtk_window_set_child(_win_, _child_) \
  gtk_container_add(GTK_CONTAINER(_win_), _child_)
#define gtk_scrolled_window_set_child(_sw_, _child_) \
  gtk_container_add(GTK_CONTAINER(_sw_), _child_)
#define gtk_combo_box_set_child(_cb_, _child_) \
  gtk_container_add(GTK_CONTAINER(_cb_), _child_)
#define gtk_paned_set_start_child(_paned_, _child_) \
  gtk_paned_pack1(_paned_, _child_, TRUE, TRUE)
#define gtk_paned_set_end_child(_paned_, _child_) \
  gtk_paned_pack2(_paned_, _child_, FALSE, TRUE)
#define gtk_button_set_child(_but_, _child_) \
  gtk_container_add(GTK_CONTAINER(_but_), _child_)

#define gtk_combo_box_get_child(_box_) \
  gtk_bin_get_child(GTK_BIN(_box_))
#define gtk_button_get_child(_but_) \
  gtk_bin_get_child(GTK_BIN(_but_))
#define gtk_window_get_child(_win_) \
  gtk_bin_get_child(GTK_BIN(_win_))

#endif  /* GTK version < 3.98.4 */


#if !GTK_CHECK_VERSION(3,99,0)
/* Compatibility mode */

GtkWidget *compat_scrolled_window_new_wrapper(void);

#ifndef GTKCOMPAT_ITSELF
#define gtk_scrolled_window_new() compat_scrolled_window_new_wrapper()
#endif  /* GTKCOMPAT_ITSELF */

#endif  /* GTK version < 3.99 */

#endif  /* FC__GTKCOMPAT_H */
