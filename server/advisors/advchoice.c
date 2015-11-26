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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "support.h"

/* common */
#include "requirements.h"

#include "advchoice.h"

/**************************************************************************
  Sets the values of the choice to initial values.
**************************************************************************/
void adv_init_choice(struct adv_choice *choice)
{
  choice->value.utype = NULL;
  choice->want = 0;
  choice->type = CT_NONE;
  choice->need_boat = FALSE;
}
