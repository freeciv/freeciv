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
#ifndef FC__HELPDATA_H
#define FC__HELPDATA_H

#include "helpdlg_g.h"		/* enum help_page_type */

struct help_item {
  char *topic, *text;
  enum help_page_type type;
};

void boot_help_texts(void);
void free_help_texts(void);

int num_help_items(void);
const struct help_item *get_help_item(int pos);
const struct help_item *get_help_item_spec(const char *name,
					   enum help_page_type htype,
					   int *pos);
void help_iter_start(void);
const struct help_item *help_iter_next(void);

char *helptext_building(char *buf, size_t bufsz, Impr_Type_id which,
			const char *user_text);
void helptext_unit(char *buf, int i, const char *user_text);
void helptext_tech(char *buf, int i, const char *user_text);
void helptext_terrain(char *buf, int i, const char *user_text);
void helptext_government(char *buf, int i, const char *user_text);

char *helptext_unit_upkeep_str(int i);

#define help_items_iterate(pitem) {       \
        const struct help_item *pitem;    \
        help_iter_start();                \
        while((pitem=help_iter_next())) {   
#define help_items_iterate_end }}

#endif  /* FC__HELPDATA_H */
