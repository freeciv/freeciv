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

#include <aitools.h>

/********************************************************************** 
... this function should assign a value to choice and want, where 
    want is a value between 1 and 100.
    if choice is A_NONE this advisor doesn't want any tech researched at
    the moment
***********************************************************************/

void trade_advisor_choose_tech(struct player *pplayer, struct ai_choice *choice)
{
  choice->choice = A_NONE;
  choice->want   = 0;
  /* this function haven't been implemented yet */
}
