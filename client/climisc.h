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
#ifndef FC__CLIMISC_H
#define FC__CLIMISC_H

#include "shared.h" /* MAX_LEN_NAME */

struct city;
struct Clause;
struct player;
struct player_spaceship;
typedef int cid;
typedef int wid;

void client_init_player(struct player *plr);
void client_remove_player(int plrno);
void client_remove_city(struct city *pcity);
void client_remove_unit(int unit_id);

void climap_init_continents(void);
void climap_update_continents(int x, int y);
void client_change_all(cid x, cid y);

void format_duration(char *buffer, int buffer_size, int duration);

char *get_embassy_status(struct player *me, struct player *them);
char *get_vision_status(struct player *me, struct player *them);
void client_diplomacy_clause_string(char *buf, int bufsiz,
				    struct Clause *pclause);

int client_research_sprite(void);
int client_warming_sprite(void);
int client_cooling_sprite(void);

enum known_type tile_get_known(int x, int y);
void center_on_something(void);

int concat_tile_activity_text(char *buf, int buf_size, int x, int y);

/* 
 * A compound id (cid) can hold all objects a city can build:
 * improvements (with wonders) and units. This is achieved by
 * seperation the value set: a cid < B_LAST denotes a improvement
 * (including wonders). A cid >= B_LAST denotes a unit with the
 * unit_type_id of (cid - B_LAST).
 */

cid cid_encode(bool is_unit, int id);
cid cid_encode_from_city(struct city *pcity);
void cid_decode(cid cid, bool *is_unit, int *id);
bool cid_is_unit(cid cid);
int cid_id(cid cid);

/* 
 * A worklist id (wid) can hold all objects which can be part of a
 * city worklist: improvements (with wonders), units and global
 * worklists. This is achieved by seperation the value set: 
 *  - (wid < B_LAST) denotes a improvement (including wonders)
 *  - (B_LAST <= wid < B_LAST + U_LAST) denotes a unit with the
 *  unit_type_id of (wid - B_LAST)
 *  - (B_LAST + U_LAST<= wid) denotes a global worklist with the id of
 *  (wid - (B_LAST + U_LAST))
 */

#define WORKLIST_END (-1)

wid wid_encode(bool is_unit, bool is_worklist, int id);
bool wid_is_unit(wid wid);
bool wid_is_worklist(wid wid);
int wid_id(wid wid);

bool city_can_build_impr_or_unit(struct city *pcity, cid cid);
bool city_unit_supported(struct city *pcity, cid cid);
bool city_unit_present(struct city *pcity, cid cid);

struct item {
  cid cid;
  char descr[MAX_LEN_NAME + 40];

  /* Privately used for sorting */
  int section;
};

void name_and_sort_items(int *pcids, int num_cids, struct item *items,
			 bool show_cost, struct city *pcity);
int collect_cids1(cid * dest_cids, struct city **selected_cities,
		 int num_selected_cities, bool append_units,
		 bool append_wonders, bool change_prod,
		 int (*test_func) (struct city *, int));
int collect_cids2(cid * dest_cids);
int collect_cids3(cid * dest_cids);
int collect_cids4(cid * dest_cids, struct city *pcity, bool advanced_tech);
int collect_cids5(cid * dest_cids, struct city *pcity);
int collect_wids1(wid * dest_wids, struct city *pcity, bool wl_first, 
		  bool advanced_tech);

/* the number of units in city */
int num_present_units_in_city(struct city* pcity);
int num_supported_units_in_city(struct city* pcity);	

char *get_spaceship_descr(struct player_spaceship *pship);

void create_event(int x, int y, int event, const char *format, ...)
     fc__attribute((format (printf, 4, 5)));

#endif  /* FC__CLIMISC_H */
