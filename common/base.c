/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include "base.h"

/****************************************************************************
  Check if base provides effect
****************************************************************************/
bool base_flag(Base_type_id base_type, enum base_flag_id flag)
{
  switch(base_type) {
  case BASE_FORTRESS:
    /* Fortress */
    switch(flag) {
    case BF_NOT_AGGRESSIVE:
    case BF_DEFENSE_BONUS:
    case BF_WATCHTOWER:
    case BF_CLAIM_TERRITORY:
    case BF_NO_STACK_DEATH:
    case BF_DIPLOMAT_DEFENSE:
      return TRUE;

    case BF_REFUEL:
    case BF_NO_HP_LOSS:
    case BF_ATTACK_UNREACHABLE:
    case BF_PARADROP_FROM:
    case BF_LAST:
      return FALSE;
    }
    break;

  case BASE_AIRBASE:
    /* Airbase */
    switch(flag) {
    case BF_NO_STACK_DEATH:
    case BF_DIPLOMAT_DEFENSE:
    case BF_REFUEL:
    case BF_NO_HP_LOSS:
    case BF_ATTACK_UNREACHABLE:
    case BF_PARADROP_FROM:
      return TRUE;

    case BF_NOT_AGGRESSIVE:
    case BF_DEFENSE_BONUS:
    case BF_WATCHTOWER:
    case BF_CLAIM_TERRITORY:
    case BF_LAST:
      return FALSE;
    }
    break;
  }

  return FALSE;
}
