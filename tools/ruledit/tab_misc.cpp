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

#ifdef FREECIV_MSWINDOWS
#include <shlobj.h>
#endif

// Qt
#include <QCheckBox>
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
#include "achievements.h"
#include "counters.h"
#include "game.h"
#include "government.h"
#include "specialist.h"

// server
#include "rssanity.h"

// ruledit
#include "conversion_log.h"
#include "ruledit.h"
#include "ruledit_qt.h"
#include "rulesave.h"

#include "tab_misc.h"

/**********************************************************************//**
  Setup tab_misc object
**************************************************************************/
tab_misc::tab_misc(ruledit_gui *ui_in) : QWidget()
{
  QGridLayout *main_layout = new QGridLayout(this);
  QLabel *save_label;
  QLabel *save_ver_label;
  QLabel *label;
  QLabel *name_label;
  QLabel *version_label;
  QPushButton *button;
  int row = 0;
  QTableWidgetItem *item;
  char ttbuf[2048];

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

#ifdef FREECIV_MSWINDOWS
  PWSTR folder_path;

  if (SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT,
                           nullptr, &folder_path) == S_OK) {
    savedir->setText(QString::fromWCharArray(folder_path) + "\\ruledit-tmp");
  } else {
    savedir->setText("ruledit-tmp");
  }

#else  // FREECIV_MSWINDOWS
  savedir->setText("ruledit-tmp");
#endif // FREECIV_MSWINDOWS

  /* TRANS: %s%s%s -> path + directory separator ('/' or '\') + path
   *        Do not translate command '/rulesetdir' name. */
  fc_snprintf(ttbuf, sizeof(ttbuf),
              R__("If you want to be able to load the ruleset directly "
                  "to freeciv, place it as a subdirectory under %s%s%s\n"
                  "Use server command \"/rulesetdir <subdirectory>\" "
                  "to load it to freeciv."),
              freeciv_storage_dir(), DIR_SEPARATOR, DATASUBDIR);
  savedir->setToolTip(ttbuf);

  savedir->setFocus();
  main_layout->addWidget(savedir, row++, 1);
  save_ver_label = new QLabel(QString::fromUtf8(R__("Version suffix to directory name")));
  save_ver_label->setParent(this);
  main_layout->addWidget(save_ver_label, row, 0);
  savedir_version = new QCheckBox(this);
  main_layout->addWidget(savedir_version, row++, 1);
  button = new QPushButton(QString::fromUtf8(R__("Save now")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(save_now()));
  main_layout->addWidget(button, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Description from file")));
  label->setParent(this);
  main_layout->addWidget(label, row, 0);
  desc_via_file = new QCheckBox(this);
  connect(desc_via_file, SIGNAL(toggled(bool)), this, SLOT(desc_file_toggle(bool)));
  main_layout->addWidget(desc_via_file, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Description file")));
  label->setParent(this);
  main_layout->addWidget(label, row, 0);
  desc_file = new QLineEdit(this);
  main_layout->addWidget(desc_file, row++, 1);

  button = new QPushButton(QString::fromUtf8(R__("Sanity check rules")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(sanity_check()));
  main_layout->addWidget(button, row++, 1);

  button = new QPushButton(QString::fromUtf8(R__("Always active Effects")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(edit_aae_effects()));
  main_layout->addWidget(button, row++, 1);
  button = new QPushButton(QString::fromUtf8(R__("All Effects")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(edit_all_effects()));
  main_layout->addWidget(button, row++, 1);

  stats = new QTableWidget(this);
  stats->setColumnCount(8);
  stats->setRowCount(7);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Terrains")));
  stats->setItem(0, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(0, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Resources")));
  stats->setItem(1, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(1, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Tech Classes")));
  stats->setItem(2, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(2, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Techs")));
  stats->setItem(3, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(3, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Unit Classes")));
  stats->setItem(4, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(4, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Unit Types")));
  stats->setItem(5, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(5, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Enablers")));
  stats->setItem(6, 0, item);
  item = new QTableWidgetItem("-");
  stats->setItem(6, 1, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Buildings")));
  stats->setItem(0, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(0, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Nations")));
  stats->setItem(1, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(1, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Styles")));
  stats->setItem(2, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(2, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Specialists")));
  stats->setItem(3, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(3, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Governments")));
  stats->setItem(4, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(4, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Disasters")));
  stats->setItem(5, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(5, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Counters")));
  stats->setItem(6, 3, item);
  item = new QTableWidgetItem("-");
  stats->setItem(6, 4, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Achievements")));
  stats->setItem(0, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(0, 7, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Extras")));
  stats->setItem(1, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(1, 7, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Bases")));
  stats->setItem(2, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(2, 7, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Roads")));
  stats->setItem(3, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(3, 7, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Goods")));
  stats->setItem(4, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(4, 7, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Multipliers")));
  stats->setItem(5, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(5, 7, item);
  item = new QTableWidgetItem(QString::fromUtf8(RQ_("?stat:Effects")));
  stats->setItem(6, 6, item);
  item = new QTableWidgetItem("-");
  stats->setItem(6, 7, item);
  stats->verticalHeader()->setVisible(false);
  stats->horizontalHeader()->setVisible(false);
  stats->setEditTriggers(QAbstractItemView::NoEditTriggers);
  main_layout->addWidget(stats, row++, 0, 1, 2);
  button = new QPushButton(QString::fromUtf8(R__("Refresh Stats")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(refresh_stats()));
  main_layout->addWidget(button, row++, 0, 1, 2);

  refresh();

  setLayout(main_layout);
}

/**********************************************************************//**
  Setup the information from loaded ruleset
**************************************************************************/
void tab_misc::ruleset_loaded()
{
  if (game.server.ruledit.description_file != nullptr) {
    desc_via_file->setChecked(true);
    desc_file->setText(game.server.ruledit.description_file);
  } else {
    desc_file->setEnabled(false);
  }

  refresh();
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void tab_misc::refresh()
{
  name->setText(game.control.name);
  version->setText(game.control.version);
  refresh_stats();
}

/**********************************************************************//**
  User entered savedir
**************************************************************************/
void tab_misc::save_now()
{
  char nameUTF8[MAX_LEN_NAME];
  QString full_dir;
  QByteArray ba_bytes;

  ui->flush_widgets();

  ba_bytes = name->text().toUtf8();
  strncpy(nameUTF8, ba_bytes.data(), sizeof(nameUTF8) - 1);

  if (nameUTF8[0] != '\0') {
    strncpy(game.control.name, nameUTF8, sizeof(game.control.name));
  }

  ba_bytes = version->text().toUtf8();
  strncpy(game.control.version, ba_bytes.data(),
          sizeof(game.control.version) - 1);

  if (!autoadjust_ruleset_data() || !sanity_check_ruleset_data(nullptr)) {
    QMessageBox *box = new QMessageBox();

    box->setText("Current data fails sanity checks. Save anyway?");
    box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box->exec();

    if (box->result() != QMessageBox::Yes) {
      return;
    }
  }

  if (savedir_version->isChecked()) {
    full_dir = savedir->text() + "-" + version->text();
  } else {
    full_dir = savedir->text();
  }

  ba_bytes = full_dir.toUtf8();
  save_ruleset(ba_bytes.data(), nameUTF8,
               &(ui->data));

  ui->display_msg(R__("Ruleset saved"));
}

/**********************************************************************//**
  Callback to count number of effects

  @param peff   effect to look at - ignored by this callback
  @param data   pointer to counter integer
  @return that iteration should continue until all effects calculated
**************************************************************************/
static bool effect_counter(struct effect *peff, void *data)
{
  (*(int *)data)++;

  return TRUE;
}

/**********************************************************************//**
  Recalculate stats
**************************************************************************/
void tab_misc::refresh_stats()
{
  int row = 0;
  int count;
  int base_count;
  int road_count;

  count = 0;
  terrain_re_active_iterate(pterr) {
    count++;
  } terrain_re_active_iterate_end;
  stats->item(row++, 1)->setText(QString::number(count));

  stats->item(row++, 1)->setText(QString::number(game.control.num_resource_types));

  count = 0;
  tech_class_re_active_iterate(ptclass) {
    count++;
  } tech_class_re_active_iterate_end;
  stats->item(row++, 1)->setText(QString::number(count));

  count = 0;
  advance_re_active_iterate(padv) {
    count++;
  } advance_re_active_iterate_end;
  stats->item(row++, 1)->setText(QString::number(count));

  count = 0;
  unit_class_re_active_iterate(pclass) {
    count++;
  } unit_class_re_active_iterate_end;
  stats->item(row++, 1)->setText(QString::number(count));

  count = 0;
  unit_type_re_active_iterate(ptype) {
    count++;
  } unit_type_re_active_iterate_end;
  stats->item(row++, 1)->setText(QString::number(count));

  count = 0;
  action_iterate(act) {
    action_enabler_list_re_iterate(action_enablers_for_action(act), enabler) {
      count++;
    } action_enabler_list_re_iterate_end;
  } action_iterate_end;
  stats->item(row++, 1)->setText(QString::number(count));

  // Second column
  row = 0;
  count = 0;
  improvement_re_active_iterate(pimpr) {
    count++;
  } improvement_re_active_iterate_end;
  stats->item(row++, 4)->setText(QString::number(count));

  stats->item(row++, 4)->setText(QString::number(game.control.nation_count));

  count = 0;
  styles_re_active_iterate(pstyle) {
    count++;
  } styles_re_active_iterate_end;
  stats->item(row++, 4)->setText(QString::number(count));

  count = 0;
  specialist_type_re_active_iterate(pspe) {
    count++;
  } specialist_type_re_active_iterate_end;
  stats->item(row++, 4)->setText(QString::number(count));

  count = 0;
  governments_re_active_iterate(pgov) {
    count++;
  } governments_re_active_iterate_end;
  stats->item(row++, 4)->setText(QString::number(count));

  stats->item(row++, 4)->setText(QString::number(game.control.num_disaster_types));
  count = 0;
  counters_re_iterate(pcount) {
    count++;
  } counters_re_iterate_end;
  stats->item(row++, 4)->setText(QString::number(count));

  // Third column
  row = 0;

  count = 0;
  achievements_re_active_iterate(pach) {
    count++;
  } achievements_re_active_iterate_end;
  stats->item(row++, 7)->setText(QString::number(count));

  count = 0;
  base_count = 0;
  road_count = 0;
  extra_type_re_active_iterate(pextra) {
    count++;
    if (is_extra_caused_by(pextra, EC_BASE)) {
      base_count++;
    }
    if (is_extra_caused_by(pextra, EC_ROAD)) {
      road_count++;
    }
  } extra_type_re_active_iterate_end;
  stats->item(row++, 7)->setText(QString::number(count));
  stats->item(row++, 7)->setText(QString::number(base_count));
  stats->item(row++, 7)->setText(QString::number(road_count));

  count = 0;
  goods_type_re_active_iterate(pg) {
    count++;
  } goods_type_re_active_iterate_end;
  stats->item(row++, 7)->setText(QString::number(count));

  count = 0;
  multipliers_re_active_iterate(pmul) {
    count++;
  } multipliers_re_active_iterate_end;
  stats->item(row++, 7)->setText(QString::number(count));

  count = 0;
  iterate_effect_cache(effect_counter, &count);
  stats->item(row++, 7)->setText(QString::number(count));

  stats->resizeColumnsToContents();
}

/**********************************************************************//**
  User wants to edit always active effects
**************************************************************************/
void tab_misc::edit_aae_effects()
{
  ui->open_effect_edit(QString::fromUtf8(R__("Always active")),
                       nullptr, EFMC_NONE);
}

static conversion_log *sanitylog;

/**********************************************************************//**
  Ruleset conversion log callback
**************************************************************************/
static void sanity_log_cb(const char *msg)
{
  sanitylog->add(msg);
}

/**********************************************************************//**
  Run sanity check for current rules
**************************************************************************/
void tab_misc::sanity_check()
{
  struct rscompat_info compat_info;

  sanitylog = new conversion_log(QString::fromUtf8(R__("Sanity Check")));

  rscompat_init_info(&compat_info);
  compat_info.compat_mode = FALSE;
  compat_info.log_cb = sanity_log_cb;

  if (!sanity_check_ruleset_data(&compat_info)) {
    ui->display_msg(R__("Sanity check failed!"));
  } else {
    ui->display_msg(R__("Sanity check success"));
  }
}

/**********************************************************************//**
  User wants to edit effects from full list
**************************************************************************/
void tab_misc::edit_all_effects()
{
  ui->open_effect_edit(QString::fromUtf8(R__("All effects")),
                       nullptr, EFMC_ALL);
}

/**********************************************************************//**
  Flush information from widgets to stores where it can be saved from.
**************************************************************************/
void tab_misc::flush_widgets()
{
  if (desc_via_file->isChecked()) {
    QByteArray df_bytes;

    df_bytes = desc_file->text().toUtf8();
    game.server.ruledit.description_file = fc_strdup(df_bytes.data());
  } else {
    if (game.server.ruledit.description_file != nullptr) {
      free(game.server.ruledit.description_file);
    }
    game.server.ruledit.description_file = nullptr;
  }
}

/**********************************************************************//**
  Toggled description from file setting
**************************************************************************/
void tab_misc::desc_file_toggle(bool checked)
{
  desc_file->setEnabled(checked);
}
