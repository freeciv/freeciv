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
#ifndef FC__CITYREPDATA_H
#define FC__CITYREPDATA_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "fc_types.h"
#include "support.h"            /* bool type */

/* Number of city report columns: have to set this manually now... */
#define NUM_CREPORT_COLS (num_city_report_spec())

struct city_report_spec {
  bool show;                    /* Modify this to customize */
  int width;                    /* 0 means variable; rightmost only */
  const char *title1;           /* Already translated or NULL */
  const char *title2;           /* Already translated or NULL */
  const char *explanation;      /* Already translated */
  void *data;
  const char *(*func)(const struct city *pcity, const void *data);
  const char *tagname;          /* For save_options */
};

extern struct city_report_spec *city_report_specs;

/* Use tagname rather than index for load/save, because later
   additions won't necessarily be at the end.
*/

/* Following are wanted to save/load options; use wrappers rather
   than expose the grotty details of the city_report_spec:
   (well, the details are exposed now too, but still keep
   this "clean" interface...)
*/
int num_city_report_spec(void);
bool *city_report_spec_show_ptr(int i);
const char *city_report_spec_tagname(int i);

void init_city_report_game_data(void);

int cityrepfield_compare(const char *field1, const char *field2);

bool can_city_sell_universal(const struct city *pcity,
                             const struct universal *target);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__CITYREPDATA_H */
