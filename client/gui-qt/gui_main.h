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

#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

void popup_quit_dialog();
QApplication *current_app();

#ifdef FREECIV_HAVE_CXX20_CAPTURE_THIS
#define CAPTURE_DEFAULT_THIS [=, this]
#else  // FREECIV_HAVE_CXX20_CAPTURE_THIS
#define CAPTURE_DEFAULT_THIS [=]
#endif // FREECIV_HAVE_CXX20_CAPTURE_THIS

void set_theme_style();

#endif // FC__GUI_MAIN_H
