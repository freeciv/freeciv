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

// Qt
#include <QGridLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>

// utility
#include "fcintl.h"
#include "log.h"
#include "registry.h"

// common
#include "game.h"

// server
#include "rssanity.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "rulesave.h"

#include "tab_misc.h"

/**************************************************************************
  Setup tab_misc object
**************************************************************************/
tab_misc::tab_misc(ruledit_gui *ui_in) : QWidget()
{
  QGridLayout *main_layout = new QGridLayout(this);
  QLabel *save_label;
  QLabel *name_label;
  QLabel *version_label;
  QPushButton *save_button;
  QPushButton *refresh_button;
  int row = 0;
  QTableWidgetItem *item;

  ui = ui_in;

  main_layout->setSizeConstraint(QLayout::SetMaximumSize);

  name_label = new QLabel(QString::fromUtf8(R__("Ruleset name")));
  name_label->setParent(this);
  main_layout->addWidget(name_label, row, 0);
  name = new QLineEdit(this);
  main_layout->addWidget(name, row++, 1);
  version_label = new QLabel(QString::fromUtf8(R__("Ruleset version")));
  version_label->setParent(this);
  main_layout->addWidget(version_label, row, 0);
  version = new QLineEdit(this);
  main_layout->addWidget(version, row++, 1);
  save_label = new QLabel(QString::fromUtf8(R__("Save to directory")));
  save_label->setParent(this);
  main_layout->addWidget(save_label, row, 0);
  savedir = new QLineEdit(this);
  savedir->setText("ruledit-tmp");
  savedir->setFocus();
  main_layout->addWidget(savedir, row++, 1);
  save_button = new QPushButton(QString::fromUtf8(R__("Save now")), this);
  connect(save_button, SIGNAL(pressed()), this, SLOT(save_now()));
  main_layout->addWidget(save_button, row++, 1);

  stats = new QTableWidget(this);
  stats->setColumnCount(8);
  stats->setRowCount(6);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Terrains")));
  stats->setItem(0, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(0, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Resources")));
  stats->setItem(1, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(1, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Techs")));
  stats->setItem(2, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(2, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Unit Classes")));
  stats->setItem(3, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(3, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Unit Types")));
  stats->setItem(4, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(4, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Buildings")));
  stats->setItem(5, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(5, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Nations")));
  stats->setItem(0, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(0, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Styles")));
  stats->setItem(1, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(1, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Specialists")));
  stats->setItem(2, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(2, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Governments")));
  stats->setItem(3, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(3, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Disasters")));
  stats->setItem(4, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(4, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Achievements")));
  stats->setItem(5, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(5, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Extras")));
  stats->setItem(0, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(0, 7, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Bases")));
  stats->setItem(1, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(1, 7, item);
  item = new QTableWidgetItem(QString::fromUtf8(R__("Roads")));
  stats->setItem(2, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(2, 7, item);
  stats->verticalHeader()->setVisible(false);
  stats->horizontalHeader()->setVisible(false);
  stats->setEditTriggers(QAbstractItemView::NoEditTriggers);
  main_layout->addWidget(stats, row++, 0, 1, 2);
  refresh_button = new QPushButton(QString::fromUtf8(R__("Refresh Stats")), this);
  connect(refresh_button, SIGNAL(pressed()), this, SLOT(refresh_stats()));
  main_layout->addWidget(refresh_button, row++, 0, 1, 2);

  // Stats never change except with experimental features. Hide useless
  // button.
  show_experimental(refresh_button);

  refresh();

  setLayout(main_layout);
}

/**************************************************************************
  Refresh the information.
**************************************************************************/
void tab_misc::refresh()
{
  name->setText(game.control.name);
  version->setText(game.control.version);
  refresh_stats();
}

/**************************************************************************
  User entered savedir
**************************************************************************/
void tab_misc::save_now()
{
  char nameUTF8[MAX_LEN_NAME];

  ui->flush_widgets();

  strncpy(nameUTF8, name->text().toUtf8().data(), sizeof(nameUTF8));

  if (nameUTF8[0] != '\0') {
    strncpy(game.control.name, nameUTF8, sizeof(game.control.name));
  }

  strncpy(game.control.version, version->text().toUtf8().data(),
          sizeof(game.control.version));

  if (!sanity_check_ruleset_data()) {
    QMessageBox *box = new QMessageBox();

    box->setText("Current data fails sanity checks. Save anyway?");
    box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box->exec();

    if (box->result() != QMessageBox::Yes) {
      return;
    }
  }

  save_ruleset(savedir->text().toUtf8().data(), nameUTF8,
               &(ui->data));

  ui->display_msg(R__("Ruleset saved"));
}

/**************************************************************************
  Recalculate stats
**************************************************************************/
void tab_misc::refresh_stats()
{
  int row = 0;
  int count;

  stats->item(row++, 1)->setText(QString::number(game.control.terrain_count));
  stats->item(row++, 1)->setText(QString::number(game.control.resource_count));

  count = 0;
  advance_iterate(A_FIRST, padv) {
    if (padv->require[AR_ONE] != A_NEVER) {
      count++;
    }
  } advance_iterate_end;
  stats->item(row++, 1)->setText(QString::number(count));

  stats->item(row++, 1)->setText(QString::number(game.control.num_unit_classes));

  count = 0;
  unit_type_iterate(ptype) {
    if (!ptype->disabled) {
      count++;
    }
  } unit_type_iterate_end;
  stats->item(row++, 1)->setText(QString::number(count));

  count = 0;
  improvement_iterate(pimpr) {
    if (!pimpr->disabled) {
      count++;
    }
  } improvement_iterate_end;
  stats->item(row++, 1)->setText(QString::number(count));

  // Second column
  row = 0;
  stats->item(row++, 4)->setText(QString::number(game.control.nation_count));
  stats->item(row++, 4)->setText(QString::number(game.control.styles_count));
  stats->item(row++, 4)->setText(QString::number(game.control.num_specialist_types));
  stats->item(row++, 4)->setText(QString::number(game.control.government_count));
  stats->item(row++, 4)->setText(QString::number(game.control.num_disaster_types));
  stats->item(row++, 4)->setText(QString::number(game.control.num_achievement_types));

  // Third column
  row = 0;
  stats->item(row++, 7)->setText(QString::number(game.control.num_extra_types));
  stats->item(row++, 7)->setText(QString::number(game.control.num_base_types));
  stats->item(row++, 7)->setText(QString::number(game.control.num_road_types));

  stats->resizeColumnsToContents();
}
