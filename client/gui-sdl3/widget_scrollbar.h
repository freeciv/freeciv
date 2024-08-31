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

#ifndef FC__WIDGET_SCROLLBAR_H
#define FC__WIDGET_SCROLLBAR_H

/* gui-sdl3 */
#include "widget.h"

struct scroll_bar {
  struct widget *up_left_button;
  struct widget *pscroll_bar;
  struct widget *down_right_button;
  Uint8 active;		/* used by scroll: numbers of displayed rows */
  Uint8 step;		/* used by scroll: numbers of displayed columns */
  /* total displayed widget = active * step */
  Uint16 count;		/* total size of scroll list */
  Sint16 min;		/* used by scroll: min pixel position */
  Sint16 max;		/* used by scroll: max pixel position */
};

#define scrollbar_size(scroll)				\
        ((float)((float)(scroll->active * scroll->step) /	\
        (float)scroll->count) * (scroll->max - scroll->min))

#define hide_scrollbar(scrollbar)				\
do {								\
  if (scrollbar->up_left_button) {				\
    if (!(get_wflags(scrollbar->up_left_button) & WF_HIDDEN)) {\
      widget_undraw(scrollbar->up_left_button);                \
      widget_mark_dirty(scrollbar->up_left_button);            \
      set_wflag(scrollbar->up_left_button, WF_HIDDEN);		\
    }                                                           \
    if (!(get_wflags(scrollbar->down_right_button) & WF_HIDDEN)) {\
      widget_undraw(scrollbar->down_right_button);             \
      widget_mark_dirty(scrollbar->down_right_button);         \
      set_wflag(scrollbar->down_right_button, WF_HIDDEN);	\
    }                                                           \
  }								\
  if (scrollbar->pscroll_bar) {					\
    if (!(get_wflags(scrollbar->pscroll_bar) & WF_HIDDEN)) {     \
      widget_undraw(scrollbar->pscroll_bar);                     \
      widget_mark_dirty(scrollbar->pscroll_bar);                 \
      set_wflag(scrollbar->pscroll_bar, WF_HIDDEN);	        \
    }                                                           \
  }								\
} while (FALSE)

#define show_scrollbar(scrollbar)				\
do {								\
  if (scrollbar->up_left_button) {				\
    clear_wflag(scrollbar->up_left_button, WF_HIDDEN);		\
    clear_wflag(scrollbar->down_right_button, WF_HIDDEN);	\
  }								\
  if (scrollbar->pscroll_bar) {					\
    clear_wflag(scrollbar->pscroll_bar, WF_HIDDEN);		\
  }								\
} while (FALSE)

/* VERTICAL */
struct widget *create_vertical(SDL_Surface *vert_theme, struct gui_layer *pdest,
                               Uint16 height, Uint32 flags);
int draw_vert(struct widget *vert, Sint16 x, Sint16 y);

Uint32 create_vertical_scrollbar(struct advanced_dialog *dlg,
                                 Uint8 step, Uint8 active,
                                 bool create_scrollbar, bool create_buttons);

void setup_vertical_scrollbar_area(struct scroll_bar *scroll,
                                   Sint16 start_x, Sint16 start_y, Uint16 height,
                                   bool swap_start_x);

void setup_vertical_scrollbar_default_callbacks(struct scroll_bar *scroll);

/* HORIZONTAL */
struct widget *create_horizontal(SDL_Surface *horiz_theme,
                                 struct gui_layer *pdest,
                                 Uint16 width, Uint32 flags);
int draw_horiz(struct widget *horiz, Sint16 x, Sint16 y);

Uint32 create_horizontal_scrollbar(struct advanced_dialog *dlg,
                                   Sint16 start_x, Sint16 start_y,
                                   Uint16 width, Uint16 active,
                                   bool create_scrollbar, bool create_buttons,
                                   bool swap_start_y);

#endif /* FC__WIDGET_SCROLLBAR_H */
