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

#include "city.h"		/* enum specialist_type */
#include "improvement.h"	/* Impr_Type_id */
#include "shared.h"		/* bool type */

struct city;
struct canvas;

enum citizen_type {
  CITIZEN_ELVIS,
  CITIZEN_SCIENTIST,
  CITIZEN_TAXMAN,
  CITIZEN_CONTENT,
  CITIZEN_HAPPY,
  CITIZEN_UNHAPPY,
  CITIZEN_ANGRY,
  CITIZEN_LAST
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
			    enum citizen_type *citizens);
void city_rotate_specialist(struct city *pcity, int citizen_index);

void activate_all_units(int map_x, int map_y);

int city_change_production(struct city *pcity, bool is_unit, int build_id);
int city_set_worklist(struct city *pcity, struct worklist *pworklist);
void city_get_queue(struct city *pcity, struct worklist *pqueue);
void city_set_queue(struct city *pcity, struct worklist *pqueue);
int city_sell_improvement(struct city *pcity, Impr_Type_id sell_id);
int city_buy_production(struct city *pcity);
int city_change_specialist(struct city *pcity, enum specialist_type from,
			   enum specialist_type to);
int city_toggle_worker(struct city *pcity, int city_x, int city_y);
int city_rename(struct city *pcity, const char *name);

#endif /* FC__CITYDLG_COMMON_H */
