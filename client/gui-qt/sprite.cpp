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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QImageReader>
#include <QPainter>

// gui-qt
#include "colors.h"
#include "fc_client.h"
#include "qtg_cxxside.h"
#include "sprite.h"

static const char **gfx_array_extensions = nullptr;

/************************************************************************//**
  Return a nullptr-terminated, permanently allocated array of possible
  graphics types extensions.  Extensions listed first will be checked
  first.
****************************************************************************/
const char **gfx_fileextensions(void)
{
  QList<QByteArray> gfx_ext;
  QList<QByteArray>::iterator iter;
  int j = 0;

  if (gfx_array_extensions != nullptr) {
    return gfx_array_extensions;
  }

  gfx_ext = QImageReader::supportedImageFormats();

  gfx_array_extensions = new const char *[gfx_ext.count() + 1];
  for (iter = gfx_ext.begin(); iter != gfx_ext.end(); iter++) {
    char *ext;
    int extlen = iter->size() + 1;

    ext = static_cast<char *>(fc_malloc(extlen));
    strncpy(ext, iter->data(), extlen);
    gfx_array_extensions[j] = ext;
    j++;
  }
  gfx_array_extensions[j] = nullptr;

  return gfx_array_extensions;
}

/************************************************************************//**
  Load the given graphics file into a sprite. This function loads an
  entire image file, which may later be broken up into individual sprites
  with crop_sprite().
****************************************************************************/
struct sprite *qtg_load_gfxfile(const char *filename, bool svgflag)
{
  sprite *entire = new sprite;
  QPixmap *pm = new QPixmap;

  if (QPixmapCache::find(QString(filename), pm)) {
    entire->pm = pm;
    return entire;
  }
  pm->load(QString(filename));
  entire->pm = pm;
  QPixmapCache::insert(QString(filename), *pm);

  return entire;
}

/************************************************************************//**
  Create a new sprite by cropping and taking only the given portion of
  the image.

  source gives the sprite that is to be cropped.

  x,y, width, height gives the rectangle to be cropped.  The pixel at
  position of the source sprite will be at (0,0) in the new sprite, and
  the new sprite will have dimensions (width, height).

  mask gives an additional mask to be used for clipping the new
  sprite. Only the transparency value of the mask is used in
  crop_sprite. The formula is: dest_trans = src_trans *
  mask_trans. Note that because the transparency is expressed as an
  integer it is common to divide it by 256 afterwards.

  mask_offset_x, mask_offset_y is the offset of the mask relative to the
  origin of the source image.  The pixel at (mask_offset_x,mask_offset_y)
  in the mask image will be used to clip pixel (0,0) in the source image
  which is pixel (-x,-y) in the new image.
****************************************************************************/
struct sprite *qtg_crop_sprite(struct sprite *source,
                               int x, int y, int width, int height,
                               struct sprite *mask,
                               int mask_offset_x, int mask_offset_y,
                               float scale, bool smooth)
{
  QPainter p;
  QRectF source_rect;
  QRectF dest_rect;
  sprite *cropped;
  int widthzoom;
  int heightzoom;
  int hex = 0;

  fc_assert_ret_val(source, nullptr);

  if (!width || !height) {
    return nullptr;
  }
  if (scale != 1.0f && (tileset_hex_height(tileset) > 0
      || tileset_hex_width(tileset) > 0)) {
    hex = 1;
  }
  widthzoom = ceil(width * scale) + hex;
  heightzoom = ceil(height * scale) + hex;
  cropped = new sprite;
  cropped->pm = new QPixmap(widthzoom, heightzoom);
  cropped->pm->fill(Qt::transparent);
  source_rect = QRectF(x, y, width, height);
  dest_rect = QRectF(0, 0, widthzoom, heightzoom);

  p.begin(cropped->pm);
  if (smooth) {
    p.setRenderHint(QPainter::SmoothPixmapTransform);
  }
  p.setRenderHint(QPainter::Antialiasing);
  p.drawPixmap(dest_rect, *source->pm, source_rect);
  p.end();

  if (mask) {
    int mw = mask->pm->width();
    int mh = mask->pm->height();

    source_rect = QRectF(0, 0, mw, mh);
    dest_rect = QRectF(mask_offset_x - x, mask_offset_y - y, mw, mh);
    p.begin(cropped->pm);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawPixmap(dest_rect, *mask->pm, source_rect);
    p.end();
  }

  return cropped;
}

/************************************************************************//**
  Find the dimensions of the sprite.
****************************************************************************/
void qtg_get_sprite_dimensions(struct sprite *sprite, int *width, int *height)
{
  *width = sprite->pm->width();
  *height = sprite->pm->height();
}

/************************************************************************//**
  Free a sprite and all associated image data.
****************************************************************************/
void qtg_free_sprite(struct sprite *s)
{
  delete s->pm;
  delete s;
}

/************************************************************************//**
  Create a new sprite with the given height, width and color.
****************************************************************************/
struct sprite *qtg_create_sprite(int width, int height, struct color *pcolor)
{
  struct sprite *created = new sprite;

  created->pm = new QPixmap(width, height);

  created->pm->fill(pcolor->qcolor);

  return created;
}

/************************************************************************//**
  Creates a sprite with the number in it.
****************************************************************************/
struct sprite *qtg_load_gfxnumber(int num)
{
  struct sprite *spr = new sprite;
  QString ns;
  int w, h;
  QPixmap *pm;
  // FIXME: This should not depend on city_productions font setting
  QFont *qf = fc_font::instance()->get_font(fonts::city_productions);

  if (gui()->map_scale != 1.0f && gui()->map_font_scale) {
    int ssize = ceil(gui()->map_scale * fc_font::instance()->prod_fontsize);

    if (qf->pointSize() != ssize) {
      qf->setPointSize(ssize);
    }
  }

  QFontMetrics fm(*qf);

  if (num > 20) {
    ns = QString::number(num);
  } else {
#ifdef __cpp_char8_t
    const char8_t
#else
    const char
#endif
        *numsbuf[21] = {
      u8"\0xF0\0x9F\0x84\0x8C",
      u8"\u278A",
      u8"\u278B",
      u8"\u278C",
      u8"\u278D",
      u8"\u278E",
      u8"\u278F",
      u8"\u2790",
      u8"\u2791",
      u8"\u2792",
      u8"\u2793",
      u8"\u24EB",
      u8"\u24EC",
      u8"\u24ED",
      u8"\u24EE",
      u8"\u24EF",
      u8"\u24F0",
      u8"\u24F1",
      u8"\u24F2",
      u8"\u24F3",
      u8"\u24F4"
    };

    ns = QString(numsbuf[num]);
  }

  w = fm.horizontalAdvance(ns);
  h = fm.height();
  pm = new QPixmap(w, h);
  pm->fill(Qt::transparent);

  QPainter paint(pm);

  paint.setFont(*qf);
  paint.setBrush(Qt::transparent);
  paint.setPen(QColor(Qt::black));
  paint.drawText(QRect(0, 0, w, h), Qt::AlignLeft | Qt::AlignVCenter,
                 QString(u8"\u26AB"));

  if (num > 20) {
    paint.setPen(QColor(Qt::yellow));
    paint.drawText(QRect(-2, 0, w, h), Qt::AlignLeft | Qt::AlignVCenter,
                   QString(u8"\u2B24"));
    paint.drawText(QRect(4, -2, w, h), Qt::AlignLeft | Qt::AlignVCenter,
                   QString(u8"\u2B24"));
    paint.drawText(QRect(4, 2, w, h), Qt::AlignLeft | Qt::AlignVCenter,
                   QString(u8"\u2B24"));
    paint.drawText(QRect(8, 0, w, h), Qt::AlignLeft | Qt::AlignVCenter,
                   QString(u8"\u2B24"));
  }

  paint.setPen(QColor((num > 20) ? Qt::black : Qt::yellow));
  paint.drawText(QRect(0, 0, w, h), Qt::AlignLeft | Qt::AlignVCenter, ns);
  paint.end();

  spr->pm = pm;

  return spr;
}
