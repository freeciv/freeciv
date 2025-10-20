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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdlib.h>

#include <gtk/gtk.h>

/* utility */
#include "log.h"
#include "mem.h"
#include "string_vector.h"

/* client */
#include "options.h"

/* client/gui-gtk-5.0 */
#include "colors.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "pages.h"

#include "optiondlg.h"


/* The option dialog data. */
struct option_dialog {
  const struct option_set *poptset;     /* The option set. */
  GtkWidget *shell;                     /* The main widget. */
  GtkWidget *notebook;                  /* The notebook. */
  GtkWidget **vboxes;                   /* Category boxes. */
  int *box_children;                    /* The number of children for
                                         * each category. */
};

#define SPECLIST_TAG option_dialog
#define SPECLIST_TYPE struct option_dialog
#include "speclist.h"
#define option_dialogs_iterate(pdialog) \
  TYPED_LIST_ITERATE(struct option_dialog, option_dialogs, pdialog)
#define option_dialogs_iterate_end  LIST_ITERATE_END

/* All option dialog are set on this list. */
static struct option_dialog_list *option_dialogs = NULL;

enum {
  RESPONSE_CANCEL,
  RESPONSE_OK,
  RESPONSE_APPLY,
  RESPONSE_RESET,
  RESPONSE_REFRESH,
  RESPONSE_SAVE
};

static GtkWidget *opt_popover = NULL;


/* Option dialog main functions. */
static struct option_dialog *
option_dialog_get(const struct option_set *poptset);
static struct option_dialog *
option_dialog_new(const char *name, const struct option_set *poptset);
static void option_dialog_destroy(struct option_dialog *pdialog);

static void option_dialog_reorder_notebook(struct option_dialog *pdialog);
static inline void option_dialog_foreach(struct option_dialog *pdialog,
                                         void (*option_action)
                                         (struct option *));

/* Option dialog option-specific functions. */
static void option_dialog_option_add(struct option_dialog *pdialog,
                                     struct option *poption,
                                     bool reorder_notebook);
static void option_dialog_option_remove(struct option_dialog *pdialog,
                                        struct option *poption);

static void option_dialog_option_refresh(struct option *poption);
static void option_dialog_option_reset(struct option *poption);
static void option_dialog_option_apply(struct option *poption);


/************************************************************************//**
  Option dialog widget response callback.
****************************************************************************/
static void option_dialog_reponse_callback(GtkDialog *dialog,
                                           gint response_id, gpointer data)
{
  struct option_dialog *pdialog = (struct option_dialog *) data;

  switch (response_id) {
  case RESPONSE_CANCEL:
    gtk_window_destroy(GTK_WINDOW(dialog));
    break;
  case RESPONSE_OK:
    option_dialog_foreach(pdialog, option_dialog_option_apply);
    gtk_window_destroy(GTK_WINDOW(dialog));
    break;
  case RESPONSE_APPLY:
    option_dialog_foreach(pdialog, option_dialog_option_apply);
    break;
  case RESPONSE_RESET:
    option_dialog_foreach(pdialog, option_dialog_option_reset);
    break;
  case RESPONSE_REFRESH:
    option_dialog_foreach(pdialog, option_dialog_option_refresh);
    break;
  case RESPONSE_SAVE:
    option_dialog_foreach(pdialog, option_dialog_option_apply);
    queue_options_save(NULL);
    break;
  }
}

/************************************************************************//**
  Option dialog widget destroyed callback.
****************************************************************************/
static void option_dialog_destroy_callback(GtkWidget *object, gpointer data)
{
  struct option_dialog *pdialog = (struct option_dialog *) data;

  /* Save size of the dialog. */
  gtk_window_get_default_size(GTK_WINDOW(object),
                              &GUI_GTK_OPTION(optionsdlg_xsize),
                              &GUI_GTK_OPTION(optionsdlg_ysize));

  if (NULL != pdialog->shell) {
    /* Mark as destroyed, see also option_dialog_destroy(). */
    pdialog->shell = NULL;
    option_dialog_destroy(pdialog);
  }
}

/************************************************************************//**
  Option refresh requested from menu.
****************************************************************************/
static void option_refresh_callback(GSimpleAction *action, GVariant *parameter,
                                    gpointer data)
{
  struct option *poption = (struct option *)data;
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_refresh(poption);
  }

  gtk_widget_unparent(opt_popover);
  g_object_unref(opt_popover);
}

/************************************************************************//**
  Option reset requested from menu.
****************************************************************************/
static void option_reset_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  struct option *poption = (struct option *) data;
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_reset(poption);
  }

  gtk_widget_unparent(opt_popover);
  g_object_unref(opt_popover);
}

/************************************************************************//**
  Option apply requested from menu.
****************************************************************************/
static void option_apply_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  struct option *poption = (struct option *) data;
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_apply(poption);
  }

  gtk_widget_unparent(opt_popover);
  g_object_unref(opt_popover);
}

/************************************************************************//**
  Called when a button is pressed on an option.
****************************************************************************/
static gboolean option_button_press_callback(GtkGestureClick *gesture,
                                             int n_press,
                                             double x, double y,
                                             gpointer data)
{
  struct option *poption = (struct option *) data;
  GtkEventController *controller = GTK_EVENT_CONTROLLER(gesture);
  GtkWidget *parent = gtk_event_controller_get_widget(controller);
  GMenu *menu;
  GActionGroup *group;
  GSimpleAction *act;
  GdkRectangle rect = { .x = x, .y = y, .width = 1, .height = 1};

  if (!option_is_changeable(poption)) {
    return FALSE;
  }

  group = G_ACTION_GROUP(g_simple_action_group_new());

  menu = g_menu_new();

  act = g_simple_action_new("refresh", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(option_refresh_callback), poption);
  menu_item_append_unref(menu, g_menu_item_new(_("Refresh this option"),
                                               "win.refresh"));

  act = g_simple_action_new("unit_reset", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(option_reset_callback), poption);
  menu_item_append_unref(menu, g_menu_item_new(_("Reset this option"),
                                               "win.unit_reset"));

  act = g_simple_action_new("units_apply", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(option_apply_callback), poption);
  menu_item_append_unref(menu,
                         g_menu_item_new(_("Apply the changes for this option"),
                                         "win.units_apply"));

  opt_popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
  g_object_ref(opt_popover);
  gtk_widget_insert_action_group(opt_popover, "win", group);

  gtk_widget_set_parent(opt_popover, parent);
  gtk_popover_set_pointing_to(GTK_POPOVER(opt_popover), &rect);

  gtk_popover_popup(GTK_POPOVER(opt_popover));

  return TRUE;
}

/************************************************************************//**
  Returns the option dialog which fit the option set.
****************************************************************************/
static struct option_dialog *
option_dialog_get(const struct option_set *poptset)
{
  if (NULL != option_dialogs) {
    option_dialogs_iterate(pdialog) {
      if (pdialog->poptset == poptset) {
        return pdialog;
      }
    } option_dialogs_iterate_end;
  }
  return NULL;
}

/************************************************************************//**
  GDestroyNotify callback.
****************************************************************************/
static void option_color_destroy_notify(gpointer data)
{
  GdkRGBA *color = (GdkRGBA *) data;

  if (NULL != color) {
    gdk_rgba_free(color);
  }
}

/************************************************************************//**
  Set the color of a button.
****************************************************************************/
static void option_color_set_button_color(GtkButton *button,
                                          const GdkRGBA *new_color)
{
  GdkRGBA *current_color = g_object_get_data(G_OBJECT(button), "color");
  GtkWidget *child;

  if (NULL == new_color) {
    if (NULL != current_color) {
      g_object_set_data(G_OBJECT(button), "color", NULL);
      gtk_button_set_child(button, NULL);
    }
  } else {
    GdkPixbuf *pixbuf;

    /* Apply the new color. */
    if (NULL != current_color) {
      /* We already have a GdkRGBA pointer. */
      *current_color = *new_color;
    } else {
      /* We need to make a GdkRGBA pointer. */
      current_color = gdk_rgba_copy(new_color);
      g_object_set_data_full(G_OBJECT(button), "color", current_color,
                             option_color_destroy_notify);
    }
    gtk_button_set_child(button, nullptr);

    /* Update the button. */
    {
      cairo_surface_t *surface = cairo_image_surface_create(
          CAIRO_FORMAT_RGB24, 16, 16);
      cairo_t *cr = cairo_create(surface);

      gdk_cairo_set_source_rgba(cr, current_color);
      cairo_paint(cr);
      cairo_destroy(cr);
      pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, 16, 16);
      cairo_surface_destroy(surface);
    }

    child = gtk_image_new_from_pixbuf(pixbuf);
    gtk_button_set_child(GTK_BUTTON(button), child);
    gtk_widget_set_visible(child, TRUE);
    g_object_unref(G_OBJECT(pixbuf));
  }
}

/************************************************************************//**
  "response" signal callback.
****************************************************************************/
static void color_selector_response_callback(GtkDialog *dialog,
                                             gint res, gpointer data)
{
  if (res == GTK_RESPONSE_REJECT) {
    /* Clears the current color. */
    option_color_set_button_color(GTK_BUTTON(data), NULL);
  } else if (res == GTK_RESPONSE_OK) {
    /* Apply the new color. */
    GtkColorChooser *chooser =
      GTK_COLOR_CHOOSER(g_object_get_data(G_OBJECT(dialog), "chooser"));
    GdkRGBA new_color;

    gtk_color_chooser_get_rgba(chooser, &new_color);
    option_color_set_button_color(GTK_BUTTON(data), &new_color);
  }

  gtk_window_destroy(GTK_WINDOW(dialog));
}

/************************************************************************//**
  Called when the user press a color button.
****************************************************************************/
static void option_color_select_callback(GtkButton *button, gpointer data)
{
  GtkWidget *dialog, *chooser;
  GdkRGBA *current_color = g_object_get_data(G_OBJECT(button), "color");

  dialog = gtk_dialog_new_with_buttons(_("Select a color"), NULL,
                                       GTK_DIALOG_MODAL,
                                       _("_Cancel"), GTK_RESPONSE_CANCEL,
                                       _("C_lear"), GTK_RESPONSE_REJECT,
                                       _("_OK"), GTK_RESPONSE_OK, NULL);
  setup_dialog(dialog, toplevel);
  g_signal_connect(dialog, "response",
                   G_CALLBACK(color_selector_response_callback), button);

  chooser = gtk_color_chooser_widget_new();
  g_object_set_data(G_OBJECT(dialog), "chooser", chooser);
  gtk_box_insert_child_after(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                             chooser, nullptr);
  if (current_color != nullptr) {
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(chooser), current_color);
  }

  gtk_widget_set_visible(dialog, TRUE);
}

/************************************************************************//**
  Creates a new option dialog.
****************************************************************************/
static struct option_dialog *
option_dialog_new(const char *name, const struct option_set *poptset)
{
  struct option_dialog *pdialog;
  const int CATEGORY_NUM = optset_category_number(poptset);

  /* Create the dialog structure. */
  pdialog = fc_malloc(sizeof(*pdialog));
  pdialog->poptset = poptset;
  pdialog->shell = gtk_dialog_new_with_buttons(name, NULL, 0,
                                               _("_Cancel"), RESPONSE_CANCEL,
                                               _("_Save"), RESPONSE_SAVE,
                                               _("_Refresh"), RESPONSE_REFRESH,
                                               _("Reset"), RESPONSE_RESET,
                                               _("_Apply"), RESPONSE_APPLY,
                                               _("_OK"), RESPONSE_OK, NULL);
  pdialog->notebook = gtk_notebook_new();
  pdialog->vboxes = fc_calloc(CATEGORY_NUM, sizeof(*pdialog->vboxes));
  pdialog->box_children = fc_calloc(CATEGORY_NUM,
                                    sizeof(*pdialog->box_children));

  /* Append to the option dialog list. */
  if (NULL == option_dialogs) {
    option_dialogs = option_dialog_list_new();
  }
  option_dialog_list_append(option_dialogs, pdialog);

  /* Shell */
  setup_dialog(pdialog->shell, toplevel);
  gtk_window_set_default_size(GTK_WINDOW(pdialog->shell),
                              GUI_GTK_OPTION(optionsdlg_xsize),
                              GUI_GTK_OPTION(optionsdlg_ysize));
  g_signal_connect(pdialog->shell, "response",
                   G_CALLBACK(option_dialog_reponse_callback), pdialog);
  g_signal_connect(pdialog->shell, "destroy",
                   G_CALLBACK(option_dialog_destroy_callback), pdialog);

  gtk_box_insert_child_after(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(pdialog->shell))),
                             pdialog->notebook, NULL);

  /* Add the options. */
  options_iterate(poptset, poption) {
    option_dialog_option_add(pdialog, poption, FALSE);
  } options_iterate_end;

  option_dialog_reorder_notebook(pdialog);

  /* Show the widgets. */
  gtk_widget_set_visible(pdialog->shell, TRUE);

  return pdialog;
}

/************************************************************************//**
  Destroys an option dialog.
****************************************************************************/
static void option_dialog_destroy(struct option_dialog *pdialog)
{
  GtkWidget *shell = pdialog->shell;

  if (NULL != option_dialogs) {
    option_dialog_list_remove(option_dialogs, pdialog);
  }

  options_iterate(pdialog->poptset, poption) {
    option_set_gui_data(poption, NULL);
  } options_iterate_end;

  if (NULL != shell) {
    /* Maybe already destroyed, see also option_dialog_destroy_callback(). */
    pdialog->shell = NULL;
    gtk_window_destroy(GTK_WINDOW(shell));
  }

  free(pdialog->vboxes);
  free(pdialog->box_children);
  free(pdialog);
}

/************************************************************************//**
  Utility for sorting the pages of an option dialog.
****************************************************************************/
static int option_dialog_pages_sort_func(const void *w1, const void *w2)
{
  GObject *obj1 = G_OBJECT(*(GtkWidget **) w1);
  GObject *obj2 = G_OBJECT(*(GtkWidget **) w2);

  return (GPOINTER_TO_INT(g_object_get_data(obj1, "category"))
          - GPOINTER_TO_INT(g_object_get_data(obj2, "category")));
}

/************************************************************************//**
  Reorder the pages of the notebook of the option dialog.
****************************************************************************/
static void option_dialog_reorder_notebook(struct option_dialog *pdialog)
{
  GtkNotebook *notebook = GTK_NOTEBOOK(pdialog->notebook);
  const int pages_num = gtk_notebook_get_n_pages(notebook);

  if (0 < pages_num) {
    GtkWidget *pages[pages_num];
    int i;

    for (i = 0; i < pages_num; i++) {
      pages[i] = gtk_notebook_get_nth_page(notebook, i);
    }
    qsort(pages, pages_num, sizeof(*pages), option_dialog_pages_sort_func);
    for (i = 0; i < pages_num; i++) {
      gtk_notebook_reorder_child(notebook, pages[i], i);
    }
  }
}

/************************************************************************//**
  Do an action for all options of the option dialog.
****************************************************************************/
static inline void option_dialog_foreach(struct option_dialog *pdialog,
                                         void (*option_action)
                                         (struct option *))
{
  fc_assert_ret(NULL != pdialog);

  options_iterate(pdialog->poptset, poption) {
    option_action(poption);
  } options_iterate_end;
}

/************************************************************************//**
  Add an option to the option dialog.
****************************************************************************/
static void option_dialog_option_add(struct option_dialog *pdialog,
                                     struct option *poption,
                                     bool reorder_notebook)
{
  const int category = option_category(poption);
  GtkWidget *main_hbox, *label, *w = NULL;
  int main_col = 0;
  GtkGesture *gesture;
  GtkEventController *controller;

  fc_assert(NULL == option_get_gui_data(poption));

  /* Add category if needed. */
  if (NULL == pdialog->vboxes[category]) {
    GtkWidget *sw;

    sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), TRUE);
    g_object_set_data(G_OBJECT(sw), "category", GINT_TO_POINTER(category));
    gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), sw,
                             gtk_label_new_with_mnemonic
                             (option_category_name(poption)));

    if (reorder_notebook) {
      option_dialog_reorder_notebook(pdialog);
    }

    pdialog->vboxes[category] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_bottom(pdialog->vboxes[category], 8);
    gtk_widget_set_margin_end(pdialog->vboxes[category], 8);
    gtk_widget_set_margin_start(pdialog->vboxes[category], 8);
    gtk_widget_set_margin_top(pdialog->vboxes[category], 8);
    gtk_widget_set_hexpand(pdialog->vboxes[category], TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw),
                                  pdialog->vboxes[category]);

    gtk_widget_set_visible(sw, TRUE);
  }
  pdialog->box_children[category]++;

  main_hbox = gtk_grid_new();
  label = gtk_label_new(option_description(poption));
  gtk_widget_set_margin_bottom(label, 2);
  gtk_widget_set_margin_end(label, 2);
  gtk_widget_set_margin_start(label, 2);
  gtk_widget_set_margin_top(label, 2);
  gtk_grid_attach(GTK_GRID(main_hbox), label, main_col++, 0, 1, 1);
  gtk_widget_set_tooltip_text(main_hbox, option_help_text(poption));
  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
  controller = GTK_EVENT_CONTROLLER(gesture);
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(option_button_press_callback), poption);
  gtk_widget_add_controller(main_hbox, controller);
  gtk_box_append(GTK_BOX(pdialog->vboxes[category]), main_hbox);

  switch (option_type(poption)) {
  case OT_BOOLEAN:
    w = gtk_check_button_new();
    break;

  case OT_INTEGER:
    {
      int min = option_int_min(poption), max = option_int_max(poption);

      w = gtk_spin_button_new_with_range(min, max, MAX((max - min) / 50, 1));
    }
    break;

  case OT_STRING:
    {
      const struct strvec *values = option_str_values(poption);

      if (NULL != values) {
        w = gtk_combo_box_text_new_with_entry();
        strvec_iterate(values, value) {
          gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), value);
        } strvec_iterate_end;
      } else {
        w = gtk_entry_new();
      }
    }
    break;

  case OT_ENUM:
    {
      int i;
      const char *str;
      GtkListStore *model;
      GtkCellRenderer *renderer;
      GtkTreeIter iter;

      /* 0: enum index, 1: translated enum name. */
      model = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
      w = gtk_combo_box_new_with_model(GTK_TREE_MODEL(model));
      g_object_unref(model);

      renderer = gtk_cell_renderer_text_new();
      gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(w), renderer, FALSE);
      gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(w), renderer,
                                     "text", 1, NULL);
      for (i = 0; (str = option_enum_int_to_str(poption, i)); i++) {
        gtk_list_store_append(model, &iter);
        gtk_list_store_set(model, &iter, 0, i, 1, _(str), -1);
      }
    }
    break;

  case OT_BITWISE:
    {
      GList *list = NULL;
      GtkWidget *grid, *check;
      const struct strvec *values = option_bitwise_values(poption);
      int i;

      w = gtk_frame_new(NULL);
      grid = gtk_grid_new();
      gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
      gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
      gtk_frame_set_child(GTK_FRAME(w), grid);
      for (i = 0; i < strvec_size(values); i++) {
        check = gtk_check_button_new();
        gtk_grid_attach(GTK_GRID(grid), check, 0, i, 1, 1);
        label = gtk_label_new(_(strvec_get(values, i)));
        gtk_grid_attach(GTK_GRID(grid), label, 1, i, 1, 1);
        list = g_list_append(list, check);
      }
      g_object_set_data_full(G_OBJECT(w), "check_buttons", list,
                             (GDestroyNotify) g_list_free);
    }
    break;

  case OT_FONT:
    w = gtk_font_button_new();
    g_object_set(G_OBJECT(w), "use-font", TRUE, NULL);
    break;

  case OT_COLOR:
    {
      GtkWidget *button;
      int grid_col = 0;

      w = gtk_grid_new();
      gtk_grid_set_column_spacing(GTK_GRID(w), 4);
      gtk_grid_set_row_homogeneous(GTK_GRID(w), TRUE);

      /* Foreground color selector button. */
      button = gtk_button_new();
      gtk_grid_attach(GTK_GRID(w), button, grid_col++, 0, 1, 1);
      gtk_widget_set_tooltip_text(GTK_WIDGET(button),
                                  _("Select the text color"));
      g_object_set_data(G_OBJECT(w), "fg_button", button);
      g_signal_connect(button, "clicked",
                       G_CALLBACK(option_color_select_callback), NULL);

      /* Background color selector button. */
      button = gtk_button_new();
      gtk_grid_attach(GTK_GRID(w), button, grid_col++, 0, 1, 1);
      gtk_widget_set_tooltip_text(GTK_WIDGET(button),
                                  _("Select the background color"));
      g_object_set_data(G_OBJECT(w), "bg_button", button);
      g_signal_connect(button, "clicked",
                       G_CALLBACK(option_color_select_callback), NULL);
    }
    break;

  case OT_VIDEO_MODE:
    log_error("Option type %s (%d) not supported yet.",
              option_type_name(option_type(poption)),
              option_type(poption));
    break;
  }

  option_set_gui_data(poption, w);
  if (NULL == w) {
    log_error("Failed to create a widget for option %d \"%s\".",
              option_number(poption), option_name(poption));
  } else {
    g_object_set_data(G_OBJECT(w), "main_widget", main_hbox);
    g_object_set_data(G_OBJECT(w), "parent_of_main", pdialog->vboxes[category]);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(main_hbox), w, main_col++, 0, 1, 1);
  }

  gtk_widget_set_visible(main_hbox, TRUE);

  /* Set as current value. */
  option_dialog_option_refresh(poption);
}

/************************************************************************//**
  Remove an option from the option dialog.
****************************************************************************/
static void option_dialog_option_remove(struct option_dialog *pdialog,
                                        struct option *poption)
{
  GObject *object = G_OBJECT(option_get_gui_data(poption));

  if (NULL != object) {
    const int category = option_category(poption);

    option_set_gui_data(poption, NULL);
    gtk_box_remove(GTK_BOX(g_object_get_data(object, "parent_of_main")),
                   GTK_WIDGET(g_object_get_data(object, "main_widget")));

    /* Remove category if needed. */
    if (0 == --pdialog->box_children[category]) {
      gtk_notebook_remove_page(GTK_NOTEBOOK(pdialog->notebook), category);
      pdialog->vboxes[category] = NULL;
    }
  }
}

/************************************************************************//**
  Set the boolean value of the option.
****************************************************************************/
static inline void option_dialog_option_bool_set(struct option *poption,
                                                 bool value)
{
  gtk_check_button_set_active(GTK_CHECK_BUTTON
                              (option_get_gui_data(poption)),
                              value);
}

/************************************************************************//**
  Set the integer value of the option.
****************************************************************************/
static inline void option_dialog_option_int_set(struct option *poption,
                                                int value)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(option_get_gui_data(poption)),
                            value);
}

/************************************************************************//**
  Set the string value of the option.
****************************************************************************/
static inline void option_dialog_option_str_set(struct option *poption,
                                                const char *string)
{
  GtkWidget *wdg = option_get_gui_data(poption);

  if (NULL != option_str_values(poption)) {
    GtkWidget *child = gtk_combo_box_get_child(GTK_COMBO_BOX(wdg));

    gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(child)), string, -1);
  } else {
    gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(wdg)), string, -1);
  }
}

/************************************************************************//**
  Set the enum value of the option.
****************************************************************************/
static inline void option_dialog_option_enum_set(struct option *poption,
                                                 int value)
{
  GtkComboBox *combo = GTK_COMBO_BOX(option_get_gui_data(poption));
  GtkTreeModel *model = gtk_combo_box_get_model(combo);
  GtkTreeIter iter;
  int i;

  if (gtk_tree_model_get_iter_first(model, &iter)) {
    do {
      gtk_tree_model_get(model, &iter, 0, &i, -1);
      if (i == value) {
        gtk_combo_box_set_active_iter(combo, &iter);
        return;
      }
    } while (gtk_tree_model_iter_next(model, &iter));
  }

  log_error("Didn't find the value %d for option \"%s\" (nb %d).",
            value, option_name(poption), option_number(poption));
}

/************************************************************************//**
  Set the enum value of the option.
****************************************************************************/
static inline void option_dialog_option_bitwise_set(struct option *poption,
                                                    unsigned value)
{
  GObject *data = option_get_gui_data(poption);
  GList *iter = g_object_get_data(data, "check_buttons");
  int bit;

  for (bit = 0; NULL != iter; iter = g_list_next(iter), bit++) {
    gtk_check_button_set_active(GTK_CHECK_BUTTON(iter->data),
                                value & (1 << bit));
  }
}

/************************************************************************//**
  Set the font value of the option.
****************************************************************************/
static inline void option_dialog_option_font_set(struct option *poption,
                                                 const char *font)
{
  gtk_font_chooser_set_font(GTK_FONT_CHOOSER
                            (option_get_gui_data(poption)), font);
}

/************************************************************************//**
  Set the font value of the option.
****************************************************************************/
static inline void option_dialog_option_color_set(struct option *poption,
                                                  struct ft_color color)
{
  GtkWidget *w = option_get_gui_data(poption);
  GdkRGBA gdk_color;

  /* Update the foreground button. */
  if (NULL != color.foreground
      && '\0' != color.foreground[0]
      && gdk_rgba_parse(&gdk_color, color.foreground)) {
    option_color_set_button_color(g_object_get_data(G_OBJECT(w),
                                                    "fg_button"),
                                  &gdk_color);
  } else {
    option_color_set_button_color(g_object_get_data(G_OBJECT(w),
                                                    "fg_button"), NULL);
  }

  /* Update the background button. */
  if (NULL != color.background
      && '\0' != color.background[0]
      && gdk_rgba_parse(&gdk_color, color.background)) {
    option_color_set_button_color(g_object_get_data(G_OBJECT(w),
                                                    "bg_button"),
                                  &gdk_color);
  } else {
    option_color_set_button_color(g_object_get_data(G_OBJECT(w),
                                                    "bg_button"), NULL);
  }
}

/************************************************************************//**
  Update an option in the option dialog.
****************************************************************************/
static void option_dialog_option_refresh(struct option *poption)
{
  switch (option_type(poption)) {
  case OT_BOOLEAN:
    option_dialog_option_bool_set(poption, option_bool_get(poption));
    break;
  case OT_INTEGER:
    option_dialog_option_int_set(poption, option_int_get(poption));
    break;
  case OT_STRING:
    option_dialog_option_str_set(poption, option_str_get(poption));
    break;
  case OT_ENUM:
    option_dialog_option_enum_set(poption, option_enum_get_int(poption));
    break;
  case OT_BITWISE:
    option_dialog_option_bitwise_set(poption, option_bitwise_get(poption));
    break;
  case OT_FONT:
    option_dialog_option_font_set(poption, option_font_get(poption));
    break;
  case OT_COLOR:
    option_dialog_option_color_set(poption, option_color_get(poption));
    break;
  case OT_VIDEO_MODE:
    log_error("Option type %s (%d) not supported yet.",
              option_type_name(option_type(poption)),
              option_type(poption));
    break;
  }

  gtk_widget_set_sensitive(option_get_gui_data(poption),
                           option_is_changeable(poption));
}

/************************************************************************//**
  Reset the option.
****************************************************************************/
static void option_dialog_option_reset(struct option *poption)
{
  switch (option_type(poption)) {
  case OT_BOOLEAN:
    option_dialog_option_bool_set(poption, option_bool_def(poption));
    break;
  case OT_INTEGER:
    option_dialog_option_int_set(poption, option_int_def(poption));
    break;
  case OT_STRING:
    option_dialog_option_str_set(poption, option_str_def(poption));
    break;
  case OT_ENUM:
    option_dialog_option_enum_set(poption, option_enum_def_int(poption));
    break;
  case OT_BITWISE:
    option_dialog_option_bitwise_set(poption, option_bitwise_def(poption));
    break;
  case OT_FONT:
    option_dialog_option_font_set(poption, option_font_def(poption));
    break;
  case OT_COLOR:
    option_dialog_option_color_set(poption, option_color_def(poption));
    break;
  case OT_VIDEO_MODE:
    log_error("Option type %s (%d) not supported yet.",
              option_type_name(option_type(poption)),
              option_type(poption));
    break;
  }
}

/************************************************************************//**
  Apply the option change.
****************************************************************************/
static void option_dialog_option_apply(struct option *poption)
{
  GtkWidget *w = GTK_WIDGET(option_get_gui_data(poption));

  switch (option_type(poption)) {
  case OT_BOOLEAN:
    (void) option_bool_set(poption, gtk_check_button_get_active
                           (GTK_CHECK_BUTTON(w)));
    break;

  case OT_INTEGER:
    (void) option_int_set(poption, gtk_spin_button_get_value_as_int
                          (GTK_SPIN_BUTTON(w)));
    break;

  case OT_STRING:
    if (NULL != option_str_values(poption)) {
      (void) option_str_set(poption,
                            gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(w)));
    } else {
      (void) option_str_set(poption, gtk_entry_buffer_get_text(
                                                 gtk_entry_get_buffer(GTK_ENTRY(w))));
    }
    break;

  case OT_ENUM:
    {
      GtkTreeIter iter;
      int value;

      if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(w), &iter)) {
        break;
      }

      gtk_tree_model_get(gtk_combo_box_get_model(GTK_COMBO_BOX(w)),
                         &iter, 0, &value, -1);
      (void) option_enum_set_int(poption, value);
    }
    break;

  case OT_BITWISE:
    {
      GList *iter = g_object_get_data(G_OBJECT(w), "check_buttons");
      unsigned value = 0;
      int bit;

      for (bit = 0; NULL != iter; iter = g_list_next(iter), bit++) {
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(iter->data))) {
          value |= 1 << bit;
        }
      }
      (void) option_bitwise_set(poption, value);
    }
    break;

  case OT_FONT:
    (void) option_font_set(poption, gtk_font_chooser_get_font
                           (GTK_FONT_CHOOSER(w)));
    break;

  case OT_COLOR:
    {
      gchar *fg_color_text = NULL, *bg_color_text = NULL;
      GObject *button;
      GdkRGBA *color;

      /* Get foreground color. */
      button = g_object_get_data(G_OBJECT(w), "fg_button");
      color = g_object_get_data(button, "color");
      if (color) fg_color_text = gdk_rgba_to_string(color);

      /* Get background color. */
      button = g_object_get_data(G_OBJECT(w), "bg_button");
      color = g_object_get_data(button, "color");
      if (color) bg_color_text = gdk_rgba_to_string(color);

      (void) option_color_set(poption,
                              ft_color_construct(fg_color_text, bg_color_text));
      g_free(fg_color_text);
      g_free(bg_color_text);
    }
    break;

  case OT_VIDEO_MODE:
    log_error("Option type %s (%d) not supported yet.",
              option_type_name(option_type(poption)),
              option_type(poption));
    break;
  }
}

/************************************************************************//**
  Popup the option dialog for the option set.
****************************************************************************/
void option_dialog_popup(const char *name, const struct option_set *poptset)
{
  struct option_dialog *pdialog = option_dialog_get(poptset);

  if (NULL != pdialog) {
    option_dialog_foreach(pdialog, option_dialog_option_refresh);
  } else {
    (void) option_dialog_new(name, poptset);
  }
}

/************************************************************************//**
  Popdown the option dialog for the option set.
****************************************************************************/
void option_dialog_popdown(const struct option_set *poptset)
{
  struct option_dialog *pdialog = option_dialog_get(poptset);

  if (NULL != pdialog) {
    option_dialog_destroy(pdialog);
  }
}

/************************************************************************//**
  Pass on updated option values to controls outside the main option
  dialogs.
****************************************************************************/
static void option_gui_update_extra(struct option *poption)
{
  if (option_optset(poption) == server_optset) {
    if (strcmp(option_name(poption), "aifill") == 0) {
      ai_fill_changed_by_server(option_int_get(poption));
    } else if (strcmp(option_name(poption), "nationset") == 0) {
      nationset_sync_to_server(option_str_get(poption));
    }
  }
}

/************************************************************************//**
  Update the GUI for the option.
****************************************************************************/
void option_gui_update(struct option *poption)
{
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_refresh(poption);
  }

  option_gui_update_extra(poption);
}

/************************************************************************//**
  Add the GUI for the option.
****************************************************************************/
void option_gui_add(struct option *poption)
{
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_add(pdialog, poption, TRUE);
  }

  option_gui_update_extra(poption);
}

/************************************************************************//**
  Remove the GUI for the option.
****************************************************************************/
void option_gui_remove(struct option *poption)
{
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_remove(pdialog, poption);
  }
}
