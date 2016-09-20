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

#ifndef FC__FONTS_H
#define FC__FONTS_H

// gui-qt
#include "listener.h"

// client
#include "canvas_g.h" // client_font

// Qt
#include <QFont>
#include <QMap>
#include <QObject>

class QWidget;

/***************************************************************************
  The list of font names supported by the font classes. Keep it in sync with
  the options in client/options.c
***************************************************************************/
namespace fonts
{
  const char * const city_label       = "city_label";
  const char * const default_font     = "default";
  const char * const notify_label     = "notify_label";
  const char * const spaceship_label  = "spaceship_label";
  const char * const help_label       = "help_label";
  const char * const help_link        = "help_link";
  const char * const help_text        = "help_text";
  const char * const help_title       = "help_title";
  const char * const chatline         = "chatline";
  const char * const beta_label       = "beta_label";
  const char * const small            = "small";
  const char * const comment_label    = "comment_label";
  const char * const city_names       = "city_names";
  const char * const city_productions = "city_productions";
  const char * const reqtree_text     = "reqtree_text";
}

/***************************************************************************
  Handles font configuration. Font names are those defined in the fonts
  namespace.
***************************************************************************/
class font_options_listener : public listener<font_options_listener>
{
  static QMap<QString, QFont> font_map;
  static void init_font_map();

public:
  static void set_font(const char *name, const char *font_info);
  static QFont get_font(client_font font);

  explicit font_options_listener();

  virtual void update_font(const QString &name, const QFont &font);

  QFont get_font(const QString &name) const;
};

/***************************************************************************
  Automatically updates the font of a widget whenever it changes. All you
  need to do is creating a font_updater:

  ~~~~~{.cpp}
  new font_updater(the_widget, the_font_name);
  ~~~~~

  You don't need to care about deleting this object.
***************************************************************************/
class font_updater : public QObject, private font_options_listener
{
  Q_OBJECT

  QString font_name;
  QWidget *widget;

public:
  explicit font_updater(QWidget *widget, const QString &font_name);

  virtual void update_font(const QString &name, const QFont &font);
};

#endif // FC__FONTS_H
