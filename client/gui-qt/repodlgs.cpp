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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// common
#include "research.h"

// client
#include "repodlgs_common.h"
#include "reqtree.h"
#include "text.h"

// gui-qt
#include "cityrep.h"
#include "qtg_cxxside.h"

#include "repodlgs.h"

/****************************************************************************
  Compare unit_items (used for techs) by name
****************************************************************************/
bool comp_less_than(const qlist_item &q1, const qlist_item &q2)
{
  return (q1.tech_str < q2.tech_str);
}

/****************************************************************************
  Constructor for research diagram
****************************************************************************/
research_diagram::research_diagram(QWidget *parent): QWidget(parent)
{
  req = create_reqtree(client_player(), true);
  get_reqtree_dimensions(req, &width, &height);
  pcanvas = qtg_canvas_create(width, height);
  pcanvas->map_pixmap.fill(Qt::transparent);
}

/****************************************************************************
  Destructor for research diagram
****************************************************************************/
research_diagram::~research_diagram()
{
  qtg_canvas_free(pcanvas);
  destroy_reqtree(req);
}

/****************************************************************************
  Recreates whole diagram and schedules update
****************************************************************************/
void research_diagram::update_reqtree()
{
  destroy_reqtree(req);
  req = create_reqtree(client_player(), true);
  draw_reqtree(req, pcanvas, 0, 0, 0, 0, width, height);
  update();
}

/****************************************************************************
  Mouse handler for research_diagram
****************************************************************************/
void research_diagram::mousePressEvent(QMouseEvent *event)
{
  Tech_type_id tech = get_tech_on_reqtree(req, event->x(), event->y());

  if (event->button() == Qt::LeftButton && can_client_issue_orders()) {
    switch (player_invention_state(client_player(), tech)) {
    case TECH_PREREQS_KNOWN:
      dsend_packet_player_research(&client.conn, tech);
      break;
    case TECH_UNKNOWN:
      dsend_packet_player_tech_goal(&client.conn, tech);
      break;
    case TECH_KNOWN:
      break;
    }
  }
}

/****************************************************************************
  Paint event for research_diagram
****************************************************************************/
void research_diagram::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  painter.drawPixmap(0, 0, width, height, pcanvas->map_pixmap);
  painter.end();
}

/****************************************************************************
  Returns size of research_diagram
****************************************************************************/
QSize research_diagram::size()
{
  QSize s;

  s.setWidth(width);;
  s.setHeight(height);
  return s;
}

/****************************************************************************
  Consctructor for science_report
****************************************************************************/
science_report::science_report(): QWidget()
{
  QSize size;
  QSizePolicy size_expanding_policy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  QSizePolicy size_fixed_policy(QSizePolicy::Minimum, QSizePolicy::Minimum);

  goal_combo = new QComboBox();
  info_label = new QLabel();
  progress = new QProgressBar();
  progress_label = new QLabel();
  researching_combo = new QComboBox();
  sci_layout = new QGridLayout();
  res_diag = new research_diagram();
  scroll = new QScrollArea();

  curr_list = NULL;
  goal_list = NULL;
  progress->setTextVisible(true);
  progress_label->setSizePolicy(size_fixed_policy);
  sci_layout->addWidget(progress_label, 0, 0, 1, 8);
  sci_layout->addWidget(researching_combo, 1, 0, 1, 4);
  researching_combo->setSizePolicy(size_fixed_policy);
  sci_layout->addWidget(progress, 1, 5, 1, 4);
  progress->setSizePolicy(size_fixed_policy);
  sci_layout->addWidget(goal_combo, 2, 0, 1, 4);
  goal_combo->setSizePolicy(size_fixed_policy);
  sci_layout->addWidget(info_label, 2, 5, 1, 4);
  info_label->setSizePolicy(size_fixed_policy);

  size = res_diag->size();
  res_diag->setMinimumSize(size);
  scroll->setBackgroundRole(QPalette::Dark);
  scroll->setWidget(res_diag);
  scroll->setSizePolicy(size_expanding_policy);
  sci_layout->addWidget(scroll, 4, 0, 1, 10);

  QObject::connect(researching_combo, SIGNAL(currentIndexChanged(int)),
                   SLOT(current_tech_changed(int)));

  QObject::connect(goal_combo, SIGNAL(currentIndexChanged(int)),
                   SLOT(goal_tech_changed(int)));

  setLayout(sci_layout);

}

/****************************************************************************
  Destructor for science report
  Removes "SCI" string marking it as closed
  And frees given index on list marking it as ready for new widget
****************************************************************************/
science_report::~science_report()
{
  gui()->remove_repo_dlg("SCI");
  gui()->remove_place(index);
}

/****************************************************************************
  Updates science_report and marks it as opened
  It has to be called soon after constructor.
  It could be in constructor but compiler will yell about not used variable
****************************************************************************/
void science_report::init()
{
  index = gui()->gimme_place();
  gui()->add_game_tab(this, _("Research"), index);
  gui()->game_tab_widget->setCurrentIndex(index);
  gui()->add_repo_dlg("SCI");
  update_report();
}

/****************************************************************************
  Schedules paint event in some qt queue
****************************************************************************/
void science_report::redraw()
{
  update();
}

/****************************************************************************
  Updates all important widgets on science_report
****************************************************************************/
void science_report::update_report()
{

  struct player_research *research = player_research_get(client_player());
  const char *text;
  int total;
  int done = research->bulbs_researched;
  QVariant qvar, qres;
  double not_used;
  QString str;
  qlist_item item;

  fc_assert_ret(NULL != research);

  if (curr_list) {
    delete curr_list;
  }

  if (goal_list) {
    delete goal_list;
  }

  if (research->researching != A_UNSET) {
    total = total_bulbs_required(client.conn.playing);
  } else  {
    total = -1;
  }

  curr_list = new QList<qlist_item>;
  goal_list = new QList<qlist_item>;
  progress_label->setText(science_dialog_text());
  progress_label->setAlignment(Qt::AlignHCenter);
  info_label->setAlignment(Qt::AlignHCenter);
  info_label->setText(get_science_goal_text(research->tech_goal));
  text = get_science_target_text(&not_used);
  str = QString::fromUtf8(text);
  progress->setFormat(str);
  progress->setMinimum(0);
  progress->setMaximum(total);

  if (done <= total) {
    progress->setValue(done);
  } else {
    done = total;
    progress->setValue(done);
  }

  /** Collect all techs which are reachable in the next step. */
  advance_index_iterate(A_FIRST, i) {
    if (TECH_PREREQS_KNOWN == research->inventions[i].state) {
      item.tech_str
      =
        QString::fromUtf8(advance_name_translation(advance_by_number(i)));
      item.id = i;
      curr_list->append(item);
    }
  }
  advance_index_iterate_end;


  /** Collect all techs which are reachable in next 10 steps. */
  advance_index_iterate(A_FIRST, i) {
    if (player_invention_reachable(client_player(), i, FALSE)
        && TECH_KNOWN != research->inventions[i].state
        && (i == research->tech_goal
            || 10 >= research->inventions[i].num_required_techs)) {
      item.tech_str
         = QString::fromUtf8(advance_name_translation(advance_by_number(i)));
      item.id = i;
      goal_list->append(item);
    }
  }
  advance_index_iterate_end;

  /** sort both lists */
   qSort(goal_list->begin(), goal_list->end(), comp_less_than);
   qSort(curr_list->begin(), curr_list->end(), comp_less_than);

  /** fill combo boxes */
  researching_combo->blockSignals(true);
  goal_combo->blockSignals(true);
  
  researching_combo->clear();
  goal_combo->clear();
  for (int i = 0; i < curr_list->count(); i++) {
    qvar = curr_list->at(i).id;
    researching_combo->insertItem(i, curr_list->at(i).tech_str, qvar);
  }

  for (int i = 0; i < goal_list->count(); i++) {
    qvar = goal_list->at(i).id;
    goal_combo->insertItem(i, goal_list->at(i).tech_str, qvar);
  }

  /** set current tech and goal */
  qres = research->researching;

  if (qres == A_UNSET) {
    researching_combo->insertItem(0, _("None"), A_UNSET);
    researching_combo->setCurrentIndex(0);
  } else {
    for (int i = 0; i < researching_combo->count(); i++) {
      qvar = researching_combo->itemData(i);

      if (qvar == qres) {
        researching_combo->setCurrentIndex(i);
      }
    }
  }

  qres = research->tech_goal;

  if (qres == A_UNSET) {
    goal_combo->insertItem(0, _("None"), A_UNSET);
    goal_combo->setCurrentIndex(0);
  } else {
    for (int i = 0; i < goal_combo->count(); i++) {
      qvar = goal_combo->itemData(i);

      if (qvar == qres) {
        goal_combo->setCurrentIndex(i);
      }
    }
  }

  researching_combo->blockSignals(false);
  goal_combo->blockSignals(false);

  update_reqtree();
}

/****************************************************************************
  Calls update for research_diagram
****************************************************************************/
void science_report::update_reqtree()
{
  res_diag->update_reqtree();
}

/****************************************************************************
  Slot used when combo box with current tech changes
****************************************************************************/
void science_report::current_tech_changed(int index)
{
  QVariant qvar;
  qvar = researching_combo->itemData(index);

  if (researching_combo->hasFocus()) {
    if (can_client_issue_orders()) {
      dsend_packet_player_research(&client.conn, qvar.toInt());
    }
  }
}

/****************************************************************************
  Slot used when combo box with goal have been changed
****************************************************************************/
void science_report::goal_tech_changed(int index)
{
  QVariant qvar;
  qvar = goal_combo->itemData(index);

  if (goal_combo->hasFocus()) {
    if (can_client_issue_orders()) {
      dsend_packet_player_tech_goal(&client.conn, qvar.toInt());
    }
  }
}

/**************************************************************************
  Update the science report.
**************************************************************************/
void real_science_report_dialog_update(void)
{
  int i;
  science_report *sci_rep;
  QWidget *w;

  if (gui()->is_repo_dlg_open("SCI")) {
    i = gui()->gimme_index_of("SCI");
    fc_assert(i != -1);
    w = gui()->game_tab_widget->widget(i);
    sci_rep = reinterpret_cast<science_report*>(w);
    sci_rep->update_report();
  }
}

/**************************************************************************
  Display the science report.  Optionally raise it.
  Typically triggered by F6.
**************************************************************************/
void science_report_dialog_popup(bool raise)
{
  if (!gui()->is_repo_dlg_open("SCI")) {
    science_report *sci_rep = new science_report;
    sci_rep->init();
  }
}

/**************************************************************************
  Update the economy report.
**************************************************************************/
void real_economy_report_dialog_update(void)
{
  /* PORTME */
}

/**************************************************************************
  Display the economy report.  Optionally raise it.
  Typically triggered by F5.
**************************************************************************/
void economy_report_dialog_popup(bool raise)
{
  /* PORTME */
}

/**************************************************************************
  Update the units report.
**************************************************************************/
void real_units_report_dialog_update(void)
{
  /* PORTME */
}

/**************************************************************************
  Display the units report.  Optionally raise it.
  Typically triggered by F2.
**************************************************************************/
void units_report_dialog_popup(bool raise)
{
  /* PORTME */
}

/****************************************************************************
  Show a dialog with player statistics at endgame.
****************************************************************************/
void endgame_report_dialog_start(const struct packet_endgame_report *packet)
{
  /* PORTME */
}

/****************************************************************************
  Received endgame report information about single player.
****************************************************************************/
void endgame_report_dialog_player(const struct packet_endgame_player *packet)
{
  /* PORTME */
}

/****************************************************************************
  Resize and redraw the requirement tree.
****************************************************************************/
void science_report_dialog_redraw(void)
{
  int i;
  science_report *sci_rep;
  QWidget *w;

  if (gui()->is_repo_dlg_open("SCI")) {
    i = gui()->gimme_index_of("SCI");
    fc_assert(i != -1);
    w = gui()->game_tab_widget->widget(i);
    sci_rep = reinterpret_cast<science_report*>(w);
    sci_rep->redraw();
  }
}
