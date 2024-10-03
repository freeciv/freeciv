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
#ifndef FC__CITYDLG_H
#define FC__CITYDLG_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QElapsedTimer>
#include <QItemDelegate>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QTableView>
#include <QtMath>
#include <QVariant>

// common
#include "unittype.h"

// client
#include "canvas.h"

// gui-qt
#include "fonts.h"
#include "dialogs.h"

class QCheckBox;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QHBoxLayout;
class QPushButton;
class QRadioButton;
class QSplitter;
class QTableWidget;
class QVBoxLayout;

#define SPECENUM_NAME city_info

#define SPECENUM_VALUE0 INFO_CITIZEN
#define SPECENUM_VALUE0NAME N_("Size:")
#define SPECENUM_VALUE1 INFO_FOOD
#define SPECENUM_VALUE1NAME N_("Food:")
#define SPECENUM_VALUE2 INFO_SHIELD
#define SPECENUM_VALUE2NAME N_("Prod:")
#define SPECENUM_VALUE3 INFO_TRADE
#define SPECENUM_VALUE3NAME N_("Trade:")
#define SPECENUM_VALUE4 INFO_GOLD
#define SPECENUM_VALUE4NAME N_("Gold:")
#define SPECENUM_VALUE5 INFO_LUXURY
#define SPECENUM_VALUE5NAME N_("Luxury:")
#define SPECENUM_VALUE6 INFO_SCIENCE
#define SPECENUM_VALUE6NAME N_("Science:")
#define SPECENUM_VALUE7 INFO_GRANARY
#define SPECENUM_VALUE7NAME N_("Granary:")
#define SPECENUM_VALUE8 INFO_GROWTH
#define SPECENUM_VALUE8NAME N_("Change in:")
#define SPECENUM_VALUE9 INFO_CORRUPTION
#define SPECENUM_VALUE9NAME N_("Corruption:")
#define SPECENUM_VALUE10 INFO_WASTE
#define SPECENUM_VALUE10NAME N_("Waste:")
#define SPECENUM_VALUE11 INFO_CULTURE
#define SPECENUM_VALUE11NAME N_("Culture:")
#define SPECENUM_VALUE12 INFO_POLLUTION
#define SPECENUM_VALUE12NAME N_("Pollution:")
#define SPECENUM_VALUE13 INFO_ILLNESS
#define SPECENUM_VALUE13NAME N_("Plague risk:")
#define SPECENUM_VALUE14 INFO_STEAL
#define SPECENUM_VALUE14NAME N_("Tech Stolen:")
#define SPECENUM_VALUE15 INFO_AIRLIFT
#define SPECENUM_VALUE15NAME N_("Airlift:")

#define SPECENUM_COUNT    NUM_INFO_FIELDS // number of city_info panel enumerated fields
#include "specenum_gen.h"

class QImage;

QString get_tooltip(QVariant qvar);
QString get_tooltip_improvement(const impr_type *building,
                                struct city *pcity = nullptr,
                                bool ext = false);
QString get_tooltip_unit(const struct unit_type *utype, bool ext = false);

class fc_tooltip : public QObject
{
  Q_OBJECT
public:
  explicit fc_tooltip(QObject *parent = nullptr): QObject(parent) {}

protected:
  bool eventFilter(QObject *obj, QEvent *event);
};


/****************************************************************************
  Custom progressbar with animated progress and right click event
****************************************************************************/
class progress_bar: public QProgressBar
{
  Q_OBJECT
  QElapsedTimer m_timer;
signals:
  void clicked();

public:
  progress_bar(QWidget *parent);
  ~progress_bar();
  void mousePressEvent(QMouseEvent *event) {
    emit clicked();
  }
  void set_pixmap(struct universal *target);
  void set_pixmap(int n);

protected:
  void paintEvent(QPaintEvent *event);
  void timerEvent(QTimerEvent *event);
  void resizeEvent(QResizeEvent *event);
private:
  void create_region();
  int m_animate_step;
  QPixmap *pix;
  QRegion reg;
  QFont *sfont;
};

/****************************************************************************
  Single item on unit_info in city dialog representing one unit
****************************************************************************/
class unit_item: public QLabel
{
  Q_OBJECT
  QAction *disband_action;
  QAction *change_home;
  QAction *activate;
  QAction *activate_and_close;
  QAction *sentry;
  QAction *fortify;
  QAction *load;
  QAction *unload;
  QAction *upgrade;
  QAction *unload_trans;
  QMenu *unit_menu;

public:
  unit_item(QWidget *parent, struct unit *punit, bool supp = false,
            int happy_cost = 0);
  ~unit_item();
  void init_pix();

private:
  struct unit *qunit;
  QImage unit_img;
  void contextMenuEvent(QContextMenuEvent *ev);
  void create_actions();
  int happy_cost;
  bool supported;

private slots:
  void disband();
  void change_homecity();
  void activate_unit();
  void activate_and_close_dialog();
  void sentry_unit();
  void fortify_unit();
  void upgrade_unit();
  void load_unit();
  void unload_unit();
  void unload_all();

protected:
  void wheelEvent(QWheelEvent *event);
  void mousePressEvent(QMouseEvent *event);
  void leaveEvent(QEvent *event);
#ifndef FC_QT5_MODE
  void enterEvent(QEnterEvent *event);
#else  // FC_QT5_MODE
  void enterEvent(QEvent *event);
#endif // FC_QT5_MODE
};

/****************************************************************************
  Shows list of units ( as labels - unit_info )
****************************************************************************/
class unit_info: public QFrame
{

  Q_OBJECT

public:
  unit_info(bool supp);
  ~unit_info();
  void add_item(unit_item *item);
  void init_layout();
  void update_units();
  void clear_layout();
  QHBoxLayout *layout;
  QList<unit_item *> unit_list;

private:
  bool supports;
protected:
  void wheelEvent(QWheelEvent *event);
};


/****************************************************************************
  Single item on unit_info in city dialog representing one unit
****************************************************************************/
class impr_item: public QLabel
{
  Q_OBJECT

public:
  impr_item(QWidget *parent, const struct impr_type *building, struct city *pcity);
  ~impr_item();
  void init_pix();

private:
  const struct impr_type *impr;
  struct canvas *impr_pixmap;
  struct city *dlgcity;

protected:
  void wheelEvent(QWheelEvent *event);
  void mouseDoubleClickEvent(QMouseEvent *event);
  void leaveEvent(QEvent *event);
#ifndef FC_QT5_MODE
  void enterEvent(QEnterEvent *event);
#else  // FC_QT5_MODE
  void enterEvent(QEvent *event);
#endif // FC_QT5_MODE
};


/****************************************************************************
  Shows list of improvements
****************************************************************************/
class impr_info: public QFrame
{
  Q_OBJECT
public:
  impr_info(QWidget *parent);
  ~impr_info();
  void add_item(impr_item *item);
  void init_layout();
  void update_buildings();
  void clear_layout();
  QHBoxLayout *layout;
  QList<impr_item *> impr_list;
protected:
  void wheelEvent(QWheelEvent *event);
};

/****************************************************************************
  Class used for showing tiles and workers view in city dialog
****************************************************************************/
class city_map: public QWidget
{

  Q_OBJECT
  canvas *view;
  canvas *miniview;
  QPixmap zoomed_pixmap;

public:
  city_map(QWidget *parent);
  ~city_map();
  void set_pixmap(struct city *pcity, float z);
private:
  void mousePressEvent(QMouseEvent *event);
  void paintEvent(QPaintEvent *event);
  struct city *mcity;
  int radius;
  float zoom;
  int wdth;
  int height;
  int cutted_width;
  int cutted_height;
  int delta_x;
  int delta_y;
protected:
  QSize sizeHint() const;
  QSize minimumSizeHint() const;
private slots:
  void context_menu(QPoint point);
};

/****************************************************************************
  Item delegate for production popup
****************************************************************************/
class city_production_delegate: public QItemDelegate
{
  Q_OBJECT

public:
  city_production_delegate(QPoint sh, QObject *parent, struct city *city);
  ~city_production_delegate() {}
  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const;
  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const;

private:
  int item_height;
  QPoint pd;
  struct city *pcity;

protected:
  void drawFocus(QPainter *painter, const QStyleOptionViewItem &option,
                 const QRect &rect) const;
};

/****************************************************************************
  Single item in production popup
****************************************************************************/
class production_item: public QObject
{
  Q_OBJECT

public:
  production_item(struct universal *ptarget, QObject *parent);
  ~production_item();
  inline int columnCount() const {
    return 1;
  }
  QVariant data() const;
  bool setData();

private:
  struct universal *target;
};

/***************************************************************************
  City production model
***************************************************************************/
class city_production_model : public QAbstractListModel
{
  Q_OBJECT

public:
  city_production_model(struct city *pcity, bool f, bool su, bool sw, bool sb,
                        QObject *parent = nullptr);
  ~city_production_model();
  inline int rowCount(const QModelIndex &index = QModelIndex()) const {
    Q_UNUSED(index);
    return (qCeil(static_cast<float>(city_target_list.size()) / 3));
  }
  int columnCount(const QModelIndex &parent = QModelIndex()) const {
    Q_UNUSED(parent);
    return 3;
  }
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
  bool setData(const QModelIndex &index, const QVariant &value,
               int role = Qt::DisplayRole);
  QPoint size_hint();
  void populate();
  QPoint sh;

private:
  QList<production_item *> city_target_list;
  struct city *mcity;
  bool future_t;
  bool show_units;
  bool show_buildings;
  bool show_wonders;
};

/****************************************************************************
  Class for popup available production
****************************************************************************/
class production_widget: public QTableView
{
  Q_OBJECT

  city_production_model *list_model;
  city_production_delegate *c_p_d;

public:
  production_widget(QWidget *parent, struct city *pcity, bool future,
                    int when, int curr, bool show_units, bool buy = false,
                    bool show_wonders = true, bool show_buildings = true);
  ~production_widget();

public slots:
  void prod_selected(const QItemSelection &sl, const QItemSelection &ds);

protected:
  void mousePressEvent(QMouseEvent *event);
  bool eventFilter(QObject *obj, QEvent *ev);

private:
  struct city *pw_city;
  int when_change;
  int curr_selection;
  bool sh_units;
  bool buy_it;
  fc_tooltip *fc_tt;
};


/****************************************************************************
  city_label is used only for showing citizens icons
  and was created to catch mouse events
****************************************************************************/
class city_label: public QLabel
{
  Q_OBJECT

public:
  city_label(int type, QWidget *parent = nullptr);
  void set_city(struct city *pcity);

private:
  struct city *pcity;
  int type;

protected:
  void mousePressEvent(QMouseEvent *event);
};

/****************************************************************************
  City dialog
****************************************************************************/
class city_dialog: public qfc_dialog
{
  Q_OBJECT

  enum city_dialog_tab { common,
    happiness, counters }
  current_tab;

  QHBoxLayout *single_page_layout;
  QHBoxLayout *happiness_layout;
  QVBoxLayout *counterss_layout;
  QSplitter *prod_unit_splitter;
  QSplitter *central_left_splitter;
  QSplitter *central_splitter;
  QHBoxLayout *leftbot_layout;
  QWidget *prod_happ_widget;
  QWidget *top_widget;
  QVBoxLayout *left_layout;
  city_map *view;
  city_label *citizens_label;
  city_label *lab_table[6];
  QGridLayout *info_grid_layout;
  QGroupBox *info_labels_group;
  QGroupBox *happiness_group;
  QWidget *happiness_widget;
  QFrame *counterss_frame;
  QWidget *info_widget;
  QLabel *qlt[NUM_INFO_FIELDS];
  QLabel *cma_info_text;
  QLabel *cma_result;
  QLabel *cma_result_pix;
  QLabel *supp_units;
  QLabel *curr_units;
  QLabel *curr_impr;
  progress_bar *production_combo_p;
  QTableWidget *p_table_p;
  QTableWidget *nationality_table;
  QTableWidget *cma_table;
  QCheckBox *cma_celeb_checkbox;
  QCheckBox *cma_max_growth;
  QCheckBox *future_targets;
  QCheckBox *show_units;
  QCheckBox *show_buildings;
  QCheckBox *show_wonders;
  QRadioButton *r1, *r2, *r3, *r4;
  QPushButton *button;
  QPushButton *buy_button;
  QPushButton *cma_enable_but;
  QPushButton *next_city_but;
  QPushButton *prev_city_but;
  QPushButton *work_next_but;
  QPushButton *work_prev_but;
  QPushButton *work_add_but;
  QPushButton *work_rem_but;
  QPushButton *happiness_button;
  QPushButton *counterss_button;
  QPushButton *zoom_in_button;
  QPushButton *zoom_out_button;
  QPixmap *citizen_pixmap;
  unit_info *current_units;
  unit_info *supported_units;
  impr_info *city_buildings;
  QPushButton *lcity_name;
  int selected_row_p;
  QSlider *slider_tab[2 * O_LAST + 2];

public:
  city_dialog(QWidget *parent = nullptr);
  ~city_dialog();
  void setup_ui(struct city *qcity);
  void refresh();
  struct city *dlgcity;
  int scroll_height;
  float zoom;

private:
  int current_building;
  void update_title();
  void update_counters_table();
  void update_building();
  void update_info_label();
  void update_buy_button();
  void update_citizens();
  void update_improvements();
  void update_units();
  void update_nation_table();
  void update_cma_tab();
  void update_disabled();
  void update_sliders();
  void update_prod_buttons();
  void update_tabs();
  void change_production(bool next);

private slots:
  void next_city();
  void prev_city();
  void production_changed(int index);
  void show_targets();
  void show_targets_worklist();
  void show_happiness();
  void show_counters();
  void buy();
  void dbl_click_p(QTableWidgetItem *item);
  void item_selected(const QItemSelection &sl, const QItemSelection &ds);
  void clear_worklist();
  void save_worklist();
  void worklist_up();
  void worklist_down();
  void worklist_del();
  void display_worklist_menu(const QPoint &p);
  void disband_state_changed(bool allow_disband);
  void cma_slider(int val);
  void cma_toggle_changed(Qt::CheckState state);
  void cma_toggle_changed_depr(int state);
  void cma_remove();
  void cma_enable();
  void cma_changed();
  void cma_selected(const QItemSelection &sl, const QItemSelection &ds);
  void cma_double_clicked(int row, int column);
  void cma_context_menu(const QPoint &p);
  void save_cma();
  void city_rename();
  void zoom_in();
  void zoom_out();
protected:
  void showEvent(QShowEvent *event);
  void hideEvent(QHideEvent *event);
  void closeEvent(QCloseEvent *event);
  bool eventFilter(QObject *obj, QEvent *event);
};

void destroy_city_dialog();
void city_font_update();

#endif // FC__CITYDLG_H
