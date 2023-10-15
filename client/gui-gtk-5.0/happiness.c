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

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* common */
#include "city.h"
#include "game.h"
#include "government.h"

/* client */
#include "text.h"
#include "tilespec.h"

/* client/gui-gtk-4.0 */
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "happiness.h"
#include "mapview.h"

/* Semi-arbitrary number that controls the width of the happiness widget */
#define HAPPINESS_PIX_WIDTH 30

#define FEELING_WIDTH   (HAPPINESS_PIX_WIDTH * tileset_small_sprite_width(tileset))
#define FEELING_HEIGHT  (tileset_small_sprite_height(tileset))

#define NUM_HAPPINESS_MODIFIERS 6

enum { CITIES, LUXURIES, BUILDINGS, NATIONALITY, UNITS, WONDERS };

struct happiness_dialog {
  struct city *pcity;
  GtkWidget *win;
  GtkWidget *shell;
  GtkWidget *cityname_label;
  GtkWidget *feeling_pics[NUM_HAPPINESS_MODIFIERS];
  cairo_surface_t *feeling_surfaces[NUM_HAPPINESS_MODIFIERS];
  GtkWidget *happiness_label[NUM_HAPPINESS_MODIFIERS];
  GtkWidget *close;
};

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct happiness_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct happiness_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list *dialog_list;
static struct happiness_dialog *get_happiness_dialog(struct city *pcity);
static struct happiness_dialog *create_happiness_dialog(struct city *pcity,
                                                        bool low_dlg,
                                                        GtkWidget *win);
static gboolean show_happiness_popup(GtkGestureClick *gesture,
                                     int n_press,
                                     double x, double y, gpointer data);

/**********************************************************************//**
  Create happiness dialog
**************************************************************************/
void happiness_dialog_init(void)
{
  dialog_list = dialog_list_new();
}

/**********************************************************************//**
  Remove happiness dialog
**************************************************************************/
void happiness_dialog_done(void)
{
  dialog_list_destroy(dialog_list);
}

/**********************************************************************//**
  Return happiness dialog for a city
**************************************************************************/
static struct happiness_dialog *get_happiness_dialog(struct city *pcity)
{
  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pcity == pcity) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Popup for the happiness display.
**************************************************************************/
static gboolean show_happiness_popup(GtkGestureClick *gesture,
                                     int n_press,
                                     double x, double y, gpointer data)
{
  GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  struct happiness_dialog *pdialog = g_object_get_data(G_OBJECT(w),
                                                       "pdialog");
  GtkWidget *p, *label;
  char buf[1024];

  switch (GPOINTER_TO_UINT(data)) {
  case CITIES:
    sz_strlcpy(buf, text_happiness_cities(pdialog->pcity));
    break;
  case LUXURIES:
    sz_strlcpy(buf, text_happiness_luxuries(pdialog->pcity));
    break;
  case BUILDINGS:
    sz_strlcpy(buf, text_happiness_buildings(pdialog->pcity));
    break;
  case NATIONALITY:
    sz_strlcpy(buf, text_happiness_nationality(pdialog->pcity));
    break;
  case UNITS:
    sz_strlcpy(buf, text_happiness_units(pdialog->pcity));
    break;
  case WONDERS:
    sz_strlcpy(buf, text_happiness_wonders(pdialog->pcity));
    break;
  default:
    return TRUE;
  }

  p = gtk_popover_new();

  gtk_widget_set_parent(p, w);

  label = gtk_label_new(buf);
  /* FIXME: there is no font option corresponding to this style name.
   * Remove?: */
  gtk_widget_set_name(label, "city_happiness_label");
  gtk_widget_set_margin_start(label, 4);
  gtk_widget_set_margin_end(label, 4);
  gtk_widget_set_margin_top(label, 4);
  gtk_widget_set_margin_bottom(label, 4);
  gtk_popover_set_child(GTK_POPOVER(p), label);

  gtk_popover_popup(GTK_POPOVER(p));

  return TRUE;
}

/**********************************************************************//**
  Create the happiness notebook page.
**************************************************************************/
static struct happiness_dialog *create_happiness_dialog(struct city *pcity,
                                                        bool low_dlg,
                                                        GtkWidget *win)
{
  int i;
  struct happiness_dialog *pdialog;
  GtkWidget *label, *table;
  char buf[700];

  static const char *happiness_label_str[NUM_HAPPINESS_MODIFIERS] = {
    N_("Cities:"),
    N_("Luxuries:"),
    N_("Buildings:"),
    N_("Nationality:"),
    N_("Units:"),
    N_("Wonders:"),
  };
  static bool happiness_label_str_done;

  pdialog = fc_malloc(sizeof(struct happiness_dialog));
  pdialog->pcity = pcity;

  pdialog->shell = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(pdialog->shell),
                                 GTK_ORIENTATION_VERTICAL);

  pdialog->cityname_label = gtk_frame_new(_("Happiness"));
  gtk_grid_attach(GTK_GRID(pdialog->shell), pdialog->cityname_label, 0, 0, 1, 1);

  table = gtk_grid_new();
  gtk_widget_set_margin_bottom(table, 4);
  gtk_widget_set_margin_end(table, 4);
  gtk_widget_set_margin_start(table, 4);
  gtk_widget_set_margin_top(table, 4);
  gtk_grid_set_row_spacing(GTK_GRID(table), 10);

  intl_slist(ARRAY_SIZE(happiness_label_str), happiness_label_str,
             &happiness_label_str_done);

  gtk_frame_set_child(GTK_FRAME(pdialog->cityname_label), table);

  for (i = 0; i < NUM_HAPPINESS_MODIFIERS; i++) {
    GtkWidget *pic;
    GtkEventController *controller;

    /* Set spacing between lines of citizens */

    /* Happiness labels */
    label = gtk_label_new(happiness_label_str[i]);
    pdialog->happiness_label[i] = label;
    gtk_widget_set_name(label, "city_label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);

    gtk_grid_attach(GTK_GRID(table), label, 0, i, 1, 1);

    /* List of citizens */
    pdialog->feeling_surfaces[i] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                              FEELING_WIDTH,
                                                              FEELING_HEIGHT);
    pic = gtk_picture_new();
    pdialog->feeling_pics[i] = pic;
    gtk_widget_set_margin_start(pic, 5);
    g_object_set_data(G_OBJECT(pic), "pdialog", pdialog);

    controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
    g_signal_connect(controller, "pressed",
                     G_CALLBACK(show_happiness_popup), GUINT_TO_POINTER(i));
    gtk_widget_add_controller(pic, controller);

    gtk_widget_set_halign(pic, GTK_ALIGN_START);
    gtk_widget_set_valign(pic, GTK_ALIGN_START);

    gtk_grid_attach(GTK_GRID(table), pic, 1, i, 1, 1);
  }

  /* TRANS: the width of this text defines the width of the city dialog.
   *        '%s' is either space or newline depending on screen real estate. */
  fc_snprintf(buf, sizeof(buf),
              _("Additional information is available%svia left "
                "click on the citizens."), low_dlg ? "\n" : " ");
  label = gtk_label_new(buf);
  gtk_widget_set_name(label, "city_label");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(table), label, 0, NUM_HAPPINESS_MODIFIERS, 2, 1);

  gtk_widget_set_visible(pdialog->shell, TRUE);
  dialog_list_prepend(dialog_list, pdialog);
  refresh_happiness_dialog(pcity);

  pdialog->win = win;

  return pdialog;
}

/**********************************************************************//**
  Refresh citizens surface
**************************************************************************/
static void refresh_feeling_surface(GtkWidget *pic,
                                    cairo_surface_t *dst, struct city *pcity,
                                    enum citizen_feeling index)
{
  enum citizen_category categories[MAX_CITY_SIZE];
  int i;
  int num_citizens = get_city_citizen_types(pcity, index, categories);
  int offset = MIN(tileset_small_sprite_width(tileset), FEELING_WIDTH / num_citizens);
  cairo_t *cr;

  cr = cairo_create(dst);

  for (i = 0; i < num_citizens; i++) {
    cairo_set_source_surface(cr,
                             get_citizen_sprite(tileset, categories[i], i, pcity)->surface,
                             i * offset, 0);
    cairo_rectangle(cr, i * offset, 0, offset, FEELING_HEIGHT);
    cairo_fill(cr);
  }

  picture_set_from_surface(GTK_PICTURE(pic), dst);

  cairo_destroy(cr);
}

/**********************************************************************//**
  Refresh whole happiness dialog
**************************************************************************/
void refresh_happiness_dialog(struct city *pcity)
{
  int i;
  struct happiness_dialog *pdialog = get_happiness_dialog(pcity);

  for (i = 0; i < FEELING_LAST; i++) {
    refresh_feeling_surface(pdialog->feeling_pics[i],
                            pdialog->feeling_surfaces[i], pdialog->pcity, i);
  }
}

/**********************************************************************//**
  Close happiness dialog of given city
**************************************************************************/
void close_happiness_dialog(struct city *pcity)
{
  struct happiness_dialog *pdialog = get_happiness_dialog(pcity);
  int i;

  if (pdialog == nullptr) {
    /* City which is being investigated doesn't contain happiness tab */
    return;
  }

  gtk_widget_set_visible(pdialog->shell, FALSE);

  dialog_list_remove(dialog_list, pdialog);

  gtk_box_remove(GTK_BOX(gtk_widget_get_parent(pdialog->shell)),
                 pdialog->shell);

  for (i = 0; i < NUM_HAPPINESS_MODIFIERS; i++) {
    cairo_surface_destroy(pdialog->feeling_surfaces[i]);
  }

  free(pdialog);
}

/**********************************************************************//**
  Create happiness dialog and get its widget
**************************************************************************/
GtkWidget *get_top_happiness_display(struct city *pcity, bool low_dlg,
                                     GtkWidget *win)
{
  return create_happiness_dialog(pcity, low_dlg, win)->shell;
}
