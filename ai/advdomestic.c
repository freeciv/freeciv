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
... this function should assign a value to choice and want and type, where 
    want is a value between 1 and 100.
    if want is 0 this advisor doesn't want anything
    type = 1 means unit, type = 0 means building
***********************************************************************/

void domestic_advisor_choose_build(struct player *pplayer, 
				   struct ai_choice *choice)
{
  choice->choice = 0;
  choice->want   = 0;
  choice->type   = 0;
  /* this function haven't been implemented yet */
}






