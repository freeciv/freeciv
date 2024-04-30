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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// utility
#include "string_vector.h"

// ruledit
#include "helpeditor.h"

#include "values_dlg.h"


/**********************************************************************//**
  values_dlg constructor
**************************************************************************/
values_dlg::values_dlg() : QDialog()
{
  help = nullptr;
}

/**********************************************************************//**
  Open help editor
**************************************************************************/
void values_dlg::open_help(struct strvec **helptext)
{
  if (help == nullptr) {
    // Create the strvec if it does not exist
    if (*helptext == nullptr) {
      *helptext = strvec_new();
    }

    // Make sure there's at least one element to edit
    if (strvec_size(*helptext) == 0) {
      strvec_append(*helptext, "");
    }

    help = new helpeditor(this, *helptext);

    help->show();
  }
}

/**********************************************************************//**
  Help editor is closing.
**************************************************************************/
void values_dlg::close_help()
{
  if (help != nullptr) {
    help->close();

    help = nullptr;
  }
}
