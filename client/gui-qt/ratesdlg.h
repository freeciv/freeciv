/********************************************************************** 
 Freeciv - Copyright (C) 2003 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__RATESDLG_H
#define FC__RATESDLG_H

// In this case we have to include fc_config.h from header file.
// Some other headers we include demand that fc_config.h must be
// included also. Usually all source files include fc_config.h, but
// there's moc generated meta_ratesdlg.cpp file which does not.
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QLabel>
#include <QCheckBox>
#include <QSlider>
#include <QDialog>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QString>
#include <QGroupBox>

// common
#include "government.h"

// client
#include "client_main.h"

extern "C" {
#include "ratesdlg_g.h"
}

/**************************************************************************
 * Dialog used to change tax rates
 *************************************************************************/
class tax_rates_dialog: public  QDialog
{
Q_OBJECT

  public:
  tax_rates_dialog(QWidget *parent = 0);
  int tax;
  int sci;
  int lux;

private:
  QLabel *some_label;
  QLabel *tax_label;
  QLabel *sci_label;
  QLabel *lux_label;
  QCheckBox *tax_checkbox;
  QCheckBox *sci_checkbox;
  QCheckBox *lux_checkbox;
  QSlider *tax_slider;
  QSlider *sci_slider;
  QSlider *lux_slider;
  QDialogButtonBox *button_box;
  QPushButton *cancel_button;
  QPushButton *ok_button;
  void check(QString);
private slots:
  void slot_set_value(int i);
  void slot_ok_button_pressed();
  void slot_cancel_button_pressed();
};
 
#endif /* FC__RATESDLG_H */
