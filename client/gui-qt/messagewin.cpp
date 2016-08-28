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
#include <QApplication>
#include <QHeaderView>
#include <QStyleFactory>

// gui-qt
#include "fc_client.h"
#include "messagewin.h"
#include "qtg_cxxside.h"
#include "sprite.h"


/***************************************************************************
  info_tab constructor
***************************************************************************/
info_tab::info_tab(QWidget *parent)
{
  setParent(parent);
  setStyleSheet("QPushButton {background-color: transparent;}"
    "QPushButton {color: #038713;}"
    "QPushButton:enabled {color: #038713;}"
    "QPushButton:hover {background-color: blue;}"
    "QPushButton {border: noborder;}");

  layout = new QGridLayout;
  msgwdg = new messagewdg(this);
  layout->addWidget(msgwdg, 0, 0);
  chtwdg = new chatwdg(this);
  layout->addWidget(chtwdg, 1, 0);
  layout->setHorizontalSpacing(0);
  layout->setVerticalSpacing(0);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->setVerticalSpacing(0);
  setLayout(layout);
  resize_mode = false;
  move_mode = false;
  resx = false;
  resy = false;
  setMouseTracking(true);
}


/***************************************************************************
  Paints semi-transparent background
***************************************************************************/
void info_tab::paint(QPainter *painter, QPaintEvent *event)
{
  painter->setBrush(QColor(0, 0, 0, 175));
  painter->drawRect(0, 0, width(), height());
}

/***************************************************************************
  Sets chat to default size of 3 lines
***************************************************************************/
void info_tab::restore_chat()
{
  msgwdg->setFixedHeight(qMax(0,(height() - chtwdg->default_size(3))));
  chtwdg->setFixedHeight(chtwdg->default_size(3));
  chat_maximized = false;
  chtwdg->scroll_to_bottom();
}

/***************************************************************************
  Maximizes size of chat
***************************************************************************/
void info_tab::maximize_chat()
{
  msgwdg->setFixedHeight(0);
  chtwdg->setFixedHeight(height());
  chat_maximized = true;
  chtwdg->scroll_to_bottom();
}

/***************************************************************************
  Paint event for info_tab
***************************************************************************/
void info_tab::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  paint(&painter, event);
  painter.end();
}

/***************************************************************************
  Checks if info_tab can be moved
***************************************************************************/
void info_tab::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton) {
    cursor = event->globalPos() - geometry().topLeft();
    if (event->y() > 0 && event->y() < 25 && event->x() > width() - 25
        && event->x() < width()) {
      move_mode = true;
      return;
    }
    if (event->y() > 0 && event->y() < 5){
      resize_mode = true;
      resy = true;
    } else if (event->x() > width() - 5 && event->x() < width()){
      resize_mode = true;
      resx = true;
    }
  }
  event->setAccepted(true);
}

/***************************************************************************
  Restores cursor when resizing is done
***************************************************************************/
void info_tab::mouseReleaseEvent(QMouseEvent* event)
{
  if (resize_mode) {
    resize_mode = false;
    resx = false;
    resy = false;
    setCursor(Qt::ArrowCursor);
    gui()->qt_settings.chat_width = width() * 100 / mapview.width;
    gui()->qt_settings.chat_height = height() * 100 / mapview.height;
  }
  if (move_mode) {
    move_mode = false;
  }

}

/**************************************************************************
  Called when mouse moved (mouse track is enabled).
  Used to resizing info_tab.
**************************************************************************/
void info_tab::mouseMoveEvent(QMouseEvent *event)
{
  if ((event->buttons() & Qt::LeftButton) && resize_mode && resy) {
    QPoint to_move;
    int newheight = event->globalY() - cursor.y() - geometry().y();
    resize(width(), this->geometry().height()-newheight);
    to_move = event->globalPos() - cursor;
    move(this->x(), to_move.y());
    setCursor(Qt::SizeVerCursor);
    restore_chat();
  } else if ((event->buttons() & Qt::LeftButton) && resize_mode && resx) {
    resize(event->x(), height());
    setCursor(Qt::SizeHorCursor);
  } else if (event->x() > width() - 5 && event->x() < width()) {
    setCursor(Qt::SizeHorCursor);
  } else if (event->y() > 0 && event->y() < 5) {
    setCursor(Qt::SizeVerCursor);
  } else if (move_mode && (event->buttons() & Qt::LeftButton)) {
    setCursor(Qt::SizeAllCursor);
    move(event->globalPos() - cursor);
  } else {
    setCursor(Qt::ArrowCursor);
  }
  event->setAccepted(true);
}


/***************************************************************************
  Inherited from abstract parent, does nothing here
***************************************************************************/
void info_tab::update_menu()
{
}


/***************************************************************************
  Messagewdg constructor
***************************************************************************/
messagewdg::messagewdg(QWidget *parent): QWidget(parent)
{
  QPalette palette;
  layout = new QGridLayout;
  setStyle(QStyleFactory::create("fusion"));
  setStyleSheet("QScrollBar:vertical "
                "{border: 1px solid #90A4FF; background: transparent;}"
                "QScrollBar::sub-line:vertical {width: 0px;height: 0px}"
                "QScrollBar::sub-page:vertical {width: 0px;height: 0px}"
                "QScrollBar::add-line:vertical {width: 0px;height: 0px}"
                "QScrollBar::add-page:vertical {width: 0px;height: 0px}"
                "QScrollBar::handle:vertical {background: #90A4FF;"
                "min-height: 20px}"
                "QTableWidget {background-color: transparent;}"
                "QTableWidget::item:hover {background: #107511;}"
                "QTableCornerButton::section "
                "{background-color: transparent;}");
  mesg_table = new QTableWidget;
  mesg_table->setColumnCount(1);
  mesg_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mesg_table->verticalHeader()->setVisible(false);
  mesg_table->setSelectionMode(QAbstractItemView::SingleSelection);
  mesg_table->horizontalHeader()->setStretchLastSection(true);
  mesg_table->horizontalHeader()->setVisible(false);
  mesg_table->setShowGrid(false);
  layout->addWidget(mesg_table, 0, 2, 1, 1);
  setLayout(layout);

  /* dont highlight show current cell - set the same colors*/
  palette.setColor(QPalette::Highlight, QColor(0, 0, 0, 0));
  palette.setColor(QPalette::HighlightedText, QColor(205, 206, 173));
  palette.setColor(QPalette::Text, QColor(205, 206, 173));

  mesg_table->setPalette(palette);
  connect(mesg_table->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(item_selected(const QItemSelection &,
                             const QItemSelection &)));
  setMouseTracking(true);
}

/***************************************************************************
  Slot executed when selection on meg_table has changed
***************************************************************************/
void messagewdg::item_selected(const QItemSelection &sl,
                               const QItemSelection &ds)
{
  const struct message *pmsg;
  int i, j;
  QFont f;
  QModelIndex index;
  QModelIndexList indexes = sl.indexes();
  QTableWidgetItem *item;

  if (indexes.isEmpty()) {
    return;
  }
  index = indexes.at(0);
  i = index.row();
  pmsg = meswin_get_message(i);
  if (i > -1 && pmsg != NULL) {
    if (QApplication::mouseButtons() == Qt::LeftButton
        || QApplication::mouseButtons() == Qt::RightButton) {
      meswin_set_visited_state(i, true);
      item = mesg_table->item(i, 0);
      f = item->font();
      f.setItalic(true);
      item->setFont(f);
    }
    if (QApplication::mouseButtons() == Qt::LeftButton && pmsg->location_ok) {
      meswin_goto(i);
    }
    if (QApplication::mouseButtons() == Qt::RightButton && pmsg->city_ok) {
      meswin_popup_city(i);
    }
    if (QApplication::mouseButtons() == Qt::RightButton
        && pmsg->event == E_DIPLOMACY) {
      j = gui()->gimme_index_of("DDI");
      gui()->game_tab_widget->setCurrentIndex(j);
    }
  }
  mesg_table->clearSelection();
}

/***************************************************************************
  Paints semi-transparent background
***************************************************************************/
void messagewdg::paint(QPainter *painter, QPaintEvent *event)
{
  painter->setBrush(QColor(0, 0, 0, 35));
  painter->drawRect(0, 0, width(), height());
}

/***************************************************************************
  Paint event for messagewdg
***************************************************************************/
void messagewdg::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  paint(&painter, event);
  painter.end();
}

/***************************************************************************
  Clears and removes mesg_table all items
***************************************************************************/
void messagewdg::clr()
{
  mesg_table->clearContents();
  mesg_table->setRowCount(0);
}

/***************************************************************************
  Adds news message to mesg_table
***************************************************************************/
void messagewdg::msg(const struct message *pmsg)
{
  int i;
  struct sprite *icon;
  QFont f;
  QTableWidgetItem *item;

  item = new QTableWidgetItem;
  item->setText(pmsg->descr);
  i = mesg_table->rowCount();
  mesg_table->insertRow(i);
  if (pmsg->visited) {
    f = item->font();
    f.setItalic(true);
    item->setFont(f);
  }
  icon = get_event_sprite(tileset, pmsg->event);
  if (icon != NULL) {
    pix = icon->pm;
    item->setIcon(QIcon(*pix));
  }
  mesg_table->setItem(i, 0, item);
  msg_update();
  mesg_table->scrollToBottom();
}

/***************************************************************************
  Updates mesg_table painting
***************************************************************************/
void messagewdg::msg_update()
{
  mesg_table->resizeRowsToContents();
  update();
}

/***************************************************************************
  Resize event for messagewdg
***************************************************************************/
void messagewdg::resizeEvent(QResizeEvent* event)
{
  msg_update();
}

/**************************************************************************
  Display the message dialog.  Optionally raise it.
  Typically triggered by F10.
**************************************************************************/
void meswin_dialog_popup(bool raise)
{
  /* PORTME */
}

/**************************************************************************
  Return whether the message dialog is open.
**************************************************************************/
bool meswin_dialog_is_open(void)
{
  /* PORTME */
  return true;
}

/**************************************************************************
  Do the work of updating (populating) the message dialog.
**************************************************************************/
void real_meswin_dialog_update(void)
{
  int  i, num;
  const struct message *pmsg;

  if (gui()->infotab == NULL) {
    return;
  }
  gui()->infotab->msgwdg->clr();
  num = meswin_get_num_messages();
  for (i = 0; i < num; i++) {
    pmsg = meswin_get_message(i);
    gui()->infotab->msgwdg->msg(pmsg);
  }
  gui()->infotab->msgwdg->msg_update();

}
