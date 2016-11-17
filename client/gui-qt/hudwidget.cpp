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
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QRadioButton>
#include <QSpacerItem>
#include <QVBoxLayout>

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
#include "sprite.h"

static QString popup_terrain_info(struct tile *ptile);

/***************************************************************************
  Returns true if player has any unit of unit_type
***************************************************************************/
bool has_player_unit_type(Unit_type_id utype)
{
  unit_list_iterate(client.conn.playing->units, punit) {
    if (utype_number(punit->utype) == utype) {
      return true;
    }
  } unit_list_iterate_end;

  return false;
}

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
  QVBoxLayout *unit_lab;
  QSpacerItem *sp;
  setParent(parent);

  main_layout = new QHBoxLayout;
  sp = new QSpacerItem(50, 2);
  vbox = new QVBoxLayout;
  unit_lab = new QVBoxLayout;
  unit_lab->setContentsMargins(6, 9, 0, 3);
  vbox->setSpacing(0);
  unit_lab->addWidget(&unit_label);
  main_layout->addLayout(unit_lab);
  main_layout->addWidget(&tile_label);
  unit_icons = new unit_actions(this, nullptr);
  vbox->addSpacerItem(sp);
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
  int num;
  int wwidth;
  QFont font = *fc_font::instance()->get_font(fonts::notify_label);
  QFontMetrics *fm;
  QImage cropped_img;
  QImage img;
  QPixmap pix;
  QRect crop;
  QString mp;
  QString snum;
  QString text_str;
  struct canvas *tile_pixmap;
  struct canvas *unit_pixmap;
  struct city *pcity;
  struct player *owner;
  struct unit *punit;

  punit = head_of_units_in_focus();
  if (punit == nullptr) {
    hide();
    return;
  }

  font.setCapitalization(QFont::AllUppercase);
  font.setBold(true);
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
  if (unit_list_size(get_units_in_focus()) > 1) {
    text_str = text_str + QString(_(" (Selected %1 units)"))
               .arg(unit_list_size(get_units_in_focus()));
  } else if (num > 2) {
    text_str = text_str + QString(_(" +%1 units"))
                                  .arg(snum.toLocal8Bit().data());
  } else if (num == 2) {
    text_str = text_str + QString(_(" +1 unit"));
  }
  text_label.setText(text_str);
  font.setPixelSize((text_label.height() * 9) / 10);
  text_label.setFont(font);
  fm = new QFontMetrics(font);
  text_label.setFixedWidth(fm->width(text_str) + 20);
  delete fm;

  unit_pixmap = qtg_canvas_create(tileset_unit_width(tileset),
                                  tileset_unit_height(tileset));
  unit_pixmap->map_pixmap.fill(Qt::transparent);
  put_unit(punit, unit_pixmap, 1,  0, 0);
  img = unit_pixmap->map_pixmap.toImage();
  crop = zealous_crop_rect(img);
  cropped_img = img.copy(crop);
  img = cropped_img.scaledToHeight(height(), Qt::SmoothTransformation);
  pix = QPixmap::fromImage(img);
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
  img = tile_pixmap->map_pixmap.toImage();
  crop = zealous_crop_rect(img);
  cropped_img = img.copy(crop);
  img = cropped_img.scaledToWidth(wwidth - 10,
                                  Qt::SmoothTransformation);
  pix = QPixmap::fromImage(img);
  tile_label.setPixmap(pix);
  unit_label.setToolTip(popup_info_text(punit->tile));
  tile_label.setToolTip(popup_terrain_info(punit->tile));
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

  if (unit_can_add_or_build_city(current_unit)) {
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
  /* Set homecity */
  if (tile_city(unit_tile(current_unit))) {
    if (can_unit_change_homecity_to(current_unit,
                                    tile_city(unit_tile(current_unit)))) {
      a = new hud_action(this);
      a->action_shortcut = SC_SETHOME;
      a->set_pixmap(fc_icons::instance()->get_pixmap("set_homecity"));
      actions.append(a);
    }
  }
  /* Upgrade */
  if (UU_OK == unit_upgrade_test(current_unit, FALSE)) {
    a = new hud_action(this);
    a->action_shortcut = SC_UPGRADE_UNIT;
    a->set_pixmap(fc_icons::instance()->get_pixmap("upgrade"));
    actions.append(a);
  }
  /* Automate */
  if (can_unit_do_autosettlers(current_unit)) {
    a = new hud_action(this);
    a->action_shortcut = SC_AUTOMATE;
    a->set_pixmap(fc_icons::instance()->get_pixmap("automate"));
    actions.append(a);
  }
  /* Paradrop */
  if (can_unit_paradrop(current_unit)) {
    a = new hud_action(this);
    a->action_shortcut = SC_PARADROP;
    a->set_pixmap(fc_icons::instance()->get_pixmap("paradrop"));
    actions.append(a);
  }
  /* Clean pollution */
  if (can_unit_do_activity(current_unit, ACTIVITY_POLLUTION)) {
    a = new hud_action(this);
    a->action_shortcut = SC_PARADROP;
    a->set_pixmap(fc_icons::instance()->get_pixmap("pollution"));
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
  if (unit_can_do_action(current_unit, ACTION_NUKE)) {
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
    delete ui;
  }
  while (!actions.empty()) {
    actions.removeFirst();
  }
  setUpdatesEnabled(true);
}

/****************************************************************************
  Constructor for widget allowing loading units on transports
****************************************************************************/
hud_unit_loader::hud_unit_loader(struct unit *pcargo, struct tile *ptile)
{
  setProperty("showGrid", "false");
  setProperty("selectionBehavior", "SelectRows");
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setSelectionMode(QAbstractItemView::SingleSelection);
  verticalHeader()->setVisible(false);
  horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  horizontalHeader()->setVisible(false);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  connect(selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)), this,
          SLOT(selection_changed(const QItemSelection &,
                                 const QItemSelection &)));
  cargo = pcargo;
  qtile = ptile;

}

/****************************************************************************
  Destructor for units loader
****************************************************************************/
hud_unit_loader::~hud_unit_loader()
{

}

/****************************************************************************
  Shows unit loader, adds possible tranportsand units to table
  Calculates table size
****************************************************************************/
void hud_unit_loader::show_me()
{
  QTableWidgetItem *new_item;
  int max_size = 0;
  int i, j;
  int w,h;
  sprite *spite;

  unit_list_iterate(qtile->units, ptransport) {
    if (can_unit_transport(ptransport, cargo)
        && get_transporter_occupancy(ptransport)
        < get_transporter_capacity(ptransport)) {
      transports.append(ptransport);
      max_size = qMax(max_size, get_transporter_occupancy(ptransport));
    }
  } unit_list_iterate_end;

  setRowCount(transports.count());
  setColumnCount(max_size + 1);
  for (i = 0 ; i < transports.count(); i++) {
    QString str;
    spite = get_unittype_sprite(tileset, transports.at(i)->utype,
                                direction8_invalid());
    str = utype_rule_name(transports.at(i)->utype);
    /* TRANS: MP - just movement points */
    str = str + " ("
          + QString(move_points_text(transports.at(i)->moves_left, false))
          + _("MP") + ")";
    new_item = new QTableWidgetItem(QIcon(*spite->pm), str);
    setItem(i, 0, new_item);
    j = 1;
    unit_list_iterate(transports.at(i)->transporting, tunit) {
      spite = get_unittype_sprite(tileset, tunit->utype,
                                  direction8_invalid());
      new_item = new QTableWidgetItem(QIcon(*spite->pm), "");
      setItem(i, j, new_item);
      j++;
    } unit_list_iterate_end;
  }

   w = verticalHeader()->width() + 4;
   for (i = 0; i < columnCount(); i++) {
      w += columnWidth(i);
   }
   h = horizontalHeader()->height() + 4;
   for (i = 0; i < rowCount(); i++) {
      h += rowHeight(i);
   }

  resize(w, h);
  setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Dialog
                 | Qt::FramelessWindowHint);
  show();
}

/****************************************************************************
  Selects given tranport and closes widget
****************************************************************************/
void hud_unit_loader::selection_changed(const QItemSelection& s1,
                                        const QItemSelection& s2)
{
  int curr_row;

  curr_row = s1.indexes().at(0).row();
  request_unit_load(cargo, transports.at(curr_row), qtile);
  close();
}

/****************************************************************************
  Constructor for unit_hud_selector
****************************************************************************/
unit_hud_selector::unit_hud_selector(QWidget *parent) : QFrame(parent)
{
  QHBoxLayout *hbox, *hibox;
  Unit_type_id utype_id;
  QGroupBox *no_name;
  QVBoxLayout *groupbox_layout;

  hide();
  struct unit *punit = head_of_units_in_focus();

  setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Dialog
                 | Qt::FramelessWindowHint);
  main_layout = new QVBoxLayout(this);

  unit_sel_type = new QComboBox();

  unit_type_iterate(utype) {
    utype_id = utype_index(utype);
    if (has_player_unit_type(utype_id)) {
      unit_sel_type->addItem(utype_name_translation(utype), utype_id);
    }
  }
  unit_type_iterate_end;

  if (punit) {
    int i;
    i = unit_sel_type->findText(utype_name_translation(punit->utype));
    unit_sel_type->setCurrentIndex(i);
  }
  no_name = new QGroupBox();
  no_name->setTitle(_("Unit type"));
  this_type = new QRadioButton(_("Selected type"), no_name);
  this_type->setChecked(true);
  any_type = new QRadioButton(_("All types"), no_name);
  connect(unit_sel_type, SIGNAL(currentIndexChanged(int)), this,
          SLOT(select_units(int)));
  connect(this_type, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  connect(any_type, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  groupbox_layout = new QVBoxLayout;
  groupbox_layout->addWidget(unit_sel_type);
  groupbox_layout->addWidget(this_type);
  groupbox_layout->addWidget(any_type);
  no_name->setLayout(groupbox_layout);
  hibox = new QHBoxLayout;
  hibox->addWidget(no_name);

  no_name = new QGroupBox();
  no_name->setTitle(_("Unit activity"));
  any_activity = new QRadioButton(_("Any activity"), no_name);
  any_activity->setChecked(true);
  fortified = new QRadioButton(_("Fortified"), no_name);
  idle = new QRadioButton(_("Idle"), no_name);
  sentried = new QRadioButton(_("Sentried"), no_name);
  connect(any_activity, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  connect(idle, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  connect(fortified, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  connect(sentried, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  groupbox_layout = new QVBoxLayout;
  groupbox_layout->addWidget(any_activity);
  groupbox_layout->addWidget(idle);
  groupbox_layout->addWidget(fortified);
  groupbox_layout->addWidget(sentried);
  no_name->setLayout(groupbox_layout);
  hibox->addWidget(no_name);
  main_layout->addLayout(hibox);

  no_name = new QGroupBox();
  no_name->setTitle(_("Unit HP and MP"));
  any = new QRadioButton(_("Any unit"), no_name);
  full_hp = new QRadioButton(_("Full HP"), no_name);
  full_mp = new QRadioButton(_("Full MP"), no_name);
  full_hp_mp = new QRadioButton(_("Full HP and MP"), no_name);
  full_hp_mp->setChecked(true);
  connect(any, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  connect(full_hp, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  connect(full_mp, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  connect(full_hp_mp, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  groupbox_layout = new QVBoxLayout;
  groupbox_layout->addWidget(any);
  groupbox_layout->addWidget(full_hp);
  groupbox_layout->addWidget(full_mp);
  groupbox_layout->addWidget(full_hp_mp);
  no_name->setLayout(groupbox_layout);
  hibox = new QHBoxLayout;
  hibox->addWidget(no_name);

  no_name = new QGroupBox();
  no_name->setTitle(_("Location"));
  everywhere = new QRadioButton(_("Everywhere"), no_name);
  this_tile = new QRadioButton(_("Current tile"), no_name);
  this_continent = new QRadioButton(_("Current continent"), no_name);
  main_continent = new QRadioButton(_("Main continent"), no_name);
  groupbox_layout = new QVBoxLayout;

  if (punit) {
    this_tile->setChecked(true);
  } else {
    this_tile->setDisabled(true);
    this_continent->setDisabled(true);
    main_continent->setChecked(true);
  }

  groupbox_layout->addWidget(this_tile);
  groupbox_layout->addWidget(this_continent);
  groupbox_layout->addWidget(main_continent);
  groupbox_layout->addWidget(everywhere);

  no_name->setLayout(groupbox_layout);
  hibox->addWidget(no_name);
  main_layout->addLayout(hibox);

  select = new QPushButton(_("Select"));
  cancel = new QPushButton(_("Cancel"));
  connect(everywhere, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  connect(this_tile, SIGNAL(toggled(bool)), this, SLOT(select_units(bool)));
  connect(this_continent, SIGNAL(toggled(bool)), this,
          SLOT(select_units(bool)));
  connect(main_continent, SIGNAL(toggled(bool)), this,
          SLOT(select_units(bool)));
  connect(select, SIGNAL(clicked()), this, SLOT(uhs_select()));
  connect(cancel, SIGNAL(clicked()), this, SLOT(uhs_cancel()));
  hbox = new QHBoxLayout;
  hbox->addWidget(cancel);
  hbox->addWidget(select);

  result_label.setAlignment(Qt::AlignCenter);
  main_layout->addWidget(&result_label, Qt::AlignHCenter);
  main_layout->addLayout(hbox);
  setLayout(main_layout);

}

/****************************************************************************
  Shows and moves to center unit_hud_selector
****************************************************************************/
void unit_hud_selector::show_me()
{
  QPoint p;

  p = QPoint((parentWidget()->width() - sizeHint().width()) / 2,
             (parentWidget()->height() - sizeHint().height()) / 2);
  p = parentWidget()->mapToGlobal(p);
  move(p);
  setVisible(true);
  show();
  select_units();
}

/****************************************************************************
  Unit_hud_selector destructor
****************************************************************************/
unit_hud_selector::~unit_hud_selector()
{

}

/****************************************************************************
  Selects and closes widget
****************************************************************************/
void unit_hud_selector::uhs_select()
{
  const struct player *pplayer;

  pplayer = client_player();

  unit_list_iterate(pplayer->units, punit) {
    if (activity_filter(punit) && hp_filter(punit)
        && island_filter(punit) && type_filter(punit)) {
      unit_focus_add(punit);
    }
  } unit_list_iterate_end;
  close();
}

/****************************************************************************
  Closes current widget
****************************************************************************/
void unit_hud_selector::uhs_cancel()
{
  close();
}

/****************************************************************************
  Shows number of selected units on label
****************************************************************************/
void unit_hud_selector::select_units(int x)
{
  int num = 0;
  const struct player *pplayer;

  pplayer = client_player();

  unit_list_iterate(pplayer->units, punit) {
    if (activity_filter(punit) && hp_filter(punit)
        && island_filter(punit) && type_filter(punit)) {
      num++;
    }
  } unit_list_iterate_end;
  result_label.setText(QString(PL_("%1 unit", "%1 units", num)).arg(num));
}

/****************************************************************************
  Convinient slot for ez connect
****************************************************************************/
void unit_hud_selector::select_units(bool x)
{
  select_units(0);
}

/****************************************************************************
  Key press event for unit_hud_selector
****************************************************************************/
void unit_hud_selector::keyPressEvent(QKeyEvent *event)
{
  if ((event->key() == Qt::Key_Return)
      || (event->key() == Qt::Key_Enter)) {
    uhs_select();
  }
  if (event->key() == Qt::Key_Escape) {
    close();
    event->accept();
  }
  QWidget::keyPressEvent(event);
}

/****************************************************************************
  Filter by activity
****************************************************************************/
bool unit_hud_selector::activity_filter(struct unit *punit)
{
  if ((punit->activity == ACTIVITY_FORTIFIED && fortified->isChecked())
      || (punit->activity == ACTIVITY_SENTRY && sentried->isChecked())
      || (punit->activity == ACTIVITY_IDLE && idle->isChecked())
      || any_activity->isChecked()) {
    return true;
  }
  return false;
}

/****************************************************************************
  Filter by hp/mp
****************************************************************************/
bool unit_hud_selector::hp_filter(struct unit *punit)
{
  if ((any->isChecked()
       || (full_mp->isChecked()
           && punit->moves_left  >= punit->utype->move_rate)
       || (full_hp->isChecked() && punit->hp >= punit->utype->hp)
       || (full_hp_mp->isChecked() && punit->hp >= punit->utype->hp
           && punit->moves_left  >= punit->utype->move_rate))) {
    return true;
  }
  return false;
}

/****************************************************************************
  Filter by location
****************************************************************************/
bool unit_hud_selector::island_filter(struct unit *punit)
{
  int island = -1;
  struct unit *cunit = head_of_units_in_focus();

  if (this_tile->isChecked() && cunit) {
    if (punit->tile == cunit->tile) {
      return true;
    }
  }

  if (main_continent->isChecked() && player_capital(client_player())) {
    island = player_capital(client_player())->tile->continent;
  } else if (this_continent->isChecked() && cunit) {
    island = cunit->tile->continent;
  }

  if (island > -1) {
    if (punit->tile->continent == island) {
      return true;
    }
  }

  if (everywhere->isChecked()) {
    return true;
  }
  return false;
}

/****************************************************************************
  Filter by type
****************************************************************************/
bool unit_hud_selector::type_filter(struct unit *punit)
{
  QVariant qvar;
  Unit_type_id utype_id;

  if (this_type->isChecked()) {
    qvar = unit_sel_type->currentData();
    utype_id = qvar.toInt();
    if (utype_id == utype_index(punit->utype)) {
      return true;
    } else {
      return false;
    }
  }
  if (any_type->isChecked()) {
    return true;
  }
  return false;
}

/****************************************************************************
  Tooltip text for terrain information
****************************************************************************/
QString popup_terrain_info(struct tile *ptile)
{
  QString ret, t;
  struct terrain *terr;

  terr = ptile->terrain;
  ret = QString(_("Terrain: %1\n")).arg(tile_get_info_text(ptile, TRUE, 0));
  ret = ret + QString(_("Food/Prod/Trade: %1\n"))
              .arg(get_tile_output_text(ptile));
  t = get_infrastructure_text(ptile->extras);
  if (t != '\0') {
    ret = ret + QString(_("Infrastructure: %1\n")).arg(t);
  }
  ret = ret + QString(_("Defence bonus: %1%")).arg(terr->defense_bonus);
  return ret;
}
