/***********************************************************************
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

#ifndef FC__SHORTCUTSDLG_H
#define FC__SHORTCUTSDLG_H

// Qt
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

class QDialogButtonBox;
class QVBoxLayout;
struct fc_shortcut;

void popup_shortcuts_dialog();
QString shortcut_to_string(fc_shortcut *sc);
void write_shortcuts();
bool read_shortcuts();
void shortcutreset();

// Assign numbers for casting
enum shortcut_id {
  SC_NONE = 0,
  SC_SCROLL_MAP = 1,
  SC_CENTER_VIEW = 2,
  SC_FULLSCREEN = 3,
  SC_MINIMAP = 4,
  SC_CITY_OUTPUT = 5,
  SC_MAP_GRID = 6,
  SC_NAT_BORDERS = 7,
  SC_QUICK_BUY = 8,
  SC_QUICK_SELECT = 9,
  SC_SELECT_BUTTON = 10,
  SC_ADJUST_WORKERS = 11,
  SC_APPEND_FOCUS = 12,
  SC_POPUP_INFO = 13,
  SC_WAKEUP_SENTRIES = 14,
  SC_MAKE_LINK = 15,
  SC_PASTE_PROD = 16,
  SC_COPY_PROD = 17,
  SC_HIDE_WORKERS = 18,
  SC_SHOW_UNITS = 19,
  SC_TRADE_ROUTES = 20,
  SC_CITY_PROD = 21,
  SC_CITY_NAMES = 22,
  SC_DONE_MOVING = 23,
  SC_GOTOAIRLIFT = 24,
  SC_AUTOEXPLORE = 25,
  SC_PATROL = 26,
  SC_UNSENTRY_TILE = 27,
  SC_DO = 28,
  SC_UPGRADE_UNIT = 29,
  SC_SETHOME = 30,
  SC_BUILDMINE = 31,
  SC_PLANT = 32,
  SC_BUILDIRRIGATION = 33,
  SC_CULTIVATE = 34,
  SC_BUILDROAD = 35,
  SC_BUILDCITY = 36,
  SC_SENTRY = 37,
  SC_FORTIFY = 38,
  SC_GOTO = 39,
  SC_WAIT = 40,
  SC_TRANSFORM = 41,
  SC_NUKE = 42,
  SC_BOARD = 43,
  SC_DEBOARD = 44,
  SC_BUY_MAP = 45,
  SC_IFACE_LOCK = 46,
  SC_AUTOMATE = 47,
  SC_CLEAN = 48,
  SC_POPUP_COMB_INF = 49,
  SC_RELOAD_THEME = 50,
  SC_RELOAD_TILESET = 51,
  SC_SHOW_FULLBAR = 52,
  SC_ZOOM_IN = 53,
  SC_ZOOM_OUT = 54,
  SC_LOAD_LUA = 55,
  SC_RELOAD_LUA = 56,
  SC_ZOOM_RESET = 57,
  SC_GOBUILDCITY = 58,
  SC_GOJOINCITY = 59,
  SC_STACK_SIZE = 60,
  SC_PARADROP = 61
};

#define SC_NUM_SHORTCUTS 61


/**************************************************************************
  Base shortcut struct
**************************************************************************/
struct fc_shortcut
{
  shortcut_id id;
  int key;
  Qt::MouseButton mouse;
  Qt::KeyboardModifiers mod;
  QString str;
  bool operator==(const fc_shortcut& a) const {
    return ((key == a.key) && (mouse == a.mouse) && (mod == a.mod));
  }
};

/**************************************************************************
  Class with static members holding all shortcuts
**************************************************************************/
class fc_shortcuts
{
  Q_DISABLE_COPY(fc_shortcuts);
  fc_shortcuts();
  static fc_shortcuts *m_instance;

public:
  ~fc_shortcuts();
  static fc_shortcuts *sc();
  static bool is_instantiated();
  static void drop();
  static QMap<shortcut_id, fc_shortcut*> hash;
public:
  static void init_default(bool read);
  fc_shortcut *get_shortcut(shortcut_id id);
  shortcut_id get_id(fc_shortcut *sc);
  void set_shortcut(fc_shortcut *sc);
  QString get_desc(shortcut_id id);
};

/**************************************************************************
  Widget for picking shortcuts
**************************************************************************/
class line_edit : public QLineEdit
{
  Q_OBJECT
public:
  line_edit();
  fc_shortcut shc;
  protected:
  void mousePressEvent(QMouseEvent *event);
  void keyReleaseEvent(QKeyEvent *event);
};

/**************************************************************************
  Popup for picking shortcuts
**************************************************************************/
class fc_shortcut_popup : public QDialog
{
  Q_OBJECT
public:
  fc_shortcut_popup(QWidget *parent);
  void run(fc_shortcut *s);
  fc_shortcut *sc;
protected:
 void closeEvent(QCloseEvent*);
private:
  bool check_if_exist();
  line_edit edit;
};

/**************************************************************************
  QPushButton holding shortcut
**************************************************************************/
class fc_sc_button : public QPushButton
{
  Q_OBJECT
  QString err_message;
public:
  fc_sc_button();
  fc_sc_button(fc_shortcut *s);
  fc_shortcut *sc;
  void show_info(QString str);
private slots:
  void popup_error();
};

/**************************************************************************
  Shortcut dialog
**************************************************************************/
class fc_shortcuts_dialog : public  QDialog
{
  Q_OBJECT
  QVBoxLayout *main_layout;
  QVBoxLayout *scroll_layout;
  QDialogButtonBox *button_box;
  QMap<shortcut_id, fc_shortcut*> *hashcopy;
  void add_option(fc_shortcut *sc);
  void init();
  void refresh();
public:
  fc_shortcuts_dialog(QWidget *parent = 0);
virtual ~fc_shortcuts_dialog();
private slots:
  void apply_option(int response);
  void edit_shortcut();
};

#endif // FC__SHORTCUTSDLG_H
