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
#ifndef FC__PLRHAND_H
#define FC__PLRHAND_H

#include "attribute.h"

struct player;
struct packet_player_request;
struct section_file;
struct connection;
struct conn_list;

void server_player_init(struct player *pplayer, int initmap);
void server_remove_player(struct player *pplayer);
void begin_player_turn(struct player *pplayer);
void update_player_activities(struct player *pplayer);

void handle_player_revolution(struct player *pplayer);
void handle_player_rates(struct player *pplayer, 
			 struct packet_player_request *preq);
void check_player_government_rates(struct player *pplayer);
void handle_player_cancel_pact(struct player *pplayer, int other_player);
void make_contact(int player1, int player2, int x, int y);
void maybe_make_first_contact(int x, int y, int playerid);
void neutralize_ai_player(struct player *pplayer);

void send_player_info(struct player *src, struct player *dest);

void page_conn(struct conn_list *dest, char *caption, char *headline,
	       char *lines);
void page_conn_etype(struct conn_list *dest, char *caption, char *headline,
		     char *lines, int event);

void notify_conn_ex(struct conn_list *dest, int x, int y, int event,
		    const char *format, ...) 
                    fc__attribute((format (printf, 5, 6)));
void notify_conn(struct conn_list *dest, const char *format, ...) 
                 fc__attribute((format (printf, 2, 3)));
void notify_player_ex(const struct player *pplayer, int x, int y,
		      int event, const char *format, ...)
                      fc__attribute((format (printf, 5, 6)));
void notify_player(const struct player *pplayer, const char *format, ...)
                   fc__attribute((format (printf, 2, 3)));
void notify_embassies(struct player *pplayer, struct player *exclude,
		      const char *format, ...)
		      fc__attribute((format (printf, 3, 4)));
		      
void handle_player_government(struct player *pplayer,
			     struct packet_player_request *preq);
void handle_player_research(struct player *pplayer,
			    struct packet_player_request *preq);
void handle_player_tech_goal(struct player *pplayer,
                            struct packet_player_request *preq);
void handle_player_worklist(struct player *pplayer,
                            struct packet_player_request *preq);
void found_new_tech(struct player *plr, int tech_found, char was_discovery, 
		    char saving_bulbs);
void tech_researched(struct player* plr);
int update_tech(struct player *plr, int bulbs);
void init_tech(struct player *plr, int tech);
void choose_random_tech(struct player *plr);
void choose_tech(struct player *plr, int tech);
void choose_tech_goal(struct player *plr, int tech);
int choose_goal_tech(struct player *plr);

void make_history_report(void);
void report_wonders_of_the_world(struct conn_list *dest);
void report_top_five_cities(struct conn_list *dest);
void report_demographics(struct connection *pconn);

void send_player_turn_notifications(struct player *pplayer);

void do_dipl_cost(struct player *pplayer);
void do_free_cost(struct player *pplayer);
void do_conquer_cost(struct player *pplayer);
void show_ending(void);
void player_load(struct player *plr, int plrno, struct section_file *file);
void player_map_load(struct player *plr, int plrno, struct section_file *file);
void player_save(struct player *plr, int plrno, struct section_file *file);

void associate_player_connection(struct player *plr, struct connection *pconn);
void unassociate_player_connection(struct player *plr, struct connection *pconn);

#endif  /* FC__PLRHAND_H */
