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

#ifndef FC__HELPDLG_H
#define FC__HELPDLG_H

// Qt
#include <QDialog>
#include <QHash>
#include <QList>

extern "C" {
#include "helpdlg_g.h"
}

// Forward declarations
struct help_item;

class QFrame;
class QLabel;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

class help_widget;

class help_dialog : public QDialog
{
  Q_OBJECT
  QTreeWidget *tree_wdg;
  help_widget *help_wdg;
  QHash<QTreeWidgetItem *, const help_item *> topics_map;

  void make_tree();
public:
  help_dialog(QWidget *parent = 0);
  void update_fonts();
public slots:
  void set_topic(const help_item *item);
private slots:
  void item_changed(QTreeWidgetItem *item);
};

class help_widget : public QWidget
{
  Q_OBJECT
  QTextBrowser *text_browser;
  QFrame *box_wdg;
  QLabel *title_label;
  QWidget* main_widget;
  QList<QLabel *> label_list;

  void setup_ui();
  void set_main_widget(QWidget *main_widget);
  QWidget *create_progress_widget(const QString& label,
                                  int progress,
                                  int min, int max,
                                  const QString& value = QString()
                                 );

  void set_topic_other(const help_item *item, const char *title);

  void set_topic_any(const help_item *item, const char *title);
  void set_topic_text(const help_item *item, const char *title);
  void set_topic_unit(const help_item *item, const char *title);
  void set_topic_building(const help_item *item, const char *title);
  void set_topic_tech(const help_item *item, const char *title);
  void set_topic_terrain(const help_item *item, const char *title);
  void set_topic_extra(const help_item *item, const char *title);
  void set_topic_specialist(const help_item *item, const char *title);
  void set_topic_government(const help_item *item, const char *title);
  void set_topic_nation(const help_item *item, const char *title);
  void set_topic_road(const help_item *item, const char *title);
  void set_topic_base(const help_item *item, const char *title);

public:
  help_widget(QWidget *parent = 0);
  help_widget(const help_item *item, QWidget *parent = 0);
  void update_fonts();

public slots:
  void set_topic(const help_item *item);

public:
  struct unit_type *uclass_max_values(struct unit_class *uclass);
};

void update_help_fonts();
#endif /* FC__HELPDLG_H */
