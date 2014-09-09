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
    "QPushButton {min-width: 80px;}"
    "QPushButton {border: noborder;}");

  layout = new QGridLayout;
  msg_button = new QPushButton;
  msg_button->setText(_("Messages"));
  chat_button = new QPushButton;
  chat_button->setText(_("Chat"));
  hide_button = new QPushButton(
                    style()->standardIcon(QStyle::SP_TitleBarMinButton), "");

  layout->addWidget(hide_button, 1, 0, 1, 1);
  layout->addWidget(msg_button, 1, 1, 1, 4);
  layout->addWidget(chat_button, 1, 5, 1, 5);
  msgwdg = new messagewdg(this);
  layout->addWidget(msgwdg, 0, 0, 1, 5);
  layout->setRowStretch(0, 10);
  chtwdg = new chatwdg(this);
  layout->addWidget(chtwdg, 0, 5, 1, 5);
  layout->setColumnStretch(6, 3);
  layout->setColumnStretch(4, 3);
  layout->setHorizontalSpacing(0);
  layout->setVerticalSpacing(0);
  layout->setContentsMargins(0, 0, 0, 0);
  setLayout(layout);
  hidden_chat = false;
  hidden_mess = false;
  hidden_state = false;
  layout_changed = false;
  resize_mode = false;
  connect(hide_button, SIGNAL(clicked()), SLOT(hide_me()));
  connect(msg_button, SIGNAL(clicked()), SLOT(activate_msg()));
  connect(chat_button, SIGNAL(clicked()), SLOT(activate_chat()));
  resx = false;
  resy = false;
  chat_stretch = 5;
  msg_stretch = 5;
}

/***************************************************************************
  Paints semi-transparent background
***************************************************************************/
void info_tab::paint(QPainter *painter, QPaintEvent *event)
{
  painter->setBrush(QColor(0, 0, 0, 135));
  painter->drawRect(0, 0, width(), height());
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
void info_tab::mousePressEvent(QMouseEvent * event)
{
  if (event->button() == Qt::LeftButton) {
    cursor = event->globalPos() - geometry().topLeft();
    if (event->y() > 0 && event->y() < 20){
      resize_mode = true;
      resy = true;
    } else if (event->x() > width() - 20 && event->x() < width()){
      resize_mode = true;
      resx = true;
    }
  }
  event->setAccepted(false);
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
  }
}

/**************************************************************************
  Called when mouse button was pressed. Used to moving info_tab.
**************************************************************************/
void info_tab::mouseMoveEvent(QMouseEvent *event)
{
  if ((event->buttons() & Qt::LeftButton) && resize_mode && resy) {
    resize(width(), gui()->mapview_wdg->height()
           - event->globalPos().y() + cursor.y());
    move(0, event->globalPos().y() - cursor.y());
    setCursor(Qt::SizeVerCursor);
  } else if ((event->buttons() & Qt::LeftButton) && resize_mode && resx) {
    resize(event->x(), height());
    move(0, gui()->mapview_wdg->height() - height());
    setCursor(Qt::SizeHorCursor);
  }
}

/***************************************************************************
  Moves hide and chat button to another cell, or restores it
***************************************************************************/
void info_tab::change_layout()
{
  if (layout_changed) {
    layout->addWidget(hide_button, 1, 0, 1, 1);
    layout->addWidget(chat_button, 1, 5, 1, 5);
    layout_changed =  false;
  } else {
    layout->addWidget(hide_button, 1, 5, 1, 1);
    layout->addWidget(chat_button, 1, 6, 1, 4);
    layout_changed =  true;
  }
}

/***************************************************************************
  Chat button was pressed
  Changes stretch value for chat (increases chat's width)
***************************************************************************/
void info_tab::activate_chat()
{
  int i;

  if (hidden_mess){
    return;
  }
  i = layout->columnStretch(6);
  i++;
  i = qMin(i, 5);
  layout->setColumnStretch(6, i);
  chat_stretch = i;
  i = layout->columnStretch(4);
  i--;
  i = qMax(i, 1);
  layout->setColumnStretch(4, i);
  msg_stretch = i;

}

/***************************************************************************
  Hides or restores chat widget
***************************************************************************/
void info_tab::hide_chat(bool hyde)
{
  if (hyde == true) {
    chtwdg->hide();
    chat_button->hide();
    gui()->menu_bar->chat_status->setChecked(false);
    layout->setColumnStretch(4, 999);
    hidden_chat = true;
  } else {
    chtwdg->show();
    chat_button->show();
    gui()->menu_bar->chat_status->setChecked(true);
    layout->setColumnStretch(4, 3);
    hidden_chat = false;
  }
}

/***************************************************************************
  Hides or restores messages widget
***************************************************************************/
void info_tab::hide_messages(bool hyde)
{
  if (hyde == true) {
    msgwdg->hide();
    msg_button->hide();
    if (!layout_changed) {
      change_layout();
    }
    gui()->menu_bar->messages_status->setChecked(false);
    layout->setColumnStretch(6, 999);
    hidden_mess = true;
  } else {
    msgwdg->show();
    msg_button->show();
    if (layout_changed) {
      change_layout();
    }
    gui()->menu_bar->messages_status->setChecked(true);
    layout->setColumnStretch(6, 3);
    hidden_mess = false;
  }
}

/***************************************************************************
  Messages button was pressed.
  Changes stretch value for messages (increases messages width)
***************************************************************************/
void info_tab::activate_msg()
{
  int i;
  if (hidden_chat) {
    return;
  }
  i = layout->columnStretch(4);
  i++;
  i = qMin(i, 5);
  msg_stretch = i;
  layout->setColumnStretch(4, i);
  i = layout->columnStretch(6);
  i--;
  i = qMax(i, 1);
  layout->setColumnStretch(6, i);
  chat_stretch = i;
}

/***************************************************************************
  Inherited from abstract parent, does nothing here
***************************************************************************/
void info_tab::update_menu()
{
}

/***************************************************************************
  Hides both chat and messages window
***************************************************************************/
void info_tab::hide_me()
{
  if (!hidden_state) {
    whats_hidden = 0;
    if (hidden_mess && !hidden_chat) {
      whats_hidden = 1;
    }
    if (hidden_chat && !hidden_mess) {
      whats_hidden = 2;
    }
    hide_messages(true);
    hide_chat(true);
    last_size.setWidth(width());
    last_size.setHeight(height());
    resize(width(), hide_button->fontMetrics().height());
    move(0 , gui()->mapview_wdg->size().height()
         - hide_button->fontMetrics().height());
    hide_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    hidden_state = true;
    if (layout_changed) {
      change_layout();
    }
  } else {
    resize(last_size);
    move(0 , gui()->mapview_wdg->size().height() - last_size.height());
    hide_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));
    hidden_state = false;
    if (layout_changed) {
      change_layout();
    }
    switch (whats_hidden) {
    case 0:
      hide_messages(false);
      hide_chat(false);
      layout->setColumnStretch(4, msg_stretch);
      layout->setColumnStretch(6, chat_stretch);
      break;
    case 1:
      hide_messages(true);
      hide_chat(false);
      break;
    case 2:
      hide_messages(false);
      hide_chat(true);
      break;
    }
  }
}

/***************************************************************************
  Messagewdg constructor
***************************************************************************/
messagewdg::messagewdg(QWidget *parent): QWidget(parent)
{
  QPalette palette;
  layout = new QGridLayout;
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
}

/***************************************************************************
  Slot executed when selection on meg_table has changed
***************************************************************************/
void messagewdg::item_selected(const QItemSelection &sl,
                               const QItemSelection &ds)
{
  const struct message *pmsg;
  int i;
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
  mesg_table->setItem(i, 0, item);
  mesg_table->scrollToBottom();
  mesg_table->resizeRowToContents(i);
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
