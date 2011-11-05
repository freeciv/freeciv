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
#ifndef FC__TAIMSG_H
#define FC__TAIMSG_H

struct tai_msg
{
  int type;
  void *data;
};

#define SPECLIST_TAG taimsg
#define SPECLIST_TYPE struct tai_msg
#include "speclist.h"

#define TAI_MSG_THR_EXIT 0

#endif /* FC__TAIMSG_H */
