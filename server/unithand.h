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
#ifndef FC__UNITHAND_H
#define FC__UNITHAND_H

#include "packets.h"
#include "unit.h"

void handle_unit_goto_tile(struct player *pplayer, 
			   struct packet_unit_request *req);
void handle_unit_connect(struct player *pplayer, 
		          struct packet_unit_connect *req);
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
void do_unit_disband_safe(struct city *pcity, struct unit *punit,
			  struct genlist_iterator *iter); /* AI uses this directly */
void handle_unit_build_city(struct player *pplayer, 
			    struct packet_unit_request *req);
void handle_unit_info(struct player *pplayer, struct packet_unit_info *pinfo);
int handle_unit_move_request(struct unit *punit, int dest_x, int dest_y,
			     int igzoc, int move_diplomat_city);
void handle_unit_help_build_wonder(struct player *pplayer, 
				   struct packet_unit_request *req);
int handle_unit_establish_trade(struct player *pplayer, 
				struct packet_unit_request *req);
void handle_unit_auto_request(struct player *pplayer, 
			      struct packet_unit_request *req);
void handle_unit_activity_request(struct unit *punit, 
				  enum unit_activity new_activity);
void handle_unit_activity_request_targeted(struct unit *punit,
					   enum unit_activity new_activity,
					   int new_target, int select_unit);
void handle_unit_unload_request(struct player *pplayer, 
				struct packet_unit_request *req);
void handle_unit_nuke(struct player *pplayer, 
                     struct packet_unit_request *req);
void handle_unit_paradrop_to(struct player *pplayer, 
                     struct packet_unit_request *req);
void handle_move_unit(struct player *pplayer, struct packet_move_unit *pmove);
void handle_incite_inq(struct connection *pconn,
		       struct packet_generic_integer *packet);

void handle_goto_route(struct player *pplayer, struct packet_goto_route *packet);
void handle_patrol_route(struct player *pplayer, struct packet_goto_route *packet);

#endif  /* FC__UNITHAND_H */
