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
#ifndef FC__ACHIEVEMENTS_H
#define FC__ACHIEVEMENTS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum achievement_type
  { ACHIEVEMENT_SPACESHIP = 0,
    ACHIEVEMENT_LAST };

struct achievement
{
  int id;
  enum achievement_type type;
  struct player *first;
  const char *msg;
};

void achievements_init(void);
void achievements_free(void);

struct achievement *achievement_by_number(int id);

struct player *achievement_plr(struct achievement *ach);
  bool achievement_check(struct achievement *ach, struct player *pplayer);

#define achievements_iterate(_ach_)                          \
{                                                            \
  int _i_;                                                   \
  for (_i_ = 0; _i_ < ACHIEVEMENT_LAST; _i_++) {             \
    struct achievement *_ach_ = achievement_by_number(_i_);

#define achievements_iterate_end                             \
  }                                                          \
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__ACHIEVEMENTS_H */
