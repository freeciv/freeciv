/***********************************************************************
 Freeciv - Copyright (C) 2021 The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/* Random number initialization */

#ifndef FC__RANDSEED_H
#define FC__RANDSEED_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef unsigned int randseed;

randseed generate_game_seed(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__RAND_H */
