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
#ifndef FC__DIPLODLG_H
#define FC__DIPLODLG_H

#include "packets.h"

void handle_diplomacy_accept_treaty(struct packet_diplomacy_info *pa);
void handle_diplomacy_init_meeting(struct packet_diplomacy_info *pa);
void handle_diplomacy_create_clause(struct packet_diplomacy_info *pa);
void handle_diplomacy_cancel_meeting(struct packet_diplomacy_info *pa);
void handle_diplomacy_remove_clause(struct packet_diplomacy_info *pa);
void diplo_dialog_returnkey(GtkWidget *w, gpointer data);

#endif  /* FC__DIPLODLG_H */
