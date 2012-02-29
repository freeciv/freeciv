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

int max_trade_routes(const struct city *pcity);

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
