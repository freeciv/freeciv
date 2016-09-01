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

#include "fonts.h"

// client
#include "options.h"

// Qt
#include <QWidget>

FC_CPP_DECLARE_LISTENER(font_options_listener)

QMap<QString, QFont> font_options_listener::font_map =
    QMap<QString, QFont>();

/***************************************************************************
  Constructor
***************************************************************************/
font_options_listener::font_options_listener()
{
  if (font_map.empty()) {
    init_font_map();
  }
}

/***************************************************************************
  Initializes the font map
***************************************************************************/
void font_options_listener::init_font_map()
{
  QFont font;
  QString s;

  options_iterate(client_optset, poption) {
    if (option_type(poption) == OT_FONT) {
      s = option_font_get(poption);
      font.fromString(s);
      s = option_name(poption);
      font_map[s] = font;
    }
  } options_iterate_end;
}

/***************************************************************************
  Called whenever a font changes. Default implementation does nothing.
***************************************************************************/
void font_options_listener::update_font(const QString &name,
                                        const QFont &font)
{}

/***************************************************************************
  Returns the font with the given name.
***************************************************************************/
QFont font_options_listener::get_font(const QString &name) const
{
  return font_map[name];
}

/***************************************************************************
  Returns the appropriate QFont for the given client_font
***************************************************************************/
QFont font_options_listener::get_font(client_font font)
{
  if (font_map.empty()) {
    init_font_map();
  }
  switch (font) {
  case FONT_CITY_NAME:
    return font_map[fonts::city_names];
  case FONT_CITY_PROD:
    return font_map[fonts::city_productions];
  case FONT_REQTREE_TEXT:
    return font_map[fonts::reqtree_text];
  case FONT_COUNT:
    break;
    // Will trigger a warning if not all cases are covered
  }
  // Reasonable default
  return font_map[fonts::default_font];
}

/***************************************************************************
  Sets the font with the given name. The configuration is *not* updated.
***************************************************************************/
void font_options_listener::set_font(const QString &name,
                                     const QFont &font)
{
  font_map[name] = font;
  invoke(&font_options_listener::update_font, name, font);
}

/***************************************************************************
  Constructor.
***************************************************************************/
font_updater::font_updater(QWidget *widget, const QString &font_name) :
  QObject(widget),
  font_name(font_name),
  widget(widget)
{
  widget->setFont(get_font(font_name));
  font_options_listener::listen();
}

/***************************************************************************
  Updates the widget's font.
***************************************************************************/
void font_updater::update_font(const QString &name, const QFont &font)
{
  if (name == font_name) {
    widget->setFont(font);
  }
}
