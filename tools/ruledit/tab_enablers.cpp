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
#include <QGridLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>

// utility
#include "fcintl.h"
#include "log.h"

// common
#include "game.h"
#include "government.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "validity.h"

#include "tab_enablers.h"

class fix_enabler_item : public req_vec_fix_item
{
public:
  explicit fix_enabler_item(struct action_enabler *enabler);
  virtual ~fix_enabler_item();
  void close();

  const void *item();
  void *item_working_copy();
  const char *name();
  struct req_vec_problem *find_next_problem(void);
  void apply_accepted_changes();
  void undo_accepted_changes();
  int num_vectors();
  requirement_vector_namer vector_namer();
  requirement_vector_by_number vector_getter();
  bool vector_in_item(const struct requirement_vector *vec);

private:
  struct action_enabler *current_enabler;
  struct action_enabler *local_copy;
  QString my_name;
};

/**********************************************************************//**
  Returns how big a problem an action enabler has.
  @param enabler the enabler to check the problem size for
  @return how serious a problem the enabler has
**************************************************************************/
static enum req_vec_problem_seriousness
enabler_problem_level(struct action_enabler *enabler)
{
  struct req_vec_problem *problem = action_enabler_suggest_repair(enabler);

  if (problem != nullptr) {
    req_vec_problem_free(problem);
    return RVPS_REPAIR;
  }

  problem = action_enabler_suggest_improvement(enabler);
  if (problem != nullptr) {
    req_vec_problem_free(problem);
    return RVPS_IMPROVE;
  }

  return RVPS_NO_PROBLEM;
}

/**********************************************************************//**
  Setup tab_enabler object
**************************************************************************/
tab_enabler::tab_enabler(ruledit_gui *ui_in) : QWidget()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *enabler_layout = new QGridLayout();
  QLabel *label;
  QPushButton *add_button;
  QLabel *lbl;
  int row = 0;

  ui = ui_in;
  connect(ui, SIGNAL(req_vec_may_have_changed(const requirement_vector *)),
          this, SLOT(incoming_req_vec_change(const requirement_vector *)));

  selected = 0;

  enabler_list = new QListWidget(this);

  connect(enabler_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_enabler()));
  main_layout->addWidget(enabler_list);

  enabler_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(QString::fromUtf8(R__("Type")));
  label->setParent(this);
  enabler_layout->addWidget(label, row, 0);

  type_button = new QToolButton();
  type_menu = new QMenu();

  action_iterate(act) {
    type_menu->addAction(action_id_rule_name(act));
  } action_iterate_end;

  connect(type_menu, SIGNAL(triggered(QAction *)),
          this, SLOT(edit_type(QAction *)));

  type_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  type_button->setPopupMode(QToolButton::MenuButtonPopup);

  type_button->setMenu(type_menu);
  type_button->setText(R__("None"));

  type_button->setEnabled(false);
  enabler_layout->addWidget(type_button, row++, 1);

  act_reqs_button
      = new QPushButton(QString::fromUtf8(R__("Actor Requirements")), this);
  connect(act_reqs_button, SIGNAL(pressed()),
          this, SLOT(edit_actor_reqs()));
  act_reqs_button->setEnabled(false);
  enabler_layout->addWidget(act_reqs_button, row++, 1);

  tgt_reqs_button
      = new QPushButton(QString::fromUtf8(R__("Target Requirements")),
                        this);
  connect(tgt_reqs_button, SIGNAL(pressed()),
          this, SLOT(edit_target_reqs()));
  tgt_reqs_button->setEnabled(false);
  enabler_layout->addWidget(tgt_reqs_button, row++, 1);

  lbl = new QLabel(QString::fromUtf8(R__("Comment")));
  enabler_layout->addWidget(lbl, row, 0);
  comment = new QLineEdit(this);
  connect(comment, SIGNAL(returnPressed()), this, SLOT(comment_given()));
  enabler_layout->addWidget(comment, row++, 1);

  add_button = new QPushButton(QString::fromUtf8(R__("Add Enabler")), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now()));
  enabler_layout->addWidget(add_button, row, 0);
  show_experimental(add_button);

  delete_button = new QPushButton(QString::fromUtf8(R__("Remove this Enabler")), this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));
  delete_button->setEnabled(false);
  enabler_layout->addWidget(delete_button, row++, 1);
  show_experimental(delete_button);

  repair_button = new QPushButton(this);
  connect(repair_button, SIGNAL(pressed()), this, SLOT(repair_now()));
  repair_button->setEnabled(false);
  repair_button->setText(QString::fromUtf8(R__("Enabler Issues")));
  enabler_layout->addWidget(repair_button, row++, 1);

  refresh();

  main_layout->addLayout(enabler_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void tab_enabler::refresh()
{
  int n = 0;

  enabler_list->clear();

  action_enablers_iterate(enabler) {
    if (!enabler->rulesave.ruledit_disabled) {
      char buffer[512];
      QListWidgetItem *item;

      fc_snprintf(buffer, sizeof(buffer), "#%d: %s", n,
                  action_rule_name(enabler_get_action(enabler)));

      item = new QListWidgetItem(QString::fromUtf8(buffer));

      enabler_list->insertItem(n++, item);

      mark_item(item, enabler_problem_level(enabler));

      item->setSelected(enabler == selected);
    }
  } action_enablers_iterate_end;
}

/**********************************************************************//**
  Update info of the enabler
**************************************************************************/
void tab_enabler::update_enabler_info(struct action_enabler *enabler)
{
  int i = 0;

  selected = enabler;

  if (selected != nullptr) {
    QString dispn = QString::fromUtf8(action_rule_name(enabler_get_action(enabler)));

    type_button->setText(dispn);

    type_button->setEnabled(true);
    act_reqs_button->setEnabled(true);
    tgt_reqs_button->setEnabled(true);

    if (selected->rulesave.comment != nullptr) {
      comment->setText(selected->rulesave.comment);
    } else {
      comment->setText("");
    }

    delete_button->setEnabled(true);

    switch (enabler_problem_level(selected)) {
    case RVPS_REPAIR:
      // Offer to repair the enabler if it has a problem.
      // TRANS: Fix an error in an action enabler.
      repair_button->setText(QString::fromUtf8(R__("Repair Enabler")));
      repair_button->setEnabled(TRUE);
      break;
    case RVPS_IMPROVE:
      // TRANS: Fix a non error issue in an action enabler.
      repair_button->setText(QString::fromUtf8(R__("Improve Enabler")));
      repair_button->setEnabled(true);
      break;
    case RVPS_NO_PROBLEM:
      repair_button->setText(QString::fromUtf8(R__("Enabler Issues")));
      repair_button->setEnabled(false);
      break;
    }
  } else {
    type_button->setText(R__("None"));
    type_button->setEnabled(false);

    act_reqs_button->setEnabled(false);
    tgt_reqs_button->setEnabled(false);

    repair_button->setEnabled(false);
    repair_button->setText(QString::fromUtf8(R__("Enabler Issues")));

    comment->setText("");

    delete_button->setEnabled(false);
  }

  // The enabler may have gotten (rid of) a problem.
  action_enablers_iterate(other_enabler) {
    QListWidgetItem *item = enabler_list->item(i++);

    if (item == nullptr) {
      continue;
    }

    mark_item(item, enabler_problem_level(other_enabler));
  } action_enablers_iterate_end;
}

/**********************************************************************//**
  User selected enabler from the list.
**************************************************************************/
void tab_enabler::select_enabler()
{
  int i = 0;

  // Save previously selected one's comment.
  comment_given();

  action_enablers_iterate(enabler) {
    QListWidgetItem *item = enabler_list->item(i++);

    if (item != nullptr && item->isSelected()) {
      update_enabler_info(enabler);
    }
  } action_enablers_iterate_end;
}

/**********************************************************************//**
  User requested enabler deletion
**************************************************************************/
void tab_enabler::delete_now()
{
  if (selected != nullptr) {
    selected->rulesave.ruledit_disabled = true;

    refresh();
    update_enabler_info(nullptr);
  }
}

/**********************************************************************//**
  Initialize new enabler for use.
**************************************************************************/
bool tab_enabler::initialize_new_enabler(struct action_enabler *enabler)
{
  if (enabler->rulesave.comment != nullptr) {
    free(enabler->rulesave.comment);
    enabler->rulesave.comment = nullptr;
  }

  return true;
}

/**********************************************************************//**
  User requested new enabler
**************************************************************************/
void tab_enabler::add_now()
{
  struct action_enabler *new_enabler;

  // Try to reuse freed enabler slot
  action_enablers_iterate(enabler) {
    if (enabler->rulesave.ruledit_disabled) {
      if (initialize_new_enabler(enabler)) {
        enabler->rulesave.ruledit_disabled = false;
        update_enabler_info(enabler);
        refresh();
      }
      return;
    }
  } action_enablers_iterate_end;

  // Try to add completely new enabler
  new_enabler = action_enabler_new();

  fc_assert_ret(NUM_ACTIONS > 0);
  fc_assert_ret(action_id_exists(NUM_ACTIONS - 1));
  new_enabler->action = (NUM_ACTIONS - 1);

  action_enabler_add(new_enabler);

  update_enabler_info(new_enabler);
  refresh();
}

/**********************************************************************//**
  User requested enabler repair
**************************************************************************/
void tab_enabler::repair_now()
{
  if (selected == nullptr) {
    // Nothing to repair
    return;
  }

  ui->open_req_vec_fix(new fix_enabler_item(selected));
}

/**********************************************************************//**
  A requirement vector may have been changed.
  @param vec the requirement vector that may have been changed.
**************************************************************************/
void tab_enabler::incoming_req_vec_change(const requirement_vector *vec)
{
  action_enablers_iterate(enabler) {
    if (&enabler->actor_reqs == vec || &enabler->target_reqs == vec) {
      update_enabler_info(enabler);
    }
  } action_enablers_iterate_end;
}

/**********************************************************************//**
  User selected action to enable
**************************************************************************/
void tab_enabler::edit_type(QAction *action)
{
  struct action *paction;
  QByteArray an_bytes;

  an_bytes = action->text().toUtf8();
  paction = action_by_rule_name(an_bytes.data());

  if (selected != nullptr && paction != nullptr) {
    // Must remove and add back because enablers are stored by action.
    action_enabler_remove(selected);
    selected->action = action_id(paction);
    action_enabler_add(selected);

    // Show the changes.
    update_enabler_info(selected);
    refresh();
  }
}

/**********************************************************************//**
  User wants to edit target reqs
**************************************************************************/
void tab_enabler::edit_target_reqs()
{
  if (selected != nullptr) {
    ui->open_req_edit(QString::fromUtf8(R__("Enabler (target)")),
                      &selected->target_reqs);
  }
}

/**********************************************************************//**
  User wants to edit actor reqs
**************************************************************************/
void tab_enabler::edit_actor_reqs()
{
  if (selected != nullptr) {
    ui->open_req_edit(QString::fromUtf8(R__("Enabler (actor)")),
                      &selected->actor_reqs);
  }
}

/**********************************************************************//**
  User entered comment for the enabler
**************************************************************************/
void tab_enabler::comment_given()
{
  if (selected != nullptr) {
    if (selected->rulesave.comment != nullptr) {
      free(selected->rulesave.comment);
    }

    if (!comment->text().isEmpty()) {
      selected->rulesave.comment = fc_strdup(comment->text().toUtf8());
    } else {
      selected->rulesave.comment = nullptr;
    }
  }
}

/**********************************************************************//**
  Construct fix_enabler_item to help req_vec_fix with the action enabler
  unique stuff.
**************************************************************************/
fix_enabler_item::fix_enabler_item(struct action_enabler *enabler)
{
  char buf[MAX_LEN_NAME * 2];
  struct action *paction = enabler_get_action(enabler);

  fc_assert_ret(paction != nullptr);

  /* Can't use QString::asprintf() as msys libintl.h defines asprintf()
   * as a macro */
  fc_snprintf(buf, sizeof(buf),
              R__("action enabler for %s"), action_rule_name(paction));

  // Don't modify the original until the user accepts
  local_copy = action_enabler_copy(enabler);
  current_enabler = enabler;

  // As precise a title as possible
  my_name = QString(buf);
}

/**********************************************************************//**
  Destructor for fix_enabler_item
**************************************************************************/
fix_enabler_item::~fix_enabler_item()
{
  action_enabler_free(local_copy);
}

/********************************************************************//**
  Tell the helper that it has outlived its usefulnes.
************************************************************************/
void fix_enabler_item::close()
{
  delete this;
}

/********************************************************************//**
  Returns a pointer to the ruleset item.
  @return a pointer to the ruleset item.
************************************************************************/
const void *fix_enabler_item::item()
{
  return current_enabler;
}

/********************************************************************//**
    Returns a pointer to the working copy of the ruleset item.
    @return a pointer to the working copy of the ruleset item.
************************************************************************/
void *fix_enabler_item::item_working_copy()
{
  return local_copy;
}

/**********************************************************************//**
  Returns a name to describe the item, hopefully good enough to
  distinguish it from other items. Must be short enough for a quick
  mention.
  @return a (not always unique) name for the ruleset item.
**************************************************************************/
const char *fix_enabler_item::name()
{
  return my_name.toUtf8().data();
}

/**********************************************************************//**
  Returns the next detected requirement vector problem for the ruleset
  item or nullptr if no fix is found to be needed.
  Caller needs to free the result with req_vec_problem_free()
  @return the next requirement vector problem for the item.
**************************************************************************/
struct req_vec_problem *fix_enabler_item::find_next_problem(void)
{
  struct req_vec_problem *out = action_enabler_suggest_repair(local_copy);

  if (out != nullptr) {
    return out;
  }

  return action_enabler_suggest_improvement(local_copy);
}

/**********************************************************************//**
  Do all the changes the user has accepted to the ruleset item.
  N.B.: This could be called *before* all problems are fixed if the user
  wishes to try to fix problems by hand or to come back and fix the
  remaining problems later.
**************************************************************************/
void fix_enabler_item::apply_accepted_changes()
{
  // The user has approved the solution
  current_enabler->action = local_copy->action;

  requirement_vector_copy(&current_enabler->actor_reqs,
                          &local_copy->actor_reqs);
  requirement_vector_copy(&current_enabler->target_reqs,
                          &local_copy->target_reqs);
}

/**********************************************************************//**
  Undo all the changes the user has accepted to the ruleset item.
  N.B.: This could be called *after* all problems are fixed if the user
  wishes to see all problems and try to fix them by hand.
**************************************************************************/
void fix_enabler_item::undo_accepted_changes()
{
  // The user has rejected all solutions
  local_copy->action = current_enabler->action;

  requirement_vector_copy(&local_copy->actor_reqs,
                          &current_enabler->actor_reqs);
  requirement_vector_copy(&local_copy->target_reqs,
                          &current_enabler->target_reqs);
}

/********************************************************************//**
  Returns the number of requirement vectors in this item.

  @return the number of requirement vectors the item has.
************************************************************************/
int fix_enabler_item::num_vectors()
{
  return 2;
}

/********************************************************************//**
  Returns a function pointer to a function that names this item kind's
  requirement vector number number. Useful when there is more than one
  requirement vector.

  @return the requirement vector namer for ruleset items of this kind.
************************************************************************/
requirement_vector_namer fix_enabler_item::vector_namer()
{
  return action_enabler_vector_by_number_name;
}

/********************************************************************//**
  Returns a function pointer to a function that returns a writable
  pointer to the specified requirement vector in the specified parent
  item.

  @return a writable pointer to the requirement vector getter function.
************************************************************************/
requirement_vector_by_number fix_enabler_item::vector_getter()
{
  return action_enabler_vector_by_number;
}

/**********************************************************************//**
  Check if the specified vector belongs to this item

  @param vec the requirement vector that may belong to this item.
  @return true iff the vector belongs to this item.
**************************************************************************/
bool fix_enabler_item::vector_in_item(const struct requirement_vector *vec)
{
  return (&current_enabler->actor_reqs == vec
          || &current_enabler->target_reqs == vec);
}
