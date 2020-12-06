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

#if !GTK_CHECK_VERSION(3,98,0)
/* Compatibility mode */

GtkWidget *compat_window_new_wrapper(void);

#ifndef GTKCOMPAT_ITSELF
#define gtk_window_new() compat_window_new_wrapper()
#endif  /* GTKCOMPAT_ITSELF */

#endif  /* GTK version < 3.98 */

#endif  /* FC__GTKCOMPAT_H */
