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

//Qt
#include <QApplication>
#include <QScrollBar>
#include <QStyleFactory>

// common
#include "chat.h"

// client
#include "climisc.h"      /* for write_chatline_content */
#include "climap.h"
#include "control.h"
#include "connectdlg_common.h"
#include "game.h"

// gui-qt
#include "colors.h"
#include "fc_client.h"
#include "gui_main.h"
#include "qtg_cxxside.h"

#include "chatline.h"

static bool gui_qt_allied_chat_only = true;
static bool is_plain_public_message(QString s);
/***************************************************************************
  Constructor for chatwdg
***************************************************************************/
chatwdg::chatwdg(QWidget *parent)
{
  QVBoxLayout *vb;
  QHBoxLayout *hl;
  QSpacerItem *si;
  setStyle(QStyleFactory::create("fusion"));
  setStyleSheet("QTextEdit {background-color: transparent;}"
    "QTextEdit {color: #cdcead;}"
    "QTextEdit {background-attachment: fixed;}"
    "QScrollBar:vertical { border: 1px solid #90A4FF; "
    "background: transparent;}"
    "QScrollBar::sub-line:vertical {width: 0px;height: 0px}"
    "QScrollBar::sub-page:vertical {width: 0px;height: 0px}"
    "QScrollBar::add-line:vertical {width: 0px;height: 0px}"
    "QScrollBar::add-page:vertical {width: 0px;height: 0px}"
    "QScrollBar::handle:vertical {background: #90A4FF; min-height: 20px}"
    "QLineEdit {background-color: transparent;}"
    "QLineEdit {color: #cdcead;}"
    "QLineEdit {border: 1px solid gray;}"
    "QCheckBox {color: #5d4e4d;}");

  setParent(parent);
  si = new QSpacerItem(0,0,QSizePolicy::Expanding);
  cb = new QCheckBox(_("Allies only"));
  cb->setChecked(gui_qt_allied_chat_only);
  vb = new QVBoxLayout;
  hl = new QHBoxLayout;
  chat_line = new QLineEdit;
  chat_output = new QTextBrowser;
  remove_links = new QPushButton(_("Clear links"));
  remove_links->setStyleSheet("QPushButton {color: #5d4e4d;}");
  vb->addWidget(chat_output);
  hl->addWidget(cb);
  hl->addItem(si);
  hl->addWidget(remove_links);
  vb->addLayout(hl);
  vb->addWidget(chat_line);
  setLayout(vb);
  chat_output->setReadOnly(true);
  chat_line->setReadOnly(false);
  chat_line->setVisible(true);
  chat_line->installEventFilter(this);
  chat_output->setVisible(true);
  chat_output->setAcceptRichText(true);
  chat_output->setOpenLinks(false);
  chat_output->setReadOnly(true);
  connect(chat_output, SIGNAL(anchorClicked(const QUrl)),
          this, SLOT(anchor_clicked(const QUrl)));
  connect(chat_line, SIGNAL(returnPressed()), this, SLOT(send()));
  connect(remove_links, SIGNAL(clicked()), this, SLOT(rm_links()));
  connect(cb, SIGNAL(stateChanged(int)), this, SLOT(state_changed(int)));
  setMouseTracking(true);
}

/***************************************************************************
  Manages "To allies" chat button state
***************************************************************************/
void chatwdg::state_changed(int state)
{
  if (state > 0) {
    ::gui_qt_allied_chat_only = true;
  } else {
    ::gui_qt_allied_chat_only = false;
  }
}

/***************************************************************************
  User clicked clear links button
***************************************************************************/
void chatwdg::rm_links()
{
  link_marks_clear_all();
}

/***************************************************************************
  User clicked some custom link
***************************************************************************/
void chatwdg::anchor_clicked(const QUrl &link)
{
  int n;
  QStringList sl;
  int id;
  enum text_link_type type;
  sl = link.toString().split(",");
  n = sl.at(0).toInt();
  id = sl.at(1).toInt();

  type = static_cast<text_link_type>(n);
  struct tile *ptile = NULL;
  switch (type) {
  case TLT_CITY: {
    struct city *pcity = game_city_by_number(id);

    if (pcity) {
      ptile = client_city_tile(pcity);
    } else {
      output_window_append(ftc_client, _("This city isn't known!"));
    }
  }
  break;
  case TLT_TILE:
    ptile = index_to_tile(id);

    if (!ptile) {
      output_window_append(ftc_client,
                           _("This tile doesn't exist in this game!"));
    }
    break;
  case TLT_UNIT: {
    struct unit *punit = game_unit_by_number(id);

    if (punit) {
      ptile = unit_tile(punit);
    } else {
      output_window_append(ftc_client, _("This unit isn't known!"));
    }
  }
  break;
  }
  if (ptile) {
    center_tile_mapcanvas(ptile);
    link_mark_restore(type, id);
  }
}


/***************************************************************************
  Adds news string to chatwdg
***************************************************************************/
void chatwdg::append(QString str)
{
  QTextCursor cursor;

  chat_output->append(str);
  chat_line->setCompleter(gui()->chat_completer);
  chat_output->verticalScrollBar()->setSliderPosition(
                              chat_output->verticalScrollBar()->maximum());
}

/***************************************************************************
  Sends string from chat input to server
***************************************************************************/
void chatwdg::send()
{
  gui()->chat_history.prepend(chat_line->text());
  if (chat_line->text() != "") {
    if (client_state() >= C_S_RUNNING && ::gui_qt_allied_chat_only
        && is_plain_public_message(chat_line->text())) {
      send_chat(QString("%1 %2")
                  .arg(CHAT_ALLIES_PREFIX)
                  .arg(chat_line->text().toUtf8().data()).toUtf8().data());
    } else {
      send_chat(chat_line->text().toUtf8().data());
    }
  }
  chat_line->clear();
}

/***************************************************************************
  Draws semi-transparent backgrounf
***************************************************************************/
void chatwdg::paint(QPainter *painter, QPaintEvent *event)
{
  painter->setBrush(QColor(0, 0, 0, 35));
  painter->drawRect(0, 0, width(), height());
}

/***************************************************************************
  Paint event for chatwdg
***************************************************************************/
void chatwdg::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  paint(&painter, event);
  painter.end();
}

/***************************************************************************
  Processess history for chat
***************************************************************************/
bool chatwdg::eventFilter(QObject *obj, QEvent *event)
{
  if (obj == chat_line) {
    if (event->type() == QEvent::KeyPress) {
      QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
      if (keyEvent->key() == Qt::Key_Up) {
        gui()->history_pos++;
        gui()->history_pos = qMin(gui()->chat_history.count(), 
                                  gui()->history_pos);
        if (gui()->history_pos < gui()->chat_history.count()) {
          chat_line->setText(gui()->chat_history.at(gui()->history_pos));
        }
        if (gui()->history_pos == gui()->chat_history.count()) {
          chat_line->setText("");
        }
        return true;
      } else if (keyEvent->key() == Qt::Key_Down) {
        gui()->history_pos--;
        gui()->history_pos = qMax(-1, gui()->history_pos);
        if (gui()->history_pos < gui()->chat_history.count() 
            && gui()->history_pos != -1) {
          chat_line->setText(gui()->chat_history.at(gui()->history_pos));
        }
        if (gui()->history_pos == -1) {
          chat_line->setText("");
        }
        return true;
      }
    }
    if (event->type() == QEvent::ShortcutOverride) {
      event->setAccepted(true);
    }
  }
  return QObject::eventFilter(obj, event);
}


/***************************************************************************
  Updates font for chatwdg
***************************************************************************/
void chatwdg::update_font()
{
  QFont *qf;
  qf = gui()->fc_fonts.get_font("gui_qt_font_chatline");
  chat_output->setFont(*qf);
}

/***************************************************************************
  Hides allies and links button for local game
***************************************************************************/
void chatwdg::update_widgets()
{
  if (is_server_running()) {
    cb->hide();
    remove_links->hide();
  } else {
    cb->show();
    remove_links->show();
  }
}

/***************************************************************************
  Makes link to tile/unit or city
***************************************************************************/
void chatwdg::make_link(struct tile *ptile)
{
  struct unit *punit;
  char buf[MAX_LEN_MSG];

  punit = find_visible_unit(ptile);
  if (tile_city(ptile)) {
    sz_strlcpy(buf, city_link(tile_city(ptile)));
  } else if (punit) {
    sz_strlcpy(buf, unit_link(punit));
  } else {
    sz_strlcpy(buf, tile_link(ptile));
  }
  chat_line->insert(QString(buf));
  chat_line->setFocus();
}


/***************************************************************************
  Applies tags to text
***************************************************************************/
QString apply_tags(QString str, const struct text_tag_list *tags,
                   bool colors_change)
{
  int start, stop, last_i;
  QString str_col;
  QString color;
  QString final_string;
  QByteArray qba;
  QColor qc;
  QMultiMap <int, QString> mm;
  if (tags == NULL) {
    return str;
  }
  qba = str.toLocal8Bit().data();
  text_tag_list_iterate(tags, ptag) {
    if ((text_tag_stop_offset(ptag) == FT_OFFSET_UNSET)) {
      stop = qba.count();
    } else {
      stop = text_tag_stop_offset(ptag);
    }

    if ((text_tag_start_offset(ptag) == FT_OFFSET_UNSET)) {
      start = 0;
    } else {
      start = text_tag_start_offset(ptag);
    }
    switch (text_tag_type(ptag)) {
    case TTT_BOLD:
      mm.insert(stop, "</b>");
      mm.insert(start, "<b>");
      break;
    case TTT_ITALIC:
      mm.insert(stop, "</i>");
      mm.insert(start, "<i>");
      break;
    case TTT_STRIKE:
      mm.insert(stop, "</s>");
      mm.insert(start, "<s>");
      break;
    case TTT_UNDERLINE:
      mm.insert(stop, "</u>");
      mm.insert(start, "<u>");
      break;
    case TTT_COLOR:
      if (text_tag_color_foreground(ptag)) {
        color = text_tag_color_foreground(ptag);
        if (colors_change) {
          if (color == "#00008B") {
            color = "#E8FF00";
          } else {
            qc.setNamedColor(color);
            qc = qc.lighter(200);
            color = qc.name();
          }
        }
          str_col = QString("<span style=color:%1>").arg(color);
          mm.insert(stop, "</span>");
          mm.insert(start, str_col);
      }
      if (text_tag_color_background(ptag)) {
        color = text_tag_color_background(ptag);
        if (QColor::isValidColor(color)) {
          str_col = QString("<span style= background-color:%1;>").arg(color);
          mm.insert(stop, "</span>");
          mm.insert(start, str_col);
        }
      }
      break;
    case TTT_LINK: {
      struct color *pcolor = NULL;

      switch (text_tag_link_type(ptag)) {
      case TLT_CITY:
        pcolor = get_color(tileset, COLOR_MAPVIEW_CITY_LINK);
        break;
      case TLT_TILE:
        pcolor = get_color(tileset, COLOR_MAPVIEW_TILE_LINK);
        break;
      case TLT_UNIT:
        pcolor = get_color(tileset, COLOR_MAPVIEW_UNIT_LINK);
        break;
      }

      if (!pcolor) {
        break; /* Not a valid link type case. */
      }
      color = pcolor->qcolor.name(QColor::HexRgb);
      str_col = QString("<font color=\"%1\">").arg(color);
      mm.insert(stop, "</a></font>");

      color = QString(str_col + "<a href=%1,%2>").
              arg(QString::number(text_tag_link_type(ptag)),
                  QString::number(text_tag_link_id(ptag)));
      mm.insert(start, color);
    }
    }
  } text_tag_list_iterate_end;

  /* insert html starting from last items */
  last_i = str.count();
  QMultiMap<int, QString>::const_iterator i = mm.constEnd();
  while (i != mm.constBegin()) {
    --i;
    if (i.key() < last_i) {
      final_string = final_string.prepend(QString(qba.mid(i.key(),
                                                          last_i - i.key()))
                                             .toHtmlEscaped());
    }
    last_i = i.key();
    final_string = final_string.prepend(i.value());
  }
  if (last_i == str.count()) {
    return str;
  }

  return final_string;
}


/**************************************************************************
  Helper function to determine if a given client input line is intended as
  a "plain" public message.
**************************************************************************/
static bool is_plain_public_message(QString s)
{
  QString s1, str;
  int i;

  str = s.trimmed();
  if (str.at(0) == SERVER_COMMAND_PREFIX
      || str.at(0) == CHAT_ALLIES_PREFIX
      || str.at(0) == CHAT_DIRECT_PREFIX) {
    return false;
  }

  /* Search for private message */
  if (!str.contains(CHAT_DIRECT_PREFIX)) {
    return true;
  }
  i = str.indexOf(CHAT_DIRECT_PREFIX);
  str = str.left(i);

  /* Compare all players and connections looking for match */
  conn_list_iterate(game.all_connections, pconn) {
    s1 = pconn->username;
    if (s1.length() < i) {
      continue;
    }
    if (!QString::compare(s1.left(i), str, Qt::CaseInsensitive)) {
      return false;
    }
  } conn_list_iterate_end;
  players_iterate(pplayer) {
    s1 = pplayer->name;
    if (s1.length() < i) {
      continue;
    }
    if (!QString::compare(s1.left(i), str, Qt::CaseInsensitive)) {
      return false;
    }
  } players_iterate_end;

  return true;
}

/**************************************************************************
  Appends the string to the chat output window.  The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void qtg_real_output_window_append(const char *astring,
                                   const struct text_tag_list *tags,
                                   int conn_id)
{
  QString str;

  str = QString::fromUtf8(astring);
  gui()->set_status_bar(str);
  gui()->update_completer();

  gui()->append_output_window(apply_tags(str, tags, false));
  if (gui()->infotab != NULL) {
    gui()->infotab->chtwdg->append(apply_tags(str, tags, true));
  }
}

/**************************************************************************
  Get the text of the output window, and call write_chatline_content() to
  log it.
**************************************************************************/
void log_output_window(void)
{
  /* PORTME */
  write_chatline_content(NULL);
}

/**************************************************************************
  Clear all text from the output window.
**************************************************************************/
void clear_output_window(void)
{
  /* PORTME */
#if 0
  set_output_window_text(_("Cleared output window."));
#endif
}

/**************************************************************************
  Version message event constructor.
**************************************************************************/
version_message_event::version_message_event(const QString &message) :
  QEvent(QEvent::User),
  message(message)
{}

/**************************************************************************
  Got version message from metaserver thread.
**************************************************************************/
void qtg_version_message(char *vertext)
{
  current_app()->postEvent(gui(), new version_message_event(vertext));
}
