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

struct player;
struct city;
struct connection;
struct conn_list;

struct packet_city_info;
struct packet_city_request;
struct packet_generic_integer;
struct packet_generic_values;


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
void handle_city_change(struct player *pplayer, 
			struct packet_city_request *preq);
void handle_city_worklist(struct player *pplayer, 
			struct packet_city_request *preq);
void handle_city_rename(struct player *pplayer, 
			struct packet_city_request *preq);


void handle_city_options(struct player *pplayer,
 			 struct packet_generic_values *preq);
void handle_city_name_suggest_req(struct connection *pconn,
				  struct packet_generic_integer *packet);

#endif  /* FC__CITYHAND_H */
