/***********************************************************************
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
#ifndef FC__REPORT_H
#define FC__REPORT_H

/* uility */
#include "support.h"            /* bool type */

struct connection;
struct conn_list;

#define REPORT_TITLESIZE 1024
#define REPORT_BODYSIZE (128 * MAX_NUM_PLAYER_SLOTS)

struct history_report
{
  int turn;
  char title[REPORT_TITLESIZE];
  char body[REPORT_BODYSIZE];
};

void log_civ_score_init(void);
void log_civ_score_free(void);
void log_civ_score_now(void);

void make_history_report(void);
void send_current_history_report(struct conn_list *dest);
void report_wonders_of_the_world(struct conn_list *dest);
void report_wonders_of_the_world_long(struct conn_list *dest);
void report_top_cities(struct conn_list *dest);
bool is_valid_demography(const char *demography, int *error);
void report_demographics(struct connection *pconn);
void report_achievements(struct connection *pconn);
void report_final_scores(struct conn_list *dest);
int get_tag_score(const char *tag, const struct player *pplayer);

struct history_report *history_report_get(void);

#endif /* FC__REPORT_H */
