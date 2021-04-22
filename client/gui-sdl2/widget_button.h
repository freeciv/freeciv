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

#ifndef FC__WIDGET_BUTTON_H
#define FC__WIDGET_BUTTON_H

#define create_icon_button_from_chars(icon, pdest, char_string, ptsize, flags) \
        create_icon_button(icon, pdest,                                         \
                           create_utf8_from_char(char_string, ptsize),          \
                           flags)

#define create_themeicon_button_from_chars(icon_theme, pdest, char_string, ptsize, flags) \
  create_themeicon_button(icon_theme, pdest,                           \
                          create_utf8_from_char(char_string,            \
                                                ptsize),               \
                          flags)

struct widget *create_icon_button(SDL_Surface *icon,
                                  struct gui_layer *pdest, utf8_str *pstr,
                                  Uint32 flags);

struct widget *create_themeicon_button(SDL_Surface *icon_theme,
                                       struct gui_layer *pdest, utf8_str *pstr,
                                       Uint32 flags);

int draw_tibutton(struct widget *button, Sint16 start_x, Sint16 start_y);
int draw_ibutton(struct widget *button, Sint16 start_x, Sint16 start_y);

#endif /* FC__WIDGET_BUTTON_H */
