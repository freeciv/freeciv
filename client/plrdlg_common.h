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
#ifndef FC__PLRDLG_COMMON_H
#define FC__PLRDLG_COMMON_H

#include "shared.h"		/* bool type */

#include "fc_types.h"

void plrdlg_freeze(void);
void plrdlg_thaw(void);
void plrdlg_force_thaw(void);
bool is_plrdlg_frozen(void);

enum player_dlg_column_type {
  COL_FLAG,
  COL_COLOR,
  COL_BOOLEAN,
  COL_TEXT,
  COL_RIGHT_TEXT    /* right aligned text */
};

struct player_dlg_column {
  bool show;
  enum player_dlg_column_type type;
  const char *title;			/* already translated */
  const char *(*func)(struct player*);	/* if type = COL_*TEXT */
  bool (*bool_func)(struct player*);	/* if type = COL_BOOLEAN */
  const char *tagname;			/* for save_options */
};

extern struct player_dlg_column player_dlg_columns[];
extern const int num_player_dlg_columns;

void init_player_dlg_common(void);
int player_dlg_default_sort_column(void);

const char *player_addr_hack(struct player *pplayer);

#endif  /* FC__PLRDLG_COMMON_H */
