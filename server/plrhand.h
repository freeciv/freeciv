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
#ifndef __PLRHAND_H
#define __PLRHAND_H

#include "registry.h"

struct player;
struct packet_player_request;

void server_remove_player(struct player *pplayer);
void update_player_activities(struct player *pplayer);

void handle_player_revolution(struct player *pplayer);
void handle_player_ai_options(struct player *pplayer,
			      struct packet_player_request *preq);
void handle_player_rates(struct player *pplayer, 
			 struct packet_player_request *preq);

void send_player_info(struct player *src, struct player *dest);

void page_player(struct player *pplayer, char *headline, char *lines);
void notify_player(struct player *pplayer, char *format, ...);
void notify_player_ex(struct player *pplayer, int x, int y, int event, char *format, ...);
void handle_player_government(struct player *pplayer,
			     struct packet_player_request *preq);
void handle_player_research(struct player *pplayer,
			    struct packet_player_request *preq);
void researchprogress(int plr);
int update_tech(struct player *plr, int bulbs);
void set_invention(struct player *plr, int tech, int value);
void init_tech(struct player *plr, int tech);
void update_research(struct player *plr); 
void choose_random_tech(struct player *plr);
void choose_tech(struct player *plr, int tech);
void choose_tech_goal(struct player *plr, int tech);
int choose_goal_tech(struct player *plr);
void wonders_of_the_world(struct player *pplayer);
void demographics_report(struct player *pplayer);
void top_five_cities(struct player *pplayer);
void make_history_report();
void do_dipl_cost(struct player *pplayer);
void do_free_cost(struct player *pplayer);
void do_conquer_cost(struct player *pplayer);
void show_ending();
void player_load(struct player *plr, int plrno, struct section_file *file);
void player_save(struct player *plr, int plrno, struct section_file *file);

#endif
