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

struct city;
struct Clause;

void client_remove_player(int plr_id);
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

enum color_std get_grid_color(int x1, int y1, int x2, int y2);

void center_on_something(void);

int concat_tile_activity_text(char *buf, int buf_size, int x, int y);

/* 
 * A compound id (cid) can hold all objects a city can build:
 * improvements (with wonders) and units. This is achieved by
 * seperation the value set: a cid < B_LAST denotes a improvement
 * (including wonders). A cid >= B_LAST denotes a unit with the
 * unit_type_id of (cid - B_LAST).
 */
cid cid_encode(int is_unit, int id);
cid cid_encode_from_city(struct city *pcity);
void cid_decode(cid cid, int *is_unit, int *id);
int cid_is_unit(cid cid);
int cid_id(cid cid);

int city_can_build_impr_or_unit(struct city *pcity, cid cid);
int city_unit_supported(struct city *pcity, cid cid);
int city_unit_present(struct city *pcity, cid cid);

#endif  /* FC__CLIMISC_H */

