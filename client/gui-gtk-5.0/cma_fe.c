/***********************************************************************
 Freeciv - Copyright (C) 2001 - R. Falke, M. Kaufman
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

#include <gdk/gdkkeysyms.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* common */
#include "events.h"
#include "game.h"

/* client */
#include "chatline_g.h"
#include "citydlg_g.h"
#include "client_main.h"
#include "cma_fec.h"
#include "messagewin_g.h"
#include "options.h"

/* client/gui-gtk-5.0 */
#include "cityrep.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"

#include "cma_fe.h"

#define BUFFER_SIZE             64

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct cma_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct cma_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list *dialog_list;

static int allow_refreshes = 1;

static struct cma_dialog *get_cma_dialog(struct city *pcity);

static void update_cma_preset_list(struct cma_dialog *pdialog);

static gboolean cma_preset_key_pressed(GtkEventControllerKey *controller,
                                       guint keyval, guint keycode,
                                       GdkModifierType state, gpointer data);
static void cma_del_preset_callback(GtkWidget *w, gpointer data);
static void cma_preset_remove(struct cma_dialog *pdialog, int preset_index);
static void cma_preset_remove_response(GtkWidget *w, gint response,
                                       gpointer data);

static void cma_add_preset_callback(GtkWidget *w, gpointer data);
static void cma_preset_add_popup_callback(gpointer data, gint response,
                                          const char *input);

static void cma_active_callback(GtkWidget *w, gpointer data);
static void cma_activate_preset_callback(GtkSelectionModel *self,
                                         guint position,
                                         guint n_items,
                                         gpointer data);

static void hscale_changed(GtkWidget *get, gpointer data);
static void set_hscales(const struct cm_parameter *const parameter,
                        struct cma_dialog *pdialog);

#define FC_TYPE_PRESET_ROW (fc_preset_row_get_type())

G_DECLARE_FINAL_TYPE(FcPresetRow, fc_preset_row, FC, PRESET_ROW, GObject)

struct _FcPresetRow
{
  GObject parent_instance;

  char *desc;
};

struct _FcPresetRowClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcPresetRow, fc_preset_row, G_TYPE_OBJECT)

/**********************************************************************//**
  Finalizing method for FcPresetRow class
**************************************************************************/
static void fc_preset_row_finalize(GObject *gobject)
{
  FcPresetRow *row = FC_PRESET_ROW(gobject);

  if (row->desc != nullptr) {
    free(row->desc);
    row->desc = nullptr;
  }

  G_OBJECT_CLASS(fc_preset_row_parent_class)->finalize(gobject);
}

/**********************************************************************//**
  Initialization method for FcPresetRow class
**************************************************************************/
static void
fc_preset_row_class_init(FcPresetRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = fc_preset_row_finalize;
}

/**********************************************************************//**
  Initialization method for FcPresetRow
**************************************************************************/
static void
fc_preset_row_init(FcPresetRow *self)
{
  self->desc = nullptr;
}

/**********************************************************************//**
  FcPresetRow creation method
**************************************************************************/
static FcPresetRow *fc_preset_row_new(void)
{
  FcPresetRow *result;

  result = g_object_new(FC_TYPE_PRESET_ROW, nullptr);

  return result;
}

/**********************************************************************//**
  Initialize cma front end system
**************************************************************************/
void cma_fe_init(void)
{
  dialog_list = dialog_list_new();
}

/**********************************************************************//**
  Free resources allocated for cma front end system
**************************************************************************/
void cma_fe_done(void)
{
  dialog_list_destroy(dialog_list);
}

/**********************************************************************//**
 only called when the city dialog is closed.
**************************************************************************/
void close_cma_dialog(struct city *pcity)
{
  struct cma_dialog *pdialog = get_cma_dialog(pcity);

  if (pdialog == NULL) {
    /* A city which is being investigated doesn't contain cma dialog */
    return;
  }

  gtk_box_remove(GTK_BOX(gtk_widget_get_parent(pdialog->shell)),
                 pdialog->shell);
}

/**********************************************************************//**
  Destroy cma dialog
**************************************************************************/
static void cma_dialog_destroy_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  dialog_list_remove(dialog_list, pdialog);
  free(pdialog);
}

/**********************************************************************//**
  Return the cma_dialog for a given city.
**************************************************************************/
struct cma_dialog *get_cma_dialog(struct city *pcity)
{
  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pcity == pcity) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  User has pressed button in cma dialog
**************************************************************************/
static gboolean button_press_callback(GtkGestureClick *gesture, int n_press,
                                      double x, double y)
{
  GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  struct cma_dialog *pdialog
    = (struct cma_dialog *) g_object_get_data(G_OBJECT(widget), "dialog");
  int rnbr = get_column_view_row(pdialog->preset_list, y);

  if (rnbr >= 0) {
    if (n_press == 1) {
      const struct cm_parameter *pparam;

      pparam = cmafec_preset_get_parameter(rnbr);

      /* Save the change */
      cmafec_set_fe_parameter(pdialog->pcity, pparam);

      if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
        cma_release_city(pdialog->pcity);
        cma_put_city_under_agent(pdialog->pcity, pparam);
      }
      refresh_city_dialog(pdialog->pcity);
    } else if (n_press == 2) {
      struct cm_parameter param;

      cmafec_get_fe_parameter(pdialog->pcity, &param);
      cma_put_city_under_agent(pdialog->pcity, &param);
      refresh_city_dialog(pdialog->pcity);
    }
  }

  return FALSE;
}

/**********************************************************************//**
  User has requested help
**************************************************************************/
static void help_callback(GtkWidget *w, gpointer data)
{
  popup_help_dialog_string(HELP_CMA_ITEM);
}

/**********************************************************************//**
  Wlmeta table cell bind function
**************************************************************************/
static void preset_factory_bind(GtkSignalListItemFactory *self,
                                GtkListItem *list_item,
                                gpointer user_data)
{
  FcPresetRow *row;
  GtkWidget *child = gtk_list_item_get_child(list_item);

  row = gtk_list_item_get_item(list_item);

  gtk_label_set_text(GTK_LABEL(child), row->desc);
}

/**********************************************************************//**
  Wlmeta table cell setup function
**************************************************************************/
static void preset_factory_setup(GtkSignalListItemFactory *self,
                                 GtkListItem *list_item,
                                 gpointer user_data)
{
  gtk_list_item_set_child(list_item, gtk_label_new(""));
}

/**********************************************************************//**
  Instantiates a new struct for each city_dialog window that is open.
**************************************************************************/
struct cma_dialog *create_cma_dialog(struct city *pcity, bool tiny)
{
  struct cma_dialog *pdialog;
  struct cm_parameter param;
  GtkWidget *frame, *page, *hbox, *vbox, *label, *table;
  GtkWidget *hscale, *button;
  GtkListItemFactory *factory;
  GtkWidget *list;
  GtkColumnViewColumn *column;
  gint layout_width;
  GtkEventController *controller;
  int shell_row = 0;
  PangoLayout *layout;

  cmafec_get_fe_parameter(pcity, &param);
  pdialog = fc_malloc(sizeof(struct cma_dialog));
  pdialog->pcity = pcity;
  pdialog->shell = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(pdialog->shell),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(pdialog->shell), 8);
  gtk_widget_set_margin_start(pdialog->shell, 8);
  gtk_widget_set_margin_end(pdialog->shell, 8);
  gtk_widget_set_margin_top(pdialog->shell, 8);
  gtk_widget_set_margin_bottom(pdialog->shell, 8);
  g_signal_connect(pdialog->shell, "destroy",
                   G_CALLBACK(cma_dialog_destroy_callback), pdialog);

  page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_grid_attach(GTK_GRID(pdialog->shell), page, 0, shell_row++, 1, 1);

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_grid_set_row_spacing(GTK_GRID(pdialog->shell), 2);
  gtk_box_append(GTK_BOX(page), vbox);

  pdialog->store = g_list_store_new(FC_TYPE_PRESET_ROW);
  pdialog->selection
    = gtk_single_selection_new(G_LIST_MODEL(pdialog->store));
  list = gtk_column_view_new(GTK_SELECTION_MODEL(pdialog->selection));
  pdialog->preset_list = list;

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(preset_factory_bind),
                   nullptr);
  g_signal_connect(factory, "setup", G_CALLBACK(preset_factory_setup),
                   nullptr);

  column = gtk_column_view_column_new(_("Name"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(list), column);

  g_object_set_data(G_OBJECT(pdialog->preset_list), "dialog", pdialog);
  controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(button_press_callback), NULL);
  gtk_widget_add_controller(pdialog->preset_list, controller);

  gtk_widget_set_tooltip_text(list,
                              _("For information on\n"
                                "the citizen governor and governor presets,\n"
                                "including sample presets,\n"
                                "see README.governor."));

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", list,
                       "label", _("Prese_ts:"),
                       "xalign", 0.0, "yalign", 0.5, NULL);
  gtk_box_append(GTK_BOX(vbox), label);
  gtk_box_append(GTK_BOX(vbox), list);

  g_signal_connect(pdialog->selection, "selection-changed",
                   G_CALLBACK(cma_activate_preset_callback), pdialog);
  controller = gtk_event_controller_key_new();
  g_signal_connect(controller, "key-pressed",
                   G_CALLBACK(cma_preset_key_pressed), pdialog);
  gtk_widget_add_controller(list, controller);

  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_append(GTK_BOX(vbox), hbox);

  button = icon_label_button_new("document-new", _("Ne_w"));
  gtk_box_append(GTK_BOX(hbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(cma_add_preset_callback), pdialog);
  pdialog->add_preset_command = button;

  pdialog->del_preset_command = icon_label_button_new("edit-delete",
                                                      _("_Delete"));
  gtk_box_append(GTK_BOX(hbox), pdialog->del_preset_command);
  g_signal_connect(pdialog->del_preset_command, "clicked",
                   G_CALLBACK(cma_del_preset_callback), pdialog);

  /* The right-hand side */
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_bottom(vbox, 2);
  gtk_widget_set_margin_end(vbox, 2);
  gtk_widget_set_margin_start(vbox, 2);
  gtk_widget_set_margin_top(vbox, 2);
  gtk_box_append(GTK_BOX(page), vbox);

  /* Result */
  if (!tiny) {
    frame = gtk_frame_new(_("Results"));
    gtk_widget_set_vexpand(frame, TRUE);
    gtk_widget_set_valign(frame, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(vbox), frame);

    pdialog->result_label =
      gtk_label_new("food\n prod\n trade\n\n people\n grow\n prod\n name");
    gtk_widget_set_name(pdialog->result_label, "city_label");
    gtk_frame_set_child(GTK_FRAME(frame), pdialog->result_label);
    gtk_label_set_justify(GTK_LABEL(pdialog->result_label), GTK_JUSTIFY_LEFT);
  } else {
    pdialog->result_label = NULL;
  }

  /* Minimal Surplus and Factor */
  table = gtk_grid_new();
  gtk_widget_set_margin_bottom(table, 2);
  gtk_widget_set_margin_end(table, 2);
  gtk_widget_set_margin_start(table, 2);
  gtk_widget_set_margin_top(table, 2);
  gtk_box_append(GTK_BOX(vbox), table);

  label = gtk_label_new(_("Minimal Surplus"));
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(table), label, 1, 0, 1, 1);
  label = gtk_label_new(_("Factor"));
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(table), label, 2, 0, 1, 1);

  output_type_iterate(i) {
    label = gtk_label_new(get_output_name(i));
    gtk_grid_attach(GTK_GRID(table), label, 0, i + 1, 1, 1);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);

    pdialog->minimal_surplus[i] = hscale =
        gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, NULL);
    gtk_range_set_range(GTK_RANGE(hscale),
                        GUI_GTK_OPTION(governor_range_min),
                        GUI_GTK_OPTION(governor_range_max));
    gtk_range_set_increments(GTK_RANGE(hscale), 1, 1);
    layout = gtk_scale_get_layout(GTK_SCALE(hscale));
    if (layout != NULL) {
      pango_layout_get_pixel_size(layout, &layout_width, NULL);
      gtk_widget_set_size_request(hscale, layout_width + 51 * 2, -1);
    }

    gtk_grid_attach(GTK_GRID(table), hscale, 1, i + 1, 1, 1);
    gtk_scale_set_digits(GTK_SCALE(hscale), 0);
    gtk_scale_set_value_pos(GTK_SCALE(hscale), GTK_POS_LEFT);

    g_signal_connect(pdialog->minimal_surplus[i],
                     "value-changed",
                     G_CALLBACK(hscale_changed), pdialog);

    pdialog->factor[i] = hscale
      = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, NULL);
    gtk_range_set_range(GTK_RANGE(hscale), 0, 25);
    gtk_range_set_increments(GTK_RANGE(hscale), 1, 1);
    layout = gtk_scale_get_layout(GTK_SCALE(hscale));
    if (layout != NULL) {
      pango_layout_get_pixel_size(layout, &layout_width, NULL);
    } else {
      layout_width = 20;
    }
    gtk_widget_set_size_request(hscale, layout_width + 26 * 2, -1);

    gtk_grid_attach(GTK_GRID(table), hscale, 2, i + 1, 1, 1);
    gtk_scale_set_digits(GTK_SCALE(hscale), 0);
    gtk_scale_set_value_pos(GTK_SCALE(hscale), GTK_POS_LEFT);

    g_signal_connect(pdialog->factor[i], "value-changed",
                     G_CALLBACK(hscale_changed), pdialog);
  } output_type_iterate_end;

  /* Happy Surplus and Factor */
  label = gtk_label_new(_("Celebrate"));
  gtk_grid_attach(GTK_GRID(table), label, 0, O_LAST + 1, 1, 1);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);

  pdialog->happy_button = gtk_check_button_new();
  gtk_widget_set_halign(pdialog->happy_button, GTK_ALIGN_END);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(pdialog->happy_button),
                              FALSE);
  gtk_grid_attach(GTK_GRID(table), pdialog->happy_button, 1, O_LAST + 1, 1, 1);

  g_signal_connect(pdialog->happy_button, "toggled",
                   G_CALLBACK(hscale_changed), pdialog);

  pdialog->factor[O_LAST] = hscale
    = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, NULL);
  gtk_range_set_range(GTK_RANGE(hscale), 0, 50);
  gtk_range_set_increments(GTK_RANGE(hscale), 1, 1);
  layout = gtk_scale_get_layout(GTK_SCALE(hscale));
  if (layout != NULL) {
    pango_layout_get_pixel_size(layout, &layout_width, NULL);
    gtk_widget_set_size_request(hscale, layout_width + 51 * 2, -1);
  }

  gtk_grid_attach(GTK_GRID(table), hscale, 2, O_LAST + 1, 1, 1);
  gtk_scale_set_digits(GTK_SCALE(hscale), 0);
  gtk_scale_set_value_pos(GTK_SCALE(hscale), GTK_POS_LEFT);

  g_signal_connect(pdialog->factor[O_LAST],
                   "value-changed",
                   G_CALLBACK(hscale_changed), pdialog);

  /* Maximize Growth */
  label = gtk_label_new(_("Maximize growth"));
  gtk_grid_attach(GTK_GRID(table), label, 0, O_LAST + 2, 1, 1);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);

  pdialog->growth_button = gtk_check_button_new();
  gtk_widget_set_halign(pdialog->growth_button, GTK_ALIGN_END);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(pdialog->growth_button),
                              FALSE);
  gtk_grid_attach(GTK_GRID(table), pdialog->growth_button, 1, O_LAST + 2, 1, 1);

  g_signal_connect(pdialog->growth_button, "toggled",
                   G_CALLBACK(hscale_changed), pdialog);

  /* Buttons */
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_append(GTK_BOX(vbox), hbox);

  button = icon_label_button_new("help-browser", _("Help"));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(help_callback), NULL);
  gtk_box_append(GTK_BOX(hbox), button);

  pdialog->active_command = gtk_toggle_button_new();
  gtk_button_set_use_underline(GTK_BUTTON(pdialog->active_command), TRUE);
  gtk_widget_set_name(pdialog->active_command, "comment_label");
  gtk_box_append(GTK_BOX(hbox), pdialog->active_command);

  gtk_widget_set_visible(pdialog->shell, TRUE);

  dialog_list_prepend(dialog_list, pdialog);

  update_cma_preset_list(pdialog);

  /* Refresh is done in refresh_city_dialog() */

  return pdialog;
}

/**********************************************************************//**
  Refreshes the cma dialog
**************************************************************************/
void refresh_cma_dialog(struct city *pcity, enum cma_refresh refresh)
{
  struct cm_result *result = cm_result_new(pcity);
  struct cm_parameter param;
  struct cma_dialog *pdialog = get_cma_dialog(pcity);
  int controlled = cma_is_city_under_agent(pcity, NULL);

  cmafec_get_fe_parameter(pcity, &param);

  if (pdialog->result_label != NULL) {
    /* Fill in result label */
    cm_result_from_main_map(result, pcity);
    gtk_label_set_text(GTK_LABEL(pdialog->result_label),
                       cmafec_get_result_descr(pcity, result, &param));
  }

  /* If called from a hscale, we _don't_ want to do this */
  if (refresh != DONT_REFRESH_HSCALES) {
    set_hscales(&param, pdialog);
  }

  gtk_widget_queue_draw(pdialog->preset_list);

  gtk_widget_set_sensitive(pdialog->active_command, can_client_issue_orders());

  g_signal_handlers_disconnect_matched(pdialog->active_command,
      G_SIGNAL_MATCH_FUNC,
      0, 0, NULL, cma_active_callback, NULL);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pdialog->active_command),
      controlled);
  g_signal_connect(pdialog->active_command, "clicked",
      G_CALLBACK(cma_active_callback), pdialog);

  if (controlled) {
    gtk_button_set_label(GTK_BUTTON(pdialog->active_command),
                         _("Governor Enabl_ed"));
  } else {
    gtk_button_set_label(GTK_BUTTON(pdialog->active_command),
                         _("Governor Disabl_ed"));
  }

  if (pdialog->result_label != NULL) {
    gtk_widget_set_sensitive(pdialog->result_label, controlled);
  }

  cm_result_destroy(result);
}

/**********************************************************************//**
  Fills in the preset list
**************************************************************************/
static void update_cma_preset_list(struct cma_dialog *pdialog)
{
  char buf[BUFFER_SIZE];
  int i;

  /* Fill preset list */
  g_list_store_remove_all(pdialog->store);

  /* Append the presets */
  if (cmafec_preset_num()) {
    for (i = 0; i < cmafec_preset_num(); i++) {
      FcPresetRow *row = fc_preset_row_new();

      fc_strlcpy(buf, cmafec_preset_get_descr(i), sizeof(buf));
      row->desc = fc_strdup(buf);
      g_list_store_append(pdialog->store, row);
      g_object_unref(row);
    }
  }
}

/**********************************************************************//**
  Callback for selecting a preset from the preset view
**************************************************************************/
static void cma_activate_preset_callback(GtkSelectionModel *self,
                                         guint position,
                                         guint n_items,
                                         gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  const struct cm_parameter *pparam;
  int preset_index = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(self));

  pparam = cmafec_preset_get_parameter(preset_index);

  /* Save the change */
  cmafec_set_fe_parameter(pdialog->pcity, pparam);

  if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
    cma_release_city(pdialog->pcity);
    cma_put_city_under_agent(pdialog->pcity, pparam);
  }
  refresh_city_dialog(pdialog->pcity);
}

/**********************************************************************//**
  Pops up a dialog to allow to name your new preset
**************************************************************************/
static void cma_add_preset_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  const char *default_name;
  GtkWidget *parent = gtk_widget_get_ancestor(pdialog->shell, GTK_TYPE_WINDOW);
  int index;

  if ((index = gtk_single_selection_get_selected(pdialog->selection)) >= 0) {
    default_name = cmafec_preset_get_descr(index);
  } else {
    default_name = _("new preset");
  }

  pdialog->name_shell = input_dialog_create(GTK_WINDOW(parent),
                                    _("Name new preset"),
                                    _("What should we name the preset?"),
                                    default_name,
                                    cma_preset_add_popup_callback, pdialog);
}

/**********************************************************************//**
  Callback for the add_preset popup
**************************************************************************/
static void cma_preset_add_popup_callback(gpointer data, gint response,
                                          const char *input)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  if (pdialog) {
    if (response == GTK_RESPONSE_OK) {
      struct cm_parameter param;

      cmafec_get_fe_parameter(pdialog->pcity, &param);
      cmafec_preset_add(input, &param);
      update_cma_preset_list(pdialog);
      refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_HSCALES);
      /* If this or other cities have this set as "custom" */
      city_report_dialog_update();
    } /* else CANCEL or DELETE_EVENT */

    pdialog->name_shell = NULL;
  }
}

/**********************************************************************//**
  Key pressed in preset list
**************************************************************************/
static gboolean cma_preset_key_pressed(GtkEventControllerKey *controller,
                                       guint keyval, guint keycode,
                                       GdkModifierType state, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  int index;

  if ((index = gtk_single_selection_get_selected(pdialog->selection)) < 0) {
    return FALSE;
  }

  switch (keyval) {
  case GDK_KEY_Delete:
    cma_preset_remove(pdialog, index);
    break;
  case GDK_KEY_Insert:
    cma_add_preset_callback(NULL, pdialog);
    break;
  default:
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Callback for del_preset
**************************************************************************/
static void cma_del_preset_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  int index;

  if ((index = gtk_single_selection_get_selected(pdialog->selection)) < 0) {
    return;
  }

  cma_preset_remove(pdialog, index);
}

/**********************************************************************//**
  Pops up a dialog to remove a preset
**************************************************************************/
static void cma_preset_remove(struct cma_dialog *pdialog, int preset_index)
{
  GtkWidget *parent = gtk_widget_get_ancestor(pdialog->shell, GTK_TYPE_WINDOW);
  GtkWidget *shl;

  pdialog->id = preset_index;
  shl = gtk_message_dialog_new(NULL,
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_YES_NO,
                               _("Remove this preset?"));
  setup_dialog(shl, parent);
  pdialog->preset_remove_shell = shl;

  gtk_window_set_title(GTK_WINDOW(shl), cmafec_preset_get_descr(preset_index));

  g_signal_connect(shl, "response",
                   G_CALLBACK(cma_preset_remove_response), pdialog);

  gtk_window_present(GTK_WINDOW(shl));
}

/**********************************************************************//**
  Callback for the remove_preset popup
**************************************************************************/
static void cma_preset_remove_response(GtkWidget *w, gint response,
                                       gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  if (response == GTK_RESPONSE_YES) {
    cmafec_preset_remove(pdialog->id);
    pdialog->id = -1;
    update_cma_preset_list(pdialog);
    refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_HSCALES);
    /* if this or other cities have this set, reset to "custom" */
    city_report_dialog_update();
  }
  gtk_window_destroy(GTK_WINDOW(w));

  pdialog->preset_remove_shell = NULL;
}

/**********************************************************************//**
  Activates/deactivates agent control.
**************************************************************************/
static void cma_active_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
    cma_release_city(pdialog->pcity);
  } else {
    struct cm_parameter param;

    cmafec_get_fe_parameter(pdialog->pcity, &param);
    cma_put_city_under_agent(pdialog->pcity, &param);
  }
  refresh_city_dialog(pdialog->pcity);
}

/**********************************************************************//**
  Called to adjust the sliders when a preset is selected
  notice that we don't want to call update_result here.
**************************************************************************/
static void set_hscales(const struct cm_parameter *const parameter,
                        struct cma_dialog *pdialog)
{
  allow_refreshes = 0;
  output_type_iterate(i) {
    gtk_range_set_value(GTK_RANGE(pdialog->minimal_surplus[i]),
                        parameter->minimal_surplus[i]);
    gtk_range_set_value(GTK_RANGE(pdialog->factor[i]), parameter->factor[i]);
  } output_type_iterate_end;
  gtk_check_button_set_active(GTK_CHECK_BUTTON(pdialog->happy_button),
                              parameter->require_happy);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(pdialog->growth_button),
                              parameter->max_growth);
  gtk_range_set_value(GTK_RANGE(pdialog->factor[O_LAST]),
                      parameter->happy_factor);
  allow_refreshes = 1;
}

/**********************************************************************//**
  Callback if we moved the sliders.
**************************************************************************/
static void hscale_changed(GtkWidget *get, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  struct cm_parameter param;

  if (!allow_refreshes) {
    return;
  }

  cmafec_get_fe_parameter(pdialog->pcity, &param);
  output_type_iterate(i) {
    param.minimal_surplus[i] =
        (int) (gtk_range_get_value(GTK_RANGE(pdialog->minimal_surplus[i])));
    param.factor[i] =
        (int) (gtk_range_get_value(GTK_RANGE(pdialog->factor[i])));
  } output_type_iterate_end;
  param.require_happy =
      (gtk_check_button_get_active(GTK_CHECK_BUTTON(pdialog->happy_button)) ? TRUE : FALSE);
  param.max_growth =
      (gtk_check_button_get_active(GTK_CHECK_BUTTON(pdialog->growth_button)) ? TRUE : FALSE);
  param.happy_factor =
      (int) (gtk_range_get_value(GTK_RANGE(pdialog->factor[O_LAST])));

  /* Save the change */
  cmafec_set_fe_parameter(pdialog->pcity, &param);

  /* Refreshes the cma */
  if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
    cma_release_city(pdialog->pcity);
    cma_put_city_under_agent(pdialog->pcity, &param);
    refresh_city_dialog(pdialog->pcity);
  } else {
    refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_HSCALES);
  }
}
