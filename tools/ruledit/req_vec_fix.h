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

#ifndef FC__REQ_VEC_FIX_H
#define FC__REQ_VEC_FIX_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* Qt */
#include <QWidget>

/* common */
#include "requirements.h"

class QButtonGroup;
class QListWidgetItem;
class QPushButton;
class QStackedLayout;

class ruledit_gui;

/* How serious a req_vec_problem is considered */
enum req_vec_problem_seriousness {
  RVPS_NO_PROBLEM,
  RVPS_IMPROVE,
  RVPS_REPAIR,
};

void mark_item(QListWidgetItem *item,
               enum req_vec_problem_seriousness problem_level);

/**********************************************************************//**
  Ruleset entity specific methods for the ruleset item having its
  requirements fixed.
**************************************************************************/
class req_vec_fix_item
{
public:
  /********************************************************************//**
    Tell the helper that it has outlived its usefulness.
  ************************************************************************/
  virtual void close() = 0;

  /********************************************************************//**
    Returns a pointer to the ruleset item.
    @return a pointer to the ruleset item.
  ************************************************************************/
  virtual const void *item() = 0;

  /********************************************************************//**
    Returns a pointer to the working copy of the ruleset item.
    @return a pointer to the working copy of the ruleset item.
  ************************************************************************/
  virtual void *item_working_copy() = 0;

  /********************************************************************//**
    Returns a name to describe the item, hopefully good enough to
    distinguish it from other items. Must be short enough for a quick
    mention.
    @return a (not always unique) name for the ruleset item.
  ************************************************************************/
  virtual const char *name() = 0;

  /********************************************************************//**
    Returns the next detected requirement vector problem for the ruleset
    item or nullptr if no fix is found to be needed.
    @return the next requirement vector problem for the item.
  ************************************************************************/
  virtual struct req_vec_problem *find_next_problem() = 0;

  /********************************************************************//**
    Do all the changes the user has accepted to the ruleset item.
    N.B.: This could be called *before* all problems are fixed if the user
    wishes to try to fix problems by hand or to come back and fix the
    remaining problems later.
  ************************************************************************/
  virtual void apply_accepted_changes() = 0;

  /********************************************************************//**
    Undo all the changes the user has accepted to the ruleset item.
    N.B.: This could be called *after* all problems are fixed if the user
    wishes to see all problems and try to fix them by hand.
  ************************************************************************/
  virtual void undo_accepted_changes() = 0;

  /********************************************************************//**
    Returns the number of requirement vectors in this item.
    @return the number of requirement vectors the item has.
  ************************************************************************/
  virtual int num_vectors() = 0;

  /********************************************************************//**
    Returns a function pointer to a function that names this item kind's
    requirement vector number number. Useful when there is more than one
    requirement vector.
    @return the requirement vector namer for ruleset items of this kind.
  ************************************************************************/
  virtual requirement_vector_namer vector_namer() = 0;

  /********************************************************************//**
    Returns a function pointer to a function that returns a writable
    pointer to the specified requirement vector in the specified parent
    item.
    @return a writable pointer to the requirement vector getter function.
  ************************************************************************/
  virtual requirement_vector_by_number vector_getter() = 0;

  /********************************************************************//**
    Check if the specified vector belongs to this item
    @param vec the requirement vector that may belong to this item.
    @return true iff the vector belongs to this item.
  ************************************************************************/
  virtual bool vector_in_item(const struct requirement_vector *vec) = 0;

  /********************************************************************//**
    Trivial destructor
  ************************************************************************/
  virtual ~req_vec_fix_item() {};
};

/**********************************************************************//**
  Widget for choosing among the suggested solutions to a problem.
**************************************************************************/
class req_vec_fix_problem : public QWidget
{
  Q_OBJECT

public:
  explicit req_vec_fix_problem(const struct req_vec_problem *problem,
                               req_vec_fix_item *item_info);

private:
  QButtonGroup *solutions;

signals:
  void solution_accepted(int selected_solution);

private slots:
  void accept_solution();
};

/**********************************************************************//**
  Widget for solving requirement vector problems for a ruleset item.
**************************************************************************/
class req_vec_fix : public QWidget
{
  Q_OBJECT

public:
  explicit req_vec_fix(ruledit_gui *ui_in, req_vec_fix_item *item_info);
  ~req_vec_fix();

  const void *item();

  bool refresh(void);

signals:
  /********************************************************************//**
    A requirement vector may have been changed.
    @param vec the requirement vector that was changed.
  ************************************************************************/
  void req_vec_may_have_changed(const requirement_vector *vec);

private:
  struct req_vec_problem *current_problem;
  req_vec_fix_item *item_info;
  bool did_apply_a_solution;

  ruledit_gui *ui;

  req_vec_fix_problem *current_problem_viewer;
  QStackedLayout *current_problem_area;
  QPushButton *apply_changes, *abort, *close;

private slots:
  void apply_solution(int selected_solution);
  void accept_applied_solutions();
  void reject_applied_solutions();

  void incoming_req_vec_change(const requirement_vector *vec);
};

#endif /* FC__REQ_VEC_FIX_H */
