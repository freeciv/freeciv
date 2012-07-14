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

// gui-qt
#include "fc_client.h"
#include "qtg_cxxside.h"

#include "ratesdlg.h"

/**************************************************************************
  Dialog constructor for changing rates with sliders.
  Automatic destructor will clean qobjects, so there is no one
**************************************************************************/
tax_rates_dialog::tax_rates_dialog(QWidget *parent)
  : QDialog (parent)
{
  QString str;
  int max;

  some_label = new QLabel;
  tax_label = new QLabel;
  sci_label = new QLabel;
  lux_label = new QLabel;
  tax_checkbox = new QCheckBox;
  sci_checkbox = new QCheckBox;
  lux_checkbox = new QCheckBox;
  tax_slider = new QSlider;
  sci_slider = new QSlider;
  lux_slider = new QSlider;
  button_box = new QDialogButtonBox;
  cancel_button = new QPushButton;
  ok_button = new QPushButton;

  tax = client.conn.playing->economic.tax / 10;
  lux = client.conn.playing->economic.luxury / 10;
  sci = client.conn.playing->economic.science / 10;

  if (NULL != client.conn.playing) {
    max = get_player_bonus(client.conn.playing, EFT_MAX_RATES);
  } else {
    max = 100;
  }

  setWindowTitle(_("Select tax, luxury and science rates"));
  QVBoxLayout *main_layout = new QVBoxLayout;

  // FIXME: Translations support.
  str = QString::fromUtf8
        (government_name_for_player(client.conn.playing)) + " "
        + "max rate: " + QString::number(max) + "%";

  some_label->setText(str);
  main_layout->addWidget(some_label);
  main_layout->addSpacing(20);

  // tax
  QGroupBox* group_box = new QGroupBox("Tax");
  QHBoxLayout *group_box_layout = new QHBoxLayout;
  QHBoxLayout *some_layout = new QHBoxLayout;
  tax_slider->setMaximum(max / 10);
  tax_slider->setMinimum(0);
  tax_slider->setValue(tax);
  tax_slider->setOrientation(Qt::Horizontal);
  connect(tax_slider, SIGNAL(valueChanged(int)),
          SLOT(slot_set_value(int)));
  group_box_layout->addWidget(tax_slider);
  str = QString::number(tax_slider->value() * 10) + "%"; // FIXME: Translations
  tax_label->setText(str);
  group_box_layout->addWidget(tax_label);
  group_box_layout->addSpacing(20);
  str = _("Lock");
  some_label = new QLabel;
  some_label->setText(str);
  group_box_layout->addWidget(some_label);
  group_box_layout->addWidget(tax_checkbox);
  group_box->setLayout(group_box_layout);
  main_layout->addWidget(group_box);

  // sci
  group_box = new QGroupBox("Science");
  group_box_layout = new QHBoxLayout;
  some_layout = new QHBoxLayout;
  sci_slider->setMaximum(max / 10);
  sci_slider->setMinimum(0);
  sci_slider->setValue(sci);
  sci_slider->setOrientation(Qt::Horizontal);
  connect(sci_slider, SIGNAL(valueChanged (int)),
          SLOT(slot_set_value (int)));
  group_box_layout->addWidget(sci_slider);
  str = QString::number(sci_slider->value() * 10) + "%";
  sci_label->setText(str);
  group_box_layout->addWidget(sci_label);
  group_box_layout->addSpacing(20);
  str = _("Lock");
  some_label = new QLabel;
  some_label->setText(str);
  group_box_layout->addWidget(some_label);
  group_box_layout->addWidget(sci_checkbox);
  group_box->setLayout(group_box_layout);
  main_layout->addWidget(group_box);

  // lux
  group_box = new QGroupBox("Luxury");
  group_box_layout = new QHBoxLayout;
  some_layout = new QHBoxLayout;
  lux_slider->setMaximum(max / 10);
  lux_slider->setMinimum(0);
  lux_slider->setValue(lux);
  lux_slider->setOrientation(Qt::Horizontal);
  connect(lux_slider, SIGNAL(valueChanged(int)),
          SLOT(slot_set_value(int)));
  group_box_layout->addWidget(lux_slider);
  str = QString::number(lux_slider->value() * 10) + "%";
  lux_label->setText(str);
  group_box_layout->addWidget(lux_label);
  group_box_layout->addSpacing(20);
  str = _("Lock");
  some_label = new QLabel;
  some_label->setText(str);
  group_box_layout->addWidget(some_label);
  group_box_layout->addWidget(lux_checkbox);
  group_box->setLayout(group_box_layout);
  main_layout->addWidget(group_box);

  some_layout = new QHBoxLayout;
  cancel_button->setText(_("Cancel"));
  ok_button->setText(_("Ok"));
  connect(cancel_button, SIGNAL(pressed()),
          SLOT(slot_cancel_button_pressed()));
  connect(ok_button, SIGNAL(pressed()),
          SLOT(slot_ok_button_pressed()));
  some_layout->addWidget(cancel_button);
  some_layout->addWidget(ok_button);

  main_layout->addSpacing(20);
  main_layout->addLayout(some_layout);
  setLayout(main_layout);

}

/**************************************************************************
  Slot in dialog executed by qt-signal setValue() when moving sliders.
**************************************************************************/
void tax_rates_dialog::slot_set_value(int i)
{
  QSlider* qo;
  QString str;
  qo = (QSlider*) QObject::sender();
  QString sender;

  if (qo == tax_slider) {
    tax = i;
    tax_label->setText(QString::number(10 * tax) + "%");
    sender = "TAX";
  }

  if (qo == sci_slider) {
    sci = i;
    sci_label->setText(QString::number(10 * tax) + "%");
    sender = "SCI";
  }

  if (qo == lux_slider) {
    lux = i;
    lux_label->setText(QString::number(10 * tax) + "%");
    sender = "LUX";
  }

  if (sci + tax + lux != 10) {
    check(sender);
  }

  str = QString::number(lux_slider->value() * 10) + "%";
  lux_label->setText (str);
  str = QString::number(tax_slider->value() * 10) + "%";
  tax_label->setText(str);
  str = QString::number(sci_slider->value() * 10) + "%";
  sci_label->setText(str);

}

/**************************************************************************
  Checks if rates are correct and moves other rates.
**************************************************************************/
void tax_rates_dialog::check(QString qo)
{
  int maxrate;
  bool tax_lock;
  bool sci_lock;
  bool lux_lock;

  if (NULL != client.conn.playing) {
    maxrate = get_player_bonus(client.conn.playing, EFT_MAX_RATES) / 10;
  } else {
    maxrate = 10;
  }

  tax_lock = tax_checkbox->isChecked();
  sci_lock = sci_checkbox->isChecked();
  lux_lock = lux_checkbox->isChecked();

  if (qo == "TAX") {
    if (!lux_lock) {
      lux = MIN(MAX(10 - tax - sci, 0), maxrate);
    }

    if (!sci_lock) {
      sci = MIN(MAX(10 - tax - lux, 0), maxrate);
    }
  } else if (qo == "LUX") {

    if (!tax_lock) {
      tax = MIN(MAX(10 - lux - sci, 0), maxrate);
    }

    if (!sci_lock) {
      sci = MIN (MAX (10 - lux - tax, 0), maxrate);
    }
  } else if (qo == "SCI") {
    if (!lux_lock) {
      lux = MIN(MAX(10 - tax - sci, 0), maxrate);
    }

    if (!tax_lock) {
      tax = MIN(MAX(10 - lux - sci, 0), maxrate);
    }
  }

  tax_slider->setValue(tax);
  lux_slider->setValue(lux);
  sci_slider->setValue(sci);
}

/***************************************************************************
  When cancel in qtpushbutton pressed selfdestruction :D.
***************************************************************************/
void tax_rates_dialog::slot_cancel_button_pressed()
{
  delete this;
}

/***************************************************************************
  When ok in qpushbutton pressed send info to server and selfdestroy :D.
***************************************************************************/
void tax_rates_dialog::slot_ok_button_pressed()
{
  dsend_packet_player_rates(&client.conn, 10 * tax, 10 * lux, 10 * sci);
  delete this;
}

/**************************************************************************
  Popup (or raise) the (tax/science/luxury) rates selection dialog.
**************************************************************************/
void popup_rates_dialog(void)
{
  tax_rates_dialog* trd = new tax_rates_dialog(gui()->central_wdg);
  trd->show();
}
