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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// utility
#include "support.h"

// common
#include "game.h"

// client
#include "climisc.h"
#include "mapctrl_common.h"
#include "overview_common.h"
#include "sprite.h"
#include "text.h"

// qui-qt
#include "qtg_cxxside.h"
#include "mapview.h"

const char*get_timeout_label_text();
static int mapview_frozen_level = 0;
extern struct canvas *canvas;
extern QApplication *qapp;

/**************************************************************************
  Constructor for idle callbacks
**************************************************************************/
mr_idle::mr_idle()
{
  connect(&timer, SIGNAL(timeout()), this, SLOT(idling()));
  /*if there would be messages in
   *that queue is big we may want to decrease it*/
  timer.start(50);
}

/**************************************************************************
  slot used to execute 1 callback from callabcks stored in idle list
**************************************************************************/
void mr_idle::idling()
{
  call_me_back* cb = new call_me_back;

  if (!callback_list.isEmpty()) {
    cb = callback_list.dequeue();
    (cb->callback) (cb->data);
    delete cb;
  }

}

/**************************************************************************
  Adds one callback to execute later
**************************************************************************/
void mr_idle::add_callback(call_me_back* cb)
{
  callback_list.enqueue(cb);
}

/**************************************************************************
  Constructor for map
**************************************************************************/
map_view::map_view() : QWidget()
{
  background = QBrush(QColor (0, 0, 0));
}

/**************************************************************************
  slot inherited from QPixamp
**************************************************************************/
void map_view::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  painter.setBackground(background);
  paint(&painter, event);
  painter.end();
}

/**************************************************************************
  Redraws visible map
**************************************************************************/
void map_view::paint(QPainter *painter, QPaintEvent *event)
{
  int width = mapview.store->map_pixmap.width();
  int height = mapview.store->map_pixmap.height();

  painter->drawPixmap(0, 0, width, height, mapview.store->map_pixmap);
}

/****************************************************************************
  Called when map view has been resized
****************************************************************************/
void map_view::resizeEvent(QResizeEvent* event)
{
  QSize size;

  size = event->size();

  if (C_S_RUNNING == client_state()) {
    map_canvas_resized(size.width(), size.height());
  }
}

/****************************************************************************
  Constructor for resize widget
****************************************************************************/
resize_widget::resize_widget(QWidget *parent) : QLabel()
{
  setParent(parent);
  setCursor(Qt::SizeFDiagCursor);
  setPixmap(QPixmap(resize_button));
}

/****************************************************************************
  Puts resize widget to right bottom corner
****************************************************************************/
void resize_widget::put_to_corner()
{
  move(parentWidget()->width() - width(),
       parentWidget()->height() - height());
}

/****************************************************************************
  Mouse handler for resize widget (resizes parent widget)
****************************************************************************/
void resize_widget::mouseMoveEvent(QMouseEvent * event)
{
  QPoint qp, np;
  qp = event->globalPos();
  np.setX(qp.x() - point.x());
  np.setY(qp.y() - point.y());
  np.setX(qMax(np.x(), 32));
  np.setY(qMax(np.y(), 32));
  parentWidget()->resize(np.x(), np.y());
}

/****************************************************************************
  Sets moving point for resize widget;
****************************************************************************/
void resize_widget::mousePressEvent(QMouseEvent* event)
{
  QPoint qp;
  qp = event->globalPos();
  point.setX(qp.x() - parentWidget()->width());
  point.setY(qp.y() - parentWidget()->height());
  update();
}

/****************************************************************************
  Constructor for close widget
****************************************************************************/
close_widget::close_widget(QWidget *parent) : QLabel()
{
  setParent(parent);
  setCursor(Qt::ArrowCursor);
  setPixmap(QPixmap(close_button));
}

/****************************************************************************
  Puts close widget to right top corner
****************************************************************************/
void close_widget::put_to_corner()
{
  move(parentWidget()->width()-width(), 0);
}

/****************************************************************************
  Mouse handler for close widget, hides parent widget
****************************************************************************/
void close_widget::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    parentWidget()->hide();
    notify_parent();
  }
}
/****************************************************************************
  Notifies parent to do custom action, parent is already hidden.
****************************************************************************/
void close_widget::notify_parent()
{
  fcwidget *fcw;
  fcw = reinterpret_cast<fcwidget *>(parentWidget());
  fcw->update_menu();
}


/**************************************************************************
  Constructor for minimap
**************************************************************************/
minimap_view::minimap_view(QWidget *parent) : fcwidget()
{
  setParent(parent);
  setScaledContents(true);
  w_ratio = 1.0;
  h_ratio = 1.0;
  move(4, 4);
  background = QBrush(QColor (0, 0, 0));
  setCursor(Qt::CrossCursor);
  rw = new resize_widget(this);
  rw->put_to_corner();
  cw = new close_widget(this);
  cw->put_to_corner();
  pix = new QPixmap;
  scale_factor = 1.0;
}

/**************************************************************************
  Paint event for minimap
**************************************************************************/
void minimap_view::paintEvent(QPaintEvent *event)
{
  QPainter painter;
  painter.begin(this);
  paint(&painter, event);
  painter.end();
}

/**************************************************************************
  Sets scaling factor for minimap
**************************************************************************/
void minimap_view::scale(double factor)
{
  scale_factor *= factor;
  if (scale_factor < 1) {
    scale_factor = 1.0;
  };
  update_image();
}

/**************************************************************************
  Converts gui to overview position.
**************************************************************************/
static void gui_to_overview(int *map_x, int *map_y, int gui_x, int gui_y)
{
  const int W = tileset_tile_width(tileset);
  const int H = tileset_tile_height(tileset);
  const int HH = tileset_hex_height(tileset);
  const int HW = tileset_hex_width(tileset);
  int a, b;

  if (HH > 0 || HW > 0) {
    int x, y, dx, dy;
    int xmult, ymult, mod, compar;
    x = DIVIDE(gui_x, W);
    y = DIVIDE(gui_y, H);
    dx = gui_x - x * W;
    dy = gui_y - y * H;
    xmult = (dx >= W / 2) ? -1 : 1;
    ymult = (dy >= H / 2) ? -1 : 1;
    dx = (dx >= W / 2) ? (W - 1 - dx) : dx;
    dy = (dy >= H / 2) ? (H - 1 - dy) : dy;
    if (HW > 0) {
      compar = (dx - HW / 2) * (H / 2) - (H / 2 - 1 - dy) * (W / 2 - HW);
    } else {
      compar = (dy - HH / 2) * (W / 2) - (W / 2 - 1 - dx) * (H / 2 - HH);
    }
    mod = (compar < 0) ? -1 : 0;
    *map_x = (x + y) + mod * (xmult + ymult) / 2;
    *map_y = (y - x) + mod * (ymult - xmult) / 2;
  } else if (tileset_is_isometric(tileset)) {
    gui_x -= W / 2;
    *map_x = DIVIDE(gui_x * H + gui_y * W, W * H);
    *map_y = DIVIDE(gui_y * W - gui_x * H, W * H);
  } else {
    *map_x = DIVIDE(gui_x, W);
    *map_y = DIVIDE(gui_y, H);
  }
  a = *map_x;
  b = *map_y;
  map_to_overview_pos(map_x, map_y, a, b);
  *map_x = *map_x + 1;
  *map_y = *map_y + 1;
}

/**************************************************************************
  Called by close widget, cause widget has been hidden. Updates menu.
**************************************************************************/
void minimap_view::update_menu()
{
  ::gui()->menu_bar->minimap_status->setChecked(false);
}

/**************************************************************************
  Minimap is being moved, position is being remebered
**************************************************************************/
void minimap_view::moveEvent(QMoveEvent* event)
{
  position = event->pos();
}

/**************************************************************************
  Minimap is just unhidden, old position is restored
**************************************************************************/
void minimap_view::showEvent(QShowEvent* event)
{
  move(position);
  event->setAccepted(true);
}

/**************************************************************************
  Draws viewport on minimap
**************************************************************************/
void minimap_view::draw_viewport(QPainter *painter)
{
  int i, x[4], y[4];
  int src_x, src_y, dst_x, dst_y;

  if (!overview.map) {
    return;
  }
  gui_to_overview(&x[0], &y[0], mapview.gui_x0, mapview.gui_y0);
  gui_to_overview(&x[1], &y[1], mapview.gui_x0 + mapview.width,
                  mapview.gui_y0);
  gui_to_overview(&x[2], &y[2], mapview.gui_x0 + mapview.width,
                  mapview.gui_y0 + mapview.height);
  gui_to_overview(&x[3], &y[3], mapview.gui_x0,
                  mapview.gui_y0 + mapview.height);
  painter->setPen(QColor(Qt::white));

  if (scale_factor > 1) {
    for (i = 0; i < 4; i++) {
      scale_point(x[i], y[i]);
    }
  }

  for (i = 0; i < 4; i++) {
    src_x = x[i] * w_ratio;
    src_y = y[i] * h_ratio;
    dst_x = x[(i + 1) % 4] * w_ratio;
    dst_y = y[(i + 1) % 4] * h_ratio;
    painter->drawLine(src_x, src_y, dst_x, dst_y);
  }
}

/**************************************************************************
  Scales point from real overview coords to scaled overview coords.
**************************************************************************/
void minimap_view::scale_point(int &x, int &y)
{
  int ax, bx;
  int dx, dy;

  gui_to_overview(&ax, &bx, mapview.gui_x0 + mapview.width / 2,
                  mapview.gui_y0 + mapview.height / 2);
  x = qRound(x * scale_factor);
  y = qRound(y * scale_factor);
  dx = qRound(ax * scale_factor - overview.width / 2);
  dy = qRound(bx * scale_factor - overview.height / 2);
  x = x - dx;
  y = y - dy;

}

/**************************************************************************
  Scales point from scaled overview coords to real overview coords.
**************************************************************************/
void minimap_view::unscale_point(int &x, int &y)
{
  int ax, bx;
  int dx, dy;
  gui_to_overview(&ax, &bx, mapview.gui_x0 + mapview.width / 2,
                  mapview.gui_y0 + mapview.height / 2);
  dx = qRound(ax * scale_factor - overview.width / 2);
  dy = qRound(bx * scale_factor - overview.height / 2);
  x = x + dx;
  y = y + dy;
  x = qRound(x / scale_factor);
  y = qRound(y / scale_factor);

}


/**************************************************************************
  Updates minimap's pixmap
**************************************************************************/
void minimap_view::update_image()
{
  QPixmap *tpix;
  QPixmap gpix;
  QPixmap bigger_pix(overview.width * 2, overview.height * 2);
  int delta_x, delta_y;
  int x, y, ix, iy;
  float wf, hf;
  QPixmap *src, *dst;

  if (isHidden() == true ){
    return; 
  }
  if (overview.map != NULL) {
    if (scale_factor > 1) {
      /* move minimap now, 
         scale later and draw without looking for origin */
      src = &overview.map->map_pixmap;
      dst = &overview.window->map_pixmap;
      x = overview.map_x0;
      y = overview.map_y0;
      ix = overview.width - x;
      iy = overview.height - y;
      pixmap_copy(dst, src, 0, 0, ix, iy, x, y);
      pixmap_copy(dst, src, 0, y, ix, 0, x, iy);
      pixmap_copy(dst, src, x, 0, 0, iy, ix, y);
      pixmap_copy(dst, src, x, y, 0, 0, ix, iy);
      tpix = &overview.window->map_pixmap;
      wf = static_cast <float>(overview.width) / scale_factor;
      hf = static_cast <float>(overview.height) / scale_factor;
      x = 0;
      y = 0;
      unscale_point(x, y);
      /* qt 4.8 is going to copy pixmap badly if coords x+size, y+size 
         will go over image so we create extra black bigger image */
      bigger_pix.fill(Qt::black);
      delta_x = overview.width / 2;
      delta_y = overview.height / 2;
      pixmap_copy(&bigger_pix, tpix, 0, 0, delta_x, delta_y, overview.width,
                  overview.height);
      gpix = bigger_pix.copy(delta_x + x, delta_y + y, wf, hf);
      *pix = gpix.scaled(width(), height(),
                         Qt::IgnoreAspectRatio, Qt::FastTransformation);
    } else {
      tpix = &overview.map->map_pixmap;
      *pix = tpix->scaled(width(), height(),
                          Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
  }
  update();
}

/**************************************************************************
  Redraws visible map using stored pixmap
**************************************************************************/
void minimap_view::paint(QPainter * painter, QPaintEvent * event)
{
  int x, y, ix, iy;

  x = overview.map_x0 * w_ratio;
  y = overview.map_y0 * h_ratio;
  ix = pix->width() - x;
  iy = pix->height() - y;

  if (scale_factor > 1) {
    painter->drawPixmap(0, 0, *pix, 0, 0, pix->width(), pix->height());
  } else {
    painter->drawPixmap(ix, iy, *pix, 0, 0, x, y);
    painter->drawPixmap(ix, 0, *pix, 0, y, x, iy);
    painter->drawPixmap(0, iy, *pix, x, 0, ix, y);
    painter->drawPixmap(0, 0, *pix, x, y, ix, iy);
  }
  painter->setPen(QColor(Qt::yellow));
  painter->setRenderHint(QPainter::Antialiasing);
  painter->drawRect(0, 0, width() - 1, height() - 1);
  draw_viewport(painter);
  rw->put_to_corner();
  cw->put_to_corner();
}

/****************************************************************************
  Called when minimap has been resized
****************************************************************************/
void minimap_view::resizeEvent(QResizeEvent* event)
{
  QSize size;
  size = event->size();

  if (C_S_RUNNING == client_state()) {
    w_ratio = static_cast<float>(width()) / overview.width;
    h_ratio = static_cast<float>(height()) / overview.height;
  }
  update_image();
}

/****************************************************************************
  Wheel event for minimap - zooms it in or out
****************************************************************************/
void minimap_view::wheelEvent(QWheelEvent * event)
{
  if (event->delta() > 0) {
    zoom_in();
  } else {
    zoom_out();
  }
  event->accept();
}

/****************************************************************************
  Sets scale factor to scale minimap 20% up
****************************************************************************/
void minimap_view::zoom_in()
{
  if (scale_factor < overview.width / 8) {
    scale(1.2);
  }
}

/****************************************************************************
  Sets scale factor to scale minimap 20% down
****************************************************************************/
void minimap_view::zoom_out()
{
  scale(0.833);
}

/**************************************************************************
  Mouse Handler for minimap_view
  Left button - moves minimap
  Right button - recenters on some point
  For wheel look mouseWheelEvent
**************************************************************************/
void minimap_view::mousePressEvent(QMouseEvent * event)
{
  int fx, fy;
  int x, y;

  if (event->button() == Qt::LeftButton) {
    cursor = event->globalPos() - geometry().topLeft();
  }
  if (event->button() == Qt::RightButton) {
    cursor = event->pos();
    fx = event->pos().x();
    fy = event->pos().y();
    fx = qRound(fx / w_ratio);
    fy = qRound(fy / h_ratio);
    if (scale_factor > 1) {
      unscale_point(fx, fy);
    }
    fx = qMax(fx, 1);
    fy = qMax(fy, 1);
    fx = qMin(fx, overview.width - 1);
    fy = qMin(fy, overview.height - 1);
    overview_to_map_pos(&x, &y, fx, fy);
    center_tile_mapcanvas(map_pos_to_tile(x, y));
    update_image();
  }
  event->setAccepted(true);
}

/**************************************************************************
  Called when mouse button was pressed. Used to moving minimap.
**************************************************************************/
void minimap_view::mouseMoveEvent(QMouseEvent* event)
{
  if (event->buttons() & Qt::LeftButton) {
    move(event->globalPos() - cursor);
    setCursor(Qt::SizeAllCursor);
  }
}

/**************************************************************************
  Called when mouse button unpressed. Restores cursor.
**************************************************************************/
void minimap_view::mouseReleaseEvent(QMouseEvent* event)
{
  setCursor(Qt::CrossCursor);
}


/**************************************************************************
  Constructor for information label
**************************************************************************/
info_label::info_label() : QLabel()
{
  layout = new QHBoxLayout;
  turn_info = new QLabel;
  time_label = new QLabel;
  eco_info = new QLabel;
  rates_label =  new QLabel;
  indicator_icons = new QLabel;
  turn_info->setText(_("TURN"));
  end_turn_button = new QPushButton;
  layout->addWidget(turn_info);
  layout->addStretch();
  layout->addWidget(eco_info);
  layout->addStretch();
  layout->addWidget(indicator_icons);
  layout->addStretch();
  layout->addWidget(rates_label);
  layout->addStretch();
  layout->addWidget(time_label);
  layout->addStretch();
  layout->addWidget(end_turn_button);

  end_turn_button->setText(_("End Turn"));
  QObject::connect(end_turn_button , SIGNAL(clicked()), this,
                   SLOT(slot_end_turn_done_button()));
  setLayout(layout);
}

/**************************************************************************
  Sets information about current turn
**************************************************************************/
void info_label::set_turn_info(QString str)
{
  turn_info->setText(str);
}

/**************************************************************************
  Sets information about current time
**************************************************************************/
void info_label::set_time_info(QString str)
{
  time_label->setText(str);
}

/**************************************************************************
  Sets information about current economy
**************************************************************************/
void info_label::set_eco_info(QString str)
{
  eco_info->setText(str);
}

/**************************************************************************
 * sets minimal size of information label
 *************************************************************************/
QSize info_label::sizeHint() const
{
  QSize s;

  s.setWidth(200);
  s.setHeight(32);

  return s;
}

/****************************************************************************
  Typically an info box is provided to tell the player about the state
  of their civilization.  This function is called when the label is
  changed.
****************************************************************************/
void update_info_label(void)
{
  QString eco_info;
  int d;
  struct sprite *sprite = get_tax_sprite(tileset, O_LUXURY);
  int w = sprite->pm->width();
  int h = sprite->pm->height();
  QPixmap final(10 * w, h);
  QPainter p;
  QRect source_rect(0, 0, w, h);
  QRect dest_rect(0, 0, w, h);
  final.fill (Qt::black);
  QString s = QString::fromLatin1(textyear(game.info.year)) + "  ( "
  +_("Turn") + ":" + QString::number(game.info.turn)+" )";
  if (client.conn.playing != NULL) {
    gui()->game_info_label->set_turn_info(s);
    eco_info = "<font color='purple'>G:"
               + QString::number(client.conn.playing->economic.gold)
               + "</font>" + "<font color='blue'>|R:"
               + QString::number(client.conn.playing->bulbs_last_turn)
               + "</font>";
    gui()->game_info_label->set_eco_info(eco_info);
    set_indicator_icons(client_research_sprite(),
                        client_warming_sprite(),
                        client_cooling_sprite(),
                        client_government_sprite());
    d = 0;

    for (; d < client.conn.playing->economic.luxury / 10; d++) {
      dest_rect.moveTo(d * w, 0);
      p.begin(&final);
      p.drawPixmap(dest_rect, *sprite->pm, source_rect);
      p.end();
    }

    sprite = get_tax_sprite(tileset, O_SCIENCE);

    for (; d < (client.conn.playing->economic.science
                + client.conn.playing->economic.luxury) / 10; d++) {
      dest_rect.moveTo(d * w, 0);
      p.begin(&final);
      p.drawPixmap(dest_rect, *sprite->pm, source_rect);
      p.end();
    }

    sprite = get_tax_sprite(tileset, O_GOLD);

    for (; d < 10; d++) {
      dest_rect.moveTo(d * w, 0);
      p.begin(&final);
      p.drawPixmap(dest_rect, *sprite->pm, source_rect);

      p.end();
    }

    gui()->game_info_label->rates_label->setPixmap(final);
  }

}

/****************************************************************************
  Update the information label which gives info on the current unit
  and the tile under the current unit, for specified unit.  Note that
  in practice punit is always the focus unit.

  Clears label if punit is NULL.

  Typically also updates the cursor for the map_canvas (this is
  related because the info label may includes "select destination"
  prompt etc).  And it may call update_unit_pix_label() to update the
  icons for units on this tile.
****************************************************************************/
void update_unit_info_label(struct unit_list *punitlist)
{
  /* PORTME */
}

/****************************************************************************
  Update the mouse cursor. Cursor type depends on what user is doing and
  pointing.
****************************************************************************/
void update_mouse_cursor(enum cursor_type new_cursor_type)
{
  /* PORTME */
}

/****************************************************************************
  Update the timeout display.  The timeout is the time until the turn
  ends, in seconds.
****************************************************************************/
void qtg_update_timeout_label(void)
{
  gui()->game_info_label->set_time_info (
    QString::fromLatin1(get_timeout_label_text()));
}

/****************************************************************************
  If do_restore is FALSE it should change the turn button style (to
  draw the user's attention to it).  If called regularly from a timer
  this will give a blinking turn done button.  If do_restore is TRUE
  this should reset the turn done button to the default style.
****************************************************************************/
void update_turn_done_button(bool do_restore)
{

  if (!get_turn_done_button_state()) {
    return;
  }

  if (do_restore) {
    gui()->game_info_label->end_turn_button->setEnabled(true);
  }

}

/**************************************************************************
  Clicked end turn button
**************************************************************************/
void info_label::slot_end_turn_done_button()
{
  key_end_turn();
  end_turn_button->setDisabled(true);
  gui()->mapview_wdg->setFocus();
}

/**************************************************************************
  Sets icons in information label
  I assume all icons have the same size
**************************************************************************/
void info_label::set_indicator_icons(QPixmap* bulb, QPixmap* sol,
                                     QPixmap* flake, QPixmap* gov)
{
  QPixmap final(4 * bulb->width(), bulb->height()) ;
  final.fill(Qt::black);

  QPainter p;
  QRect source_rect(0, 0, bulb->width(), bulb->height());
  QRect dest_rect(0, 0, bulb->width(), bulb->height());

  p.begin(&final);
  p.drawPixmap(dest_rect, *bulb, source_rect);
  dest_rect.setLeft(bulb->width());
  p.drawPixmap(dest_rect, *sol, source_rect);
  dest_rect.setLeft(2 * bulb->width());
  p.drawPixmap(dest_rect, *flake, source_rect);
  dest_rect.setLeft(3 * bulb->width());
  p.drawPixmap(dest_rect, *gov, source_rect);
  p.end();

  indicator_icons->setPixmap(final);
}
/****************************************************************************
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
****************************************************************************/
void set_indicator_icons(struct sprite *bulb, struct sprite *sol,
                         struct sprite *flake, struct sprite *gov)
{
  gui()->game_info_label->set_indicator_icons(bulb->pm, sol->pm, flake->pm,
                                              gov->pm);
}

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  return NULL;
}

/****************************************************************************
  Flush the given part of the canvas buffer (if there is one) to the
  screen.
****************************************************************************/
void flush_mapcanvas(int canvas_x, int canvas_y,
                     int pixel_width, int pixel_height)
{
  gui()->mapview_wdg->update(canvas_x - pixel_width,
                             canvas_y - pixel_height,
                             3 * pixel_width, 3 * pixel_height);
}

/****************************************************************************
  Mark the rectangular region as "dirty" so that we know to flush it
  later.
****************************************************************************/
void dirty_rect(int canvas_x, int canvas_y,
                int pixel_width, int pixel_height)
{
  gui()->mapview_wdg->update(canvas_x - pixel_width,
                             canvas_y - pixel_height, 3 * pixel_width,
                             3 * pixel_height);
}

/****************************************************************************
  Mark the entire screen area as "dirty" so that we can flush it later.
****************************************************************************/
void dirty_all(void)
{
  gui()->mapview_wdg->update();
}

/****************************************************************************
  Flush all regions that have been previously marked as dirty.  See
  dirty_rect and dirty_all.  This function is generally called after we've
  processed a batch of drawing operations.
****************************************************************************/
void flush_dirty(void)
{
  gui()->minimapview_wdg->update_image();
}

/****************************************************************************
  Do any necessary synchronization to make sure the screen is up-to-date.
  The canvas should have already been flushed to screen via flush_dirty -
  all this function does is make sure the hardware has caught up.
****************************************************************************/
void gui_flush(void)
{
}

/****************************************************************************
  Update (refresh) the locations of the mapview scrollbars (if it uses
  them).
****************************************************************************/
void update_map_canvas_scrollbars(void)
{
  /* PORTME */
}

/****************************************************************************
  Update the size of the sliders on the scrollbars.
****************************************************************************/
void update_map_canvas_scrollbars_size(void)
{
  /* PORTME */
}

/****************************************************************************
  Update (refresh) all city descriptions on the mapview.
****************************************************************************/
void update_city_descriptions(void)
{
  update_map_canvas_visible();
}

/**************************************************************************
  Put overlay tile to pixmap
**************************************************************************/
void pixmap_put_overlay_tile(int canvas_x, int  canvas_y,
                              struct sprite *ssprite)
{
  if (!ssprite) {
    return;
  }

  /* PORTME */
}

/****************************************************************************
  Draw a cross-hair overlay on a tile.
****************************************************************************/
void put_cross_overlay_tile(struct tile *ptile)
{
  int canvas_x, canvas_y;

  if (tile_to_canvas_pos(&canvas_x, &canvas_y, ptile)) {
    pixmap_put_overlay_tile(canvas_x, canvas_y,
                            get_attention_crosshair_sprite(tileset));
  }

}

/****************************************************************************
 Area Selection
****************************************************************************/
void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
{
  /* PORTME */
}

/****************************************************************************
  This function is called when the tileset is changed.
****************************************************************************/
void tileset_changed(void)
{
  /* PORTME */
  /* Here you should do any necessary redraws (for instance, the city
   * dialogs usually need to be resized). */
}

/****************************************************************************
  Return the dimensions of the area (container widget; maximum size) for
  the overview.
****************************************************************************/
void get_overview_area_dimensions(int *width, int *height)
{
  *width = ::gui()->minimapview_wdg->width();
  *height = ::gui()->minimapview_wdg->height();
}

/****************************************************************************
  Called when the map size changes. This may be used to change the
  size of the GUI element holding the overview canvas. The
  overview.width and overview.height are updated if this function is
  called.
  It's used for first creation of overview only, later overview stays the
  same size, scaled by qt-specific function.
****************************************************************************/
void overview_size_changed(void)
{
  int map_width, map_height, over_width, over_height, ow, oh;
  float ratio;
  map_width = gui()->mapview_wdg->width();
  map_height = gui()->mapview_wdg->height();
  over_width = overview.width;
  over_height = overview.height;

  /* lower overview width size to max 20% of map width, keep aspect ratio*/
  if (map_width/over_width < 5){
    ratio = static_cast<float>(map_width)/over_width;
    ow = static_cast<float>(map_width)/5.0;
    ratio = static_cast<float>(over_width)/ow;
    over_height=static_cast<float>(over_height)/ratio;
    over_width=ow;
  }
  /* if height still too high lower again */
  if (map_height/over_height < 5){
    ratio = static_cast<float>(map_height)/over_height;
    oh = static_cast<float>(map_height)/5.0;
    ratio = static_cast<float>(over_height)/oh;
    over_width=static_cast<float>(over_width)/ratio;
    over_height = oh;
  }
  /* make minimap not less than 48x48 */
  if (over_width < 48) {
    over_width =48;
  }
  if (over_height < 48) {
    over_height = 48;
  }
  gui()->minimapview_wdg->resize(over_width, over_height);
}

/**************************************************************************
 Sets the position of the overview scroll window based on mapview position.
**************************************************************************/
void update_overview_scroll_window_pos(int x, int y)
{
  /* TODO: PORTME. */
}


/****************************************************************************
  Return whether the map should be drawn or not.
****************************************************************************/
bool mapview_is_frozen(void)
{
  return (0 < mapview_frozen_level);
}


/****************************************************************************
  Freeze the drawing of the map.
****************************************************************************/
void mapview_freeze(void)
{
  mapview_frozen_level++;
}

/****************************************************************************
  Thaw the drawing of the map.
****************************************************************************/
void mapview_thaw(void)
{
  if (1 < mapview_frozen_level) {
    mapview_frozen_level--;
  } else {
    fc_assert(0 < mapview_frozen_level);
    mapview_frozen_level = 0;
    dirty_all();
  }
}
