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

#ifndef FC__CITYDLG_COMMON_H
#define FC__CITYDLG_COMMON_H

#include <stddef.h>		/* size_t */

#include "shared.h"		/* bool type */

#include "city.h"		/* Specialist_type_id */
#include "fc_types.h"

struct canvas;

struct citizen_type {
  enum {
    CITIZEN_SPECIALIST,
    CITIZEN_CONTENT,
    CITIZEN_HAPPY,
    CITIZEN_UNHAPPY,
    CITIZEN_ANGRY,
    CITIZEN_LAST
  } type;
  Specialist_type_id spec_type;
};

int get_citydlg_canvas_width(void);
int get_citydlg_canvas_height(void);
void generate_citydlg_dimensions(void);

bool city_to_canvas_pos(int *canvas_x, int *canvas_y,
			int city_x, int city_y);
bool canvas_to_city_pos(int *city_x, int *city_y,
			int canvas_x, int canvas_y);
void city_dialog_redraw_map(struct city *pcity,
			    struct canvas *pcanvas);

void get_city_dialog_production(struct city *pcity,
                                char *buffer, size_t buffer_len);
void get_city_dialog_production_full(char *buffer, size_t buffer_len,
				     int id, bool is_unit,
				     struct city *pcity);
void get_city_dialog_production_row(char *buf[], size_t column_size, int id,
				    bool is_unit, struct city *pcity);

void get_city_citizen_types(struct city *pcity, int index,
			    struct citizen_type *citizens);
void city_rotate_specialist(struct city *pcity, int citizen_index);

void activate_all_units(struct tile *ptile);

int city_change_production(struct city *pcity, bool is_unit, int build_id);
int city_set_worklist(struct city *pcity, struct worklist *pworklist);
bool city_queue_insert(struct city *pcity, int position,
		       bool item_is_unit, int item_id);
void city_get_queue(struct city *pcity, struct worklist *pqueue);
bool city_set_queue(struct city *pcity, struct worklist *pqueue);
bool city_can_buy(const struct city *pcity);
int city_sell_improvement(struct city *pcity, Impr_Type_id sell_id);
int city_buy_production(struct city *pcity);
int city_change_specialist(struct city *pcity, Specialist_type_id from,
			   Specialist_type_id to);
int city_toggle_worker(struct city *pcity, int city_x, int city_y);
int city_rename(struct city *pcity, const char *name);

#endif /* FC__CITYDLG_COMMON_H */
