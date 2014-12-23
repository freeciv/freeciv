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

#define MAX_HELP_TEXT_SIZE 8192 // Taken from Gtk 3 client

// common
#include "nation.h"
#include "terrain.h"
#include "specialist.h"
#include "unit.h"

// utility
#include "fcintl.h"

// common
#include "movement.h"
#include "nation.h"
#include "terrain.h"
#include "specialist.h"
#include "unit.h"

// client
#include "helpdata.h"

// gui-qt
#include "qtg_cxxside.h"
#include "helpdlg.h"

// Qt
#include <QApplication>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStack>
#include <QStringList>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

static help_dialog *help_dlg = NULL;

/**************************************************************************
  Popup the help dialog to get help on the given string topic.  Note
  that the topic may appear in multiple sections of the help (it may
  be both an improvement and a unit, for example).

  The given string should be untranslated.
**************************************************************************/
void popup_help_dialog_string(const char *item)
{
  popup_help_dialog_typed(Q_(item), HELP_ANY);
}

/**************************************************************************
  Popup the help dialog to display help on the given string topic from
  the given section.

  The string will be translated.
**************************************************************************/
void popup_help_dialog_typed(const char *item, enum help_page_type htype)
{
  int pos;
  const help_item *topic;

  if (!help_dlg) {
    help_dlg = new help_dialog();
  }
  topic = get_help_item_spec(item, htype, &pos);
  if (pos >= 0) {
    help_dlg->set_topic(topic);
  }
  help_dlg->setVisible(true);
  help_dlg->activateWindow();
}

/**************************************************************************
  Close the help dialog.
**************************************************************************/
void popdown_help_dialog(void)
{
  if (help_dlg) {
    help_dlg->setVisible(false);
    help_dlg->deleteLater();
    help_dlg = NULL;
  }
}

/**************************************************************************
  Updates fonts
**************************************************************************/
void update_help_fonts()
{
  if (help_dlg) {
    help_dlg->update_fonts();
  }
}

/**************************************************************************
  Help dialog constructor
**************************************************************************/
help_dialog::help_dialog(QWidget *parent) :
  QDialog(parent)
{
  QSplitter *splitter;
  QList<int> sizes;
  QTreeWidgetItem *first;
  QVBoxLayout *layout;
  QDialogButtonBox *box;

  setWindowTitle(_("Freeciv Help Browser"));
  setWindowFlags(Qt::WindowStaysOnTopHint);
  resize(750, 450);
  layout = new QVBoxLayout(this);

  splitter = new QSplitter(this);
  layout->addWidget(splitter);

  tree_wdg = new QTreeWidget();
  tree_wdg->setHeaderHidden(true);
  make_tree();
  splitter->addWidget(tree_wdg);

  help_wdg = new help_widget(splitter);
  connect(
    tree_wdg,
    SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)),
    this, SLOT(item_changed(QTreeWidgetItem *)));
  help_wdg->layout()->setContentsMargins(0, 0, 0, 0);
  splitter->addWidget(help_wdg);

  sizes << 150 << 600;
  splitter->setSizes(sizes);

  box = new QDialogButtonBox(QDialogButtonBox::Close);
  layout->addWidget(box);
  box->addButton(_("About Qt"), QDialogButtonBox::HelpRole);
  box->button(QDialogButtonBox::Close)->setDefault(true);
  connect(box, &QDialogButtonBox::rejected, this, &QWidget::close);
  connect(box, &QDialogButtonBox::helpRequested, &QApplication::aboutQt);

  first = tree_wdg->topLevelItem(0);
  if (first) {
    tree_wdg->setCurrentItem(first);
  }
}

/**************************************************************************
  Update fonts for help_wdg
**************************************************************************/
void help_dialog::update_fonts()
{
  help_wdg->update_fonts();
}

/**************************************************************************
  Create the help tree.
**************************************************************************/
void help_dialog::make_tree()
{
  QTreeWidgetItem *item;
  int dep;
  char *title;
  QHash<int, QTreeWidgetItem*> hash;

  help_items_iterate(pitem) {
    const char *s;
    int last;
    title = pitem->topic;
    for (s = pitem->topic; *s == ' '; s++) {
      /* nothing */
    }
    item = new QTreeWidgetItem(QStringList(title));
    topics_map[item] = pitem;
    dep = s - pitem->topic;
    hash.insert(dep, item);
    if (dep == 0) {
      tree_wdg->addTopLevelItem(item);
    } else {
      last = dep - 1;
      hash.value(last)->addChild(item);
    }
  } help_items_iterate_end;
}

/**************************************************************************
  Changes the displayed topic.
**************************************************************************/
void help_dialog::set_topic(const help_item *topic)
{
  help_wdg->set_topic(topic);
  // Reverse search of the item to select.
  QHash<QTreeWidgetItem *, const help_item *>::const_iterator
      i = topics_map.cbegin();
  for ( ; i != topics_map.cend(); ++i) {
    if (i.value() == topic) {
      tree_wdg->setCurrentItem(i.key());
      break;
    }
  }
}

/**************************************************************************
  Called when a tree item is activated.
**************************************************************************/
void help_dialog::item_changed(QTreeWidgetItem *item)
{
  help_wdg->set_topic(topics_map[item]);
}

/**************************************************************************
  Creates a new, empty help widget.
**************************************************************************/
help_widget::help_widget(QWidget *parent) :
  QWidget(parent),
  main_widget(NULL)
{
  setup_ui();
}

/**************************************************************************
  Creates a new help widget displaying the specified topic.
**************************************************************************/
help_widget::help_widget(const help_item *topic, QWidget *parent) :
  QWidget(parent),
  main_widget(NULL)
{
  setup_ui();
  set_topic(topic);
}

/**************************************************************************
  Creates the UI.
**************************************************************************/
void help_widget::setup_ui()
{

  QVBoxLayout *layout;
  QHBoxLayout *group_layout;

  layout = new QVBoxLayout();
  setLayout(layout);

  box_wdg = new QFrame(this);
  layout->addWidget(box_wdg);
  group_layout = new QHBoxLayout(box_wdg);
  box_wdg->setLayout(group_layout);
  box_wdg->setFrameShape(QFrame::StyledPanel);
  box_wdg->setFrameShadow(QFrame::Raised);

  title_label = new QLabel(box_wdg);
  group_layout->addWidget(title_label);

  text_browser = new QTextBrowser(this);
  layout->addWidget(text_browser);
  main_widget = text_browser;

  update_fonts();
}

/**************************************************************************
  Updates fonts for manual
**************************************************************************/
void help_widget::update_fonts()
{
  QFont *help_font, *label_font, *title_font;
  QLabel *label;

  label_font = gui()->fc_fonts.get_font("gui_qt_font_help_label");
  help_font = gui()->fc_fonts.get_font("gui_qt_font_help_text");
  title_font = gui()->fc_fonts.get_font("gui_qt_font_help_title");
  text_browser->setFont(*help_font);
  title_label->setFont(*title_font);
  foreach (label, label_list) {
    label->setFont(*label_font);
  }
}

/**************************************************************************
  Shows the given help page.
**************************************************************************/
void help_widget::set_topic(const help_item *topic)
{
  // Remove any widget added by set_topic_xxx
  set_main_widget(text_browser);
  label_list.clear();

  char *title = topic->topic;
  for ( ; *title == ' '; ++title) {
    // Do nothing
  }
  title_label->setText(title);

  switch (topic->type) {
    case HELP_ANY:
    case HELP_BASE:
      set_topic_base(topic, title);
      break;
    case HELP_ROAD:
      set_topic_road(topic, title);
      break;
    case HELP_RULESET:
    case HELP_TEXT:
      set_topic_other(topic, title);
      break;
    case HELP_GOVERNMENT:
      set_topic_government(topic, title);
      break;
    case HELP_IMPROVEMENT:
    case HELP_WONDER:
      set_topic_building(topic, title);
      break;
    case HELP_NATIONS:
      set_topic_nation(topic, title);
      break;
    case HELP_SPECIALIST:
      set_topic_specialist(topic, title);
      break;
    case HELP_TECH:
      set_topic_tech(topic, title);
      break;
    case HELP_TERRAIN:
      set_topic_terrain(topic, title);
      break;
    case HELP_UNIT:
      set_topic_unit(topic, title);
      break;
    case HELP_LAST: // Just to avoid warning
      break;
  }
  update_fonts();
}

/**************************************************************************
  Sets the main content widget.
**************************************************************************/
void help_widget::set_main_widget(QWidget *widget)
{
  QLayoutItem *item;

  if (main_widget != widget) {
    item = layout()->replaceWidget(main_widget, widget);
    delete item;
    if (main_widget != text_browser) {
      main_widget->deleteLater();
    }
    main_widget = widget;
    main_widget->setParent(this);
  }
}

/**************************************************************************
  Creates a widget indicating a progress, used eg on unit help pages.
  Arguments:
    text: A descriptive text
    progress: The progress to display
    [min,max]: The interval progress is in
    value: Use this to display a non-numeral value
**************************************************************************/
QWidget *help_widget::create_progress_widget(const QString &text,
                                             int progress,
                                             int min, int max,
                                             const QString &value)
{
  QGridLayout *layout;
  QLabel *label;
  QProgressBar *bar;
  QWidget *wdg;

  wdg = new QWidget();
  layout = new QGridLayout(wdg);
  layout->setMargin(0);
  layout->setVerticalSpacing(0);

  label = new QLabel(text, wdg);
  label_list << label;
  layout->addWidget(label, 0, 0);

  label = new QLabel(wdg);
  label_list << label;
  if (value.isEmpty()) {
    label->setNum(progress);
  } else {
    label->setText(value);
  }
  layout->addWidget(label, 0, 1, Qt::AlignRight);

  bar = new QProgressBar(wdg);
  bar->setMaximumHeight(4);
  bar->setRange(min, max != min ? max : min + 1);
  bar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  bar->setTextVisible(false);
  bar->setValue(progress);
  layout->addWidget(bar, 1, 0, 1, 2);

  return wdg;
}

/**************************************************************************
  Creates help pages with no special widgets.
**************************************************************************/
void help_widget::set_topic_other(const help_item *topic,
                                    const char *title)
{
  if (topic->text) {
    text_browser->setText(topic->text);
  } else {
    text_browser->setText(""); // Something better to do ?
  }
}

/**************************************************************************
  Creates unit help pages.
**************************************************************************/
void help_widget::set_topic_unit(const help_item *topic,
                                   const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  int upkeep, max_upkeep;
  struct advance *tech;
  struct canvas *canvas;
  struct unit_type *obsolete, *utype, *max_utype;
  QLabel *label;
  QList<int> list;
  QSplitter *splitter;
  QVBoxLayout *vbox;
  QWidget *panel;

  utype = unit_type_by_translated_name(title);
  if (utype) {
    helptext_unit(buffer, sizeof(buffer), client.conn.playing,
                  topic->text, utype);
    text_browser->setText(buffer);

    // Create information widget
    max_utype = uclass_max_values(utype->uclass);
    splitter = new QSplitter(this);
    splitter->setSizePolicy(QSizePolicy::Expanding,
                            QSizePolicy::Expanding);
    set_main_widget(splitter);

    panel = new QWidget(splitter);
    vbox = new QVBoxLayout(panel);
    splitter->addWidget(panel);
    splitter->setStretchFactor(0, 25);
    splitter->addWidget(text_browser);
    splitter->setStretchFactor(1, 75);
    list << 200 << 400;
    splitter->setSizes(list);

    label = new QLabel();
    // Create unit icon
    canvas = qtg_canvas_create(
               tileset_full_tile_width(tileset),
               tileset_full_tile_height(tileset)
             );
    canvas->map_pixmap.fill(Qt::transparent);
    put_unittype(utype, canvas, 0, 0);
    label->setAlignment(Qt::AlignHCenter);
    label->setPixmap(canvas->map_pixmap);
    qtg_canvas_free(canvas);
    vbox->addWidget(label);

    vbox->addWidget(create_progress_widget(
                      _("Attack:"), utype->attack_strength, 0,
                      max_utype->attack_strength));
    vbox->addWidget(create_progress_widget(
                      _("Defense:"), utype->defense_strength,
                      0, max_utype->defense_strength));
    vbox->addWidget(create_progress_widget(
                      _("Moves:"), utype->move_rate, 0, max_utype->move_rate,
                      move_points_text(utype->move_rate, true)));

    vbox->addItem(new QSpacerItem(0, 2 * vbox->spacing()));

    vbox->addWidget(create_progress_widget(
                      _("Hitpoints:"), utype->hp, 0, max_utype->hp));
    vbox->addWidget(create_progress_widget(
                      _("Cost:"), utype_build_shield_cost(utype),
                      0, utype_build_shield_cost(max_utype)));
    vbox->addWidget(create_progress_widget(
                      _("Firepower:"), utype->firepower, 0, 
                      max_utype->firepower));

    // Upkeep
    upkeep = utype->upkeep[O_FOOD] + utype->upkeep[O_GOLD]
             + utype->upkeep[O_LUXURY] + utype->upkeep[O_SCIENCE]
             + utype->upkeep[O_SHIELD] + utype->upkeep[O_TRADE]
             + utype->happy_cost;
    max_upkeep = max_utype->upkeep[O_FOOD] + max_utype->upkeep[O_GOLD]
                 + max_utype->upkeep[O_LUXURY] + max_utype->upkeep[O_SCIENCE]
                 + max_utype->upkeep[O_SHIELD] + max_utype->upkeep[O_TRADE]
                 + max_utype->happy_cost;
    vbox->addWidget(create_progress_widget(
                      _("Basic upkeep:"), upkeep, 0, max_upkeep,
                      helptext_unit_upkeep_str(utype)));

    vbox->addItem(new QSpacerItem(0, 2 * vbox->spacing()));

    // Tech requirement
    label = new QLabel();
    label_list << label;
    tech = utype->require_advance;
    if (tech && tech != advance_by_number(0)) {
      // TRANS: Unit requires technology
      label->setText(QString(_("Requires %1."))
                     .arg(advance_name_translation(tech)));
    } else {
      label->setText(_("No technology required."));
    }
    label->setWordWrap(true);
    vbox->addWidget(label);

    // Obsolescence
    label = new QLabel();
    label_list << label;
    obsolete = utype->obsoleted_by;
    if (obsolete) {
      tech = obsolete->require_advance;
      if (tech && tech != advance_by_number(0)) {
        // TRANS: Current unit obsoleted by other unit (technology
        // required to build other unit)
        label->setText(QString(_("Obsoleted by %1 (%2)."))
                       .arg(utype_name_translation(obsolete))
                       .arg(advance_name_translation(tech)));
      } else {
        label->setText(
          // TRANS: Current unit obsoleted by other unit
          QString(_("Obsoleted by %1."))
          .arg(utype_name_translation(obsolete)));
      }
    } else {
      label->setText(_("Never obsolete."));
    }
    label->setWordWrap(true);
    vbox->addWidget(label);

    vbox->addItem(new QSpacerItem(
                    0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    delete max_utype;
  } else {
    set_topic_other(topic, title);
  }
}

/**************************************************************************
  Creates improvement help pages.
**************************************************************************/
void help_widget::set_topic_building(const help_item *topic,
                                       const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  struct impr_type *itype = improvement_by_translated_name(title);
  if (itype) {
    helptext_building(buffer, sizeof(buffer), client.conn.playing,
                      topic->text, itype);
    text_browser->setText(buffer);
  } else {
    set_topic_other(topic, title);
  }
}

/**************************************************************************
  Creates technology help pages.
**************************************************************************/
void help_widget::set_topic_tech(const help_item *topic,
                                   const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  struct advance *padvance = advance_by_translated_name(title);
  if (padvance) {
    int n = advance_number(padvance);
    if (!is_future_tech(n)) {
      helptext_advance(buffer, sizeof(buffer), client.conn.playing,
                      topic->text, n);
      text_browser->setText(buffer);
    }
  } else {
    set_topic_other(topic, title);
  }
}

/**************************************************************************
  Creates terrain help pages.
**************************************************************************/
void help_widget::set_topic_terrain(const help_item *topic,
                                      const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  struct terrain *pterrain = terrain_by_translated_name(title);
  if (pterrain) {
    helptext_terrain(buffer, sizeof(buffer), client.conn.playing,
                    topic->text, pterrain);
    text_browser->setText(buffer);
  } else {
    set_topic_other(topic, title);
  }
}

/**************************************************************************
  Creates specialist help pages.
**************************************************************************/
void help_widget::set_topic_specialist(const help_item *topic,
                                         const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  struct specialist *pspec = specialist_by_translated_name(title);
  if (pspec) {
    helptext_specialist(buffer, sizeof(buffer), client.conn.playing,
                        topic->text, pspec);
    text_browser->setText(buffer);
  } else {
    set_topic_other(topic, title);
  }
}

/**************************************************************************
  Creates government help pages.
**************************************************************************/
void help_widget::set_topic_government(const help_item *topic,
                                         const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  struct government *pgov = government_by_translated_name(title);
  if (pgov) {
    helptext_government(buffer, sizeof(buffer), client.conn.playing,
                        topic->text, pgov);
    text_browser->setText(buffer);
  } else {
    set_topic_other(topic, title);
  }
}

/**************************************************************************
  Creates roads help pages.
**************************************************************************/
void help_widget::set_topic_road(const help_item *topic, const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  struct road_type *proad = road_type_by_translated_name(title);

  if (!proad) {
    set_topic_other(topic, title);
  } else {
    helptext_road(buffer, sizeof(buffer), client.conn.playing,
                  topic->text, proad);
    text_browser->setText(buffer);
  }
}

/**************************************************************************
  Creates help pages for base
**************************************************************************/
void help_widget::set_topic_base(const help_item *topic, const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  struct base_type *pbase = base_type_by_translated_name(title);

  if (!pbase) {
    set_topic_other(topic, title);
  } else {
    helptext_base(buffer, sizeof(buffer), client.conn.playing,
                  topic->text, pbase);
    text_browser->setText(buffer);
  }
}

/**************************************************************************
  Creates nation help pages.
**************************************************************************/
void help_widget::set_topic_nation(const help_item *topic,
                                     const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  struct nation_type *pnation = nation_by_translated_plural(title);
  if (pnation) {
    helptext_nation(buffer, sizeof(buffer), pnation, topic->text);
    text_browser->setText(buffer);
  } else {
    set_topic_other(topic, title);
  }
}

/**************************************************************************
  Retrieves the maximum values any unit of uclass will ever have.
  Supported fields:
    attack_strength, bombard_rate, build_cost, city_size, convert_time,
    defense_strength, firepower, fuel, happy_cost, hp, move_rate, pop_cost,
    upkeep, vision_radius_sq
  Other fiels in returned value are undefined. Especially, all pointers are
  invalid except uclass.
**************************************************************************/
struct unit_type *help_widget::uclass_max_values(struct unit_class *uclass)
{
  struct unit_type *max = new struct unit_type;
  max->uclass = uclass;
  max->attack_strength = 0;
  max->bombard_rate = 0;
  max->build_cost = 0;
  max->city_size = 0;
  max->defense_strength = 0;
  max->firepower = 0;
  max->fuel = 0;
  max->happy_cost = 0;
  max->hp = 0;
  max->move_rate = 0;
  max->pop_cost = 0;
  max->upkeep[O_FOOD] = 0;
  max->upkeep[O_GOLD] = 0;
  max->upkeep[O_LUXURY] = 0;
  max->upkeep[O_SCIENCE] = 0;
  max->upkeep[O_SHIELD] = 0;
  max->upkeep[O_TRADE] = 0;
  max->vision_radius_sq = 0;
  unit_type_iterate(utype) {
    if (utype->uclass == uclass) {
#define SET_MAX(v) \
      max->v = max->v > utype->v ? max->v : utype->v
      SET_MAX(attack_strength);
      SET_MAX(bombard_rate);
      SET_MAX(build_cost);
      SET_MAX(city_size);
      SET_MAX(convert_time);
      SET_MAX(defense_strength);
      SET_MAX(firepower);
      SET_MAX(fuel);
      SET_MAX(happy_cost);
      SET_MAX(hp);
      SET_MAX(move_rate);
      SET_MAX(pop_cost);
      SET_MAX(upkeep[O_FOOD]);
      SET_MAX(upkeep[O_GOLD]);
      SET_MAX(upkeep[O_LUXURY]);
      SET_MAX(upkeep[O_SCIENCE]);
      SET_MAX(upkeep[O_SHIELD]);
      SET_MAX(upkeep[O_TRADE]);
      SET_MAX(vision_radius_sq);
#undef SET_MAX
    }
  } unit_type_iterate_end
  return max;
}
