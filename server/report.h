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
#ifndef FC__REPORT_H
#define FC__REPORT_H

struct connection;
struct conn_list;

void page_conn(struct conn_list *dest, char *caption, char *headline,
	       char *lines);
void page_conn_etype(struct conn_list *dest, char *caption, char *headline,
		     char *lines, int event);

void make_history_report(void);
void report_wonders_of_the_world(struct conn_list *dest);
void report_top_five_cities(struct conn_list *dest);
void report_demographics(struct connection *pconn);
void report_scores(bool final);

/* See also report_server_options() in stdinhand.h */

#endif  /* FC__REPORT_H */
