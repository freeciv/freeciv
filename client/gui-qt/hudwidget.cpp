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
#include <QAction>
#include <QApplication>
#include <QGridLayout>
#include <QPaintEvent>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPainter>
#include <QSpacerItem>

// common
#include "movement.h"
#include "unitlist.h"
#include "tile.h"
#include "unit.h"

// client
#include "text.h"

//gui-qt
#include "hudwidget.h"
#include "fonts.h"
#include "qtg_cxxside.h"

/****************************************************************************
  Custom message box constructor
****************************************************************************/
hud_message_box::hud_message_box(QWidget *parent): QMessageBox(parent)
{
  int size;
  setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Dialog
                | Qt::FramelessWindowHint);
  f_text = *fc_font::instance()->get_font(fonts::default_font);
  f_title = *fc_font::instance()->get_font(fonts::default_font);

  size = f_text.pointSize();
  if (size > 0) {
    f_text.setPointSize(size * 4 / 3);
    f_title.setPointSize(size * 3 / 2);
  } else {
    size = f_text.pixelSize();
    f_text.setPixelSize(size * 4 / 3);
    f_title.setPointSize(size * 3 / 2);
  }
  f_title.setBold(true);
  f_title.setCapitalization(QFont::SmallCaps);
  fm_text = new QFontMetrics(f_text);
  fm_title = new QFontMetrics(f_title);
  top = 0;
  m_animate_step = 0;
  hide();
  mult = 1;
}

/****************************************************************************
  Custom message box destructor
****************************************************************************/
hud_message_box::~hud_message_box()
{
  delete fm_text;
  delete fm_title;
}
/****************************************************************************
  Key press event for hud message box
****************************************************************************/
void hud_message_box::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape) {
    close();
    destroy();
    event->accept();
  }
  QWidget::keyPressEvent(event);
}

/****************************************************************************
  Sets text and title and shows message box
****************************************************************************/
void hud_message_box::set_text_title(QString s1, QString s2)
{
  QSpacerItem *spacer;
  QGridLayout *layout;
  int w, w2, h;
  QPoint p;

  if (s1.contains('\n')) {
    int i;
    i = s1.indexOf('\n');
    cs1 = s1.left(i);
    cs2 = s1.right(s1.count() - i);
    mult = 2;
    w2 = qMax(fm_text->width(cs1), fm_text->width(cs2));
    w = qMax(w2, fm_title->width(s2));
  } else {
    w = qMax(fm_text->width(s1), fm_title->width(s2));
  }
  w = w + 20;
  h = mult * (fm_text->height() * 3 / 2) + 2 * fm_title->height();
  top = 2 * fm_title->height();
  spacer = new QSpacerItem(w, 0, QSizePolicy::Minimum,
                           QSizePolicy::Expanding);
  layout = (QGridLayout *)this->layout();
  layout->addItem(spacer, layout->rowCount(), 0, 1, layout->columnCount());
  spacer = new QSpacerItem(0, h, QSizePolicy::Expanding,
                           QSizePolicy::Minimum);
  layout->addItem(spacer, 0, 0, 1, layout->columnCount());

  text = s1;
  title = s2;

  p = QPoint((parentWidget()->width() - w) / 2,
             (parentWidget()->height() - h) / 2);
  p = parentWidget()->mapToGlobal(p);
  move(p);
  show();
  m_timer.start();
  startTimer(45);
}

/****************************************************************************
  Timer event used to animate message box
****************************************************************************/
void hud_message_box::timerEvent(QTimerEvent *event)
{
  m_animate_step = m_timer.elapsed() / 40;
  update();
}

/****************************************************************************
  Paint event for custom message box
****************************************************************************/
void hud_message_box::paintEvent(QPaintEvent *event)
{
  QPainter p;
  QRect rx, ry, rfull;
  QLinearGradient g;
  QColor c1;
  QColor c2;
  int step;

  step = m_animate_step % 300;
  if (step > 150) {
    step = step - 150;
    step = 150 - step;
  }
  step = step + 30;

  rfull = QRect(2 , 2, width() - 4 , height() - 4);
  rx = QRect(2 , 2, width() - 4 , top);
  ry = QRect(2 , top, width() - 4, height() - top - 4);

  c1 = QColor(palette().color(QPalette::Highlight));
  c2 = QColor(palette().color(QPalette::AlternateBase));
  step = qMax(0, step);
  step = qMin(255, step);
  c1.setAlpha(step);
  c2.setAlpha(step);

  g = QLinearGradient(0 , 0, width(), height());
  g.setColorAt(0, c1);
  g.setColorAt(1, c2);

  p.begin(this);
  p.fillRect(rx, QColor(palette().color(QPalette::Highlight)));
  p.fillRect(ry, QColor(palette().color(QPalette::AlternateBase)));
  p.fillRect(rfull, g);
  p.setFont(f_title);
  p.drawText((width() - fm_title->width(title)) / 2,
             fm_title->height() * 4 / 3, title);
  p.setFont(f_text);
  if (mult == 1) {
    p.drawText((width() - fm_text->width(text)) / 2,
              2 * fm_title->height() + fm_text->height() * 4 / 3, text);
  } else {
    p.drawText((width() - fm_text->width(cs1)) / 2,
              2 * fm_title->height() + fm_text->height() * 4 / 3, cs1);
    p.drawText((width() - fm_text->width(cs2)) / 2,
              2 * fm_title->height() + fm_text->height() * 8 / 3, cs2);
  }
  p.end();
  event->accept();
}

/****************************************************************************
  Custom input box constructor
****************************************************************************/
hud_input_box::hud_input_box(QWidget *parent): QDialog(parent)
{
  int size;

  setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Dialog
                | Qt::FramelessWindowHint);

  f_text = *fc_font::instance()->get_font(fonts::default_font);
  f_title = *fc_font::instance()->get_font(fonts::default_font);

  size = f_text.pointSize();
  if (size > 0) {
    f_text.setPointSize(size * 4 / 3);
    f_title.setPointSize(size * 3 / 2);
  } else {
    size = f_text.pixelSize();
    f_text.setPixelSize(size * 4 / 3);
    f_title.setPointSize(size * 3 / 2);
  }
  f_title.setBold(true);
  f_title.setCapitalization(QFont::SmallCaps);
  fm_text = new QFontMetrics(f_text);
  fm_title = new QFontMetrics(f_title);
  top = 0;
  m_animate_step = 0;
  hide();
  mult = 1;
}

/****************************************************************************
  Custom input box destructor
****************************************************************************/
hud_input_box::~hud_input_box()
{
  delete fm_text;
  delete fm_title;
}
/****************************************************************************
  Sets text, title and default text and shows input box
****************************************************************************/
void hud_input_box::set_text_title_definput(QString s1, QString s2,
                                            QString def_input)
{
  QSpacerItem *spacer;
  QVBoxLayout *layout;
  int w, w2, h;
  QDialogButtonBox *button_box;
  QPoint p;

  button_box = new QDialogButtonBox(QDialogButtonBox::Ok
                                    | QDialogButtonBox::Cancel,
                                    Qt::Horizontal, this);
  layout = new QVBoxLayout;
  if (s1.contains('\n')) {
    int i;
    i = s1.indexOf('\n');
    cs1 = s1.left(i);
    cs2 = s1.right(s1.count() - i);
    mult = 2;
    w2 = qMax(fm_text->width(cs1), fm_text->width(cs2));
    w = qMax(w2, fm_title->width(s2));
  } else {
    w = qMax(fm_text->width(s1), fm_title->width(s2));
  }
  w = w + 20;
  h = mult * (fm_text->height() * 3 / 2) + 2 * fm_title->height();
  top = 2 * fm_title->height();

  spacer = new QSpacerItem(w, h, QSizePolicy::Expanding,
                           QSizePolicy::Minimum);
  layout->addItem(spacer);
  layout->addWidget(&input_edit);
  layout->addWidget(button_box);
  input_edit.setFont(f_text);
  input_edit.setText(def_input);
  setLayout(layout);
  QObject::connect(button_box, SIGNAL(accepted()), this, SLOT(accept()));
  QObject::connect(button_box, SIGNAL(rejected()), this, SLOT(reject()));

  text = s1;
  title = s2;
  p = QPoint((parentWidget()->width() - w) / 2,
             (parentWidget()->height() - h) / 2);
  p = parentWidget()->mapToGlobal(p);
  move(p);
  input_edit.activateWindow();
  input_edit.setFocus();
  m_timer.start();
  startTimer(41);
  show();
  update();
}

/****************************************************************************
  Timer event used to animate input box
****************************************************************************/
void hud_input_box::timerEvent(QTimerEvent *event)
{
  m_animate_step = m_timer.elapsed() / 40;
  update();
}


/****************************************************************************
  Paint event for custom input box
****************************************************************************/
void hud_input_box::paintEvent(QPaintEvent *event)
{
  QPainter p;
  QRect rx, ry;
  QLinearGradient g;
  QColor c1;
  QColor c2;
  QColor c3;
  int step;
  float fstep;


  step = m_animate_step % 300;
  if (step > 150) {
    step = step - 150;
    step = 150 - step;
  }
  step = step + 10;
  rx = QRect(2 , 2, width() - 4 , top);
  ry = QRect(2 , top, width() - 4, height() - top - 4);

  c1 = QColor(palette().color(QPalette::Highlight));
  c2 = QColor(Qt::transparent);
  c3 = QColor(palette().color(QPalette::Highlight)).lighter(145);
  step = qMax(0, step);
  step = qMin(255, step);
  c1.setAlpha(step);
  c2.setAlpha(step);
  c3.setAlpha(step);

  fstep = static_cast<float>(step) / 400;
  g = QLinearGradient(0 , 0, width(), height());
  g.setColorAt(0, c2);
  g.setColorAt(fstep, c3);
  g.setColorAt(1, c2);

  p.begin(this);
  p.fillRect(rx, QColor(palette().color(QPalette::Highlight)));
  p.fillRect(ry, QColor(palette().color(QPalette::AlternateBase)));
  p.fillRect(rx, g);
  p.setFont(f_title);
  p.drawText((width() - fm_title->width(title)) / 2,
             fm_title->height() * 4 / 3, title);
  p.setFont(f_text);
  if (mult == 1) {
    p.drawText((width() - fm_text->width(text)) / 2,
              2 * fm_title->height() + fm_text->height() * 4 / 3, text);
  } else {
    p.drawText((width() - fm_text->width(cs1)) / 2,
              2 * fm_title->height() + fm_text->height() * 4 / 3, cs1);
    p.drawText((width() - fm_text->width(cs2)) / 2,
              2 * fm_title->height() + fm_text->height() * 8 / 3, cs2);
  }
  p.end();
  event->accept();
}

/****************************************************************************
  Constructor for hud_units (holds layout for whole uunits info)
****************************************************************************/
hud_units::hud_units(QWidget *parent) : QFrame(parent)
{
  QVBoxLayout *vbox;
  setParent(parent);
  main_layout = new QHBoxLayout;
  vbox = new QVBoxLayout;
  vbox->setSpacing(0);
  main_layout->addWidget(&unit_label);
  main_layout->addWidget(&tile_label);
  unit_icons = new unit_actions(this, nullptr);
  vbox->addWidget(&text_label);
  vbox->addWidget(unit_icons);
  main_layout->addLayout(vbox);
  main_layout->setSpacing(0);
  main_layout->setSpacing(3);
  main_layout->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(3);
  vbox->setContentsMargins(0, 0, 0, 0);
  setLayout(main_layout);
  mw = new move_widget(this);
  setFocusPolicy(Qt::ClickFocus);

}

/****************************************************************************
  Hud_units destructor
****************************************************************************/
hud_units::~hud_units()
{
}


/****************************************************************************
  Move Event for hud_units, used to save position
****************************************************************************/
void hud_units::moveEvent(QMoveEvent *event)
{
  if (event->pos().x() != 0) {
    gui()->qt_settings.unit_info_pos_x = 1 + (event->pos().x() * 1000)
                                          / gui()->mapview_wdg->width();
  } else {
    gui()->qt_settings.unit_info_pos_x = 0;
  }
  if (event->pos().y() != 0) {
    gui()->qt_settings.unit_info_pos_y = 1 + (event->pos().y() * 1000)
                                          / gui()->mapview_wdg->height();
  } else {
    gui()->qt_settings.unit_info_pos_y = 0;
  }
}


/****************************************************************************
  Update possible action for given units
****************************************************************************/
void hud_units::update_actions(unit_list *punits)
{
  QString text_str;
  struct city *pcity;
  struct unit *punit;
  struct player *owner;
  struct canvas *unit_pixmap;
  struct canvas *tile_pixmap;
  QPixmap pix;
  QFont font = *fc_font::instance()->get_font(fonts::notify_label);
  QFontMetrics *fm;
  QString mp;
  int wwidth;
  int num;
  QString snum;

  punit = head_of_units_in_focus();
  if (punit == nullptr) {
    hide();
    return;
  }

  setFixedHeight(parentWidget()->height() / 12);
  text_label.setFixedHeight((height() * 2) / 10);
  move((gui()->mapview_wdg->width()
        * gui()->qt_settings.unit_info_pos_x) / 1000,
        (gui()->mapview_wdg->height()
        * gui()->qt_settings.unit_info_pos_y) / 1000);
  unit_icons->setFixedHeight((height() * 8) / 10);

  setUpdatesEnabled(false);

  text_str = QString(unit_name_translation(punit));
  owner = punit->owner;
  pcity = player_city_by_number(owner, punit->homecity);
  if (pcity != NULL) {
    text_str = QString(("%1(%2)"))
               .arg(unit_name_translation(punit), city_name_get(pcity));
  }
  text_str = text_str + " ";
  mp = QString(move_points_text(punit->moves_left, false));
  if (utype_fuel(unit_type_get(punit))) {
    mp = mp + QString("(") + QString(move_points_text((
                                     unit_type_get(punit)->move_rate
                                     * ((punit->fuel) - 1)
                                     + punit->moves_left), false))
                                     + QString(")");
  }
  /* TRANS: MP = Movement points */
  mp = QString(_("MP: ")) + mp;
  text_str = text_str + mp + " ";
  text_str += QString(_("HP:%1/%2")).arg(
                QString::number(punit->hp),
                QString::number(unit_type_get(punit)->hp));
  num = unit_list_size(punit->tile->units);
  snum = QString::number(unit_list_size(punit->tile->units) - 1);
  if (num > 2) {
    text_str = text_str + QString(_(" +%1 units"))
                                  .arg(snum.toLocal8Bit().data());
  } else if (num == 2) {
    text_str = text_str + QString(_(" +1 unit"));
  }
  text_label.setText(text_str);
  font.setPixelSize((text_label.height() * 8) / 10);
  text_label.setFont(font);
  fm = new QFontMetrics(font);
  text_label.setFixedWidth(fm->width(text_str) + 3);
  delete fm;
  if (tileset_is_isometric(tileset)) {
    unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                    tileset_tile_height(tileset) * 3 / 2);
  } else {
    unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                    tileset_tile_height(tileset));
  }
  unit_pixmap->map_pixmap.fill(Qt::transparent);
  put_unit(punit, unit_pixmap, 1,  0, 0);
  pix = (&unit_pixmap->map_pixmap)->scaledToHeight(height());
  wwidth = 2 * 3 + pix.width();
  unit_label.setPixmap(pix);
  if (tileset_is_isometric(tileset)) {
    tile_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                    tileset_tile_height(tileset) * 2);
  } else {
    tile_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                    tileset_tile_height(tileset));
  }
  tile_pixmap->map_pixmap.fill(QColor(0 , 0 , 0 , 0));
  put_terrain(punit->tile, tile_pixmap, 1.0,  0, 0);
  pix = (&tile_pixmap->map_pixmap)->scaledToHeight(height());
  tile_label.setPixmap(pix);
  unit_label.setToolTip(popup_info_text(punit->tile));
  tile_label.setToolTip(popup_info_text(punit->tile));
  wwidth = wwidth + pix.width();
  qtg_canvas_free(tile_pixmap);
  qtg_canvas_free(unit_pixmap);

  setFixedWidth(wwidth + qMax(unit_icons->update_actions() * (height() * 8)
                              / 10, text_label.width()));
  mw->put_to_corner();
  setUpdatesEnabled(true);
  updateGeometry();
  update();

  show();
}

/****************************************************************************
  Custom label with extra mouse events
****************************************************************************/
click_label::click_label() : QLabel()
{
  connect(this, SIGNAL(left_clicked()), SLOT(on_clicked()));
}

/****************************************************************************
  Mouse event for click_label
****************************************************************************/
void click_label::mousePressEvent(QMouseEvent *e)
{
  if (e->button() == Qt::LeftButton) {
    emit left_clicked();
  }
}

/****************************************************************************
  Centers on current unit
****************************************************************************/
void click_label::on_clicked()
{
  gui()->game_tab_widget->setCurrentIndex(0);
  request_center_focus_unit();
}

/****************************************************************************
  Hud action constructor, used to show one action
****************************************************************************/
hud_action::hud_action(QWidget *parent) : QWidget(parent)
{
  connect(this, SIGNAL(left_clicked()), SLOT(on_clicked()));
  setFocusPolicy(Qt::ClickFocus);
  focus = false;
  action_pixmap = nullptr;
}

/****************************************************************************
  Sets given pixmap for hud_action
****************************************************************************/
void hud_action::set_pixmap(QPixmap *p)
{
  action_pixmap = p;
}

/****************************************************************************
  Custom painting for hud_action
****************************************************************************/
void hud_action::paintEvent(QPaintEvent *event)
{
  QRect rx, ry, rz;
  QPainter p;

  rx = QRect(0, 0, width(), height());
  ry = QRect(0, 0, action_pixmap->width(), action_pixmap->height());
  rz = QRect(0, 0, width() - 1, height() - 3);
  p.begin(this);
  p.setCompositionMode(QPainter::CompositionMode_Source);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.drawPixmap(rx, *action_pixmap, ry);
  p.setPen(QColor(palette().color(QPalette::Text)));
  p.drawRect(rz);
   if (focus == true) {
     p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
     p.fillRect(rx, QColor(palette().color(QPalette::Highlight)));
   }
  p.end();

}

/****************************************************************************
  Hud action destructor
****************************************************************************/
hud_action::~hud_action()
{
  if (action_pixmap) {
    delete action_pixmap;
  }
}

/****************************************************************************
  Mouse press event for hud_action
****************************************************************************/
void hud_action::mousePressEvent(QMouseEvent *e)
{
  if (e->button() == Qt::RightButton) {
    emit right_clicked();
  } else if (e->button() == Qt::LeftButton) {
    emit left_clicked();
  }
}

/****************************************************************************
  Leave event for hud_action, used to get status of pixmap higlight
****************************************************************************/
void hud_action::leaveEvent(QEvent *event)
{
  focus = false;
  update();
  QWidget::leaveEvent(event);
}

/****************************************************************************
  Enter event for hud_action, used to get status of pixmap higlight
****************************************************************************/
void hud_action::enterEvent(QEvent *event)
{
  focus = true;
  update();
  QWidget::enterEvent(event);
}

/****************************************************************************
  Right click event for hud_action
****************************************************************************/
void hud_action::on_right_clicked()
{
}

/****************************************************************************
  Left click event for hud_action
****************************************************************************/
void hud_action::on_clicked()
{
  gui()->menu_bar->execute_shortcut(action_shortcut);
}

/****************************************************************************
  Units action contructor, holds possible hud_actions
****************************************************************************/
unit_actions::unit_actions(QWidget *parent, unit *punit) : QWidget(parent)
{
  layout = new QHBoxLayout(this);
  layout->setSpacing(3);
  layout->setContentsMargins(0, 0, 0, 0);
  current_unit = punit;
  init_layout();
  setFocusPolicy(Qt::ClickFocus);
}



/****************************************************************************
  Destructor for unit_actions
****************************************************************************/
unit_actions::~unit_actions()
{
  qDeleteAll(actions);
  actions.clear();
}


/****************************************************************************
  Initiazlizes layout ( layout needs to be changed after adding units )
****************************************************************************/
void unit_actions::init_layout()
{
  QSizePolicy size_fixed_policy(QSizePolicy::MinimumExpanding,
                                QSizePolicy::Fixed,
                                QSizePolicy::Frame);
  setSizePolicy(size_fixed_policy);
  layout->setSpacing(0);
  setLayout(layout);
}


/****************************************************************************
  Updates avaialable actions, returns actions count
****************************************************************************/
int unit_actions::update_actions()
{
  hud_action *a;

  current_unit = head_of_units_in_focus();

  if (current_unit == nullptr) {
    clear_layout();
    hide();
    return 0;
  }
  hide();
  clear_layout();
  setUpdatesEnabled(false);


  foreach (a, actions) {
    delete a;
  }
  qDeleteAll(actions);
  actions.clear();

  /* Create possible actions */

  if (unit_can_build_city(current_unit)) {
    a = new hud_action(this);
    a->action_shortcut = SC_BUILDCITY;
    a->set_pixmap(fc_icons::instance()->get_pixmap("home"));
    actions.append(a);
  }


  if (can_unit_do_activity(current_unit, ACTIVITY_MINE)) {
    struct terrain *pterrain = tile_terrain(unit_tile(current_unit));
    a = new hud_action(this);
    a->action_shortcut = SC_BUILDMINE;
    actions.append(a);
    if (pterrain->mining_result != T_NONE
        && pterrain->mining_result != pterrain) {
      if (!strcmp(terrain_rule_name(pterrain), "Jungle")
          || !strcmp(terrain_rule_name(pterrain), "Plains")
          || !strcmp(terrain_rule_name(pterrain), "Grassland")
          || !strcmp(terrain_rule_name(pterrain), "Swamp")) {
        a->set_pixmap(fc_icons::instance()->get_pixmap("plantforest"));
      } else {
        a->set_pixmap(fc_icons::instance()->get_pixmap("transform"));
      }
    } else {
      a->set_pixmap(fc_icons::instance()->get_pixmap("mine"));
    }
  }

  if (can_unit_do_activity(current_unit, ACTIVITY_IRRIGATE)) {
    struct terrain *pterrain = tile_terrain(unit_tile(current_unit));
    a = new hud_action(this);
    a->action_shortcut = SC_BUILDIRRIGATION;
    if (pterrain->irrigation_result != T_NONE
        && pterrain->irrigation_result != pterrain) {
      if ((!strcmp(terrain_rule_name(pterrain), "Forest") ||
           !strcmp(terrain_rule_name(pterrain), "Jungle"))) {
        a->set_pixmap(fc_icons::instance()->get_pixmap("chopchop"));
      } else {
        a->set_pixmap(fc_icons::instance()->get_pixmap("transform"));
      }
    } else {
      a->set_pixmap(fc_icons::instance()->get_pixmap("irrigation"));
    }
    actions.append(a);
  }

  if (can_unit_do_activity(current_unit, ACTIVITY_TRANSFORM)) {
    a = new hud_action(this);
    a->action_shortcut = SC_TRANSFORM;
    a->set_pixmap(fc_icons::instance()->get_pixmap("transform"));
    actions.append(a);
  }

  /* Road */
  {
    bool ok = false;
    extra_type_by_cause_iterate(EC_ROAD, pextra) {
      struct road_type *proad = extra_road_get(pextra);
      if (can_build_road(proad, current_unit, unit_tile(current_unit))) {
        ok = true;
      }
    }
    extra_type_by_cause_iterate_end;
    if (ok) {
      a = new hud_action(this);
      a->action_shortcut = SC_BUILDROAD;
      a->set_pixmap(fc_icons::instance()->get_pixmap("buildroad"));
      actions.append(a);
    }
  }
  /* Goto */
  a = new hud_action(this);
  a->action_shortcut = SC_GOTO;
  a->set_pixmap(fc_icons::instance()->get_pixmap("goto"));
  actions.append(a);


  if (can_unit_do_activity(current_unit, ACTIVITY_FORTIFYING)) {
    a = new hud_action(this);
    a->action_shortcut = SC_FORTIFY;
    a->set_pixmap(fc_icons::instance()->get_pixmap("fortify"));
    actions.append(a);
  }


  if (can_unit_do_activity(current_unit, ACTIVITY_SENTRY)) {
    a = new hud_action(this);
    a->action_shortcut = SC_SENTRY;
    a->set_pixmap(fc_icons::instance()->get_pixmap("sentry"));
    actions.append(a);
  }
  /* Load */
  if (unit_can_load(current_unit)) {
    a = new hud_action(this);
    a->action_shortcut = SC_LOAD;
    a->set_pixmap(fc_icons::instance()->get_pixmap("load"));
    actions.append(a);
  }
  /* Unload */
  if (unit_transported(current_unit)
      && can_unit_unload(current_unit, unit_transport_get(current_unit))
      && can_unit_exist_at_tile(current_unit, unit_tile(current_unit))) {
    a = new hud_action(this);
    a->action_shortcut = SC_UNLOAD;
    a->set_pixmap(fc_icons::instance()->get_pixmap("unload"));
    actions.append(a);
  }
  /* Nuke */
  if (unit_has_type_flag(current_unit, UTYF_NUCLEAR)) {
    a = new hud_action(this);
    a->action_shortcut = SC_NUKE;
    a->set_pixmap(fc_icons::instance()->get_pixmap("nuke"));
    actions.append(a);
  }

  /* Wait */
  a = new hud_action(this);
  a->action_shortcut = SC_WAIT;
  a->set_pixmap(fc_icons::instance()->get_pixmap("wait"));
  actions.append(a);

  /* Done moving */
  a = new hud_action(this);
  a->action_shortcut = SC_DONE_MOVING;
  a->set_pixmap(fc_icons::instance()->get_pixmap("done"));
  actions.append(a);


  foreach (a, actions) {
    a->setToolTip(gui()->menu_bar->shortcut_2_menustring(a->action_shortcut));
    a->setFixedHeight(height());
    a->setFixedWidth(height());
    layout->addWidget(a);
  }

  setFixedWidth(actions.count() * height());
  setUpdatesEnabled(true);
  show();
  layout->update();
  updateGeometry();
  return actions.count();
}

/****************************************************************************
  Cleans layout - run it before layout initialization
****************************************************************************/
void unit_actions::clear_layout()
{
  int i = actions.count();
  hud_action *ui;
  int j;

  setUpdatesEnabled(false);
  for (j = 0; j < i; j++) {
    ui = actions[j];
    layout->removeWidget(ui);
  }
  while (!actions.empty()) {
    actions.removeFirst();
  }
  setUpdatesEnabled(true);
}

