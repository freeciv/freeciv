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
#include <config.h>
#endif

#include <stdlib.h>

#include <gtk/gtk.h>

/* utility */
#include "log.h"
#include "string_vector.h"

/* client */
#include "options.h"

/* client/gui-gtk-2.0 */
#include "gui_main.h"
#include "gui_stuff.h"

#include "optiondlg.h"


/* The option dialog data. */
struct option_dialog {
  const struct option_set *poptset;     /* The option set. */
  GtkWidget *shell;                     /* The main widget. */
  GtkWidget *notebook;                  /* The notebook. */
  GtkWidget **vboxes;                   /* Category boxes. */
  int *box_children;                    /* The number of children for
                                         * each category. */
  GtkTooltips *tips;                    /* The tips stuff. */
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


/****************************************************************************
  Option dialog widget response callback.
****************************************************************************/
static void option_dialog_reponse_callback(GtkDialog *dialog,
                                           gint response_id, gpointer data)
{
  struct option_dialog *pdialog = (struct option_dialog *) data;

  switch (response_id) {
  case RESPONSE_CANCEL:
    gtk_widget_destroy(GTK_WIDGET(dialog));
    break;
  case RESPONSE_OK:
    option_dialog_foreach(pdialog, option_dialog_option_apply);
    gtk_widget_destroy(GTK_WIDGET(dialog));
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
    options_save();
    break;
  }
}

/****************************************************************************
  Option dialog widget destroyed callback.
****************************************************************************/
static void option_dialog_destroy_callback(GtkObject *object, gpointer data)
{
  struct option_dialog *pdialog = (struct option_dialog *) data;

  if (NULL != pdialog->shell) {
    /* Mark as destroyed, see also option_dialog_destroy(). */
    pdialog->shell = NULL;
    option_dialog_destroy(pdialog);
  }
}

/*************************************************************************
  Option refresh requested from menu.
*************************************************************************/
static void option_refresh_callback(GtkMenuItem *menuitem, gpointer data)
{
  struct option *poption = (struct option *) data;
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_refresh(poption);
  }
}

/*************************************************************************
  Option reset requested from menu.
*************************************************************************/
static void option_reset_callback(GtkMenuItem *menuitem, gpointer data)
{
  struct option *poption = (struct option *) data;
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_reset(poption);
  }
}

/*************************************************************************
  Option apply requested from menu.
*************************************************************************/
static void option_apply_callback(GtkMenuItem *menuitem, gpointer data)
{
  struct option *poption = (struct option *) data;
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_apply(poption);
  }
}

/*************************************************************************
  Called when a button is pressed on a option.
*************************************************************************/
static gboolean option_button_press_callback(GtkWidget *widget,
                                             GdkEventButton *event,
                                             gpointer data)
{
  struct option *poption = (struct option *) data;
  GtkWidget *menu, *item;

  if (3 != event->button || !option_is_changeable(poption)) {
    /* Only right button please! */
    return FALSE;
  }

  menu = gtk_menu_new();

  item = gtk_image_menu_item_new_with_label(_("Refresh this option"));
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
      gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_MENU));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
                   G_CALLBACK(option_refresh_callback), poption);

  item = gtk_image_menu_item_new_with_label(_("Reset this option"));
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
      gtk_image_new_from_stock(GTK_STOCK_CLEAR, GTK_ICON_SIZE_MENU));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
                   G_CALLBACK(option_reset_callback), poption);

  item = gtk_image_menu_item_new_with_label(
             _("Apply the changes for this option"));
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
      gtk_image_new_from_stock(GTK_STOCK_APPLY, GTK_ICON_SIZE_MENU));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
                   G_CALLBACK(option_apply_callback), poption);

  gtk_widget_show_all(menu);
  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, 0);

  return TRUE;
}

/****************************************************************************
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

/****************************************************************************
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
                       GTK_STOCK_CANCEL, RESPONSE_CANCEL,
                       GTK_STOCK_SAVE, RESPONSE_SAVE,
                       GTK_STOCK_REFRESH, RESPONSE_REFRESH,
                       _("Reset"), RESPONSE_RESET,
                       GTK_STOCK_APPLY, RESPONSE_APPLY,
                       GTK_STOCK_OK, RESPONSE_OK, NULL);
  pdialog->notebook = gtk_notebook_new();
  pdialog->vboxes = fc_calloc(CATEGORY_NUM, sizeof(*pdialog->vboxes));
  pdialog->box_children = fc_calloc(CATEGORY_NUM,
                                    sizeof(*pdialog->box_children));
  pdialog->tips = gtk_tooltips_new();

  /* Append to the option dialog list. */
  if (NULL == option_dialogs) {
    option_dialogs = option_dialog_list_new();
  }
  option_dialog_list_append(option_dialogs, pdialog);

  /* Shell */
  setup_dialog(pdialog->shell, toplevel);
  gtk_window_set_position(GTK_WINDOW(pdialog->shell), GTK_WIN_POS_MOUSE);
  gtk_window_set_default_size(GTK_WINDOW(pdialog->shell), -1, 480);
  g_signal_connect(pdialog->shell, "response",
                   G_CALLBACK(option_dialog_reponse_callback), pdialog);
  g_signal_connect(pdialog->shell, "destroy",
                   G_CALLBACK(option_dialog_destroy_callback), pdialog);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->vbox),
                     pdialog->notebook, TRUE, TRUE, 0);

  /* Add the options. */
  options_iterate(poptset, poption) {
    option_dialog_option_add(pdialog, poption, FALSE);
  } options_iterate_end;

  option_dialog_reorder_notebook(pdialog);

  /* Show the widgets. */
  gtk_widget_show_all(pdialog->shell);

  return pdialog;
}

/****************************************************************************
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
    gtk_widget_destroy(shell);
  }

  gtk_object_destroy(GTK_OBJECT(pdialog->tips));
  free(pdialog->vboxes);
  free(pdialog->box_children);
  free(pdialog);
}

/****************************************************************************
  Utility for sorting the pages of a option dialog.
****************************************************************************/
static int option_dialog_pages_sort_func(const void *w1, const void *w2)
{
  GObject *obj1 = G_OBJECT(*(GtkWidget **) w1);
  GObject *obj2 = G_OBJECT(*(GtkWidget **) w2);

  return (GPOINTER_TO_INT(g_object_get_data(obj1, "category"))
          - GPOINTER_TO_INT(g_object_get_data(obj2, "category")));
}

/****************************************************************************
  Reoder the pages of the notebook of the option dialog.
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

/****************************************************************************
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

/****************************************************************************
  Add an option to the option dialog.
****************************************************************************/
static void option_dialog_option_add(struct option_dialog *pdialog,
                                     struct option *poption,
                                     bool reorder_notebook)
{
  const int category = option_category(poption);
  GtkWidget *hbox, *ebox, *w = NULL;

  fc_assert(NULL == option_get_gui_data(poption));

  /* Add category if needed. */
  if (NULL == pdialog->vboxes[category]) {
    GtkWidget *sw, *align;

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    g_object_set_data(G_OBJECT(sw), "category", GINT_TO_POINTER(category));
    gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), sw,
                             gtk_label_new_with_mnemonic
                             (option_category_name(poption)));

    if (reorder_notebook) {
      option_dialog_reorder_notebook(pdialog);
    }

    align = gtk_alignment_new(0.0, 0.0, 1.0, 0.0);
    gtk_container_set_border_width(GTK_CONTAINER(align), 8);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(sw), align);

    pdialog->vboxes[category] = gtk_vbox_new(TRUE, 0);
    gtk_container_add(GTK_CONTAINER(align), pdialog->vboxes[category]);

    gtk_widget_show_all(sw);
  }
  pdialog->box_children[category]++;

  ebox = gtk_event_box_new();
  gtk_tooltips_set_tip(pdialog->tips, ebox, option_help_text(poption), NULL);
  gtk_box_pack_start(GTK_BOX(pdialog->vboxes[category]), ebox,
                     FALSE, FALSE, 0);
  g_signal_connect(ebox, "button_press_event",
                   G_CALLBACK(option_button_press_callback), poption);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox),
                     gtk_label_new(option_description(poption)),
                     FALSE, FALSE, 5);
  gtk_container_add(GTK_CONTAINER(ebox), hbox);

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
        w = gtk_combo_box_entry_new_text();
        strvec_iterate(values, value) {
          gtk_combo_box_append_text(GTK_COMBO_BOX(w), value);
        } strvec_iterate_end;
      } else {
        w = gtk_entry_new();
      }
    }
    break;

  case OT_FONT:
    w = gtk_font_button_new();
    g_object_set(G_OBJECT(w), "use-font", TRUE, NULL);
    break;
  }

  option_set_gui_data(poption, w);
  if (NULL == w) {
    log_error("Failed to create a widget for option %d \"%s\".",
              option_number(poption), option_name(poption));
  } else {
    g_object_set_data(G_OBJECT(w), "main_widget", ebox);
    gtk_box_pack_end(GTK_BOX(hbox), w, FALSE, FALSE, 0);
  }

  gtk_widget_show_all(ebox);

  /* Set as current value. */
  option_dialog_option_refresh(poption);
}

/****************************************************************************
  Remove an option from the option dialog.
****************************************************************************/
static void option_dialog_option_remove(struct option_dialog *pdialog,
                                        struct option *poption)
{
  GObject *object = G_OBJECT(option_get_gui_data(poption));

  if (NULL != object) {
    const int category = option_category(poption);

    option_set_gui_data(poption, NULL);
    gtk_widget_destroy(GTK_WIDGET(g_object_get_data(object, "main_widget")));

    /* Remove category if needed. */
    if (0 == --pdialog->box_children[category]) {
      gtk_notebook_remove_page(GTK_NOTEBOOK(pdialog->notebook), category);
      pdialog->vboxes[category] = NULL;
    }
  }
}

/****************************************************************************
  Set the boolean value of the option.
****************************************************************************/
static inline void option_dialog_option_bool_set(struct option *poption,
                                                 bool value)
{
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                               (option_get_gui_data(poption)),
                               value);
}

/****************************************************************************
  Set the integer value of the option.
****************************************************************************/
static inline void option_dialog_option_int_set(struct option *poption,
                                                int value)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(option_get_gui_data(poption)),
                            value);
}

/****************************************************************************
  Set the string value of the option.
****************************************************************************/
static inline void option_dialog_option_str_set(struct option *poption,
                                                const char *string)
{
  if (NULL != option_str_values(poption)) {
    gtk_entry_set_text(GTK_ENTRY(GTK_BIN
                       (option_get_gui_data(poption))->child), string);
  } else {
    gtk_entry_set_text(GTK_ENTRY(option_get_gui_data(poption)), string);
  }
}

/****************************************************************************
  Set the font value of the option.
****************************************************************************/
static inline void option_dialog_option_font_set(struct option *poption,
                                                 const char *font)
{
  gtk_font_button_set_font_name(GTK_FONT_BUTTON
                                (option_get_gui_data(poption)), font);
}

/****************************************************************************
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
  case OT_FONT:
    option_dialog_option_font_set(poption, option_font_get(poption));
    break;
  }

  gtk_widget_set_sensitive(option_get_gui_data(poption),
                           option_is_changeable(poption));
}

/****************************************************************************
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
  case OT_FONT:
    option_dialog_option_font_set(poption, option_font_def(poption));
    break;
  }
}

/****************************************************************************
  Apply the option change.
****************************************************************************/
static void option_dialog_option_apply(struct option *poption)
{
  GtkWidget *w = GTK_WIDGET(option_get_gui_data(poption));

  switch (option_type(poption)) {
  case OT_BOOLEAN:
    (void) option_bool_set(poption, gtk_toggle_button_get_active
                           (GTK_TOGGLE_BUTTON(w)));
    break;

  case OT_INTEGER:
    (void) option_int_set(poption, gtk_spin_button_get_value_as_int
                          (GTK_SPIN_BUTTON(w)));
    break;

  case OT_STRING:
    if (NULL != option_str_values(poption)) {
      (void) option_str_set(poption, gtk_entry_get_text
                            (GTK_ENTRY(GTK_BIN(w)->child)));
    } else {
      (void) option_str_set(poption, gtk_entry_get_text(GTK_ENTRY(w)));
    }
    break;

  case OT_FONT:
    (void) option_font_set(poption, gtk_font_button_get_font_name
                           (GTK_FONT_BUTTON(w)));
    break;
  }
}

/****************************************************************************
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

/****************************************************************************
  Popdown the option dialog for the option set.
****************************************************************************/
void option_dialog_popdown(const struct option_set *poptset)
{
  struct option_dialog *pdialog = option_dialog_get(poptset);

  if (NULL != pdialog) {
    option_dialog_destroy(pdialog);
  }
}

/****************************************************************************
  Update the GUI for the option.
****************************************************************************/
void option_gui_update(struct option *poption)
{
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_refresh(poption);
  }
}

/****************************************************************************
  Add the GUI for the option.
****************************************************************************/
void option_gui_add(struct option *poption)
{
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_add(pdialog, poption, TRUE);
  }
}

/****************************************************************************
  Remove the GUI for the option.
****************************************************************************/
void option_gui_remove(struct option *poption)
{
  struct option_dialog *pdialog = option_dialog_get(option_optset(poption));

  if (NULL != pdialog) {
    option_dialog_option_remove(pdialog, poption);
  }
}
