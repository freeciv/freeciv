/***********************************************************************
 Freeciv - Copyright (C) 2006 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__WIDGET_CHECKBOX_H
#define FC__WIDGET_CHECKBOX_H

struct checkbox {
  SDL_Surface *true_theme;
  SDL_Surface *false_theme;
  bool state;
};

struct widget *create_textcheckbox(struct gui_layer *pdest, bool state,
                                   utf8_str *pstr, Uint32 flags);
struct widget *create_checkbox(struct gui_layer *pdest, bool state, Uint32 flags);
void toggle_checkbox(struct widget *cbox);
bool get_checkbox_state(struct widget *cbox);
int set_new_checkbox_theme(struct widget *cbox,
                           SDL_Surface *true_surf, SDL_Surface *false_surf);

#endif /* FC__WIDGET_CHECKBOX_H */
