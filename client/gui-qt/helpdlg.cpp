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
#include <QDebug>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QPushButton>
#include <QSplitter>
#include <QStack>
#include <QStringList>
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
  Constructor for help dialog
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
  resize(600, 350);
  layout = new QVBoxLayout(this);
  setLayout(layout);

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
    this, SLOT(item_changed(QTreeWidgetItem *))
  );
  help_wdg->layout()->setContentsMargins(0, 0, 0, 0);
  splitter->addWidget(help_wdg);

  sizes << 100 << 300;
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
  Create the help tree.
**************************************************************************/
void help_dialog::make_tree()
{
  QTreeWidgetItem *item;
  int d, depht = 0;
  QStack<QTreeWidgetItem *> stack;
  char *title;

  help_items_iterate(pitem) {
    title = pitem->topic;
    for (d = 0; *title == ' '; ++title, ++d) {
      // Do nothing
    }
    item = new QTreeWidgetItem(QStringList(title));
    topics_map[item] = pitem;
    if (d > 0 && d == depht) {
      // Same level in the tree
      stack.pop();
    } else if (d > 0 && d < depht) {
      // Less deep
      for ( ; d <= depht; --depht) {
        stack.pop();
      }
      stack.top()->addChild(item);
      stack.push(item);
    }
    depht = d;
    if (depht > 0) {
      stack.top()->addChild(item);
    } else {
      tree_wdg->addTopLevelItem(item);
    }
    stack.push(item);
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
  QWidget(parent)
{
  setup_ui();
}

/**************************************************************************
  Update fonts for help_wdg
**************************************************************************/
void help_dialog::update_fonts()
{
  help_wdg->update_fonts();
}

/**************************************************************************
  Creates a new help widget displaying the specified topic.
**************************************************************************/
help_widget::help_widget(const help_item *topic, QWidget *parent) :
  QWidget(parent)
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
  QVBoxLayout *group_layout;

  layout = new QVBoxLayout();
  setLayout(layout);

  box_wdg = new QFrame(this);
  layout->addWidget(box_wdg);
  group_layout = new QVBoxLayout(box_wdg);
  box_wdg->setLayout(group_layout);
  box_wdg->setFrameShape(QFrame::StyledPanel);
  box_wdg->setFrameShadow(QFrame::Raised);

  title_label = new QLabel(box_wdg);
  group_layout->addWidget(title_label);

  text_browser = new QTextBrowser(this);
  text_browser->setFont(QFont(QLatin1String("mono")));
  layout->addWidget(text_browser);
  update_fonts();
}

/**************************************************************************
  Updates fonts for manual
**************************************************************************/
void help_widget::update_fonts()
{
  QFont *label_font;
  QFont *help_font;

  label_font = gui()->fc_fonts.get_font("gui_qt_font_help_label");
  help_font = gui()->fc_fonts.get_font("gui_qt_font_help_text");
  title_label->setFont(*label_font);
  text_browser->setFont(*help_font);
}

/**************************************************************************
  Shows the given help page.
**************************************************************************/
void help_widget::set_topic(const help_item *topic)
{
  char *title = topic->topic;
  for ( ; *title == ' '; ++title) {
    // Do nothing
  }
  title_label->setText(title);

  switch (topic->type) {
    case HELP_ANY:
    case HELP_MULTIPLIER:
    case HELP_RULESET:
    case HELP_TEXT:
      set_topic_other(topic, title);
      break;
    case HELP_EXTRA:
      set_topic_extra(topic, title);
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
  struct unit_type *utype = unit_type_by_translated_name(title);
  if (utype) {
    helptext_unit(buffer, sizeof(buffer), client.conn.playing,
                  topic->text, utype);
    text_browser->setText(buffer);
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
  Creates extra help pages.
**************************************************************************/
void help_widget::set_topic_extra(const help_item *topic,
                                    const char *title)
{
  char buffer[MAX_HELP_TEXT_SIZE];
  struct extra_type *pextra = extra_type_by_translated_name(title);
  if (pextra) {
    helptext_extra(buffer, sizeof(buffer), client.conn.playing,
                  topic->text, pextra);
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
