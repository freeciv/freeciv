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

/* client/gui-gtk-3.0 */
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "happiness.h"
#include "mapview.h"

/* semi-arbitrary number that controls the width of the happiness widget */
#define HAPPINESS_PIX_WIDTH 30

#define	PIXCOMM_WIDTH	(HAPPINESS_PIX_WIDTH * tileset_small_sprite_width(tileset))
#define	PIXCOMM_HEIGHT	(tileset_small_sprite_height(tileset))

#define NUM_HAPPINESS_MODIFIERS 6

enum { CITIES, LUXURIES, BUILDINGS, NATIONALITY, UNITS, WONDERS };

struct happiness_dialog {
  struct city *pcity;
  GtkWidget *shell;
  GtkWidget *cityname_label;
  GtkWidget *hpixmaps[NUM_HAPPINESS_MODIFIERS];
  GtkWidget *happiness_ebox[NUM_HAPPINESS_MODIFIERS];
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
static struct happiness_dialog *create_happiness_dialog(struct city *pcity);
static gboolean show_happiness_popup(GtkWidget *w,
                                     GdkEventButton *ev,
                                     gpointer data);
static gboolean show_happiness_button_release(GtkWidget *w,
                                              GdkEventButton *ev,
                                              gpointer data);

/****************************************************************
  Create happiness dialog
*****************************************************************/
void happiness_dialog_init()
{
  dialog_list = dialog_list_new();
}

/****************************************************************
  Remove happiness dialog
*****************************************************************/
void happiness_dialog_done()
{
  dialog_list_destroy(dialog_list);
}

/****************************************************************
  Return happiness dialog for a city
*****************************************************************/
static struct happiness_dialog *get_happiness_dialog(struct city *pcity)
{
  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pcity == pcity) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/****************************************************************
  Popup for the happiness display.
*****************************************************************/
static gboolean show_happiness_popup(GtkWidget *w,
                                     GdkEventButton *ev,
                                     gpointer data)
{
  struct happiness_dialog *pdialog = g_object_get_data(G_OBJECT(w),
                                                        "pdialog");

  if (ev->button == 1) {
    GtkWidget *p, *label, *frame;
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
    case UNITS:
      sz_strlcpy(buf, text_happiness_units(pdialog->pcity));
      break;
    case WONDERS:
      sz_strlcpy(buf, text_happiness_wonders(pdialog->pcity));
      break;
    default:
      return TRUE;
    }

    p = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_name(p, "Freeciv");
    gtk_container_set_border_width(GTK_CONTAINER(p), 2);
    gtk_window_set_position(GTK_WINDOW(p), GTK_WIN_POS_MOUSE);

    frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(p), frame);

    label = gtk_label_new(buf);
    gtk_widget_set_name(label, "city_happiness_label");
    gtk_misc_set_padding(GTK_MISC(label), 4, 4);
    gtk_container_add(GTK_CONTAINER(frame), label);
    gtk_widget_show_all(p);

    gdk_device_grab(ev->device, gtk_widget_get_window(p),
                    GDK_OWNERSHIP_NONE, TRUE, GDK_BUTTON_RELEASE_MASK, NULL,
                    ev->time);
    gtk_grab_add(p);

    g_signal_connect_after(p, "button_release_event",
                           G_CALLBACK(show_happiness_button_release), NULL);
  }

  return TRUE;
}

/**************************************************************************
  Clear the happiness popup.
**************************************************************************/
static gboolean show_happiness_button_release(GtkWidget *w,
                                              GdkEventButton *ev,
                                              gpointer data)
{
  gtk_grab_remove(w);
  gdk_device_ungrab(ev->device, ev->time);
  gtk_widget_destroy(w);
  return FALSE;
}

/**************************************************************************
  Create the happiness notebook page.
**************************************************************************/
static struct happiness_dialog *create_happiness_dialog(struct city *pcity)
{
  int i;
  struct happiness_dialog *pdialog;
  GtkWidget *ebox, *label, *table;

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
  gtk_container_add(GTK_CONTAINER(pdialog->shell), pdialog->cityname_label);

  table = gtk_grid_new();
  g_object_set(table, "margin", 4, NULL);
  gtk_grid_set_row_spacing(GTK_GRID(table), 10);

  intl_slist(ARRAY_SIZE(happiness_label_str), happiness_label_str,
             &happiness_label_str_done);

  gtk_container_add(GTK_CONTAINER(pdialog->cityname_label), table);

  for (i = 0; i < NUM_HAPPINESS_MODIFIERS; i++) {
    /* set spacing between lines of citizens*/

    /* happiness labels */
    label = gtk_label_new(happiness_label_str[i]);
    pdialog->happiness_label[i] = label;
    gtk_widget_set_name(label, "city_label");
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

    gtk_grid_attach(GTK_GRID(table), label, 0, i, 1, 1);

    /* list of citizens */
    ebox = gtk_event_box_new();
    gtk_widget_set_margin_left(ebox, 5);
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), FALSE);
    g_object_set_data(G_OBJECT(ebox), "pdialog", pdialog);
    g_signal_connect(ebox, "button_press_event",
                     G_CALLBACK(show_happiness_popup), GUINT_TO_POINTER(i));
    pdialog->happiness_ebox[i] = ebox;

    pdialog->hpixmaps[i] = gtk_pixcomm_new(PIXCOMM_WIDTH, PIXCOMM_HEIGHT);
    gtk_container_add(GTK_CONTAINER(ebox), pdialog->hpixmaps[i]);
    gtk_misc_set_alignment(GTK_MISC(pdialog->hpixmaps[i]), 0, 0);

    gtk_grid_attach(GTK_GRID(table), ebox, 1, i, 1, 1);
  }

  /* TRANS: the width of this text defines the width of the city dialog. */
  label = gtk_label_new(_("Additional information is available via left "
                          "click on the citizens."));
  gtk_widget_set_name(label, "city_label");
  gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
  gtk_grid_attach(GTK_GRID(table), label, 0, NUM_HAPPINESS_MODIFIERS, 2, 1);

  gtk_widget_show_all(pdialog->shell);
  dialog_list_prepend(dialog_list, pdialog);
  refresh_happiness_dialog(pcity);

  return pdialog;
}

/**************************************************************************
  Refresh citizens pixcomm
**************************************************************************/
static void refresh_pixcomm(GtkPixcomm *dst, struct city *pcity,
			    enum citizen_feeling index)
{
  enum citizen_category citizens[MAX_CITY_SIZE];
  int i;
  int num_citizens = get_city_citizen_types(pcity, index, citizens);
  int offset = MIN(tileset_small_sprite_width(tileset), PIXCOMM_WIDTH / num_citizens);

  gtk_pixcomm_clear(dst);

  for (i = 0; i < num_citizens; i++) {
    gtk_pixcomm_copyto(dst, get_citizen_sprite(tileset, citizens[i], i, pcity),
		       i * offset, 0);
  }
}

/**************************************************************************
  Refresh whole happiness dialog
**************************************************************************/
void refresh_happiness_dialog(struct city *pcity)
{
  int i;
  struct happiness_dialog *pdialog = get_happiness_dialog(pcity);

  for (i = 0; i < FEELING_LAST; i++) {
    refresh_pixcomm(GTK_PIXCOMM(pdialog->hpixmaps[i]), pdialog->pcity, i);
  }
}

/**************************************************************************
  Close happiness dialog of given city
**************************************************************************/
void close_happiness_dialog(struct city *pcity)
{
  struct happiness_dialog *pdialog = get_happiness_dialog(pcity);
  if (pdialog == NULL) {
    /* City which is being investigated doesn't contain happiness tab */
    return;
  }

  gtk_widget_hide(pdialog->shell);

  dialog_list_remove(dialog_list, pdialog);

  gtk_widget_destroy(pdialog->shell);
  free(pdialog);
}

/**************************************************************************
  Create happiness dialog and get its widget
**************************************************************************/
GtkWidget *get_top_happiness_display(struct city *pcity)
{
  return create_happiness_dialog(pcity)->shell;
}
