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

/**************************************************************************
  Dynamically allocate a new choice.
**************************************************************************/
struct adv_choice *adv_new_choice(void)
{
  struct adv_choice *choice = fc_malloc(sizeof(*choice));

  adv_init_choice(choice);

  return choice;
}

/**************************************************************************
  Free dynamically allocated choice.
**************************************************************************/
void adv_free_choice(struct adv_choice *choice)
{
  free(choice);
}

/**************************************************************************
  Return better one of the choices given. In case of a draw, first one
  is preferred.
**************************************************************************/
struct adv_choice *adv_better_choice(struct adv_choice *first,
                                     struct adv_choice *second)
{
  if (second->want > first->want) {
    return second;
  } else {
    return first;
  }
}

/**************************************************************************
  Return better one of the choices given, and free the other.
**************************************************************************/
struct adv_choice *adv_better_choice_free(struct adv_choice *first,
                                          struct adv_choice *second)
{
  if (second->want > first->want) {
    free(first);

    return second;
  } else {
    free(second);

    return first;
  }
}
