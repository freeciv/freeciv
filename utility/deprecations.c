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
#endif /* HAVE_CONFIG_H */

#include "fc_prehdrs.h"

/* utility */
#include "log.h"
#include "shared.h"

#include "deprecations.h"

static bool depr_warns_enabled = FALSE;

/************************************************************************
  Enable deprecation warnings.
************************************************************************/
void deprecation_warnings_enable(void)
{
  depr_warns_enabled = TRUE;
}

/************************************************************************
  Return whether deprecation warnings are currently enabled.
************************************************************************/
bool are_deprecation_warnings_enabled(void)
{
  return depr_warns_enabled;
}
