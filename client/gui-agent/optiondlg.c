/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include "gui_agent.h"

/* client include */
#include "optiondlg_g.h"

void option_dialog_popup(const char *name, const struct option_set *poptset);

/**********************************************************************//**
  A headless client has no option windows. These callbacks intentionally do
  nothing; ordinary option storage and network handling remain in client core.
**************************************************************************/
void option_dialog_popup(const char *name, const struct option_set *poptset)
{
}

void option_dialog_popdown(const struct option_set *poptset)
{
}

void option_gui_update(struct option *poption)
{
}

void option_gui_add(struct option *poption)
{
}

void option_gui_remove(struct option *poption)
{
}
