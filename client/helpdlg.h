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
#ifndef __HELPDLG__H
#define __HELPDLG__H

void popup_help_dialog(int item);
void popup_help_dialog_string(char *item);
void boot_help_texts(void);

#define HELP_PLAYING_ITEM "Playing the game"
#define HELP_CONTROLS_ITEM "Controls"
#define HELP_IMPROVEMENTS_ITEM "City Improvements"
#define HELP_UNITS_ITEM "Units"
#define HELP_TECHS_ITEM "Technology"
#define HELP_WONDERS_ITEM "Wonders of the World"
#define HELP_COPYING_ITEM "Copying"
#define HELP_ABOUT_ITEM "About"

#define TREE_NODE_UNKNOWN_TECH_BG "red"
#define TREE_NODE_KNOWN_TECH_BG "green"
#define TREE_NODE_REACHABLE_TECH_BG "yellow"

#endif
