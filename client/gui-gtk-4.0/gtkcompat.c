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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <gtk/gtk.h>

#define GTKCOMPAT_ITSELF
#include "gtkcompat.h"

#if !GTK_CHECK_VERSION(3,98,0)

/************************************************************************//**
  Compatibility wrapper for dropping parameter from gtk_window_new() calls
****************************************************************************/
GtkWidget *compat_window_new_wrapper(void)
{
  return gtk_window_new(GTK_WINDOW_TOPLEVEL);
}

/************************************************************************//**
  Version of gdk_key_event_get_keyval() for gtk < 3.98
****************************************************************************/
guint gdk_key_event_get_keyval(GdkEvent *ev)
{
  guint keyval;

  gdk_event_get_keyval(ev, &keyval);

  return keyval;
}

/************************************************************************//**
  Version of gdk_event_get_modifier_state() for gtk < 3.98
****************************************************************************/
GdkModifierType gdk_event_get_modifier_state(GdkEvent *ev)
{
  GdkModifierType state;

  gdk_event_get_state(ev, &state);

  return state;
}

/************************************************************************//**
  Version of gdk_button_event_get_button() for gtk < 3.98
****************************************************************************/
guint gdk_button_event_get_button(GdkEvent *ev)
{
  guint button;

  gdk_event_get_button(ev, &button);

  return button;
}

/************************************************************************//**
  Version of gdk_scroll_event_get_direction() for gtk < 3.98
****************************************************************************/
GdkScrollDirection gdk_scroll_event_get_direction(GdkEvent *ev)
{
  GdkScrollDirection direction;

  gdk_event_get_scroll_direction(ev, &direction);

  return direction;
}

#endif /* GTK version < 3.98 */


#if !GTK_CHECK_VERSION(3,99,0)

/************************************************************************//**
  Compatibility wrapper for dropping parameter from
  gtk_scrolled_window_new() calls
****************************************************************************/
GtkWidget *compat_scrolled_window_new_wrapper(void)
{
  return gtk_scrolled_window_new(NULL, NULL);
}

#endif /* GTK version < 3.99.0 */
