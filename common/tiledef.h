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
#ifndef FC__TILEDEF_H
#define FC__TILEDEF_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "extras.h"

struct tiledef
{
  int id;
  struct name_translation name;

  struct extra_type_list *extras;
};

void tiledefs_init(void);
void tiledefs_free(void);

int tiledef_count(void);
int tiledef_number(const struct tiledef *td);
struct tiledef *tiledef_by_number(int id);

/* For optimization purposes (being able to have it as macro instead of function
 * call) this is now same as tiledef_number(). tiledef.c does have semantically
 * correct implementation too. */
#define tiledef_index(_td_) (_td_)->id

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__TILEDEF_H */
