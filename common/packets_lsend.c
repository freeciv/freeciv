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

/**********************************************************************
  This file was auto-generated, by create_lsend.pl (must be run manually)
**********************************************************************/

#include "packets.h"

void lsend_packet_diplomacy_info(struct conn_list *dest, enum packet_type pt,
			       const struct packet_diplomacy_info *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_diplomacy_info(pconn, pt, packet);
  conn_list_iterate_end;
}

void lsend_packet_diplomat_action(struct conn_list *dest, 
				const struct packet_diplomat_action *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_diplomat_action(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_nuke_tile(struct conn_list *dest, 
			  const struct packet_nuke_tile *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_nuke_tile(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_unit_combat(struct conn_list *dest, 
			    const struct packet_unit_combat *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_unit_combat(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_unit_connect(struct conn_list *dest, 
			     const struct packet_unit_connect *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_unit_connect(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_tile_info(struct conn_list *dest, 
			  const struct packet_tile_info *pinfo)
{
  conn_list_iterate(*dest, pconn)
    send_packet_tile_info(pconn, pinfo);
  conn_list_iterate_end;
}

void lsend_packet_map_info(struct conn_list *dest, 
			 const struct packet_map_info *pinfo)
{
  conn_list_iterate(*dest, pconn)
    send_packet_map_info(pconn, pinfo);
  conn_list_iterate_end;
}

void lsend_packet_game_info(struct conn_list *dest, 
			  const struct packet_game_info *pinfo)
{
  conn_list_iterate(*dest, pconn)
    send_packet_game_info(pconn, pinfo);
  conn_list_iterate_end;
}

void lsend_packet_player_info(struct conn_list *dest, 
			    const struct packet_player_info *pinfo)
{
  conn_list_iterate(*dest, pconn)
    send_packet_player_info(pconn, pinfo);
  conn_list_iterate_end;
}

void lsend_packet_conn_info(struct conn_list *dest,
			  const struct packet_conn_info *pinfo)
{
  conn_list_iterate(*dest, pconn)
    send_packet_conn_info(pconn, pinfo);
  conn_list_iterate_end;
}

void lsend_packet_new_year(struct conn_list *dest, 
			 const struct packet_new_year *request)
{
  conn_list_iterate(*dest, pconn)
    send_packet_new_year(pconn, request);
  conn_list_iterate_end;
}

void lsend_packet_move_unit(struct conn_list *dest, 
			  const struct packet_move_unit *request)
{
  conn_list_iterate(*dest, pconn)
    send_packet_move_unit(pconn, request);
  conn_list_iterate_end;
}

void lsend_packet_unit_info(struct conn_list *dest,
			  const struct packet_unit_info *req)
{
  conn_list_iterate(*dest, pconn)
    send_packet_unit_info(pconn, req);
  conn_list_iterate_end;
}

void lsend_packet_req_join_game(struct conn_list *dest, 
			      const struct packet_req_join_game *request)
{
  conn_list_iterate(*dest, pconn)
    send_packet_req_join_game(pconn, request);
  conn_list_iterate_end;
}

void lsend_packet_join_game_reply(struct conn_list *dest, 
			        const struct packet_join_game_reply *reply)
{
  conn_list_iterate(*dest, pconn)
    send_packet_join_game_reply(pconn, reply);
  conn_list_iterate_end;
}

void lsend_packet_alloc_nation(struct conn_list *dest, 
			     const struct packet_alloc_nation *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_alloc_nation(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_generic_message(struct conn_list *dest, int type,
				const struct packet_generic_message *message)
{
  conn_list_iterate(*dest, pconn)
    send_packet_generic_message(pconn, type, message);
  conn_list_iterate_end;
}

void lsend_packet_generic_integer(struct conn_list *dest, int type,
				const struct packet_generic_integer *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_generic_integer(pconn, type, packet);
  conn_list_iterate_end;
}

void lsend_packet_city_info(struct conn_list *dest,
                          const struct packet_city_info *req)
{
  conn_list_iterate(*dest, pconn)
    send_packet_city_info(pconn, req);
  conn_list_iterate_end;
}

void lsend_packet_short_city(struct conn_list *dest,
                           const struct packet_short_city *req)
{
  conn_list_iterate(*dest, pconn)
    send_packet_short_city(pconn, req);
  conn_list_iterate_end;
}

void lsend_packet_city_request(struct conn_list *dest, 
			     const struct packet_city_request *packet,
			     enum packet_type req_type)
{
  conn_list_iterate(*dest, pconn)
    send_packet_city_request(pconn, packet, req_type);
  conn_list_iterate_end;
}

void lsend_packet_player_request(struct conn_list *dest, 
			       const struct packet_player_request *packet,
			       enum packet_type req_type)
{
  conn_list_iterate(*dest, pconn)
    send_packet_player_request(pconn, packet, req_type);
  conn_list_iterate_end;
}

void lsend_packet_unit_request(struct conn_list *dest, 
			     const struct packet_unit_request *packet,
			     enum packet_type req_type)
{
  conn_list_iterate(*dest, pconn)
    send_packet_unit_request(pconn, packet, req_type);
  conn_list_iterate_end;
}

void lsend_packet_before_new_year(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn)
    send_packet_before_new_year(pconn);
  conn_list_iterate_end;
}

void lsend_packet_unittype_info(struct conn_list *dest, int type, int action)
{
  conn_list_iterate(*dest, pconn)
    send_packet_unittype_info(pconn, type, action);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_control(struct conn_list *dest, 
				const struct packet_ruleset_control *pinfo)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_control(pconn, pinfo);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_unit(struct conn_list *dest,
			     const struct packet_ruleset_unit *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_unit(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_tech(struct conn_list *dest,
			     const struct packet_ruleset_tech *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_tech(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_building(struct conn_list *dest,
			     const struct packet_ruleset_building *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_building(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_terrain(struct conn_list *dest,
			     const struct packet_ruleset_terrain *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_terrain(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_terrain_control(struct conn_list *dest,
					const struct terrain_misc *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_terrain_control(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_government(struct conn_list *dest,
			       const struct packet_ruleset_government *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_government(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_government_ruler_title(struct conn_list *dest,
		   const struct packet_ruleset_government_ruler_title *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_government_ruler_title(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_nation(struct conn_list *dest,
			       const struct packet_ruleset_nation *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_nation(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_city(struct conn_list *dest,
			     const struct packet_ruleset_city *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_city(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_ruleset_game(struct conn_list *dest,
                             const struct packet_ruleset_game *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_ruleset_game(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_generic_values(struct conn_list *dest, int type,
			       const struct packet_generic_values *req)
{
  conn_list_iterate(*dest, pconn)
    send_packet_generic_values(pconn, type, req);
  conn_list_iterate_end;
}

void lsend_packet_spaceship_info(struct conn_list *dest,
			       const struct packet_spaceship_info *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_spaceship_info(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_spaceship_action(struct conn_list *dest,
				 const struct packet_spaceship_action *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_spaceship_action(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_city_name_suggestion(struct conn_list *dest,
				const struct packet_city_name_suggestion *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_city_name_suggestion(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_sabotage_list(struct conn_list *dest,
			      const struct packet_sabotage_list *packet)
{
  conn_list_iterate(*dest, pconn)
    send_packet_sabotage_list(pconn, packet);
  conn_list_iterate_end;
}

void lsend_packet_goto_route(struct conn_list *dest,
                           const struct packet_goto_route *packet,
			   enum goto_route_type type)
{
  conn_list_iterate(*dest, pconn)
    send_packet_goto_route(pconn, packet, type);
  conn_list_iterate_end;
}

