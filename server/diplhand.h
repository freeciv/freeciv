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
#ifndef FC__DIPLHAND_H
#define FC__DIPLHAND_H

struct Treaty;
struct player;
struct packet_diplomacy_info;
struct connection;

struct Treaty *find_treaty(struct player *plr0, struct player *plr1);

void handle_diplomacy_cancel_meeting(struct player *pplayer, 
				     struct packet_diplomacy_info *packet);
void handle_diplomacy_create_clause(struct player *pplayer, 
				    struct packet_diplomacy_info *packet);
void handle_diplomacy_remove_clause(struct player *pplayer, 
				    struct packet_diplomacy_info *packet);
void handle_diplomacy_init(struct player *pplayer, 
			   struct packet_diplomacy_info *packet);
void handle_diplomacy_accept_treaty(struct player *pplayer, 
				    struct packet_diplomacy_info *packet);

void send_diplomatic_meetings(struct connection *dest);

#endif  /* FC__DIPLHAND_H */
