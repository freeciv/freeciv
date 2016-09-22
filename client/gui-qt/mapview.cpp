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

// Qt
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QToolTip>
#include <QWheelEvent>

// utility
#include "log.h"
#include "support.h"

// common
#include "game.h"
#include "map.h"
#include "research.h"

// client
#include "climisc.h"
#include "mapctrl_common.h"
#include "menu.h" // gov_menu
#include "overview_common.h"
#include "sprite.h"
#include "repodlgs.h"
#include "text.h"

// qui-qt
#include "qtg_cxxside.h"
#include "mapview.h"

const char *get_timeout_label_text();
static int mapview_frozen_level = 0;
extern void destroy_city_dialog();
extern struct canvas *canvas;
extern QApplication *qapp;

#define MAX_DIRTY_RECTS 20
static int num_dirty_rects = 0;
static QRect dirty_rects[MAX_DIRTY_RECTS];

/**************************************************************************
  Check if point x, y is in area (px -> pxe, py - pye)
**************************************************************************/
bool is_point_in_area(int x, int y, int px, int py, int pxe, int pye)
{
  if (x >= px && y >= py && x <= pxe && y <= pye) {
      return true;
    }
  return false;
}


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
  call_me_back* cb;

  while (!callback_list.isEmpty()) {
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
  cursor = -1;
  QTimer *timer = new QTimer(this);
  connect(timer, SIGNAL(timeout()), this, SLOT(timer_event()));
  timer->start(200);
  setMouseTracking(true);
}

/**************************************************************************
  Updates cursor
**************************************************************************/
void map_view::update_cursor(enum cursor_type ct)
{
  int i;

  if (ct == CURSOR_DEFAULT) {
    setCursor(Qt::ArrowCursor);
    cursor = -1;
    return;
  }
  cursor_frame = 0;
  i = static_cast<int>(ct);
  cursor = i;
  setCursor(*(gui()->fc_cursors[i][0]));
}

/**************************************************************************
  Timer for cursor
**************************************************************************/
void map_view::timer_event()
{
  if (gui()->infotab->underMouse()
      || gui()->minimapview_wdg->underMouse()
      || gui()->game_info_label->underMouse()
      || gui()->unitinfo_wdg->underMouse()) {
    update_cursor(CURSOR_DEFAULT);
    return;
  }
  if (cursor == -1) {
    return;
  }
  cursor_frame++;
  if (cursor_frame == NUM_CURSOR_FRAMES) {
    cursor_frame = 0;
  }
  setCursor(*(gui()->fc_cursors[cursor][cursor_frame]));
}

/**************************************************************************
  Focus lost event
**************************************************************************/
void map_view::focusOutEvent(QFocusEvent *event)
{
  update_cursor(CURSOR_DEFAULT);
}

/**************************************************************************
  Leave event
**************************************************************************/
void map_view::leaveEvent(QEvent *event)
{
  update_cursor(CURSOR_DEFAULT);
}

/**************************************************************************
  slot inherited from QPixamp
**************************************************************************/
void map_view::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  paint(&painter, event);
  painter.end();
}

/**************************************************************************
  Redraws given rectangle on map
**************************************************************************/
void map_view::paint(QPainter *painter, QPaintEvent *event)
{
  painter->drawPixmap(event->rect(), mapview.store->map_pixmap,
                      event->rect());
}

/**************************************************************************
  Sets new point for new search 
**************************************************************************/
void map_view::resume_searching(int pos_x ,int pos_y ,int &w, int &h,
                                int wdth, int hght, int recursive_nr)
{
  int new_pos_x, new_pos_y;

  recursive_nr++;
  new_pos_x = pos_x;
  new_pos_y = pos_y;

  if (pos_y + hght + 4 < height() && pos_x > width() / 2) {
    new_pos_y = pos_y + 5;
  } else if (pos_x > 0 && pos_y > 10) {
    new_pos_x = pos_x - 5;
  } else if (pos_y > 0) {
    new_pos_y = pos_y - 5;
  } else if (pos_x + wdth + 4 < this->width()) {
    new_pos_x = pos_x + 5;
  }
  find_place(new_pos_x, new_pos_y, w, h, wdth, hght, recursive_nr);
}

/**************************************************************************
  Searches place for widget with size w and height h
  Starts looking from position pos_x, pos_y, going clockwork
  Returns position as (w,h)
  Along with resume_searching its recursive function.
**************************************************************************/
void map_view::find_place(int pos_x, int pos_y, int &w, int &h, int wdth, 
                          int hght, int recursive_nr)
{
  int i;
  int x, y, xe, ye;
  QList <fcwidget *>widgets = this->findChildren <fcwidget *>();
  bool cont_searching = false;

  if (recursive_nr >= 1000) {
    /**
     * give up searching position
     */
    return;
  }
  /**
   * try position pos_x, pos_y,
   * check middle and borders if aren't  above other widget
   */

  for (i = 0; i < widgets.count(); i++) {
    if (widgets[i]->isVisible() == false) {
      continue;
    }
    x = widgets[i]->pos().x();
    y = widgets[i]->pos().y();

    if (x == 0 && y ==0) {
      continue;
    }
    xe = widgets[i]->pos().x() + widgets[i]->width();
    ye = widgets[i]->pos().y() + widgets[i]->height();

    if (is_point_in_area(pos_x, pos_y, x, y, xe, ye)) {
      cont_searching = true;
    }
    if (is_point_in_area(pos_x + wdth, pos_y, x, y, xe, ye)) {
      cont_searching = true;
    }
    if (is_point_in_area(pos_x + wdth, pos_y + hght, x, y, xe, ye)) {
      cont_searching = true;
    }
    if (is_point_in_area(pos_x, pos_y + hght, x, y, xe, ye)) {
      cont_searching = true;
    }
    if (is_point_in_area(pos_x + wdth / 2, pos_y + hght / 2, x, y, xe, ye)) {
      cont_searching = true;
    }
  }
  w = pos_x;
  h = pos_y;
  if (cont_searching) {
    resume_searching(pos_x, pos_y, w, h, wdth, hght, recursive_nr);
  }
}


/****************************************************************************
  Called when map view has been resized
****************************************************************************/
void map_view::resizeEvent(QResizeEvent *event)
{
  QSize size;
  QSize delta;

  size = event->size();
  if (C_S_RUNNING <= client_state()) {
    map_canvas_resized(size.width(), size.height());
    gui()->infotab->resize(((size.width()
                             - gui()->end_turn_rect->sizeHint().width())
                             * gui()->qt_settings.infotab_width) / 100,
                             (size.height()
                             * gui()->qt_settings.infotab_height) / 100);
    gui()->infotab->move(0 , size.height() - gui()->infotab->height());
    gui()->unitinfo_wdg->move(width() - gui()->unitinfo_wdg->width(), 0);
    gui()->end_turn_rect->end_turn_update();
    delta = size - gui()->end_turn_rect->size();
    gui()->game_info_label->move(0, 0);
    gui()->game_info_label->resize(gui()->game_info_label->sizeHint());
    gui()->game_info_label->setMaximumWidth(size.width()
                                            - gui()->unitinfo_wdg->width());
    gui()->minimapview_wdg->move(size.width() -
                                 gui()->minimapview_wdg->width(),
                                 delta.height() -
                                 gui()->minimapview_wdg->height() - 10);
    gui()->x_vote->move(width() / 2 - gui()->x_vote->width() / 2, 0);
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
  Puts resize widget to left top corner
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

  if (gui()->infotab->locked) {
    return;
  }

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
  w_ratio = 0.0;
  h_ratio = 0.0;
  // Dark magic: This call is required for the widget to work.
  resize(0, 0);
  background = QBrush(QColor (0, 0, 0));
  setCursor(Qt::CrossCursor);
  rw = new resize_widget(this);
  rw->put_to_corner();
  cw = new close_widget(this);
  cw->put_to_corner();
  pix = new QPixmap;
  scale_factor = 1.0;
  locked = false;
}

/**************************************************************************
  Minimap_view destructor
**************************************************************************/
minimap_view::~minimap_view()
{
  if (pix) {
    delete pix;
  }
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
static void gui_to_overview(int *ovr_x, int *ovr_y, int gui_x, int gui_y)
{
  double ntl_x, ntl_y;
  const double gui_xd = gui_x, gui_yd = gui_y;
  const double W = tileset_tile_width(tileset);
  const double H = tileset_tile_height(tileset);
  double map_x, map_y;

  if (tileset_is_isometric(tileset)) {
    map_x = (gui_xd * H + gui_yd * W) / (W * H);
    map_y = (gui_yd * W - gui_xd * H) / (W * H);
  } else {
    map_x = gui_xd / W;
    map_y = gui_yd / H;
  }

  if (MAP_IS_ISOMETRIC) {
    ntl_y = map_x + map_y - map.xsize;
    ntl_x = 2 * map_x - ntl_y;
  } else {
    ntl_x = map_x;
    ntl_y = map_y;
  }

  *ovr_x = floor((ntl_x - (double)overview.map_x0) * OVERVIEW_TILE_SIZE);
  *ovr_y = floor((ntl_y - (double)overview.map_y0) * OVERVIEW_TILE_SIZE);

  if (current_topo_has_flag(TF_WRAPX)) {
    *ovr_x = FC_WRAP(*ovr_x, NATURAL_WIDTH * OVERVIEW_TILE_SIZE);
  } else {
    if (MAP_IS_ISOMETRIC) {
      *ovr_x -= OVERVIEW_TILE_SIZE;
    }
  }
  if (current_topo_has_flag(TF_WRAPY)) {
    *ovr_y = FC_WRAP(*ovr_y, NATURAL_HEIGHT * OVERVIEW_TILE_SIZE);
  }
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
  Sets minimap scale to default
**************************************************************************/
void minimap_view::reset()
{
  scale_factor = 1;
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

  if (C_S_RUNNING <= client_state()) {
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
  if (locked) {
    return;
  }
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

/****************************************************************************
  Creates a sprite widget.
****************************************************************************/
sprite_widget::sprite_widget(QWidget *parent) :
  QPushButton(parent),
  pixmap(NULL),
  user_id(-1)
{
  setFixedSize(0, 0);
}

/****************************************************************************
  Destructor.
****************************************************************************/
sprite_widget::~sprite_widget()
{
  // Don't delete pixmap, the tileset owns it.
}

/****************************************************************************
  Sets the sprite this widget represents.
****************************************************************************/
void sprite_widget::set_sprite(const struct sprite *sprite)
{
  if (sprite) {
    // Don't delete pixmap, the tileset owns it.
    pixmap = sprite->pm;
  } else {
    pixmap = new QPixmap();
  }
  // We don't want to be resized.
  setFixedSize(pixmap->size());
  repaint();
}

/****************************************************************************
  Sets an user-defined identifier. It will be passed as argument to signals.
****************************************************************************/
void sprite_widget::set_user_id(int id)
{
  user_id = id;
}

/****************************************************************************
  Emits button_clicked().
****************************************************************************/
void sprite_widget::mouseReleaseEvent(QMouseEvent *ev)
{
  QPushButton::mouseReleaseEvent(ev);
  emit button_clicked(ev->button(), user_id);
  ev->accept();
}

/****************************************************************************
  Draws the sprite.
****************************************************************************/
void sprite_widget::paintEvent(QPaintEvent *ev) {
  if (pixmap) {
    QPainter painter;
    painter.begin(this);
    painter.drawPixmap(0, 0, *pixmap);
    painter.end();
  }
}

/****************************************************************************
  Emits wheel_rolled().
****************************************************************************/
void sprite_widget::wheelEvent(QWheelEvent *ev)
{
  int pixels = ev->pixelDelta().y();
  int degrees = ev->angleDelta().y() / 8;

  if (pixels != 0) {
    emit wheel_rolled(pixels, user_id);
    ev->accept();
  } else if (degrees != 0) {
    emit wheel_rolled(degrees / 15, user_id);
    ev->accept();
  } else {
    ev->ignore();
  }
}

/****************************************************************************
  Stylesheets for the 'Turn Done' button.
****************************************************************************/
static const char *end_turn_area_stylesheet =
  "QPushButton {"
    "background-color: transparent;"
    "border: none;"
    "color: rgb(30, 175, 30);"
    "font-size: %1pt;"
  "}"
  "QPushButton:hover {"
    "color: rgb(38, 225, 38);" // Lighter green
  "}"
  "QPushButton:disabled {"
    "color: rgb(172, 175, 175);" // Gray
  "}";
// Highlighted version, inverts hover/not hover
static const char *end_turn_area_stylesheet_hl =
  "QPushButton {"
    "background-color: transparent;"
    "border: none;"
    "color: rgb(38, 225, 38);"
    "font-size: %1pt;"
  "}"
  "QPushButton:hover {"
    "color: rgb(30, 175, 30);" // Darker green
  "}"
  "QPushButton:disabled {"
    "color: rgb(172, 175, 175);" // Gray
  "}";

/****************************************************************************
  Stylesheets for the information labels.
****************************************************************************/
static const char *info_label_stylesheet =
  "color: rgb(232, 255, 0); background: transparent;";
static const char *res_label_stylesheet =
  "color: rgb(40, 138, 200); background: transparent;";
static const char *res_blink_label_stylesheet =
  "color: rgb(120, 220, 255); background: transparent;";
static const char *time_label_stylesheet =
  "color: rgb(180, 0, 0); background: transparent;";

/****************************************************************************
  Constructor for 'turn done' area
****************************************************************************/
end_turn_area::end_turn_area(QWidget *parent): QFrame(parent)
{
  QFontMetrics fm(this->font());
  QVBoxLayout *layout = new QVBoxLayout(this);
  QHBoxLayout *hbox;
  int i;

  setStyleSheet("QFrame { background-color: rgba(0, 0, 0, 135); }");
  etb_button = new QPushButton();
  ascent_plus_descent = fm.height() + 1;
  etb_button->setStyleSheet(
    QString(end_turn_area_stylesheet).arg(ascent_plus_descent));
  connect(etb_button, &QPushButton::clicked, &key_end_turn);
  hbox = new QHBoxLayout();
  hbox->setSpacing(0);
  hbox->addStretch();
  research_indicator = new sprite_widget();
  hbox->addWidget(research_indicator);
  connect(research_indicator, &QPushButton::clicked,
          &science_report_dialog_popup);
  government_indicator = new sprite_widget();
  government_indicator->setMenu(new gov_menu());
  hbox->addWidget(government_indicator);
  pollution_indicator = new sprite_widget();
  hbox->addWidget(pollution_indicator);
  nuclear_indicator = new sprite_widget();
  hbox->addWidget(nuclear_indicator);
  hbox->addStretch();
  layout->addLayout(hbox);

  hbox = new QHBoxLayout();
  hbox->setSpacing(0);
  hbox->addStretch();
  for (i = 0; i < 10; ++i) {
    tax_indicators[i] = new sprite_widget();
    tax_indicators[i]->set_user_id(i);
    connect(tax_indicators[i], &sprite_widget::button_clicked,
            this, &end_turn_area::change_tax_rate_click);
    connect(tax_indicators[i], &sprite_widget::wheel_rolled,
            this, &end_turn_area::change_tax_rate_wheel);
    hbox->addWidget(tax_indicators[i]);
  }
  hbox->addStretch();
  layout->addLayout(hbox);
  layout->addWidget(etb_button, Qt::AlignCenter);
  etb_button->setText(_("Turn Done"));
  etb_button->setFocusPolicy(Qt::NoFocus);
  setLayout(layout);
}

/****************************************************************************
  Enables/Disables the 'End Turn' button.
****************************************************************************/
void end_turn_area::set_turn_button_enabled(bool enabled)
{
  etb_button->setEnabled(enabled);
}

/****************************************************************************
  Highlights (or not) the 'End Turn' button. One can create a blinking effect
  using a timer.
****************************************************************************/
void end_turn_area::set_highlight_turn_button(bool highlight)
{
  if (highlight) {
    etb_button->setStyleSheet(
      QString(end_turn_area_stylesheet_hl).arg(ascent_plus_descent));
  } else {
    etb_button->setStyleSheet(
      QString(end_turn_area_stylesheet).arg(ascent_plus_descent));
  }
}

/***************************************************************************
  Constructor for clicked_label
***************************************************************************/
clicked_label::clicked_label(QWidget *parent) : QLabel(parent)
{
}

/***************************************************************************
  Mouse event for clicked_label
***************************************************************************/
void clicked_label::mousePressEvent(QMouseEvent *e)
{
  emit clicked();
}

/**************************************************************************
  Constructor for information label
**************************************************************************/
info_label::info_label(QWidget *parent) : fcwidget()
{
  QHBoxLayout *layout;
  QSpacerItem *si;

  setStyleSheet("QFrame { background-color: rgba(0, 0, 0, 195); }");

  setParent(parent);
  layout = new QHBoxLayout();

  eco_info = new QLabel();
  eco_info->setStyleSheet(info_label_stylesheet);
  layout->addWidget(eco_info);
  si = new QSpacerItem(10, 0);
  layout->addSpacerItem(si);
  res_info = new clicked_label();
  res_info->setStyleSheet(res_label_stylesheet);
  layout->addWidget(res_info);
  si = new QSpacerItem(10, 0);
  layout->addSpacerItem(si);
  turn_info = new QLabel();
  turn_info->setStyleSheet(info_label_stylesheet);
  layout->addWidget(turn_info);
  si = new QSpacerItem(10, 0);
  layout->addSpacerItem(si);
  time_label = new QLabel();
  time_label->setStyleSheet(time_label_stylesheet);
  layout->addWidget(time_label);
  si = new QSpacerItem(10, 0);
  layout->addSpacerItem(si);
  timer_active = false;
  res_timer = new QTimer;
  res_timer->setSingleShot(true);
  setLayout(layout);
  connect(res_timer, SIGNAL(timeout()), this, SLOT(blink()));
  connect(res_info, SIGNAL(clicked()), this, SLOT(show_research_tab()));
}


/**************************************************************************
  Destructor for information label
**************************************************************************/
info_label::~info_label()
{
}

/***************************************************************************
  Slot for showing research tab
***************************************************************************/
void info_label::show_research_tab()
{
  science_report_dialog_popup(true);
}

/**************************************************************************
  Sets information about current turn
**************************************************************************/
void info_label::set_turn_info(const QString &info)
{
  turn_info->setText(info);
}

/**************************************************************************
  Sets information about current time
**************************************************************************/
void info_label::set_res_info(const QString &info)
{
  res_info->setText(info);
}

/**************************************************************************
  Sets information about current time
**************************************************************************/
void info_label::set_time_info(const QString &info)
{
  time_label->setText(info);
}

/**************************************************************************
  Sets information about current economy
**************************************************************************/
void info_label::set_eco_info(const QString &info)
{
  eco_info->setText(info);
}

/**************************************************************************
  Updates menu
**************************************************************************/
void info_label::update_menu()
{
  /** Function inherited from abstract parent 
   *  PORTME , if needed */
}

/**************************************************************************
  Updates indicator sprites from tileset
**************************************************************************/
void end_turn_area::update_tileset()
{
  set_indicator_icons(client_research_sprite(),
                      client_warming_sprite(),
                      client_cooling_sprite(),
                      client_government_sprite());
  end_turn_update(); // Updates tax rate indicators
}

/**************************************************************************
  Updates icons in end turn area
**************************************************************************/
void end_turn_area::end_turn_update()
{
  const struct sprite *sprite;
  int d;
  QSize delta;
  QFontMetrics fm(etb_button->font());

  if (client_is_global_observer()) {
    hide();
    return;
  } else {
    show();
  }
  // Update tax rates
  sprite = get_tax_sprite(tileset, O_LUXURY);
  for (d = 0; d < client.conn.playing->economic.luxury / 10; ++d) {
    tax_indicators[d]->set_sprite(sprite);
  }
  sprite = get_tax_sprite(tileset, O_SCIENCE);
  for (; d < (client.conn.playing->economic.science
              + client.conn.playing->economic.luxury) / 10; ++d) {
    tax_indicators[d]->set_sprite(sprite);
  }
  sprite = get_tax_sprite(tileset, O_GOLD);
  for (; d < 10; ++d) {
    tax_indicators[d]->set_sprite(sprite);
  }
  // Set tooltips
  government_indicator->setToolTip(get_government_tooltip());
  nuclear_indicator->setToolTip(get_nuclear_winter_tooltip());
  pollution_indicator->setToolTip(get_global_warming_tooltip());
  research_indicator->setToolTip(get_bulb_tooltip());
  for (d = 0; d < 10; ++d) {
    tax_indicators[d]->setToolTip(_("Shows your current luxury/science/tax "
                                    "rates. Use mouse wheel to change them"));
  }
  setMinimumWidth(qMax(get_tax_sprite(tileset, O_LUXURY)->pm->width() * 10
                  + 25, fm.width(etb_button->text())));
  setMinimumHeight(fm.height() + client_research_sprite()->pm->height()
                   + get_tax_sprite(tileset, O_LUXURY)->pm->height() + 25);
  updateGeometry();
  delta = gui()->mapview_wdg->size() - gui()->end_turn_rect->size();
  move(delta.width(), delta.height());
}

/****************************************************************************
  Slot for sprite_widget::wheel_rolled
****************************************************************************/
void end_turn_area::change_tax_rate_wheel(int delta, int id)
{
  if (id < client.conn.playing->economic.luxury / 10) {
    change_tax_rate(O_LUXURY, delta);
  } else if (id < (client.conn.playing->economic.science / 10
                   + client.conn.playing->economic.luxury / 10)) {
    change_tax_rate(O_SCIENCE, delta);
  } else {
    change_tax_rate(O_GOLD, delta);
  }
}

/****************************************************************************
  Slot for sprite_widget::button_clicked
****************************************************************************/
void end_turn_area::change_tax_rate_click(Qt::MouseButton button, int id)
{
  int delta;

  if (button == Qt::LeftButton) {
    delta = -1;
  } else if (button == Qt::RightButton) {
    delta = +1;
  } else {
    return;
  }

  if (id < client.conn.playing->economic.luxury / 10) {
    change_tax_rate(O_LUXURY, delta);
  } else if (id < (client.conn.playing->economic.science / 10
                   + client.conn.playing->economic.luxury / 10)) {
    change_tax_rate(O_SCIENCE, delta);
  } else {
    change_tax_rate(O_GOLD, delta);
  }
}

/****************************************************************************
  Change the given tax rate by delta.
****************************************************************************/
void end_turn_area::change_tax_rate(output_type_id type, int delta)
{
  int l, s; // Luxury, science

  l = client.conn.playing->economic.luxury / 10;
  s = client.conn.playing->economic.science / 10;

  switch (type) {
  case O_LUXURY:
    l += delta;
    s -= delta;
    break;
  case O_SCIENCE:
    s += delta;
    break;
  case O_GOLD:
    l -= delta;
    break;
  default:
    log_error("Cannot change tax rate different from luxury, science,"
              " and gold.");
    return;
  }

  // Normalize
  l = qMax(qMin(l, 10), 0);
  s = qMax(qMin(s, 10), 0);

  if (l + s <= 10) {
    dsend_packet_player_rates(&client.conn, (10 - l - s) * 10,
                              l * 10, s * 10);
  }
}

/**************************************************************************
  Updates size and size of objects
**************************************************************************/
void info_label::info_update()
{
  QString str;

  if (client_is_global_observer()) {
    return;
  }

  str = get_bulb_tooltip();
  str.remove(0, str.indexOf('\n') + 1);
  str.remove(str.indexOf('('), str.count());
  set_res_info(str);

  if (nullptr != client.conn.playing) {
    struct player_research *research = player_research_get(client_player());

    if (research->researching == A_UNSET && research->tech_goal == A_UNSET
        && research->techs_researched < game.control.num_tech_types
        && !timer_active) {
      res_timer->start(700);
      blink_state = true;
    }
  }

  resize(sizeHint());
}

/**************************************************************************
  Makes research text blinking ( it's slot for timeout() from res_timer)
**************************************************************************/
void info_label::blink()
{
  if (client_is_observer()) {
    res_info->setStyleSheet(res_label_stylesheet);
    res_timer->stop();
    timer_active = false;
    return;
  }

  if (blink_state) {
    res_info->setStyleSheet(res_blink_label_stylesheet);
  } else {
    res_info->setStyleSheet(res_label_stylesheet);
  }

  if (nullptr != client.conn.playing) {
    struct player_research *research = player_research_get(client_player());
    if (timer_active && ( research->researching != A_UNSET
        || research->tech_goal != A_UNSET)) {
      res_timer->stop();
      timer_active = false;
      res_info->setStyleSheet(res_label_stylesheet);
    } else {
      timer_active = true;
      res_timer->start(700);
      blink_state = !blink_state;
    }
  }
}

/****************************************************************************
  Typically an info box is provided to tell the player about the state
  of their civilization.  This function is called when the label is
  changed.
****************************************************************************/
void update_info_label(void)
{
  gui()->update_info_label();
}

/****************************************************************************
  Real update, updates only once per 100 ms.
****************************************************************************/
void fc_client::update_info_label(void)
{
  QString s, eco_info;

  if (current_page() != PAGE_GAME) {
    return;
  }
  if (update_info_timer == nullptr) {
    update_info_timer = new QTimer();
    update_info_timer->setSingleShot(true);
    connect(update_info_timer, SIGNAL(timeout()),
            this, SLOT(update_info_label()));
    update_info_timer->start(100);
    return;
  }

  if (update_info_timer->remainingTime() > 0) {
    return;
  }

  s = QString(_("%1 (Turn:%2)")).arg(textyear(game.info.year),
                                     QString::number(game.info.turn));
  game_info_label->set_turn_info(s);
  set_indicator_icons(client_research_sprite(),
                      client_warming_sprite(),
                      client_cooling_sprite(), client_government_sprite());
  if (client.conn.playing != NULL) {
    if (player_get_expected_income(client.conn.playing) > 0) {
      eco_info = QString(_("Gold:%1 (+%2)"))
           .arg(QString::number(client.conn.playing->economic.gold),
           QString::number(player_get_expected_income(client.conn.playing)));
    } else {
      eco_info = QString(_("Gold:%1 (%2)"))
           .arg(QString::number(client.conn.playing->economic.gold),
           QString::number(player_get_expected_income(client.conn.playing)));
    }
    game_info_label->set_eco_info(eco_info);
  }
  game_info_label->info_update();
  end_turn_rect->end_turn_update();
  delete update_info_timer;
  update_info_timer = nullptr;
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
  gui()->unitinfo_wdg->uupdate(punitlist);
}

/****************************************************************************
  Update the mouse cursor. Cursor type depends on what user is doing and
  pointing.
****************************************************************************/
void update_mouse_cursor(enum cursor_type new_cursor_type)
{
  gui()->mapview_wdg->update_cursor(new_cursor_type);
}

/****************************************************************************
  Update the timeout display.  The timeout is the time until the turn
  ends, in seconds.
****************************************************************************/
void qtg_update_timeout_label(void)
{
  gui()->game_info_label->set_time_info (
    QString(get_timeout_label_text()));
  gui()->game_info_label->resize(gui()->game_info_label->sizeHint());
}

/****************************************************************************
  If do_restore is false it should change the turn button style (to
  draw the user's attention to it).  If called regularly from a timer
  this will give a blinking turn done button.  If do_restore is true
  this should reset the turn done button to the default style.
****************************************************************************/
void update_turn_done_button(bool do_restore)
{
  if (!get_turn_done_button_state()) {
    return;
  }
  gui()->end_turn_rect->set_highlight_turn_button(!do_restore);
}

/**************************************************************************
  Sets indicator icons in information label.
**************************************************************************/
void end_turn_area::set_indicator_icons(const struct sprite *bulb,
                                     const struct sprite *sol,
                                     const struct sprite *flake,
                                     const struct sprite *gov)
{
  research_indicator->set_sprite(bulb);
  government_indicator->set_sprite(gov);
  nuclear_indicator->set_sprite(flake);
  pollution_indicator->set_sprite(sol);
}

/****************************************************************************
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
****************************************************************************/
void set_indicator_icons(struct sprite *bulb, struct sprite *sol,
                         struct sprite *flake, struct sprite *gov)
{
  gui()->end_turn_rect->set_indicator_icons(bulb, sol, flake, gov);
}

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  gui()->minimapview_wdg->update_image();
  return NULL;
}

/****************************************************************************
  Flush the given part of the canvas buffer (if there is one) to the
  screen.
****************************************************************************/
void flush_mapcanvas(int canvas_x, int canvas_y,
                     int pixel_width, int pixel_height)
{
  gui()->mapview_wdg->repaint(canvas_x, canvas_y, pixel_width, pixel_height);
}

/****************************************************************************
  Mark the rectangular region as "dirty" so that we know to flush it
  later.
****************************************************************************/
void dirty_rect(int canvas_x, int canvas_y,
                int pixel_width, int pixel_height)
{
  if (mapview_is_frozen()) {
    return;
  }
  if (num_dirty_rects < MAX_DIRTY_RECTS) {
    dirty_rects[num_dirty_rects].setX(canvas_x);
    dirty_rects[num_dirty_rects].setY(canvas_y);
    dirty_rects[num_dirty_rects].setWidth(pixel_width);
    dirty_rects[num_dirty_rects].setHeight(pixel_height);
    num_dirty_rects++;
  }
}

/****************************************************************************
  Mark the entire screen area as "dirty" so that we can flush it later.
****************************************************************************/
void dirty_all(void)
{
  if (mapview_is_frozen()) {
    return;
  }
  num_dirty_rects = MAX_DIRTY_RECTS;
}

/****************************************************************************
  Flush all regions that have been previously marked as dirty.  See
  dirty_rect and dirty_all.  This function is generally called after we've
  processed a batch of drawing operations.
****************************************************************************/
void flush_dirty(void)
{
  if (mapview_is_frozen()) {
    return;
  }
  if (num_dirty_rects == MAX_DIRTY_RECTS) {
    flush_mapcanvas(0, 0, gui()->mapview_wdg->width(),
                    gui()->mapview_wdg->height());
  } else {
    int i;
    for (i = 0; i < num_dirty_rects; i++) {
      flush_mapcanvas(dirty_rects[i].x(), dirty_rects[i].y(),
                      dirty_rects[i].width(), dirty_rects[i].height());
    }
  }
  num_dirty_rects = 0;
}

/****************************************************************************
  Do any necessary synchronization to make sure the screen is up-to-date.
  The canvas should have already been flushed to screen via flush_dirty -
  all this function does is make sure the hardware has caught up.
****************************************************************************/
void gui_flush(void)
{
  gui()->mapview_wdg->update();
}

/****************************************************************************
  Update (refresh) the locations of the mapview scrollbars (if it uses
  them).
****************************************************************************/
void update_map_canvas_scrollbars(void)
{
  gui()->mapview_wdg->update();
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
  int i;
  science_report *sci_rep;
  QWidget *w;

  gui()->unitinfo_wdg->update_arrow_pix();
  gui()->end_turn_rect->update_tileset();
  update_unit_info_label(get_units_in_focus());
  destroy_city_dialog();
  /* Update science report if open */
  if (gui()->is_repo_dlg_open("SCI")) {
    i = gui()->gimme_index_of("SCI");
    fc_assert(i != -1);
    w = gui()->game_tab_widget->widget(i);
    sci_rep = reinterpret_cast<science_report*>(w);
    sci_rep->reset_tree();
    sci_rep->update_report();
    sci_rep->repaint();
  }
}

/****************************************************************************
  Return the dimensions of the area (container widget; maximum size) for
  the overview.
****************************************************************************/
void get_overview_area_dimensions(int *width, int *height)
{
  *width = 0;
  *height = 0;
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
  gui()->minimapview_wdg->resize(0, 0);
  gui()->minimapview_wdg->resize(gui()->end_turn_rect->width(),
                                 gui()->end_turn_rect->width()
                                 * (static_cast<float>(map.xsize)
                                 / map.ysize));
}

/**************************************************************************
  Constructor for unit_label (shows information about unit) and 
  might call for unit_selection_dialog
  It uses default font for display text with modified size to fit on screen
**************************************************************************/
unit_label::unit_label(QWidget *parent)
{
  setParent(parent);
  arrow_pix = NULL;
  ufont = new QFont;
  w_width = 0;
  selection_area.setWidth(0);
  highlight_pix = false;
  setMouseTracking(true);
  setFixedWidth(0);
  setFixedHeight(0);
  tile_pix =  new QPixmap();
  pix = new QPixmap();
}

/**************************************************************************
  Updates units label (pixmap and text) and calls update() to redraw
  Font size is fixed to match widget size.
**************************************************************************/
void unit_label::uupdate(unit_list *punits)
{
  struct city *pcity;
  struct unit *punit;
  struct player *owner;
  struct canvas *unit_pixmap;
  struct canvas *tile_pixmap;
  no_units = false;
  one_unit = true;
  setFixedHeight(56);

  if (punits == nullptr) {
    return;
  }
  if (unit_list_size(punits) == 0 || C_S_OVER == client_state()) {
    unit_label1 = "";
    unit_label2 = "";
    no_units = true;
    update();
    return;
  }
  punit = unit_list_get(punits, 0);
  if (unit_list_size(punits) == 1) {
    if (unit_list_size(unit_tile(punit)->units) > 1) {
      one_unit = false;
    }
  }

  ufont->setPixelSize(height() / 3);
  ul_units = punits;
  unit_label1 = get_unit_info_label_text1(punits);
  owner = unit_owner(punit);
  pcity = player_city_by_number(owner, punit->homecity);
  if (pcity != NULL && unit_list_size(punits) == 1) {
    /* TRANS: unitX from cityZ */
    unit_label1 = QString(_("%1 from %2"))
                   .arg(get_unit_info_label_text1(punits), city_name(pcity));
  }
  /* TRANS: HP - hit points */
  unit_label2 = QString(_("%1 HP:%2/%3")).arg(unit_activity_text(
                   unit_list_get(punits, 0)),
                   QString::number(punit->hp),
                   QString::number(unit_type(punit)->hp));

  punit = head_of_units_in_focus();
  if (punit) {
    if (tileset_is_isometric(tileset)){
      unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_tile_height(tileset) * 3 / 2);
    } else {
      unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_tile_height(tileset));
    }
    unit_pixmap->map_pixmap.fill(Qt::transparent);
    put_unit(punit, unit_pixmap, 0, 0);
    *pix = (&unit_pixmap->map_pixmap)->scaledToHeight(height());
    w_width = pix->width() + 1;

    if (tileset_is_isometric(tileset)){
      tile_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_tile_height(tileset) * 2);
    } else {
      tile_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_tile_height(tileset));
    }
    tile_pixmap->map_pixmap.fill(QColor(0 , 0 , 0 , 85));
    put_terrain(punit->tile, tile_pixmap, 0, 0);
    *tile_pix = (&tile_pixmap->map_pixmap)->scaledToHeight(height());
     w_width = w_width + tile_pix->width() + 1;
     qtg_canvas_free(tile_pixmap);
     qtg_canvas_free(unit_pixmap);
  }

  QFontMetrics fm(*ufont);
  if (arrow_pix == NULL) {
    arrow_pix = get_arrow_sprite(tileset, ARROW_PLUS)->pm;
    *arrow_pix = arrow_pix->scaledToHeight(height());
  }
  w_width += qMax(fm.width(unit_label1), fm.width(unit_label2));
  if (one_unit == false) {
    w_width += arrow_pix->width() + 1;
  }
  w_width += 5;
  setFixedWidth(w_width);
  move(parentWidget()->width() - width(), 0);
  update();
}

/**************************************************************************
  Mouse press event for unit label, it calls unit selector
**************************************************************************/
void unit_label::mousePressEvent(QMouseEvent *event)
{
  struct unit *punit = unit_list_get(ul_units, 0);

  if (event->button() == Qt::LeftButton) {
    if (selection_area.contains(event->x(), event->y())) {
      if (punit != NULL && selection_area.width() > 0) {
        unit_select_dialog_popup(unit_tile(punit));
      }
    }
  }
}

/**************************************************************************
  Mouse move event for unit label, used for highlighting pixmap of 
  unit selector
**************************************************************************/
void unit_label::mouseMoveEvent(QMouseEvent *event)
{
  bool redraw = false;

  if (selection_area.contains(event->x(), event->y())) {
    if (highlight_pix == false) {
      redraw = true;
    }
    highlight_pix = true;
  } else {
    if (highlight_pix == true) {
      redraw = true;
    }
    highlight_pix = false;
  }
  if (redraw) {
    update();
  }
}

/**************************************************************************
  Paint event for unit label
**************************************************************************/
void unit_label::paint(QPainter *painter, QPaintEvent *event)
{
  int w;
  QPainter::CompositionMode comp_mode = painter->compositionMode();
  QPen pen;
  QFontMetrics fm(*ufont);

  selection_area.setWidth(0);
  pen.setWidth(1);
  pen.setColor(QColor(232, 255, 0));
  painter->setBrush(QColor(0, 0, 0, 135));
  painter->drawRect(0, 0, w_width, height());
  painter->setFont(*ufont);
  painter->setPen(pen);

  w = 0;
  if (!no_units) {
    painter->drawPixmap(w, (height() - pix->height()) / 2, *pix);
    w = w + pix->width() + 1;
    if (one_unit == false) {
      if (highlight_pix) {
        painter->setCompositionMode(QPainter::CompositionMode_HardLight);
      }
      painter->drawPixmap(w, 0, *arrow_pix);
      selection_area.setRect(w, 5, arrow_pix->width(),
                             arrow_pix->height() - 10);
      w = w + arrow_pix->width() + 1;
    }
    painter->setCompositionMode(comp_mode);
    painter->drawText(w, height() / 2.5, unit_label1);
    painter->drawText(w, height() - 8, unit_label2);
    w = w + 5 + qMax(fm.width(unit_label1), fm.width(unit_label2));
    if (tile_pix != NULL) {
      painter->drawPixmap(w, (height() - pix->height()) / 2, *tile_pix);
      w = tile_pix->width() + 1;
    }
  } else {
    painter->drawText(5, height() / 3 + 5, _("No units selected."));
  }
}

/**************************************************************************
  Updates unit selector pixmap, necessary when changing tileset
**************************************************************************/
void unit_label::update_arrow_pix()
{
  arrow_pix = get_arrow_sprite(tileset, ARROW_PLUS)->pm;
  *arrow_pix = arrow_pix->scaledToHeight(height());
}

/**************************************************************************
  Paint event for unit label, it calls paint(...)
**************************************************************************/
void unit_label::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  paint(&painter, event);
  painter.end();
}

/**************************************************************************
  Updates menu for unit label
**************************************************************************/
void unit_label::update_menu()
{
  /* PORTME, if needed */
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

/**************************************************************************
  Constructor for info_tile
**************************************************************************/
info_tile::info_tile(struct tile *ptile, QWidget *parent): QLabel(parent)
{
  setParent(parent);
  info_font = gui()->fc_fonts.get_font("gui_qt_font_comment_label");
  itile = ptile;
  calc_size();
}

/**************************************************************************
  Calculates size of info_tile and moves it to be fully visible
**************************************************************************/
void info_tile::calc_size()
{
  QFontMetrics fm(*info_font);
  QString str;
  int hh = tileset_tile_height(tileset);
  int fin_x;
  int fin_y;
  int x, y;
  int w = 0;

  str = popup_info_text(itile);
  str_list = str.split("\n");

  foreach(str, str_list) {
    w = qMax(w, fm.width(str));
  }
  setFixedHeight(str_list.count() * (fm.height() + 5));
  setFixedWidth(w + 10);
  if (tile_to_canvas_pos(&x, &y, itile)) {
    fin_x = x;
    fin_y = y;
    if (y - height() > 0) {
      fin_y = y - height();
    } else {
      fin_y = y + hh;
    }
    if (x + width() > parentWidget()->width()) {
      fin_x = parentWidget()->width() - width();
    }
    move(fin_x, fin_y);
  }
}

/**************************************************************************
  Redirected paint event for info_tile
**************************************************************************/
void info_tile::paint(QPainter *painter, QPaintEvent *event)
{
  QPen pen;
  QFontMetrics fm(*info_font);
  int pos, h;

  h = fm.height();
  pos = h;
  pen.setWidth(1);
  pen.setColor(QColor(232, 255, 0));
  painter->setBrush(QColor(0, 0, 0, 205));
  painter->drawRect(0, 0, width(), height());
  painter->setPen(pen);
  painter->setFont(*info_font);
  for (int i = 0; i < str_list.count(); i++) {
    painter->drawText(5, pos, str_list.at(i));
    pos = pos + 5 + h;
  }
}

/**************************************************************************
  Paint event for info_tile
**************************************************************************/
void info_tile::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  paint(&painter, event);
  painter.end();
}
