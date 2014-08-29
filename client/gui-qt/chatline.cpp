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

// client
#include "climisc.h"      /* for write_chatline_content */

// gui-qt
#include "fc_client.h"
#include "qtg_cxxside.h"

#include "chatline.h"

static bool gui_qt_allied_chat_only = false;
static bool is_plain_public_message(const char *s);

/***************************************************************************
  Constructor for chatwdg
***************************************************************************/
chatwdg::chatwdg(QWidget *parent)
{
  QVBoxLayout *vb;
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
  cb = new QCheckBox(_("Allies only"));
  cb->setChecked(gui_qt_allied_chat_only);
  vb = new QVBoxLayout;
  chat_line = new QLineEdit;
  chat_output = new QTextEdit;
  vb->addWidget(chat_output);
  vb->addWidget(cb);
  vb->addWidget(chat_line);
  setLayout(vb);
  chat_output->setReadOnly(true);
  chat_line->setReadOnly(false);
  chat_line->setVisible(true);
  chat_output->setVisible(true);
  connect(chat_line, SIGNAL(returnPressed()), this, SLOT(send()));
  connect(cb, SIGNAL(stateChanged(int)), this, SLOT(state_changed(int)));
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
  Adds news string to chatwdg
***************************************************************************/
void chatwdg::append(QString str)
{
  chat_output->append(str);
}

/***************************************************************************
  Sends string from chat input to server
***************************************************************************/
void chatwdg::send()
{
  const char *theinput;

  theinput = chat_line->text().toUtf8().data();
  if (*theinput) {
    if (client_state() == C_S_RUNNING && ::gui_qt_allied_chat_only
        && is_plain_public_message(theinput)) {
      char buf[MAX_LEN_MSG];
      fc_snprintf(buf, sizeof(buf), ". %s", theinput);
      send_chat(buf);
    } else {
      send_chat(theinput);
    }
  }
  chat_line->clear();
}

/***************************************************************************
  Draws semi-transparent backgrounf
***************************************************************************/
void chatwdg::paint(QPainter *painter, QPaintEvent *event)
{
  painter->setBrush(QColor(0, 0, 0, 65));
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

/**************************************************************************
  Helper function to determine if a given client input line is intended as
  a "plain" public message.
**************************************************************************/
static bool is_plain_public_message(const char *s)
{
  const char ALLIES_CHAT_PREFIX = '.';
  const char SERVER_COMMAND_PREFIX = '/';
  const char MESSAGE_PREFIX = ':';
  const char *p;

  if (s[0] == SERVER_COMMAND_PREFIX || s[0] == ALLIES_CHAT_PREFIX) {
    return FALSE;
  }

  if (s[0] == '\'' || s[0] == '"') {
    p = strchr(s + 1, s[0]);
  } else {
    p = s;
  }

  while (p != NULL && *p != '\0') {
    if (fc_isspace(*p)) {
      return TRUE;
    } else if (*p == MESSAGE_PREFIX) {
      return FALSE;
    }
    p++;
  }
  return TRUE;
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
  gui()->append_output_window(str);
  gui()->set_status_bar(str);
  if (gui()->infotab != NULL){
    gui()->infotab->chtwdg->append(str);
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
  Got version message from metaserver thread.
**************************************************************************/
void qtg_version_message(char *vertext)
{
  /* FIXME - this will crash at some point - event will come 
   * later with non existent pointer 
  output_window_append(ftc_client, vertext);
  */
}
