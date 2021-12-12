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
