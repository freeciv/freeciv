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
#ifndef FC__CITYHAND_H
#define FC__CITYHAND_H

#include "packets.h"

struct player;
struct city;

void create_city(struct player *pplayer, const int x, const int y, char *name);
void remove_city(struct city *pcity);
void send_city_info(struct player *dest, struct city *pcity);
void send_city_info_at_tile(struct player *dest, int x, int y);
void send_adjacent_cities(struct city *pcity);

void do_sell_building(struct player *pplayer, struct city *pcity, int id);

void handle_city_change_specialist(struct player *pplayer, 
				   struct packet_city_request *preq);
void handle_city_make_specialist(struct player *pplayer, 
				 struct packet_city_request *preq);
void handle_city_make_worker(struct player *pplayer, 
			     struct packet_city_request *preq);
void handle_city_sell(struct player *pplayer, 
		      struct packet_city_request *preq);
void really_handle_city_sell(struct player *pplayer, struct city *pcity, int id);
void handle_city_buy(struct player *pplayer, struct packet_city_request *preq);
void really_handle_city_buy(struct player *pplayer, struct city *pcity);
void handle_city_refresh(struct player *pplayer, struct packet_generic_integer *preq);
void change_build_target(struct player *pplayer, struct city *pcity, 
			 int target, int is_unit, int event);
void handle_city_change(struct player *pplayer, 
			struct packet_city_request *preq);
void handle_city_worklist(struct player *pplayer, 
			struct packet_city_request *preq);
void handle_city_rename(struct player *pplayer, 
			struct packet_city_request *preq);


int establish_trade_route(struct city *pc1, struct city *pc2);
void send_player_cities(struct player *pplayer);
void update_map_with_city_workers(struct city *pcity);
void reestablish_city_trade_routes(struct city *pcity);

void handle_city_options(struct player *pplayer,
 			 struct packet_generic_values *preq);
void handle_city_name_suggest_req(struct player *pplayer,
				  struct packet_generic_integer *packet);
char *city_name_suggestion(struct player *pplayer);
void reality_check_city(struct player *pplayer,int x, int y);
void update_dumb_city(struct player *pplayer, struct city *pcity);
void send_all_known_cities(struct player *dest);
void package_city(struct city *pcity, struct packet_city_info *packet,
		  int dipl_invest);

#endif  /* FC__CITYHAND_H */
