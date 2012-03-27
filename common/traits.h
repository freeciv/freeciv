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
#ifndef FC__TRAITS_H
#define FC__TRAITS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SPECENUM_NAME trait
#define SPECENUM_VALUE0 TRAIT_EXPANSIONIST
#define SPECENUM_VALUE0NAME "Expansionist"
#define SPECENUM_VALUE1 TRAIT_TRADER
#define SPECENUM_VALUE1NAME "Trader"
#include "specenum_gen.h"

#define TRAIT_DEFAULT_VALUE 50

struct ai_trait
{
  int mod;   /* This is modification that changes during game.
                Not yet used, always 0.
                TODO: mod must go to savegames as soon as it can get values
                      other than 0 */
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__TRAITS_H */
