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
#include <QApplication>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QWidget>
#include <QScreen>

// client
#include "options.h"

// gui-qt
#include "fonts.h"

/************************************************************************//**
  Font provider constructor
****************************************************************************/
fc_font::fc_font()
{
}

/************************************************************************//**
  Returns instance of fc_font
****************************************************************************/
fc_font *fc_font::instance()
{
  if (!m_instance)
    m_instance = new fc_font;
  return m_instance;
}

/************************************************************************//**
  Deletes fc_font instance
****************************************************************************/
void fc_font::drop()
{
  if (m_instance) {
    m_instance->release_fonts();
    delete m_instance;
    m_instance = nullptr;
  }
}

/************************************************************************//**
  Returns desired font
****************************************************************************/
QFont *fc_font::get_font(QString name)
{
  // Example: get_font("gui_qt_font_notify_label")

  if (font_map.contains(name)) {
    return font_map.value(name);
  } else {
    return nullptr;
  }
}

/************************************************************************//**
  Initializes fonts from client options
****************************************************************************/
void fc_font::init_fonts()
{
  QFont *f;
  QString s;

  /**
   * Default font names are:
   * gui_qt_font_notify_label and so on.
   * (full list is in options.c in client dir)
   */

  options_iterate(client_optset, poption) {
    if (option_type(poption) == OT_FONT) {
      f = new QFont;
      s = option_font_get(poption);
      f->fromString(s);
      s = option_name(poption);
      set_font(s, f);
    }
  } options_iterate_end;

  get_mapfont_size();
}

/************************************************************************//**
  Deletes all fonts
****************************************************************************/
void fc_font::release_fonts()
{
  foreach (QFont *f, font_map) {
    delete f;
  }
}

/************************************************************************//**
  Stores default font sizes
****************************************************************************/
void fc_font::get_mapfont_size()
{
  city_fontsize = get_font(fonts::city_names)->pointSize();
  prod_fontsize = get_font(fonts::city_productions)->pointSize();
}

/************************************************************************//**
  Adds new font or overwrite old one
****************************************************************************/
void fc_font::set_font(QString name, QFont * qf)
{
  font_map.insert(name, qf);
}

/************************************************************************//**
  Tries to choose good font for a single role.
****************************************************************************/
static void configure_single(QString role, QStringList sl, int size,
                             char *font_opt, bool bold = false)
{
  QString font_name;

  font_name = configure_font(role, sl, size, bold);
  if (font_name.isEmpty()) {
    QByteArray fn_bytes;

    fn_bytes = role.toUtf8();
    log_error(_("Failed to setup font for role %s."),
              fn_bytes.data());
  } else {
    QByteArray fn_bytes;

    fn_bytes = font_name.toUtf8();
    fc_strlcpy(font_opt, fn_bytes.data(), FONT_NAME_SIZE);
  }
}

/************************************************************************//**
  Tries to choose good fonts for freeciv-qt
****************************************************************************/
void configure_fonts()
{
  int max, smaller, default_size;
  QStringList sl;
  QString font_name;
  const QScreen *screen = QApplication::primaryScreen();
  qreal logical_dpi = screen->logicalDotsPerInchX();
  qreal physical_dpi = screen->physicalDotsPerInchX();
  qreal screen_size = screen->geometry().width() / physical_dpi + 5;
  qreal scale = (physical_dpi * screen_size / (logical_dpi * 27))
                / screen->devicePixelRatio();

  max = qRound(scale * 16);
  smaller = qRound(scale * 12);
  default_size = qRound(scale *14);

  // Default and help label
  sl << "Segoe UI" << "Cousine" << "Liberation Sans" << "Droid Sans"
     << "Ubuntu" << "Noto Sans" << "DejaVu Sans" << "Luxi Sans"
     << "Lucida Sans" << "Trebuchet MS" << "Times New Roman";
  configure_single(fonts::default_font, sl, max,
                   gui_options.gui_qt_font_default);

  configure_single(fonts::city_names, sl, smaller,
                   gui_options.gui_qt_font_city_names, true);

  // Default for help text
  configure_single(fonts::help_text, sl, default_size,
                   gui_options.gui_qt_font_help_text);

  // Notify
  sl.clear();
  sl  <<  "Cousine" << "Liberation Mono" << "Source Code Pro"
      << "Source Code Pro [ADBO]"
      << "Noto Mono" << "Ubuntu Mono" << "Courier New";

  configure_single(fonts::notify_label, sl, default_size,
                   gui_options.gui_qt_font_notify_label);

  // Standard for chat
  configure_single(fonts::chatline, sl, default_size,
                   gui_options.gui_qt_font_chatline);

  // City production
  sl.clear();
  sl  << "Arimo" << "Play" <<  "Tinos" << "Ubuntu" << "Times New Roman"
      << "Droid Sans" << "Noto Sans";

  configure_single(fonts::city_productions, sl, default_size,
                   gui_options.gui_qt_font_city_productions, true);

  // Reqtree
  sl.clear();
  sl  << "Papyrus" << "Segoe Script" << "Comic Sans MS"
      << "Droid Sans" << "Noto Sans" << "Ubuntu";

  configure_single(fonts::reqtree_text, sl, max,
                   gui_options.gui_qt_font_reqtree_text, true);
}

/************************************************************************//**
  Returns long font name, sets given font for use
****************************************************************************/
QString configure_font(QString font_name, QStringList sl, int size,
                       bool bold)
{
  QString str;
  QFont *f;
  QString style;

  if (bold) {
    style = "Bold";
  }

  foreach (str, sl)  {
    QList<int> sizes = QFontDatabase::smoothSizes(str, style);

    if (!sizes.isEmpty()) {
      QListIterator<int> i(sizes);
      int lower = -1;
      int upper = -1;
      QByteArray fn_bytes;

      while (i.hasNext()) {
        int cur = i.next();

        if (cur <= size && lower < cur) {
          lower = cur;
        }
        if (cur >= size && (upper < 0 || upper > cur)) {
          upper = cur;
        }
      }

      if (size - lower > upper - size) {
        size = upper;
      } else {
        size = lower;
      }

      f = new QFont(str, size);
      if (bold) {
        f->setBold(true);
      }
      fc_font::instance()->set_font(font_name, f);
      fn_bytes = f->toString().toUtf8();

      return fn_bytes.data();
    }
  }

  return QString();
}
