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

#ifndef FC__TAB_ENABLERS_H
#define FC__TAB_ENABLERS_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QWidget>

/* ruledit */
#include "req_vec_fix.h"

class QPushButton;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QRadioButton;
class QToolButton;

class ruledit_gui;

class tab_enabler : public QWidget
{
  Q_OBJECT

  public:
    explicit tab_enabler(ruledit_gui *ui_in);
    void refresh();

  private:
    ruledit_gui *ui;
    void update_enabler_info(struct action_enabler *enabler);
    bool initialize_new_enabler(struct action_enabler *enabler);

    QToolButton *type_button;
    QMenu *type_menu;
    QPushButton *act_reqs_button;
    QPushButton *tgt_reqs_button;
    QPushButton *delete_button;
    QPushButton *repair_button;
    QListWidget *enabler_list;

    struct action_enabler *selected;

  private slots:
    void select_enabler();
    void add_now();
    void repair_now();
    void delete_now();
    void edit_type(QAction *action);
    void edit_target_reqs();
    void edit_actor_reqs();
};

class fix_enabler_item : public req_vec_fix_item
{
public:
  explicit fix_enabler_item(struct action_enabler *enabler);
  virtual ~fix_enabler_item();
  void close();

  const void *item();
  const char *name();
  struct req_vec_problem *find_next_problem(void);
  void apply_accepted_changes();
  void undo_accepted_changes();
  int num_vectors();
  const struct requirement_vector *vector_by_number(int number);
  const char *vector_name(const struct requirement_vector *vec);
  struct requirement_vector *
  vector_writable(const struct requirement_vector *vec);

private:
  struct action_enabler *current_enabler;
  struct action_enabler *local_copy;
  QString my_name;
};

#endif // FC__TAB_ENABLERS_H
