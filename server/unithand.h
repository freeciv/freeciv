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
#ifndef __UNITHAND_H
#define __UNITHAND_H

#include "packets.h"
#include "unit.h"
void handle_unit_goto_tile(struct player *pplayer, 
			   struct packet_unit_request *req);
void handle_upgrade_unittype_request(struct player *pplayer, 
				     struct packet_unittype_info *packet);
void handle_unit_upgrade_request(struct player *pplayer,
				 struct packet_unit_request *packet);
void handle_diplomat_action(struct player *pplayer, 
			    struct packet_diplomat_action *packet);
void handle_unit_change_homecity(struct player *pplayer, 
				 struct packet_unit_request *req);
void handle_unit_disband(struct player *pplayer, 
			 struct packet_unit_request *req);
void handle_unit_disband_safe(struct player *pplayer, 
			      struct packet_unit_request *req,
			      struct genlist_iterator *iter);
void handle_unit_build_city(struct player *pplayer, 
			    struct packet_unit_request *req);
void handle_unit_info(struct player *pplayer, struct packet_unit_info *pinfo);
void handle_unit_attack_request(struct player *pplayer, struct unit *punit,
				struct unit *pdefender);
void handle_unit_enter_hut(struct unit *punit);
int handle_unit_move_request(struct player *pplayer, struct unit *punit,
			      int dest_x, int dest_y);
void handle_unit_help_build_wonder(struct player *pplayer, 
				   struct packet_unit_request *req);
void handle_unit_establish_trade(struct player *pplayer, 
				 struct packet_unit_request *req);
void handle_unit_enter_city(struct player *pplayer, struct city *pcity);
void handle_unit_auto_request(struct player *pplayer, 
			      struct packet_unit_request *req);
void handle_unit_activity_request(struct player *pplayer, struct unit *punit, 
				  enum unit_activity new_activity);
void handle_unit_unload_request(struct player *pplayer, 
				struct packet_unit_request *req);
void handle_move_unit(struct player *pplayer, struct packet_move_unit *pmove);
void handle_incite_inq(struct player *pplayer,
		       struct packet_generic_integer *packet);

#endif



