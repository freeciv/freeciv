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
#ifndef __CITYHAND_H
#define __CITYHAND_H

#include "packets.h"
#include "city.h"

void create_city(struct player *pplayer, int x, int y, char *name);
void remove_city(struct city *pcity);
void send_city_info(struct player *dest, struct city *pcity, int dosend);

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
void handle_city_change(struct player *pplayer, 
			struct packet_city_request *preq);
void handle_city_rename(struct player *pplayer, 
			struct packet_city_request *preq);


int establish_trade_route(struct city *pc1, struct city *pc2);
void remove_trade_route(int c1, int c2); 
void send_player_cities(struct player *pplayer);

#endif





