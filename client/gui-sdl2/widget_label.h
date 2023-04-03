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

#ifndef FC__WIDGET_LABEL_H
#define FC__WIDGET_LABEL_H

#define create_iconlabel_from_chars(picon, pdest, chars, ptsize, flags) \
  create_iconlabel(picon, pdest, create_utf8_from_char(chars, ptsize), flags)

#define create_iconlabel_from_chars_fonto(picon, pdest, chars, fonto, flags) \
  create_iconlabel(picon, pdest, create_utf8_from_char_fonto(chars, fonto), flags)

#define create_active_iconlabel(buf, pdest, pstr, pstring, cb)           \
do { 									 \
  pstr = create_utf8_from_char(pstring, 10);				 \
  pstr->style |= TTF_STYLE_BOLD;					 \
  buf = create_iconlabel(NULL, pdest, pstr, 				 \
             (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE)); \
  buf->action = cb;                                                  \
} while (FALSE)

struct widget *create_themelabel(SDL_Surface *icon, struct gui_layer *pdest,
                                 utf8_str *pstr, Uint16 w, Uint16 h,
                                 Uint32 flags);
struct widget *create_themelabel2(SDL_Surface *icon, struct gui_layer *pdest,
                                  utf8_str *pstr, Uint16 w, Uint16 h, Uint32 flags);
struct widget *create_iconlabel(SDL_Surface *icon, struct gui_layer *pdest,
                                utf8_str *text, Uint32 flags);
struct widget *convert_iconlabel_to_themeiconlabel2(struct widget *icon_label);
int draw_label(struct widget *label, Sint16 start_x, Sint16 start_y);

int redraw_iconlabel(struct widget *label);
void remake_label_size(struct widget *label);

#endif /* FC__WIDGET_LABEL_H */
