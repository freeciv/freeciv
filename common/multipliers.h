/********************************************************************** 
 Freeciv - Copyright (C) 2014 - Sławomir Lach
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__MULTIPLIERS_H
#define FC__MULTIPLIERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* utility */
#include "bitvector.h"

/* common */
#include "fc_types.h"
#include "name_translation.h"
  
#define MAX_MULTIPLIERS_COUNT 15
  
struct multiplier
{
  int id;
  struct name_translation name;
  int start;
  int stop;
  int step;
  int def; /* default value */
  struct strvec *helptext;
};

struct multiplier *multiplier_by_number(int id);
int multiplier_index(const struct multiplier *pmul);
struct multiplier *multiplier_new(void);
void multipliers_init(void);
void multipliers_free(void);
void set_multiplier_count(int);
int  get_multiplier_count(void);

#define multipliers_iterate(_mul_) \
{      \
     int _i; \
       for (_i = 0; _i < get_multiplier_count(); _i++) { \
        struct multiplier *_mul_ = multiplier_by_number(_i);

#define multipliers_iterate_end \
      }  \
}

#ifdef __cplusplus
}
#endif

#endif /* FC__MULTIPLIERS_H */
