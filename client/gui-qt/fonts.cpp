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

// client
#include "options.h"

#include "fonts.h"

// Qt
#include <QWidget>

/****************************************************************************
  Font provider constructor
****************************************************************************/
fc_font::fc_font()
{
}

/****************************************************************************
  Returns instance of fc_font
****************************************************************************/
fc_font *fc_font::instance()
{
  if (!m_instance)
    m_instance = new fc_font;
  return m_instance;
}

/****************************************************************************
  Deletes fc_icons instance
****************************************************************************/
void fc_font::drop()
{
  if (m_instance) {
    m_instance->release_fonts();
    delete m_instance;
    m_instance = 0;
  }
}


/****************************************************************************
  Returns desired font
****************************************************************************/
QFont *fc_font::get_font(QString name)
{
  /**
   * example: get_font("gui_qt_font_city_label")
   */

  if (font_map.contains(name)) {
    return font_map.value(name);
  } else {
    return nullptr;
  }
}

/****************************************************************************
  Initiazlizes fonts from client options
****************************************************************************/
void fc_font::init_fonts()
{
  QFont *f;
  QString s;

  /**
   * default font names are:
   * gui_qt_font_city_label
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
}

/****************************************************************************
  Deletes all fonts
****************************************************************************/
void fc_font::release_fonts()
{
  foreach (QFont *f, font_map) {
    delete f;
  }
}


/****************************************************************************
  Adds new font or overwrite old one
****************************************************************************/
void fc_font::set_font(QString name, QFont * qf)
{
  font_map.insert(name,qf);
}

