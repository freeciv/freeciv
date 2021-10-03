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

#if !GTK_CHECK_VERSION(3,98,0)
/* Compatibility mode */

GtkWidget *compat_window_new_wrapper(void);

#ifndef GTKCOMPAT_ITSELF
#define gtk_window_new() compat_window_new_wrapper()
#endif  /* GTKCOMPAT_ITSELF */

guint gdk_key_event_get_keyval(GdkEvent *ev);
GdkModifierType gdk_event_get_modifier_state(GdkEvent *ev);
guint gdk_button_event_get_button(GdkEvent *ev);
GdkScrollDirection gdk_scroll_event_get_direction(GdkEvent *ev);
#define gdk_event_get_position(_ev_, _x_, _y_) gdk_event_get_coords(_ev_, _x_, _y_)

/* Wrap GtkNative away */
#define GtkNative GtkWidget
#define gtk_widget_get_native(_wdg_) (_wdg_)
#define gtk_native_get_surface(_nat_) gtk_widget_get_surface(_nat_)

#define gtk_label_set_wrap(_wdg_, _wrap_) gtk_label_set_line_wrap(_wdg_, _wrap_)

#endif  /* GTK version < 3.98 */

#endif  /* FC__GTKCOMPAT_H */
