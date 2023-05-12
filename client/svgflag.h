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
#ifndef FC__SVGFLAG_H
#define FC__SVGFLAG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct canvas;

struct area_rect
{
  int x, y, w, h;
};

extern bool _prefer_svg;

bool svg_flag_enable(void);
#define is_svg_flag_enabled() _prefer_svg

const char **ordered_gfx_fextensions(void);

void free_svg_flag_API(void);

void get_flag_dimensions(struct sprite *flag, struct area_rect *rect,
                         int svg_height);
void canvas_put_flag_sprite(struct canvas *pcanvas,
                            int canvas_x, int canvas_y, int canvas_w, int canvas_h,
                            struct sprite *flag);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__SVGFLAG_H */
