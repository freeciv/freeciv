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
#ifndef __CITYREP_H
#define __CITYREP_H

void popup_city_report_dialog(int make_modal);
void city_report_dialog_update();
void city_report_dialog_update_city(struct city *pcity);

/* These are wanted to save/load options; use wrappers rather than
   expose the grotty details of the city_report_spec:
*/
int num_city_report_spec(void);
int *city_report_spec_show_ptr(int i);
char *city_report_spec_tagname(int i);

#endif
