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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "support.h"

/* common */
#include "game.h"
#include "government.h"
#include "map.h"
#include "packets.h"
#include "player.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "connectdlg_common.h"
#include "control.h"
#include "goto.h"
#include "options.h"
#include "packhand.h"
#include "text.h"
#include "tilespec.h"

/* client/gui-gtk-3.0 */
#include "chatline.h"
#include "choice_dialog.h"
#include "citydlg.h"
#include "editprop.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "plrdlg.h"
#include "wldlg.h"
#include "unitselect.h"

#include "dialogs.h"

/******************************************************************/
static GtkWidget  *races_shell;
struct player *races_player;
static GtkWidget  *races_nation_list[MAX_NUM_NATION_GROUPS + 1];
static GtkWidget  *races_leader;
static GtkWidget  *races_sex[2];
static GtkWidget  *races_city_style_list;
static GtkTextBuffer *races_text;

static void create_races_dialog(struct player *pplayer);
static void races_destroy_callback(GtkWidget *w, gpointer data);
static void races_response(GtkWidget *w, gint response, gpointer data);
static void races_nation_callback(GtkTreeSelection *select, gpointer data);
static void races_leader_callback(void);
static void races_sex_callback(GtkWidget *w, gpointer data);
static void races_city_style_callback(GtkTreeSelection *select, gpointer data);
static gboolean races_selection_func(GtkTreeSelection *select,
				     GtkTreeModel *model, GtkTreePath *path,
				     gboolean selected, gpointer data);

static int selected_nation;
static int selected_sex;
static int selected_city_style;

static int is_showing_pillage_dialog = FALSE;
static int unit_to_use_to_pillage;

/**************************************************************************
  Popup a generic dialog to display some generic information.
**************************************************************************/
void popup_notify_dialog(const char *caption, const char *headline,
			 const char *lines)
{
  static struct gui_dialog *shell;
  GtkWidget *vbox, *label, *headline_label, *sw;

  gui_dialog_new(&shell, GTK_NOTEBOOK(bottom_notebook), NULL);
  gui_dialog_set_title(shell, caption);

  gui_dialog_add_button(shell, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE); 
  gui_dialog_set_default_response(shell, GTK_RESPONSE_CLOSE);

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(shell->vbox), vbox, TRUE, TRUE, 0);

  headline_label = gtk_label_new(headline);   
  gtk_box_pack_start(GTK_BOX(vbox), headline_label, FALSE, FALSE, 0);
  gtk_widget_set_name(headline_label, "notify_label");

  gtk_label_set_justify(GTK_LABEL(headline_label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment(GTK_MISC(headline_label), 0.0, 0.0);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  label = gtk_label_new(lines);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(sw), label);

  gtk_widget_set_name(label, "notify_label");
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);

  gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

  gui_dialog_show_all(shell);

  gui_dialog_set_default_size(shell, -1, 265);
  gui_dialog_present(shell);

  shell = NULL;
}

/****************************************************************
  User has responded to notify dialog with possibility to
  center (goto) on event location.
*****************************************************************/
static void notify_goto_response(GtkWidget *w, gint response)
{
  struct city *pcity = NULL;
  struct tile *ptile = g_object_get_data(G_OBJECT(w), "tile");

  switch (response) {
  case 1:
    center_tile_mapcanvas(ptile);
    break;
  case 2:
    pcity = tile_city(ptile);

    if (center_when_popup_city) {
      center_tile_mapcanvas(ptile);
    }

    if (pcity) {
      popup_city_dialog(pcity);
    }
    break;
  }
  gtk_widget_destroy(w);
}

/****************************************************************
  User clicked close for connect message dialog
*****************************************************************/
static void notify_connect_msg_response(GtkWidget *w, gint response)
{
  gtk_widget_destroy(w);
}

/**************************************************************************
  Popup a dialog to display information about an event that has a
  specific location.  The user should be given the option to goto that
  location.
**************************************************************************/
void popup_notify_goto_dialog(const char *headline, const char *lines,
                              const struct text_tag_list *tags,
                              struct tile *ptile)
{
  GtkWidget *shell, *label, *goto_command, *popcity_command;
  
  shell = gtk_dialog_new_with_buttons(headline,
        NULL,
        0,
        NULL);
  setup_dialog(shell, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_CLOSE);
  gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_CENTER_ON_PARENT);

  label = gtk_label_new(lines);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), label);
  gtk_widget_show(label);
  
  goto_command = gtk_stockbutton_new(GTK_STOCK_JUMP_TO,
	_("Goto _Location"));
  gtk_dialog_add_action_widget(GTK_DIALOG(shell), goto_command, 1);
  gtk_widget_show(goto_command);

  popcity_command = gtk_stockbutton_new(GTK_STOCK_ZOOM_IN,
	_("I_nspect City"));
  gtk_dialog_add_action_widget(GTK_DIALOG(shell), popcity_command, 2);
  gtk_widget_show(popcity_command);

  gtk_dialog_add_button(GTK_DIALOG(shell), GTK_STOCK_CLOSE,
			GTK_RESPONSE_CLOSE);

  if (!ptile) {
    gtk_widget_set_sensitive(goto_command, FALSE);
    gtk_widget_set_sensitive(popcity_command, FALSE);
  } else {
    struct city *pcity;

    pcity = tile_city(ptile);
    gtk_widget_set_sensitive(popcity_command,
      (NULL != pcity && city_owner(pcity) == client.conn.playing));
  }

  g_object_set_data(G_OBJECT(shell), "tile", ptile);

  g_signal_connect(shell, "response", G_CALLBACK(notify_goto_response), NULL);
  gtk_widget_show(shell);
}

/**************************************************************************
  Popup a dialog to display connection message from server.
**************************************************************************/
void popup_connect_msg(const char *headline, const char *message)
{
  GtkWidget *shell, *label;
  
  shell = gtk_dialog_new_with_buttons(headline,
        NULL,
        0,
        NULL);
  setup_dialog(shell, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_CLOSE);
  gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_CENTER_ON_PARENT);

  label = gtk_label_new(message);
  gtk_label_set_selectable(GTK_LABEL(label), 1);

  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), label);
  gtk_widget_show(label);

  gtk_dialog_add_button(GTK_DIALOG(shell), GTK_STOCK_CLOSE,
			GTK_RESPONSE_CLOSE);

  g_signal_connect(shell, "response", G_CALLBACK(notify_connect_msg_response),
                   NULL);
  gtk_widget_show(shell);
}

/****************************************************************
  User has responded to revolution dialog
*****************************************************************/
static void revolution_response(GtkWidget *w, gint response, gpointer data)
{
  struct government *government = data;

  if (response == GTK_RESPONSE_YES) {
    if (!government) {
      start_revolution();
    } else {
      set_government_choice(government);
    }
  }
  if (w) {
    gtk_widget_destroy(w);
  }
}

/****************************************************************
  Popup revolution dialog for user
*****************************************************************/
void popup_revolution_dialog(struct government *government)
{
  static GtkWidget *shell = NULL;

  if (0 > client.conn.playing->revolution_finishes) {
    if (!shell) {
      shell = gtk_message_dialog_new(NULL,
	  0,
	  GTK_MESSAGE_WARNING,
	  GTK_BUTTONS_YES_NO,
	  _("You say you wanna revolution?"));
      gtk_window_set_title(GTK_WINDOW(shell), _("Revolution!"));
      setup_dialog(shell, toplevel);

      g_signal_connect(shell, "destroy",
	  G_CALLBACK(gtk_widget_destroyed), &shell);
    }
    g_signal_connect(shell, "response",
	G_CALLBACK(revolution_response), government);

    gtk_window_present(GTK_WINDOW(shell));
  } else {
    revolution_response(shell, GTK_RESPONSE_YES, government);
  }
}


/***********************************************************************
  NB: 'data' is a value of enum tile_special_type casted to a pointer.
***********************************************************************/
static void pillage_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  int what = GPOINTER_TO_INT(data);

  punit = game_unit_by_number(unit_to_use_to_pillage);
  if (punit) {
    Base_type_id pillage_base = -1;

    if (what > S_LAST) {
      pillage_base = what - S_LAST - 1;
      what = S_LAST;
    }

    request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE,
                                       what, pillage_base);
  }
}

/****************************************************************
  Pillage dialog destroyed
*****************************************************************/
static void pillage_destroy_callback(GtkWidget *w, gpointer data)
{
  is_showing_pillage_dialog = FALSE;
}

/****************************************************************
  Opens pillage dialog listing possible pillage targets.
*****************************************************************/
void popup_pillage_dialog(struct unit *punit,
			  bv_special may_pillage,
                          bv_bases bases)
{
  GtkWidget *shl;
  int what;

  if (!is_showing_pillage_dialog) {
    is_showing_pillage_dialog = TRUE;
    unit_to_use_to_pillage = punit->id;

    shl = choice_dialog_start(GTK_WINDOW(toplevel),
			       _("What To Pillage"),
			       _("Select what to pillage:"));

    while ((what = get_preferred_pillage(may_pillage, bases)) != S_LAST) {
      bv_special what_bv;
      bv_bases what_base;

      BV_CLR_ALL(what_bv);
      BV_CLR_ALL(what_base);

      if (what > S_LAST) {
        BV_SET(what_base, what % (S_LAST + 1));
      } else {
        BV_SET(what_bv, what);
      }

      choice_dialog_add(shl, get_infrastructure_text(what_bv, what_base),
                        G_CALLBACK(pillage_callback), GINT_TO_POINTER(what));

      if (what > S_LAST) {
        BV_CLR(bases, what % (S_LAST + 1));
      } else {
        clear_special(&may_pillage, what);
      }
    }

    choice_dialog_add(shl, GTK_STOCK_CANCEL, 0, 0);

    choice_dialog_end(shl);

    g_signal_connect(shl, "destroy", G_CALLBACK(pillage_destroy_callback),
		     NULL);   
  }
}

/*****************************************************************************
  Popup unit selection dialog. It is a wrapper for the main function; see
  unitselect.c:unit_select_dialog_popup_main().
*****************************************************************************/
void unit_select_dialog_popup(struct tile *ptile)
{
  unit_select_dialog_popup_main(ptile, TRUE);
}

/*****************************************************************************
  Update unit selection dialog. It is a wrapper for the main function; see
  unitselect.c:unit_select_dialog_popup_main().
*****************************************************************************/
void unit_select_dialog_update_real(void)
{
  unit_select_dialog_popup_main(NULL, FALSE);
}

/****************************************************************
  NATION SELECTION DIALOG
****************************************************************/
/****************************************************************
  Creates a list of nation of given group
  Inserts apropriate gtk_tree_view into races_nation_list[i]
  If group == NULL, create a list of all nations
+****************************************************************/
static GtkWidget* create_list_of_nations_in_group(struct nation_group* group,
						  int index)
{
  GtkWidget *sw;
  GtkListStore *store;
  GtkWidget *list;
  GtkTreeSelection *select;
  GtkCellRenderer *render;
  GtkTreeViewColumn *column;

  store = gtk_list_store_new(5, G_TYPE_INT, G_TYPE_BOOLEAN,
      GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING);
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),
      3, GTK_SORT_ASCENDING);

  list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_tree_view_set_search_column(GTK_TREE_VIEW(list), 3);
  races_nation_list[index] = list;
  g_object_unref(store);

  select = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
  g_signal_connect(select, "changed", G_CALLBACK(races_nation_callback), NULL);
  gtk_tree_selection_set_select_function(select, races_selection_func,
      NULL, NULL);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
      GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(sw), list);
 
  render = gtk_cell_renderer_pixbuf_new();
  column = gtk_tree_view_column_new_with_attributes(_("Flag"), render,
      "pixbuf", 2, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
  render = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(_("Nation"), render,
      "text", 3, "strikethrough", 1, NULL);
  gtk_tree_view_column_set_sort_column_id(column, 3);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
  render = gtk_cell_renderer_text_new();
  g_object_set(render, "style", PANGO_STYLE_ITALIC, NULL);

  /* Populate nation list store. */
  nations_iterate(pnation) {
    bool used;
    GdkPixbuf *img;
    GtkTreeIter it;
    GValue value = { 0, };

    if (!is_nation_playable(pnation) || !pnation->is_available) {
      continue;
    }

    if (NULL != group && !nation_is_in_group(pnation, group)) {
      continue;
    }

    gtk_list_store_append(store, &it);

    used = (pnation->player != NULL && pnation->player != races_player);
    gtk_list_store_set(store, &it, 0, nation_number(pnation), 1, used, -1);
    img = get_flag(pnation);
    if (img != NULL) {
      gtk_list_store_set(store, &it, 2, img, -1);
      g_object_unref(img);
    }
    if (pnation->player == races_player) {
      /* FIXME: should select this one by default. */
    }

    g_value_init(&value, G_TYPE_STRING);
    g_value_set_static_string(&value, nation_adjective_translation(pnation));
    gtk_list_store_set_value(store, &it, 3, &value);
    g_value_unset(&value);
  } nations_iterate_end;
  return sw;
}

/****************************************************************
  Creates left side of nation selection dialog
****************************************************************/
static GtkWidget* create_nation_selection_list(void)
{
  GtkWidget *vbox;
  GtkWidget *notebook;
  
  GtkWidget *label;
  GtkWidget *nation_list;
  GtkWidget *group_name_label;
  
  int i;
  
  vbox = gtk_vbox_new(FALSE, 2);
  
  nation_list = create_list_of_nations_in_group(NULL, 0);  
  label = g_object_new(GTK_TYPE_LABEL,
      "use-underline", TRUE,
      "mnemonic-widget", nation_list,
      "label", _("Nation _Groups:"),
      "xalign", 0.0,
      "yalign", 0.5,
      NULL);
  notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_LEFT);  
  
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);  
  gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);
  
  for (i = 1; i <= nation_group_count(); i++) {
    struct nation_group* group = (nation_group_by_number(i - 1));
    nation_list = create_list_of_nations_in_group(group, i);
    group_name_label = gtk_label_new(nation_group_name_translation(group));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), nation_list, group_name_label);
  }
  
  nation_list = create_list_of_nations_in_group(NULL, 0);
  group_name_label = gtk_label_new(_("All"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), nation_list, group_name_label);

  return vbox;
}


/****************************************************************
  Create nations dialog
*****************************************************************/
static void create_races_dialog(struct player *pplayer)
{
  GtkWidget *shell;
  GtkWidget *cmd;
  GtkWidget *vbox, *hbox, *table;
  GtkWidget *frame, *label, *combo;
  GtkWidget *text;
  GtkWidget *notebook;
  GtkWidget* nation_selection_list;
  
  
  GtkWidget *sw;
  GtkWidget *list;  
  GtkListStore *store;
  GtkCellRenderer *render;
  GtkTreeViewColumn *column;
  
  int i;
  char *title;

  if (C_S_RUNNING == client_state()) {
    title = _("Edit Nation");
  } else if (NULL != pplayer && pplayer == client.conn.playing) {
    title = _("What Nation Will You Be?");
  } else {
    title = _("Pick Nation");
  }

  shell = gtk_dialog_new_with_buttons(title,
				      NULL,
				      0,
				      GTK_STOCK_CANCEL,
				      GTK_RESPONSE_CANCEL,
				      _("_Random Nation"),
				      GTK_RESPONSE_NO, /* arbitrary */
				      GTK_STOCK_OK,
				      GTK_RESPONSE_ACCEPT,
				      NULL);
  races_shell = shell;
  races_player = pplayer;
  setup_dialog(shell, toplevel);

  gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_window_set_default_size(GTK_WINDOW(shell), -1, 590);

  frame = gtk_frame_new(_("Select a nation"));
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), frame);

  hbox = gtk_hbox_new(FALSE, 18);
  gtk_container_set_border_width(GTK_CONTAINER(hbox), 3);
  gtk_container_add(GTK_CONTAINER(frame), hbox);

  /* Nation list */
  nation_selection_list = create_nation_selection_list();
  gtk_container_add(GTK_CONTAINER(hbox), nation_selection_list);


  /* Right side. */
  notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_BOTTOM);
  gtk_container_add(GTK_CONTAINER(hbox), notebook);

  /* Properties pane. */
  label = gtk_label_new_with_mnemonic(_("_Properties"));

  vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox, label);

  table = gtk_table_new(3, 4, FALSE); 
  gtk_table_set_row_spacings(GTK_TABLE(table), 2);
  gtk_table_set_col_spacing(GTK_TABLE(table), 0, 12);
  gtk_table_set_col_spacing(GTK_TABLE(table), 1, 12);
  gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

  /* Leader. */ 
  {
    GtkListStore *model = gtk_list_store_new(1, G_TYPE_STRING);
    combo = gtk_combo_box_entry_new_with_model(GTK_TREE_MODEL(model), 0);
    g_object_unref(G_OBJECT(model));
  }
  races_leader = combo;
  label = g_object_new(GTK_TYPE_LABEL,
      "use-underline", TRUE,
      "mnemonic-widget", GTK_COMBO_BOX(combo),
      "label", _("_Leader:"),
      "xalign", 0.0,
      "yalign", 0.5,
      NULL);
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 2, 0, 0, 0, 0);
  gtk_table_attach(GTK_TABLE(table), combo, 1, 3, 0, 1, 0, 0, 0, 0);

  cmd = gtk_radio_button_new_with_mnemonic(NULL, _("_Female"));
  races_sex[0] = cmd;
  gtk_table_attach(GTK_TABLE(table), cmd, 1, 2, 1, 2, 0, 0, 0, 0);

  cmd = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(cmd),
      _("_Male"));
  races_sex[1] = cmd;
  gtk_table_attach(GTK_TABLE(table), cmd, 2, 3, 1, 2, 0, 0, 0, 0);

  /* City style. */
  store = gtk_list_store_new(3, G_TYPE_INT,
      GDK_TYPE_PIXBUF, G_TYPE_STRING);

  list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  races_city_style_list = list;
  g_object_unref(store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);
  g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), "changed",
      G_CALLBACK(races_city_style_callback), NULL);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
      GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(sw), list);
  gtk_table_attach(GTK_TABLE(table), sw, 1, 3, 2, 4,
      GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);

  label = g_object_new(GTK_TYPE_LABEL,
      "use-underline", TRUE,
      "mnemonic-widget", list,
      "label", _("City _Styles:"),
      "xalign", 0.0,
      "yalign", 0.5,
      NULL);
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3, 0, 0, 0, 0);

  render = gtk_cell_renderer_pixbuf_new();
  column = gtk_tree_view_column_new_with_attributes(NULL, render,
      "pixbuf", 1, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
  render = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(NULL, render,
      "text", 2, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  gtk_table_set_row_spacing(GTK_TABLE(table), 1, 12);

  /* Populate city style store. */
  for (i = 0; i < game.control.styles_count; i++) {
    GdkPixbuf *img;
    struct sprite *s;
    GtkTreeIter it;

    if (city_style_has_requirements(&city_styles[i])) {
      continue;
    }

    gtk_list_store_append(store, &it);

    s = crop_blankspace(get_sample_city_sprite(tileset, i));
    img = sprite_get_pixbuf(s);
    g_object_ref(img);
    free_sprite(s);
    gtk_list_store_set(store, &it, 0, i, 1, img, 2,
                       city_style_name_translation(i), -1);
    g_object_unref(img);
  }

  /* Legend pane. */
  label = gtk_label_new_with_mnemonic(_("_Description"));

  vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox, label);

  text = gtk_text_view_new();
  races_text = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), FALSE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 6);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text), 6);

  gtk_box_pack_start(GTK_BOX(vbox), text, TRUE, TRUE, 0);

  /* Signals. */
  g_signal_connect(shell, "destroy",
      G_CALLBACK(races_destroy_callback), NULL);
  g_signal_connect(shell, "response",
      G_CALLBACK(races_response), NULL);

  g_signal_connect(GTK_COMBO_BOX(races_leader), "changed",
      G_CALLBACK(races_leader_callback), NULL);

  g_signal_connect(races_sex[0], "toggled",
      G_CALLBACK(races_sex_callback), GINT_TO_POINTER(0));
  g_signal_connect(races_sex[1], "toggled",
      G_CALLBACK(races_sex_callback), GINT_TO_POINTER(1));

  /* Init. */
  selected_nation = -1;

  /* Finish up. */
  gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_CANCEL);

  /* Don't allow ok without a selection */
  gtk_dialog_set_response_sensitive(GTK_DIALOG(shell), GTK_RESPONSE_ACCEPT,
                                    FALSE);                                          
  /* You can't assign NO_NATION during a running game. */
  if (C_S_RUNNING == client_state()) {
    gtk_dialog_set_response_sensitive(GTK_DIALOG(shell), GTK_RESPONSE_NO,
                                      FALSE);
  }

  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
}

/****************************************************************
  popup the dialog 10% inside the main-window 
 *****************************************************************/
void popup_races_dialog(struct player *pplayer)
{
  if (!pplayer) {
    return;
  }

  if (!races_shell) {
    create_races_dialog(pplayer);
    gtk_window_present(GTK_WINDOW(races_shell));
  }
}

/****************************************************************
  Close nations dialog  
*****************************************************************/
void popdown_races_dialog(void)
{
  if (races_shell) {
    gtk_widget_destroy(races_shell);
  }

  /* We're probably starting a new game, maybe with a new ruleset.
     So we warn the worklist dialog. */
  blank_max_unit_size();
}


/****************************************************************
  Nations dialog has been destroyed
*****************************************************************/
static void races_destroy_callback(GtkWidget *w, gpointer data)
{
  races_shell = NULL;
}

/****************************************************************
  Populates leader list.
*****************************************************************/
static void populate_leader_list(void)
{
  int i;
  int idx;
  GtkListStore *model =
      GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(races_leader)));

  i = 0;
  gtk_list_store_clear(model);
  nation_leader_list_iterate(nation_leaders(nation_by_number
                                            (selected_nation)), pleader) {
    const char *leader_name = nation_leader_name(pleader);
    GtkTreeIter iter; /* unused */

    gtk_list_store_insert_with_values(model, &iter, i, 0, leader_name, -1);
    i++;
  } nation_leader_list_iterate_end;

  idx = fc_rand(i);

  gtk_combo_box_set_active(GTK_COMBO_BOX(races_leader), idx);
}

/**************************************************************************
  Set sensitivity of nations to select.
**************************************************************************/
void races_toggles_set_sensitive(void)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  GtkTreePath *path;
  gboolean chosen;
  int i;

  if (!races_shell) {
    return;
  }

  for (i = 0; i <= nation_group_count(); i++) {
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(races_nation_list[i]));
    if (gtk_tree_model_get_iter_first(model, &it)) {
      do {
        int nation_no;
	struct nation_type *nation;

        gtk_tree_model_get(model, &it, 0, &nation_no, -1);
	nation = nation_by_number(nation_no);

        chosen = !nation->is_available || nation->player;

        gtk_list_store_set(GTK_LIST_STORE(model), &it, 1, chosen, -1);

      } while (gtk_tree_model_iter_next(model, &it));
    }
  }

  for (i = 0; i <= nation_group_count(); i++) {
    gtk_tree_view_get_cursor(GTK_TREE_VIEW(races_nation_list[i]), &path, NULL);
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(races_nation_list[i]));    
    if (path) {
      gtk_tree_model_get_iter(model, &it, path);
      gtk_tree_model_get(model, &it, 1, &chosen, -1);

      if (chosen) {
          GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(races_nation_list[i]));
	  gtk_tree_selection_unselect_all(select);
      }

      gtk_tree_path_free(path);
    }
  }
}

/**************************************************************************
  Called whenever a user selects a nation in nation list
 **************************************************************************/
static void races_nation_callback(GtkTreeSelection *select, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(select, &model, &it)) {
    gboolean chosen;
    struct nation_type *nation;

    gtk_tree_model_get(model, &it, 0, &selected_nation, 1, &chosen, -1);
    nation = nation_by_number(selected_nation);

    if (!chosen) {
      int cs, i, j;
      GtkTreePath *path;
     

      /* Unselect other nations in other pages 
       * This can set selected_nation to -1, so we have to copy it
       */
      int selected_nation_copy = selected_nation;      
      for (i = 0; i <= nation_group_count(); i++) {
        gtk_tree_view_get_cursor(GTK_TREE_VIEW(races_nation_list[i]), &path, NULL);
        model = gtk_tree_view_get_model(GTK_TREE_VIEW(races_nation_list[i]));    
        if (path) {
          int other_nation;
          gtk_tree_model_get_iter(model, &it, path);
          gtk_tree_model_get(model, &it, 0, &other_nation, -1);
          if (other_nation != selected_nation_copy) {
            GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(races_nation_list[i]));
            gtk_tree_selection_unselect_all(select);
          }

          gtk_tree_path_free(path);
        }
      }
      selected_nation = selected_nation_copy;

      populate_leader_list();

      /* Select city style for chosen nation. */
      cs = city_style_of_nation(nation_by_number(selected_nation));
      for (i = 0, j = 0; i < game.control.styles_count; i++) {
        if (city_style_has_requirements(&city_styles[i])) {
	  continue;
	}

	if (i < cs) {
	  j++;
	} else {
	  break;
	}
      }

      path = gtk_tree_path_new();
      gtk_tree_path_append_index(path, j);
      gtk_tree_view_set_cursor(GTK_TREE_VIEW(races_city_style_list), path,
			       NULL, FALSE);
      gtk_tree_path_free(path);

      /* Update nation legend text. */
      gtk_text_buffer_set_text(races_text, nation->legend , -1);
    }

    /* Once we've made a selection, allow user to ok */
    gtk_dialog_set_response_sensitive(GTK_DIALOG(races_shell), 
                                      GTK_RESPONSE_ACCEPT, TRUE);
  } else {
    selected_nation = -1;
  }
}

/**************************************************************************
  Leader name has been chosen
**************************************************************************/
static void races_leader_callback(void)
{
  const struct nation_leader *pleader;
  const gchar *name;

  name =
    gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(races_leader))));

  if (selected_nation != -1
      &&(pleader = nation_leader_by_name(nation_by_number(selected_nation),
                                         name))) {
    selected_sex = nation_leader_is_male(pleader);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(races_sex[selected_sex]),
                                 TRUE);
  }
}

/**************************************************************************
  Leader sex has been chosen
**************************************************************************/
static void races_sex_callback(GtkWidget *w, gpointer data)
{
  selected_sex = GPOINTER_TO_INT(data);
}

/**************************************************************************
  Nation has been selected
**************************************************************************/
static gboolean races_selection_func(GtkTreeSelection *select,
				     GtkTreeModel *model, GtkTreePath *path,
				     gboolean selected, gpointer data)
{
  GtkTreeIter it;
  gboolean chosen;

  gtk_tree_model_get_iter(model, &it, path);
  gtk_tree_model_get(model, &it, 1, &chosen, -1);
  return (!chosen || selected);
}

/**************************************************************************
  City style has been chosen
**************************************************************************/
static void races_city_style_callback(GtkTreeSelection *select, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(select, &model, &it)) {
    gtk_tree_model_get(model, &it, 0, &selected_city_style, -1);
  } else {
    selected_city_style = -1;
  }
}

/**************************************************************************
  User has selected some of the responses for whole nations dialog
**************************************************************************/
static void races_response(GtkWidget *w, gint response, gpointer data)
{
  if (response == GTK_RESPONSE_ACCEPT) {
    const char *s;

    /* This shouldn't be possible but... */
    if (selected_nation == -1) {
      return;
    }

    if (selected_sex == -1) {
      output_window_append(ftc_client, _("You must select your sex."));
      return;
    }

    if (selected_city_style == -1) {
      output_window_append(ftc_client, _("You must select your city style."));
      return;
    }

    s = gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(races_leader))));

    /* Perform a minimum of sanity test on the name. */
    /* This could call is_allowed_player_name if it were available. */
    if (strlen(s) == 0) {
      output_window_append(ftc_client, _("You must type a legal name."));
      return;
    }

    dsend_packet_nation_select_req(&client.conn,
				   player_number(races_player), selected_nation,
				   selected_sex, s, selected_city_style);
  } else if (response == GTK_RESPONSE_NO) {
    dsend_packet_nation_select_req(&client.conn,
				   player_number(races_player),
				   -1, FALSE, "", 0);
  }

  popdown_races_dialog();
}


/**************************************************************************
  Adjust tax rates from main window
**************************************************************************/
gboolean taxrates_callback(GtkWidget * w, GdkEventButton * ev, gpointer data)
{
  common_taxrates_callback((size_t) data);
  return TRUE;
}

/****************************************************************************
  Pops up a dialog to confirm upgrading of the unit.
****************************************************************************/
void popup_upgrade_dialog(struct unit_list *punits)
{
  GtkWidget *shell;
  char buf[512];

  if (!punits || unit_list_size(punits) == 0) {
    return;
  }

  if (!get_units_upgrade_info(buf, sizeof(buf), punits)) {
    shell = gtk_message_dialog_new(NULL, 0,
				   GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
				   "%s", buf);
    gtk_window_set_title(GTK_WINDOW(shell), _("Upgrade Unit!"));
    setup_dialog(shell, toplevel);
    g_signal_connect(shell, "response", G_CALLBACK(gtk_widget_destroy),
                    NULL);
    gtk_window_present(GTK_WINDOW(shell));
  } else {
    shell = gtk_message_dialog_new(NULL, 0,
				   GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				   "%s", buf);
    gtk_window_set_title(GTK_WINDOW(shell), _("Upgrade Obsolete Units"));
    setup_dialog(shell, toplevel);
    gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_YES);

    if (gtk_dialog_run(GTK_DIALOG(shell)) == GTK_RESPONSE_YES) {
      unit_list_iterate(punits, punit) {
	request_unit_upgrade(punit);
      } unit_list_iterate_end;
    }
    gtk_widget_destroy(shell);
  }
}

/********************************************************************** 
  This function is called when the client disconnects or the game is
  over.  It should close all dialog windows for that game.
***********************************************************************/
void popdown_all_game_dialogs(void)
{
  gui_dialog_destroy_all();
  property_editor_popdown(editprop_get_property_editor());
  unit_select_dialog_popdown();
}
