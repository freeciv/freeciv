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


// client
#include "gui_interface.h"

#include "qtg_cxxside.h"

void setup_gui_funcs()
{
  struct gui_funcs *funcs = get_gui_funcs();

  funcs->ui_init = qtg_ui_init;
  funcs->ui_main = qtg_ui_main;
  funcs->ui_exit = qtg_ui_exit;

  funcs->get_gui_type = qtg_get_gui_type;

  funcs->real_output_window_append = qtg_real_output_window_append;

  funcs->isometric_view_supported = qtg_isometric_view_supported;
  funcs->overhead_view_supported = qtg_overhead_view_supported;
  funcs->free_intro_radar_sprites = qtg_free_intro_radar_sprites;
  funcs->load_gfxfile = qtg_load_gfxfile;
  funcs->create_sprite = qtg_create_sprite;
  funcs->get_sprite_dimensions = qtg_get_sprite_dimensions;
  funcs->crop_sprite = qtg_crop_sprite;
  funcs->free_sprite = qtg_free_sprite;

  funcs->color_alloc = qtg_color_alloc;
  funcs->color_free = qtg_color_free;

  funcs->canvas_create = qtg_canvas_create;
  funcs->canvas_free = qtg_canvas_free;
  funcs->canvas_copy = qtg_canvas_copy;
  funcs->canvas_put_sprite = qtg_canvas_put_sprite;
  funcs->canvas_put_sprite_full = qtg_canvas_put_sprite_full;
  funcs->canvas_put_sprite_fogged = qtg_canvas_put_sprite_fogged;
  funcs->canvas_put_rectangle = qtg_canvas_put_rectangle;
  funcs->canvas_fill_sprite_area = qtg_canvas_fill_sprite_area;
  funcs->canvas_fog_sprite_area = qtg_canvas_fog_sprite_area;
  funcs->canvas_put_line = qtg_canvas_put_line;
  funcs->canvas_put_curved_line = qtg_canvas_put_curved_line;
  funcs->get_text_size = qtg_get_text_size;
  funcs->canvas_put_text = qtg_canvas_put_text;

  funcs->gui_set_rulesets = qtg_gui_set_rulesets;
  funcs->gui_options_extra_init = qtg_gui_options_extra_init;
  funcs->gui_server_connect = qtg_gui_server_connect;
  funcs->add_net_input = qtg_add_net_input;
  funcs->remove_net_input = qtg_remove_net_input;
  funcs->real_conn_list_dialog_update = qtg_real_conn_list_dialog_update;
  funcs->close_connection_dialog = qtg_close_connection_dialog;
  funcs->add_idle_callback = qtg_add_idle_callback;
  funcs->sound_bell = qtg_sound_bell;

  funcs->real_set_client_page = qtg_real_set_client_page;
  funcs->get_current_client_page = qtg_get_current_client_page;

  funcs->set_unit_icon = qtg_set_unit_icon;
  funcs->set_unit_icons_more_arrow = qtg_set_unit_icons_more_arrow;
  funcs->real_focus_units_changed = qtg_real_focus_units_changed;
  funcs->gui_update_font = qtg_gui_update_font;
  funcs->set_city_names_font_sizes = qtg_set_city_names_font_sizes;

  funcs->editgui_refresh = qtg_editgui_refresh;
  funcs->editgui_notify_object_created = qtg_editgui_notify_object_created;
  funcs->editgui_notify_object_changed = qtg_editgui_notify_object_changed;
  funcs->editgui_popup_properties = qtg_editgui_popup_properties;
  funcs->editgui_tileset_changed = qtg_editgui_tileset_changed;
  funcs->editgui_popdown_all = qtg_editgui_popdown_all;

  funcs->gui_ggz_embed_ensure_server = qtg_gui_ggz_embed_ensure_server;
  funcs->gui_ggz_embed_leave_table = qtg_gui_ggz_embed_leave_table;
  funcs->add_ggz_input = qtg_add_ggz_input;
  funcs->remove_ggz_input = qtg_remove_ggz_input;

  funcs->update_timeout_label = qtg_update_timeout_label;
  funcs->real_city_dialog_popup = qtg_real_city_dialog_popup;
  funcs->real_city_dialog_refresh = qtg_real_city_dialog_refresh;
  funcs->popdown_city_dialog = qtg_popdown_city_dialog;
  funcs->popdown_all_city_dialogs = qtg_popdown_all_city_dialogs;
  funcs->refresh_unit_city_dialogs = qtg_refresh_unit_city_dialogs;
  funcs->city_dialog_is_open = qtg_city_dialog_is_open;

  funcs->gui_load_theme = qtg_gui_load_theme;
  funcs->gui_clear_theme = qtg_gui_clear_theme;
  funcs->get_gui_specific_themes_directories = qtg_get_gui_specific_themes_directories;
  funcs->get_useable_themes_in_directory = qtg_get_useable_themes_in_directory;
}
