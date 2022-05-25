/***********************************************************************
 Freeciv - Copyright (C) 2020 - The Freeciv Project contributors.
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

// Qt
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedLayout>
#include <QVBoxLayout>

// ruledit
#include "ruledit_qt.h"


#include "req_vec_fix.h"

/**********************************************************************//**
  Mark a ruleset item in a list as having a problem.
  @param item the ruleset item's representation in the list.
  @param problem_level how serious the problem, if it exists at all, is.
**************************************************************************/
void mark_item(QListWidgetItem *item,
               enum req_vec_problem_seriousness problem_level)
{
  QWidget *in = item->listWidget();

  switch (problem_level) {
  case RVPS_NO_PROBLEM:
    item->setIcon(QIcon());
    break;
  case RVPS_IMPROVE:
    item->setIcon(in->style()->standardIcon(QStyle::SP_MessageBoxWarning));
    break;
  case RVPS_REPAIR:
    item->setIcon(in->style()->standardIcon(QStyle::SP_MessageBoxCritical));
    break;
  }
}

/**********************************************************************//**
  Set up the display and solution choice of the specified problem.
  @param problem the problem to choose a solution for.
  @param item_info ruleset entity item specific helpers
**************************************************************************/
req_vec_fix_problem::req_vec_fix_problem(
    const struct req_vec_problem *problem,
    req_vec_fix_item *item_info) : QWidget()
{
  int i;
  QLabel *description;
  QPushButton *accept;

  QVBoxLayout *layout_main = new QVBoxLayout();

  // Sanity check.
  fc_assert_ret(item_info);

  description = new QLabel();
  layout_main->addWidget(description);

  if (problem == nullptr) {
    // Everything is OK.

    /* TRANS: Trying to fix a requirement vector problem but can't find
     * any. */
    description->setText(R__("No problem found"));
    this->setLayout(layout_main);
    return;
  }

  if (problem->num_suggested_solutions == 0) {
    // Didn't get any suggestions about how to solve this.

    char buf[MAX_LEN_NAME * 3];

    fc_snprintf(buf, sizeof(buf),
                /* TRANS: Trying to fix a requirement vector problem but
                 * don't know how to solve it. */
                R__("Don't know how to fix %s: %s"),
                item_info->name(), problem->description);

    description->setText(buf);
    this->setLayout(layout_main);
    return;
  }

  // A problem with at least one solution exists.
  fc_assert_ret(problem && problem->num_suggested_solutions > 0);

  // Display the problem text
  description->setText(QString::fromUtf8(problem->description_translated));

  // Display each solution
  solutions = new QButtonGroup(this);
  for (i = 0; i < problem->num_suggested_solutions; i++) {
    QRadioButton *solution = new QRadioButton(
        req_vec_change_translation(&problem->suggested_solutions[i],
        item_info->vector_namer()));

    solutions->addButton(solution, i);
    solution->setChecked(i == 0);

    layout_main->addWidget(solution);
  }

  // TRANS: Apply the selected requirement vector problem fix.
  accept = new QPushButton(R__("Accept selected solution"));
  connect(accept, SIGNAL(pressed()), this, SLOT(accept_solution()));
  layout_main->addWidget(accept);

  this->setLayout(layout_main);
}

/**********************************************************************//**
  The user selected one of the suggested solutions to the requirement
  vector problem.
**************************************************************************/
void req_vec_fix_problem::accept_solution()
{
  emit solution_accepted(solutions->checkedId());
}

/**********************************************************************//**
  Set up a widget for displaying and fixing requirement vector problems
  for a specific ruleset entity item.
  @param ui_in ruledit instance this is for.
  @param item ruleset entity item specific helpers. req_vec_fix's
              destructor calls close() on it.
**************************************************************************/
req_vec_fix::req_vec_fix(ruledit_gui *ui_in,
                         req_vec_fix_item *item) : QWidget()
{
  QVBoxLayout *layout_main = new QVBoxLayout();
  QHBoxLayout *layout_buttons = new QHBoxLayout();

  this->ui = ui_in;
  connect(ui, SIGNAL(req_vec_may_have_changed(const requirement_vector *)),
          this, SLOT(incoming_req_vec_change(const requirement_vector *)));

  this->item_info = item;

  this->current_problem = nullptr;
  this->did_apply_a_solution = false;

  this->setWindowTitle(R__("Requirement problem"));
  this->setAttribute(Qt::WA_DeleteOnClose);

  // Set up the area for viewing problems
  this->current_problem_viewer = nullptr;
  this->current_problem_area = new QStackedLayout();
  layout_main->addLayout(current_problem_area);

  /* TRANS: Button text in the requirement vector fixer dialog. Cancels all
   * changes done since the last time all accepted changes were done. */
  abort = new QPushButton(R__("Undo all"));
  /* TRANS: Tool tip text in the requirement vector fixer dialog. Cancels
   * all changes done since the last time all accepted changes were done. */
  abort->setToolTip(R__("Undo all accepted solutions since you started or"
                        " since last time you ordered all accepted changes"
                        " done."));
  connect(abort, SIGNAL(pressed()), this, SLOT(reject_applied_solutions()));
  layout_buttons->addWidget(abort);

  /* TRANS: Perform all the changes to the ruleset item the user has
   * accepted. Button text in the requirement vector fixer dialog. */
  apply_changes = new QPushButton(R__("Do accepted changes"));
  /* TRANS: Perform all the changes to the ruleset item the user has
   * accepted. Tool tip text in the requirement vector fixer dialog. */
  apply_changes->setToolTip(R__("Perform all the changes you have accepted"
                                " to the ruleset item. You can then fix the"
                                " current issue by hand and come back here"
                                " to find the next issue."));
  connect(apply_changes, SIGNAL(pressed()),
          this, SLOT(accept_applied_solutions()));
  layout_buttons->addWidget(apply_changes);

  close = new QPushButton(R__("Close"));
  connect(close, SIGNAL(pressed()), this, SLOT(close()));
  layout_buttons->addWidget(close);

  layout_main->addLayout(layout_buttons);

  this->setLayout(layout_main);
}

/**********************************************************************//**
  Destructor for req_vec_fix.
**************************************************************************/
req_vec_fix::~req_vec_fix()
{
  if (current_problem != nullptr) {
    req_vec_problem_free(current_problem);
  }

  this->item_info->close();

  this->ui->unregister_req_vec_fix(this);
}

/**********************************************************************//**
  Returns the item this dialog is trying to fix.
  @return the item this dialog is trying to fix.
**************************************************************************/
const void *req_vec_fix::item()
{
  return this->item_info->item();
}

/**********************************************************************//**
  Find the next requirement vector problem and its suggested solutions.
  @return true iff a new problem was found.
**************************************************************************/
bool req_vec_fix::refresh()
{
  if (current_problem != nullptr) {
    /* The old problem is hopefully solved. If it isn't it will probably be
     * returned again. */
    req_vec_problem_free(current_problem);
  }

  // Update the current problem.
  this->current_problem = item_info->find_next_problem();

  // Display the new problem
  if (current_problem_viewer != nullptr) {
    current_problem_viewer->hide();
    current_problem_viewer->deleteLater();
  }
  current_problem_viewer = new req_vec_fix_problem(current_problem,
                                                   item_info);
  connect(current_problem_viewer, SIGNAL(solution_accepted(int)),
          this, SLOT(apply_solution(int)));
  current_problem_area->addWidget(current_problem_viewer);
  current_problem_area->setCurrentWidget(current_problem_viewer);

  // Only shown when there is something to do
  apply_changes->setVisible(did_apply_a_solution);
  abort->setVisible(did_apply_a_solution);

  // Only shown when no work will be lost
  close->setVisible(!did_apply_a_solution);

  return current_problem != nullptr;
}

/**********************************************************************//**
  Apply the selected solution to the current requirement vector problem.
  @param selected_solution the selected solution
**************************************************************************/
void req_vec_fix::apply_solution(int selected_solution)
{
  const struct req_vec_change *solution;

  fc_assert_ret(current_problem);
  fc_assert_ret(selected_solution >= 0);
  fc_assert_ret(selected_solution
                < current_problem->num_suggested_solutions);

  solution = &current_problem->suggested_solutions[selected_solution];

  if (!req_vec_change_apply(solution, item_info->vector_getter(),
                            item_info->item_working_copy())) {
    QMessageBox *box = new QMessageBox();

    box->setWindowTitle(R__("Unable to apply solution"));

    box->setText(
          // TRANS: requirement vector fix failed to apply
          QString(R__("Failed to apply solution %1 for %2 to %3."))
                 .arg(req_vec_change_translation(solution,
                                                 item_info->vector_namer()),
                      current_problem->description, item_info->name()));
    box->setStandardButtons(QMessageBox::Ok);
    box->exec();
  } else {
    // A solution has been applied
    this->did_apply_a_solution = true;
  }

  this->refresh();
}

/**********************************************************************//**
  Do all the accepted solutions for real.
**************************************************************************/
void req_vec_fix::accept_applied_solutions()
{
  int i;

  this->item_info->apply_accepted_changes();

  // All is accepted
  this->did_apply_a_solution = false;

  // New check point.
  this->refresh();

  for (i = 0; i < item_info->num_vectors(); i++) {
    emit req_vec_may_have_changed(item_info->vector_getter()
                                  (item_info->item(), i));
  }
}

/**********************************************************************//**
  Undo all accepted solutions.
**************************************************************************/
void req_vec_fix::reject_applied_solutions()
{
  this->item_info->undo_accepted_changes();

  // All is gone
  this->did_apply_a_solution = false;

  // Back to the start again.
  this->refresh();
}

/**********************************************************************//**
  A requirement vector may have been changed.
  @param vec the requirement vector that may have been changed.
**************************************************************************/
void req_vec_fix::incoming_req_vec_change(const requirement_vector *vec)
{
  if (this->item_info->vector_in_item(vec)) {
    // Can't trust the changes done against a previous version.
    reject_applied_solutions();
  }
}
