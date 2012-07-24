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
#include "sprite.h"
#include "text.h"

// qui-qt
#include "qtg_cxxside.h"

const char*get_timeout_label_text();
static int mapview_frozen_level = 0;
extern struct canvas *canvas;

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

  // painter.setRenderHint(QPainter::Antialiasing);
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
  /* PORTME */
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
  /* PORTME */
  *width = 0;
  *height = 0;
}

/****************************************************************************
  Called when the map size changes. This may be used to change the
  size of the GUI element holding the overview canvas. The
  overview.width and overview.height are updated if this function is
  called.
****************************************************************************/
void overview_size_changed(void)
{
  /* PORTME */
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
