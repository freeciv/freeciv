/********************************************************************** 
 Freeciv - Copyright (C) 2005 - Freeciv Development Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__GGZCLIENT_H
#define FC__GGZCLIENT_H

#ifdef GGZ_CLIENT

#include "shared.h"

extern bool with_ggz;

void ggz_initialize(void);
void input_from_ggz(int socket);

#else

#  define with_ggz FALSE
#  define ggz_initialize() (void)0
#  define input_from_ggz(socket) (void)0

#endif

#endif  /* FC__GGZCLIENT_H */
