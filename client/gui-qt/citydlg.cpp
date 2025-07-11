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

// Qt
#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QImage>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QRadioButton>
#include <QRect>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

// utility
#include "support.h"

// common
#include "citizens.h"
#include "city.h"
#include "counters.h"
#include "game.h"

//agents
#include "cma_core.h"
#include "cma_fec.h"

// client
#include "citydlg_common.h"
#include "client_main.h"
#include "climisc.h"
#include "control.h"
#include "global_worklist.h"
#include "helpdata.h"
#include "mapview_common.h"
#include "movement.h"
#include "sprite.h"
#include "text.h"
#include "tilespec.h"

// gui-qt
#include "citydlg.h"
#include "colors.h"
#include "fc_client.h"
#include "gui_main.h"
#include "hudwidget.h"

static bool city_dlg_created = false; /** defines if dialog for city has been
                                       * already created. It's created only
                                       * once per client
                                       */
static city_dialog *city_dlg;
extern QString split_text(QString text, bool cut);
extern QString cut_helptext(QString text);

/************************************************************************//**
  Custom progressbar constructor
****************************************************************************/
progress_bar::progress_bar(QWidget *parent): QProgressBar(parent)
{
  m_timer.start();
  startTimer(50);
  create_region();
  sfont = new QFont;
  m_animate_step = 0;
  pix = nullptr;
}

/************************************************************************//**
  Custom progressbar destructor
****************************************************************************/
progress_bar::~progress_bar()
{
  if (pix != nullptr) {
    delete pix;
  }
  delete sfont;
}

/************************************************************************//**
  Custom progressbar resize event
****************************************************************************/
void progress_bar::resizeEvent(QResizeEvent *event)
{
  create_region();
}

/************************************************************************//**
  Sets pixmap from given universal for custom progressbar
****************************************************************************/
void progress_bar::set_pixmap(struct universal *target)
{
  struct sprite *sprite;
  QImage cropped_img;
  QImage img;
  QPixmap tpix;
  QRect crop;

  if (VUT_UTYPE == target->kind) {
    sprite = get_unittype_sprite(get_tileset(), target->value.utype,
                                 ACTIVITY_LAST, direction8_invalid());
  } else {
    sprite = get_building_sprite(tileset, target->value.building);
  }
  if (pix != nullptr) {
    delete pix;
  }
  if (sprite == nullptr) {
    pix = nullptr;
    return;
  }
  img = sprite->pm->toImage();
  crop = zealous_crop_rect(img);
  cropped_img = img.copy(crop);
  tpix = QPixmap::fromImage(cropped_img);
  pix = new QPixmap(tpix.width(), tpix.height());
  pix->fill(Qt::transparent);
  pixmap_copy(pix, &tpix, 0 , 0, 0, 0, tpix.width(), tpix.height());
}

/************************************************************************//**
  Sets pixmap from given tech number for custom progressbar
****************************************************************************/
void progress_bar::set_pixmap(int n)
{
  struct sprite *sprite;

  if (valid_advance_by_number(n) && !is_future_tech(n)) {
    sprite = get_tech_sprite(tileset, n);
  } else {
    sprite = nullptr;
  }
  if (pix != nullptr) {
    delete pix;
  }
  if (sprite == nullptr) {
    pix = nullptr;
    return;
  }
  pix = new QPixmap(sprite->pm->width(),
                    sprite->pm->height());
  pix->fill(Qt::transparent);
  pixmap_copy(pix, sprite->pm, 0 , 0, 0, 0,
              sprite->pm->width(), sprite->pm->height());
  if (isVisible()) {
    update();
  }
}

/************************************************************************//**
  Timer event used to animate progress
****************************************************************************/
void progress_bar::timerEvent(QTimerEvent *event)
{
  if ((value() != minimum() && value() < maximum())
      || (0 == minimum() && 0 == maximum())) {
    m_animate_step = m_timer.elapsed() / 50;
    update();
  }
}

/************************************************************************//**
  Paint event for custom progress bar
****************************************************************************/
void progress_bar::paintEvent(QPaintEvent *event)
{
  QPainter p;
  QLinearGradient g, gx;
  QColor c;
  QRect r, rx, r2;
  int max;
  int f_pixel_size;
  int pix_width = 0;
  int pixel_size = sfont->pixelSize();
  int pbw, pbh;

  if (pix != nullptr) {
    pix_width = height() - 4;
  }

  if (pixel_size > 0) {
    f_pixel_size = pixel_size;
  } else {
    QFontMetrics fm(*sfont);

    f_pixel_size = fm.height();
  }

  pbw = width();
  pbh = height();

  rx.setX(0);
  rx.setY(0);
  rx.setWidth(pbw);
  rx.setHeight(pbh);
  p.begin(this);
  p.drawLine(rx.topLeft(), rx.topRight());
  p.drawLine(rx.bottomLeft(), rx.bottomRight());

  max = maximum();

  if (max == 0) {
    max = 1;
  }

  r = QRect(0, 0, pbw * value() / max, pbh);

  gx = QLinearGradient(0 , 0, 0, pbh);
  c = QColor(palette().color(QPalette::Highlight));
  gx.setColorAt(0, c);
  gx.setColorAt(0.5, QColor(40, 40, 40));
  gx.setColorAt(1, c);
  p.fillRect(r, QBrush(gx));
  p.setClipRegion(reg.translated(m_animate_step % 32, 0));

  g = QLinearGradient(0 , 0, pbw, pbh);
  c.setAlphaF(0.1);
  g.setColorAt(0, c);
  c.setAlphaF(0.9);
  g.setColorAt(1, c);
  p.fillRect(r, QBrush(g));

  p.setClipping(false);
  r2 = QRect(pbw * value() / max, 0, pbw, pbh);
  c = palette().color(QPalette::Window);
  p.fillRect(r2, c);

  // Draw icon
  if (pix != nullptr) {
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.drawPixmap(2 , 2, pix_width
    * static_cast<float>(pix->width()) / pix->height(),
                 pix_width, *pix, 0, 0, pix->width(), pix->height());
  }

  // Draw text
  c = palette().color(QPalette::Text);
  p.setPen(c);
  sfont->setCapitalization(QFont::AllUppercase);
  sfont->setBold(true);

  if (text().contains('\n')) {
    QString s1, s2;
    int i, j;
    QFont tmp_font = *sfont; // Don't make changes to font to be kept

    i = text().indexOf('\n');
    s1 = text().left(i);
    s2 = text().right(text().length() - i);

    if (2 * f_pixel_size >= 3 * pbh / 2) {
      tmp_font.setPixelSize(pbh / 3);
    }

    p.setFont(tmp_font);

    j = pbh - 2 * f_pixel_size;
    p.setCompositionMode(QPainter::CompositionMode_ColorDodge);
    QFontMetrics fm(tmp_font);

    if (fm.horizontalAdvance(s1) > rx.width()) {
      s1 = fm.elidedText(s1, Qt::ElideRight, rx.width());
    }

    i = rx.width() - fm.horizontalAdvance(s1) + pix_width;
    i = qMax(0, i);
    p.drawText(i / 2, j / 3 + f_pixel_size, s1);

    if (fm.horizontalAdvance(s2) > rx.width()) {
      s2 = fm.elidedText(s2, Qt::ElideRight, rx.width());
    }

    i = rx.width() - fm.horizontalAdvance(s2) + pix_width;
    i = qMax(0, i);

    p.drawText(i / 2, pbh - j / 3, s2);
  } else {
    QString s;
    int i, j;

    p.setFont(*sfont);

    s = text();
    j = pbh - f_pixel_size;
    p.setCompositionMode(QPainter::CompositionMode_ColorDodge);
    QFontMetrics fm(*sfont);

    if (fm.horizontalAdvance(s) > rx.width()) {
      s = fm.elidedText(s, Qt::ElideRight, rx.width());
    }

    i = rx.width() - fm.horizontalAdvance(s) + pix_width;
    i = qMax(0, i);
    p.drawText(i / 2, j / 2 + f_pixel_size, s);
  }

  p.end();
}

/************************************************************************//**
  Creates region with diagonal lines
****************************************************************************/
void progress_bar::create_region()
{
  int offset;
  QRect   r(-50, 0, width() + 50, height());
  int chunk_width = 16;
  int size = width()  + 50;
  reg = QRegion();

  for (offset = 0; offset < (size * 2); offset += (chunk_width * 2)) {
    QPolygon a;

    a.setPoints(4, r.x(), r.y() + offset,
                r.x() + r.width(), (r.y() + offset) - size,
                r.x() + r.width(),
                (r.y() + offset + chunk_width) - size,
                r.x(), r.y() + offset + chunk_width);
    reg += QRegion(a);
  }

}

/************************************************************************//**
  Draws X on pixmap pointing its useless
****************************************************************************/
static void pixmap_put_x(QPixmap *pix)
{
  QPen pen(QColor(0, 0, 0));
  QPainter p;

  pen.setWidth(2);
  p.begin(pix);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(pen);
  p.drawLine(0, 0, pix->width(), pix->height());
  p.drawLine(pix->width(), 0, 0, pix->height());
  p.end();
}

/************************************************************************//**
  Improvement item constructor
****************************************************************************/
impr_item::impr_item(QWidget *parent, const impr_type *building,
                     struct city *pcity): QLabel(parent)
{
  setParent(parent);
  dlgcity = pcity;
  impr = building;
  impr_pixmap = nullptr;
  struct sprite *sprite;
  sprite = get_building_sprite(tileset , building);

  if (sprite != nullptr) {
    impr_pixmap = qtg_canvas_create(sprite->pm->width(),
                                    sprite->pm->height());
    impr_pixmap->map_pixmap.fill(Qt::transparent);
    pixmap_copy(&impr_pixmap->map_pixmap, sprite->pm, 0 , 0, 0, 0,
                sprite->pm->width(), sprite->pm->height());
  } else {
    impr_pixmap = qtg_canvas_create(10, 10);
    impr_pixmap->map_pixmap.fill(Qt::red);
  }

  setFixedWidth(impr_pixmap->map_pixmap.width() + 4);
  setFixedHeight(impr_pixmap->map_pixmap.height());
  setToolTip(get_tooltip_improvement(building, pcity, true).trimmed());
}

/************************************************************************//**
  Improvement item destructor
****************************************************************************/
impr_item::~impr_item()
{
  if (impr_pixmap) {
    canvas_free(impr_pixmap);
  }
}

/************************************************************************//**
  Sets pixmap to improvemnt item
****************************************************************************/
void impr_item::init_pix()
{
  setPixmap(impr_pixmap->map_pixmap);
  update();
}

/************************************************************************//**
  Mouse enters widget
****************************************************************************/
void impr_item::enterEvent(QEnterEvent *event)
{
  struct sprite *sprite;
  QPainter p;

  if (impr_pixmap) {
    canvas_free(impr_pixmap);
  }

  sprite = get_building_sprite(tileset , impr);
  if (impr && sprite) {
    impr_pixmap = qtg_canvas_create(sprite->pm->width(),
                                    sprite->pm->height());
    impr_pixmap->map_pixmap.fill(QColor(palette().color(QPalette::Highlight)));
    pixmap_copy(&impr_pixmap->map_pixmap, sprite->pm, 0 , 0, 0, 0,
                sprite->pm->width(), sprite->pm->height());
  } else {
    impr_pixmap = qtg_canvas_create(10, 10);
    impr_pixmap->map_pixmap.fill(QColor(palette().color(QPalette::Highlight)));
  }

  init_pix();
}

/************************************************************************//**
  Mouse leaves widget
****************************************************************************/
void impr_item::leaveEvent(QEvent *event)
{
  struct sprite *sprite;

  if (impr_pixmap) {
    canvas_free(impr_pixmap);
  }

  sprite = get_building_sprite(tileset , impr);
  if (impr && sprite) {
    impr_pixmap = qtg_canvas_create(sprite->pm->width(),
                                    sprite->pm->height());
    impr_pixmap->map_pixmap.fill(Qt::transparent);
    pixmap_copy(&impr_pixmap->map_pixmap, sprite->pm, 0 , 0, 0, 0,
                sprite->pm->width(), sprite->pm->height());
  } else {
    impr_pixmap = qtg_canvas_create(10, 10);
    impr_pixmap->map_pixmap.fill(Qt::red);
  }

  init_pix();
}

/************************************************************************//**
  Improvement list constructor
****************************************************************************/
impr_info::impr_info(QWidget *parent): QFrame(parent)
{
  setParent(parent);
  layout = new QHBoxLayout(this);
  init_layout();
}

/************************************************************************//**
  Inits improvement list constructor
****************************************************************************/
void impr_info::init_layout()
{
  QSizePolicy size_fixed_policy(QSizePolicy::Fixed,
                                QSizePolicy::MinimumExpanding,
                                QSizePolicy::Slider);

  setSizePolicy(size_fixed_policy);
  setLayout(layout);
}

/************************************************************************//**
  Improvement list destructor
****************************************************************************/
impr_info::~impr_info()
{

}

/************************************************************************//**
  Adds improvement item to list
****************************************************************************/
void impr_info::add_item(impr_item *item)
{
  impr_list.append(item);
}

/************************************************************************//**
  Clears layout on improvement list
****************************************************************************/
void impr_info::clear_layout()
{
  int i = impr_list.count();
  impr_item *ui;
  int j;
  setUpdatesEnabled(false);
  setMouseTracking(false);

  for (j = 0; j < i; j++) {
    ui = impr_list[j];
    layout->removeWidget(ui);
    delete ui;
  }

  while (!impr_list.empty()) {
    impr_list.removeFirst();
  }

  setMouseTracking(true);
  setUpdatesEnabled(true);
}

/************************************************************************//**
  Mouse wheel event - send it to scrollbar
****************************************************************************/
void impr_info::wheelEvent(QWheelEvent *event)
{
  QPoint p;

  p = parentWidget()->parentWidget()->pos();
  p = mapToGlobal(p);

  QWheelEvent new_event(QPoint(5, 5), p + QPoint(5,5), event->pixelDelta(),
                        event->angleDelta(),
                        event->buttons(),
                        event->modifiers(),
                        event->phase(), false, event->source());

  QApplication::sendEvent(parentWidget(), &new_event);
}

/************************************************************************//**
  Updates list of improvements
****************************************************************************/
void impr_info::update_buildings()
{
  int i = impr_list.count();
  int j;
  int h = 0;
  impr_item *ui;

  setUpdatesEnabled(false);
  hide();

  for (j = 0; j < i; j++) {
    ui = impr_list[j];
    h = ui->height();
    layout->addWidget(ui, 0, Qt::AlignVCenter);
  }

  if (impr_list.count() > 0) {
    parentWidget()->parentWidget()->setFixedHeight(city_dlg->scroll_height
                                                   + h + 6);
  } else {
    parentWidget()->parentWidget()->setFixedHeight(0);
  }

  show();
  setUpdatesEnabled(true);
  layout->update();
  updateGeometry();
}

/************************************************************************//**
  Mouse wheel event - send it to scrollbar
****************************************************************************/
void impr_item::wheelEvent(QWheelEvent *event)
{
  QPoint p;

  p = parentWidget()->parentWidget()->pos();
  p = mapToGlobal(p);

  QWheelEvent new_event(QPoint(5, 5), p + QPoint(5,5), event->pixelDelta(),
                        event->angleDelta(),
                        event->buttons(),
                        event->modifiers(),
                        event->phase(), false, event->source());

  QApplication::sendEvent(parentWidget()->parentWidget(),
                          &new_event);
}

/************************************************************************//**
  Double click event on improvement item
****************************************************************************/
void impr_item::mouseDoubleClickEvent(QMouseEvent *event)
{
  if (!can_client_issue_orders()) {
    return;
  }

  if (event->button() == Qt::LeftButton) {
    char buf[256];
    int price;
    const int impr_id = improvement_number(impr);
    const int city_id = dlgcity->id;
    hud_message_box *ask;

    if (test_player_sell_building_now(client_player(), dlgcity,
                                      impr) != TR_SUCCESS) {
      return;
    }

    ask = new hud_message_box(city_dlg);

    price = impr_sell_gold(impr);
    fc_snprintf(buf, ARRAY_SIZE(buf),
                PL_("Sell %s for %d gold?",
                    "Sell %s for %d gold?", price),
                city_improvement_name_translation(dlgcity, impr), price);

    ask->set_text_title(buf, (_("Sell improvement?")));
    ask->setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    ask->setAttribute(Qt::WA_DeleteOnClose);
    connect(ask, &hud_message_box::accepted, [=]() {
      struct city *pcity = game_city_by_number(city_id);

      if (pcity == nullptr) {
        return;
      }
      city_sell_improvement(pcity, impr_id);
    });
    ask->show();
  }
}

/************************************************************************//**
  Class representing one unit, allows context menu, holds pixmap for it
****************************************************************************/
unit_item::unit_item(QWidget *parent, struct unit *punit,
                     bool supp, int hppy_cost) : QLabel()
{
  happy_cost = hppy_cost;
  QImage cropped_img;
  QImage img;
  QRect crop;
  qunit = punit;
  struct canvas *unit_pixmap;
  struct tileset *tmp;
  float isosize;

  setParent(parent);
  supported = supp;

  tmp = nullptr;
  if (unscaled_tileset) {
    tmp = tileset;
    tileset = unscaled_tileset;
  }
  isosize = 0.6;
  if (tileset_hex_height(tileset) > 0 || tileset_hex_width(tileset) > 0) {
    isosize = 0.45;
  }

  if (punit) {
    if (supported) {
      unit_pixmap = qtg_canvas_create(tileset_unit_width(get_tileset()),
                             tileset_unit_with_upkeep_height(get_tileset()));
    } else {
      unit_pixmap = qtg_canvas_create(tileset_unit_width(get_tileset()),
                                      tileset_unit_height(get_tileset()));
    }

    unit_pixmap->map_pixmap.fill(Qt::transparent);
    put_unit(punit, unit_pixmap, 1.0, 0, 0);

    if (supported) {
      put_unit_city_overlays(punit, unit_pixmap, 0,
                             tileset_unit_layout_offset_y(get_tileset()),
                             punit->upkeep, happy_cost);
    }
  } else {
    unit_pixmap = qtg_canvas_create(10, 10);
    unit_pixmap->map_pixmap.fill(Qt::transparent);
  }

  img = unit_pixmap->map_pixmap.toImage();
  crop = zealous_crop_rect(img);
  cropped_img = img.copy(crop);
  if (tileset_is_isometric(tileset)) {
    unit_img = cropped_img.scaledToHeight(tileset_unit_width(get_tileset())
                                          * isosize, Qt::SmoothTransformation);
  } else {
    unit_img = cropped_img.scaledToHeight(tileset_unit_width(get_tileset()),
                                          Qt::SmoothTransformation);
  }
  canvas_free(unit_pixmap);
  if (tmp != nullptr) {
    tileset = tmp;
  }

  create_actions();
  setFixedWidth(unit_img.width() + 4);
  setFixedHeight(unit_img.height());
  setToolTip(unit_description(qunit));
}

/************************************************************************//**
  Sets pixmap for unit_item class
****************************************************************************/
void unit_item::init_pix()
{
  setPixmap(QPixmap::fromImage(unit_img));
  update();
}

/************************************************************************//**
  Destructor for unit item
****************************************************************************/
unit_item::~unit_item()
{
}

/************************************************************************//**
  Context menu handler
****************************************************************************/
void unit_item::contextMenuEvent(QContextMenuEvent *event)
{
  QMenu *menu;

  if (!can_client_issue_orders()) {
    return;
  }

  if (unit_owner(qunit) != client_player()) {
    return;
  }

  menu = new QMenu(gui()->central_wdg);
  menu->addAction(activate);
  menu->addAction(activate_and_close);

  if (sentry) {
    menu->addAction(sentry);
  }

  if (fortify) {
    menu->addAction(fortify);
  }

  if (change_home) {
    menu->addAction(change_home);
  }

  if (load) {
    menu->addAction(load);
  }

  if (unload) {
    menu->addAction(unload);
  }

  if (unload_trans) {
    menu->addAction(unload_trans);
  }

  if (disband_action) {
    menu->addAction(disband_action);
  }

  if (upgrade) {
    menu->addAction(upgrade);
  }

  menu->popup(event->globalPos());
}

/************************************************************************//**
  Initializes context menu
****************************************************************************/
void unit_item::create_actions()
{
  struct unit_list *qunits;

  if (unit_owner(qunit) != client_player() || !can_client_issue_orders()) {
    return;
  }

  qunits = unit_list_new();
  unit_list_append(qunits, qunit);
  activate = new QAction(_("Activate unit"), this);
  connect(activate, &QAction::triggered, this, &unit_item::activate_unit);
  activate_and_close = new QAction(_("Activate and close dialog"), this);
  connect(activate_and_close, &QAction::triggered, this,
          &unit_item::activate_and_close_dialog);

  if (can_unit_do_activity_client(qunit, ACTIVITY_SENTRY)) {
    sentry = new QAction(_("Sentry unit"), this);
    connect(sentry, &QAction::triggered, this, &unit_item::sentry_unit);
  } else {
    sentry = nullptr;
  }

  if (can_unit_do_activity_client(qunit, ACTIVITY_FORTIFYING)) {
    fortify = new QAction(_("Fortify unit"), this);
    connect(fortify, &QAction::triggered, this, &unit_item::fortify_unit);
  } else {
    fortify = nullptr;
  }
  if (unit_can_do_action(qunit, ACTION_DISBAND_UNIT)) {
    disband_action = new QAction(_("Disband unit"), this);
    connect(disband_action, &QAction::triggered, this, &unit_item::disband);
  } else {
    disband_action = nullptr;
  }

  if (can_unit_change_homecity(&(wld.map), qunit)) {
    change_home = new QAction(action_id_name_translation(ACTION_HOME_CITY),
                              this);
    connect(change_home, &QAction::triggered, this, &unit_item::change_homecity);
  } else {
    change_home = nullptr;
  }

  if (units_can_load(qunits)) {
    load = new QAction(_("Load"), this);
    connect(load, &QAction::triggered, this, &unit_item::load_unit);
  } else {
    load = nullptr;
  }

  if (units_can_unload(&(wld.map), qunits)) {
    unload = new QAction(_("Unload"), this);
    connect(unload, &QAction::triggered, this, &unit_item::unload_unit);
  } else {
    unload = nullptr;
  }

  if (units_are_occupied(qunits)) {
    unload_trans = new QAction(_("Unload All From Transporter"), this);
    connect(unload_trans, &QAction::triggered, this, &unit_item::unload_all);
  } else {
    unload_trans = nullptr;
  }

  if (units_can_upgrade(&(wld.map), qunits)) {
    upgrade = new QAction(_("Upgrade Unit"), this);
    connect(upgrade, &QAction::triggered, this, &unit_item::upgrade_unit);
  } else {
    upgrade = nullptr;
  }

  unit_list_destroy(qunits);
}

/************************************************************************//**
  Popups MessageBox  for disbanding unit and disbands it
****************************************************************************/
void unit_item::disband()
{
  struct unit_list *punits;
  struct unit *punit = player_unit_by_number(client_player(), qunit->id);

  if (punit == nullptr) {
    return;
  }

  punits = unit_list_new();
  unit_list_append(punits, punit);
  popup_disband_dialog(punits);
  unit_list_destroy(punits);
}

/************************************************************************//**
  Loads unit into some transport
****************************************************************************/
void unit_item::load_unit()
{
  qtg_request_transport(qunit, unit_tile(qunit));
}

/************************************************************************//**
  Unloads unit
****************************************************************************/
void unit_item::unload_unit()
{
  request_unit_unload(qunit);
}

/************************************************************************//**
  Unloads all units from transporter
****************************************************************************/
void unit_item::unload_all()
{
  request_unit_unload_all(qunit);
}

/************************************************************************//**
  Upgrades unit
****************************************************************************/
void unit_item::upgrade_unit()
{
  struct unit_list *qunits;
  qunits = unit_list_new();
  unit_list_append(qunits, qunit);
  popup_upgrade_dialog(qunits);
  unit_list_destroy(qunits);
}

/************************************************************************//**
  Changes homecity for given unit
****************************************************************************/
void unit_item::change_homecity()
{
  if (qunit) {
    request_unit_change_homecity(qunit);
  }
}

/************************************************************************//**
  Activates unit and closes city dialog
****************************************************************************/
void unit_item::activate_and_close_dialog()
{
  if (qunit) {
    unit_focus_set(qunit);
    qtg_popdown_all_city_dialogs();
  }
}

/************************************************************************//**
  Activates unit in city dialog
****************************************************************************/
void unit_item::activate_unit()
{
  if (qunit) {
    unit_focus_set(qunit);
  }
}

/************************************************************************//**
  Fortifies unit in city dialog
****************************************************************************/
void unit_item::fortify_unit()
{
  if (qunit) {
    request_unit_fortify(qunit);
  }
}

/************************************************************************//**
  Mouse entered widget
****************************************************************************/
void unit_item::enterEvent(QEnterEvent *event)
{
  QImage temp_img(unit_img.size(), QImage::Format_ARGB32_Premultiplied);
  QPainter p;

  p.begin(&temp_img);
  p.fillRect(0, 0, unit_img.width(), unit_img.height(),
             QColor(palette().color(QPalette::Highlight)));
  p.drawImage(0, 0, unit_img);
  p.end();

  setPixmap(QPixmap::fromImage(temp_img));
  update();
}

/************************************************************************//**
  Mouse left widget
****************************************************************************/
void unit_item::leaveEvent(QEvent *event)
{
  init_pix();
}

/************************************************************************//**
  Mouse wheel event - send it to scrollbar
****************************************************************************/
void unit_item::wheelEvent(QWheelEvent *event)
{
  QPoint p;

  p = parentWidget()->parentWidget()->pos();
  p = mapToGlobal(p);

  QWheelEvent new_event(QPoint(5, 5), p + QPoint(5,5), event->pixelDelta(),
                        event->angleDelta(),
                        event->buttons(),
                        event->modifiers(),
                        event->phase(), false, event->source());

  QApplication::sendEvent(parentWidget()->parentWidget(),
                          &new_event);
}

/************************************************************************//**
  Mouse press event -activates unit and closes dialog
****************************************************************************/
void unit_item::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton) {
    if (qunit) {
      unit_focus_set(qunit);
      qtg_popdown_all_city_dialogs();
    }
  }
}

/************************************************************************//**
  Sentries unit in city dialog
****************************************************************************/
void unit_item::sentry_unit()
{
  if (qunit) {
    request_unit_sentry(qunit);
  }
}

/************************************************************************//**
  Class representing list of units ( unit_item 's)
****************************************************************************/
unit_info::unit_info(bool supp) : QFrame()
{
  layout = new QHBoxLayout(this);
  init_layout();
  supports = supp;
}

/************************************************************************//**
  Destructor for unit_info
****************************************************************************/
unit_info::~unit_info()
{
  qDeleteAll(unit_list);
  unit_list.clear();
}

/************************************************************************//**
  Adds one unit to list
****************************************************************************/
void unit_info::add_item(unit_item *item)
{
  unit_list.append(item);
}

/************************************************************************//**
  Initializes layout ( layout needs to be changed after adding units )
****************************************************************************/
void unit_info::init_layout()
{
  QSizePolicy size_fixed_policy(QSizePolicy::Fixed,
                                QSizePolicy::MinimumExpanding,
                                QSizePolicy::Slider);
  setSizePolicy(size_fixed_policy);
  setLayout(layout);
}

/************************************************************************//**
  Mouse wheel event - send it to scrollbar
****************************************************************************/
void unit_info::wheelEvent(QWheelEvent *event)
{
  QPoint p;

  p = parentWidget()->parentWidget()->pos();
  p = mapToGlobal(p);

  QWheelEvent new_event(QPoint(5, 5), p + QPoint(5,5), event->pixelDelta(),
                        event->angleDelta(),
                        event->buttons(),
                        event->modifiers(),
                        event->phase(), false, event->source());

  QApplication::sendEvent(parentWidget(), &new_event);
}

/************************************************************************//**
  Updates units
****************************************************************************/
void unit_info::update_units()
{
  int i = unit_list.count();
  int j;
  int h;
  float hexfix;
  unit_item *ui;

  setUpdatesEnabled(false);
  hide();

  for (j = 0; j < i; j++) {
    ui = unit_list[j];
    layout->addWidget(ui, 0, Qt::AlignVCenter);
  }

  hexfix = 1.0;
  if (tileset_hex_height(tileset) > 0 || tileset_hex_width(tileset) > 0) {
    hexfix = 0.75;
  }

  if (tileset_is_isometric(tileset)) {
    h = tileset_unit_width(get_tileset()) * 0.7 * hexfix + 6;
  } else {
    h = tileset_unit_width(get_tileset()) + 6;
  }
  if (unit_list.count() > 0) {
    parentWidget()->parentWidget()->setFixedHeight(city_dlg->scroll_height
                                                   + h);
  } else {
    parentWidget()->parentWidget()->setFixedHeight(0);
  }
  show();
  setUpdatesEnabled(true);
  layout->update();
  updateGeometry();
}

/************************************************************************//**
  Cleans layout - run it before layout initialization
****************************************************************************/
void unit_info::clear_layout()
{
  int i = unit_list.count();
  unit_item *ui;
  int j;
  setUpdatesEnabled(false);
  setMouseTracking(false);

  for (j = 0; j < i; j++) {
    ui = unit_list[j];
    layout->removeWidget(ui);
    delete ui;
  }

  while (!unit_list.empty()) {
    unit_list.removeFirst();
  }

  setMouseTracking(true);
  setUpdatesEnabled(true);
}

/************************************************************************//**
  city_label is used only for showing citizens icons
  and was created only to catch mouse events
****************************************************************************/
city_label::city_label(int t, QWidget *parent) : QLabel(parent)
{
  type = t;
}

/************************************************************************//**
  Mouse handler for city_label
****************************************************************************/
void city_label::mousePressEvent(QMouseEvent *event)
{
  int citnum, i;
  int w = tileset_small_sprite_width(tileset) / gui()->map_scale;
  int num_citizens = pcity->size;

  if (cma_is_city_under_agent(pcity, nullptr)) {
    return;
  }

  i = 1 + (num_citizens * 5 / 200);
  w = w / i;
  citnum = event->pos().x() / w;

  if (!can_client_issue_orders()) {
    return;
  }

  city_rotate_specialist(pcity, citnum);
}

/************************************************************************//**
  Just sets target city for city_label
****************************************************************************/
void city_label::set_city(city *pciti)
{
  pcity = pciti;
}

/************************************************************************//**
  Used for showing tiles and workers view in city dialog
****************************************************************************/
city_map::city_map(QWidget *parent): QWidget(parent)
{
  setParent(parent);
  radius = 0;
  wdth = get_citydlg_canvas_width();
  height = get_citydlg_canvas_height();
  cutted_width = wdth;
  cutted_height = height;
  view = qtg_canvas_create(wdth, height);
  view->map_pixmap.fill(Qt::black);
  miniview = qtg_canvas_create(0, 0);
  miniview->map_pixmap.fill(Qt::black);
  delta_x = 0;
  delta_y = 0;
  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, SIGNAL(customContextMenuRequested(const QPoint &)),
          this, SLOT(context_menu(const QPoint &)));
}

/************************************************************************//**
  Destructor for city map
****************************************************************************/
city_map::~city_map()
{
  qtg_canvas_free(view);
  qtg_canvas_free(miniview);
}

/************************************************************************//**
  Redraws whole view in city_map
****************************************************************************/
void city_map::paintEvent(QPaintEvent *event)
{
  QPainter painter;
  QString str;

  painter.begin(this);
  painter.drawPixmap(0, 0, zoomed_pixmap);

  if (cma_is_city_under_agent(mcity, nullptr)) {
    painter.fillRect(0, 0, zoomed_pixmap.width(), zoomed_pixmap.height(),
                     QBrush(QColor(60, 60 , 60 , 110)));
    painter.setPen(QColor(255, 255, 255));
    // TRANS: %1 is a custom string chosen by player.
    str = QString(_("Governor %1"))
          .arg(cmafec_get_short_descr_of_city(mcity));
    painter.drawText(5, zoomed_pixmap.height() - 10, str);
  }

  painter.end();
}

/************************************************************************//**
  Calls function to put pixmap on view ( it doesn't draw on screen )
****************************************************************************/
void city_map::set_pixmap(struct city *pcity, float z)
{
  int r, max_r;
  QSize size;

  zoom = z;
  r = sqrt(city_map_radius_sq_get(pcity));

  if (radius != r) {
    max_r = sqrt(rs_max_city_radius_sq());
    radius = r;
    qtg_canvas_free(miniview);
    cutted_width = wdth * (r + 1) / max_r;
    cutted_height = height * (r + 1) / max_r;
    cutted_width = qMin(cutted_width, wdth);
    cutted_height = qMin(cutted_height, height);
    delta_x = (wdth - cutted_width) / 2;
    delta_y = (height - cutted_height) / 2;
    miniview = qtg_canvas_create(cutted_width, cutted_height);
    miniview->map_pixmap.fill(Qt::black);
  }

  city_dialog_redraw_map(pcity, view);
  qtg_canvas_copy(miniview, view, delta_x, delta_y,
                  0, 0, cutted_width, cutted_height);
  size = miniview->map_pixmap.size();
  zoomed_pixmap = miniview->map_pixmap.scaled(size * zoom,
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);
  setFixedSize(zoomed_pixmap.size());
  mcity = pcity;
}

/************************************************************************//**
  Size hint for city map
****************************************************************************/
QSize city_map::sizeHint() const
{
  return zoomed_pixmap.size();
}

/************************************************************************//**
  Minimum size hint for city map
****************************************************************************/
QSize city_map::minimumSizeHint() const
{
  return zoomed_pixmap.size();
}

/************************************************************************//**
  Used to change workers on view
****************************************************************************/
void city_map::mousePressEvent(QMouseEvent *event)
{
  int canvas_x, canvas_y, city_x, city_y;
  QPoint pos;

  if (!can_client_issue_orders() || event->button() != Qt::LeftButton) {
    return;
  }

  pos = event->pos();
  canvas_x = pos.x() / zoom + delta_x;
  canvas_y = pos.y() / zoom + delta_y;

  if (canvas_to_city_pos(&city_x, &city_y, city_map_radius_sq_get(mcity),
                         canvas_x, canvas_y)) {
    city_toggle_worker(mcity, city_x, city_y);
  }
}

/************************************************************************//**
  Context menu for setting worker tasks
****************************************************************************/
void city_map::context_menu(QPoint point)
{
  int canvas_x, canvas_y, city_x, city_y;
  QAction *con_cultivate = nullptr;
  QAction *con_irrig = nullptr;
  QAction *con_plant = nullptr;
  QAction *con_mine = nullptr;
  QAction *con_road = nullptr;
  QAction *con_trfrm = nullptr;
  QAction *con_clean = nullptr;
  QAction *con_clear = nullptr;
  QMenu *con_menu;
  QWidgetAction *wid_act;
  struct terrain *pterr;
  struct tile *ptile;
  struct universal for_terr;
  struct worker_task *ptask;
  int city_id = mcity->id;

  if (!can_client_issue_orders()) {
    return;
  }

  canvas_x = point.x() / zoom + delta_x;
  canvas_y = point.y() / zoom + delta_y;

  if (!canvas_to_city_pos(&city_x, &city_y, city_map_radius_sq_get(mcity),
                          canvas_x, canvas_y)) {
    return;
  }

  ptile = city_map_to_tile(&(wld.map), mcity->tile, city_map_radius_sq_get(mcity),
                           city_x, city_y);
  pterr = tile_terrain(ptile);
  for_terr.kind = VUT_TERRAIN;
  for_terr.value.terrain = pterr;
  ptask = worker_task_list_get(mcity->task_reqs, 0);

  wid_act = new QWidgetAction(this);
  wid_act->setDefaultWidget(new QLabel(_("Autoworker activity:")));

  con_menu = new QMenu(this);
  con_menu->addAction(wid_act);

  if (pterr->plant_result != nullptr
      && action_id_univs_not_blocking(ACTION_PLANT, nullptr, &for_terr)) {
    con_plant = con_menu->addAction(_("Plant"));
  }
  if (action_id_univs_not_blocking(ACTION_MINE, nullptr, &for_terr)) {
    con_mine = con_menu->addAction(Q_("?act:Mine"));
  }

  if (pterr->cultivate_result != nullptr
      && action_id_univs_not_blocking(ACTION_CULTIVATE, nullptr, &for_terr)) {
    con_cultivate = con_menu->addAction(_("Cultivate"));
  }
  if (action_id_univs_not_blocking(ACTION_IRRIGATE, nullptr, &for_terr)) {
    con_irrig = con_menu->addAction(_("Irrigate"));
  }

  if (pterr->transform_result != pterr && pterr->transform_result != nullptr
      && action_id_univs_not_blocking(ACTION_TRANSFORM_TERRAIN,
                                      nullptr, &for_terr)) {
    con_trfrm = con_menu->addAction(_("Transform"));
  }

  if (next_extra_for_tile(ptile, EC_ROAD, city_owner(mcity), nullptr) != nullptr) {
    con_road = con_menu->addAction(_("Road"));
  }

  if (prev_extra_in_tile(ptile, ERM_CLEAN,
                         city_owner(mcity), nullptr) != nullptr) {
    con_clean = con_menu->addAction(_("Clean"));
  }

  if (ptask != nullptr) {
    con_clear = con_menu->addAction(_("Clear"));
  }

  con_menu->setAttribute(Qt::WA_DeleteOnClose);
  connect(con_menu, &QMenu::triggered,
          CAPTURE_DEFAULT_THIS (QAction *act) {
    bool target = false;
    struct packet_worker_task task;

    if (!act) {
      return;
    }

    task.city_id = city_id;

    if (act == con_road) {
      task.activity = ACTIVITY_GEN_ROAD;
      target = TRUE;
    } else if (act == con_mine) {
      task.activity = ACTIVITY_MINE;
      target = TRUE;
    } else if (act == con_plant) {
      task.activity = ACTIVITY_PLANT;
    } else if (act == con_irrig) {
      task.activity = ACTIVITY_IRRIGATE;
      target = TRUE;
    } else if (act == con_cultivate) {
      task.activity = ACTIVITY_CULTIVATE;
    } else if (act == con_trfrm) {
      task.activity = ACTIVITY_TRANSFORM;
    } else if (act == con_clean) {
      task.activity = ACTIVITY_CLEAN;
      target = TRUE;
    } else if (act == con_clear) {
      task.activity = ACTIVITY_LAST;
    } else {
      // Closed dialog without selecting any activity entry.
      return;
    }

    task.want = 100;

    if (target) {
      enum extra_cause cause = activity_to_extra_cause(task.activity);
      enum extra_rmcause rmcause = activity_to_extra_rmcause(task.activity);
      struct extra_type *tgt;

      if (cause != EC_NONE) {
        tgt = next_extra_for_tile(ptile, cause, city_owner(mcity), nullptr);
      } else if (rmcause != ERM_NONE) {
        tgt = prev_extra_in_tile(ptile, rmcause, city_owner(mcity), nullptr);
      } else {
        tgt = nullptr;
      }

      if (tgt != nullptr) {
        task.tgt = extra_index(tgt);
      } else {
        task.tgt = -1;
      }
    } else {
      task.tgt = -1;
    }

    task.tile_id = ptile->index;
    send_packet_worker_task(&client.conn, &task);
  });

  con_menu->popup(mapToGlobal(point));
}

/************************************************************************//**
  Constructor for city_dialog, sets layouts, policies ...
****************************************************************************/
city_dialog::city_dialog(QWidget *parent): qfc_dialog(parent)
{
  QFont f = QApplication::font();
  QFont *small_font;
  QFontMetrics fm(f);
  QGridLayout *gridl, *slider_grid;
  QGroupBox *group_box, *map_box, *prod_options,
            *qgbox, *qgbprod, *qsliderbox, *result_box;
  QHBoxLayout *hbox, *hbox_layout, *prod_option_layout,
              *work_but_layout;
  QHeaderView *header;
  QLabel *lab2, *label, *ql, *some_label;
  QPushButton *qpush2;
  QScrollArea *scroll, *scroll2, *scroll3, *scroll_info, *scroll_unit;
  QSizePolicy size_expanding_policy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  QSlider *slider;
  QStringList info_list, str_list;
  QVBoxLayout *lefttop_layout, *units_layout, *worklist_layout,
              *right_layout, *vbox, *vbox_layout, *zoom_vbox, *v_layout;
  QWidget *split_widget1, *split_widget2, *info_wdg, *curr_unit_wdg,
          *supp_unit_wdg, *curr_impr_wdg;
  int list_size;
  int h = 2 * fm.height() + 2;

  current_tab = common;
  small_font = fc_font::instance()->get_font(fonts::notify_label);
  zoom = 1.0;

  central_splitter = new QSplitter;
  central_splitter->setOpaqueResize(false);
  central_left_splitter = new QSplitter;
  central_left_splitter->setOpaqueResize(false);
  prod_unit_splitter = new QSplitter;
  prod_unit_splitter->setOpaqueResize(false);

  setMouseTracking(true);
  selected_row_p = -1;
  dlgcity = nullptr;
  lcity_name = new QPushButton(this);
  lcity_name->setToolTip(_("Click to change city name"));

  single_page_layout = new QHBoxLayout();
  single_page_layout->setContentsMargins(0, 0 ,0 ,0);
  size_expanding_policy.setHorizontalStretch(0);
  size_expanding_policy.setVerticalStretch(0);
  setSizePolicy(size_expanding_policy);
  current_building = 0;

  // Map view
  map_box = new QGroupBox(this);

  // City information widget texts about surpluses and so on
  info_wdg = new QWidget(this);

  // Fill info_wdg with labels
  info_grid_layout = new QGridLayout(parent);

  info_wdg->setFont(*small_font);
  info_grid_layout->setSpacing(0);
  info_grid_layout->setContentsMargins(0, 0, 0, 0);


  for (enum city_info info_field = city_info_begin();
      info_field != city_info_end();
      info_field = city_info_next(info_field)) {

    ql = new QLabel(_(city_info_name(info_field)), info_wdg);
    ql->setFont(*small_font);
    ql->setProperty(fonts::notify_label, "true");
    info_grid_layout->addWidget(ql, info_field, 0);
    qlt[info_field] = new QLabel(info_wdg);
    qlt[info_field]->setFont(*small_font);
    qlt[info_field]->setProperty(fonts::notify_label, "true");
    info_grid_layout->addWidget(qlt[info_field], info_field, 1);
    info_grid_layout->setRowStretch(info_field, 0);
  }

  info_wdg->setLayout(info_grid_layout);

  // Buy button
  buy_button = new QPushButton();
  buy_button->setIcon(fc_icons::instance()->get_icon("help-donate"));
  connect(buy_button, &QAbstractButton::clicked, this, &city_dialog::buy);

  connect(lcity_name, &QAbstractButton::clicked, this, &city_dialog::city_rename);
  citizens_label = new city_label(FEELING_FINAL, this);
  citizen_pixmap = nullptr;
  view = new city_map(this);

  zoom_vbox = new QVBoxLayout();
  zoom_in_button = new QPushButton();
  zoom_in_button->setIcon(fc_icons::instance()->get_icon("plus"));
  zoom_in_button->setIconSize(QSize(16, 16));
  zoom_in_button->setFixedSize(QSize(20, 20));
  zoom_in_button->setToolTip(_("Zoom in"));
  connect(zoom_in_button, &QAbstractButton::clicked, this, &city_dialog::zoom_in);
  zoom_out_button = new QPushButton();
  zoom_out_button->setIcon(fc_icons::instance()->get_icon("minus"));
  zoom_out_button->setIconSize(QSize(16, 16));
  zoom_out_button->setFixedSize(QSize(20, 20));
  zoom_out_button->setToolTip(_("Zoom out"));
  connect(zoom_out_button, &QAbstractButton::clicked, this, &city_dialog::zoom_out);
  zoom_vbox->addWidget(zoom_in_button);
  zoom_vbox->addWidget(zoom_out_button);

  // City map group box
  vbox_layout = new QVBoxLayout;
  hbox_layout = new QHBoxLayout;
  hbox_layout->addStretch(100);
  hbox_layout->addWidget(view);
  hbox_layout->addStretch(100);
  hbox_layout->addLayout(zoom_vbox);
  vbox_layout->addLayout(hbox_layout);
  vbox_layout->addWidget(lcity_name);
  map_box->setLayout(vbox_layout);
  map_box->setTitle(_("City map"));

  // Current/supported units/improvements widgets
  supp_units = new QLabel();
  curr_units = new QLabel();
  curr_impr = new QLabel();
  curr_units->setAlignment(Qt::AlignLeft);
  curr_impr->setAlignment(Qt::AlignLeft);
  supp_units->setAlignment(Qt::AlignLeft);
  supported_units = new unit_info(true);
  scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setMaximumHeight(tileset_unit_with_upkeep_height(get_tileset()) + 6
                           + scroll->horizontalScrollBar()->height());
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(supported_units);
  current_units = new unit_info(false);
  scroll2 = new QScrollArea;
  scroll2->setWidgetResizable(true);
  scroll2->setMaximumHeight(tileset_unit_height(get_tileset()) + 6
                            + scroll2->horizontalScrollBar()->height());
  scroll2->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll2->setWidget(current_units);
  scroll_height = scroll2->horizontalScrollBar()->height();
  city_buildings = new impr_info(this);
  scroll3 = new QScrollArea;
  scroll3->setWidgetResizable(true);
  scroll3->setMaximumHeight(tileset_unit_height(tileset) + 6
                            + scroll3->horizontalScrollBar()->height());
  scroll3->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll3->setWidget(city_buildings);
  scroll->setProperty("city_scroll", true);
  scroll2->setProperty("city_scroll", true);
  scroll3->setProperty("city_scroll", true);

  lefttop_layout = new QVBoxLayout();
  worklist_layout = new QVBoxLayout();
  right_layout = new QVBoxLayout();
  leftbot_layout = new QHBoxLayout();
  units_layout = new QVBoxLayout();
  left_layout = new QVBoxLayout();

  /* Checkboxes to show units/wonders/imrovements
   * on production list */
  prod_option_layout = new QHBoxLayout;
  show_buildings = new QCheckBox;
  show_buildings->setToolTip(_("Show buildings"));
  show_buildings->setChecked(true);
  label = new QLabel();
  label->setPixmap(*fc_icons::instance()->get_pixmap("building"));
  label->setToolTip(_("Show buildings"));
  prod_option_layout->addWidget(show_buildings, Qt::AlignLeft);
  prod_option_layout->addWidget(label, Qt::AlignLeft);
  prod_option_layout->addStretch(100);
  label = new QLabel();
  label->setPixmap(*fc_icons::instance()->get_pixmap("cunits"));
  label->setToolTip(_("Show units"));
  show_units = new QCheckBox;
  show_units->setToolTip(_("Show units"));
  show_units->setChecked(true);
  prod_option_layout->addWidget(show_units, Qt::AlignHCenter);
  prod_option_layout->addWidget(label, Qt::AlignHCenter);
  prod_option_layout->addStretch(100);
  label = new QLabel();
  label->setPixmap(*fc_icons::instance()->get_pixmap("wonder"));
  label->setToolTip(_("Show wonders"));
  show_wonders = new QCheckBox;
  show_wonders->setToolTip(_("Show wonders"));
  show_wonders->setChecked(true);
  prod_option_layout->addWidget(show_wonders);
  prod_option_layout->addWidget(label);
  prod_option_layout->addStretch(100);
  label = new QLabel();
  label->setPixmap(*fc_icons::instance()->get_pixmap("future"));
  label->setToolTip(_("Show future targets"));
  future_targets = new QCheckBox;
  future_targets->setToolTip(_("Show future targets"));
  future_targets->setChecked(false);
  prod_option_layout->addWidget(future_targets);
  prod_option_layout->addWidget(label, Qt::AlignRight);
  prod_options = new QGroupBox(this);
  prod_options->setLayout(prod_option_layout);
  prod_options->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

  // Prev/next and close buttons
  button = new QPushButton;
  button->setIcon(fc_icons::instance()->get_icon("city-close"));
  button->setIconSize(QSize(56, 56));
  button->setToolTip(_("Close city dialog"));
  connect(button, &QAbstractButton::clicked, this, &QWidget::hide);

  next_city_but = new QPushButton();
  next_city_but->setIcon(fc_icons::instance()->get_icon("city-right"));
  next_city_but->setIconSize(QSize(56, 56));
  next_city_but->setToolTip(_("Show next city"));
  connect(next_city_but, &QAbstractButton::clicked, this, &city_dialog::next_city);

  prev_city_but = new QPushButton();
  connect(prev_city_but, &QAbstractButton::clicked, this, &city_dialog::prev_city);
  prev_city_but->setIcon(fc_icons::instance()->get_icon("city-left"));
  prev_city_but->setIconSize(QSize(56, 56));
  prev_city_but->setToolTip(_("Show previous city"));

  happiness_button = new QPushButton();
  happiness_button->setIcon(fc_icons::instance()->get_icon("city-switch"));
  happiness_button->setIconSize(QSize(56, 28));
  connect(happiness_button, &QAbstractButton::clicked, this, &city_dialog::show_happiness);

  button->setFixedSize(64, 64);
  prev_city_but->setFixedSize(64, 64);
  next_city_but->setFixedSize(64, 64);
  happiness_button->setFixedSize(64, 32);
  vbox_layout = new QVBoxLayout;
  vbox_layout->addWidget(prev_city_but);
  vbox_layout->addWidget(next_city_but);
  vbox_layout->addWidget(button);
  vbox_layout->addWidget(happiness_button, Qt::AlignHCenter);

  counterss_button = new QPushButton();
  connect(counterss_button, &QAbstractButton::clicked, this, &city_dialog::show_counters);
  vbox_layout->addWidget(counterss_button, Qt::AlignHCenter);

  update_tabs();
  hbox_layout = new QHBoxLayout;

  hbox_layout->addLayout(vbox_layout, Qt::AlignLeft);
  hbox_layout->addWidget(info_wdg, Qt::AlignLeft);
  hbox_layout->addWidget(map_box, Qt::AlignCenter);

  // Layout with city view and buttons
  lefttop_layout->addWidget(citizens_label, Qt::AlignHCenter);
  lefttop_layout->addStretch(0);
  lefttop_layout->addLayout(hbox_layout);
  lefttop_layout->addStretch(50);

  // Layout for units/buildings
  curr_unit_wdg = new QWidget();
  supp_unit_wdg = new QWidget();
  curr_impr_wdg = new QWidget();
  v_layout = new QVBoxLayout;
  v_layout->addWidget(curr_impr);
  v_layout->addWidget(scroll3);
  v_layout->setContentsMargins(0 , 0 , 0, 0);
  v_layout->setSpacing(0);
  curr_impr_wdg->setLayout(v_layout);
  v_layout = new QVBoxLayout;
  v_layout->addWidget(curr_units);
  v_layout->addWidget(scroll2);
  v_layout->setContentsMargins(0 , 0 , 0, 0);
  v_layout->setSpacing(0);
  curr_unit_wdg->setLayout(v_layout);
  v_layout = new QVBoxLayout;
  v_layout->addWidget(supp_units);
  v_layout->addWidget(scroll);
  v_layout->setContentsMargins(0 , 0 , 0, 0);
  v_layout->setSpacing(0);
  supp_unit_wdg->setLayout(v_layout);

  units_layout->addWidget(curr_unit_wdg);
  units_layout->addWidget(supp_unit_wdg);
  units_layout->addWidget(curr_impr_wdg);
  units_layout->setSpacing(0);
  units_layout->setContentsMargins(0 , 0 , 0, 0);

  vbox = new QVBoxLayout;
  vbox_layout = new QVBoxLayout;
  qgbprod = new QGroupBox;
  group_box = new QGroupBox(_("Worklist Option"));
  work_but_layout = new QHBoxLayout;
  work_next_but = new QPushButton(fc_icons::instance()->get_icon(
                                    "go-down"), "");
  work_prev_but = new QPushButton(fc_icons::instance()->get_icon(
                                    "go-up"), "");
  work_add_but = new QPushButton(fc_icons::instance()->get_icon(
                                   "list-add"), "");
  work_rem_but = new QPushButton(style()->standardIcon(
                                   QStyle::SP_DialogDiscardButton), "");
  work_but_layout->addWidget(work_add_but);
  work_but_layout->addWidget(work_next_but);
  work_but_layout->addWidget(work_prev_but);
  work_but_layout->addWidget(work_rem_but);
  production_combo_p = new progress_bar(parent);
  production_combo_p->setToolTip(_("Click to change current production"));
  p_table_p = new QTableWidget;

  r1 = new QRadioButton(_("Change"));
  r2 = new QRadioButton(_("Insert Before"));
  r3 = new QRadioButton(_("Insert After"));
  r4 = new QRadioButton(_("Add Last"));
  r4->setChecked(true);
  group_box->setLayout(vbox);


  p_table_p->setColumnCount(3);
  p_table_p->setProperty("showGrid", "false");
  p_table_p->setProperty("selectionBehavior", "SelectRows");
  p_table_p->setEditTriggers(QAbstractItemView::NoEditTriggers);
  p_table_p->verticalHeader()->setVisible(false);
  p_table_p->horizontalHeader()->setVisible(false);
  p_table_p->setSelectionMode(QAbstractItemView::SingleSelection);
  production_combo_p->setFixedHeight(h);
  p_table_p->setMinimumWidth(200);
  p_table_p->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContentsOnFirstShow);
  p_table_p->setContextMenuPolicy(Qt::CustomContextMenu);
  p_table_p->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
  header = p_table_p->horizontalHeader();
  header->setStretchLastSection(true);

  qgbprod->setTitle(_("Worklist"));
  vbox_layout->setSpacing(0);
  vbox_layout->addWidget(prod_options);
  vbox_layout->addWidget(buy_button);
  vbox_layout->addWidget(production_combo_p);
  vbox_layout->addLayout(work_but_layout);
  vbox_layout->addWidget(p_table_p);
  qgbprod->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  qgbprod->setLayout(vbox_layout);

  worklist_layout->setSpacing(0);
  worklist_layout->addWidget(qgbprod);
  connect(p_table_p,
          &QWidget::customContextMenuRequested, this,
          &city_dialog::display_worklist_menu);
  connect(production_combo_p, &progress_bar::clicked, this, &city_dialog::show_targets);
  connect(work_add_but, &QAbstractButton::clicked, this, &city_dialog::show_targets_worklist);
  connect(work_prev_but, &QAbstractButton::clicked, this, &city_dialog::worklist_up);
  connect(work_next_but, &QAbstractButton::clicked, this, &city_dialog::worklist_down);
  connect(work_rem_but, &QAbstractButton::clicked, this, &city_dialog::worklist_del);
  connect(p_table_p,
          &QTableWidget::itemDoubleClicked,
          this, &city_dialog::dbl_click_p);
  connect(p_table_p->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(item_selected(const QItemSelection &,
                             const QItemSelection &)));
  happiness_group = new QGroupBox(_("Happiness"));
  gridl = new QGridLayout;

  nationality_table = new QTableWidget;
  nationality_table->setColumnCount(3);
  nationality_table->setProperty("showGrid", "false");
  nationality_table->setProperty("selectionBehavior", "SelectRows");
  nationality_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  nationality_table->verticalHeader()->setVisible(false);
  nationality_table->horizontalHeader()->setStretchLastSection(true);

  info_list.clear();
  info_list << _("Cities:") << _("Luxuries:") << _("Buildings:")
            << _("Nationality:") << _("Units:") <<  _("Wonders:");

  for (int i = 0; i < info_list.count(); i++) {
    lab_table[i] = new city_label(1 + i, this);
    gridl->addWidget(lab_table[i], i, 1, 1, 1);
    lab2 = new QLabel(this);
    lab2->setFont(*small_font);
    lab2->setProperty(fonts::notify_label, "true");
    lab2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lab2->setText(info_list.at(i));
    gridl->addWidget(lab2, i, 0, 1, 1);
  }

  gridl->setSpacing(0);
  happiness_group->setLayout(gridl);


  happiness_layout = new QHBoxLayout;
  happiness_layout->addWidget(happiness_group);
  happiness_layout->addWidget(nationality_table);
  happiness_layout->setStretch(0, 10);
  happiness_widget = new QWidget();
  happiness_widget->setLayout(happiness_layout);
  qgbox = new QGroupBox(_("Presets:"));
  qsliderbox = new QGroupBox(_("Governor settings"));
  result_box = new QGroupBox(_("Results:"));
  hbox = new QHBoxLayout;
  gridl = new QGridLayout;
  slider_grid = new QGridLayout;

  counterss_layout = new QVBoxLayout();
  counterss_frame = new QFrame();
  counterss_frame->setLayout(counterss_layout);

  qpush2
    = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton),
                      _("Save"));
  connect(qpush2, &QAbstractButton::pressed, this, &city_dialog::save_cma);

  cma_info_text = new QLabel;
  cma_info_text->setFont(*small_font);
  cma_info_text->setAlignment(Qt::AlignCenter);
  cma_table = new QTableWidget;
  cma_table->setColumnCount(1);
  cma_table->setProperty("showGrid", "false");
  cma_table->setProperty("selectionBehavior", "SelectRows");
  cma_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  cma_table->setSelectionMode(QAbstractItemView::SingleSelection);
  cma_table->setContextMenuPolicy(Qt::CustomContextMenu);
  cma_table->verticalHeader()->setVisible(false);
  cma_table->horizontalHeader()->setVisible(false);
  cma_table->horizontalHeader()->setSectionResizeMode(
    QHeaderView::Stretch);

  connect(cma_table->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(cma_selected(const QItemSelection &,
                            const QItemSelection &)));
  connect(cma_table,
          &QWidget::customContextMenuRequested, this,
          &city_dialog::cma_context_menu);
  connect(cma_table, &QTableWidget::cellDoubleClicked, this,
          &city_dialog::cma_double_clicked);
  gridl->addWidget(cma_table, 0, 0, 1, 2);
  qgbox->setLayout(gridl);
  hbox->addWidget(cma_info_text);
  result_box->setLayout(hbox);
  str_list << _("Food") << _("Shield") << _("Trade") << _("Gold")
           << _("Luxury") << _("Science") << _("Celebrate")
           << _("Maximize growth");
  some_label = new QLabel(_("Minimal Surplus"));
  some_label->setFont(*fc_font::instance()->get_font(fonts::notify_label));
  some_label->setAlignment(Qt::AlignRight);
  slider_grid->addWidget(some_label, 0, 0, 1, 3);
  some_label = new QLabel(_("Priority"));
  some_label->setFont(*fc_font::instance()->get_font(fonts::notify_label));
  some_label->setAlignment(Qt::AlignCenter);
  slider_grid->addWidget(some_label, 0, 3, 1, 2);

  list_size = str_list.count();
  for (int i = 0; i < list_size; i++) {
    some_label = new QLabel(str_list.at(i));
    slider_grid->addWidget(some_label, i + 1, 0, 1, 1);
    some_label = new QLabel("0");
    some_label->setMinimumWidth(25);

    if (i < list_size - 2) {
      slider = new QSlider(Qt::Horizontal);
      slider->setPageStep(1);
      slider->setFocusPolicy(Qt::TabFocus);
      slider_tab[2 * i] = slider;
      slider->setRange(-20, 20);
      slider->setSingleStep(1);
      slider_grid->addWidget(some_label, i + 1, 1, 1, 1);
      slider_grid->addWidget(slider, i + 1, 2, 1, 1);
      slider->setProperty("FC", QVariant::fromValue((void *)some_label));

      connect(slider, &QAbstractSlider::valueChanged, this, &city_dialog::cma_slider);
    } else if (i == list_size - 2) {
      cma_celeb_checkbox = new QCheckBox;
      slider_grid->addWidget(cma_celeb_checkbox, i + 1, 2 , 1 , 1);
#ifdef FC_QT6X_MODE
      // Qt-6.7
      connect(cma_celeb_checkbox,
              &QCheckBox::checkStateChanged, this, &city_dialog::cma_toggle_changed);
#else  // FC_QT6X_MODE
      connect(cma_celeb_checkbox,
              &QCheckBox::stateChanged, this, &city_dialog::cma_toggle_changed_depr);
#endif // FC_QT6X_MODE
    } else {
      fc_assert(i == list_size - 1);

      cma_max_growth = new QCheckBox;
      slider_grid->addWidget(cma_max_growth, i + 1, 2 , 1 , 1);
#ifdef FC_QT6X_MODE
      // Qt-6.7
      connect(cma_max_growth,
              &QCheckBox::checkStateChanged, this, &city_dialog::cma_toggle_changed);
#else  // FC_QT6X_MODE
      connect(cma_max_growth,
              &QCheckBox::stateChanged, this, &city_dialog::cma_toggle_changed_depr);
#endif // FC_QT6X_MODE
    }

    if (i <= list_size - 2) {
      some_label = new QLabel("0");
      some_label->setMinimumWidth(25);
      slider = new QSlider(Qt::Horizontal);
      slider->setFocusPolicy(Qt::TabFocus);
      slider->setRange(0, 25);
      slider_tab[2 * i + 1] = slider;
      slider->setProperty("FC", QVariant::fromValue((void *)some_label));
      slider_grid->addWidget(some_label, i + 1, 3, 1, 1);
      slider_grid->addWidget(slider, i + 1, 4, 1, 1);
      connect(slider, &QAbstractSlider::valueChanged, this, &city_dialog::cma_slider);
    }
  }

  cma_enable_but = new QPushButton();
  cma_enable_but->setFocusPolicy(Qt::TabFocus);
  connect(cma_enable_but, &QAbstractButton::pressed, this, &city_dialog::cma_enable);
  slider_grid->addWidget(cma_enable_but, O_LAST + 4, 0, 1, 3);
  slider_grid->addWidget(qpush2, O_LAST + 4, 3, 1, 2);

  qsliderbox->setLayout(slider_grid);
  cma_result = new QLabel;
  cma_result_pix = new QLabel;

  hbox = new QHBoxLayout;
  QScrollArea *govA = new QScrollArea();
  hbox->addWidget(cma_result_pix);
  hbox->addWidget(cma_result);
  hbox->addStretch(10);
  govA->setWidget(qsliderbox);
  govA->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
  right_layout->addWidget(qgbox);
  right_layout->addLayout(hbox);
  right_layout->addWidget(govA);

  split_widget1 = new QWidget;
  split_widget1->setLayout(worklist_layout);
  split_widget2 = new QWidget;
  split_widget2->setLayout(units_layout);
  prod_unit_splitter->addWidget(split_widget1);
  prod_unit_splitter->addWidget(split_widget2);
  prod_unit_splitter->setStretchFactor(0, 3);
  prod_unit_splitter->setStretchFactor(1, 97);
  prod_unit_splitter->setOrientation(Qt::Horizontal);
  leftbot_layout->addWidget(prod_unit_splitter);
  top_widget = new QWidget;
  top_widget->setLayout(lefttop_layout);
  top_widget->setSizePolicy(QSizePolicy::Minimum,
                            QSizePolicy::Minimum);
  scroll_info = new QScrollArea();
  scroll_info->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
  scroll_unit = new QScrollArea();
  scroll_info->setWidget(top_widget);
  scroll_info->setWidgetResizable(true);
  prod_happ_widget = new QWidget;
  prod_happ_widget->setLayout(leftbot_layout);
  prod_happ_widget->setSizePolicy(QSizePolicy::MinimumExpanding,
                                  QSizePolicy::MinimumExpanding);
  scroll_unit->setWidget(prod_happ_widget);
  scroll_unit->setWidgetResizable(true);
  central_left_splitter->addWidget(scroll_info);
  central_left_splitter->addWidget(scroll_unit);
  central_left_splitter->setStretchFactor(0, 40);
  central_left_splitter->setStretchFactor(1, 60);
  central_left_splitter->setOrientation(Qt::Vertical);
  left_layout->addWidget(central_left_splitter);

  split_widget1 = new QWidget(this);
  split_widget2 = new QWidget(this);
  split_widget1->setLayout(left_layout);
  split_widget2->setLayout(right_layout);
  central_splitter->addWidget(split_widget1);
  central_splitter->addWidget(split_widget2);
  central_splitter->setStretchFactor(0, 99);
  central_splitter->setStretchFactor(1, 1);
  central_splitter->setOrientation(Qt::Horizontal);
  single_page_layout->addWidget(central_splitter);
  setSizeGripEnabled(true);
  setLayout(single_page_layout);

  installEventFilter(this);

  ::city_dlg_created = true;
}

/************************************************************************//**
  Changes production to next one or previous
****************************************************************************/
void city_dialog::change_production(bool next)
{

  cid cprod;
  int i, pos;
  int item, targets_used;
  QList<cid> prod_list;
  QString str;
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct universal univ;

  pos = 0;
  cprod = cid_encode(dlgcity->production);
  targets_used = collect_eventually_buildable_targets(targets, dlgcity, false);
  name_and_sort_items(targets, targets_used, items, false, dlgcity);

  for (item = 0; item < targets_used; item++) {
    if (can_city_build_now(&(wld.map), dlgcity, &items[item].item)) {
      prod_list << cid_encode(items[item].item);
    }
  }

  for (i = 0; i < prod_list.size(); i++) {
    if (prod_list.at(i) == cprod) {
      if (next) {
        pos = i + 1;
      } else {
        pos = i - 1;
      }
    }
  }
  if (pos == prod_list.size()) {
    pos = 0;
  }
  if (pos == - 1) {
    pos = prod_list.size() - 1;
  }
  univ = cid_decode(static_cast<cid>(prod_list.at(pos)));
  city_change_production(dlgcity, &univ);
}

/************************************************************************//**
  Sets tooltip for happiness/pruction button switcher
****************************************************************************/
void city_dialog::update_tabs()
{
  if (current_tab == happiness) {
    happiness_button->setToolTip(_("Show city production"));
    counterss_button->setToolTip(_("Show counters information"));

  } else {
    happiness_button->setToolTip(_("Show happiness information"));

    if (current_tab == counters) {
      counterss_button->setToolTip(_("Show city production"));
    }
    else {
      counterss_button->setToolTip(_("Show counters information"));
    }
  }
}

/************************************************************************//**
  Shows happiness tab
****************************************************************************/
void city_dialog::show_happiness()
{
  QWidget *active_tab = current_tab == counters ? (QWidget*) counterss_frame : prod_unit_splitter;
  setUpdatesEnabled(false);


  if (current_tab != happiness) {
    leftbot_layout->replaceWidget(active_tab,
                                  happiness_widget,
                                  Qt::FindDirectChildrenOnly);
    active_tab->hide();
    happiness_widget->show();
    happiness_widget->updateGeometry();
    current_tab = happiness;
  } else {
    /* We do not change prod_unit_splitter to current_tab,
     * because happiness is active tab and clicked -
     * switching to default */
    leftbot_layout->replaceWidget(happiness_widget,
                                  prod_unit_splitter,
                                  Qt::FindDirectChildrenOnly);
    prod_unit_splitter->show();
    prod_unit_splitter->updateGeometry();
    happiness_widget->hide();
    current_tab = common;
  }

  setUpdatesEnabled(true);
  update();
  update_tabs();
}

/************************************************************************//**
  Shows counters tab
****************************************************************************/
void city_dialog::show_counters()
{
  QWidget *active_tab = current_tab == happiness ? happiness_widget : prod_unit_splitter;
  setUpdatesEnabled(false);


  if (current_tab != counters) {
    leftbot_layout->replaceWidget(active_tab,
                                  counterss_frame,
                                  Qt::FindDirectChildrenOnly);
    active_tab->hide();
    counterss_frame->show();
    counterss_frame->updateGeometry();
    current_tab = counters;
  } else {
    /* We do not change prod_unit_splitter to current_tab,
     * because counters is active tab and clicked -
     * switching to default */
    leftbot_layout->replaceWidget(counterss_frame,
                                  prod_unit_splitter,
                                  Qt::FindDirectChildrenOnly);
    prod_unit_splitter->show();
    prod_unit_splitter->updateGeometry();
    counterss_frame->hide();
    current_tab = common;
  }

  setUpdatesEnabled(true);
  update();
  update_tabs();
}

/************************************************************************//**
  Updates buttons/widgets which should be enabled/disabled
****************************************************************************/
void city_dialog::update_disabled()
{
  struct player *pplayer = client_player();

  if (pplayer == nullptr
      || city_owner(dlgcity) != pplayer) {
    prev_city_but->setDisabled(true);
    next_city_but->setDisabled(true);
    buy_button->setDisabled(true);
    cma_enable_but->setDisabled(true);
    production_combo_p->setDisabled(true);
    current_units->setDisabled(true);
    supported_units->setDisabled(true);
    view->setDisabled(true);
  } else {
    prev_city_but->setEnabled(true);
    next_city_but->setEnabled(true);
    buy_button->setEnabled(true);
    cma_enable_but->setEnabled(true);
    production_combo_p->setEnabled(true);
    current_units->setEnabled(true);
    supported_units->setEnabled(true);
    view->setEnabled(true);
  }

  if (can_client_issue_orders()) {
    cma_enable_but->setEnabled(true);
  } else  {
    cma_enable_but->setDisabled(true);
  }

  update_prod_buttons();
}

/************************************************************************//**
  Update sensitivity of buttons in production tab
****************************************************************************/
void city_dialog::update_prod_buttons()
{
  struct player *pplayer = client_player();

  work_next_but->setDisabled(true);
  work_prev_but->setDisabled(true);
  work_add_but->setDisabled(true);
  work_rem_but->setDisabled(true);

  if (pplayer != nullptr
      && city_owner(dlgcity) == pplayer) {
    work_add_but->setEnabled(true);

    if (selected_row_p >= 0 && selected_row_p < p_table_p->rowCount()) {
      work_rem_but->setEnabled(true);
    }

    if (selected_row_p >= 0 && selected_row_p < p_table_p->rowCount() - 1) {
      work_next_but->setEnabled(true);
    }

    if (selected_row_p > 0 && selected_row_p < p_table_p->rowCount()) {
      work_prev_but->setEnabled(true);
    }
  }
}

/************************************************************************//**
  City dialog destructor
****************************************************************************/
city_dialog::~city_dialog()
{
  if (citizen_pixmap) {
    citizen_pixmap->detach();
    delete citizen_pixmap;
  }

  cma_table->clear();
  p_table_p->clear();
  nationality_table->clear();
  current_units->clear_layout();
  supported_units->clear_layout();
  removeEventFilter(this);
  ::city_dlg_created = false;

  // Delete the one widget that currently does NOT have a parent
  if (current_tab == common) {
    delete happiness_widget;
    delete counterss_frame;
  }
  else if (current_tab == counters) {
    delete happiness_widget;
    delete prod_unit_splitter;
  }else {
    delete counterss_frame;
    delete prod_unit_splitter;
  }
}

/************************************************************************//**
  Hide event
****************************************************************************/
void city_dialog::hideEvent(QHideEvent *event)
{
  gui()->qt_settings.city_geometry = saveGeometry();
  gui()->qt_settings.city_splitter1 = prod_unit_splitter->saveState();
  gui()->qt_settings.city_splitter2 = central_left_splitter->saveState();
  gui()->qt_settings.city_splitter3 = central_splitter->saveState();
}

/************************************************************************//**
  Show event
****************************************************************************/
void city_dialog::showEvent(QShowEvent *event)
{
  if (!gui()->qt_settings.city_geometry.isNull()) {
    restoreGeometry(gui()->qt_settings.city_geometry);
    prod_unit_splitter->restoreState(gui()->qt_settings.city_splitter1);
    central_left_splitter->restoreState(gui()->qt_settings.city_splitter2);
    central_splitter->restoreState(gui()->qt_settings.city_splitter3);
  } else {
    QRect rect = QApplication::primaryScreen()->availableGeometry();

    resize((rect.width() * 4) / 5, (rect.height() * 5) / 6);
  }
}

/************************************************************************//**
  Show event
****************************************************************************/
void city_dialog::closeEvent(QCloseEvent *event)
{
  gui()->qt_settings.city_geometry = saveGeometry();
  gui()->qt_settings.city_splitter1 = prod_unit_splitter->saveState();
  gui()->qt_settings.city_splitter2 = central_left_splitter->saveState();
  gui()->qt_settings.city_splitter3 = central_splitter->saveState();
}

/************************************************************************//**
  Event filter for catching keyboard events
****************************************************************************/
bool city_dialog::eventFilter(QObject *obj, QEvent *event)
{

  if (obj == this) {
    if (event->type() == QEvent::KeyPress) {
    }

    if (event->type() == QEvent::ShortcutOverride) {
      QKeyEvent *key_event = static_cast<QKeyEvent *>(event);
      if (key_event->key() == Qt::Key_Right) {
        next_city();
        event->setAccepted(true);
        return true;
      }
      if (key_event->key() == Qt::Key_Left) {
        prev_city();
        event->setAccepted(true);
        return true;
      }
      if (key_event->key() == Qt::Key_Up) {
        change_production(true);
        event->setAccepted(true);
        return true;
      }
      if (key_event->key() == Qt::Key_Down) {
        change_production(false);
        event->setAccepted(true);
        return true;
      }
    }
  }
  return QObject::eventFilter(obj, event);
}

/************************************************************************//**
  City rename dialog input
****************************************************************************/
void city_dialog::city_rename()
{
  hud_input_box *ask;
  const int city_id = dlgcity->id;

  if (!can_client_issue_orders()) {
    return;
  }

  ask = new hud_input_box(gui()->central_wdg);
  ask->set_text_title_definput(_("What should we rename the city to?"),
                               _("Rename City"), city_name_get(dlgcity));
  ask->setAttribute(Qt::WA_DeleteOnClose);
  connect(ask, &hud_message_box::accepted, this, [=]() {
    struct city *pcity = game_city_by_number(city_id);
    QByteArray ask_bytes;

    if (!pcity) {
      return;
    }

    ask_bytes = ask->input_edit.text().toUtf8();
    ::city_rename(pcity, ask_bytes.data());
  });
  ask->show();
}

/************************************************************************//**
  Zooms in tiles view
****************************************************************************/
void city_dialog::zoom_in()
{
  zoom = zoom * 1.2;
  if (dlgcity) {
    view->set_pixmap(dlgcity, zoom);
  }
  updateGeometry();
  left_layout->update();
}

/************************************************************************//**
  Zooms out tiles view
****************************************************************************/
void city_dialog::zoom_out()
{
  zoom = zoom / 1.2;
  if (dlgcity) {
    view->set_pixmap(dlgcity, zoom);
  }
  updateGeometry();
  left_layout->update();
}

/************************************************************************//**
  Save cma dialog input
****************************************************************************/
void city_dialog::save_cma()
{
  hud_input_box *ask = new hud_input_box(gui()->central_wdg);

  ask->set_text_title_definput(_("What should we name the preset?"),
                               _("Name new preset"),
                               _("new preset"));
  ask->setAttribute(Qt::WA_DeleteOnClose);
  connect(ask, &hud_message_box::accepted, this,
          CAPTURE_DEFAULT_THIS () {
    struct cm_parameter param;
    QByteArray ask_bytes = ask->input_edit.text().toUtf8();
    QString text = ask_bytes.data();
    if (!text.isEmpty()) {
      param.allow_disorder = false;
      param.allow_specialists = true;
      param.require_happy = cma_celeb_checkbox->isChecked();
      param.max_growth = cma_max_growth->isChecked();
      param.happy_factor = slider_tab[2 * O_LAST + 1]->value();

      for (int i = O_FOOD; i < O_LAST; i++) {
        param.minimal_surplus[i] = slider_tab[2 * i]->value();
        param.factor[i] = slider_tab[2 * i + 1]->value();
      }

      ask_bytes = text.toUtf8();
      cmafec_preset_add(ask_bytes.data(), &param);
      update_cma_tab();
    }
  });
  ask->show();
}

/************************************************************************//**
  Enables cma slot, triggered by clicked button or changed cma
****************************************************************************/
void city_dialog::cma_enable()
{
  if (cma_is_city_under_agent(dlgcity, nullptr)) {
    cma_release_city(dlgcity);

    return;
  }

  cma_changed();
  update_cma_tab();
}

/************************************************************************//**
  Sliders moved and cma has been changed
****************************************************************************/
void city_dialog::cma_changed()
{
  struct cm_parameter param;

  param.allow_disorder = false;
  param.allow_specialists = true;
  param.require_happy = cma_celeb_checkbox->isChecked();
  param.max_growth = cma_max_growth->isChecked();
  param.happy_factor = slider_tab[2 * O_LAST + 1]->value();

  for (int i = O_FOOD; i < O_LAST; i++) {
    param.minimal_surplus[i] = slider_tab[2 * i]->value();
    param.factor[i] = slider_tab[2 * i + 1]->value();
  }

  cma_put_city_under_agent(dlgcity, &param);
}

/************************************************************************//**
  Double click on some row ( column is unused )
****************************************************************************/
void city_dialog::cma_double_clicked(int row, int column)
{
  const struct cm_parameter *param;

  if (!can_client_issue_orders()) {
    return;
  }
  param = cmafec_preset_get_parameter(row);
  if (cma_is_city_under_agent(dlgcity, nullptr)) {
    cma_release_city(dlgcity);
  }

  cma_put_city_under_agent(dlgcity, param);
  update_cma_tab();
}

/************************************************************************//**
  CMA has been selected from list
****************************************************************************/
void city_dialog::cma_selected(const QItemSelection &sl,
                               const QItemSelection &ds)
{
  const struct cm_parameter *param;
  QModelIndex index;
  QModelIndexList indexes = sl.indexes();

  if (indexes.isEmpty() || cma_table->signalsBlocked()) {
    return;
  }

  index = indexes.at(0);
  int ind = index.row();

  if (cma_table->currentRow() == -1 || cmafec_preset_num() == 0) {
    return;
  }

  param = cmafec_preset_get_parameter(ind);
  update_sliders();

  if (cma_is_city_under_agent(dlgcity, nullptr)) {
    cma_release_city(dlgcity);
    cma_put_city_under_agent(dlgcity, param);
  }
}

/************************************************************************//**
  Updates sliders ( cma params )
****************************************************************************/
void city_dialog::update_sliders()
{
  struct cm_parameter param;
  const struct cm_parameter *cparam;
  int output;
  QVariant qvar;
  QLabel *label;

  if (!cma_is_city_under_agent(dlgcity, &param)) {
    if (cma_table->currentRow() == -1 || cmafec_preset_num() == 0) {
      return;
    }
    cparam = cmafec_preset_get_parameter(cma_table->currentRow());
    cm_copy_parameter(&param, cparam);
  }

  for (output = O_FOOD; output < 2 * O_LAST; output++) {
    slider_tab[output]->blockSignals(true);
  }

  for (output = O_FOOD; output < O_LAST; output++) {
    qvar = slider_tab[2 * output + 1]->property("FC");
    label = reinterpret_cast<QLabel *>(qvar.value<void *>());
    label->setText(QString::number(param.factor[output]));
    slider_tab[2 * output + 1]->setValue(param.factor[output]);
    qvar = slider_tab[2 * output]->property("FC");
    label = reinterpret_cast<QLabel *>(qvar.value<void *>());
    label->setText(QString::number(param.minimal_surplus[output]));
    slider_tab[2 * output]->setValue(param.minimal_surplus[output]);
  }

  slider_tab[2 * O_LAST + 1]->blockSignals(true);
  qvar = slider_tab[2 * O_LAST + 1]->property("FC");
  label = reinterpret_cast<QLabel *>(qvar.value<void *>());
  label->setText(QString::number(param.happy_factor));
  slider_tab[2 * O_LAST + 1]->setValue(param.happy_factor);
  slider_tab[2 * O_LAST + 1]->blockSignals(false);
  cma_celeb_checkbox->blockSignals(true);
  cma_celeb_checkbox->setChecked(param.require_happy);
  cma_celeb_checkbox->blockSignals(false);
  cma_max_growth->blockSignals(true);
  cma_max_growth->setChecked(param.max_growth);
  cma_max_growth->blockSignals(false);

  for (output = O_FOOD; output < 2 * O_LAST; output++) {
    slider_tab[output]->blockSignals(false);
  }
}

/************************************************************************//**
  Updates cma tab
****************************************************************************/
void city_dialog::update_cma_tab()
{
  QString s;
  QTableWidgetItem *item;
  struct cm_parameter param;
  QPixmap pix;
  int i;

  cma_table->clear();
  cma_table->setRowCount(0);

  for (i = 0; i < cmafec_preset_num(); i++) {
    item = new QTableWidgetItem;
    item->setText(cmafec_preset_get_descr(i));
    cma_table->insertRow(i);
    cma_table->setItem(i, 0, item);
  }

  if (cmafec_preset_num() == 0) {
    cma_table->insertRow(0);
    item = new QTableWidgetItem;
    item->setText(_("No governor defined"));
    cma_table->setItem(0, 0, item);
  }

  if (cma_is_city_under_agent(dlgcity, nullptr)) {
    view->update();
    s = QString(cmafec_get_short_descr_of_city(dlgcity));
    pix = style()->standardPixmap(QStyle::SP_DialogApplyButton);
    pix = pix.scaled(2 * pix.width(), 2 * pix.height(),
                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    cma_result_pix->setPixmap(pix);
    cma_result_pix->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // TRANS: %1 is custom string chosen by player
    cma_result->setText(QString(_("<h3>Governor Enabled<br>(%1)</h3>"))
                        .arg(s.toHtmlEscaped()));
    cma_result->setAlignment(Qt::AlignCenter);
  } else {
    pix = style()->standardPixmap(QStyle::SP_DialogCancelButton);
    pix = pix.scaled(1.6 * pix.width(), 1.6 * pix.height(),
                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    cma_result_pix->setPixmap(pix);
    cma_result_pix->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    cma_result->setText(QString(_("<h3>Governor Disabled</h3>")));
    cma_result->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  }

  if (cma_is_city_under_agent(dlgcity, nullptr)) {
    cmafec_get_fe_parameter(dlgcity, &param);
    i = cmafec_preset_get_index_of_parameter(const_cast <struct
                                             cm_parameter *const >(&param));
    if (i >= 0 && i < cma_table->rowCount()) {
      cma_table->blockSignals(true);
      cma_table->setCurrentCell(i, 0);
      cma_table->blockSignals(false);
    }

    cma_enable_but->setText(_("Disable"));
  } else {
    cma_enable_but->setText(_("Enable"));
  }
  update_sliders();
}

/************************************************************************//**
  Removes selected CMA
****************************************************************************/
void city_dialog::cma_remove()
{
  int i;
  hud_message_box *ask;

  i = cma_table->currentRow();

  if (i == -1 || cmafec_preset_num() == 0) {
    return;
  }

  ask = new hud_message_box(city_dlg);
  ask->set_text_title(_("Remove this preset?"), cmafec_preset_get_descr(i));
  ask->setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  ask->setDefaultButton(QMessageBox::Cancel);
  ask->setAttribute(Qt::WA_DeleteOnClose);
  connect(ask, &hud_message_box::accepted, this,
          CAPTURE_DEFAULT_THIS () {
    cmafec_preset_remove(i);
    update_cma_tab();
  });
  ask->show();
}

/************************************************************************//**
  CMA option 'celebrate' or 'max_growth' QCheckBox state has been changed.
****************************************************************************/
void city_dialog::cma_toggle_changed(Qt::CheckState state)
{
  if (cma_is_city_under_agent(dlgcity, nullptr)) {
    cma_changed();
    update_cma_tab();
  }
}

/************************************************************************//**
  CMA option 'celebrate' or 'max_growth' QCheckBox state has been changed.
  This deprecated slot is used on Qt < 6.7.
****************************************************************************/
void city_dialog::cma_toggle_changed_depr(int state)
{
  if (cma_is_city_under_agent(dlgcity, nullptr)) {
    cma_changed();
    update_cma_tab();
  }
}

/************************************************************************//**
  CMA options on slider has been changed
****************************************************************************/
void city_dialog::cma_slider(int value)
{
  QVariant qvar;
  QSlider *slider;
  QLabel *label;

  slider = qobject_cast<QSlider *>(sender());
  qvar = slider->property("FC");

  if (qvar.isNull() || !qvar.isValid()) {
    return;
  }

  label = reinterpret_cast<QLabel *>(qvar.value<void *>());
  label->setText(QString::number(value));

  if (cma_is_city_under_agent(dlgcity, nullptr)) {
    cma_changed();
    update_cma_tab();
  }
}

/************************************************************************//**
  Received signal about changed checkbox - allow disbanding city
****************************************************************************/
void city_dialog::disband_state_changed(bool allow_disband)
{
  bv_city_options new_options;

  BV_CLR_ALL(new_options);

  if (allow_disband) {
    BV_SET(new_options, CITYO_DISBAND);
  } else {
    BV_CLR(new_options, CITYO_DISBAND);
  }

  if (!client_is_observer()) {
    dsend_packet_city_options_req(&client.conn, dlgcity->id,
                                  new_options, dlgcity->wlcb);
  }
}

/************************************************************************//**
  Context menu on governor tab in city worklist
****************************************************************************/
void city_dialog::cma_context_menu(const QPoint &p)
{
  QMenu *cma_menu = new QMenu(this);
  QAction *cma_del_item;

  cma_menu->setAttribute(Qt::WA_DeleteOnClose);
  cma_del_item = cma_menu->addAction(_("Remove Governor"));
  connect(cma_menu, &QMenu::triggered, this,
          CAPTURE_DEFAULT_THIS (QAction *act) {
    if (act == cma_del_item) {
      cma_remove();
    }
  });

  cma_menu->popup(QCursor::pos());
}

/************************************************************************//**
  Context menu on production tab in city worklist
****************************************************************************/
void city_dialog::display_worklist_menu(const QPoint &p)
{
  QAction *action;
  QAction *disband;
  QAction *wl_save;
  QAction *wl_clear;
  QAction *wl_empty;
  QMap<QString, cid> list;
  QMap<QString, cid>::const_iterator map_iter;
  QMenu *change_menu;
  QMenu *insert_menu;
  QMenu *list_menu;
  QMenu *options_menu;
  int city_id = dlgcity->id;

  if (!can_client_issue_orders()) {
    return;
  }
  list_menu = new QMenu(this);
  change_menu = list_menu->addMenu(_("Change worklist"));
  insert_menu = list_menu->addMenu(_("Insert worklist"));
  wl_clear = list_menu->addAction(_("Clear"));
  connect(wl_clear, &QAction::triggered, this, &city_dialog::clear_worklist);
  list.clear();

  global_worklists_iterate(pgwl) {
    list.insert(global_worklist_name(pgwl), global_worklist_id(pgwl));
  } global_worklists_iterate_end;

  if (list.count() == 0) {
    wl_empty = change_menu->addAction(_("(no worklists defined)"));
    insert_menu->addAction(wl_empty);
  }

  map_iter = list.constBegin();

  while (map_iter != list.constEnd()) {
    action = change_menu->addAction(map_iter.key());
    action->setData(map_iter.value());

    action = insert_menu->addAction(map_iter.key());
    action->setData(map_iter.value());

    map_iter++;
  }

  wl_save = list_menu->addAction(_("Save worklist"));
  connect(wl_save, &QAction::triggered, this, &city_dialog::save_worklist);
  options_menu = list_menu->addMenu(_("Options"));
  disband = options_menu->addAction(_("Allow disbanding city"));
  disband->setCheckable(true);
  disband->setChecked(is_city_option_set(dlgcity, CITYO_DISBAND));
  connect(disband, &QAction::triggered, this,
          &city_dialog::disband_state_changed);

  connect(change_menu, &QMenu::triggered, this, [=](QAction *act) {
    QVariant id = act->data();
    struct city *pcity = game_city_by_number(city_id);
    const struct worklist *worklist;

    if (!pcity) {
      return;
    }

    fc_assert_ret(id.typeId() == QMetaType::Int);

    worklist = global_worklist_get(global_worklist_by_id(id.toInt()));
    city_set_queue(pcity, worklist);
  });

  connect(insert_menu, &QMenu::triggered, this,
          CAPTURE_DEFAULT_THIS (QAction *act) {
    QVariant id = act->data();
    struct city *pcity = game_city_by_number(city_id);
    const struct worklist *worklist;

    if (!pcity) {
      return;
    }

    fc_assert_ret(id.typeId() == QMetaType::Int);

    worklist = global_worklist_get(global_worklist_by_id(id.toInt()));
    city_queue_insert_worklist(pcity, selected_row_p + 1, worklist);
  });

  list_menu->popup(QCursor::pos());
}

/************************************************************************//**
  Enables/disables buy buttons depending on gold
****************************************************************************/
void city_dialog::update_buy_button()
{
  QString str;
  int value;
  struct player *pplayer = client_player();

  buy_button->setDisabled(true);

  if (!client_is_observer() && pplayer != nullptr) {
    value = dlgcity->client.buy_cost;
    str = QString(PL_("Buy (%1 gold)", "Buy (%1 gold)",
                      value)).arg(QString::number(value));

    if (pplayer->economic.gold >= value && value != 0) {
      buy_button->setEnabled(true);
    }
  } else {
    str = QString(_("Buy"));
  }

  buy_button->setText(str);
}

/************************************************************************//**
  Redraws citizens for city_label (citizens_label)
****************************************************************************/
void city_dialog::update_citizens()
{
  enum citizen_category categories[MAX_CITY_SIZE];
  int i, j, width, height;
  QPainter p;
  QPixmap *pix;
  int num_citizens = get_city_citizen_types(dlgcity, FEELING_FINAL, categories);
  int num_supers
    = city_try_fill_superspecialists(dlgcity,
                                     ARRAY_SIZE(categories) - num_citizens,
                                     &categories[num_citizens]);

  int w = tileset_small_sprite_width(tileset) / gui()->map_scale;
  int h = tileset_small_sprite_height(tileset) / gui()->map_scale;

  if (num_supers >= 0) {
    /* Just draw superspecialists in the common roster */
    num_citizens += num_supers;
  } else {
    /* FIXME: show them in some compact way */
    num_citizens = ARRAY_SIZE(categories);
  }
  i = 1 + (num_citizens * 5 / 200);
  w = w  / i;
  QRect source_rect(0, 0, w, h);
  QRect dest_rect(0, 0, w, h);
  width = w * num_citizens;
  height = h;

  if (citizen_pixmap) {
    citizen_pixmap->detach();
    delete citizen_pixmap;
  }

  citizen_pixmap = new QPixmap(width, height);

  for (j = 0, i = 0; i < num_citizens; i++, j++) {
    dest_rect.moveTo(i * w, 0);
    pix = get_citizen_sprite(tileset, categories[j], j, dlgcity)->pm;
    p.begin(citizen_pixmap);
    p.drawPixmap(dest_rect, *pix, source_rect);
    p.end();
  }

  citizens_label->set_city(dlgcity);
  citizens_label->setPixmap(*citizen_pixmap);

  lab_table[FEELING_FINAL]->setPixmap(*citizen_pixmap);
  lab_table[FEELING_FINAL]->setToolTip(text_happiness_wonders(dlgcity));

  for (int k = 0; k < FEELING_LAST - 1; k++) {
    lab_table[k]->set_city(dlgcity);
    num_citizens = get_city_citizen_types(dlgcity,
                                          static_cast<citizen_feeling>(k),
                                          categories);

    for (j = 0, i = 0; i < num_citizens; i++, j++) {
      dest_rect.moveTo(i * w, 0);
      pix = get_citizen_sprite(tileset, categories[j], j, dlgcity)->pm;
      p.begin(citizen_pixmap);
      p.drawPixmap(dest_rect, *pix, source_rect);
      p.end();
    }

    lab_table[k]->setPixmap(*citizen_pixmap);

    switch (k) {
    case FEELING_BASE:
      lab_table[k]->setToolTip(text_happiness_cities(dlgcity));
      break;

    case FEELING_LUXURY:
      lab_table[k]->setToolTip(text_happiness_luxuries(dlgcity));
      break;

    case FEELING_EFFECT :
      lab_table[k]->setToolTip(text_happiness_buildings(dlgcity));
      break;

    case FEELING_NATIONALITY:
      lab_table[k]->setToolTip(text_happiness_nationality(dlgcity));
      break;

    case FEELING_MARTIAL:
      lab_table[k]->setToolTip(text_happiness_units(dlgcity));
      break;

    default:
      break;
    }
  }
}

/************************************************************************//**
  Various refresh after getting new info/reply from server
****************************************************************************/
void city_dialog::refresh()
{
  setUpdatesEnabled(false);
  production_combo_p->blockSignals(true);

  if (dlgcity) {
    view->set_pixmap(dlgcity, zoom);
    view->update();
    update_title();
    update_info_label();
    update_buy_button();
    update_citizens();
    update_building();
    update_counters_table();
    update_improvements();
    update_units();
    update_nation_table();
    update_cma_tab();
    update_disabled();
  } else {
    destroy_city_dialog();
  }

  production_combo_p->blockSignals(false);
  setUpdatesEnabled(true);
  updateGeometry();
  update();
}

/************************************************************************//**
  Updates counters table
****************************************************************************/
void city_dialog::update_counters_table()
{
  QFont *small_font;

  small_font = fc_font::instance()->get_font(fonts::notify_label);

  qDeleteAll(counterss_frame->findChildren<QWidget*>("", Qt::FindDirectChildrenOnly));
  city_counters_iterate(pcount) {
    QString helptext;
    char buf[1024];
    QLabel *name, *value, *activated, *help;

    name = new QLabel(name_translation_get(&pcount->name));
    fc_snprintf(buf, sizeof(buf), _("Current value is: %d"),
                dlgcity->counter_values[counter_index(pcount)]);
    value = new QLabel(buf);
    fc_snprintf(buf, sizeof(buf), _("Activated once value equal "
                                    "or higher than: %d"),
                pcount->checkpoint);
    activated = new QLabel(buf);
    if (pcount->helptext != nullptr) {
      strvec_iterate(pcount->helptext, text_) {
        helptext += text_;
      } strvec_iterate_end;
    }
    help = new QLabel(helptext);

    name->setFont(*small_font);
    value->setFont(*small_font);
    activated->setFont(*small_font);
    help->setFont(*small_font);

    counterss_layout->addWidget(name);
    counterss_layout->addWidget(value);
    counterss_layout->addWidget(activated);
    counterss_layout->addWidget(help);
  } city_counters_iterate_end;
}

/************************************************************************//**
  Updates nationality table in happiness tab
****************************************************************************/
void city_dialog::update_nation_table()
{
  QFont f = QApplication::font();
  QFontMetrics fm(f);
  QPixmap *pix = nullptr;
  QPixmap pix_scaled;
  QString str;
  QStringList info_list;
  QTableWidgetItem *item;
  char buf[8];
  citizens nationality_i;
  int h;
  int i = 0;
  struct sprite *sprite;

  h = fm.height() + 6;
  nationality_table->clear();
  nationality_table->setRowCount(0);
  info_list.clear();
  info_list << _("#") << _("Flag") << _("Nation");
  nationality_table->setHorizontalHeaderLabels(info_list);

  citizens_iterate(dlgcity, pslot, nationality) {
    nationality_table->insertRow(i);

    for (int j = 0; j < nationality_table->columnCount(); j++) {
      item = new QTableWidgetItem;

      switch (j) {
      case 0:
        nationality_i = citizens_nation_get(dlgcity, pslot);

        if (nationality_i == 0) {
          str = "-";
        } else {
          fc_snprintf(buf, sizeof(buf), "%d", nationality_i);
          str = QString(buf);
        }

        item->setText(str);
        break;

      case 1:
        sprite = get_nation_flag_sprite(tileset,
                                        nation_of_player
                                        (player_slot_get_player(pslot)));

        if (sprite != nullptr) {
          pix = sprite->pm;
          pix_scaled = pix->scaledToHeight(h);
          item->setData(Qt::DecorationRole, pix_scaled);
        } else {
          item->setText("FLAG MISSING");
        }
        break;

      case 2:
        item->setText(nation_adjective_for_player
                      (player_slot_get_player(pslot)));
        break;

      default:
        break;
      }
      nationality_table->setItem(i, j, item);
    }
    i++;
  } citizens_iterate_end;
  nationality_table->horizontalHeader()->setStretchLastSection(false);
  nationality_table->resizeColumnsToContents();
  nationality_table->resizeRowsToContents();
  nationality_table->horizontalHeader()->setStretchLastSection(true);
}

/************************************************************************//**
  Updates information label ( food, prod ... surpluses ...)
****************************************************************************/
void city_dialog::update_info_label()
{
  int illness = 0;
  char buffer[512];
  char buf_info[NUM_INFO_FIELDS][512];
  char buf_tooltip[NUM_INFO_FIELDS][512];
  int granaryturns;
  int spec, supers;

  for (int i = 0; i < NUM_INFO_FIELDS; i++) {
    buf_info[i][0] = '\0';
    buf_tooltip[i][0] = '\0';
  }

  // Fill the buffers with the necessary info
  spec = city_specialists(dlgcity);
  supers = city_superspecialists(dlgcity);

  fc_snprintf(buf_info[INFO_CITIZEN], sizeof(buf_info[INFO_CITIZEN]),
              "%3d (%4d)", dlgcity->size, spec);
  if (supers) {
    fc_snprintf(buf_tooltip[INFO_CITIZEN], sizeof(buf_tooltip[INFO_CITIZEN]),
                _("Population: %d, Specialists: %d + %d"),
                dlgcity->size, spec, supers);
  } else {
    fc_snprintf(buf_tooltip[INFO_CITIZEN], sizeof(buf_tooltip[INFO_CITIZEN]),
                _("Population: %d, Specialists: %d"),
                dlgcity->size, spec);
  }
  fc_snprintf(buf_info[INFO_FOOD], sizeof(buf_info[INFO_FOOD]), "%3d (%+4d)",
              dlgcity->prod[O_FOOD], dlgcity->surplus[O_FOOD]);
  fc_snprintf(buf_info[INFO_SHIELD], sizeof(buf_info[INFO_SHIELD]),
              "%3d (%+4d)", dlgcity->prod[O_SHIELD] + dlgcity->waste[O_SHIELD],
              dlgcity->surplus[O_SHIELD]);
  fc_snprintf(buf_info[INFO_TRADE], sizeof(buf_info[INFO_TRADE]), "%3d (%+4d)",
              dlgcity->surplus[O_TRADE] + dlgcity->waste[O_TRADE],
              dlgcity->surplus[O_TRADE]);
  fc_snprintf(buf_info[INFO_GOLD], sizeof(buf_info[INFO_GOLD]), "%3d (%+4d)",
              dlgcity->prod[O_GOLD], dlgcity->surplus[O_GOLD]);
  fc_snprintf(buf_info[INFO_LUXURY], sizeof(buf_info[INFO_LUXURY]), "%3d",
              dlgcity->prod[O_LUXURY]);
  fc_snprintf(buf_info[INFO_SCIENCE], sizeof(buf_info[INFO_SCIENCE]), "%3d",
              dlgcity->prod[O_SCIENCE]);
  fc_snprintf(buf_info[INFO_GRANARY], sizeof(buf_info[INFO_GRANARY]),
              "%4d/%-4d", dlgcity->food_stock,
              city_granary_size(city_size_get(dlgcity)));

  get_city_dialog_output_text(dlgcity, O_FOOD, buf_tooltip[INFO_FOOD],
                              sizeof(buf_tooltip[INFO_FOOD]));
  get_city_dialog_output_text(dlgcity, O_SHIELD, buf_tooltip[INFO_SHIELD],
                              sizeof(buf_tooltip[INFO_SHIELD]));
  get_city_dialog_output_text(dlgcity, O_TRADE, buf_tooltip[INFO_TRADE],
                              sizeof(buf_tooltip[INFO_TRADE]));
  get_city_dialog_output_text(dlgcity, O_GOLD, buf_tooltip[INFO_GOLD],
                              sizeof(buf_tooltip[INFO_GOLD]));
  get_city_dialog_output_text(dlgcity, O_SCIENCE, buf_tooltip[INFO_SCIENCE],
                              sizeof(buf_tooltip[INFO_SCIENCE]));
  get_city_dialog_output_text(dlgcity, O_LUXURY, buf_tooltip[INFO_LUXURY],
                              sizeof(buf_tooltip[INFO_LUXURY]));
  get_city_dialog_culture_text(dlgcity, buf_tooltip[INFO_CULTURE],
                               sizeof(buf_tooltip[INFO_CULTURE]));
  get_city_dialog_pollution_text(dlgcity, buf_tooltip[INFO_POLLUTION],
                                 sizeof(buf_tooltip[INFO_POLLUTION]));
  get_city_dialog_illness_text(dlgcity, buf_tooltip[INFO_ILLNESS],
                               sizeof(buf_tooltip[INFO_ILLNESS]));

  granaryturns = city_turns_to_grow(dlgcity);

  if (granaryturns == 0) {
    // TRANS: city growth is blocked. Keep short.
    fc_snprintf(buf_info[INFO_GROWTH], sizeof(buf_info[INFO_GROWTH]), _("blocked"));
  } else if (granaryturns == FC_INFINITY) {
    // TRANS: city is not growing. Keep short.
    fc_snprintf(buf_info[INFO_GROWTH], sizeof(buf_info[INFO_GROWTH]), _("never"));
  } else {
    /* A negative value means we'll have famine in that many turns.
       But that's handled down below. */
    // TRANS: city growth turns. Keep short.
    fc_snprintf(buf_info[INFO_GROWTH], sizeof(buf_info[INFO_GROWTH]),
                PL_("%d turn", "%d turns", abs(granaryturns)),
                abs(granaryturns));
  }

  fc_snprintf(buf_info[INFO_CORRUPTION], sizeof(buf_info[INFO_CORRUPTION]),
              "%4d", dlgcity->waste[O_TRADE]);
  fc_snprintf(buf_info[INFO_WASTE], sizeof(buf_info[INFO_WASTE]), "%4d",
              dlgcity->waste[O_SHIELD]);
  fc_snprintf(buf_info[INFO_CULTURE], sizeof(buf_info[INFO_CULTURE]), "%4d",
              dlgcity->client.culture);
  fc_snprintf(buf_info[INFO_POLLUTION], sizeof(buf_info[INFO_POLLUTION]), "%4d",
              dlgcity->pollution);

  if (!game.info.illness_on) {
    fc_snprintf(buf_info[INFO_ILLNESS], sizeof(buf_info[INFO_ILLNESS]),
                "  -.-");
  } else {
    illness = city_illness_calc(dlgcity, nullptr, nullptr, nullptr, nullptr);
    // Illness is in tenth of percent
    fc_snprintf(buf_info[INFO_ILLNESS], sizeof(buf_info[INFO_ILLNESS]),
                "%5.1f%%", (float) illness / 10.0);
  }
  if (dlgcity->steal) {
    fc_snprintf(buf_info[INFO_STEAL], sizeof(buf_info[INFO_STEAL]),
                PL_("%d time", "%d times", dlgcity->steal), dlgcity->steal);
  } else {
    fc_snprintf(buf_info[INFO_STEAL], sizeof(buf_info[INFO_STEAL]),
                _("Not stolen"));
  }

  get_city_dialog_airlift_value(dlgcity, buf_info[INFO_AIRLIFT],
                                sizeof(buf_info[INFO_AIRLIFT]));
  get_city_dialog_airlift_text(dlgcity, buf_tooltip[INFO_AIRLIFT],
                               sizeof(buf_tooltip[INFO_AIRLIFT]));

  get_city_dialog_output_text(dlgcity, O_FOOD, buffer, sizeof(buffer));

  for (int i = 0; i < NUM_INFO_FIELDS; i++) {
    qlt[i]->setText(QString(buf_info[i]));

    if (buf_tooltip[i][0]) {
      qlt[i]->setToolTip("<pre>" + QString(buf_tooltip[i]).toHtmlEscaped()
                         + "</pre>");
    }
  }
}

/************************************************************************//**
  Setups whole city dialog, public function
****************************************************************************/
void city_dialog::setup_ui(struct city *qcity)
{
  QPixmap q_pix = *get_icon_sprite(tileset, ICON_CITYDLG)->pm;
  QIcon q_icon =::QIcon(q_pix);

  setContentsMargins(0, 0 ,0 ,0);
  setWindowIcon(q_icon);
  dlgcity = qcity;
  production_combo_p->blockSignals(true);
  refresh();
  production_combo_p->blockSignals(false);
}

/************************************************************************//**
  Double clicked item in worklist table in production tab
****************************************************************************/
void city_dialog::dbl_click_p(QTableWidgetItem *item)
{
  struct worklist queue;

  city_get_queue(dlgcity, &queue);

  if (selected_row_p < 0 || selected_row_p > worklist_length(&queue)) {
    return;
  }

  worklist_remove(&queue, selected_row_p);
  city_set_queue(dlgcity, &queue);
}

/************************************************************************//**
  Updates layouts for supported and present units in city
****************************************************************************/
void city_dialog::update_units()
{
  unit_item *ui;
  struct unit_list *units;
  char buf[256];
  int n;
  int happy_cost;
  int free_unhappy = get_city_bonus(dlgcity, EFT_MAKE_CONTENT_MIL);
  struct player *pplayer = client_player();
  const struct civ_map *nmap = &(wld.map);

  supported_units->setUpdatesEnabled(false);
  supported_units->clear_layout();

  if (pplayer != nullptr
      && city_owner(dlgcity) != pplayer) {
    units = dlgcity->client.info_units_supported;
  } else {
    units = dlgcity->units_supported;
  }

  unit_list_iterate(units, punit) {
    happy_cost = city_unit_unhappiness(nmap, punit, &free_unhappy);
    ui = new unit_item(this, punit, true, happy_cost);
    ui->init_pix();
    supported_units->add_item(ui);
  } unit_list_iterate_end;
  n = unit_list_size(units);
  fc_snprintf(buf, sizeof(buf), _("Supported units %d"), n);
  supp_units->setText(QString(buf));
  supported_units->update_units();
  supported_units->setUpdatesEnabled(true);
  current_units->setUpdatesEnabled(true);
  current_units->clear_layout();

  if (pplayer != nullptr
      && city_owner(dlgcity) != pplayer) {
    units = dlgcity->client.info_units_present;
  } else {
    units = dlgcity->tile->units;
  }

  unit_list_iterate(units, punit) {
    ui = new unit_item(this , punit, false);
    ui->init_pix();
    current_units->add_item(ui);
  } unit_list_iterate_end;

  n = unit_list_size(units);
  fc_snprintf(buf, sizeof(buf), _("Present units %d"), n);
  curr_units->setText(QString(buf));

  current_units->update_units();
  current_units->setUpdatesEnabled(true);
}

/************************************************************************//**
  Selection changed in production tab, in worklist tab
****************************************************************************/
void city_dialog::item_selected(const QItemSelection &sl,
                                const QItemSelection &ds)
{
  QModelIndex index;
  QModelIndexList indexes = sl.indexes();

  if (indexes.isEmpty()) {
    return;
  }

  index = indexes.at(0);
  selected_row_p = index.row();
  update_prod_buttons();
}

/************************************************************************//**
  Changes city_dialog to next city after pushing next city button
****************************************************************************/
void city_dialog::next_city()
{
  int size, i;
  struct city *other_pcity = nullptr;
  struct player *pplayer = client_player();

  if (pplayer == nullptr) {
    return;
  }

  size = city_list_size(pplayer->cities);

  if (size <= 1) {
    return;
  }

  for (i = 0; i < size; i++) {
    if (dlgcity == city_list_get(pplayer->cities, i)) {
      break;
    }
  }

  if (i >= size - 1) {
    // Current city last in the list (size - 1) or disappeared (size)
    other_pcity = city_list_get(pplayer->cities, 0);
  } else {
    other_pcity = city_list_get(pplayer->cities, i + 1);
  }

  center_tile_mapcanvas(other_pcity->tile);
  qtg_real_city_dialog_popup(other_pcity);
}

/************************************************************************//**
  Changes city_dialog to previous city after pushing prev city button
****************************************************************************/
void city_dialog::prev_city()
{
  int size, i;
  struct city *other_pcity = nullptr;
  struct player *pplayer = client_player();

  if (pplayer == nullptr) {
    return;
  }

  size = city_list_size(pplayer->cities);

  if (size <= 1) {
    return;
  }

  for (i = 0; i < size; i++) {
    if (dlgcity == city_list_get(pplayer->cities, i)) {
      break;
    }
  }

  if (i == 0 || i == size) {
    // Current city in the beginning of the list or disappeared
    other_pcity = city_list_get(pplayer->cities, size - 1);
  } else {
    other_pcity = city_list_get(pplayer->cities, i - 1);
  }

  center_tile_mapcanvas(other_pcity->tile);
  qtg_real_city_dialog_popup(other_pcity);
}

/************************************************************************//**
  Updates building improvement/unit
****************************************************************************/
void city_dialog::update_building()
{
  char buf[32];
  QString str;
  int cost = city_production_build_shield_cost(dlgcity);

  get_city_dialog_production(dlgcity, buf, sizeof(buf));
  production_combo_p->setRange(0, cost);
  production_combo_p->set_pixmap(&dlgcity->production);
  if (dlgcity->shield_stock >= cost) {
    production_combo_p->setValue(cost);
  } else {
    production_combo_p->setValue(dlgcity->shield_stock);
  }
  production_combo_p->setAlignment(Qt::AlignCenter);
  str = QString(buf);
  str = str.simplified();

  production_combo_p->setFormat(QString("(%p%) %2\n%1")
                                .arg(city_production_name_translation(dlgcity),
                                     str));

  production_combo_p->updateGeometry();

}

/************************************************************************//**
  Buy button. Shows message box asking for confirmation
****************************************************************************/
void city_dialog::buy()
{
  char buf[1024], buf2[1024];
  const char *name = city_production_name_translation(dlgcity);
  int value = dlgcity->client.buy_cost;
  hud_message_box *ask;
  int city_id = dlgcity->id;

  if (!can_client_issue_orders()) {
    return;
  }

  ask = new hud_message_box(city_dlg);
  fc_snprintf(buf2, ARRAY_SIZE(buf2), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);
  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Buy %s for %d gold?",
                                        "Buy %s for %d gold?", value),
              name, value);
  ask->set_text_title(buf, buf2);
  ask->setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  ask->setDefaultButton(QMessageBox::Cancel);
  ask->setAttribute(Qt::WA_DeleteOnClose);
  connect(ask, &hud_message_box::accepted, this, [=]() {
    struct city *pcity = game_city_by_number(city_id);

    if (!pcity) {
      return;
    }

    city_buy_production(pcity);
  });
  ask->show();
}

/************************************************************************//**
  Updates list of improvements
****************************************************************************/
void city_dialog::update_improvements()
{
  QFont f = QApplication::font();
  QFontMetrics fm(f);
  QPixmap *pix = nullptr;
  QPixmap pix_scaled;
  QString str, tooltip;
  QTableWidgetItem *qitem;
  struct sprite *sprite;
  int h, cost, item, targets_used, col, upkeep;
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct worklist queue;
  impr_item *ui;

  upkeep = 0;
  city_buildings->setUpdatesEnabled(false);
  city_buildings->clear_layout();

  h = fm.height() + 6;
  targets_used = collect_already_built_targets(targets, dlgcity);
  name_and_sort_items(targets, targets_used, items, false, dlgcity);

  for (item = 0; item < targets_used; item++) {
    struct universal target = items[item].item;

    ui = new impr_item(this, target.value.building, dlgcity);
    ui->init_pix();
    city_buildings->add_item(ui);

    fc_assert_action(VUT_IMPROVEMENT == target.kind, continue);
    sprite = get_building_sprite(tileset, target.value.building);
    upkeep = upkeep + city_improvement_upkeep(dlgcity, target.value.building);
    if (sprite != nullptr) {
      pix = sprite->pm;
      pix_scaled = pix->scaledToHeight(h);
    }
  }

  city_get_queue(dlgcity, &queue);
  p_table_p->setRowCount(worklist_length(&queue));

  for (int i = 0; i < worklist_length(&queue); i++) {
    struct universal target = queue.entries[i];

    tooltip = "";

    if (VUT_UTYPE == target.kind) {
      str = utype_values_translation(target.value.utype);
      cost = utype_build_shield_cost(dlgcity, nullptr, target.value.utype);
      tooltip = get_tooltip_unit(target.value.utype, true).trimmed();
      sprite = get_unittype_sprite(get_tileset(), target.value.utype,
                                   ACTIVITY_LAST,
                                   direction8_invalid());
    } else {
      str = city_improvement_name_translation(dlgcity, target.value.building);
      sprite = get_building_sprite(tileset, target.value.building);
      tooltip = get_tooltip_improvement(target.value.building,
                                        dlgcity, true).trimmed();

      if (is_convert_improvement(target.value.building)) {
        cost = -1;
      } else {
        cost = impr_build_shield_cost(dlgcity, target.value.building);
      }
    }

    for (col = 0; col < 3; col++) {
      qitem = new QTableWidgetItem();
      qitem->setToolTip(tooltip);

      switch (col) {
      case 0:
        if (sprite) {
          pix = sprite->pm;
          pix_scaled = pix->scaledToHeight(h);
          qitem->setData(Qt::DecorationRole, pix_scaled);
        }
        break;

      case 1:
        if (str.contains('[') && str.contains(']')) {
          int ii, ij;

          ii = str.lastIndexOf('[');
          ij = str.lastIndexOf(']');
          if (ij > ii) {
            str = str.remove(ii, ij - ii + 1);
          }
        }
        qitem->setText(str);
        break;

      case 2:
        qitem->setTextAlignment(Qt::AlignRight);
        qitem->setText(QString::number(cost));
        break;
      }
      p_table_p->setItem(i, col, qitem);
    }
  }

  p_table_p->horizontalHeader()->setStretchLastSection(false);
  p_table_p->resizeColumnsToContents();
  p_table_p->resizeRowsToContents();
  p_table_p->horizontalHeader()->setStretchLastSection(true);

  city_buildings->update_buildings();
  city_buildings->setUpdatesEnabled(true);
  city_buildings->setUpdatesEnabled(true);

  curr_impr->setText(QString(_("Improvements - upkeep %1")).arg(upkeep));
}

/************************************************************************//**
  Slot executed when user changed production in customized table widget
****************************************************************************/
void city_dialog::production_changed(int index)
{
  cid id;
  QVariant qvar;

  if (can_client_issue_orders()) {
    struct universal univ;

    id = qvar.toInt();
    univ = cid_production(id);
    city_change_production(dlgcity, &univ);
  }
}

/************************************************************************//**
  Shows customized table widget with available items to produce
  Shows default targets in overview city page
****************************************************************************/
void city_dialog::show_targets()
{
  production_widget *pw;
  int when = 1;

  pw = new production_widget(this, dlgcity, future_targets->isChecked(),
                             when, selected_row_p, show_units->isChecked(),
                             false, show_wonders->isChecked(),
                             show_buildings->isChecked());
  pw->show();
}

/************************************************************************//**
  Shows customized table widget with available items to produce
  Shows customized targets in city production page
****************************************************************************/
void city_dialog::show_targets_worklist()
{
  production_widget *pw;
  int when = 4;

  pw = new production_widget(this, dlgcity, future_targets->isChecked(),
                             when, selected_row_p, show_units->isChecked(),
                             false, show_wonders->isChecked(),
                             show_buildings->isChecked());
  pw->show();
}

/************************************************************************//**
  Clears worklist in production page
****************************************************************************/
void city_dialog::clear_worklist()
{
  struct worklist empty;

  if (!can_client_issue_orders()) {
    return;
  }

  worklist_init(&empty);
  city_set_worklist(dlgcity, &empty);
}

/************************************************************************//**
  Move current item on worklist up
****************************************************************************/
void city_dialog::worklist_up()
{
  QModelIndex index;
  struct worklist queue;
  struct universal *target;

  if (selected_row_p < 1 || selected_row_p >= p_table_p->rowCount()) {
    return;
  }

  target = new universal;
  city_get_queue(dlgcity, &queue);
  worklist_peek_ith(&queue, target, selected_row_p);
  worklist_remove(&queue, selected_row_p);
  worklist_insert(&queue, target, selected_row_p - 1);
  city_set_queue(dlgcity, &queue);
  index = p_table_p->model()->index(selected_row_p - 1, 0);
  p_table_p->setCurrentIndex(index);
  delete target;

}

/************************************************************************//**
  Remove current item on worklist
****************************************************************************/
void city_dialog::worklist_del()
{
  QTableWidgetItem *item;

  if (selected_row_p < 0
      || selected_row_p >= p_table_p->rowCount()) {
    return;
  }

  item = p_table_p->item(selected_row_p, 0);
  dbl_click_p(item);
  update_prod_buttons();
}

/************************************************************************//**
  Move current item on worklist down
****************************************************************************/
void city_dialog::worklist_down()
{
  QModelIndex index;
  struct worklist queue;
  struct universal *target;

  if (selected_row_p < 0 || selected_row_p >= p_table_p->rowCount() - 1) {
    return;
  }

  target = new universal;
  city_get_queue(dlgcity, &queue);
  worklist_peek_ith(&queue, target, selected_row_p);
  worklist_remove(&queue, selected_row_p);
  worklist_insert(&queue, target, selected_row_p + 1);
  city_set_queue(dlgcity, &queue);
  index = p_table_p->model()->index(selected_row_p + 1, 0);
  p_table_p->setCurrentIndex(index);
  delete target;
}

/************************************************************************//**
  Save worklist
****************************************************************************/
void city_dialog::save_worklist()
{
  hud_input_box *ask = new hud_input_box(gui()->central_wdg);
  int city_id = dlgcity->id;

  ask->set_text_title_definput(_("What should we name new worklist?"),
                               _("Save current worklist"),
                               _("New worklist"));
  ask->setAttribute(Qt::WA_DeleteOnClose);
  connect(ask, &hud_message_box::accepted, [=]() {
    struct global_worklist *gw;
    struct worklist queue;
    QByteArray ask_bytes;
    QString text;
    struct city *pcity = game_city_by_number(city_id);

    ask_bytes = ask->input_edit.text().toUtf8();
    text = ask_bytes.data();
    if (!text.isEmpty()) {
      ask_bytes = text.toUtf8();
      gw = global_worklist_new(ask_bytes.data());
      city_get_queue(pcity, &queue);
      global_worklist_set(gw, &queue);
    }
  });
  ask->show();
}

/************************************************************************//**
  Puts city name and people count on title
****************************************************************************/
void city_dialog::update_title()
{
  QString buf;

  // Defeat keyboard shortcut mnemonics
  lcity_name->setText(QString(city_name_get(dlgcity))
                      .replace("&", "&&"));

  if (city_unhappy(dlgcity)) {
    // TRANS: city dialog title
    buf = QString(_("%1 - %2 citizens - DISORDER")).arg(city_name_get(dlgcity),
          population_to_text(city_population(dlgcity)));
  } else if (city_celebrating(dlgcity)) {
    // TRANS: city dialog title
    buf = QString(_("%1 - %2 citizens - celebrating")).arg(city_name_get(dlgcity),
          population_to_text(city_population(dlgcity)));
  } else if (city_happy(dlgcity)) {
    // TRANS: city dialog title
    buf = QString(_("%1 - %2 citizens - happy")).arg(city_name_get(dlgcity),
          population_to_text(city_population(dlgcity)));
  } else {
    // TRANS: city dialog title
    buf = QString(_("%1 - %2 citizens")).arg(city_name_get(dlgcity),
          population_to_text(city_population(dlgcity)));
  }

  setWindowTitle(buf);
}

/************************************************************************//**
  Pop up (or bring to the front) a dialog for the given city.  It may or
  may not be modal.
****************************************************************************/
void qtg_real_city_dialog_popup(struct city *pcity)
{
  if (!::city_dlg_created) {
    ::city_dlg = new city_dialog(gui()->mapview_wdg);
  }

  city_dlg->setup_ui(pcity);
  city_dlg->show();
  city_dlg->activateWindow();
  city_dlg->raise();
}

/************************************************************************//**
  Closes city dialog
****************************************************************************/
void destroy_city_dialog()
{
  if (!::city_dlg_created) {
    return;
  }

  city_dlg->close();
  ::city_dlg_created = false;
}

/************************************************************************//**
  Close the dialog for the given city.
****************************************************************************/
void qtg_popdown_city_dialog(struct city *pcity)
{
  if (!::city_dlg_created) {
    return;
  }

  destroy_city_dialog();
}

/************************************************************************//**
  Close the dialogs for all cities.
****************************************************************************/
void qtg_popdown_all_city_dialogs()
{
  destroy_city_dialog();
}

/************************************************************************//**
  Refresh (update) all data for the given city's dialog.
****************************************************************************/
void qtg_real_city_dialog_refresh(struct city *pcity)
{
  if (!::city_dlg_created) {
    return;
  }

  if (qtg_city_dialog_is_open(pcity)) {
    city_dlg->refresh();
  }
}

/************************************************************************//**
  Updates city font
****************************************************************************/
void city_font_update()
{
  QList<QLabel *> l;
  QFont *f;

  if (!::city_dlg_created) {
    return;
  }

  l = city_dlg->findChildren<QLabel *>();

  f = fc_font::instance()->get_font(fonts::notify_label);

  for (int i = 0; i < l.size(); ++i) {
    if (l.at(i)->property(fonts::notify_label).isValid()) {
      l.at(i)->setFont(*f);
    }
  }
}

/************************************************************************//**
  Update city dialogs when the given unit's status changes.  This
  typically means updating both the unit's home city (if any) and the
  city in which it is present (if any).
****************************************************************************/
void qtg_refresh_unit_city_dialogs(struct unit *punit)
{

  struct city *pcity_sup, *pcity_pre;

  pcity_sup = game_city_by_number(punit->homecity);
  pcity_pre = tile_city(punit->tile);

  qtg_real_city_dialog_refresh(pcity_sup);
  qtg_real_city_dialog_refresh(pcity_pre);

}

/************************************************************************//**
  Return whether the dialog for the given city is open.
****************************************************************************/
bool qtg_city_dialog_is_open(struct city *pcity)
{
  if (!::city_dlg_created) {
    return false;
  }

  if (city_dlg->dlgcity == pcity && city_dlg->isVisible()) {
    return true;
  }

  return false;
}

/************************************************************************//**
  Event filter for catching tooltip events
****************************************************************************/
bool fc_tooltip::eventFilter(QObject *obj, QEvent *ev)
{
  QHelpEvent *help_event;
  QString item_tooltip;
  QRect rect;

  if (ev->type() == QEvent::ToolTip) {
    QAbstractItemView *view = qobject_cast<QAbstractItemView *>(obj->parent());

    if (!view) {
      return false;
    }

    help_event = static_cast<QHelpEvent *>(ev);
    QPoint pos = help_event->pos();
    QModelIndex index = view->indexAt(pos);
    QPoint hpos;

    if (!index.isValid()) {
      return false;
    }

    item_tooltip = view->model()->data(index, Qt::ToolTipRole).toString();
    rect = view->visualRect(index);

    hpos = help_event->globalPos();

    rect.setX(rect.x() + hpos.x());
    rect.setY(rect.y() + hpos.y());

    if (!item_tooltip.isEmpty()) {
      QToolTip::showText(help_event->globalPos(), item_tooltip, view, rect);
    } else {
      QToolTip::hideText();
    }

    return true;
  }

  return false;
}

/************************************************************************//**
  'text' is assumed to have already been HTML-escaped if necessary
****************************************************************************/
static QString bold(QString text)
{
  return QString("<b>" + text + "</b>");
}

/************************************************************************//**
  Returns improvement properties to append in tooltip
  ext is used to get extra info from help
****************************************************************************/
QString get_tooltip_improvement(const impr_type *building, struct city *pcity,
                                bool ext)
{
  QString def_str;
  QString upkeep;
  QString s1, s2, str;
  const char *req = skip_intl_qualifier_prefix(_("?tech:None"));
  struct player *pplayer = client_player();

  if (pcity != nullptr) {
    upkeep = QString::number(city_improvement_upkeep(pcity, building));
  } else {
    upkeep = QString::number(building->upkeep);
  }
  requirement_vector_iterate(&building->obsolete_by, pobs) {
    if (pobs->source.kind == VUT_ADVANCE) {
      req = advance_name_translation(pobs->source.value.advance);
      break;
    }
  } requirement_vector_iterate_end;
  s2 = QString(req);
  str = _("Obsolete by:");
  str = str + " " + s2;
  def_str = "<p style='white-space:pre'><b>"
            + QString(improvement_name_translation(building)).toHtmlEscaped()
            + "</b>\n";
  if (pcity != nullptr) {
    def_str += QString(_("Cost: %1, Upkeep: %2\n"))
               .arg(impr_build_shield_cost(pcity, building))
               .arg(upkeep).toHtmlEscaped();
  } else {
    int cost_est = impr_estimate_build_shield_cost(pplayer,
                                                   nullptr, building);

    def_str += QString(_("Cost Estimate: %1, Upkeep: %2\n"))
               .arg(cost_est)
               .arg(upkeep).toHtmlEscaped();
  }
  if (s1.compare(s2) != 0) {
    def_str = def_str + str.toHtmlEscaped() + "\n";
  }
  def_str = def_str + "\n";
  if (ext) {
    char buffer[8192];

    str = helptext_building(buffer, sizeof(buffer), pplayer,
                            nullptr, building);
    str = cut_helptext(str);
    str = split_text(str, true);
    str = str.trimmed();
    def_str = def_str + str.toHtmlEscaped();
  }
  return def_str;
}

/************************************************************************//**
  Returns unit properties to append in tooltip
  ext is used to get extra info from help
****************************************************************************/
QString get_tooltip_unit(const struct unit_type *utype, bool ext)
{
  QString def_str;
  QString obsolete_str;
  QString str;
  const struct unit_type *obsolete;
  struct advance *tech;

  def_str = "<b>"
    + QString(utype_name_translation(utype)).toHtmlEscaped()
    + "</b>\n";
  obsolete = utype->obsoleted_by;
  if (obsolete) {
    // FIXME: Don't give the impression that primary tech is (always) the only one.
    tech = utype_primary_tech_req(obsolete);
    obsolete_str = QString("</td></tr><tr><td colspan=\"3\">");
    if (tech && tech != advance_by_number(A_NONE)) {
      /* TRANS: this and nearby literal strings are interpreted
       * as (Qt) HTML */
      obsolete_str = obsolete_str + QString(_("Obsoleted by %1 (%2)."))
          .arg(utype_name_translation(obsolete))
          .arg(advance_name_translation(tech)).toHtmlEscaped();
    } else {
      obsolete_str = obsolete_str + QString(_("Obsoleted by %1."))
          .arg(utype_name_translation(obsolete)).toHtmlEscaped();
    }
  }
  def_str += "<table width=\"100\%\"><tr><td>"
             + bold(QString(_("Attack:"))) + " "
             + QString::number(utype->attack_strength).toHtmlEscaped()
             + QString("</td><td>") + bold(QString(_("Defense:"))) + " "
             + QString::number(utype->defense_strength).toHtmlEscaped()
             + QString("</td><td>") + bold(QString(_("Move:"))) + " "
             + QString(move_points_text(utype->move_rate, TRUE)).toHtmlEscaped()
             + QString("</td></tr><tr><td>")
             + bold(QString(_("Cost:"))) + " "
             + QString::number(utype_build_shield_cost_base(utype))
               .toHtmlEscaped()
             + QString("</td><td colspan=\"2\">")
             + bold(QString(_("Basic Upkeep:")))
             + " " + QString(helptext_unit_upkeep_str(utype)).toHtmlEscaped()
             + QString("</td></tr><tr><td>")
             + bold(QString(_("Hitpoints:"))) + " "
             + QString::number(utype->hp).toHtmlEscaped()
             + QString("</td><td>") + bold(QString(_("Firepower:"))) + " "
             + QString::number(utype->firepower).toHtmlEscaped()
             + QString("</td><td>") + bold(QString(_("Vision:"))) + " "
             + QString::number((int) sqrt((double) utype->vision_radius_sq))
               .toHtmlEscaped()
             + obsolete_str
             + QString("</td></tr></table><p style='white-space:pre'>");
  if (ext) {
    char buffer[8192];
    char buf2[1];

    buf2[0] = '\0';
    str = helptext_unit(buffer, sizeof(buffer), client_player(),
                        buf2, utype, TRUE);
    str = cut_helptext(str);
    str = split_text(str, true);
    str = str.trimmed().toHtmlEscaped();
    def_str = def_str + str;
  }

  return def_str;
};

/************************************************************************//**
  Returns shortened help for given universal ( stored in qvar )
****************************************************************************/
QString get_tooltip(QVariant qvar)
{
  QString str, def_str, ret_str;
  QStringList sl;
  char buffer[8192];
  char buf2[1];
  struct universal *target;
  struct player *pplayer = client_player();

  buf2[0] = '\0';
  target = reinterpret_cast<universal *>(qvar.value<void *>());

  if (target == nullptr) {
  } else if (VUT_UTYPE == target->kind) {
    def_str = get_tooltip_unit(target->value.utype);
    str = helptext_unit(buffer, sizeof(buffer), pplayer,
                        buf2, target->value.utype, TRUE);
  } else {
    if (!is_convert_improvement(target->value.building)) {
      def_str = get_tooltip_improvement(target->value.building);
    }

    str = helptext_building(buffer, sizeof(buffer), pplayer,
                            nullptr, target->value.building);
  }

  // Remove all lines from help which has '*' in first 3 chars
  ret_str = cut_helptext(str);
  ret_str = split_text(ret_str, true);
  ret_str = ret_str.trimmed();
  ret_str = def_str + ret_str.toHtmlEscaped();

  return ret_str;
}

/************************************************************************//**
  City item delegate constructor
****************************************************************************/
city_production_delegate::city_production_delegate(QPoint sh,
    QObject *parent,
    struct city *city)
  : QItemDelegate(parent)
{
  pd = sh;
  item_height = sh.y();
  pcity = city;
}

/************************************************************************//**
  City item delgate paint event
****************************************************************************/
void city_production_delegate::paint(QPainter *painter,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
  struct universal *target;
  QString name;
  QVariant qvar;
  QPixmap *pix;
  QPixmap pix_scaled;
  QRect rect1;
  QRect rect2;
  struct sprite *sprite;
  bool useless = false;
  bool is_convert = false;
  bool is_neutral = false;
  bool is_sea = false;
  bool is_flying = false;
  bool is_unit = true;
  QPixmap pix_dec(option.rect.width(), option.rect.height());
  QStyleOptionViewItem opt;
  color col;
  QIcon icon = current_app()->style()->standardIcon(QStyle::SP_DialogCancelButton);
  bool free_sprite = false;
  struct unit_class *pclass;

  if (!option.rect.isValid()) {
    return;
  }

  qvar = index.data();

  if (!qvar.isValid()) {
    return;
  }

  target = reinterpret_cast<universal *>(qvar.value<void *>());

  if (target == nullptr) {
    col.qcolor = Qt::white;
    sprite = qtg_create_sprite(100, 100, &col);
    free_sprite = true;
    *sprite->pm = icon.pixmap(100, 100);
    name = _("Cancel");
    is_unit = false;
  } else if (VUT_UTYPE == target->kind) {
    name = utype_name_translation(target->value.utype);
    is_neutral = utype_has_flag(target->value.utype, UTYF_CIVILIAN);
    pclass = utype_class(target->value.utype);
    if (!uclass_has_flag(pclass, UCF_TERRAIN_DEFENSE)
        && !utype_can_do_action_result(target->value.utype, ACTRES_FORTIFY)
        && !uclass_has_flag(pclass, UCF_ZOC)) {
      is_sea = true;
    }

    if ((utype_fuel(target->value.utype)
         && !uclass_has_flag(pclass, UCF_TERRAIN_DEFENSE)
         && !utype_can_do_action_result(target->value.utype, ACTRES_PILLAGE)
         && !utype_can_do_action_result(target->value.utype, ACTRES_FORTIFY)
         && !uclass_has_flag(pclass, UCF_ZOC))
        /* FIXME: Assumed to be flying since only missiles can do suicide
         * attacks in classic-like rulesets. This isn't true for all
         * rulesets. Not a high priority to fix since all is_flying and
         * is_sea is used for is to set a color. */
        || utype_is_consumed_by_action_result(ACTRES_ATTACK,
                                              target->value.utype)) {
      if (is_sea) {
        is_sea = false;
      }
      is_flying = true;
    }

    sprite = get_unittype_sprite(get_tileset(), target->value.utype,
                                 ACTIVITY_LAST,
                                 direction8_invalid());
  } else {
    is_unit = false;
    name = improvement_name_translation(target->value.building);
    sprite = get_building_sprite(tileset, target->value.building);
    useless = is_improvement_redundant(pcity, target->value.building);
    is_convert = (target->value.building->genus == IG_CONVERT);
  }

  if (sprite != nullptr) {
    pix = sprite->pm;
    pix_scaled = pix->scaledToHeight(item_height - 2, Qt::SmoothTransformation);

    if (useless) {
      pixmap_put_x(&pix_scaled);
    }
  }

  opt = QItemDelegate::setOptions(index, option);
  painter->save();
  opt.displayAlignment = Qt::AlignLeft;
  opt.textElideMode = Qt::ElideMiddle;
  QItemDelegate::drawBackground(painter, opt, index);
  rect1 = option.rect;
  rect1.setWidth(pix_scaled.width() + 4);
  rect2 = option.rect;
  rect2.setLeft(option.rect.left() + rect1.width());
  rect2.setTop(rect2.top() + (rect2.height()
                              - painter->fontMetrics().height()) / 2);
  QItemDelegate::drawDisplay(painter, opt, rect2, name);

  if (is_unit) {
    if (is_sea) {
      pix_dec.fill(QColor(0, 0, 255, 80));
    } else if (is_flying) {
      pix_dec.fill(QColor(220, 0, 0, 80));
    } else if (is_neutral) {
      pix_dec.fill(QColor(0, 120, 0, 40));
    } else {
      pix_dec.fill(QColor(0, 0, 150, 40));
    }

    QItemDelegate::drawDecoration(painter, option, option.rect, pix_dec);
  }

  if (is_convert) {
    pix_dec.fill(QColor(255, 255, 0, 70));
    QItemDelegate::drawDecoration(painter, option, option.rect, pix_dec);
  }

  if (!pix_scaled.isNull()) {
    QItemDelegate::drawDecoration(painter, opt, rect1, pix_scaled);
  }

  drawFocus(painter, opt, option.rect);

  painter->restore();

  if (free_sprite) {
    qtg_free_sprite(sprite);
  }
}

/************************************************************************//**
  Draws focus for given item
****************************************************************************/
void city_production_delegate::drawFocus(QPainter *painter,
    const QStyleOptionViewItem &option,
    const QRect &rect) const
{
  QPixmap pix(option.rect.width(), option.rect.height());

  if ((option.state & QStyle::State_MouseOver) == 0 || !rect.isValid()) {
    return;
  }

  pix.fill(QColor(50, 50, 50, 50));
  QItemDelegate::drawDecoration(painter, option, option.rect, pix);
}

/************************************************************************//**
  Size hint for city item delegate
****************************************************************************/
QSize city_production_delegate::sizeHint(const QStyleOptionViewItem &option,
    const QModelIndex &index) const
{
  QSize s;

  s.setWidth(pd.x());
  s.setHeight(pd.y());
  return s;
}

/************************************************************************//**
  Production item constructor
****************************************************************************/
production_item::production_item(struct universal *ptarget,
                                 QObject *parent): QObject()
{
  setParent(parent);
  target = ptarget;
}

/************************************************************************//**
  Production item destructor
****************************************************************************/
production_item::~production_item()
{
  // Allocated as renegade in model
  if (target != nullptr) {
    delete target;
  }
}

/************************************************************************//**
  Returns stored data
****************************************************************************/
QVariant production_item::data() const
{
  return QVariant::fromValue((void *)target);
}

/************************************************************************//**
  Sets data for item, must be declared.
****************************************************************************/
bool production_item::setData()
{
  return false;
}

/************************************************************************//**
  Constructor for city production model
****************************************************************************/
city_production_model::city_production_model(struct city *pcity, bool f,
    bool su, bool sw, bool sb,
    QObject *parent)
  : QAbstractListModel(parent)
{
  show_units = su;
  show_wonders = sw;
  show_buildings = sb;
  mcity = pcity;
  future_t = f;
  populate();
}

/************************************************************************//**
  Destructor for city production model
****************************************************************************/
city_production_model::~city_production_model()
{
  qDeleteAll(city_target_list);
  city_target_list.clear();
}

/************************************************************************//**
  Returns data from model
****************************************************************************/
QVariant city_production_model::data(const QModelIndex &index, int role) const
{
  if (!index.isValid()) {
    return QVariant();
  }

  if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0
      && index.column() < columnCount()
      && (index.column() + index.row() * 3 < city_target_list.count())) {
    int r, c, t, new_index;

    r = index.row();
    c = index.column();
    t = r * 3 + c;
    new_index = t / 3 + rowCount() * c;
    // Exception, shift whole column
    if ((c == 2) && city_target_list.count() % 3 == 1) {
      new_index = t / 3 + rowCount() * c - 1;
    }
    if (role == Qt::ToolTipRole) {
      return get_tooltip(city_target_list[new_index]->data());
    }

    return city_target_list[new_index]->data();
  }

  return QVariant();
}

/************************************************************************//**
  Fills model with data
****************************************************************************/
void city_production_model::populate()
{
  production_item *pi;
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  struct universal *renegade;
  int item, targets_used;
  QString str;
  QFont f = *fc_font::instance()->get_font(fonts::default_font);
  QFontMetrics fm(f);

  sh.setY(fm.height() * 2);
  sh.setX(0);

  qDeleteAll(city_target_list);
  city_target_list.clear();

  targets_used = collect_eventually_buildable_targets(targets, mcity,
                 future_t);
  name_and_sort_items(targets, targets_used, items, false, mcity);

  for (item = 0; item < targets_used; item++) {
    if (future_t || can_city_build_now(&(wld.map), mcity, &items[item].item)) {
      renegade = new universal(items[item].item);

      // Renegade deleted in production_item destructor
      if (VUT_UTYPE == renegade->kind) {
        str = utype_name_translation(renegade->value.utype);
        sh.setX(qMax(sh.x(), fm.horizontalAdvance(str)));

        if (show_units) {
          pi = new production_item(renegade, this);
          city_target_list << pi;
        } else {
          delete renegade;
        }
      } else {
        str = improvement_name_translation(renegade->value.building);
        sh.setX(qMax(sh.x(), fm.horizontalAdvance(str)));

        if ((is_wonder(renegade->value.building) && show_wonders)
            || (is_improvement(renegade->value.building) && show_buildings)
            || (is_convert_improvement(renegade->value.building))
            || (is_special_improvement(renegade->value.building)
                && show_buildings)) {
          pi = new production_item(renegade, this);
          city_target_list << pi;
        } else {
          delete renegade;
        }
      }
    }
  }

  pi = new production_item(nullptr, this);
  city_target_list << pi;
  sh.setX(2 * sh.y() + sh.x());
  sh.setX(qMin(sh.x(), 250));
}

/************************************************************************//**
  Sets data in model
****************************************************************************/
bool city_production_model::setData(const QModelIndex &index,
                                    const QVariant &value, int role)
{
  if (!index.isValid() || role != Qt::DisplayRole || role != Qt::ToolTipRole)
    return false;

  if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0
      && index.column() < columnCount()) {
    bool change = city_target_list[index.row()]->setData();
    return change;
  }

  return false;
}

/************************************************************************//**
  Constructor for production widget
  future - show future targets
  show_units - if to show units
  when - where to insert
  curr - current index to insert
  buy - buy if possible
****************************************************************************/
production_widget::production_widget(QWidget *parent, struct city *pcity,
                                     bool future, int when, int curr,
                                     bool show_units, bool buy,
                                     bool show_wonders,
                                     bool show_buildings): QTableView()
{
  QPoint pos, sh;
  QRect rect = QApplication::primaryScreen()->availableGeometry();
  int desk_width = rect.width();
  int desk_height = rect.height();
  fc_tt = new fc_tooltip(this);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlags(Qt::Popup);
  verticalHeader()->setVisible(false);
  horizontalHeader()->setVisible(false);
  setProperty("showGrid", false);
  curr_selection = curr;
  sh_units = show_units;
  pw_city = pcity;
  buy_it = buy;
  when_change = when;
  list_model = new city_production_model(pw_city, future, show_units,
                                         show_wonders, show_buildings, this);
  sh = list_model->sh;
  c_p_d = new city_production_delegate(sh, this, pw_city);
  setItemDelegate(c_p_d);
  setModel(list_model);
  viewport()->installEventFilter(fc_tt);
  installEventFilter(this);
  connect(selectionModel(), SIGNAL(selectionChanged(const QItemSelection &,
                                   const QItemSelection &)),
          SLOT(prod_selected(const QItemSelection &,
                             const QItemSelection &)));
  resizeRowsToContents();
  resizeColumnsToContents();
  setFixedWidth(3 * sh.x() + 6);
  setFixedHeight(list_model->rowCount()*sh.y() + 6);

  if (width() > desk_width) {
    setFixedWidth(desk_width);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  } else {
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }

  if (height() > desk_height) {
    setFixedHeight(desk_height);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  } else {
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }

  pos = QCursor::pos();

  if (pos.x() + width() > desk_width) {
    pos.setX(desk_width - width());
  } else if (pos.x() - width() < 0) {
    pos.setX(0);
  }

  if (pos.y() + height() > desk_height) {
    pos.setY(desk_height - height());
  } else if (pos.y() - height() < 0) {
    pos.setY(0);
  }

  move(pos);
  setMouseTracking(true);
  setFocus();
}

/************************************************************************//**
  Mouse press event for production widget
****************************************************************************/
void production_widget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::RightButton) {
    close();
    return;
  }

  QAbstractItemView::mousePressEvent(event);
}

/************************************************************************//**
  Event filter for production widget
****************************************************************************/
bool production_widget::eventFilter(QObject *obj, QEvent *ev)
{
  QRect pw_rect;
  QPoint br;

  if (obj != this)
    return false;

  if (ev->type() == QEvent::MouseButtonPress) {
    pw_rect.setTopLeft(pos());
    br.setX(pos().x() + width());
    br.setY(pos().y() + height());
    pw_rect.setBottomRight(br);

    if (!pw_rect.contains(QCursor::pos())) {
      close();
    }
  }

  return false;
}

/************************************************************************//**
  Changed selection in production widget
****************************************************************************/
void production_widget::prod_selected(const QItemSelection &sl,
                                      const QItemSelection &ds)
{
  QModelIndexList indexes = selectionModel()->selectedIndexes();
  QModelIndex index;
  QVariant qvar;
  struct worklist queue;
  struct universal *target;

  if (indexes.isEmpty() || client_is_observer()) {
    return;
  }
  index = indexes.at(0);
  qvar = index.data(Qt::UserRole);
  if (!qvar.isValid()) {
    return;
  }
  target = reinterpret_cast<universal *>(qvar.value<void *>());
  if (target != nullptr) {
    city_get_queue(pw_city, &queue);
    switch (when_change) {
    case 0: // Change current target
      city_change_production(pw_city, target);
      if (city_can_buy(pw_city) && buy_it) {
        city_buy_production(pw_city);
      }
      break;

    case 1:                 // Change current (selected on list)
      if (curr_selection < 0 || curr_selection > worklist_length(&queue)) {
        city_change_production(pw_city, target);
      } else {
        worklist_remove(&queue, curr_selection);
        worklist_insert(&queue, target, curr_selection);
        city_set_queue(pw_city, &queue);
      }
      break;

    case 2:                 // Insert before
      if (curr_selection < 0 || curr_selection > worklist_length(&queue)) {
        curr_selection = 0;
      }
      curr_selection--;
      curr_selection = qMax(0, curr_selection);
      worklist_insert(&queue, target, curr_selection);
      city_set_queue(pw_city, &queue);
      break;

    case 3:                 // Insert after
      if (curr_selection < 0 || curr_selection > worklist_length(&queue)) {
        city_queue_insert(pw_city, -1, target);
        break;
      }
      curr_selection++;
      worklist_insert(&queue, target, curr_selection);
      city_set_queue(pw_city, &queue);
      break;

    case 4:                 // Add last
      city_queue_insert(pw_city, -1, target);
      break;

    default:
      break;
    }
  }
  close();
  destroy();
}

/************************************************************************//**
  Destructor for production widget
****************************************************************************/
production_widget::~production_widget()
{
  delete c_p_d;
  delete list_model;
  viewport()->removeEventFilter(fc_tt);
  removeEventFilter(this);
}
