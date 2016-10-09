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
#include <QGridLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QSpacerItem>

#include "hudwidget.h"
#include "fonts.h"

/****************************************************************************
  Custom message box constructor
****************************************************************************/
hud_message_box::hud_message_box(QWidget *parent): QMessageBox(parent)
{
  int size;
  setWindowFlags(Qt::Popup | Qt::WindowStaysOnTopHint);
  f_text = *fc_font::instance()->get_font(fonts::default_font);
  f_title = *fc_font::instance()->get_font(fonts::default_font);

  size = f_text.pointSize();
  if (size > 0) {
    f_text.setPointSize(size * 4 / 3);
    f_title.setPointSize(size * 3 / 2);
  } else {
    size = f_text.pixelSize();
    f_text.setPixelSize(size * 4 / 3);
    f_title.setPointSize(size * 3 / 2);
  }
  f_title.setBold(true);
  f_title.setCapitalization(QFont::SmallCaps);
  fm_text = new QFontMetrics(f_text);
  fm_title = new QFontMetrics(f_title);
  top = 0;
  hide();
  mult = 1;
}

/****************************************************************************
  Sets text and title and shows message box
****************************************************************************/
void hud_message_box::set_text_title(QString s1, QString s2)
{
  QSpacerItem *spacer;
  QGridLayout *layout;
  int w, w2, h;

  if (s1.contains('\n')) {
    int i;
    i = s1.indexOf('\n');
    cs1 = s1.left(i);
    cs2 = s1.right(s1.count() - i);
    mult = 2;
    w2 = qMax(fm_text->width(cs1), fm_text->width(cs2));
    w = qMax(w2, fm_title->width(s2));
  } else {
    w = qMax(fm_text->width(s1), fm_title->width(s2));
  }
  w = w + 20;
  h = mult * (fm_text->height() * 3 / 2) + 2 * fm_title->height();
  top = 2 * fm_title->height();
  spacer = new QSpacerItem(w, 0, QSizePolicy::Minimum,
                           QSizePolicy::Expanding);
  layout = (QGridLayout *)this->layout();
  layout->addItem(spacer, layout->rowCount(), 0, 1, layout->columnCount());
  spacer = new QSpacerItem(0, h, QSizePolicy::Expanding,
                           QSizePolicy::Minimum);
  layout->addItem(spacer, 0, 0, 1, layout->columnCount());

  text = s1;
  title = s2;
  move((parentWidget()->width() - w) / 2,
       (parentWidget()->height() - h) / 2);
  show();
  m_timer.start();
  startTimer(20);
}

/****************************************************************************
  Timer event used to animate message box
****************************************************************************/
void hud_message_box::timerEvent(QTimerEvent *event)
{
  m_animate_step = m_timer.elapsed() / 40;
  update();
}

/****************************************************************************
  Paint event for custom message box
****************************************************************************/
void hud_message_box::paintEvent(QPaintEvent *event)
{
  QPainter p;
  QRect rx, ry, rfull;
  QLinearGradient g;
  QColor c1;
  QColor c2;
  int step;


  step = m_animate_step % 300;
  if (step > 150) {
    step = step - 150;
    step = 150 - step;
  }
  step = step + 30;

  rfull = QRect(2 , 2, width() - 4 , height() - 4);
  rx = QRect(2 , 2, width() - 4 , top);
  ry = QRect(2 , top, width() - 4, height() - top - 4);

  c1 = QColor(palette().color(QPalette::Highlight));
  c2 = QColor(palette().color(QPalette::AlternateBase));
  c1.setAlpha(step);
  c2.setAlpha(step);

  g = QLinearGradient(0 , 0, width(), height());
  g.setColorAt(0, c1);
  g.setColorAt(1, c2);

  p.begin(this);
  p.fillRect(rx, QColor(palette().color(QPalette::Highlight)));
  p.fillRect(ry, QColor(palette().color(QPalette::AlternateBase)));
  p.fillRect(rfull, g);
  p.setFont(f_title);
  p.drawText((width() - fm_title->width(title)) / 2,
             fm_title->height() * 4 / 3, title);
  p.setFont(f_text);
  if (mult == 1) {
    p.drawText((width() - fm_text->width(text)) / 2,
              2 * fm_title->height() + fm_text->height() * 4 / 3, text);
  } else {
    p.drawText((width() - fm_text->width(cs1)) / 2,
              2 * fm_title->height() + fm_text->height() * 4 / 3, cs1);
    p.drawText((width() - fm_text->width(cs2)) / 2,
              2 * fm_title->height() + fm_text->height() * 8 / 3, cs2);
  }
  p.end();
  event->accept();
}

