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
#include <QDir>
#include <QStyleFactory>

/* utility */
#include "mem.h"

/* client */
#include "themes_common.h"

// gui-qt
#include "qtg_cxxside.h"

/* client/include */
#include "themes_g.h"

static QString stylestring;
static QString real_data_dir;
extern QApplication *qapp;
extern QString current_theme;
extern QApplication *current_app();
static QString def_app_style;

/*****************************************************************************
  Loads a qt theme directory/theme_name
*****************************************************************************/
void qtg_gui_load_theme(const char *directory, const char *theme_name)
{
  QString name;
  QString res_name;
  QString path;
  QDir dir;
  QFile f;
  QString lnb = "LittleFinger";

  if (def_app_style.isEmpty()) {
    def_app_style = QApplication::style()->objectName();
  }

  if (real_data_dir.isEmpty()) {
    real_data_dir = QString(directory);
  }

  path = real_data_dir
         + QDir::separator() + theme_name + QDir::separator();
  name = dir.absolutePath() + QDir::separator() + real_data_dir;
  name = path + "resource.qss";
  f.setFileName(name);

  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (QString(theme_name) != QString("NightStalker")) {
      qtg_gui_clear_theme();
    }
    return;
  }

  QTextStream in(&f);
  stylestring = in.readAll();
  stylestring.replace(lnb, real_data_dir
                      + QDir::separator() + theme_name
                      + QDir::separator());

  if (QString(theme_name) == QString("System")) {
    QApplication::setStyle(QStyleFactory::create(def_app_style));
  } else {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
  }

  current_app()->setStyleSheet(stylestring);
  current_theme = theme_name;
}

/*****************************************************************************
  Clears a theme (sets default system theme)
*****************************************************************************/
void qtg_gui_clear_theme()
{
  QString name;

  name = fileinfoname(get_data_dirs(), "themes/gui-qt/");
  qtg_gui_load_theme(name.toLocal8Bit().data(),
                     "NightStalker");
}

/*****************************************************************************
  Each gui has its own themes directories.

  Returns an array containing these strings and sets array size in count.
  The caller is responsible for freeing the array and the paths.
*****************************************************************************/
char **qtg_get_gui_specific_themes_directories(int *count)
{
  *count = 1;
  char **array;
  array = new char *[*count];
  array[0] = const_cast<char*>(fileinfoname(get_data_dirs(),""));
  return array;
}

/*****************************************************************************
  Return an array of names of usable themes in the given directory.
  Array size is stored in count.
  The caller is responsible for freeing the array and the names
*****************************************************************************/
char **qtg_get_useable_themes_in_directory(const char *directory, int *count)
{
  QStringList sl, theme_list;
  char **array;
  char *data;
  QByteArray qba;;
  QString str;
  QString name;
  QDir dir;
  QFile f;
  
  name = fileinfoname(get_data_dirs(),
                      QString("themes/gui-qt/").toLocal8Bit().data());
  
  dir.setPath(name);
  sl << dir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
  name = name;

  foreach(str, sl) {
    f.setFileName(name + str + QDir::separator() + "resource.qss");
    if (f.exists() == false) {
      continue;
    }
    theme_list << str;
  }
  
  array = new char *[theme_list.count()];
  *count = theme_list.count();

  for (int i = 0; i < *count; i++) {
    qba = theme_list[i].toLocal8Bit();
    data = new char[theme_list[i].toLocal8Bit().count() + 1];
    strcpy(data, theme_list[i].toLocal8Bit().data());
    array[i] = data;
  }
  
  return array;
}
