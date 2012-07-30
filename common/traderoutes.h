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
#ifndef FC__TRADEROUTES_H
#define FC__TRADEROUTES_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum trade_route_type {
  TRT_NATIONAL                        = 0,
  TRT_NATIONAL_IC                     = 1, /* Intercontinental */
  TRT_IN                              = 2,
  TRT_IN_IC                           = 3, /* International intercontinental */
  TRT_LAST                            = 4
};

struct trade_route_settings {
  int trade_pct;
};

int max_trade_routes(const struct city *pcity);
enum trade_route_type cities_trade_route_type(const struct city *pcity1,
                                              const struct city *pcity2);
int trade_route_type_trade_pct(enum trade_route_type type);

void trade_route_types_init(void);
const char *trade_route_type_name(enum trade_route_type type);
enum trade_route_type trade_route_type_by_name(const char *name);

struct trade_route_settings *
trade_route_settings_by_type(enum trade_route_type type);

#define trade_routes_iterate(c, t)                          \
do {                                                        \
  int _i##t;                                                \
  for (_i##t = 0 ; _i##t < MAX_TRADE_ROUTES ; _i##t++) {    \
    struct city *t = game_city_by_number(c->trade[_i##t]);  \
    if (t != NULL) {

#define trade_routes_iterate_end                            \
    }                                                       \
  }                                                         \
} while(FALSE)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__TRADEROUTES_H */
