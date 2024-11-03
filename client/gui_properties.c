/***********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Team
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

#include <string.h>

/* utility */
#include "support.h"

/* client/include */
#include "gui_main_g.h"

#include "gui_properties.h"

struct client_properties gui_properties;

/**********************************************************************//**
  Initialize gui_properties
**************************************************************************/
void gui_properties_init(void)
{
  memset(&gui_properties, 0, sizeof(struct client_properties));

  setup_gui_properties();
}
