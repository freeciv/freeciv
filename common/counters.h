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
#ifndef FC__COUNTERS_H
#define FC__COUNTERS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "support.h"

/* common */
#include "name_translation.h"

struct counter
{
  struct name_translation name;
  struct strvec *helptext;
  bool ruledit_disabled;
  enum counter_behavior type;
  enum counter_target target;
  int checkpoint;
  int def;    /* Default value for each entity of given type
               * for this counter */

  int index;  /* Index in specific (city/player/world) array */
};

void counters_init(void);
void counters_free(void);

struct counter *counter_by_id(int id);
int counter_id(struct counter *pcount);

struct counter *counter_by_rule_name(const char *name);
const char *counter_rule_name(struct counter *pcount);

const char *counter_name_translation(const struct counter *counter);
struct counter *counter_by_translated_name(const char *name);

int counter_index(const struct counter *pcount);
struct counter *counter_by_index(int index, enum counter_target target);
int counters_get_city_counters_count(void);
void attach_city_counter(struct counter *counter);

#define city_counters_iterate(pcount) { \
   int _i_##pcount; \
   struct counter *pcount; \
   int _ccounter_count_##pcount = counters_get_city_counters_count(); \
   for (_i_##pcount = 0; _i_##pcount < _ccounter_count_##pcount; _i_##pcount++) { \
      pcount = counter_by_index(_i_##pcount, CTGT_CITY);

#define city_counters_iterate_end } \
   }

#define counters_re_iterate(pcount) { \
   int _i_##pcount; \
   struct counter *pcount; \
   int _ccounter_count_##pcount = game.control.num_counters; { \
   for (_i_##pcount = 0; _i_##pcount < _ccounter_count_##pcount; _i_##pcount++) { \
      pcount = counter_by_id(_i_##pcount); \
      if ( ! pcount -> ruledit_disabled ) {

#define counters_re_iterate_end } \
          } \
      } \
   }
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__COUNTERS_H */
