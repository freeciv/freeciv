/**********************************************************************
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
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

// Qt
#include <QFontMetrics>

// qt-client
#include "canvas.h"
#include "sprite.h"
#include "colors.h"

/****************************************************************************
  Create a canvas of the given size.
****************************************************************************/
struct canvas *qtg_canvas_create(int width, int height)
{
  struct canvas *store = new canvas;

  store->map_pixmap = QPixmap(width, height);
  return store;
}

/****************************************************************************
  Free any resources associated with this canvas and the canvas struct
  itself.
****************************************************************************/
void qtg_canvas_free(struct canvas *store)
{
  delete store;
}

/****************************************************************************
  Copies an area from the source canvas to the destination canvas.
****************************************************************************/
void qtg_canvas_copy(struct canvas *dest, struct canvas *src,
                     int src_x, int src_y, int dest_x, int dest_y, int width,
                     int height)
{

  QRectF source_rect(src_x, src_y, width, height);
  QRectF dest_rect(dest_x, dest_y, width, height);
  QPainter p;

  if (!width || !height) {
    return;
  }

  p.begin(&dest->map_pixmap);
  p.drawPixmap(dest_rect, src->map_pixmap, source_rect);
  p.end();

}

/****************************************************************************
  Copies an area from the source pixmap to the destination pixmap.
****************************************************************************/
void pixmap_copy(QPixmap *dest, QPixmap *src, int src_x, int src_y,
                 int dest_x, int dest_y, int width, int height)
{
  QRectF source_rect(src_x, src_y, width, height);
  QRectF dest_rect(dest_x, dest_y, width, height);
  QPainter p;

  if (!width || !height) {
    return;
  }

  p.begin(dest);
  p.drawPixmap(dest_rect, *src, source_rect);
  p.end();
}

/****************************************************************************
  Draw some or all of a sprite onto the canvas.
****************************************************************************/
void qtg_canvas_put_sprite(struct canvas *pcanvas,
                           int canvas_x, int canvas_y,
                           struct sprite *sprite,
                           int offset_x, int offset_y, int width, int height)
{
  QPainter p;

  p.begin(&pcanvas->map_pixmap);
  p.drawPixmap(canvas_x, canvas_y, *sprite->pm, offset_x, offset_y, width, height);
  p.end();
}

/****************************************************************************
  Draw a full sprite onto the canvas.
****************************************************************************/
void qtg_canvas_put_sprite_full(struct canvas *pcanvas,
                                int canvas_x, int canvas_y,
                                struct sprite *sprite)
{
  int width, height;

  get_sprite_dimensions(sprite, &width, &height);
  canvas_put_sprite(pcanvas, canvas_x, canvas_y, sprite,
                    0, 0, width, height);
}

/****************************************************************************
  Draw a full sprite onto the canvas.  If "fog" is specified draw it with
  fog.
****************************************************************************/
void qtg_canvas_put_sprite_fogged(struct canvas *pcanvas,
                                  int canvas_x, int canvas_y,
                                  struct sprite *psprite,
                                  bool fog, int fog_x, int fog_y)
{
  QPainter p;

  /* no idea where its drawn so lets keep CompositionMode_Multiply
   * to see where it is really used */
  p.begin(&pcanvas->map_pixmap);
  p.setCompositionMode(QPainter::CompositionMode_Multiply);
  p.drawPixmap(canvas_x, canvas_y, *psprite->pm);
  p.end();
}

/****************************************************************************
  Draw a filled-in colored rectangle onto canvas.
****************************************************************************/
void qtg_canvas_put_rectangle(struct canvas *pcanvas,
                              struct color *pcolor,
                              int canvas_x, int canvas_y,
                              int width, int height)
{

  QBrush brush(pcolor->qcolor);
  QPen pen(pcolor->qcolor);
  QPainter p;

  p.begin(&pcanvas->map_pixmap);
  p.setPen(pen);
  p.setBrush(brush);
  if (width == 1 && height == 1) {
    p.drawPoint(canvas_x, canvas_y);
  } else if (width == 1) {
    p.drawLine(canvas_x, canvas_y, canvas_x, canvas_y + height);
  } else if (height == 1) {
    p.drawLine(canvas_x, canvas_y, canvas_x + width, canvas_y);
  } else {
    p.drawRect(canvas_x, canvas_y, width, height);
  }

  p.end();
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void qtg_canvas_fill_sprite_area(struct canvas *pcanvas,
                                 struct sprite *psprite, struct color *pcolor,
                                 int canvas_x, int canvas_y)
{
  int width, height;

  get_sprite_dimensions(psprite, &width, &height);
  qtg_canvas_put_rectangle(pcanvas, pcolor, canvas_x, canvas_y, width, height);
}

/****************************************************************************
  Fill the area covered by the sprite with fog.
****************************************************************************/
void qtg_canvas_fog_sprite_area(struct canvas *pcanvas, struct sprite *psprite,
                                int canvas_x, int canvas_y)
{
  QPainter p;

  p.begin(&pcanvas->map_pixmap);
  p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
  p.drawPixmap(canvas_x, canvas_y, *psprite->pm);
  p.end();
}

/****************************************************************************
  Draw a 1-pixel-width colored line onto the canvas.
****************************************************************************/
void qtg_canvas_put_line(struct canvas *pcanvas, struct color *pcolor,
                         enum line_type ltype, int start_x, int start_y,
                         int dx, int dy)
{
  QPen pen;
  QPainter p;

  pen.setColor(pcolor->qcolor);

  switch (ltype) {
  case LINE_NORMAL:
    pen.setWidth(1);
    break;
  case LINE_BORDER:
    pen.setStyle(Qt::DashLine);
    pen.setDashOffset(4);
    pen.setWidth(2);
    break;
  case LINE_TILE_FRAME:
    pen.setWidth(2);
    break;
  case LINE_GOTO:
    pen.setWidth(2);
    break;
  default:
    pen.setWidth(1);
    break;
  }

  p.begin(&pcanvas->map_pixmap);
  p.setPen(pen);
  p.setRenderHint(QPainter::Antialiasing);
  p.drawLine(start_x, start_y, start_x + dx, start_y + dy);
  p.end();
}

/****************************************************************************
  Draw a 1-pixel-width colored curved line onto the canvas.
****************************************************************************/
void qtg_canvas_put_curved_line(struct canvas *pcanvas, struct color *pcolor,
                                enum line_type ltype, int start_x, int start_y,
                                int dx, int dy)
{
  QPen pen;
  pen.setColor(pcolor->qcolor);
  QPainter p;

  switch (ltype) {
  case LINE_NORMAL:
    pen.setWidth(1);
    break;
  case LINE_BORDER:
    pen.setStyle(Qt::DashLine);
    pen.setDashOffset(4);
    pen.setWidth(2);
    break;
  case LINE_TILE_FRAME:
    pen.setWidth(2);
    break;
  case LINE_GOTO:
    pen.setWidth(2);
    break;
  default:
    pen.setWidth(1);
    break;
  }

  p.begin(&pcanvas->map_pixmap);
  p.setPen(pen);
  // what curve ?
  //p.drawArc(start_x,start_y,dx,dy); ?
  p.drawLine(start_x, start_y, start_x + dx, start_y + dy);
  p.end();
}

/****************************************************************************
  Return the size of the given text in the given font.  This size should
  include the ascent and descent of the text.  Either of width or height
  may be NULL in which case those values simply shouldn't be filled out.
****************************************************************************/
void qtg_get_text_size (int *width, int *height,
                        enum client_font font, const char *text)
{
  /* FIXME -> add font handling */
  QFont afont;
  int fontheight = 12;
  afont.setPixelSize(fontheight);
  QFontMetrics fm(afont);

  if (width) {
    *width = fm.width(QString::fromUtf8(text));
  }

  if (height) {
    *height = fm.height();
  }
}

/****************************************************************************
  Draw the text onto the canvas in the given color and font.  The canvas
  position does not account for the ascent of the text; this function must
  take care of this manually.  The text will not be NULL but may be empty.
****************************************************************************/
void qtg_canvas_put_text(struct canvas *pcanvas, int canvas_x, int canvas_y,
                         enum client_font font, struct color *pcolor,
                         const char *text)
{
  QPainter p;
  QPen pen;
  QFont afont;
  int fontheight = 12;
  QColor color(pcolor->qcolor);

  pen.setColor(color);
  afont.setPixelSize(fontheight);

  p.begin(&pcanvas->map_pixmap);
  p.setPen(pen);
  p.setFont(afont);
  p.drawText(canvas_x, canvas_y + fontheight, QString::fromUtf8(text));
  p.end();
  /* FIXME */ /*ADD DIFFERENT FONT HANDLING */
}
