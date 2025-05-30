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
#include "shared.h"
#include "support.h"

/* common */
#include "city.h"
#include "counters.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "packets.h"
#include "player.h"
#include "unitlist.h"

/* client */
#include "chatline_common.h"
#include "client_main.h"
#include "colors.h"
#include "control.h"
#include "climap.h"
#include "options.h"
#include "text.h"
#include "tilespec.h"

/* client/agents */
#include "cma_fec.h"

/* client/gui-gtk-5.0 */
#include "choice_dialog.h"
#include "citizensinfo.h"
#include "cityrep.h"
#include "cma_fe.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "happiness.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapview.h"
#include "update_queue.h"
#include "wldlg.h"

#include "citydlg.h"

#define CITYMAP_WIDTH MIN(512, canvas_width)
#define CITYMAP_HEIGHT (CITYMAP_WIDTH * canvas_height / canvas_width)
#define CITYMAP_SCALE ((double)CITYMAP_WIDTH / (double)canvas_width)

#define TINYSCREEN_MAX_HEIGHT (500 - 1)

/* Only CDLGR_UNITS button currently uses these, others have
 * direct callback. */
enum citydlg_response { CDLGR_UNITS, CDLGR_PREV, CDLGR_NEXT };

struct city_dialog;

/* Get 'struct dialog_list' and related function */
#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct city_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct city_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

struct unit_node {
  GtkWidget *cmd;
  GtkWidget *pix;
  int height;
  GtkEventController *left;
  GtkEventController *middle;
  GtkEventController *right;
};

/* Get 'struct unit_node' and related function */
#define SPECVEC_TAG unit_node
#define SPECVEC_TYPE struct unit_node
#include "specvec.h"

#define unit_node_vector_iterate(list, elt) \
    TYPED_VECTOR_ITERATE(struct unit_node, list, elt)
#define unit_node_vector_iterate_end  VECTOR_ITERATE_END

#define NUM_CITIZENS_SHOWN 30

enum { OVERVIEW_PAGE, WORKLIST_PAGE, HAPPINESS_PAGE, COUNTERS_PAGE,
    CMA_PAGE, SETTINGS_PAGE, STICKY_PAGE,
    NUM_PAGES  /* The number of pages in city dialog notebook
                * must match the entries in misc_whichtab_label[] */
};

enum {
    INFO_SIZE, INFO_FOOD, INFO_SHIELD, INFO_TRADE, INFO_GOLD,
    INFO_LUXURY, INFO_SCIENCE, INFO_GRANARY, INFO_GROWTH,
    INFO_CORRUPTION, INFO_WASTE, INFO_CULTURE, INFO_POLLUTION,
    INFO_ILLNESS, INFO_STEAL, INFO_AIRLIFT,
    NUM_INFO_FIELDS /* Number of fields in city_info panel
                     * must match entries in output_label[] */
};

/* Minimal size for the city map scrolling windows */
#define CITY_MAP_MIN_SIZE_X  200
#define CITY_MAP_MIN_SIZE_Y  150

struct city_map_canvas {
  GtkWidget *sw;
  GtkWidget *darea;
};

struct city_dialog {
  struct city *pcity;

  GtkWidget *shell;
  GtkWidget *name_label;
  cairo_surface_t *map_canvas_store_unscaled;
  GtkWidget *notebook;

  GtkWidget *popover;
  GtkWidget *citizen_pics;
  cairo_surface_t *citizen_surface;

  struct {
    struct city_map_canvas map_canvas;

    GtkWidget *production_bar;
    GtkWidget *buy_command;
    GListStore *improvement_list;

    GtkWidget *supported_units_frame;
    GtkWidget *supported_unit_table;

    GtkWidget *present_units_frame;
    GtkWidget *present_unit_table;

    struct unit_node_vector supported_units;
    struct unit_node_vector present_units;

    GtkWidget *info_label[NUM_INFO_FIELDS];

    GListStore *change_prod_store;
    GtkSingleSelection *change_prod_selection;
   } overview;

  struct {
    GtkWidget *production_label;
    GtkWidget *production_bar;
    GtkWidget *buy_command;
    GtkWidget *worklist;
  } production;

  struct {
    struct city_map_canvas map_canvas;

    GtkWidget *widget;
    GtkWidget *info_label[NUM_INFO_FIELDS];
    GtkWidget *citizens;
  } happiness;

  struct {
    GtkBox *container;
    GtkBox *widget;
  } counters;

  struct cma_dialog *cma_editor;

  struct {
    GtkWidget *rename_command;
    GtkWidget *new_citizens_radio[3];
    GtkWidget *disband_on_settler;
    GtkWidget *whichtab_radio[NUM_PAGES];
    short block_signal;
  } misc;

  GtkWidget *sell_shell;
  GtkTreeSelection *change_selection;
  GtkWidget *rename_shell;

  GtkWidget *show_units_command;
  GtkWidget *prev_command;
  GtkWidget *next_command;

  Impr_type_id sell_id;

  int cwidth;
};

static struct dialog_list *dialog_list;
static bool city_dialogs_have_been_initialised = FALSE;
static int canvas_width, canvas_height;
static int new_dialog_def_page = OVERVIEW_PAGE;
static int last_page = OVERVIEW_PAGE;

static bool is_showing_workertask_dialog = FALSE;

static struct
{
  struct city *owner;
  struct tile *loc;
} workertask_req;

static bool low_citydlg;

/****************************************/

static void initialize_city_dialogs(void);
static void city_dialog_map_create(struct city_dialog *pdialog,
                                   struct city_map_canvas *cmap_canvas);
static void city_dialog_map_recenter(GtkWidget *map_canvas_sw);

static struct city_dialog *get_city_dialog(struct city *pcity);
static gboolean citydlg_keyboard_handler(GtkEventControllerKey *controller,
                                         guint keyval,
                                         guint keycode,
                                         GdkModifierType state,
                                         gpointer data);

static GtkWidget *create_city_info_table(struct city_dialog *pdialog,
                                         GtkWidget **info_label);
static void create_and_append_overview_page(struct city_dialog *pdialog);
static void create_and_append_map_page(struct city_dialog *pdialog);
static void create_and_append_buildings_page(struct city_dialog *pdialog);
static void create_and_append_worklist_page(struct city_dialog *pdialog);
static void create_and_append_happiness_page(struct city_dialog *pdialog);
static void create_and_append_cma_page(struct city_dialog *pdialog);
static void create_and_append_settings_page(struct city_dialog *pdialog);

static struct city_dialog *create_city_dialog(struct city *pcity);

static void city_dialog_update_title(struct city_dialog *pdialog);
static void city_dialog_update_citizens(struct city_dialog *pdialog);
static void city_dialog_update_counters(struct city_dialog *pdialog);
static void city_dialog_update_information(GtkWidget **info_label,
                                           struct city_dialog *pdialog);
static void city_dialog_update_map(struct city_dialog *pdialog);
static void city_dialog_update_building(struct city_dialog *pdialog);
static void city_dialog_update_improvement_list(struct city_dialog
                                                *pdialog);
static void city_dialog_update_supported_units(struct city_dialog
                                               *pdialog);
static void city_dialog_update_present_units(struct city_dialog *pdialog);
static void city_dialog_update_prev_next(void);

static void show_units_response(void *data);

static gboolean supported_unit_callback(GtkGestureClick *gesture, int n_press,
                                        double x, double y, gpointer data);
static gboolean present_unit_callback(GtkGestureClick *gesture, int n_press,
                                      double x, double y, gpointer data);
static gboolean middle_supported_unit_release(GtkGestureClick *gesture, int n_press,
                                              double x, double y, gpointer data);
static gboolean middle_present_unit_release(GtkGestureClick *gesture, int n_press,
                                            double x, double y, gpointer data);
static gboolean right_unit_release(GtkGestureClick *gesture, int n_press,
                                   double x, double y, gpointer data);

static void close_citydlg_unit_popover(struct city_dialog *pdialog);

static void unit_center_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data);
static void unit_activate_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data);
static void supported_unit_activate_close_callback(GSimpleAction *action,
                                                   GVariant *parameter,
                                                   gpointer data);
static void present_unit_activate_close_callback(GSimpleAction *action,
                                                 GVariant *parameter,
                                                 gpointer data);
static void unit_load_callback(GSimpleAction *action, GVariant *parameter,
                               gpointer data);
static void unit_unload_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data);
static void unit_sentry_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data);
static void unit_fortify_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data);
static void unit_disband_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data);
static void unit_homecity_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data);
static void unit_upgrade_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data);
static gboolean citizens_callback(GtkGestureClick *gesture, int n_press,
                                  double x, double y, gpointer data);
static gboolean left_button_down_citymap(GtkGestureClick *gesture, int n_press,
                                         double x, double y, gpointer data);
static gboolean right_button_down_citymap(GtkGestureClick *gesture, int n_press,
                                          double x, double y, gpointer data);
static void draw_map_canvas(struct city_dialog *pdialog);

static void buy_callback(GtkWidget * w, gpointer data);
static void change_production_callback(GtkSelectionModel *self,
                                       guint position,
                                       guint n_items,
                                       gpointer data);

static void sell_callback(const struct impr_type *pimprove, gpointer data);
static void sell_callback_response(GtkWidget *w, gint response, gpointer data);

static void impr_callback(GtkColumnView *self, guint position,
                          gpointer data);

static void rename_callback(GtkWidget * w, gpointer data);
static void rename_popup_callback(gpointer data, gint response,
                                  const char *input);
static void set_cityopt_values(struct city_dialog *pdialog);
static void cityopt_callback(GtkWidget * w, gpointer data);
static void misc_whichtab_callback(GtkWidget * w, gpointer data);

static void city_destroy_callback(GtkWidget *w, gpointer data);
static void close_city_dialog(struct city_dialog *pdialog);
static void citydlg_response_callback(GtkDialog *dlg, gint response,
                                      void *data);
static void close_callback(GtkWidget *w, gpointer data);
static void switch_city_callback(GtkWidget *w, gpointer data);

#define FC_TYPE_IMPR_ROW (fc_impr_row_get_type())

G_DECLARE_FINAL_TYPE(FcImprRow, fc_impr_row, FC, IMPR_ROW, GObject)

struct _FcImprRow
{
  GObject parent_instance;

  const struct impr_type *impr;
  GdkPixbuf *sprite;
  char *description;
  int upkeep;
  bool redundant;
  const char *tooltip;
};

struct _FcImprClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcImprRow, fc_impr_row, G_TYPE_OBJECT)

#define IMPR_ROW_PIXBUF 0
#define IMPR_ROW_DESC   1
#define IMPR_ROW_UPKEEP 2

#define FC_TYPE_PROD_ROW (fc_prod_row_get_type())

G_DECLARE_FINAL_TYPE(FcProdRow, fc_prod_row, FC, PROD_ROW, GObject)

struct _FcProdRow
{
  GObject parent_instance;

  const char *name;
  int id;
  GdkPixbuf *sprite;
  bool useless;
};

struct _FcProdClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcProdRow, fc_prod_row, G_TYPE_OBJECT)

#define PROD_ROW_PIXBUF 0
#define PROD_ROW_NAME   1

/**********************************************************************//**
  Finalizing method for FcImprRow class
**************************************************************************/
static void fc_impr_row_finalize(GObject *gobject)
{
  FcImprRow *row = FC_IMPR_ROW(gobject);

  if (row->sprite != nullptr) {
    g_object_unref(G_OBJECT(row->sprite));
    row->sprite = nullptr;
  }

  G_OBJECT_CLASS(fc_impr_row_parent_class)->finalize(gobject);
}

/**********************************************************************//**
  Initialization method for FcImprRow class
**************************************************************************/
static void
fc_impr_row_class_init(FcImprRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = fc_impr_row_finalize;
}

/**********************************************************************//**
  Initialization method for FcImprRow
**************************************************************************/
static void
fc_impr_row_init(FcImprRow *self)
{
  self->sprite = nullptr;
}

/**********************************************************************//**
  FcImprRow creation method
**************************************************************************/
static FcImprRow *fc_impr_row_new(void)
{
  FcImprRow *result;

  result = g_object_new(FC_TYPE_IMPR_ROW, nullptr);

  return result;
}

/**********************************************************************//**
  Finalizing method for FcProdRow class
**************************************************************************/
static void fc_prod_row_finalize(GObject *gobject)
{
  FcProdRow *row = FC_PROD_ROW(gobject);

  if (row->sprite != nullptr) {
    g_object_unref(G_OBJECT(row->sprite));
    row->sprite = nullptr;
  }

  G_OBJECT_CLASS(fc_prod_row_parent_class)->finalize(gobject);
}

/**********************************************************************//**
  Initialization method for FcProdRow class
**************************************************************************/
static void
fc_prod_row_class_init(FcProdRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = fc_prod_row_finalize;
}

/**********************************************************************//**
  Initialization method for FcProdRow
**************************************************************************/
static void
fc_prod_row_init(FcProdRow *self)
{
  self->sprite = nullptr;
}

/**********************************************************************//**
  FcProdRow creation method
**************************************************************************/
static FcProdRow *fc_prod_row_new(void)
{
  FcProdRow *result;

  result = g_object_new(FC_TYPE_PROD_ROW, nullptr);

  return result;
}

/***********************************************************************//**
  Called to set the dimensions of the city dialog, both on
  startup and if the tileset is changed.
***************************************************************************/
static void init_citydlg_dimensions(void)
{
  canvas_width = get_citydlg_canvas_width();
  canvas_height = get_citydlg_canvas_height();
}

/***********************************************************************//**
  Initialize stuff needed for city dialogs
***************************************************************************/
static void initialize_city_dialogs(void)
{
  int height;

  fc_assert_ret(!city_dialogs_have_been_initialised);

  dialog_list = dialog_list_new();
  init_citydlg_dimensions();
  height = screen_height();

  /* Use default layout when height cannot be determined
   * (when height == 0) */
  if (height > 0 && height <= TINYSCREEN_MAX_HEIGHT) {
    low_citydlg = TRUE;
  } else {
    low_citydlg = FALSE;
  }

  city_dialogs_have_been_initialised = TRUE;
}

/***********************************************************************//**
  Called when the tileset changes.
***************************************************************************/
void reset_city_dialogs(void)
{
  if (!city_dialogs_have_been_initialised) {
    return;
  }

  init_citydlg_dimensions();

  dialog_list_iterate(dialog_list, pdialog) {
    /* There's no reasonable way to resize a GtkImage, so we don't try.
       Instead we just redraw the overview within the existing area.
       The player has to close and reopen the dialog to fix this. */
    city_dialog_update_map(pdialog);
  } dialog_list_iterate_end;

  popdown_all_city_dialogs();
}

/***********************************************************************//**
  Return city dialog of the given city, or NULL is it doesn't
  already exist
***************************************************************************/
static struct city_dialog *get_city_dialog(struct city *pcity)
{
  if (!city_dialogs_have_been_initialised) {
    initialize_city_dialogs();
  }

  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pcity == pcity) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/***********************************************************************//**
  Redraw map canvas.
***************************************************************************/
static void canvas_draw_cb(GtkDrawingArea *w, cairo_t *cr,
                           int width, int height, gpointer data)
{
  struct city_dialog *pdialog = data;

  cairo_scale(cr, CITYMAP_SCALE, CITYMAP_SCALE);
  cairo_set_source_surface(cr, pdialog->map_canvas_store_unscaled, 0, 0);
  if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
    cairo_paint_with_alpha(cr, 0.5);
  } else {
    cairo_paint(cr);
  }
}

/***********************************************************************//**
  Create a city map widget; used in the overview and in the happiness page.
***************************************************************************/
static void city_dialog_map_create(struct city_dialog *pdialog,
                                   struct city_map_canvas *cmap_canvas)
{
  GtkWidget *sw, *darea;
  GtkGesture *gesture;
  GtkEventController *controller;

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw),
                                            CITYMAP_WIDTH);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw),
                                             CITYMAP_HEIGHT);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), FALSE);

  darea = gtk_drawing_area_new();
  gtk_widget_set_size_request(darea, CITYMAP_WIDTH, CITYMAP_HEIGHT);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(darea),
                                 canvas_draw_cb, pdialog, NULL);

  controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(left_button_down_citymap), pdialog);
  gtk_widget_add_controller(darea, controller);
  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
  controller = GTK_EVENT_CONTROLLER(gesture);
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(right_button_down_citymap), pdialog);
  gtk_widget_add_controller(darea, controller);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), darea);

  /* save all widgets for the city map */
  cmap_canvas->sw = sw;
  cmap_canvas->darea = darea;
}

/***********************************************************************//**
  Center city dialog map.
***************************************************************************/
static void city_dialog_map_recenter(GtkWidget *map_canvas_sw)
{
  GtkAdjustment *adjust = NULL;
  gdouble value;

  fc_assert_ret(map_canvas_sw != NULL);

  adjust = gtk_scrolled_window_get_hadjustment(
    GTK_SCROLLED_WINDOW(map_canvas_sw));
  value = (gtk_adjustment_get_lower(adjust)
    + gtk_adjustment_get_upper(adjust)
    - gtk_adjustment_get_page_size(adjust)) / 2;
  gtk_adjustment_set_value(adjust, value);

  adjust = gtk_scrolled_window_get_vadjustment(
    GTK_SCROLLED_WINDOW(map_canvas_sw));
  value = (gtk_adjustment_get_lower(adjust)
    + gtk_adjustment_get_upper(adjust)
    - gtk_adjustment_get_page_size(adjust)) / 2;
  gtk_adjustment_set_value(adjust, value);
}

/***********************************************************************//**
  Refresh city dialog of the given city
***************************************************************************/
void real_city_dialog_refresh(struct city *pcity)
{
  struct city_dialog *pdialog = get_city_dialog(pcity);

  log_debug("CITYMAP_WIDTH:  %d", CITYMAP_WIDTH);
  log_debug("CITYMAP_HEIGHT: %d", CITYMAP_HEIGHT);
  log_debug("CITYMAP_SCALE:  %.3f", CITYMAP_SCALE);

  if (city_owner(pcity) == client.conn.playing) {
    city_report_dialog_update_city(pcity);
    economy_report_dialog_update();
  }

  if (!pdialog) {
    return;
  }

  city_dialog_update_title(pdialog);
  city_dialog_update_citizens(pdialog);
  city_dialog_update_counters(pdialog);
  city_dialog_update_information(pdialog->overview.info_label, pdialog);
  city_dialog_update_map(pdialog);
  city_dialog_update_building(pdialog);
  city_dialog_update_improvement_list(pdialog);
  city_dialog_update_supported_units(pdialog);
  city_dialog_update_present_units(pdialog);

  if (!client_has_player() || city_owner(pcity) == client_player()) {
    bool have_present_units = (unit_list_size(pcity->tile->units) > 0);

    refresh_worklist(pdialog->production.worklist);

    if (!low_citydlg) {
      city_dialog_update_information(pdialog->happiness.info_label, pdialog);
    }
    refresh_happiness_dialog(pdialog->pcity);
    if (game.info.citizen_nationality) {
      citizens_dialog_refresh(pdialog->pcity);
    }

    if (!client_is_observer()) {
      refresh_cma_dialog(pdialog->pcity, REFRESH_ALL);
    }

    gtk_widget_set_sensitive(pdialog->show_units_command,
                             can_client_issue_orders()
                             && have_present_units);
  } else {
    /* Set the buttons we do not want live while a Diplomat investigates */
    gtk_widget_set_sensitive(pdialog->show_units_command, FALSE);
  }
}

/***********************************************************************//**
  Refresh city dialogs of unit's homecity and city where unit
  currently is.
***************************************************************************/
void refresh_unit_city_dialogs(struct unit *punit)
{
  struct city *pcity_sup, *pcity_pre;
  struct city_dialog *pdialog;

  pcity_sup = game_city_by_number(punit->homecity);
  pcity_pre = tile_city(unit_tile(punit));

  if (pcity_sup && (pdialog = get_city_dialog(pcity_sup))) {
    city_dialog_update_supported_units(pdialog);
  }

  if (pcity_pre && (pdialog = get_city_dialog(pcity_pre))) {
    city_dialog_update_present_units(pdialog);
  }
}

/***********************************************************************//**
  Popup the dialog 10% inside the main-window
***************************************************************************/
void real_city_dialog_popup(struct city *pcity)
{
  struct city_dialog *pdialog;

  if (!(pdialog = get_city_dialog(pcity))) {
    pdialog = create_city_dialog(pcity);
  }

  gtk_window_present(GTK_WINDOW(pdialog->shell));

  /* center the city map(s); this must be *after* the city dialog was drawn
   * else the size information is missing! */
  city_dialog_map_recenter(pdialog->overview.map_canvas.sw);
  if (pdialog->happiness.map_canvas.sw) {
    city_dialog_map_recenter(pdialog->happiness.map_canvas.sw);
  }
}

/***********************************************************************//**
  Return whether city dialog for given city is open
***************************************************************************/
bool city_dialog_is_open(struct city *pcity)
{
  return get_city_dialog(pcity) != NULL;
}

/***********************************************************************//**
  Popdown the dialog
***************************************************************************/
void popdown_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog = get_city_dialog(pcity);

  if (pdialog) {
    close_city_dialog(pdialog);
  }
}

/***********************************************************************//**
  Popdown all dialogs
***************************************************************************/
void popdown_all_city_dialogs(void)
{
  if (!city_dialogs_have_been_initialised) {
    return;
  }

  while (dialog_list_size(dialog_list)) {
    close_city_dialog(dialog_list_get(dialog_list, 0));
  }
  dialog_list_destroy(dialog_list);

  city_dialogs_have_been_initialised = FALSE;
}

/***********************************************************************//**
  Keyboard handler for city dialog
***************************************************************************/
static gboolean citydlg_keyboard_handler(GtkEventControllerKey *controller,
                                         guint keyval,
                                         guint keycode,
                                         GdkModifierType state,
                                         gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;

  if (state & GDK_CONTROL_MASK) {
    switch (keyval) {
    case GDK_KEY_Left:
      gtk_notebook_prev_page(GTK_NOTEBOOK(pdialog->notebook));
      return TRUE;

    case GDK_KEY_Right:
      gtk_notebook_next_page(GTK_NOTEBOOK(pdialog->notebook));
      return TRUE;

    default:
      break;
    }
  }

  return FALSE;
}

/***********************************************************************//**
  Popup info dialog
***************************************************************************/
static gboolean show_info_popup(GtkGestureClick *gesture, int n_press,
                                double x, double y, gpointer data)
{
  GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  struct city_dialog *pdialog = g_object_get_data(G_OBJECT(w), "pdialog");
  GtkWidget *p, *label;
  char buf[1024];

  switch (GPOINTER_TO_UINT(data)) {
  case INFO_SIZE:
    fc_snprintf(buf, sizeof(buf), _("Population: %d, Specialists: %d"),
                pdialog->pcity->size, city_specialists(pdialog->pcity));
    break;
  case INFO_FOOD:
    get_city_dialog_output_text(pdialog->pcity, O_FOOD, buf, sizeof(buf));
    break;
  case INFO_SHIELD:
    get_city_dialog_output_text(pdialog->pcity, O_SHIELD,
                                buf, sizeof(buf));
    break;
  case INFO_TRADE:
    get_city_dialog_output_text(pdialog->pcity, O_TRADE, buf, sizeof(buf));
    break;
  case INFO_GOLD:
    get_city_dialog_output_text(pdialog->pcity, O_GOLD, buf, sizeof(buf));
    break;
  case INFO_SCIENCE:
    get_city_dialog_output_text(pdialog->pcity, O_SCIENCE,
                                buf, sizeof(buf));
    break;
  case INFO_LUXURY:
    get_city_dialog_output_text(pdialog->pcity, O_LUXURY,
                                buf, sizeof(buf));
    break;
  case INFO_CULTURE:
    get_city_dialog_culture_text(pdialog->pcity, buf, sizeof(buf));
    break;
  case INFO_POLLUTION:
    get_city_dialog_pollution_text(pdialog->pcity, buf, sizeof(buf));
    break;
  case INFO_ILLNESS:
    get_city_dialog_illness_text(pdialog->pcity, buf, sizeof(buf));
    break;
  case INFO_AIRLIFT:
    get_city_dialog_airlift_text(pdialog->pcity, buf, sizeof(buf));
    break;
  default:
    return TRUE;
  }

  p = gtk_popover_new();

  gtk_widget_set_parent(p, w);

  label = gtk_label_new(buf);
  gtk_widget_set_name(label, "city_label");
  gtk_widget_set_margin_start(label, 4);
  gtk_widget_set_margin_end(label, 4);
  gtk_widget_set_margin_top(label, 4);
  gtk_widget_set_margin_bottom(label, 4);

  gtk_popover_set_child(GTK_POPOVER(p), label);

  gtk_popover_popup(GTK_POPOVER(p));

  return TRUE;
}

/***********************************************************************//**
  Used once in the overview page and once in the happiness page
  **info_label points to the info_label in the respective struct
***************************************************************************/
static GtkWidget *create_city_info_table(struct city_dialog *pdialog,
                                         GtkWidget **info_label)
{
  int i;
  GtkWidget *table, *label;

  static const char *output_label[NUM_INFO_FIELDS] = {
    N_("Size:"),
    N_("Food:"),
    N_("Prod:"),
    N_("Trade:"),
    N_("Gold:"),
    N_("Luxury:"),
    N_("Science:"),
    N_("Granary:"),
    N_("Change in:"),
    N_("Corruption:"),
    N_("Waste:"),
    N_("Culture:"),
    N_("Pollution:"),
    N_("Plague risk:"),
    N_("Tech Stolen:"),
    N_("Airlift:"),
  };
  static bool output_label_done;
  GtkEventController *controller;

  table = gtk_grid_new();
  gtk_widget_set_margin_bottom(table, 4);
  gtk_widget_set_margin_end(table, 4);
  gtk_widget_set_margin_start(table, 4);
  gtk_widget_set_margin_top(table, 4);

  intl_slist(ARRAY_SIZE(output_label), output_label, &output_label_done);

  for (i = 0; i < NUM_INFO_FIELDS; i++) {
    label = gtk_label_new(output_label[i]);
    switch (i) {
    case INFO_SIZE:
    case INFO_TRADE:
    case INFO_SCIENCE:
    case INFO_GROWTH:
        gtk_widget_set_margin_bottom(label, 5);
        break;

    case INFO_FOOD:
    case INFO_GOLD:
    case INFO_GRANARY:
    case INFO_CORRUPTION:
        gtk_widget_set_margin_top(label, 5);
        break;
      default:
        break;
    }
    gtk_widget_set_margin_end(label, 5);
    gtk_widget_set_name(label, "city_label");   /* For font style? */
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(table), label, 0, i, 1, 1);

    label = gtk_label_new("");
    switch (i) {
    case INFO_TRADE:
    case INFO_SCIENCE:
    case INFO_GROWTH:
        gtk_widget_set_margin_bottom(label, 5);
        break;

    case INFO_GOLD:
    case INFO_GRANARY:
    case INFO_CORRUPTION:
        gtk_widget_set_margin_top(label, 5);
        break;
      default:
        break;
    }

    g_object_set_data(G_OBJECT(label), "pdialog", pdialog);

    controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
    g_signal_connect(controller, "pressed",
                     G_CALLBACK(show_info_popup), GUINT_TO_POINTER(i));
    gtk_widget_add_controller(label, controller);

    info_label[i] = label;
    gtk_widget_set_name(label, "city_label");   /* Ditto */
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);

    gtk_grid_attach(GTK_GRID(table), label, 1, i, 1, 1);
  }

  /*
   * Allow special highlighting of emergencies for granary etc by
   * city_dialog_update_information().
   */
  {
    /* This will persist, and can be shared between overview and happiness
     * pages. */
    static GtkCssProvider *emergency_provider = NULL;

    if (emergency_provider == NULL) {
      emergency_provider = gtk_css_provider_new();

      gtk_css_provider_load_from_data(emergency_provider,
                                      ".emergency {\n"
                                      "  color: rgba(255, 0.0, 0.0, 255);\n"
                                      "}",
                                      -1);

      gtk_style_context_add_provider_for_display(
                                   gtk_widget_get_display(toplevel),
                                   GTK_STYLE_PROVIDER(emergency_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
  }

  gtk_widget_set_visible(table, TRUE);

  return table;
}

/***********************************************************************//**
  Create main citydlg map

  Map frame will be placed to top left (col, row) position of the grid.
***************************************************************************/
static void create_citydlg_main_map(struct city_dialog *pdialog,
                                    GtkWidget *grid, int col, int row)
{
  GtkWidget *frame;

  frame = gtk_frame_new(_("City map"));
  gtk_widget_set_size_request(frame, CITY_MAP_MIN_SIZE_X,
                              CITY_MAP_MIN_SIZE_Y);
  gtk_grid_attach(GTK_GRID(grid), frame, col, row, 1, 1);

  city_dialog_map_create(pdialog, &pdialog->overview.map_canvas);
  gtk_frame_set_child(GTK_FRAME(frame), pdialog->overview.map_canvas.sw);
}

/**********************************************************************//**
  Improvement table cell bind function
**************************************************************************/
static void impr_factory_bind(GtkSignalListItemFactory *self,
                              GtkListItem *list_item,
                              gpointer user_data)
{
  FcImprRow *row;
  GtkWidget *child = gtk_list_item_get_child(list_item);

  row = gtk_list_item_get_item(list_item);

  switch (GPOINTER_TO_INT(user_data)) {
  case IMPR_ROW_PIXBUF:
    gtk_image_set_from_pixbuf(GTK_IMAGE(child), row->sprite);
    break;
  case IMPR_ROW_DESC:
    {
      gtk_label_set_text(GTK_LABEL(child), row->description);

      if (row->redundant) {
        PangoAttrList *attributes;
        PangoAttribute *attr;

        attributes = pango_attr_list_new();
        attr = pango_attr_strikethrough_new(TRUE);
        pango_attr_list_insert(attributes, attr);

        gtk_label_set_attributes(GTK_LABEL(child), attributes);
      }
    }
    break;
  case IMPR_ROW_UPKEEP:
    {
      char buf[256];

      fc_snprintf(buf, sizeof(buf), "%d", row->upkeep);
      gtk_label_set_text(GTK_LABEL(child), buf);
    }
    break;
  }
}

/**********************************************************************//**
  Improvement table cell setup function
**************************************************************************/
static void impr_factory_setup(GtkSignalListItemFactory *self,
                               GtkListItem *list_item,
                               gpointer user_data)
{
  switch (GPOINTER_TO_INT(user_data)) {
  case IMPR_ROW_PIXBUF:
    gtk_list_item_set_child(list_item, gtk_image_new());
    break;
  case IMPR_ROW_DESC:
  case IMPR_ROW_UPKEEP:
    gtk_list_item_set_child(list_item, gtk_label_new(""));
    break;
  }
}

/**********************************************************************//**
  Callback for getting main list row tooltip
**************************************************************************/
static gboolean query_impr_tooltip(GtkWidget *widget, gint x, gint y,
                                   gboolean keyboard_tip,
                                   GtkTooltip *tooltip,
                                   gpointer data)
{
  int rnum = get_column_view_row(widget, y);

  if (rnum >= 0) {
    FcImprRow *row = g_list_model_get_item(G_LIST_MODEL(data), rnum);

    if (row != nullptr && row->tooltip != nullptr) {
      gtk_tooltip_set_markup(tooltip, row->tooltip);

      return TRUE;
    }
  }

  return FALSE;
}

/***********************************************************************//**
  Create improvements list
***************************************************************************/
static GtkWidget *create_citydlg_improvement_list(struct city_dialog *pdialog)
{
  GtkWidget *list;
  GtkColumnViewColumn *column;
  GtkListItemFactory *factory;
  GtkSingleSelection *selection;

  /* Improvements */
  pdialog->overview.improvement_list = g_list_store_new(FC_TYPE_IMPR_ROW);

  selection = gtk_single_selection_new(G_LIST_MODEL(pdialog->overview.improvement_list));
  list = gtk_column_view_new(GTK_SELECTION_MODEL(selection));

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(impr_factory_bind),
                   GINT_TO_POINTER(IMPR_ROW_PIXBUF));
  g_signal_connect(factory, "setup", G_CALLBACK(impr_factory_setup),
                   GINT_TO_POINTER(IMPR_ROW_PIXBUF));

  column = gtk_column_view_column_new(_("Pix"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(list), column);

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(impr_factory_bind),
                   GINT_TO_POINTER(IMPR_ROW_DESC));
  g_signal_connect(factory, "setup", G_CALLBACK(impr_factory_setup),
                   GINT_TO_POINTER(IMPR_ROW_DESC));

  column = gtk_column_view_column_new(_("Description"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(list), column);

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(impr_factory_bind),
                   GINT_TO_POINTER(IMPR_ROW_UPKEEP));
  g_signal_connect(factory, "setup", G_CALLBACK(impr_factory_setup),
                   GINT_TO_POINTER(IMPR_ROW_UPKEEP));

  column = gtk_column_view_column_new(_("Upkeep"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(list), column);

  g_object_set(list, "has-tooltip", TRUE, nullptr);
  g_signal_connect(list, "query-tooltip",
                   G_CALLBACK(query_impr_tooltip), pdialog->overview.improvement_list);

  g_signal_connect(list, "activate",
                   G_CALLBACK(impr_callback), pdialog);

  return list;
}

/**********************************************************************//**
  Prod table cell bind function
**************************************************************************/
static void prod_factory_bind(GtkSignalListItemFactory *self,
                              GtkListItem *list_item,
                              gpointer user_data)
{
  FcProdRow *row;
  GtkWidget *child = gtk_list_item_get_child(list_item);

  row = gtk_list_item_get_item(list_item);

  switch (GPOINTER_TO_INT(user_data)) {
  case PROD_ROW_PIXBUF:
    gtk_image_set_from_pixbuf(GTK_IMAGE(child), row->sprite);
    break;
  case PROD_ROW_NAME:
    {
      gtk_label_set_text(GTK_LABEL(child), row->name);

      if (row->useless) {
        PangoAttrList *attributes;
        PangoAttribute *attr;

        attributes = pango_attr_list_new();
        attr = pango_attr_strikethrough_new(TRUE);
        pango_attr_list_insert(attributes, attr);

        gtk_label_set_attributes(GTK_LABEL(child), attributes);
      }
    }
    break;
  }
}

/**********************************************************************//**
  Prod table cell setup function
**************************************************************************/
static void prod_factory_setup(GtkSignalListItemFactory *self,
                               GtkListItem *list_item,
                               gpointer user_data)
{
  switch (GPOINTER_TO_INT(user_data)) {
  case PROD_ROW_PIXBUF:
    gtk_list_item_set_child(list_item, gtk_image_new());
    break;
  case PROD_ROW_NAME:
    gtk_list_item_set_child(list_item, gtk_label_new(""));
    break;
  }
}

/***********************************************************************//**
                  **** Overview page ****
 +- GtkWidget *page ------------------------------------------+
 | +- GtkWidget *middle -----------+------------------------+ |
 | | City map                      |  Production            | |
 | +-------------------------------+------------------------+ |
 +------------------------------------------------------------+
 | +- GtkWidget *bottom -------+----------------------------+ |
 | | Info                      | +- GtkWidget *right -----+ | |
 | |                           | | supported units        | | |
 | |                           | +------------------------+ | |
 | |                           | | present units          | | |
 | |                           | +------------------------+ | |
 | +---------------------------+----------------------------+ |
 +------------------------------------------------------------+
***************************************************************************/
static void create_and_append_overview_page(struct city_dialog *pdialog)
{
  GtkWidget *page, *bottom;
  GtkWidget *right, *frame, *table;
  GtkWidget *label, *sw, *view, *bar;
  /* TRANS: Overview tab in city dialog */
  const char *tab_title = _("_Overview");
  int unit_height = tileset_unit_with_upkeep_height(tileset);
  int page_row = 0;

  /* Main page */
  page = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(page),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start(page, 8);
  gtk_widget_set_margin_end(page, 8);
  gtk_widget_set_margin_top(page, 8);
  gtk_widget_set_margin_bottom(page, 8);
  label = gtk_label_new_with_mnemonic(tab_title);
  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  if (!low_citydlg) {
    GtkListItemFactory *factory;
    GtkWidget *list;
    GtkColumnViewColumn *column;
    GtkWidget *middle;
    GtkWidget *vbox;
    GtkWidget *hbox;
    int middle_col = 0;

    /* Middle: city map, improvements */
    middle = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(middle), 6);
    gtk_grid_attach(GTK_GRID(page), middle, 0, page_row++, 1, 1);

    /* City map */
    create_citydlg_main_map(pdialog, middle, middle_col++, 0);

    /* Improvements */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_grid_attach(GTK_GRID(middle), vbox, middle_col++, 0, 1, 1);

    view = create_citydlg_improvement_list(pdialog);

    label = g_object_new(GTK_TYPE_LABEL, "label", _("Production:"),
                         "xalign", 0.0, "yalign", 0.5, NULL);
    gtk_box_append(GTK_BOX(vbox), label);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(vbox), hbox);

    pdialog->overview.change_prod_store = g_list_store_new(FC_TYPE_PROD_ROW);

    pdialog->overview.change_prod_selection
      = gtk_single_selection_new(G_LIST_MODEL(pdialog->overview.change_prod_store));
    list = gtk_column_view_new(GTK_SELECTION_MODEL(pdialog->overview.change_prod_selection));

    factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "bind", G_CALLBACK(prod_factory_bind),
                     GINT_TO_POINTER(PROD_ROW_PIXBUF));
    g_signal_connect(factory, "setup", G_CALLBACK(prod_factory_setup),
                     GINT_TO_POINTER(PROD_ROW_PIXBUF));

    column = gtk_column_view_column_new(_("Pix"), factory);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(list), column);

    factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "bind", G_CALLBACK(prod_factory_bind),
                     GINT_TO_POINTER(PROD_ROW_NAME));
    g_signal_connect(factory, "setup", G_CALLBACK(prod_factory_setup),
                     GINT_TO_POINTER(PROD_ROW_NAME));

    column = gtk_column_view_column_new(_("Name"), factory);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(list), column);

    bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(bar), TRUE);
    pdialog->overview.production_bar = bar;
    gtk_box_append(GTK_BOX(hbox), bar);

    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bar), _("%d/%d %d turns"));

    gtk_box_append(GTK_BOX(hbox), list);

    g_signal_connect(pdialog->overview.change_prod_selection, "selection-changed",
                     G_CALLBACK(change_production_callback), pdialog);

    pdialog->overview.buy_command
      = icon_label_button_new("system-run", _("_Buy"));
    gtk_box_append(GTK_BOX(hbox), GTK_WIDGET(pdialog->overview.buy_command));
    g_signal_connect(pdialog->overview.buy_command, "clicked",
                     G_CALLBACK(buy_callback), pdialog);

    label = g_object_new(GTK_TYPE_LABEL, "use-underline", TRUE,
                         "mnemonic-widget", view,
                         "label", _("I_mprovements:"),
                         "xalign", 0.0, "yalign", 0.5, NULL);
    gtk_box_append(GTK_BOX(vbox), label);
    gtk_box_append(GTK_BOX(vbox), view);
  } else {
    pdialog->overview.buy_command = NULL;
    pdialog->overview.production_bar = NULL;
    pdialog->overview.change_prod_store = nullptr;
    pdialog->overview.change_prod_selection = nullptr;
  }

  /* Bottom: info, units */
  bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_grid_attach(GTK_GRID(page), bottom, 0, page_row++, 1, 1);

  /* Info */
  frame = gtk_frame_new(_("Info"));
  gtk_box_append(GTK_BOX(bottom), frame);

  table = create_city_info_table(pdialog,
                                 pdialog->overview.info_label);
  gtk_widget_set_halign(table, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(table, GTK_ALIGN_CENTER);
  gtk_frame_set_child(GTK_FRAME(frame), table);

  /* Right: present and supported units (overview page) */
  right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(bottom), right);

  pdialog->overview.supported_units_frame = gtk_frame_new("");
  gtk_box_append(GTK_BOX(right), pdialog->overview.supported_units_frame);
  pdialog->overview.present_units_frame = gtk_frame_new("");
  gtk_box_append(GTK_BOX(right), pdialog->overview.present_units_frame);

  /* Supported units */
  sw = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(sw, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), FALSE);
  gtk_frame_set_child(GTK_FRAME(pdialog->overview.supported_units_frame),
                      sw);


  table = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_size_request(table, -1, unit_height);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), table);

  pdialog->overview.supported_unit_table = table;
  unit_node_vector_init(&pdialog->overview.supported_units);

  /* Present units */
  sw = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(sw, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), FALSE);
  gtk_frame_set_child(GTK_FRAME(pdialog->overview.present_units_frame), sw);

  table = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_size_request(table, -1, unit_height);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), table);

  pdialog->overview.present_unit_table = table;
  unit_node_vector_init(&pdialog->overview.present_units);

  /* Show page */
  gtk_widget_set_visible(page, TRUE);
}

/***********************************************************************//**
  Create map page for small screens
***************************************************************************/
static void create_and_append_map_page(struct city_dialog *pdialog)
{
  if (low_citydlg) {
    GtkWidget *page;
    GtkWidget *label;
    const char *tab_title = _("Citymap");
    int page_row = 0;

    page = gtk_grid_new();
    gtk_orientable_set_orientation(GTK_ORIENTABLE(page),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start(page, 8);
    gtk_widget_set_margin_end(page, 8);
    gtk_widget_set_margin_top(page, 8);
    gtk_widget_set_margin_bottom(page, 8);
    label = gtk_label_new_with_mnemonic(tab_title);
    gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

    create_citydlg_main_map(pdialog, page, 0, page_row++);

    gtk_widget_set_visible(page, TRUE);
  }
}

/***********************************************************************//**
  Something dragged to worklist dialog.
***************************************************************************/
static gboolean target_drag_data_received(GtkDropTarget *target,
                                          const GValue *value,
                                          double x, double y, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  cid id;
  struct universal univ;

  if (NULL != client.conn.playing
      && city_owner(pdialog->pcity) != client.conn.playing) {
    return FALSE;
  }

  id = g_value_get_int(value);
  univ = cid_production(id);

  city_change_production(pdialog->pcity, &univ);

  return TRUE;
}

/***********************************************************************//**
  Create production page header - what tab this actually is,
  depends on screen size and layout.
***************************************************************************/
static int create_production_header(struct city_dialog *pdialog,
                                    GtkWidget *grid, int row)
{
  GtkWidget *hgrid, *bar;
  int grid_col = 0;

  hgrid = gtk_grid_new();
  gtk_widget_set_margin_bottom(hgrid, 2);
  gtk_widget_set_margin_end(hgrid, 2);
  gtk_widget_set_margin_start(hgrid, 2);
  gtk_widget_set_margin_top(hgrid, 2);
  gtk_grid_set_column_spacing(GTK_GRID(hgrid), 10);
  gtk_grid_attach(GTK_GRID(grid), hgrid, 0, row++, 1, 1);

  /* The label is set in city_dialog_update_building() */
  bar = gtk_progress_bar_new();
  gtk_widget_set_hexpand(bar, TRUE);
  gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(bar), TRUE);
  pdialog->production.production_bar = bar;
  gtk_grid_attach(GTK_GRID(hgrid), bar, grid_col++, 0, 1, 1);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bar), _("%d/%d %d turns"));

  add_worklist_dnd_target(bar, target_drag_data_received, pdialog);

  pdialog->production.buy_command
    = icon_label_button_new("system-run", _("_Buy"));
  gtk_grid_attach(GTK_GRID(hgrid), GTK_WIDGET(pdialog->production.buy_command),
                  grid_col++, 0, 1, 1);

  g_signal_connect(pdialog->production.buy_command, "clicked",
                   G_CALLBACK(buy_callback), pdialog);

  return row;
}

/***********************************************************************//**
  Create buildings list page for small screens
***************************************************************************/
static void create_and_append_buildings_page(struct city_dialog *pdialog)
{
  if (low_citydlg) {
    GtkWidget *page;
    GtkWidget *label;
    GtkWidget *vbox;
    GtkWidget *view;
    const char *tab_title = _("Buildings");
    int page_row = 0;

    page = gtk_grid_new();
    gtk_orientable_set_orientation(GTK_ORIENTABLE(page),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start(page, 8);
    gtk_widget_set_margin_end(page, 8);
    gtk_widget_set_margin_top(page, 8);
    gtk_widget_set_margin_bottom(page, 8);
    label = gtk_label_new_with_mnemonic(tab_title);

    page_row = create_production_header(pdialog, page, page_row);
    gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_grid_attach(GTK_GRID(page), vbox, 0, page_row++, 1, 1);

    view = create_citydlg_improvement_list(pdialog);

    gtk_box_append(GTK_BOX(vbox), view);

    gtk_widget_set_visible(page, TRUE);
  }
}

/***********************************************************************//**
                    **** Production Page ****
***************************************************************************/
static void create_and_append_worklist_page(struct city_dialog *pdialog)
{
  const char *tab_title = _("P_roduction");
  GtkWidget *label = gtk_label_new_with_mnemonic(tab_title);
  GtkWidget *page, *editor;
  int page_row = 0;

  page = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(page),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start(page, 8);
  gtk_widget_set_margin_end(page, 8);
  gtk_widget_set_margin_top(page, 8);
  gtk_widget_set_margin_bottom(page, 8);
  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  /* Stuff that's being currently built */
  if (!low_citydlg) {
    label = g_object_new(GTK_TYPE_LABEL,
                         "label", _("Production:"),
                         "xalign", 0.0, "yalign", 0.5, NULL);
    pdialog->production.production_label = label;
    gtk_grid_attach(GTK_GRID(page), label, 0, page_row++, 1, 1);

    page_row = create_production_header(pdialog, page, page_row);
  } else {
    pdialog->production.production_label = NULL;
  }

  editor = create_worklist();
  gtk_widget_set_margin_bottom(editor, 6);
  gtk_widget_set_margin_end(editor, 6);
  gtk_widget_set_margin_start(editor, 6);
  gtk_widget_set_margin_top(editor, 6);
  reset_city_worklist(editor, pdialog->pcity);
  gtk_grid_attach(GTK_GRID(page), editor, 0, page_row++, 1, 1);
  pdialog->production.worklist = editor;

  gtk_widget_set_visible(page, TRUE);
}

/***********************************************************************//**
                     **** Happiness Page ****
 +- GtkWidget *page ----------+-------------------------------------------+
 | +- GtkWidget *left ------+ | +- GtkWidget *right --------------------+ |
 | | Info                   | | | City map                              | |
 | +- GtkWidget *citizens --+ | +- GtkWidget pdialog->happiness.widget -+ |
 | | Citizens data          | | | Happiness                             | |
 | +------------------------+ | +---------------------------------------+ |
 +----------------------------+-------------------------------------------+
***************************************************************************/
static void create_and_append_happiness_page(struct city_dialog *pdialog)
{
  GtkWidget *page, *label, *table, *right, *left, *frame;
  const char *tab_title = _("Happ_iness");
  int page_col = 0;
  int left_row = 0;

  /* Main page */
  page = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(page), 6);
  gtk_widget_set_margin_start(page, 8);
  gtk_widget_set_margin_end(page, 8);
  gtk_widget_set_margin_top(page, 8);
  gtk_widget_set_margin_bottom(page, 8);
  label = gtk_label_new_with_mnemonic(tab_title);
  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  /* Left: info, citizens */
  left = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(left),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_attach(GTK_GRID(page), left, page_col++, 0, 1, 1);

  if (!low_citydlg) {
    /* Upper left: info */
    frame = gtk_frame_new(_("Info"));
    gtk_grid_attach(GTK_GRID(left), frame, 0, left_row++, 1, 1);

    table = create_city_info_table(pdialog,
                                   pdialog->happiness.info_label);
    gtk_widget_set_halign(table, GTK_ALIGN_CENTER);
    gtk_frame_set_child(GTK_FRAME(frame), table);
  }

  /* Lower left: citizens */
  if (game.info.citizen_nationality) {
    pdialog->happiness.citizens = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_grid_attach(GTK_GRID(left), pdialog->happiness.citizens,
                    0, left_row++, 1, 1);
    gtk_box_append(GTK_BOX(pdialog->happiness.citizens),
                   citizens_dialog_display(pdialog->pcity));
  }

  /* Right: city map, happiness */
  right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_grid_attach(GTK_GRID(page), right, page_col++, 0, 1, 1);

  if (!low_citydlg) {
    /* Upper right: city map */
    frame = gtk_frame_new(_("City map"));
    gtk_widget_set_size_request(frame, CITY_MAP_MIN_SIZE_X,
                                CITY_MAP_MIN_SIZE_Y);
    gtk_box_append(GTK_BOX(right), frame);

    city_dialog_map_create(pdialog, &pdialog->happiness.map_canvas);
    gtk_frame_set_child(GTK_FRAME(frame), pdialog->happiness.map_canvas.sw);
  }

  /* Lower right: happiness */
  pdialog->happiness.widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(right), pdialog->happiness.widget);
  gtk_box_append(GTK_BOX(pdialog->happiness.widget),
                 get_top_happiness_display(pdialog->pcity, low_citydlg,
                                           pdialog->shell));

  /* Show page */
  gtk_widget_set_visible(page, TRUE);
}

/**********************************************************************//**
                    **** Counters Page ****
**************************************************************************/
/**********************************************************************//**
  Creates counters page
**************************************************************************/

static void create_and_append_counters_page(struct city_dialog *pdialog)
{
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkLabel *label =  GTK_LABEL(gtk_label_new(_("Counters")));

  pdialog->counters.container = NULL;
  pdialog->counters.widget = NULL;

  city_dialog_update_counters(pdialog);

  gtk_box_append(GTK_BOX(page), GTK_WIDGET(pdialog->counters.container));

  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, GTK_WIDGET(label));

  gtk_widget_show(page);
}

/***********************************************************************//**
            **** Citizen Management Agent (CMA) Page ****
***************************************************************************/
static void create_and_append_cma_page(struct city_dialog *pdialog)
{
  GtkWidget *page, *label;
  const char *tab_title = _("_Governor");

  page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  label = gtk_label_new_with_mnemonic(tab_title);

  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  pdialog->cma_editor = create_cma_dialog(pdialog->pcity, low_citydlg);
  gtk_box_append(GTK_BOX(page), pdialog->cma_editor->shell);

  gtk_widget_set_visible(page, TRUE);
}

/***********************************************************************//**
                    **** Misc. Settings Page ****
***************************************************************************/
static void create_and_append_settings_page(struct city_dialog *pdialog)
{
  int i;
  GtkWidget *vgrid, *page, *frame, *label, *button;
  GtkSizeGroup *size;
  GtkWidget *group;
  const char *tab_title = _("_Settings");
  int grid_row = 0;

  static const char *new_citizens_output_label[] = {
    N_("Luxury"),
    N_("Science"),
    N_("Gold")
  };

  static const char *disband_label
    = N_("Allow unit production to disband city");

  static const char *misc_whichtab_label[NUM_PAGES] = {
    N_("Overview page"),
    N_("Production page"),
    N_("Happiness page"),
    N_("Counters Page"),
    N_("Governor page"),
    N_("This Settings page"),
    N_("Last active page")
  };

  static bool new_citizens_label_done;
  static bool misc_whichtab_label_done;

  /* Initialize signal_blocker */
  pdialog->misc.block_signal = 0;


  page = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(page), 18);
  gtk_widget_set_margin_start(page, 8);
  gtk_widget_set_margin_end(page, 8);
  gtk_widget_set_margin_top(page, 8);
  gtk_widget_set_margin_bottom(page, 8);

  size = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);

  label = gtk_label_new_with_mnemonic(tab_title);

  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  /* new_citizens radio */
  frame = gtk_frame_new(_("New citizens produce"));
  gtk_grid_attach(GTK_GRID(page), frame, 0, 0, 1, 1);
  gtk_size_group_add_widget(size, frame);

  vgrid = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_frame_set_child(GTK_FRAME(frame), vgrid);

  intl_slist(ARRAY_SIZE(new_citizens_output_label), new_citizens_output_label,
             &new_citizens_label_done);

  group = NULL;
  for (i = 0; i < ARRAY_SIZE(new_citizens_output_label); i++) {
    button = gtk_check_button_new_with_mnemonic(new_citizens_output_label[i]);
    gtk_check_button_set_group(GTK_CHECK_BUTTON(button),
                               GTK_CHECK_BUTTON(group));
    pdialog->misc.new_citizens_radio[i] = button;
    gtk_grid_attach(GTK_GRID(vgrid), button, 0, grid_row++, 1, 1);
    g_signal_connect(button, "toggled",
                     G_CALLBACK(cityopt_callback), pdialog);
    group = button;
  }

  /* Next is the next-time-open radio group in the right column */
  frame = gtk_frame_new(_("Next time open"));
  gtk_grid_attach(GTK_GRID(page), frame, 1, 0, 1, 1);
  gtk_size_group_add_widget(size, frame);

  vgrid = gtk_grid_new();
  grid_row = 0;
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_frame_set_child(GTK_FRAME(frame), vgrid);

  intl_slist(ARRAY_SIZE(misc_whichtab_label), misc_whichtab_label,
             &misc_whichtab_label_done);

  group = NULL;
  for (i = 0; i < ARRAY_SIZE(misc_whichtab_label); i++) {
    button = gtk_check_button_new_with_mnemonic(misc_whichtab_label[i]);
    gtk_check_button_set_group(GTK_CHECK_BUTTON(button),
                               GTK_CHECK_BUTTON(group));
    pdialog->misc.whichtab_radio[i] = button;
    gtk_grid_attach(GTK_GRID(vgrid), button, 0, grid_row++, 1, 1);
    g_signal_connect(button, "toggled",
                     G_CALLBACK(misc_whichtab_callback), GINT_TO_POINTER(i));
    group = button;
  }

  /* Now we go back and fill the hbox rename */
  frame = gtk_frame_new(_("City"));
  gtk_widget_set_margin_top(frame, 12);
  gtk_widget_set_margin_bottom(frame, 12);
  gtk_grid_attach(GTK_GRID(page), frame, 0, 1, 1, 1);

  vgrid = gtk_grid_new();
  grid_row = 0;
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_frame_set_child(GTK_FRAME(frame), vgrid);

  button = gtk_button_new_with_mnemonic(_("R_ename..."));
  pdialog->misc.rename_command = button;
  gtk_grid_attach(GTK_GRID(vgrid), button, 0, grid_row++, 1, 1);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(rename_callback), pdialog);

  gtk_widget_set_sensitive(button, can_client_issue_orders());

  /* The disband-city-on-unit-production button */
  button = gtk_check_button_new_with_mnemonic(_(disband_label));
  pdialog->misc.disband_on_settler = button;
  gtk_grid_attach(GTK_GRID(vgrid), button, 0, grid_row++, 1, 1);
  g_signal_connect(button, "toggled",
                   G_CALLBACK(cityopt_callback), pdialog);

  /* We choose which page to popup by default */
  gtk_check_button_set_active(GTK_CHECK_BUTTON
                              (pdialog->
                               misc.whichtab_radio[new_dialog_def_page]),
                              TRUE);

  set_cityopt_values(pdialog);

  gtk_widget_set_visible(page, TRUE);

  if (new_dialog_def_page == (NUM_PAGES - 1)) {
    gtk_notebook_set_current_page(GTK_NOTEBOOK(pdialog->notebook),
                                  last_page);
  } else {
    gtk_notebook_set_current_page(GTK_NOTEBOOK(pdialog->notebook),
                                  new_dialog_def_page);
  }
}

/***********************************************************************//**
                     **** Main City Dialog ****
 +----------------------------+-------------------------------+
 | GtkWidget *top: Citizens   | city name                     |
 +----------------------------+-------------------------------+
 | [notebook tab]                                             |
 +------------------------------------------------------------+
***************************************************************************/
static struct city_dialog *create_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog;
  GtkWidget *close_command;
  GtkWidget *vbox, *hbox, *cbox;
  int citizen_bar_width;
  int citizen_bar_height;
  int ccol = 0;
  GtkEventController *controller;
  struct player *owner;

  if (!city_dialogs_have_been_initialised) {
    initialize_city_dialogs();
  }

  pdialog = fc_malloc(sizeof(struct city_dialog));
  pdialog->pcity = pcity;
  pdialog->sell_shell = NULL;
  pdialog->rename_shell = NULL;
  pdialog->happiness.map_canvas.sw = NULL;      /* Make sure NULL if spy */
  pdialog->happiness.map_canvas.darea = NULL;   /* Ditto */
  pdialog->happiness.citizens = NULL;           /* Ditto */
  pdialog->counters.widget = NULL;
  pdialog->counters.container = NULL;
  pdialog->production.buy_command = NULL;
  pdialog->production.production_label = NULL;
  pdialog->production.production_bar = NULL;
  pdialog->cma_editor = NULL;
  pdialog->map_canvas_store_unscaled
    = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                 canvas_width, canvas_height);

  pdialog->shell = gtk_dialog_new();
  gtk_window_set_title(GTK_WINDOW(pdialog->shell), city_name_get(pcity));
  setup_dialog(pdialog->shell, toplevel);

  g_signal_connect(pdialog->shell, "destroy",
                   G_CALLBACK(city_destroy_callback), pdialog);
  gtk_widget_set_name(pdialog->shell, "Freeciv");

  gtk_widget_realize(pdialog->shell);

  /* Keep the icon of the executable on Windows (see PR#36491) */
#ifndef FREECIV_MSWINDOWS
  {
    gtk_window_set_icon_name(GTK_WINDOW(pdialog->shell), "citydlg");
  }
#endif /* FREECIV_MSWINDOWS */

  /* Restore size of the city dialog. */
  gtk_window_set_default_size(GTK_WINDOW(pdialog->shell),
                              GUI_GTK_OPTION(citydlg_xsize),
                              GUI_GTK_OPTION(citydlg_ysize));

  pdialog->popover = NULL;

  vbox = gtk_dialog_get_content_area(GTK_DIALOG(pdialog->shell));
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_set_homogeneous(GTK_BOX(hbox), TRUE);
  gtk_box_append(GTK_BOX(vbox), hbox);

  /**** Citizens bar here ****/
  cbox = gtk_grid_new();
  gtk_box_append(GTK_BOX(hbox), cbox);

  citizen_bar_width = tileset_small_sprite_width(tileset) * NUM_CITIZENS_SHOWN;
  citizen_bar_height = tileset_small_sprite_height(tileset);

  pdialog->citizen_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                        citizen_bar_width,
                                                        citizen_bar_height);
  pdialog->citizen_pics = gtk_picture_new();

  gtk_widget_set_margin_start(pdialog->citizen_pics, 2);
  gtk_widget_set_margin_end(pdialog->citizen_pics, 2);
  gtk_widget_set_margin_top(pdialog->citizen_pics, 2);
  gtk_widget_set_margin_bottom(pdialog->citizen_pics, 2);
  gtk_widget_set_size_request(pdialog->citizen_pics,
                              citizen_bar_width, citizen_bar_height);
  gtk_widget_set_halign(pdialog->citizen_pics, GTK_ALIGN_START);
  gtk_widget_set_valign(pdialog->citizen_pics, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(cbox), pdialog->citizen_pics, ccol++, 0, 1, 1);

  controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(citizens_callback), pdialog);
  gtk_widget_add_controller(pdialog->citizen_pics, controller);

  /**** City name label here ****/
  pdialog->name_label = gtk_label_new(NULL);
  gtk_widget_set_hexpand(pdialog->name_label, TRUE);
  gtk_widget_set_halign(pdialog->name_label, GTK_ALIGN_START);
  gtk_widget_set_valign(pdialog->name_label, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(hbox), pdialog->name_label);

  /**** -Start of Notebook- ****/

  pdialog->notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(pdialog->notebook),
                           GTK_POS_BOTTOM);
  gtk_box_append(GTK_BOX(vbox), pdialog->notebook);

  create_and_append_overview_page(pdialog);
  create_and_append_map_page(pdialog);
  create_and_append_buildings_page(pdialog);

  owner = city_owner(pcity);

  /* Only create these tabs if not a spy */
  if (owner == client_player() || client_is_global_observer()) {
    create_and_append_worklist_page(pdialog);
    create_and_append_happiness_page(pdialog);
  }

  create_and_append_counters_page(pdialog);

  if (owner == client_player()
      && !client_is_observer()) {
    create_and_append_cma_page(pdialog);
    create_and_append_settings_page(pdialog);
  } else {
    gtk_notebook_set_current_page(GTK_NOTEBOOK(pdialog->notebook),
                                  OVERVIEW_PAGE);
  }

  /**** End of Notebook ****/

  /* Bottom buttons */

  pdialog->show_units_command =
    gtk_dialog_add_button(GTK_DIALOG(pdialog->shell), _("_List present units..."), CDLGR_UNITS);

  g_signal_connect(GTK_DIALOG(pdialog->shell), "response",
                   G_CALLBACK(citydlg_response_callback), pdialog);

  pdialog->prev_command = gtk_button_new_from_icon_name("go-previous");
  gtk_dialog_add_action_widget(GTK_DIALOG(pdialog->shell),
                               GTK_WIDGET(pdialog->prev_command), 1);

  pdialog->next_command = gtk_button_new_from_icon_name("go-next");
  gtk_dialog_add_action_widget(GTK_DIALOG(pdialog->shell),
                               GTK_WIDGET(pdialog->next_command), 2);

  if (owner != client_player()) {
    gtk_widget_set_sensitive(GTK_WIDGET(pdialog->prev_command), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(pdialog->next_command), FALSE);
  }

  close_command = gtk_dialog_add_button(GTK_DIALOG(pdialog->shell),
                                        _("_Close"), GTK_RESPONSE_CLOSE);

  gtk_dialog_set_default_response(GTK_DIALOG(pdialog->shell),
                                  GTK_RESPONSE_CLOSE);

  g_signal_connect(close_command, "clicked",
                   G_CALLBACK(close_callback), pdialog);

  g_signal_connect(pdialog->prev_command, "clicked",
                   G_CALLBACK(switch_city_callback), pdialog);

  g_signal_connect(pdialog->next_command, "clicked",
                   G_CALLBACK(switch_city_callback), pdialog);

  /* Some other things we gotta do */

  controller = gtk_event_controller_key_new();
  g_signal_connect(controller, "key-pressed",
                   G_CALLBACK(citydlg_keyboard_handler), pdialog);
  gtk_widget_add_controller(pdialog->shell, controller);

  dialog_list_prepend(dialog_list, pdialog);

  real_city_dialog_refresh(pdialog->pcity);

  /* Need to do this every time a new dialog is opened. */
  city_dialog_update_prev_next();

  gtk_widget_set_visible(pdialog->shell, TRUE);

  gtk_window_set_focus(GTK_WINDOW(pdialog->shell), close_command);

  return pdialog;
}

/**************** Functions to update parts of the dialog *****************/

/***********************************************************************//**
  Update title of city dialog.
***************************************************************************/
static void city_dialog_update_title(struct city_dialog *pdialog)
{
  gchar *buf;
  const gchar *now;

  if (city_unhappy(pdialog->pcity)) {
    /* TRANS: city dialog title */
    buf = g_strdup_printf(_("<b>%s</b> - %s citizens - DISORDER"),
                          city_name_get(pdialog->pcity),
                          population_to_text(city_population(pdialog->pcity)));
  } else if (city_celebrating(pdialog->pcity)) {
    /* TRANS: city dialog title */
    buf = g_strdup_printf(_("<b>%s</b> - %s citizens - celebrating"),
                          city_name_get(pdialog->pcity),
                          population_to_text(city_population(pdialog->pcity)));
  } else if (city_happy(pdialog->pcity)) {
    /* TRANS: city dialog title */
    buf = g_strdup_printf(_("<b>%s</b> - %s citizens - happy"),
                          city_name_get(pdialog->pcity),
                          population_to_text(city_population(pdialog->pcity)));
  } else {
    /* TRANS: city dialog title */
    buf = g_strdup_printf(_("<b>%s</b> - %s citizens"),
                          city_name_get(pdialog->pcity),
                          population_to_text(city_population(pdialog->pcity)));
  }

  now = gtk_label_get_text(GTK_LABEL(pdialog->name_label));
  if (strcmp(now, buf) != 0) {
    gtk_window_set_title(GTK_WINDOW(pdialog->shell), city_name_get(pdialog->pcity));
    gtk_label_set_markup(GTK_LABEL(pdialog->name_label), buf);
  }

  g_free(buf);
}

/***********************************************************************//**
  Update citizens in city dialog
***************************************************************************/
static void city_dialog_update_citizens(struct city_dialog *pdialog)
{
  enum citizen_category categories[MAX_CITY_SIZE];
  int i, width, full_width, total_used_width;
  int citizen_bar_width, citizen_bar_height;
  struct city *pcity = pdialog->pcity;
  int num_citizens = get_city_citizen_types(pcity, FEELING_FINAL, categories);
  int num_supers
    = city_try_fill_superspecialists(pcity,
                                     ARRAY_SIZE(categories) - num_citizens,
                                     &categories[num_citizens]);
  cairo_t *cr;

  if (num_supers >= 0) {
    /* Just draw superspecialists in the common roster */
    num_citizens += num_supers;
  } else {
    /* FIXME: show them in some compact way */
    num_citizens = ARRAY_SIZE(categories);
  }
  /* If there is not enough space we stack the icons. We draw from left to */
  /* right. width is how far we go to the right for each drawn pixmap. The */
  /* last icon is always drawn in full, and so has reserved                */
  /* tileset_small_sprite_width(tileset) pixels.                           */

  full_width = tileset_small_sprite_width(tileset);
  if (num_citizens > 1) {
    width = MIN(full_width, ((NUM_CITIZENS_SHOWN - 1) * full_width)
                            / (num_citizens - 1));
  } else {
    width = full_width;
  }
  pdialog->cwidth = width;

  /* overview page */
  /* keep these values in sync with create_city_dialog */
  citizen_bar_width  = full_width * NUM_CITIZENS_SHOWN;
  citizen_bar_height = tileset_small_sprite_height(tileset);

  cr = cairo_create(pdialog->citizen_surface);

  for (i = 0; i < num_citizens; i++) {
    cairo_set_source_surface(cr,
                             get_citizen_sprite(tileset, categories[i], i, pcity)->surface,
                             i * width, 0);
    cairo_rectangle(cr, i * width, 0,
                    /* Always draw last citizen in full */
                    i + 1 < num_citizens ? width : full_width,
                    citizen_bar_height);
    cairo_fill(cr);
  }

  total_used_width = (i - 1) * width + full_width;

  if (total_used_width < citizen_bar_width) {
    /* Clear the rest of the area.
     * Note that this might still be necessary even in cases where
     * num_citizens > NUM_CITIZENS_SHOWN, if the available width cannot be
     * divided perfectly. */
    cairo_rectangle(cr, total_used_width, 0,
                    citizen_bar_width - total_used_width,
                    citizen_bar_height);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill(cr);
  }

  cairo_destroy(cr);

  picture_set_from_surface(GTK_PICTURE(pdialog->citizen_pics),
                           pdialog->citizen_surface);

  gtk_widget_queue_draw(pdialog->citizen_pics);
}


/**********************************************************************//**
  Update counters tab  in city dialog
**************************************************************************/
static void city_dialog_update_counters(struct city_dialog *pdialog)
{
  GtkBox *counterInfo, *valueData;
  GtkLabel *counterName;
  GtkLabel *counterValue;
  GtkLabel *counterDescription;
  char  int_val[101];
  char *text;
  int text_size;

  if (NULL != pdialog->counters.widget) {
    gtk_box_remove(GTK_BOX(gtk_widget_get_parent(GTK_WIDGET(pdialog->counters.widget))),
                   GTK_WIDGET(pdialog->counters.widget));
  }

  if (NULL == pdialog->counters.container) {
    pdialog->counters.container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  }

  pdialog->counters.widget = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  city_counters_iterate(pcount) {
    counterInfo = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));

    counterName = GTK_LABEL(gtk_label_new(name_translation_get(&pcount->name)));
    gtk_box_append(counterInfo, GTK_WIDGET(counterName));
    valueData = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    counterValue = GTK_LABEL(gtk_label_new(_("Current value is: ")));
    gtk_box_append(valueData, GTK_WIDGET(counterValue));
    fc_snprintf(int_val, sizeof(int_val), "%d", pdialog->pcity->counter_values[counter_index(pcount)]);
    counterValue = GTK_LABEL(gtk_label_new(int_val));
    gtk_box_append(valueData, GTK_WIDGET(counterValue));
    gtk_box_append(counterInfo, GTK_WIDGET(valueData));

    valueData = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    counterValue = GTK_LABEL(gtk_label_new(_("Activated once value equal or higher than: ")));
    gtk_box_append(valueData, GTK_WIDGET(counterValue));
    fc_snprintf(int_val, sizeof(int_val), "%d", pcount->checkpoint);
    counterValue = GTK_LABEL(gtk_label_new(int_val));
    gtk_box_append(valueData, GTK_WIDGET(counterValue));
    gtk_box_append(counterInfo, GTK_WIDGET(valueData));

    text_size = 0;
    if (NULL != pcount->helptext) {
      strvec_iterate(pcount->helptext, text_) {
        text_size += strlen(text_);
      } strvec_iterate_end;
    }
    if (0 < text_size) {
      text = malloc(text_size+1);
      text_size = 0;
      strvec_iterate(pcount->helptext, text_) {
        strcpy(&text[text_size], text_);
        text_size += strlen(text_);
      } strvec_iterate_end;
      counterDescription = GTK_LABEL(gtk_label_new(text));
      free(text);
      gtk_box_append(counterInfo,GTK_WIDGET(counterDescription));
    }
    gtk_box_append(pdialog->counters.widget, GTK_WIDGET(counterInfo));
  } city_counters_iterate_end;

  gtk_box_append(pdialog->counters.container, GTK_WIDGET(pdialog->counters.widget));
  gtk_widget_show(GTK_WIDGET(pdialog->counters.container));
}

/***********************************************************************//**
  Update textual info fields in city dialog
***************************************************************************/
static void city_dialog_update_information(GtkWidget **info_label,
                                           struct city_dialog *pdialog)
{
  int i, illness = 0;
  char buf[NUM_INFO_FIELDS][512];
  struct city *pcity = pdialog->pcity;
  int granaryturns;
  int non_workers = city_specialists(pcity);
  int supers = city_superspecialists(pcity);

  /* fill the buffers with the necessary info */
  if (supers) {
    fc_snprintf(buf[INFO_SIZE], sizeof(buf[INFO_SIZE]), "%3d (%3d+%d)",
                pcity->size, non_workers, supers);
  } else if (non_workers) {
    fc_snprintf(buf[INFO_SIZE], sizeof(buf[INFO_SIZE]), "%3d (%3d)",
                pcity->size, non_workers);
  } else {
    fc_snprintf(buf[INFO_SIZE], sizeof(buf[INFO_SIZE]), "%3d", pcity->size);
  }
  fc_snprintf(buf[INFO_FOOD], sizeof(buf[INFO_FOOD]), "%3d (%+4d)",
              pcity->prod[O_FOOD], pcity->surplus[O_FOOD]);
  fc_snprintf(buf[INFO_SHIELD], sizeof(buf[INFO_SHIELD]), "%3d (%+4d)",
              pcity->prod[O_SHIELD] + pcity->waste[O_SHIELD],
              pcity->surplus[O_SHIELD]);
  fc_snprintf(buf[INFO_TRADE], sizeof(buf[INFO_TRADE]), "%3d (%+4d)",
              pcity->surplus[O_TRADE] + pcity->waste[O_TRADE],
              pcity->surplus[O_TRADE]);
  fc_snprintf(buf[INFO_GOLD], sizeof(buf[INFO_GOLD]), "%3d (%+4d)",
              pcity->prod[O_GOLD], pcity->surplus[O_GOLD]);
  fc_snprintf(buf[INFO_LUXURY], sizeof(buf[INFO_LUXURY]), "%3d",
              pcity->prod[O_LUXURY]);
  fc_snprintf(buf[INFO_SCIENCE], sizeof(buf[INFO_SCIENCE]), "%3d",
              pcity->prod[O_SCIENCE]);
  fc_snprintf(buf[INFO_GRANARY], sizeof(buf[INFO_GRANARY]), "%4d/%-4d",
              pcity->food_stock, city_granary_size(city_size_get(pcity)));

  granaryturns = city_turns_to_grow(pcity);
  if (granaryturns == 0) {
    /* TRANS: city growth is blocked.  Keep short. */
    fc_snprintf(buf[INFO_GROWTH], sizeof(buf[INFO_GROWTH]), _("blocked"));
  } else if (granaryturns == FC_INFINITY) {
    /* TRANS: city is not growing.  Keep short. */
    fc_snprintf(buf[INFO_GROWTH], sizeof(buf[INFO_GROWTH]), _("never"));
  } else {
    /* A negative value means we'll have famine in that many turns.
       But that's handled down below. */
    /* TRANS: city growth turns.  Keep short. */
    fc_snprintf(buf[INFO_GROWTH], sizeof(buf[INFO_GROWTH]),
                PL_("%d turn", "%d turns", abs(granaryturns)),
                abs(granaryturns));
  }
  fc_snprintf(buf[INFO_CORRUPTION], sizeof(buf[INFO_CORRUPTION]), "%4d",
              pcity->waste[O_TRADE]);
  fc_snprintf(buf[INFO_WASTE], sizeof(buf[INFO_WASTE]), "%4d",
              pcity->waste[O_SHIELD]);
  fc_snprintf(buf[INFO_CULTURE], sizeof(buf[INFO_CULTURE]), "%4d",
              pcity->client.culture);
  fc_snprintf(buf[INFO_POLLUTION], sizeof(buf[INFO_POLLUTION]), "%4d",
              pcity->pollution);
  if (!game.info.illness_on) {
    fc_snprintf(buf[INFO_ILLNESS], sizeof(buf[INFO_ILLNESS]), "  -.-");
  } else {
    illness = city_illness_calc(pcity, NULL, NULL, NULL, NULL);
    /* illness is in tenth of percent */
    fc_snprintf(buf[INFO_ILLNESS], sizeof(buf[INFO_ILLNESS]), "%5.1f%%",
                (float)illness / 10.0);
  }
  if (pcity->steal) {
    fc_snprintf(buf[INFO_STEAL], sizeof(buf[INFO_STEAL]), PL_("%d time", "%d times", pcity->steal),
                pcity->steal);
  } else {
    fc_snprintf(buf[INFO_STEAL], sizeof(buf[INFO_STEAL]), _("Not stolen"));
  }

  get_city_dialog_airlift_value(pcity, buf[INFO_AIRLIFT], sizeof(buf[INFO_AIRLIFT]));

  /* stick 'em in the labels */
  for (i = 0; i < NUM_INFO_FIELDS; i++) {
    gtk_label_set_text(GTK_LABEL(info_label[i]), buf[i]);
  }

  /*
   * Make use of the emergency-indicating styles set up for certain labels
   * in create_city_info_table().
   */
  /* For starvation, the "4" below is arbitrary. 3 turns should be enough
   * of a warning. */
  if (granaryturns > -4 && granaryturns < 0) {
    gtk_widget_add_css_class(info_label[INFO_GRANARY], "emergency");
  } else {
    gtk_widget_remove_css_class(info_label[INFO_GRANARY], "emergency");
  }

  if (granaryturns == 0 || pcity->surplus[O_FOOD] < 0) {
    gtk_widget_add_css_class(info_label[INFO_GROWTH], "emergency");
  } else {
    gtk_widget_remove_css_class(info_label[INFO_GROWTH], "emergency");
  }

  /* Someone could add the color &orange for better granularity here */
  if (pcity->pollution >= 10) {
    gtk_widget_add_css_class(info_label[INFO_POLLUTION], "emergency");
  } else {
    gtk_widget_remove_css_class(info_label[INFO_POLLUTION], "emergency");
  }

  /* Illness is in tenth of percent, i.e 100 == 10.0% */
  if (illness >= 100) {
    gtk_widget_add_css_class(info_label[INFO_ILLNESS], "emergency");
  } else {
    gtk_widget_remove_css_class(info_label[INFO_ILLNESS], "emergency");
  }
}

/***********************************************************************//**
  Update map display of city dialog
***************************************************************************/
static void city_dialog_update_map(struct city_dialog *pdialog)
{
  struct canvas store = FC_STATIC_CANVAS_INIT;

  store.surface = pdialog->map_canvas_store_unscaled;

  /* The drawing is done in three steps.
   *   1.  First we render to a pixmap with the appropriate canvas size.
   *   2.  Then the pixmap is rendered into a pixbuf of equal size.
   *   3.  Finally this pixbuf is composited and scaled onto the GtkImage's
   *       target pixbuf.
   */

  city_dialog_redraw_map(pdialog->pcity, &store);

  /* draw to real window */
  draw_map_canvas(pdialog);
}

/***********************************************************************//**
  Update what city is building and buy cost in city dialog
***************************************************************************/
static void city_dialog_update_building(struct city_dialog *pdialog)
{
  char buf[32], buf2[200];
  gdouble pct;
  GListStore *store;
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  int targets_used, item;
  struct city *pcity = pdialog->pcity;
  gboolean sensitive = city_can_buy(pcity);
  const char *descr = city_production_name_translation(pcity);
  int cost = city_production_build_shield_cost(pcity);

  if (pdialog->overview.buy_command != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(pdialog->overview.buy_command), sensitive);
  }
  if (pdialog->production.buy_command != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(pdialog->production.buy_command), sensitive);
  }

  /* Make sure build slots info is up to date */
  if (pdialog->production.production_label != NULL) {
    int build_slots = city_build_slots(pcity);

    /* Only display extra info if more than one slot is available */
    if (build_slots > 1) {
      fc_snprintf(buf2, sizeof(buf2),
                  /* TRANS: never actually used with built_slots <= 1 */
                  PL_("Production (up to %d unit per turn):",
                      "Production (up to %d units per turn):", build_slots),
                  build_slots);
      gtk_label_set_text(
        GTK_LABEL(pdialog->production.production_label), buf2);
    } else {
      gtk_label_set_text(
        GTK_LABEL(pdialog->production.production_label), _("Production:"));
    }
  }

  /* Update what the city is working on */
  get_city_dialog_production(pcity, buf, sizeof(buf));

  if (cost > 0) {
    pct = (gdouble) pcity->shield_stock / (gdouble) cost;
    pct = CLAMP(pct, 0.0, 1.0);
  } else {
    pct = 1.0;
  }

  if (pdialog->overview.production_bar != NULL) {
    fc_snprintf(buf2, sizeof(buf2), "%s%s\n%s", descr,
                worklist_is_empty(&pcity->worklist) ? "" : " (+)", buf);
    gtk_progress_bar_set_text(
      GTK_PROGRESS_BAR(pdialog->overview.production_bar), buf2);
    gtk_progress_bar_set_fraction(
      GTK_PROGRESS_BAR(pdialog->overview.production_bar), pct);
  }

  if (pdialog->production.production_bar != NULL) {
    fc_snprintf(buf2, sizeof(buf2), "%s%s: %s", descr,
                worklist_is_empty(&pcity->worklist) ? "" : " (+)", buf);
    gtk_progress_bar_set_text(
      GTK_PROGRESS_BAR(pdialog->production.production_bar), buf2);
    gtk_progress_bar_set_fraction(
      GTK_PROGRESS_BAR(pdialog->production.production_bar), pct);
  }

  store = pdialog->overview.change_prod_store;
  if (store != nullptr) {
    int cur = -1;
    int actcount = 0;

    if (pdialog->overview.change_prod_selection != nullptr) {
      gtk_selection_model_select_item(GTK_SELECTION_MODEL(pdialog->overview.change_prod_selection),
                                      -1, TRUE);
    }

    g_list_store_remove_all(store);

    targets_used
      = collect_eventually_buildable_targets(targets, pdialog->pcity, FALSE);
    name_and_sort_items(targets, targets_used, items, FALSE, pcity);

    for (item = 0; item < targets_used; item++) {
      if (can_city_build_now(&(wld.map), pcity, &items[item].item)) {
        const char *name;
        struct sprite *sprite;
        GdkPixbuf *pix;
        struct universal *target = &items[item].item;
        bool useless;
        FcProdRow *row = fc_prod_row_new();

        if (VUT_UTYPE == target->kind) {
          name = utype_name_translation(target->value.utype);
          sprite = get_unittype_sprite(tileset, target->value.utype,
                                       ACTIVITY_LAST, direction8_invalid());
          useless = FALSE;
        } else {
          name = improvement_name_translation(target->value.building);
          sprite = get_building_sprite(tileset, target->value.building);
          useless = is_improvement_redundant(pcity, target->value.building);
        }
        pix = sprite_get_pixbuf(sprite);

        row->name = name;
        row->id = (gint)cid_encode(items[item].item);
        row->sprite = pix;
        row->useless = useless;

        g_list_store_append(store, row);
        g_object_unref(row);

        if (are_universals_equal(target, &pcity->production)) {
          cur = actcount;
        }

        actcount++;
      }
    }

    if (pdialog->overview.change_prod_selection != nullptr) {
      gtk_selection_model_select_item(GTK_SELECTION_MODEL(pdialog->overview.change_prod_selection),
                                      cur, TRUE);
    }
  }
}

/***********************************************************************//**
  Update list of improvements in city dialog
***************************************************************************/
static void city_dialog_update_improvement_list(struct city_dialog *pdialog)
{
  int item, targets_used;
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct item items[MAX_NUM_PRODUCTION_TARGETS];

  const char *tooltip_sellable = _("Press <b>ENTER</b> or double-click to "
                                   "sell an improvement.");
  const char *tooltip_great_wonder = _("Great Wonder - cannot be sold.");
  const char *tooltip_small_wonder = _("Small Wonder - cannot be sold.");

  targets_used = collect_already_built_targets(targets, pdialog->pcity);
  name_and_sort_items(targets, targets_used, items, FALSE, pdialog->pcity);

  g_list_store_remove_all(pdialog->overview.improvement_list);

  for (item = 0; item < targets_used; item++) {
    GdkPixbuf *pix;
    int upkeep;
    struct sprite *sprite;
    struct universal target = items[item].item;
    FcImprRow *row = fc_impr_row_new();

    fc_assert_action(VUT_IMPROVEMENT == target.kind, continue);
    /* This takes effects (like Adam Smith's) into account. */
    upkeep = city_improvement_upkeep(pdialog->pcity, target.value.building);
    sprite = get_building_sprite(tileset, target.value.building);

    pix = sprite_get_pixbuf(sprite);

    row->impr = target.value.building;
    row->sprite = pix;
    row->description = items[item].descr;
    row->upkeep = upkeep;
    row->redundant = is_improvement_redundant(pdialog->pcity,
                                              target.value.building);
    row->tooltip = is_great_wonder(target.value.building) ?
                         tooltip_great_wonder :
                           (is_small_wonder(target.value.building) ?
                            tooltip_small_wonder : tooltip_sellable);

    g_list_store_append(pdialog->overview.improvement_list, row);
    g_object_unref(row);
  }
}

/***********************************************************************//**
  Update list of supported units in city dialog
***************************************************************************/
static void city_dialog_update_supported_units(struct city_dialog *pdialog)
{
  struct unit_list *units;
  struct unit_node_vector *nodes;
  int n, m, i;
  gchar *buf;
  int free_unhappy = get_city_bonus(pdialog->pcity, EFT_MAKE_CONTENT_MIL);
  const struct civ_map *nmap = &(wld.map);

  if (NULL != client.conn.playing
      && city_owner(pdialog->pcity) != client.conn.playing) {
    units = pdialog->pcity->client.info_units_supported;
  } else {
    units = pdialog->pcity->units_supported;
  }

  nodes = &pdialog->overview.supported_units;

  n = unit_list_size(units);
  m = unit_node_vector_size(nodes);

  if (m > n) {
    i = 0;
    unit_node_vector_iterate(nodes, elt) {
      if (i++ >= n) {
        gtk_box_remove(GTK_BOX(pdialog->overview.supported_unit_table),
                       elt->cmd);
      }
    } unit_node_vector_iterate_end;

    unit_node_vector_reserve(nodes, n);
  } else {
    for (i = m; i < n; i++) {
      GtkWidget *cmd, *pix;
      struct unit_node node;

      cmd = gtk_button_new();
      node.cmd = cmd;

      gtk_button_set_has_frame(GTK_BUTTON(cmd), FALSE);

      pix = gtk_picture_new();
      node.pix = pix;
      node.height = tileset_unit_with_upkeep_height(tileset);

      gtk_button_set_child(GTK_BUTTON(cmd), pix);

      gtk_box_append(GTK_BOX(pdialog->overview.supported_unit_table),
                     cmd);

      node.left = NULL;
      node.middle = NULL;
      node.right = NULL;

      unit_node_vector_append(nodes, node);
    }
  }

  i = 0;
  unit_list_iterate(units, punit) {
    struct unit_node *pnode;
    int happy_cost = city_unit_unhappiness(nmap, punit, &free_unhappy);

    pnode = unit_node_vector_get(nodes, i);
    if (pnode) {
      GtkWidget *cmd, *pix;
      GtkGesture *gesture;
      GtkEventController *controller;

      cmd = pnode->cmd;
      pix = pnode->pix;

      put_unit_picture_city_overlays(punit, GTK_PICTURE(pix), pnode->height,
                                     punit->upkeep, happy_cost);

      if (pnode->left != NULL) {
        gtk_widget_remove_controller(cmd, pnode->left);
        gtk_widget_remove_controller(cmd, pnode->middle);
        gtk_widget_remove_controller(cmd, pnode->right);
      }

      gtk_widget_set_tooltip_text(cmd, unit_description(punit));

      controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
      g_signal_connect(controller, "pressed",
                       G_CALLBACK(supported_unit_callback),
                       GINT_TO_POINTER(punit->id));
      gtk_widget_add_controller(cmd, controller);
      pnode->left = controller;
      gesture = gtk_gesture_click_new();
      gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 2);
      controller = GTK_EVENT_CONTROLLER(gesture);
      g_signal_connect(controller, "released",
                       G_CALLBACK(middle_supported_unit_release),
                       GINT_TO_POINTER(punit->id));
      gtk_widget_add_controller(cmd, controller);
      pnode->middle = controller;
      gesture = gtk_gesture_click_new();
      gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
      controller = GTK_EVENT_CONTROLLER(gesture);
      g_signal_connect(controller, "released",
                       G_CALLBACK(right_unit_release),
                       GINT_TO_POINTER(punit->id));
      gtk_widget_add_controller(cmd, controller);
      pnode->right = controller;

      if (city_owner(pdialog->pcity) != client.conn.playing) {
        gtk_widget_set_sensitive(cmd, FALSE);
      } else {
        gtk_widget_set_sensitive(cmd, TRUE);
      }

      gtk_widget_set_visible(pix, TRUE);
      gtk_widget_set_visible(cmd, TRUE);
    }
    i++;
  } unit_list_iterate_end;

  buf = g_strdup_printf(_("Supported units %d"), n);
  gtk_frame_set_label(GTK_FRAME(pdialog->overview.supported_units_frame), buf);
  g_free(buf);
}

/***********************************************************************//**
  Update list of present units in city dialog
***************************************************************************/
static void city_dialog_update_present_units(struct city_dialog *pdialog)
{
  struct unit_list *units;
  struct unit_node_vector *nodes;
  int n, m, i;
  gchar *buf;

  if (NULL != client.conn.playing
      && city_owner(pdialog->pcity) != client.conn.playing) {
    units = pdialog->pcity->client.info_units_present;
  } else {
    units = pdialog->pcity->tile->units;
  }

  nodes = &pdialog->overview.present_units;

  n = unit_list_size(units);
  m = unit_node_vector_size(nodes);

  if (m > n) {
    i = 0;
    unit_node_vector_iterate(nodes, elt) {
      if (i++ >= n) {
        gtk_box_remove(GTK_BOX(pdialog->overview.present_unit_table),
                       elt->cmd);
      }
    } unit_node_vector_iterate_end;

    unit_node_vector_reserve(nodes, n);
  } else {
    for (i = m; i < n; i++) {
      GtkWidget *cmd, *pix;
      struct unit_node node;

      cmd = gtk_button_new();
      node.cmd = cmd;

      gtk_button_set_has_frame(GTK_BUTTON(cmd), FALSE);

      pix = gtk_picture_new();
      node.pix = pix;
      node.height = tileset_full_tile_height(tileset);

      gtk_button_set_child(GTK_BUTTON(cmd), pix);

      gtk_box_append(GTK_BOX(pdialog->overview.present_unit_table),
                     cmd);

      node.left = NULL;
      node.middle = NULL;
      node.right = NULL;

      unit_node_vector_append(nodes, node);
    }
  }

  i = 0;
  unit_list_iterate(units, punit) {
    struct unit_node *pnode;

    pnode = unit_node_vector_get(nodes, i);
    if (pnode) {
      GtkWidget *cmd, *pix;
      GtkEventController *controller;
      GtkGesture *gesture;

      cmd = pnode->cmd;
      pix = pnode->pix;

      put_unit_picture(punit, GTK_PICTURE(pix), pnode->height);

      if (pnode->left != NULL) {
        gtk_widget_remove_controller(cmd, pnode->left);
        gtk_widget_remove_controller(cmd, pnode->middle);
        gtk_widget_remove_controller(cmd, pnode->right);
      }

      gtk_widget_set_tooltip_text(cmd, unit_description(punit));

      controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
      g_signal_connect(controller, "pressed",
                       G_CALLBACK(present_unit_callback),
                       GINT_TO_POINTER(punit->id));
      gtk_widget_add_controller(cmd, controller);
      pnode->left = controller;
      gesture = gtk_gesture_click_new();
      gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 2);
      controller = GTK_EVENT_CONTROLLER(gesture);
      g_signal_connect(controller, "released",
                       G_CALLBACK(middle_present_unit_release),
                       GINT_TO_POINTER(punit->id));
      gtk_widget_add_controller(cmd, controller);
      pnode->middle = controller;
      gesture = gtk_gesture_click_new();
      gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
      controller = GTK_EVENT_CONTROLLER(gesture);
      g_signal_connect(controller, "released",
                       G_CALLBACK(right_unit_release),
                       GINT_TO_POINTER(punit->id));
      gtk_widget_add_controller(cmd, controller);
      pnode->right = controller;

      if (city_owner(pdialog->pcity) != client.conn.playing) {
        gtk_widget_set_sensitive(cmd, FALSE);
      } else {
        gtk_widget_set_sensitive(cmd, TRUE);
      }

      gtk_widget_set_visible(pix, TRUE);
      gtk_widget_set_visible(cmd, TRUE);
    }
    i++;
  } unit_list_iterate_end;

  buf = g_strdup_printf(_("Present units %d"), n);
  gtk_frame_set_label(GTK_FRAME(pdialog->overview.present_units_frame), buf);
  g_free(buf);
}

/***********************************************************************//**
  Updates the sensitivity of the prev and next buttons.
  this does not need pdialog as a parameter, since it iterates
  over all the open dialogs.
  note: we still need the sensitivity code in create_city_dialog()
  for the spied dialogs.
***************************************************************************/
static void city_dialog_update_prev_next(void)
{
  int count = 0;
  int city_number;

  if (NULL != client.conn.playing) {
    city_number = city_list_size(client.conn.playing->cities);
  } else {
    city_number = FC_INFINITY; /* ? */
  }

  /* The first time, we see if all the city dialogs are open */
  dialog_list_iterate(dialog_list, pdialog) {
    if (city_owner(pdialog->pcity) == client.conn.playing) {
      count++;
    }
  } dialog_list_iterate_end;

  if (count == city_number) {   /* All are open, shouldn't prev/next */
    dialog_list_iterate(dialog_list, pdialog) {
      gtk_widget_set_sensitive(GTK_WIDGET(pdialog->prev_command), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(pdialog->next_command), FALSE);
    } dialog_list_iterate_end;
  } else {
    dialog_list_iterate(dialog_list, pdialog) {
      if (city_owner(pdialog->pcity) == client.conn.playing) {
        gtk_widget_set_sensitive(GTK_WIDGET(pdialog->prev_command), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(pdialog->next_command), TRUE);
      }
    } dialog_list_iterate_end;
  }
}

/***********************************************************************//**
  User clicked button from action area.
***************************************************************************/
static void citydlg_response_callback(GtkDialog *dlg, gint response,
                                      void *data)
{
  switch (response) {
  case CDLGR_UNITS:
    show_units_response(data);
    break;
  }
}

/***********************************************************************//**
  User has clicked show units
***************************************************************************/
static void show_units_response(void *data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  struct tile *ptile = pdialog->pcity->tile;

  if (unit_list_size(ptile->units)) {
    unit_select_dialog_popup(ptile);
  }
}

/***********************************************************************//**
  Create menu for the unit in citydlg

  @param pdialog   Dialog to create menu to
  @param punit     Unit to create menu for
  @param wdg       Widget to attach menu to
  @param supported Is this supported units menu (not present units)

  @return whether menu was really created
***************************************************************************/
static bool create_unit_menu(struct city_dialog *pdialog, struct unit *punit,
                             GtkWidget *wdg, bool supported)
{
  GMenu *menu;
  GActionGroup *group;
  GSimpleAction *act;

  if (!can_client_issue_orders()) {
    return FALSE;
  }

  if (pdialog->popover != NULL) {
    close_citydlg_unit_popover(pdialog);
  }

  group = G_ACTION_GROUP(g_simple_action_group_new());
  menu = g_menu_new();

  if (supported) {
    act = g_simple_action_new("center", NULL);
    g_object_set_data(G_OBJECT(act), "dlg", pdialog);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(unit_center_callback),
                     GINT_TO_POINTER(punit->id));
    menu_item_append_unref(menu, g_menu_item_new(_("Cen_ter"), "win.center"));
  }

  act = g_simple_action_new("activate", NULL);
  g_object_set_data(G_OBJECT(act), "dlg", pdialog);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(unit_activate_callback),
                   GINT_TO_POINTER(punit->id));
  menu_item_append_unref(menu, g_menu_item_new(_("_Activate unit"),
                                               "win.activate"));

  act = g_simple_action_new("activate_close", NULL);
  g_object_set_data(G_OBJECT(act), "dlg", pdialog);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));

  if (supported) {
    g_signal_connect(act, "activate",
                     G_CALLBACK(supported_unit_activate_close_callback),
                     GINT_TO_POINTER(punit->id));
  } else {
    g_signal_connect(act, "activate",
                     G_CALLBACK(present_unit_activate_close_callback),
                     GINT_TO_POINTER(punit->id));
  }

  menu_item_append_unref(menu,
                         g_menu_item_new(_("Activate unit, _close dialog"),
                                         "win.activate_close"));

  if (!supported) {
    act = g_simple_action_new("load", NULL);
    g_object_set_data(G_OBJECT(act), "dlg", pdialog);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(unit_load_callback),
                     GINT_TO_POINTER(punit->id));
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act), unit_can_load(punit));
    menu_item_append_unref(menu, g_menu_item_new(_("_Load unit"), "win.load"));

    act = g_simple_action_new("unload", NULL);
    g_object_set_data(G_OBJECT(act), "dlg", pdialog);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(unit_unload_callback),
                     GINT_TO_POINTER(punit->id));
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                can_unit_unload(punit,
                                                unit_transport_get(punit))
                                && can_unit_exist_at_tile(&(wld.map), punit,
                                                          unit_tile(punit)));
    menu_item_append_unref(menu, g_menu_item_new(_("_Unload unit"),
                                                 "win.unload"));

    act = g_simple_action_new("sentry", NULL);
    g_object_set_data(G_OBJECT(act), "dlg", pdialog);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(unit_sentry_callback),
                     GINT_TO_POINTER(punit->id));
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                punit->activity != ACTIVITY_SENTRY
                                && can_unit_do_activity_client(punit,
                                                               ACTIVITY_SENTRY));
    menu_item_append_unref(menu, g_menu_item_new(_("_Sentry unit"),
                                                 "win.sentry"));

    act = g_simple_action_new("fortify", NULL);
    g_object_set_data(G_OBJECT(act), "dlg", pdialog);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(unit_fortify_callback),
                     GINT_TO_POINTER(punit->id));
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                punit->activity != ACTIVITY_FORTIFYING
                                && can_unit_do_activity_client(punit,
                                                               ACTIVITY_FORTIFYING));
    menu_item_append_unref(menu, g_menu_item_new(_("_Fortify unit"),
                                                 "win.fortify"));
  }

  act = g_simple_action_new("disband", NULL);
  g_object_set_data(G_OBJECT(act), "dlg", pdialog);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(unit_disband_callback),
                   GINT_TO_POINTER(punit->id));
  g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                              unit_can_do_action(punit, ACTION_DISBAND_UNIT));
  menu_item_append_unref(menu, g_menu_item_new(_("_Disband unit"),
                                               "win.disband"));

  if (!supported) {
    act = g_simple_action_new("rehome", NULL);
    g_object_set_data(G_OBJECT(act), "dlg", pdialog);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(unit_homecity_callback),
                     GINT_TO_POINTER(punit->id));
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                can_unit_change_homecity_to(&(wld.map), punit,
                                                            pdialog->pcity));
    menu_item_append_unref(menu,
                           g_menu_item_new(action_id_name_translation(ACTION_HOME_CITY),
                                           "win.rehome"));

    act = g_simple_action_new("upgrade", NULL);
    g_object_set_data(G_OBJECT(act), "dlg", pdialog);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(unit_upgrade_callback),
                     GINT_TO_POINTER(punit->id));
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                action_ever_possible(ACTION_UPGRADE_UNIT)
                                && can_client_issue_orders()
                                && can_upgrade_unittype(client_player(),
                                                        unit_type_get(punit))
                                   != NULL);
    menu_item_append_unref(menu, g_menu_item_new(_("U_pgrade unit"),
                                                 "win.upgrade"));
  }

  pdialog->popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
  g_object_ref(pdialog->popover);
  gtk_widget_insert_action_group(pdialog->popover, "win", group);
  gtk_widget_set_parent(pdialog->popover, wdg);


  gtk_popover_popup(GTK_POPOVER(pdialog->popover));

  return TRUE;
}

/***********************************************************************//**
  Pop-up menu to change attributes of supported units
***************************************************************************/
static gboolean supported_unit_callback(GtkGestureClick *gesture, int n_press,
                                        double x, double y, gpointer data)
{
  struct city_dialog *pdialog;
  struct city *pcity;
  struct unit *punit =
    player_unit_by_number(client_player(), (size_t) data);

  if (NULL != punit
      && NULL != (pcity = game_city_by_number(punit->homecity))
      && NULL != (pdialog = get_city_dialog(pcity))) {

    return create_unit_menu(pdialog, punit,
                            gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)),
                            TRUE);
  }

  return TRUE;
}

/***********************************************************************//**
  Pop-up menu to change attributes of units, ex. change homecity.
***************************************************************************/
static gboolean present_unit_callback(GtkGestureClick *gesture, int n_press,
                                      double x, double y, gpointer data)
{
  struct city_dialog *pdialog;
  struct city *pcity;
  struct unit *punit =
    player_unit_by_number(client_player(), (size_t) data);

  if (NULL != punit
      && NULL != (pcity = tile_city(unit_tile(punit)))
      && NULL != (pdialog = get_city_dialog(pcity))) {

    return create_unit_menu(pdialog, punit,
                            gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)),
                            FALSE);
  }

  return TRUE;
}

/***********************************************************************//**
  If user middle-clicked on a unit, activate it and close dialog.
  Dialog to close is that of city where unit currently is.
***************************************************************************/
static gboolean middle_present_unit_release(GtkGestureClick *gesture,
                                            int n_press, double x, double y,
                                            gpointer data)
{
  struct city_dialog *pdialog;
  struct city *pcity;
  struct unit *punit =
    player_unit_by_number(client_player(), (size_t) data);

  if (NULL != punit
      && NULL != (pcity = tile_city(unit_tile(punit)))
      && NULL != (pdialog = get_city_dialog(pcity))
      && can_client_issue_orders()) {
    unit_focus_set(punit);
    close_city_dialog(pdialog);
  }

  return TRUE;
}

/***********************************************************************//**
  If user middle-clicked on a unit, activate it and close dialog.
  Dialog to close is that of unit's home city.
***************************************************************************/
static gboolean middle_supported_unit_release(GtkGestureClick *gesture, int n_press,
                                              double x, double y, gpointer data)
{
  struct city_dialog *pdialog;
  struct city *pcity;
  struct unit *punit =
    player_unit_by_number(client_player(), (size_t) data);

  if (NULL != punit
      && NULL != (pcity = game_city_by_number(punit->homecity))
      && NULL != (pdialog = get_city_dialog(pcity))
      && can_client_issue_orders()) {
    unit_focus_set(punit);
    close_city_dialog(pdialog);
  }

  return TRUE;
}

/***********************************************************************//**
  If user right-clicked on a unit, activate it
***************************************************************************/
static gboolean right_unit_release(GtkGestureClick *gesture, int n_press,
                                   double x, double y, gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), (size_t) data);

  if (NULL != punit
      && can_client_issue_orders()) {
    unit_focus_set(punit);
  }

  return TRUE;
}

/***********************************************************************//**
  Close unit menu

  @param pdialog Dialog where the menu should be closed from
***************************************************************************/
static void close_citydlg_unit_popover(struct city_dialog *pdialog)
{
  if (pdialog->popover != NULL) {
    gtk_widget_unparent(pdialog->popover);
    g_object_unref(pdialog->popover);
    pdialog->popover = NULL;
  }
}

/***********************************************************************//**
  User has requested centering to unit
***************************************************************************/
static void unit_center_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL != punit) {
    center_tile_mapcanvas(unit_tile(punit));
  }

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested unit activation
***************************************************************************/
static void unit_activate_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL != punit) {
    unit_focus_set(punit);
  }

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested some supported unit to be activated and
  city dialog to be closed
***************************************************************************/
static void supported_unit_activate_close_callback(GSimpleAction *action,
                                                   GVariant *parameter,
                                                   gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL != punit) {
    struct city *pcity =
      player_city_by_number(client_player(), punit->homecity);

    unit_focus_set(punit);
    if (NULL != pcity) {
      struct city_dialog *pdialog = get_city_dialog(pcity);

      if (NULL != pdialog) {
        close_city_dialog(pdialog);
      }
    }
  }

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested some present unit to be activated and
  city dialog to be closed
***************************************************************************/
static void present_unit_activate_close_callback(GSimpleAction *action,
                                                 GVariant *parameter,
                                                 gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL != punit) {
    struct city *pcity = tile_city(unit_tile(punit));

    unit_focus_set(punit);
    if (NULL != pcity) {
      struct city_dialog *pdialog = get_city_dialog(pcity);

      if (NULL != pdialog) {
        close_city_dialog(pdialog);
      }
    }
  }

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested unit to be loaded to transport
***************************************************************************/
static void unit_load_callback(GSimpleAction *action, GVariant *parameter,
                               gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL != punit) {
    request_transport(punit, unit_tile(punit));
  }

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested unit to be unloaded from transport
***************************************************************************/
static void unit_unload_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL != punit) {
    request_unit_unload(punit);
  }

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested unit to be sentried
***************************************************************************/
static void unit_sentry_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL != punit) {
    request_unit_sentry(punit);
  }

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested unit to be fortified
***************************************************************************/
static void unit_fortify_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL != punit) {
    request_unit_fortify(punit);
  }

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested unit to be disbanded
***************************************************************************/
static void unit_disband_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  struct unit_list *punits;
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL == punit) {
    return;
  }

  punits = unit_list_new();
  unit_list_append(punits, punit);
  popup_disband_dialog(punits);
  unit_list_destroy(punits);

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested unit to change homecity to city where it
  currently is
***************************************************************************/
static void unit_homecity_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data)
{
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL != punit) {
    request_unit_change_homecity(punit);
  }

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/***********************************************************************//**
  User has requested unit to be upgraded
***************************************************************************/
static void unit_upgrade_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  struct unit_list *punits;
  struct unit *punit =
    player_unit_by_number(client_player(), GPOINTER_TO_INT(data));

  if (NULL == punit) {
    return;
  }

  punits = unit_list_new();
  unit_list_append(punits, punit);
  popup_upgrade_dialog(punits);
  unit_list_destroy(punits);

  close_citydlg_unit_popover(g_object_get_data(G_OBJECT(action), "dlg"));
}

/******** Callbacks for citizen bar, map funcs that are not update ********/

/***********************************************************************//**
  User clicked the list of citizens. If they clicked a specialist
  then change its type, else do nothing.
***************************************************************************/
static gboolean citizens_callback(GtkGestureClick *gesture, int n_press,
                                  double x, double y, gpointer data)
{
  struct city_dialog *pdialog = data;
  struct city *pcity = pdialog->pcity;
  int citnum, tlen, len;

  if (!can_client_issue_orders()) {
    return FALSE;
  }

  tlen = tileset_small_sprite_width(tileset);
  len = (city_size_get(pcity) - 1) * pdialog->cwidth + tlen;

  if (x > len) {
    /* No citizen that far to the right */
    return FALSE;
  }
  citnum = MIN(city_size_get(pcity) - 1, x / pdialog->cwidth);

  city_rotate_specialist(pcity, citnum);

  return TRUE;
}

/***********************************************************************//**
  Set requested workertask
***************************************************************************/
static void set_city_workertask(GtkWidget *w, gpointer data)
{
  enum unit_activity act = (enum unit_activity)GPOINTER_TO_INT(data);
  struct city *pcity = workertask_req.owner;
  struct tile *ptile = workertask_req.loc;
  struct packet_worker_task task;

  task.city_id = pcity->id;

  if (act == ACTIVITY_LAST) {
    task.tgt = -1;
    task.want = 0;
  } else {
    enum extra_cause cause = activity_to_extra_cause(act);
    enum extra_rmcause rmcause = activity_to_extra_rmcause(act);
    struct extra_type *tgt;

    if (cause != EC_NONE) {
      tgt = next_extra_for_tile(ptile, cause, city_owner(pcity), NULL);
    } else if (rmcause != ERM_NONE) {
      tgt = prev_extra_in_tile(ptile, rmcause, city_owner(pcity), NULL);
    } else {
      tgt = NULL;
    }

    if (tgt == NULL) {
      struct terrain *pterr = tile_terrain(ptile);

      if ((act != ACTIVITY_TRANSFORM
           || pterr->transform_result == NULL || pterr->transform_result == pterr)
          && (act != ACTIVITY_CULTIVATE || pterr->cultivate_result == NULL)
          && (act != ACTIVITY_PLANT || pterr->plant_result == NULL)) {
        /* No extra to order */
        output_window_append(ftc_client, _("There's no suitable extra to order."));

        return;
      }

      task.tgt = -1;
    } else {
      task.tgt = extra_index(tgt);
    }

    task.want = 100;
  }

  task.tile_id = ptile->index;
  task.activity = act;

  send_packet_worker_task(&client.conn, &task);
}

/***********************************************************************//**
  Destroy workertask dlg
***************************************************************************/
static void workertask_dlg_destroy(GtkWidget *w, gpointer data)
{
  is_showing_workertask_dialog = FALSE;
}

/***********************************************************************//**
  Open dialog for setting worker task
***************************************************************************/
static void popup_workertask_dlg(struct city *pcity, struct tile *ptile)
{
  if (!is_showing_workertask_dialog) {
    GtkWidget *shl;
    struct terrain *pterr = tile_terrain(ptile);
    struct universal for_terr = { .kind = VUT_TERRAIN,
                                  .value = { .terrain = pterr }};
    struct worker_task *ptask;

    is_showing_workertask_dialog = TRUE;
    workertask_req.owner = pcity;
    workertask_req.loc = ptile;

    shl = choice_dialog_start(GTK_WINDOW(toplevel),
                              _("What Action to Request"),
                              _("Select autoworker activity:"));

    ptask = worker_task_list_get(pcity->task_reqs, 0);
    if (ptask != NULL) {
      choice_dialog_add(shl, _("Clear request"),
                        G_CALLBACK(set_city_workertask),
                        GINT_TO_POINTER(ACTIVITY_LAST), FALSE, NULL);
    }

    if (action_id_univs_not_blocking(ACTION_MINE, NULL, &for_terr)) {
      choice_dialog_add(shl, Q_("?act:Mine"),
                        G_CALLBACK(set_city_workertask),
                        GINT_TO_POINTER(ACTIVITY_MINE), FALSE, NULL);
    }
    if (pterr->plant_result != NULL
        && action_id_univs_not_blocking(ACTION_PLANT,
                                        NULL, &for_terr)) {
      choice_dialog_add(shl, _("Plant"),
                        G_CALLBACK(set_city_workertask),
                        GINT_TO_POINTER(ACTIVITY_PLANT), FALSE, NULL);
    }
    if (action_id_univs_not_blocking(ACTION_IRRIGATE, NULL, &for_terr)) {
      choice_dialog_add(shl, _("Irrigate"),
                        G_CALLBACK(set_city_workertask),
                        GINT_TO_POINTER(ACTIVITY_IRRIGATE), FALSE, NULL);
    }
    if (pterr->cultivate_result != NULL
        && action_id_univs_not_blocking(ACTION_CULTIVATE,
                                        NULL, &for_terr)) {
      choice_dialog_add(shl, _("Cultivate"),
                        G_CALLBACK(set_city_workertask),
                        GINT_TO_POINTER(ACTIVITY_CULTIVATE), FALSE, NULL);
    }
    if (next_extra_for_tile(ptile, EC_ROAD, city_owner(pcity), NULL) != NULL) {
      choice_dialog_add(shl, _("Road"),
                        G_CALLBACK(set_city_workertask),
                        GINT_TO_POINTER(ACTIVITY_GEN_ROAD), FALSE, NULL);
    }
    if (pterr->transform_result != pterr && pterr->transform_result != NULL
        && action_id_univs_not_blocking(ACTION_TRANSFORM_TERRAIN,
                                        NULL, &for_terr)) {
      choice_dialog_add(shl, _("Transform"),
                        G_CALLBACK(set_city_workertask),
                        GINT_TO_POINTER(ACTIVITY_TRANSFORM), FALSE, NULL);
    }
    if (prev_extra_in_tile(ptile, ERM_CLEAN,
                           city_owner(pcity), NULL) != NULL) {
      choice_dialog_add(shl, _("Clean"),
                        G_CALLBACK(set_city_workertask),
                        GINT_TO_POINTER(ACTIVITY_CLEAN), FALSE, NULL);
    }

    choice_dialog_add(shl, _("_Cancel"), 0, 0, FALSE, NULL);
    choice_dialog_end(shl);

    g_signal_connect(shl, "destroy", G_CALLBACK(workertask_dlg_destroy),
                     NULL);
  }
}

/***********************************************************************//**
  User has pressed left button on citymap
***************************************************************************/
static gboolean left_button_down_citymap(GtkGestureClick *gesture, int n_press,
                                         double x, double y, gpointer data)
{
  struct city_dialog *pdialog = data;
  int canvas_x, canvas_y, city_x, city_y;

  if (!can_client_issue_orders()) {
    return FALSE;
  }

  if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
    return FALSE;
  }

  canvas_x = x * (double)canvas_width / (double)CITYMAP_WIDTH;
  canvas_y = y * (double)canvas_height / (double)CITYMAP_HEIGHT;

  if (canvas_to_city_pos(&city_x, &city_y,
                         city_map_radius_sq_get(pdialog->pcity),
                         canvas_x, canvas_y)) {
    city_toggle_worker(pdialog->pcity, city_x, city_y);
  }

  return TRUE;
}

/***********************************************************************//**
  User has pressed right button on citymap
***************************************************************************/
static gboolean right_button_down_citymap(GtkGestureClick *gesture, int n_press,
                                          double x, double y, gpointer data)
{
  struct city_dialog *pdialog = data;
  int canvas_x, canvas_y, city_x, city_y;

  if (!can_client_issue_orders()) {
    return FALSE;
  }

  canvas_x = x * (double)canvas_width / (double)CITYMAP_WIDTH;
  canvas_y = y * (double)canvas_height / (double)CITYMAP_HEIGHT;

  if (canvas_to_city_pos(&city_x, &city_y,
                         city_map_radius_sq_get(pdialog->pcity),
                         canvas_x, canvas_y)) {
    struct city *pcity = pdialog->pcity;

    popup_workertask_dlg(pdialog->pcity,
                         city_map_to_tile(&(wld.map), pcity->tile,
                                          city_map_radius_sq_get(pcity),
                                          city_x, city_y));
  }

  return TRUE;
}

/***********************************************************************//**
  Set map canvas to be drawn
***************************************************************************/
static void draw_map_canvas(struct city_dialog *pdialog)
{
  gtk_widget_queue_draw(pdialog->overview.map_canvas.darea);
  if (pdialog->happiness.map_canvas.darea) { /* in case of spy */
    gtk_widget_queue_draw(pdialog->happiness.map_canvas.darea);
  }
}

/************** Callbacks for Buy, Change, Sell, Worklist *****************/

/***********************************************************************//**
  User has answered buy cost dialog
***************************************************************************/
static void buy_callback_response(GObject *dialog, GAsyncResult *result,
                                  gpointer data)
{
  int button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(dialog),
                                              result, NULL);

  if (button == 0) {
    struct city_dialog *pdialog = data;

    city_buy_production(pdialog->pcity);
  }
}

/***********************************************************************//**
  User has clicked buy-button
***************************************************************************/
static void buy_callback(GtkWidget *w, gpointer data)
{
  GtkAlertDialog *shell;
  struct city_dialog *pdialog = data;
  const char *name = city_production_name_translation(pdialog->pcity);
  int value = pdialog->pcity->client.buy_cost;
  char buf[1024];

  if (!can_client_issue_orders()) {
    return;
  }

  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);

  if (value <= client_player()->economic.gold) {
    const char *buttons[] = { _("Yes"), _("No"), NULL };

    shell = gtk_alert_dialog_new(
        /* TRANS: Last %s is pre-pluralised "Treasury contains %d gold." */
        PL_("Buy %s for %d gold?\n%s",
            "Buy %s for %d gold?\n%s", value),
        name, value, buf);
    gtk_alert_dialog_set_buttons(shell, buttons);
    gtk_window_set_title(GTK_WINDOW(shell), _("Buy It!"));
    gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_NO);

    gtk_alert_dialog_choose(shell, GTK_WINDOW(toplevel), NULL,
                            buy_callback_response, pdialog);
  } else {
    const char *buttons[] = { _("Close"), NULL };

    shell = gtk_alert_dialog_new(
        /* TRANS: Last %s is pre-pluralised "Treasury contains %d gold." */
        PL_("%s costs %d gold.\n%s",
            "%s costs %d gold.\n%s", value),
        name, value, buf);
    gtk_alert_dialog_set_buttons(shell, buttons);
    gtk_window_set_title(GTK_WINDOW(shell), _("Buy It!"));
    gtk_alert_dialog_choose(shell, GTK_WINDOW(toplevel), NULL,
                            alert_close_response, NULL);
  }
}

/***********************************************************************//**
  Callback for the production list.
***************************************************************************/
static void change_production_callback(GtkSelectionModel *self,
                                      guint position,
                                      guint n_items,
                                      gpointer data)
{
  if (can_client_issue_orders()) {
    FcProdRow *row = gtk_single_selection_get_selected_item(
                                          GTK_SINGLE_SELECTION(self));
    struct universal univ = cid_production(row->id);

    city_change_production(((struct city_dialog *)data)->pcity, &univ);
  }
}

/***********************************************************************//**
  User has clicked sell-button
***************************************************************************/
static void sell_callback(const struct impr_type *pimprove, gpointer data)
{
  GtkWidget *shl;
  struct city_dialog *pdialog = (struct city_dialog *) data;
  pdialog->sell_id = improvement_number(pimprove);
  int price;

  if (!can_client_issue_orders()) {
    return;
  }

  if (test_player_sell_building_now(client.conn.playing, pdialog->pcity,
                                    pimprove) != TR_SUCCESS) {
    return;
  }

  price = impr_sell_gold(pimprove);
  shl = gtk_message_dialog_new(NULL,
    GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_MESSAGE_QUESTION,
    GTK_BUTTONS_YES_NO,
    PL_("Sell %s for %d gold?",
        "Sell %s for %d gold?", price),
    city_improvement_name_translation(pdialog->pcity, pimprove), price);
  setup_dialog(shl, pdialog->shell);
  pdialog->sell_shell = shl;

  gtk_window_set_title(GTK_WINDOW(shl), _("Sell It!"));

  g_signal_connect(shl, "response",
                   G_CALLBACK(sell_callback_response), pdialog);

  gtk_window_present(GTK_WINDOW(shl));
}

/***********************************************************************//**
  User has responded to sell price dialog
***************************************************************************/
static void sell_callback_response(GtkWidget *w, gint response, gpointer data)
{
  struct city_dialog *pdialog = data;

  if (response == GTK_RESPONSE_YES) {
    city_sell_improvement(pdialog->pcity, pdialog->sell_id);
  }
  gtk_window_destroy(GTK_WINDOW(w));

  pdialog->sell_shell = NULL;
}

/***********************************************************************//**
  This is here because it's closely related to the sell stuff
***************************************************************************/
static void impr_callback(GtkColumnView *self, guint position,
                          gpointer data)
{
  GdkSeat *seat;
  GdkModifierType mask;
  GListStore *store = ((struct city_dialog *)data)->overview.improvement_list;
  FcImprRow *row = g_list_model_get_item(G_LIST_MODEL(store), position);
  const struct impr_type *pimpr = row->impr;
  GtkWidget *wdg = ((struct city_dialog *)data)->shell;

  seat = gdk_display_get_default_seat(gtk_widget_get_display(wdg));
  mask = gdk_device_get_modifier_state(gdk_seat_get_keyboard(seat));

  if (!(mask & GDK_CONTROL_MASK)) {
    sell_callback(pimpr, data);
  } else {
    if (is_great_wonder(pimpr)) {
      popup_help_dialog_typed(improvement_name_translation(pimpr), HELP_WONDER);
    } else {
      popup_help_dialog_typed(improvement_name_translation(pimpr), HELP_IMPROVEMENT);
    }
  }
}

/************ Callbacks for stuff on the Misc. Settings page **************/

/***********************************************************************//**
  Called when Rename button pressed
***************************************************************************/
static void rename_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;

  pdialog = (struct city_dialog *) data;

  pdialog->rename_shell = input_dialog_create(GTK_WINDOW(pdialog->shell),
                                              /* "shellrenamecity" */
                                              _("Rename City"),
                                              _("What should we rename the city to?"),
                                              city_name_get(pdialog->pcity),
                                              rename_popup_callback, pdialog);
}

/***********************************************************************//**
  Called when user has finished with "Rename City" popup
***************************************************************************/
static void rename_popup_callback(gpointer data, gint response,
                                  const char *input)
{
  struct city_dialog *pdialog = data;

  if (pdialog) {
    if (response == GTK_RESPONSE_OK) {
      city_rename(pdialog->pcity, input);
    } /* else CANCEL or DELETE_EVENT */

    pdialog->rename_shell = NULL;
  }
}

/***********************************************************************//**
  Sets which page will be set on reopen of dialog
***************************************************************************/
static void misc_whichtab_callback(GtkWidget *w, gpointer data)
{
  new_dialog_def_page = GPOINTER_TO_INT(data);
}

/***********************************************************************//**
  City options callbacks
***************************************************************************/
static void cityopt_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;

  if (!can_client_issue_orders()) {
    return;
  }

  if (!pdialog->misc.block_signal) {
    struct city *pcity = pdialog->pcity;
    bv_city_options new_options;

    fc_assert(CITYO_LAST == 3);

    BV_CLR_ALL(new_options);
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(pdialog->misc.disband_on_settler))) {
      BV_SET(new_options, CITYO_DISBAND);
    }
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(pdialog->misc.new_citizens_radio[1]))) {
      BV_SET(new_options, CITYO_SCIENCE_SPECIALISTS);
    }
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(pdialog->misc.new_citizens_radio[2]))) {
      BV_SET(new_options, CITYO_GOLD_SPECIALISTS);
    }

    dsend_packet_city_options_req(&client.conn, pcity->id, new_options,
                                  pcity->wlcb);
  }
}

/***********************************************************************//**
  Refresh the city options (auto_[land, air, sea, helicopter] and
  disband-is-size-1) in the misc page.
***************************************************************************/
static void set_cityopt_values(struct city_dialog *pdialog)
{
  struct city *pcity = pdialog->pcity;

  pdialog->misc.block_signal = 1;

  gtk_check_button_set_active(GTK_CHECK_BUTTON(pdialog->misc.disband_on_settler),
                              is_city_option_set(pcity, CITYO_DISBAND));

  if (is_city_option_set(pcity, CITYO_SCIENCE_SPECIALISTS)) {
    gtk_check_button_set_active(GTK_CHECK_BUTTON
                                (pdialog->misc.new_citizens_radio[1]), TRUE);
  } else if (is_city_option_set(pcity, CITYO_GOLD_SPECIALISTS)) {
    gtk_check_button_set_active(GTK_CHECK_BUTTON
                                (pdialog->misc.new_citizens_radio[2]), TRUE);
  } else {
    gtk_check_button_set_active(GTK_CHECK_BUTTON
                                (pdialog->misc.new_citizens_radio[0]), TRUE);
  }
  pdialog->misc.block_signal = 0;
}

/******************** Callbacks for: Close, Prev, Next. *******************/

/***********************************************************************//**
  User has closed city dialog
***************************************************************************/
static void close_callback(GtkWidget *w, gpointer data)
{
  close_city_dialog((struct city_dialog *) data);
}

/***********************************************************************//**
  User has closed city dialog
***************************************************************************/
static void city_destroy_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  int width, height;

  /* Save size of the city dialog. */
  gtk_window_get_default_size(GTK_WINDOW(w), &width, &height);

  pdialog = (struct city_dialog *) data;
  gtk_widget_set_visible(pdialog->shell, FALSE);

  if (game.info.citizen_nationality) {
    citizens_dialog_close(pdialog->pcity);
  }
  close_happiness_dialog(pdialog->pcity);
  close_cma_dialog(pdialog->pcity);

  /* Save size of the city dialog. */
  GUI_GTK_OPTION(citydlg_xsize)
    = CLIP(GUI_GTK5_CITYDLG_MIN_XSIZE,
           width,
           GUI_GTK5_CITYDLG_MAX_XSIZE);
  GUI_GTK_OPTION(citydlg_ysize)
    = CLIP(GUI_GTK5_CITYDLG_MIN_YSIZE,
           height,
           GUI_GTK5_CITYDLG_MAX_YSIZE);

  last_page
    = gtk_notebook_get_current_page(GTK_NOTEBOOK(pdialog->notebook));

  close_citydlg_unit_popover(pdialog);
  dialog_list_remove(dialog_list, pdialog);

  unit_node_vector_free(&pdialog->overview.supported_units);
  unit_node_vector_free(&pdialog->overview.present_units);

  if (pdialog->sell_shell) {
    gtk_window_destroy(GTK_WINDOW(pdialog->sell_shell));
  }
  if (pdialog->rename_shell) {
    gtk_window_destroy(GTK_WINDOW(pdialog->rename_shell));
  }

  cairo_surface_destroy(pdialog->map_canvas_store_unscaled);
  cairo_surface_destroy(pdialog->citizen_surface);

  free(pdialog);

  /* Need to do this every time a new dialog is closed. */
  city_dialog_update_prev_next();
}

/***********************************************************************//**
  Close city dialog
***************************************************************************/
static void close_city_dialog(struct city_dialog *pdialog)
{
  gtk_window_destroy(GTK_WINDOW(pdialog->shell));
}

/***********************************************************************//**
  Callback for the prev/next buttons. Switches to the previous/next
  city.
***************************************************************************/
static void switch_city_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  int i, j, dir, size;
  struct city *new_pcity = NULL;

  if (client_is_global_observer()) {
    return;
  }

  size = city_list_size(client.conn.playing->cities);

  fc_assert_ret(city_dialogs_have_been_initialised);
  fc_assert_ret(size >= 1);
  fc_assert_ret(city_owner(pdialog->pcity) == client.conn.playing);

  if (size == 1) {
    return;
  }

  /* dir = 1 will advance to the city, dir = -1 will get previous */
  if (w == GTK_WIDGET(pdialog->next_command)) {
    dir = 1;
  } else if (w == GTK_WIDGET(pdialog->prev_command)) {
    dir = -1;
  } else {
    /* Always fails. */
    fc_assert_ret(w == GTK_WIDGET(pdialog->next_command)
                  || w == GTK_WIDGET(pdialog->prev_command));
    dir = 1;
  }

  for (i = 0; i < size; i++) {
    if (pdialog->pcity == city_list_get(client.conn.playing->cities, i)) {
      break;
    }
  }

  fc_assert_ret(i < size);

  for (j = 1; j < size; j++) {
    struct city *other_pcity = city_list_get(client.conn.playing->cities,
                                             (i + dir * j + size) % size);
    struct city_dialog *other_pdialog = get_city_dialog(other_pcity);

    fc_assert_ret(other_pdialog != pdialog);
    if (!other_pdialog) {
      new_pcity = other_pcity;
      break;
    }
  }

  if (!new_pcity) {
    /* Every other city has an open city dialog. */
    return;
  }

  /* cleanup happiness dialog */
  if (game.info.citizen_nationality) {
    citizens_dialog_close(pdialog->pcity);
  }
  close_happiness_dialog(pdialog->pcity);

  pdialog->pcity = new_pcity;

  /* reinitialize happiness, and cma dialogs */
  if (game.info.citizen_nationality) {
    gtk_box_append(GTK_BOX(pdialog->happiness.citizens),
                   citizens_dialog_display(pdialog->pcity));
  }
  gtk_box_append(GTK_BOX(pdialog->happiness.widget),
                 get_top_happiness_display(pdialog->pcity, low_citydlg, pdialog->shell));
  if (!client_is_observer()) {
    fc_assert(pdialog->cma_editor != NULL);
    pdialog->cma_editor->pcity = new_pcity;
  }

  reset_city_worklist(pdialog->production.worklist, pdialog->pcity);

  can_slide = FALSE;
  center_tile_mapcanvas(pdialog->pcity->tile);
  can_slide = TRUE;
  if (!client_is_observer()) {
    set_cityopt_values(pdialog);  /* Need not be in real_city_dialog_refresh */
  }

  real_city_dialog_refresh(pdialog->pcity);

  /* Recenter the city map(s) */
  city_dialog_map_recenter(pdialog->overview.map_canvas.sw);
  if (pdialog->happiness.map_canvas.sw) {
    city_dialog_map_recenter(pdialog->happiness.map_canvas.sw);
  }
}

/***********************************************************************//**
  Refresh worklist editor for all city dialogs.
***************************************************************************/
void refresh_all_city_worklists(void)
{
  dialog_list_iterate(dialog_list, pdialog) {
    refresh_worklist(pdialog->production.worklist);
  } dialog_list_iterate_end;
}
