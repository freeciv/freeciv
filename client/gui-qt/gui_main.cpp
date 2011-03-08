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
#include "SDL.h"
#endif

#include <stdio.h>

// Qt
#include <QApplication>

// utility
#include "fciconv.h"
#include "log.h"

// client
#include "client_main.h"
#include "editgui_g.h"
#include "ggz_g.h"
#include "options.h"

// gui-qt
#include "fc_client.h"
#include "qtg_cxxside.h"

static fc_client *freeciv_qt;
static QApplication *qapp;

/****************************************************************************
  Return fc_client instance
****************************************************************************/
class fc_client *gui()
{
  return freeciv_qt;
}

/****************************************************************************
  Called by the tileset code to set the font size that should be used to
  draw the city names and productions.
****************************************************************************/
void qtg_set_city_names_font_sizes(int my_city_names_font_size,
                                   int my_city_productions_font_size)
{
  log_error("Unimplemented set_city_names_font_sizes.");
  /* PORTME */
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
  The main loop for the UI.  This is called from main(), and when it
  exits the client will exit.
**************************************************************************/
void qtg_ui_main(int argc, char *argv[])
{
  qapp = new QApplication(argc, argv);
  freeciv_qt = new fc_client();

  freeciv_qt->main(qapp);
}

/****************************************************************************
  Extra initializers for client options.
****************************************************************************/
void qtg_gui_options_extra_init()
{
  /* Nothing to do. */
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
  /* PORTME */
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
  /* PORTME */
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
  /* PORTME */
}

/****************************************************************************
  Enqueue a callback to be called during an idle moment.  The 'callback'
  function should be called sometimes soon, and passed the 'data' pointer
  as its data.
****************************************************************************/
void qtg_add_idle_callback(void (callback)(void *), void *data)
{
  /* PORTME */

  /* This is a reasonable fallback if it's not ported. */
  log_error("Unimplemented add_idle_callback.");
  (callback)(data);
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

/**************************************************************************
  Updates a gui font style.
**************************************************************************/
void qtg_gui_update_font(const char *font_name, const char *font_value)
{
  /* PORTME */
}
