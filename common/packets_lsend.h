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
  This file should only be included via packets.h
**********************************************************************/

void lsend_packet_diplomacy_info(struct conn_list *dest, enum packet_type pt,
			       struct packet_diplomacy_info *packet);
void lsend_packet_diplomat_action(struct conn_list *dest, 
				struct packet_diplomat_action *packet);
void lsend_packet_nuke_tile(struct conn_list *dest, 
			  struct packet_nuke_tile *packet);
void lsend_packet_unit_combat(struct conn_list *dest, 
			    struct packet_unit_combat *packet);
void lsend_packet_unit_connect(struct conn_list *dest, 
			    struct packet_unit_connect *packet);
void lsend_packet_tile_info(struct conn_list *dest, 
			 struct packet_tile_info *pinfo);
void lsend_packet_map_info(struct conn_list *dest, 
			  struct packet_map_info *pinfo);
void lsend_packet_game_info(struct conn_list *dest, 
			  struct packet_game_info *pinfo);
void lsend_packet_player_info(struct conn_list *dest, 
			    struct packet_player_info *pinfo);
void lsend_packet_conn_info(struct conn_list *dest,
			  struct packet_conn_info *pinfo);
void lsend_packet_new_year(struct conn_list *dest, 
			 struct packet_new_year *request);
void lsend_packet_move_unit(struct conn_list *dest, 
			  struct packet_move_unit *request);
void lsend_packet_unit_info(struct conn_list *dest,
			  struct packet_unit_info *req);
void lsend_packet_req_join_game(struct conn_list *dest, 
			      struct packet_req_join_game *request);
void lsend_packet_join_game_reply(struct conn_list *dest, 
			       struct packet_join_game_reply *reply);
void lsend_packet_alloc_nation(struct conn_list *dest, 
			   struct packet_alloc_nation *packet);
void lsend_packet_generic_message(struct conn_list *dest, int type,
				struct packet_generic_message *message);
void lsend_packet_generic_integer(struct conn_list *dest, int type,
				struct packet_generic_integer *packet);
void lsend_packet_city_info(struct conn_list *dest,struct packet_city_info *req);
void lsend_packet_short_city(struct conn_list *dest,struct packet_short_city *req);
void lsend_packet_city_request(struct conn_list *dest, 
			     struct packet_city_request *packet,
			     enum packet_type req_type);
void lsend_packet_player_request(struct conn_list *dest, 
			       struct packet_player_request *packet,
			       enum packet_type req_type);
void lsend_packet_unit_request(struct conn_list *dest, 
			     struct packet_unit_request *packet,
			     enum packet_type req_type);
void lsend_packet_before_new_year(struct conn_list *dest);
void lsend_packet_unittype_info(struct conn_list *dest, int type, int action);
void lsend_packet_ruleset_control(struct conn_list *dest, 
				struct packet_ruleset_control *pinfo);
void lsend_packet_ruleset_unit(struct conn_list *dest,
			     struct packet_ruleset_unit *packet);
void lsend_packet_ruleset_tech(struct conn_list *dest,
			     struct packet_ruleset_tech *packet);
void lsend_packet_ruleset_building(struct conn_list *dest,
			     struct packet_ruleset_building *packet);
void lsend_packet_ruleset_terrain(struct conn_list *dest,
			     struct packet_ruleset_terrain *packet);
void lsend_packet_ruleset_terrain_control(struct conn_list *dest,
					struct terrain_misc *packet);
void lsend_packet_ruleset_government(struct conn_list *dest,
				struct packet_ruleset_government *packet);
void lsend_packet_ruleset_government_ruler_title(struct conn_list *dest,
				struct packet_ruleset_government_ruler_title *packet);
void lsend_packet_ruleset_nation(struct conn_list *dest,
			       struct packet_ruleset_nation *packet);
void lsend_packet_ruleset_city(struct conn_list *dest,
			     struct packet_ruleset_city *packet);
void lsend_packet_ruleset_game(struct conn_list *dest,
                             struct packet_ruleset_game *packet);
void lsend_packet_generic_values(struct conn_list *dest, int type,
			       struct packet_generic_values *req);
void lsend_packet_spaceship_info(struct conn_list *dest,
			       struct packet_spaceship_info *packet);
void lsend_packet_spaceship_action(struct conn_list *dest,
				 struct packet_spaceship_action *packet);
void lsend_packet_city_name_suggestion(struct conn_list *dest,
				     struct packet_city_name_suggestion *packet);
void lsend_packet_sabotage_list(struct conn_list *dest,
			      struct packet_sabotage_list *packet);
void lsend_packet_goto_route(struct conn_list *dest, struct packet_goto_route *packet,
			   enum goto_route_type type);
