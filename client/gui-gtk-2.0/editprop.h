/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__EDITPROP_H
#define FC__EDITPROP_H

struct property_editor;
struct tile_list;

struct property_editor *editprop_get_property_editor(void);
void property_editor_refresh(struct property_editor *pe,
                             const struct tile_list *tiles);
void property_editor_run(struct property_editor *pe);

#endif /* FC__EDITPROP_H */

