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

#ifdef AUDIO_SDL
/* Though it would happily compile without this include,
 * it is needed for sound to work as long as SDL-1.2 mixer is
 * being used. It defines "main" macro to rename our main() so that
 * it can install SDL's own. */
#ifdef AUDIO_SDL1_2
/* SDL */
#include <SDL/SDL.h>
#else  /* AUDIO_SDL1_2 */
/* SDL2 */
#include <SDL2/SDL.h>
#endif /* AUDIO_SDL1_2 */
#endif

#include <stdio.h>

// Qt
#include <QApplication>
#include <QMessageBox>
#include <QScrollBar>
#include <QStyleFactory>

// utility
#include "fciconv.h"
#include "log.h"

// client
#include "client_main.h"
#include "editgui_g.h"
#include "ggz_g.h"
#include "options.h"
#include "tilespec.h"
#include "sprite.h"

// gui-qt
#include "fc_client.h"
#include "helpdlg.h"
#include "gui_main.h"
#include "qtg_cxxside.h"


static QApplication *qapp;
static fc_client *freeciv_qt;
const char *client_string = "gui-qt";

const char * const gui_character_encoding = "UTF-8";
const bool gui_use_transliteration = false;

static QPixmap *unit_pixmap;
extern char gui_qt_default_theme_name[512];

void reset_unit_table(void);
static void populate_unit_pixmap_table(void);
static void apply_font(struct option *poption);
static void apply_city_font(struct option *poption);
static void apply_help_font(struct option *poption);

/****************************************************************************
  Return fc_client instance
****************************************************************************/
class fc_client *gui()
{
  return freeciv_qt;
}

/****************************************************************************
  Return QApplication instance
****************************************************************************/
class QApplication *current_app()
{
  return qapp;
}

/****************************************************************************
  Called by the tileset code to set the font size that should be used to
  draw the city names and productions.
****************************************************************************/
void qtg_set_city_names_font_sizes(int my_city_names_font_size,
                                   int my_city_productions_font_size)
{
}

/**************************************************************************
  Do any necessary pre-initialization of the UI, if necessary.
**************************************************************************/
void qtg_ui_init()
{}

/**************************************************************************
  Entry point for whole freeciv client program.
**************************************************************************/
int main(int argc, char **argv)
{
  setup_gui_funcs();
  return client_main(argc, argv);
}

/**************************************************************************
  Print extra usage information, including one line help on each option,
  to stderr. 
**************************************************************************/
static void print_usage()
{
  /* add client-specific usage information here */
  fc_fprintf(stderr,
             _("This client accepts the standard Qt command-line options\n"
               "after '--'. See the Qt documentation.\n\n"));

  /* TRANS: No full stop after the URL, could cause confusion. */
  fc_fprintf(stderr, _("Report bugs at %s\n"), BUG_URL);
}

/**************************************************************************
  Search for gui-specic command line options, that are not handled by Qt
  (QApplication). Returns true iff program is to be executed, and not
  to exit after showing the results from option parsing. 
**************************************************************************/
static bool parse_options(int argc, char **argv)
{
  int i = 1;

  while (i < argc) {
    if (is_option("--help", argv[i])) {
      print_usage();
      return false;
    }
    // Can't check against unknown options, as those might be Qt options

    i++;
  }

  return true;
}

/**************************************************************************
  The main loop for the UI.  This is called from main(), and when it
  exits the client will exit.
**************************************************************************/
void qtg_ui_main(int argc, char *argv[])
{
  if (parse_options(argc, argv)) {
    qapp = new QApplication(argc, argv);
    QPixmap *qpm = new QPixmap;
    QIcon app_icon;

    tileset_init(tileset);
    tileset_load_tiles(tileset);
    populate_unit_pixmap_table();
    qpm = get_icon_sprite(tileset, ICON_FREECIV)->pm;
    app_icon = ::QIcon(*qpm);
    qapp->setWindowIcon(app_icon);
    qapp->setStyle(QStyleFactory::create(gui_qt_default_theme_name));
    freeciv_qt = new fc_client();
    freeciv_qt->main(qapp);
  }
}

/****************************************************************************
  Extra initializers for client options.
****************************************************************************/
void qtg_gui_options_extra_init()
{
    struct option *poption;

#define option_var_set_callback(var, callback)                              \
  if ((poption = optset_option_by_name(client_optset, #var))) {             \
    option_set_changed_callback(poption, callback);                         \
  } else {                                                                  \
    log_error("Didn't find option %s!", #var);                              \
  }

  option_var_set_callback(gui_qt_font_city_names,
                          apply_font);
  option_var_set_callback(gui_qt_font_city_productions,
                          apply_font);
  option_var_set_callback(gui_qt_font_reqtree_text,
                          apply_font);
  option_var_set_callback(gui_qt_font_city_label,
                          apply_city_font);
  option_var_set_callback(gui_qt_font_help_label,
                          apply_help_font);
  option_var_set_callback(gui_qt_font_help_text,
                          apply_help_font);
  option_var_set_callback(gui_qt_font_help_title,
                          apply_help_font);
  option_var_set_callback(gui_qt_font_chatline,
                          apply_font);
#undef option_var_set_callback
}

/**************************************************************************
  Do any necessary UI-specific cleanup
**************************************************************************/
void qtg_ui_exit()
{
  delete freeciv_qt;
  delete qapp;
}

/**************************************************************************
 Update the connected users list at pregame state.
**************************************************************************/
void qtg_real_conn_list_dialog_update()
{
  gui()->update_start_page();
}

/**************************************************************************
  Make a bell noise (beep).  This provides low-level sound alerts even
  if there is no real sound support.
**************************************************************************/
void qtg_sound_bell()
{
  /* PORTME */
}

/**************************************************************************
  Wait for data on the given socket.  Call input_from_server() when data
  is ready to be read.

  This function is called after the client succesfully has connected
  to the server.
**************************************************************************/
void qtg_add_net_input(int sock)
{
  gui()->add_server_source(sock);
}

/**************************************************************************
  Stop waiting for any server network data.  See add_net_input().

  This function is called if the client disconnects from the server.
**************************************************************************/
void qtg_remove_net_input()
{
  gui()->remove_server_source();
}

/**************************************************************************
  Called to monitor a GGZ socket.
**************************************************************************/
void qtg_add_ggz_input(int sock)
{
  /* PORTME */
}

/**************************************************************************
  Called on disconnection to remove monitoring on the GGZ socket.  Only
  call this if we're actually in GGZ mode.
**************************************************************************/
void qtg_remove_ggz_input()
{
  /* PORTME */
}

/**************************************************************************
  Set one of the unit icons (specified by idx) in the information area
  based on punit.

  punit is the unit the information should be taken from. Use NULL to
  clear the icon.

  idx specified which icon should be modified. Use idx==-1 to indicate
  the icon for the active unit. Or idx in [0..num_units_below-1] for
  secondary (inactive) units on the same tile.
**************************************************************************/
void qtg_set_unit_icon(int idx, struct unit *punit)
{
  /* PORTME */
}

/**************************************************************************
  Most clients use an arrow (e.g., sprites.right_arrow) to indicate when
  the units_below will not fit. This function is called to activate or
  deactivate the arrow.

  Is disabled by default.
**************************************************************************/
void qtg_set_unit_icons_more_arrow(bool onoff)
{
  /* PORTME */
}

/****************************************************************************
  Called when the set of units in focus (get_units_in_focus()) changes.
  Standard updates like update_unit_info_label() are handled in the platform-
  independent code, so some clients will not need to do anything here.
****************************************************************************/
void qtg_real_focus_units_changed(void)
{
  if (gui()->unit_sel != NULL) {
    gui()->unit_sel->update_units();
  }
}

/****************************************************************************
  Enqueue a callback to be called during an idle moment.  The 'callback'
  function should be called sometimes soon, and passed the 'data' pointer
  as its data.
****************************************************************************/
void qtg_add_idle_callback(void (callback)(void *), void *data)
{
  call_me_back *cb = new call_me_back; /* removed in mr_idler:idling() */

  cb->callback = callback;
  cb->data = data;
  gui()->mr_idler.add_callback(cb);
}

/****************************************************************************
  Change the given font.
****************************************************************************/
static void apply_font(struct option *poption)
{
  QFont *f;
  QFont *remove_old;
  QString s;

  if (gui()) {
    f = new QFont;
    s = option_font_get(poption);
    f->fromString(s);
    s = option_name(poption);
    remove_old = gui()->fc_fonts.get_font(s);
    delete remove_old;
    gui()->fc_fonts.set_font(s, f);
    update_city_descriptions();
    gui()->infotab->chtwdg->update_font();
  }
}

static void apply_help_font(struct option *poption)
{
  QFont *f;
  QFont *remove_old;
  QString s;

  if (gui()) {
    f = new QFont;
    s = option_font_get(poption);
    f->fromString(s);
    s = option_name(poption);
    remove_old = gui()->fc_fonts.get_font(s);
    delete remove_old;
    gui()->fc_fonts.set_font(s, f);
    update_help_fonts();
  }
}


/****************************************************************************
  Changes city label font
****************************************************************************/
void apply_city_font(option *poption)
{
  QFont *f;
  QFont *remove_old;
  QString s;

  if (gui() && qtg_get_current_client_page() == PAGE_GAME) {
    f = new QFont;
    s = option_font_get(poption);
    f->fromString(s);
    s = option_name(poption);
    remove_old = gui()->fc_fonts.get_font(s);
    delete remove_old;
    gui()->fc_fonts.set_font(s, f);
    qtg_popdown_all_city_dialogs();
  }
}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void qtg_editgui_tileset_changed()
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void qtg_editgui_refresh()
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void qtg_editgui_popup_properties(const struct tile_list *tiles, int objtype)
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void qtg_editgui_popdown_all()
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void qtg_editgui_notify_object_changed(int objtype, int object_id, bool remove)
{}

/****************************************************************************
  Stub for editor function
****************************************************************************/
void qtg_editgui_notify_object_created(int tag, int id)
{}

/****************************************************************************
  Stub for ggz function
****************************************************************************/
void qtg_gui_ggz_embed_leave_table()
{}

/****************************************************************************
  Stub for ggz function
****************************************************************************/
void qtg_gui_ggz_embed_ensure_server()
{}

/****************************************************************************
  Updates a gui font style.
****************************************************************************/
void qtg_gui_update_font(const char *font_name, const char *font_value)
{
  /* PORTME */
}

/****************************************************************************
  Returns gui type of the client
****************************************************************************/
enum gui_type qtg_get_gui_type()
{
  return GUI_QT;
}

/**************************************************************************
  Called when the tileset is changed to reset the unit pixmap table.
**************************************************************************/
void reset_unit_table(void)
{
 /* FIXME */
}

/**************************************************************************
  Open dialog to confirm that user wants to quit client.
**************************************************************************/
void popup_quit_dialog()
{
  QMessageBox ask(gui()->central_wdg);
  int ret;

  ask.setText(_("Are you sure you want to quit?"));
  ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  ask.setDefaultButton(QMessageBox::Cancel);
  ask.setIcon(QMessageBox::Warning);
  ask.setWindowTitle(_("Quit?"));
  ret = ask.exec();

  switch (ret) {
  case QMessageBox::Cancel:
    return;
    break;
  case QMessageBox::Ok:
    start_quitting();
    if (client.conn.used) {
      disconnect_from_server();
    }
    gui()->write_settings();
    qapp->quit();
    break;
  }
}

/**************************************************************************
  Called to build the unit_below pixmap table.  This is the table on the
  left of the screen that shows all of the inactive units in the current
  tile.

  It may be called again if the tileset changes.
**************************************************************************/
static void populate_unit_pixmap_table(void)
{
  unit_pixmap = new QPixmap(tileset_unit_width(tileset), 
                            tileset_unit_height(tileset));
}
