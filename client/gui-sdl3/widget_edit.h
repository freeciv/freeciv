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

#ifndef FC__WIDGET_EDIT_H
#define FC__WIDGET_EDIT_H

enum edit_return_codes {
  ED_RETURN = 1,
  ED_ESC = 2,
  ED_MOUSE = 3,
  ED_FORCE_EXIT = 4
};

#define create_edit_from_chars(background, pdest, char_string, ptsize, length, flags) \
	create_edit(background, pdest,                                                \
		    create_utf8_from_char(char_string, ptsize),                       \
		    length, flags)

#define create_edit_from_chars_fonto(background, pdest, char_string, fonto, length, flags) \
	create_edit(background, pdest,                                                \
		    create_utf8_from_char_fonto(char_string, fonto),                  \
		    length, flags)

#define edit(pedit) edit_field(pedit)

struct widget *create_edit(SDL_Surface *background, struct gui_layer *pdest,
                           utf8_str *pstr, int length,
                           Uint32 flags);
enum edit_return_codes edit_field(struct widget *edit_widget);
int draw_edit(struct widget *pedit, Sint16 start_x, Sint16 start_y);

#endif /* FC__WIDGET_EDIT_H */
