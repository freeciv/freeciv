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
#ifndef FC__RAND_H
#define FC__RAND_H

#include "shared.h"		/* bool type */

/* This is duplicated in shared.h to avoid extra includes: */
#define MAX_UINT32 0xFFFFFFFF

typedef unsigned int RANDOM_TYPE;

typedef struct {
  RANDOM_TYPE v[56];
  int j, k, x;
  bool is_init;			/* initially 0 for static storage */
} RANDOM_STATE;

RANDOM_TYPE myrand(RANDOM_TYPE size);
void mysrand(RANDOM_TYPE seed);

bool myrand_is_init(void);
RANDOM_STATE get_myrand_state(void);
void set_myrand_state(RANDOM_STATE state);

void test_random1(int n);

#endif  /* FC__RAND_H */
