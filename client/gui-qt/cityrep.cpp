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
#include <QApplication>
#include <QHeaderView>
#include <QMessageBox>

// gui-qt
#include "qtg_cxxside.h"
#include "cityrep.h"

static bool can_city_sell_universal(const struct city *pcity,
                                    struct universal target);

/***************************************************************************
  City item delegate constructor
***************************************************************************/
city_item_delegate::city_item_delegate(QObject *parent)
                   :QItemDelegate(parent)
{
  QFont f = QApplication::font();
  QFontMetrics fm(f);

  item_height = fm.height() + 4;
}

/***************************************************************************
  City item delgate paint event
***************************************************************************/
void city_item_delegate::paint(QPainter *painter, 
                               const QStyleOptionViewItem &option, 
                               const QModelIndex &index) const
{
  QItemDelegate::paint(painter, option, index);
}

/***************************************************************************
  Size hint for city item delegate
***************************************************************************/
QSize city_item_delegate::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
  QSize s = QItemDelegate::sizeHint(option, index);

  s.setHeight(item_height);
  return s;
}

/***************************************************************************
  Constructor for city item
***************************************************************************/
city_item::city_item(city *pcity): QObject()
{
  i_city = pcity;
}

/***************************************************************************
  Returns used city pointer for city item creation
***************************************************************************/
city *city_item::get_city()
{
  return i_city;
}

/***************************************************************************
  Sets nothing, but must be declared
***************************************************************************/
bool city_item::setData(int column, const QVariant &value, int role)
{
  return false;
}

/***************************************************************************
  Returns data from city item (or city pointer from Qt::UserRole)
***************************************************************************/
QVariant city_item::data(int column, int role) const
{
  struct city_report_spec *spec;
  char buf[64];

  if (role == Qt::UserRole && column == 0) {
    return QVariant::fromValue((void *)i_city);
  }
  if (role != Qt::DisplayRole) {
    return QVariant();
  }
  spec = city_report_specs+column;
  fc_snprintf(buf, sizeof(buf), "%*s", NEG_VAL(spec->width),
                spec->func(i_city, spec->data));

  return QString(buf);
}

/***************************************************************************
  Constructor for city model
***************************************************************************/
city_model::city_model(QObject *parent): QAbstractListModel(parent)
{
  populate();
}

/***************************************************************************
  Destructor for city model
***************************************************************************/
city_model::~city_model()
{
  qDeleteAll(city_list);
}

/***************************************************************************
  Notifies about changed row
***************************************************************************/
void city_model::notify_city_changed(int row)
{
  emit dataChanged(index(row, 0), index(row, columnCount() - 1));
}

/***************************************************************************
  Returns stored data in index
***************************************************************************/
QVariant city_model::data(const QModelIndex &index, int role) const
{
  if (!index.isValid()) return QVariant();
  if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0
      && index.column() < columnCount())
    return city_list[index.row()]->data(index.column(), role);
  return QVariant();
}

/***************************************************************************
  Sets data in model under index
***************************************************************************/
bool city_model::setData(const QModelIndex &index, const QVariant &value, 
                         int role)
{
  if (!index.isValid() || role != Qt::DisplayRole) return false;
  if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0 
    && index.column() < columnCount()) {
    bool change = city_list[index.row()]->setData(index.column(), value, role);
    if (change) {
      notify_city_changed(index.row());
    }
    return change;
  }
  return false;
}

/***************************************************************************
  Returns header data for given section(column)
***************************************************************************/
QVariant city_model::headerData(int section, Qt::Orientation orientation, 
                                int role) const
{
  char buf[64];
  struct city_report_spec *spec;

  if (orientation == Qt::Horizontal && section < NUM_CREPORT_COLS) {
    if (role == Qt::DisplayRole) {
    spec = city_report_specs + section;
    fc_snprintf(buf, sizeof(buf), "%*s\n%*s",
                NEG_VAL(spec->width), spec->title1 ? spec->title1 : "",
                NEG_VAL(spec->width), spec->title2 ? spec->title2 : "");
      return QString(buf);
    }
  }
  return QVariant();
}

/***************************************************************************
  Returns header information about section
***************************************************************************/
QVariant city_model::menu_data(int section) const
{
  struct city_report_spec *spec;

  if (section < NUM_CREPORT_COLS) {
    spec = city_report_specs + section;
      return QString(spec->explanation);
  }
  return QVariant();
}

/***************************************************************************
  Hides given column if show is false
***************************************************************************/
QVariant city_model::hide_data(int section) const
{
  struct city_report_spec *spec;

  if (section < NUM_CREPORT_COLS) {
    spec = city_report_specs + section;
      return spec->show;
  }
  return QVariant();
}

/***************************************************************************
  Creates city model
***************************************************************************/
void city_model::populate()
{
  city_item *ci;

  if (client_has_player()) {
    city_list_iterate(client_player()->cities, pcity) {
      ci = new city_item(pcity);
      city_list << ci;
    } city_list_iterate_end;
  } else {
    cities_iterate(pcity) {
      ci = new city_item(pcity);
      city_list << ci;
    } cities_iterate_end;
  }
}
/***************************************************************************
  Notifies about changed item
***************************************************************************/
void city_model::city_changed(struct city *pcity)
{
  city_item *item;

  beginResetModel();
  endResetModel();
  for (int i = 0; i < city_list.count(); i++) {
    item = city_list.at(i);
    if (pcity == item->get_city()) {
      notify_city_changed(i);
    }
  }
}

/***************************************************************************
  Notifies about whole model changed
***************************************************************************/
void city_model::all_changed()
{
  city_list.clear();
  beginResetModel();
  populate();
  endResetModel();
  for (int i = 0; i < city_list.count(); i++) {
    notify_city_changed(i);
  }
}

/***************************************************************************
  Constructor for city widget
***************************************************************************/
city_widget::city_widget(city_report *ctr): QTreeView()
{
  cr = ctr;
  c_i_d = new city_item_delegate(this);
  setItemDelegate(c_i_d);
  list_model = new city_model(this);
  filter_model = new QSortFilterProxyModel();
  filter_model->setDynamicSortFilter(true);
  filter_model->setSourceModel(list_model);
  filter_model->setFilterRole(Qt::DisplayRole);
  setModel(filter_model);
  setRootIsDecorated(false);
  setAllColumnsShowFocus(true);
  setSortingEnabled(true);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setItemsExpandable(false);
  setAutoScroll(true);
  header()->setContextMenuPolicy(Qt::CustomContextMenu);
  setContextMenuPolicy(Qt::CustomContextMenu);
  hide_columns();
  connect(header(), SIGNAL(customContextMenuRequested(const QPoint &)),
          this, SLOT(display_header_menu(const QPoint &)));
  connect(selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(cities_selected(const QItemSelection &,
                               const QItemSelection &)));
  connect(this, SIGNAL(doubleClicked(QModelIndex)), this, 
          SLOT(city_doubleclick(QModelIndex)));
  connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), 
          this, SLOT(display_list_menu(const QPoint&)));
}

/***************************************************************************
  Slot for double clicking row
***************************************************************************/
void city_widget::city_doubleclick(const QModelIndex& index)
{
  city_view();
}

/***************************************************************************
  Shows first selected city
***************************************************************************/
void city_widget::city_view()
{
  struct city *pcity;

  if (selected_cities.isEmpty()) {
    return;
  }
  pcity = selected_cities[0];

  Q_ASSERT(pcity != NULL);
  if (center_when_popup_city) {
    center_tile_mapcanvas(pcity->tile);
  }
  qtg_real_city_dialog_popup(pcity);
}

/***************************************************************************
  Clears worklist for selected cities
***************************************************************************/
void city_widget::clear_worlist()
{
  struct worklist empty;
  worklist_init(&empty);
  struct city *pcity;

  foreach(pcity, selected_cities) {
    Q_ASSERT(pcity != NULL);
    city_set_worklist(pcity, &empty);
  }
}

/***************************************************************************
  Buys current item in city
***************************************************************************/
void city_widget::buy()
{
  struct city *pcity;

  foreach(pcity, selected_cities){
    Q_ASSERT(pcity != NULL);
    cityrep_buy(pcity);
  }
}

/***************************************************************************
  Centers map on city
***************************************************************************/
void city_widget::center()
{
  struct city *pcity;

  if (selected_cities.isEmpty()){
    return;
  }
  pcity= selected_cities[0];
  Q_ASSERT(pcity != NULL);
  center_tile_mapcanvas(pcity->tile);
  gui()->game_tab_widget->setCurrentIndex(0);
}

/***************************************************************************
  Displays right click menu on city row
***************************************************************************/
void city_widget::display_list_menu(const QPoint &)
{
  QMap<QString, cid> custom_labels;
  QMap<QString, int> cma_labels;
  QMenu *some_menu;
  QMenu *tmp2_menu;
  QMenu *tmp_menu;
  QMessageBox ask(this);
  QVariant qvar, qvar2;
  bool sell_ask;
  char buf[200];
  cid cid;
  const char *imprname;
  enum menu_labels m_state;
  int sell_count;
  int sell_gold;
  int sell_ret = QMessageBox::Cancel;
  struct city *pcity;
  struct impr_type *building;
  struct universal target;
  QMenu list_menu(this);
  QAction *act;
  QAction cty_view(style()->standardIcon(QStyle::SP_CommandLink),
                   _("?verb:View"), 0);
  QAction cty_buy(_("Buy"), 0);
  QAction cty_center(style()->standardIcon(QStyle::SP_ArrowRight),
                     _("Center"), 0);
  QAction wl_clear(_("Clear"), 0);
  QAction wl_empty(_("(no worklists defined)"), 0);
  bool worklist_defined = true;

  if (!can_client_issue_orders()) {
    return;
  }
  some_menu = list_menu.addMenu(_("Production"));
  tmp_menu = some_menu->addMenu(_("Change"));
  fill_production_menus(CHANGE_PROD_NOW, custom_labels, can_city_build_now,
                        tmp_menu);
  tmp_menu = some_menu->addMenu(_("Add next"));
  fill_production_menus(CHANGE_PROD_NEXT, custom_labels, can_city_build_now,
                        tmp_menu);
  tmp_menu = some_menu->addMenu(_("Add before last"));
  fill_production_menus(CHANGE_PROD_BEF_LAST, custom_labels,
                        can_city_build_now, tmp_menu);
  tmp_menu = some_menu->addMenu(_("Add last"));
  fill_production_menus(CHANGE_PROD_LAST, custom_labels, can_city_build_now,
                        tmp_menu);

  tmp_menu = some_menu->addMenu(_("Worklist"));
  tmp_menu->addAction(&wl_clear);
  connect(&wl_clear, SIGNAL(triggered()), this, SLOT(clear_worlist()));
  tmp2_menu = tmp_menu->addMenu(_("Add"));
  gen_worklist_labels(cma_labels);
  if (cma_labels.count() == 0) {
    tmp2_menu->addAction(&wl_empty);
    worklist_defined = false;
  }
  fill_data(WORKLIST_ADD, cma_labels, tmp2_menu);
  tmp2_menu = tmp_menu->addMenu(_("Change"));
  if (cma_labels.count() == 0) {
    tmp2_menu->addAction(&wl_empty);
    worklist_defined = false;
  }
  fill_data(WORKLIST_CHANGE, cma_labels, tmp2_menu);
  some_menu = list_menu.addMenu(_("CMA"));
  gen_cma_labels(cma_labels);
  fill_data(CMA, cma_labels, some_menu);
  some_menu = list_menu.addMenu(_("Sell"));
  gen_production_labels(SELL, custom_labels, false, false,
                        can_city_sell_universal);
  fill_data(SELL, custom_labels, some_menu);
  list_menu.addAction(&cty_view);
  connect(&cty_view, SIGNAL(triggered()), this, SLOT(city_view()));
  list_menu.addAction(&cty_buy);
  connect(&cty_buy, SIGNAL(triggered()), this, SLOT(buy()));
  list_menu.addAction(&cty_center);
  connect(&cty_center, SIGNAL(triggered()), this, SLOT(center()));
  act = 0;
  sell_count = 0;
  sell_gold = 0;
  sell_ask = true;
  act = list_menu.exec(QCursor::pos());
  if (act) {
    qvar2 = act->property("FC");
    m_state = static_cast<menu_labels>(qvar2.toInt());
    qvar = act->data();
    cid = qvar.toInt();
    target = cid_decode(cid);
    foreach (pcity, selected_cities) {
      if (NULL != pcity) {
        switch (m_state) {
        case CHANGE_PROD_NOW:
          city_change_production(pcity, target);
          break;
        case CHANGE_PROD_NEXT:
          city_queue_insert(pcity, 1, target);
          break;
        case CHANGE_PROD_BEF_LAST:
          city_queue_insert(pcity, worklist_length(&pcity->worklist), 
                            target);
          break;
        case CHANGE_PROD_LAST:
          city_queue_insert(pcity, -1, target);
          break;
        case SELL:
          building = target.value.building;
          if (sell_ask) {
            imprname = improvement_name_translation(building);
            fc_snprintf(buf, sizeof(buf),
                        _("Are you sure you want to sell those %s?"),
                        imprname);
            sell_ask = false;
            ask.setText(buf);
            ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
            ask.setDefaultButton(QMessageBox::Cancel);
            ask.setIcon(QMessageBox::Question);
            ask.setWindowTitle(" ");
            sell_ret = ask.exec();
          }
          if (sell_ret == QMessageBox::Ok) {
            if (!pcity->did_sell && city_has_building(pcity, building)) {
              sell_count++;
              sell_gold += impr_sell_gold(building);
              city_sell_improvement(pcity, improvement_number(building));
            }
          }
          break;
        case CMA:
          if (NULL != pcity) {
            if (CMA_NONE == cid) {
              cma_release_city(pcity);
            } else {
              cma_put_city_under_agent(pcity,
                                       cmafec_preset_get_parameter(cid));
            }
          }

          break;
        case WORKLIST_ADD:
          if (worklist_defined) {
            city_queue_insert_worklist(pcity, -1,
              global_worklist_get(global_worklist_by_id(cid)));
          }
          break;

        case WORKLIST_CHANGE:
          if (worklist_defined) {
            city_set_queue(pcity,
                           global_worklist_get(global_worklist_by_id(cid)));
          }
          break;
        default:
          break;
        }
      }
    }
  }
}

/***************************************************************************
  Fills menu items that can be produced or sold
***************************************************************************/
void city_widget::fill_production_menus(city_widget::menu_labels what,
                                        QMap<QString, cid> &custom_labels,
                                        TestCityFunc test_func, QMenu *menu)
{
  QMenu *m1, *m2, *m3;

  m1 = menu->addMenu(_("Buildings"));
  m2 = menu->addMenu(_("Units"));
  m3 = menu->addMenu(_("Wonders"));
  gen_production_labels(what, custom_labels, false, false, test_func);
  fill_data(what, custom_labels, m1);
  gen_production_labels(what, custom_labels, true, false, test_func);
  fill_data(what, custom_labels, m2);
  gen_production_labels(what, custom_labels, false, true, test_func);
  fill_data(what, custom_labels, m3);
}


/***************************************************************************
  Fills menu actions
***************************************************************************/
void city_widget::fill_data(menu_labels which,
                            QMap<QString, cid> &custom_labels, QMenu *menu)
{
  QAction *action;
  QMap<QString, cid>::const_iterator map_iter;

  map_iter = custom_labels.constBegin();
  while (map_iter != custom_labels.constEnd()) {
    action = menu->addAction(map_iter.key());
    action->setData(map_iter.value());
    action->setProperty("FC", which);
    map_iter++;
  }
}

/***************************************************************************
  Creates menu labels and id of available cma, stored in list
***************************************************************************/
void city_widget::gen_cma_labels(QMap<QString, int> &list)
{
  list.clear();
  for (int i = 0; i < cmafec_preset_num(); i++) {
    list.insert(cmafec_preset_get_descr(i), i);
  }
}

/***************************************************************************
  Creates menu labels and info of available worklists, stored in list
***************************************************************************/
void city_widget::gen_worklist_labels(QMap<QString, int> &list)
{
  list.clear();
  global_worklists_iterate(pgwl) {
    list.insert(global_worklist_name(pgwl), global_worklist_id(pgwl));
  } global_worklists_iterate_end;
}

/***************************************************************************
  Creates menu labels and id about available production targets
***************************************************************************/
void city_widget::gen_production_labels(city_widget::menu_labels what,
                                        QMap<QString, cid> &list,
                                        bool append_units,
                                        bool append_wonders,
                                        TestCityFunc test_func)
{

  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  int i, item, targets_used;
  QString str;
  char *row[4];
  char buf[4][64];
  struct city **data;
  int num_sel = selected_cities.count();
  struct city *array[num_sel];

  for (i = 0; i < num_sel; i++) {
    array[i] = selected_cities.at(i);
  }
  data = &array[0];
  targets_used
      = collect_production_targets(targets, data, num_sel, append_units,
                                   append_wonders, true, test_func);
  name_and_sort_items(targets, targets_used, items, true, NULL);
  for (i = 0; i < 4; i++) {
    row[i] = buf[i];
  }
  list.clear();
  for (item = 0; item < targets_used; item++) {
    struct universal target = items[item].item;
    char txt[256];
    str.clear();
    get_city_dialog_production_row(row, sizeof(buf[0]), target, NULL);
    fc_snprintf(txt, ARRAY_SIZE(txt), "%s ", row[0]);
    str = str + QString(txt);
    list.insert(str, cid_encode(target));
  }
}

/***************************************************************************
  Updates single city
***************************************************************************/
void city_widget::update_city(city *pcity)
{
  list_model->city_changed(pcity);
}

/***************************************************************************
  Updates whole model
***************************************************************************/
void city_widget::update_model()
{
  list_model->all_changed();
}

/***************************************************************************
  Context menu for header
***************************************************************************/
void city_widget::display_header_menu(const QPoint &)
{
  struct city_report_spec *spec;
  QMenu hideshowColumn(this);
  QList<QAction *> actions;
  QAction *act;

  hideshowColumn.setTitle(_("Column visibility"));
  for (int i = 0; i < list_model->columnCount(); i++) {
    QAction *myAct = hideshowColumn.addAction(
                       list_model->menu_data(i).toString());
    myAct->setCheckable(true);
    myAct->setChecked(!isColumnHidden(i));
    actions.append(myAct);
  }
  act = hideshowColumn.exec(QCursor::pos());
  if (act) {
    int col = actions.indexOf(act);
    Q_ASSERT(col >= 0);
    setColumnHidden(col, !isColumnHidden(col));
    spec = city_report_specs + col;
    spec->show = !spec->show; 
    if (!isColumnHidden(col) && columnWidth(col) <= 5)
      setColumnWidth(col, 100);
  }
}

/***************************************************************************
  Hides columns for city widget, depending on stored data (bool spec->show)
***************************************************************************/
void city_widget::hide_columns()
{
  for (int col = 0; col < list_model->columnCount(); col++) {
   if(!list_model->hide_data(col).toBool()){
    setColumnHidden(col, !isColumnHidden(col));
   }
  }
}

/***************************************************************************
  Slot for selecting items in city widget, they are stored in
  selected_cities until deselected
***************************************************************************/
void city_widget::cities_selected(const QItemSelection &sl, 
                                  const QItemSelection &ds)
{
  QModelIndexList indexes = selectionModel()->selectedIndexes();
  QModelIndex i;
  QVariant qvar;
  struct city *pcity;

  selected_cities.clear();

  if (indexes.isEmpty()) {
    return;
  }
  foreach(i,indexes){
    qvar = i.data(Qt::UserRole);
    if (qvar.isNull()) {
      continue;
    }
    pcity = reinterpret_cast<city *>(qvar.value<void *>());
    selected_cities << pcity;
  }
}

/***************************************************************************
  Returns used model
***************************************************************************/
city_model *city_widget::get_model() const
{
  return list_model;
}

/***************************************************************************
  Destructor for city widget
***************************************************************************/
city_widget::~city_widget()
{
  delete c_i_d;
  delete list_model;
  delete filter_model;
}

/***************************************************************************
  Constructor for city report
***************************************************************************/
city_report::city_report(): QWidget()
{
  layout = new QVBoxLayout;
  city_wdg = new city_widget(this);

  layout->addWidget(city_wdg);
  setLayout(layout);
}

/***************************************************************************
  Destructor for city report
***************************************************************************/
city_report::~city_report()
{
  gui()->remove_repo_dlg("CTS");
}

/***************************************************************************
  Inits place in game tab widget
***************************************************************************/
void city_report::init()
{
  gui()->gimme_place(this, "CTS");
  index = gui()->add_game_tab(this, _("Cities"));
  gui()->game_tab_widget->setCurrentIndex(index);
}
/***************************************************************************
  Updates whole report
***************************************************************************/
void city_report::update_report()
{
  city_wdg->update_model();
}

/***************************************************************************
  Updates single city
***************************************************************************/
void city_report::update_city(struct city *pcity)
{
  city_wdg->update_city(pcity);
}

/****************************************************************
  Same as can_city_sell_building(), but with universal argument.
*****************************************************************/
static bool can_city_sell_universal(const struct city *pcity,
                                    struct universal target)
{
  return (target.kind == VUT_IMPROVEMENT
          && can_city_sell_building(pcity, target.value.building));
}


/**************************************************************************
  Display the city report dialog.  Optionally raise it.
**************************************************************************/
void city_report_dialog_popup(bool raise)
{
  int i;
  city_report *cr;
  QWidget *w;

  if (!gui()->is_repo_dlg_open("CTS")) {
    cr = new city_report;
    cr->init();
    cr->update_report();
  } else {
    i = gui()->gimme_index_of("CTS");
    fc_assert(i != -1);
    w = gui()->game_tab_widget->widget(i);
    cr = reinterpret_cast<city_report*>(w);
    gui()->game_tab_widget->setCurrentWidget(cr);
  }
}

/**************************************************************************
  Update (refresh) the entire city report dialog.
**************************************************************************/
void real_city_report_dialog_update(void)
{
  int i;
  city_report *cr;
  QWidget *w;

  if (gui()->is_repo_dlg_open("CTS")) {
    i = gui()->gimme_index_of("CTS");
    fc_assert(i != -1);
    w = gui()->game_tab_widget->widget(i);
    cr = reinterpret_cast<city_report *>(w);
    cr->update_report();
  }
}

/**************************************************************************
  Update the information for a single city in the city report.
**************************************************************************/
void real_city_report_update_city(struct city *pcity)
{
  int i;
  city_report *cr;
  QWidget *w;

  if (gui()->is_repo_dlg_open("CTS")) {
    i = gui()->gimme_index_of("CTS");
    fc_assert(i != -1);
    w = gui()->game_tab_widget->widget(i);
    cr = reinterpret_cast<city_report *>(w);
    cr->update_city(pcity);
  }
}

/****************************************************************
 After a selection rectangle is defined, make the cities that
 are hilited on the canvas exclusively hilited in the
 City List window.
*****************************************************************/
void hilite_cities_from_canvas(void)
{
  /* PORTME */
}

/****************************************************************
 Toggle a city's hilited status.
*****************************************************************/
void toggle_city_hilite(struct city *pcity, bool on_off)
{
  /* PORTME */
}
