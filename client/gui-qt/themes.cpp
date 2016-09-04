/********************************************************************** 
 Freeciv - Copyright (C) 2005 The Freeciv Team
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
#include <QStyleFactory>

/* utility */
#include "mem.h"

/* client */
#include "themes_common.h"

// gui-qt
#include "qtg_cxxside.h"

/* client/include */
#include "themes_g.h"

extern QApplication *qapp;

/*****************************************************************************
  Loads a qt theme directory/theme_name
*****************************************************************************/
void qtg_gui_load_theme(const char *directory, const char *theme_name)
{
  if (QString(theme_name) == QString("Default")) {
    return;
  }
  qapp->setStyle(QStyleFactory::create(theme_name));
}

/*****************************************************************************
  Clears a theme (sets default system theme)
*****************************************************************************/
void qtg_gui_clear_theme()
{
  qapp->setStyle(QStyleFactory::create("Fusion"));
}

/*****************************************************************************
  Each gui has its own themes directories.

  Returns an array containing these strings and sets array size in count.
  The caller is responsible for freeing the array and the paths.
*****************************************************************************/
char **qtg_get_gui_specific_themes_directories(int *count)
{
  const char **array = new const char *[1];
  *count = 1;
  array[0] = "qt";
  return const_cast<char**>(array);
}

/*****************************************************************************
  Return an array of names of usable themes in the given directory.
  Array size is stored in count.
  The caller is responsible for freeing the array and the names
*****************************************************************************/
char **qtg_get_useable_themes_in_directory(const char *directory, int *count)
{
  QStringList sl;
  char **array;
  char *data;
  QByteArray qba;;
  QString str;

  sl.append("Default");
  sl = sl + QStyleFactory::keys();
  array = new char *[sl.count()];
  *count = sl.count();

  for (int i = 0; i < *count; i++) {
    qba = sl[i].toLocal8Bit();
    data = new char[sl[i].toLocal8Bit().count() + 1];
    strcpy(data, sl[i].toLocal8Bit().data());
    array[i] = data;
  }

  return array;
}
