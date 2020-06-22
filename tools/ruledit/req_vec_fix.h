/**********************************************************************
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

class QButtonGroup;
class QPushButton;

class ruledit_gui;

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
    Returns the name of the specified requirement vector. Useful when
    there is more than one requirement vector.
    @param vec the requirement vector to name
    @return the requirement vector name.
  ************************************************************************/
  virtual const char *vector_name(const struct requirement_vector *vec) = 0;

  /********************************************************************//**
    Returns a writable pointer to the specified requirement vector.
    @param vec the requirement vector to write to.
    @return a writable pointer to the requirement vector.
  ************************************************************************/
  virtual struct requirement_vector *
  vector_writable(const struct requirement_vector *vec) = 0;
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

private:
  struct req_vec_problem *current_problem;
  req_vec_fix_item *item_info;
  bool did_apply_a_solution;

  ruledit_gui *ui;

  req_vec_fix_problem *current_problem_viewer;
  QWidget *current_problem_area;
  QPushButton *apply_changes, *abort, *close;

private slots:
  void apply_solution(int selected_solution);
  void accept_applied_solutions();
  void reject_applied_solutions();
};

#endif /* FC__REQ_VEC_FIX_H */
