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

/* client */
#include "client_main.h"
#include "editgui_g.h"
#include "ggz_g.h"
#include "options.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "connectdlg_g.h"
#include "editgui_g.h"
#include "graphics_g.h"
#include "gui_main_g.h"
#include "mapview_g.h"
#include "themes_g.h"

#include "qtg_cside.h"

static struct gui_funcs funcs;

const char *client_string = "gui-qt";

const char * const gui_character_encoding = "UTF-8";
const bool gui_use_transliteration = FALSE;

/****************************************************************************
  Return gui_funcs table. Used by C++ side to get table for filling
  with C++ function addresses.
****************************************************************************/
struct gui_funcs *get_gui_funcs(void)
{
  return &funcs;
}

/**************************************************************************
  Call c++ ui_init
**************************************************************************/
void ui_init(void)
{
  funcs.ui_init();
}

/**************************************************************************
  Call c++ ui_main
**************************************************************************/
void ui_main(int argc, char *argv[])
{
  funcs.ui_main(argc, argv);
}

/**************************************************************************
  Call c++ ui_exit
**************************************************************************/
void ui_exit(void)
{
  funcs.ui_exit();
}

/**************************************************************************
  Call c++ real_output_window_append
**************************************************************************/
void real_output_window_append(const char *astring,
                               const struct text_tag_list *tags,
                               int conn_id)
{
  funcs.real_output_window_append(astring, tags, conn_id);
}

/**************************************************************************
  Return our GUI type
**************************************************************************/
enum gui_type get_gui_type(void)
{
  return GUI_QT;
}

/**************************************************************************
  Call c++ isometric_view_supported
**************************************************************************/
bool isometric_view_supported(void)
{
  return funcs.isometric_view_supported();
}

/**************************************************************************
  Call c++ overhead_view_supported
**************************************************************************/
bool overhead_view_supported(void)
{
  return funcs.overhead_view_supported();
}

/**************************************************************************
  Call c++ free_intro_radar_sprites
**************************************************************************/
void free_intro_radar_sprites(void)
{
  funcs.free_intro_radar_sprites();
}

/**************************************************************************
  Call c++ load_gfxfile
**************************************************************************/
struct sprite *load_gfxfile(const char *filename)
{
  return funcs.load_gfxfile(filename);
}

/****************************************************************************
  Call c++ create_sprite
****************************************************************************/
struct sprite *create_sprite(int width, int height, struct color *pcolor)
{
  return funcs.create_sprite(width, height, pcolor);
}

/**************************************************************************
  Call c++ get_sprite_dimensions
**************************************************************************/
void get_sprite_dimensions(struct sprite *sprite, int *width, int *height)
{
  funcs.get_sprite_dimensions(sprite, width, height);
}

/**************************************************************************
  Call c++ crop_sprite
**************************************************************************/
struct sprite *crop_sprite(struct sprite *source,
			   int x, int y, int width, int height,
			   struct sprite *mask,
			   int mask_offset_x, int mask_offset_y)
{
  return funcs.crop_sprite(source, x, y, width, height, mask,
                           mask_offset_x, mask_offset_y);
}

/**************************************************************************
  Call c++ free_sprite
**************************************************************************/
void free_sprite(struct sprite *s)
{
  funcs.free_sprite(s);
}

/**************************************************************************
  Call c++ color_alloc
**************************************************************************/
struct color *color_alloc(int r, int g, int b)
{
  return funcs.color_alloc(r, g, b);
}

/**************************************************************************
  Call c++ color_free
**************************************************************************/
void color_free(struct color *pcolor)
{
  return funcs.color_free(pcolor);
}

/**************************************************************************
  Call c++ canvas_create
**************************************************************************/
struct canvas *canvas_create(int width, int height)
{
  return funcs.canvas_create(width, height);
}

/**************************************************************************
  Call c++ canvas_free
**************************************************************************/
void canvas_free(struct canvas *store)
{
  funcs.canvas_free(store);
}

/**************************************************************************
  Call c++ canvas_copy
**************************************************************************/
void canvas_copy(struct canvas *dest, struct canvas *src,
		     int src_x, int src_y, int dest_x, int dest_y, int width,
		     int height)
{
  funcs.canvas_copy(dest, src, src_x, src_y, dest_x, dest_y, width, height);
}

/**************************************************************************
  Call c++ canvas_put_sprite
**************************************************************************/
void canvas_put_sprite(struct canvas *pcanvas,
                       int canvas_x, int canvas_y,
                       struct sprite *psprite,
                       int offset_x, int offset_y, int width, int height)
{
  funcs.canvas_put_sprite(pcanvas, canvas_x, canvas_y, psprite,
                          offset_x, offset_y, width, height);
}

/**************************************************************************
  Call c++ canvas_put_sprite_full
**************************************************************************/
void canvas_put_sprite_full(struct canvas *pcanvas,
                            int canvas_x, int canvas_y,
                            struct sprite *psprite)
{
  funcs.canvas_put_sprite_full(pcanvas, canvas_x, canvas_y, psprite);
}

/**************************************************************************
  Call c++ canvas_put_sprite_fogged
**************************************************************************/
void canvas_put_sprite_fogged(struct canvas *pcanvas,
			      int canvas_x, int canvas_y,
			      struct sprite *psprite,
			      bool fog, int fog_x, int fog_y)
{
  funcs.canvas_put_sprite_fogged(pcanvas, canvas_x, canvas_y,
                                 psprite, fog, fog_x, fog_y);
}

/**************************************************************************
  Call c++ canvas_put_rectangle
**************************************************************************/
void canvas_put_rectangle(struct canvas *pcanvas,
                          struct color *pcolor,
                          int canvas_x, int canvas_y, int width, int height)
{
  funcs.canvas_put_rectangle(pcanvas, pcolor, canvas_x, canvas_y,
                             width, height);
}

/**************************************************************************
  Call c++ canvas_fill_sprite_area
**************************************************************************/
void canvas_fill_sprite_area(struct canvas *pcanvas,
			     struct sprite *psprite, struct color *pcolor,
			     int canvas_x, int canvas_y)
{
  funcs.canvas_fill_sprite_area(pcanvas, psprite, pcolor, canvas_x, canvas_y);
}

/**************************************************************************
  Call c++ canvas_fog_sprite_area
**************************************************************************/
void canvas_fog_sprite_area(struct canvas *pcanvas, struct sprite *psprite,
			    int canvas_x, int canvas_y)
{
  funcs.canvas_fog_sprite_area(pcanvas, psprite, canvas_x, canvas_y);
}

/**************************************************************************
  Call c++ canvas_put_line
**************************************************************************/
void canvas_put_line(struct canvas *pcanvas, struct color *pcolor,
                     enum line_type ltype, int start_x, int start_y,
                     int dx, int dy)
{
  funcs.canvas_put_line(pcanvas, pcolor, ltype, start_x, start_y, dx, dy);
}

/**************************************************************************
  Call c++ canvas_put_curved_line
**************************************************************************/
void canvas_put_curved_line(struct canvas *pcanvas, struct color *pcolor,
                            enum line_type ltype, int start_x, int start_y,
                            int dx, int dy)
{
  funcs.canvas_put_curved_line(pcanvas, pcolor, ltype, start_x, start_y,
                               dx, dy);
}

/**************************************************************************
  Call c++ get_text_size
**************************************************************************/
void get_text_size(int *width, int *height,
		   enum client_font font, const char *text)
{
  funcs.get_text_size(width, height, font, text);
}

/**************************************************************************
  Call c++ canvas_put_text
**************************************************************************/
void canvas_put_text(struct canvas *pcanvas, int canvas_x, int canvas_y,
		     enum client_font font, struct color *pcolor,
		     const char *text)
{
  funcs.canvas_put_text(pcanvas, canvas_x, canvas_y, font, pcolor, text);
}

/**************************************************************************
  Call c++ gui_set_rulesets
**************************************************************************/
void gui_set_rulesets(int num_rulesets, char **rulesets)
{
  funcs.gui_set_rulesets(num_rulesets, rulesets);
}

/**************************************************************************
  Call c++ gui_options_extra_init
**************************************************************************/
void gui_options_extra_init(void)
{
  funcs.gui_options_extra_init();
}

/**************************************************************************
  Call c++ gui_server_connect
**************************************************************************/
void gui_server_connect(void)
{
  funcs.gui_server_connect();
}

/**************************************************************************
  Call c++ add_net_input
**************************************************************************/
void add_net_input(int sock)
{
  funcs.add_net_input(sock);
}

/**************************************************************************
  Call c++ remove_net_input
**************************************************************************/
void remove_net_input(void)
{
  funcs.remove_net_input();
}

/**************************************************************************
  Call c++ real_conn_list_dialog_update
**************************************************************************/
void real_conn_list_dialog_update(void)
{
  funcs.real_conn_list_dialog_update();
}

/**************************************************************************
  Call c++ close_connection_dialog
**************************************************************************/
void close_connection_dialog()
{
  funcs.close_connection_dialog();
}

/**************************************************************************
  Call c++ add_idle_callback
**************************************************************************/
void add_idle_callback(void (callback)(void *), void *data)
{
  funcs.add_idle_callback(callback, data);
}

/**************************************************************************
  Call c++ sound_bell
**************************************************************************/
void sound_bell(void)
{
  funcs.sound_bell();
}

/**************************************************************************
  Call c++ real_set_client_page
**************************************************************************/
void real_set_client_page(enum client_pages page)
{
  funcs.real_set_client_page(page);
}

/**************************************************************************
  Call c++ get_current_client_page
**************************************************************************/
enum client_pages get_current_client_page(void)
{
  return funcs.get_current_client_page();
}

/**************************************************************************
  Call c++ set_unit_icon
**************************************************************************/
void set_unit_icon(int idx, struct unit *punit)
{
  funcs.set_unit_icon(idx, punit);
}

/**************************************************************************
  Call c++ real_focus_units_changed
**************************************************************************/
void real_focus_units_changed(void)
{
  funcs.real_focus_units_changed();
}

/**************************************************************************
  Call c++ set_unit_icons_more_arrow
**************************************************************************/
void set_unit_icons_more_arrow(bool onoff)
{
  funcs.set_unit_icons_more_arrow(onoff);
}

/****************************************************************************
  Call c++ gui_update_font
****************************************************************************/
void gui_update_font(const char *font_name, const char *font_value)
{
  funcs.gui_update_font(font_name, font_value);
}

/****************************************************************************
  Call c++ set_city_names_font_sizes
****************************************************************************/
void set_city_names_font_sizes(int my_city_names_font_size,
			       int my_city_productions_font_size)
{
  funcs.set_city_names_font_sizes(my_city_names_font_size,
                                  my_city_productions_font_size);
}

/****************************************************************************
  Call c++ editgui_refresh
****************************************************************************/
void editgui_refresh(void)
{
  funcs.editgui_refresh();
}

/****************************************************************************
  Call c++ editgui_notify_object_created
****************************************************************************/
void editgui_notify_object_created(int tag, int id)
{
  funcs.editgui_notify_object_created(tag, id);
}

/****************************************************************************
  Call c++ editgui_notify_object_changed
****************************************************************************/
void editgui_notify_object_changed(int objtype, int object_id, bool remove)
{
  funcs.editgui_notify_object_changed(objtype, object_id, remove);
}

/****************************************************************************
  Call c++ editgui_popup_properties
****************************************************************************/
void editgui_popup_properties(const struct tile_list *tiles, int objtype)
{
  funcs.editgui_popup_properties(tiles, objtype);
}

/****************************************************************************
  Call c++ editgui_tileset_changed
****************************************************************************/
void editgui_tileset_changed(void)
{
  funcs.editgui_tileset_changed();
}

/****************************************************************************
  Call c++ editgui_popdown_all
****************************************************************************/
void editgui_popdown_all(void)
{
  funcs.editgui_popdown_all();
}

/**************************************************************************
  Call c++ gui_ggz_embed_ensure_server
**************************************************************************/
void gui_ggz_embed_ensure_server(void)
{
  funcs.gui_ggz_embed_ensure_server();
}

/**************************************************************************
  Call c++ add_ggz_input
**************************************************************************/
void add_ggz_input(int sock)
{
  funcs.add_ggz_input(sock);
}

/**************************************************************************
  Call c++ remove_ggz_input
**************************************************************************/
void remove_ggz_input(void)
{
  funcs.remove_ggz_input();
}

/****************************************************************************
  Call c++ gui_ggz_embed_leave_table
****************************************************************************/
void gui_ggz_embed_leave_table(void)
{
  funcs.gui_ggz_embed_leave_table();
}

/****************************************************************************
  Call c++ update_timeout_label
****************************************************************************/
void update_timeout_label(void)
{
  funcs.update_timeout_label();
}

/**************************************************************************
  Call c++ real_city_dialog_popup
**************************************************************************/
void real_city_dialog_popup(struct city *pcity)
{
  funcs.real_city_dialog_popup(pcity);
}

/**************************************************************************
  Call c++ real_city_dialog_refresh
**************************************************************************/
void real_city_dialog_refresh(struct city *pcity)
{
  funcs.real_city_dialog_refresh(pcity);
}

/**************************************************************************
  Call c++ popdown_city_dialog
**************************************************************************/
void popdown_city_dialog(struct city *pcity)
{
  funcs.popdown_city_dialog(pcity);
}

/**************************************************************************
  Call c++ popdown_all_city_dialogs
**************************************************************************/
void popdown_all_city_dialogs()
{
  funcs.popdown_all_city_dialogs();
}

/**************************************************************************
  Call c++ refresh_unit_city_dialogs
**************************************************************************/
void refresh_unit_city_dialogs(struct unit *punit)
{
  funcs.refresh_unit_city_dialogs(punit);
}

/**************************************************************************
  Call c++ city_dialog_is_open
**************************************************************************/
bool city_dialog_is_open(struct city *pcity)
{
  return funcs.city_dialog_is_open(pcity);
}

/**************************************************************************
  Call c++ gui_load_theme
**************************************************************************/
void gui_load_theme(const char *directory, const char *theme_name)
{
  funcs.gui_load_theme(directory, theme_name);
}

/**************************************************************************
  Call c++ gui_clear_theme
**************************************************************************/
void gui_clear_theme(void)
{
  funcs.gui_clear_theme();
}

/**************************************************************************
  Call c++ get_gui_specific_themes_directories
**************************************************************************/
char **get_gui_specific_themes_directories(int *count)
{
  return funcs.get_gui_specific_themes_directories(count);
}

/**************************************************************************
  Call c++ get_useable_themes_in_directory
**************************************************************************/
char **get_useable_themes_in_directory(const char *directory, int *count)
{
  return funcs.get_useable_themes_in_directory(directory, count);
}
