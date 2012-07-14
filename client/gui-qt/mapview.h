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

#ifndef FC__MAPVIEW_H
#define FC__MAPVIEW_H

// In this case we have to include fc_config.h from header file.
// Some other headers we include demand that fc_config.h must be
// included also. Usually all source files include fc_config.h, but
// there's moc generated meta_mapview.cpp file which does not.
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

extern "C" {
#include "mapview_g.h"
}

// Qt
#include <QQueue>
#include <QObject>
#include <QTimer>
#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>

/**************************************************************************
  Struct used for idle callback to execute some callbacks later
**************************************************************************/
struct call_me_back {
  void (*callback) (void *data);
  void *data;
};

/**************************************************************************
  Class to handle idle callbacks
**************************************************************************/
class mr_idle : public QObject
{
  Q_OBJECT
public:
  mr_idle();
  void add_callback(call_me_back* cb);
  QQueue<call_me_back*> callback_list;
private slots:
  void idling();
private:
  QTimer timer;
};

/**************************************************************************
  QWidget used for displaying map
**************************************************************************/
class map_view : public QWidget
{
public:
  map_view();
  void paint(QPainter *painter, QPaintEvent *event);

protected:
  void paintEvent(QPaintEvent *event);
  void keyPressEvent(QKeyEvent * event);
  void resizeEvent(QResizeEvent * event);
  void mousePressEvent(QMouseEvent *event);

private:
  QBrush background;

};

/**************************************************************************
  Information label about civilization, turn, time etc
**************************************************************************/
class info_label : public QLabel
{
  Q_OBJECT
  QHBoxLayout* layout;

  QLabel* turn_info;
  QLabel* time_label;
  QLabel* indicator_icons;
  QLabel* eco_info;

public:
  info_label();
  void set_turn_info(QString);
  void set_time_info(QString);
  void set_eco_info(QString);
  void set_indicator_icons(QPixmap*, QPixmap*, QPixmap*, QPixmap*);
  QPushButton* end_turn_button;
  QSize sizeHint() const;
  QLabel* rates_label;

private:

private slots:
  void slot_end_turn_done_button();
};


void mapview_freeze(void);
void mapview_thaw(void);
bool mapview_is_frozen(void);
void pixmap_put_overlay_tile(int canvas_x, int  canvas_y,
                             struct sprite *ssprite);

#endif /* FC__MAPVIEW_H */
