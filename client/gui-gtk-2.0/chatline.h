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
#ifndef FC__CHATLINE_H
#define FC__CHATLINE_H

#include <gtk/gtk.h>

#include "chatline_g.h"

#define	MAX_CHATLINE_HISTORY 20

extern struct genlist history_list;
extern int history_pos;

void inputline_return(GtkWidget *w, gpointer data);
void set_output_window_text(const char *text);

#endif  /* FC__CHATLINE_H */
