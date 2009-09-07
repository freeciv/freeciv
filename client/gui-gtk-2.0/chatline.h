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

/* common */
#include "featured_text.h"      /* enum text_tag_type */

/* include */
#include "chatline_g.h"

#define	MAX_CHATLINE_HISTORY 20

extern struct genlist *history_list;
extern int history_pos;

void inputline_return(GtkEntry *w, gpointer data);
void inputline_make_tag(enum text_tag_type type);
void inputline_make_chat_link(struct tile *ptile, bool unit);

void set_output_window_text(const char *text);
void chatline_scroll_to_bottom(void);

void set_message_buffer_view_link_handlers(GtkWidget *view);

#endif  /* FC__CHATLINE_H */
