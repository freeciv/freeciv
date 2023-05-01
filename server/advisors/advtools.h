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
#ifndef FC__ADVTOOLS_H
#define FC__ADVTOOLS_H

/* common */
#include "fc_types.h"

#define MORT 24

/* relative comparison for equality of floats */
#define FC_EPSILON (0.001)
#define ADV_WANTS_EQ(_w1, _w2) (fabs((_w1) - (_w2)) < FC_EPSILON * (fabs((float) (_w1)) + fabs((float) (_w2))))

adv_want amortize(adv_want benefit, int delay);

/*
 * To prevent integer overflows the product "power * hp * firepower"
 * is divided by POWER_DIVIDER.
 *
 */
#define POWER_DIVIDER (POWER_FACTOR * 3)

#endif /* FC__ADVTOOLS_H */
