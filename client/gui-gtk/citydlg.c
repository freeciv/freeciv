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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "cityicon.ico"
#include "civclient.h"
#include "climap.h"
#include "clinet.h"
#include "cma_fec.h" 
#include "control.h"
#include "options.h"
#include "tilespec.h"

#include "chatline.h"
#include "cityrep.h"
#include "cma_fe.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "happiness.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapview.h"
#include "repodlgs.h"
#include "text.h"
#include "wldlg.h"

#include "citydlg.h"

struct city_dialog;

/* get 'struct dialog_list' and related functions: */
#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct city_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct city_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static int NUM_UNITS_SHOWN;
static int MAX_UNIT_ROWS;
static int MINI_NUM_UNITS;

enum { OVERVIEW_PAGE, UNITS_PAGE, WORKLIST_PAGE,
  HAPPINESS_PAGE, CMA_PAGE, TRADE_PAGE, MISC_PAGE
};

enum info_style { NORMAL, ORANGE, RED, NUM_INFO_STYLES };

#define NUM_CITIZENS_SHOWN 25
#define NUM_CITY_OPTS 5
#define NUM_INFO_FIELDS 11      /* number of fields in city_info */
#define NUM_PAGES 8             /* the number of pages in city dialog notebook 
                                 * (+1) if you change this, you must add an
                                 * entry to misc_whichtab_label[] */

static int citydialog_width, citydialog_height, support_frame_width,
    support_frame_height;

struct city_dialog {
  struct city *pcity;

  GtkWidget *shell;
  GtkWidget *citizen_pixmap;
  GdkPixmap *map_canvas_store;
  GtkWidget *title_frame;
  GtkWidget *notebook;
  GtkAccelGroup *accel;

  struct {
    GtkWidget *map_canvas;
    GtkWidget *map_canvas_pixmap;
    GtkWidget *tradelist;
    GtkWidget *currently_building_frame;
    GtkWidget *progress_label;
    GtkWidget *improvement_list;
    GtkWidget *buy_command;
    GtkWidget *change_command;
    GtkWidget *sell_command;

    GtkWidget *production_label;
    GtkWidget *output_label;
    GtkWidget *granary_label;
    GtkWidget *pollution_label;

    GtkWidget *present_units_frame;
    GtkWidget **present_unit_boxes;
    GtkWidget *supported_units_frame;
    GtkWidget **supported_unit_boxes;

    GtkWidget **supported_unit_pixmaps;
    GtkWidget *supported_unit_button[2];
    GtkWidget **present_unit_pixmaps;
    GtkWidget *present_unit_button[2];

    int *supported_unit_ids;
    int supported_unit_pos;
    int *present_unit_ids;
    int present_unit_pos;

    GtkWidget *info_label[NUM_INFO_FIELDS];
  } overview;

  struct {
    GtkWidget *activate_command;
    GtkWidget *sentry_all_command;
    GtkWidget *show_units_command;

    GtkWidget *present_units_frame;
    GtkWidget **present_unit_boxes;
    GtkWidget *supported_units_frame;
    GtkWidget **supported_unit_boxes;

    GtkWidget **supported_unit_pixmaps;
    GtkWidget *supported_unit_button[2];
    GtkWidget **present_unit_pixmaps;
    GtkWidget *present_unit_button[2];

    int *supported_unit_ids;
    int supported_unit_pos;
    int *present_unit_ids;
    int present_unit_pos;
  } unit;

  struct worklist_editor *wl_editor;

  struct {
    GtkWidget *map_canvas;
    GtkWidget *map_canvas_pixmap;
    GtkWidget *widget;
    GtkWidget *info_label[NUM_INFO_FIELDS];
  } happiness;

  struct cma_dialog *cma_editor;

  struct {
    GtkWidget *rename_command;
    GtkWidget *new_citizens_radio[3];
    GtkWidget *city_opts[NUM_CITY_OPTS];
    GtkWidget *whichtab_radio[NUM_PAGES];
    short block_signal;
  } misc;

  GtkWidget *buy_shell, *sell_shell;
  GtkWidget *change_shell, *change_list;
  GtkWidget *rename_shell, *rename_input;

  GtkWidget *prev_command, *next_command;

  Impr_Type_id sell_id;

  int cwidth;

  /* This is used only to avoid too many refreshes. */
  int last_improvlist_seen[B_LAST];

  bool is_modal;
};

static GdkBitmap *icon_bitmap;
static GtkRcStyle *info_label_style[NUM_INFO_STYLES] = { NULL, NULL, NULL };
static int notebook_tab_accels[NUM_PAGES - 1];	/* so localization works */

static struct dialog_list dialog_list;
static bool city_dialogs_have_been_initialised;
static int canvas_width, canvas_height;
static int new_dialog_def_page = OVERVIEW_PAGE;
static int last_page = OVERVIEW_PAGE;

/****************************************/

static void initialize_city_dialogs(void);

static struct city_dialog *get_city_dialog(struct city *pcity);
static gint keyboard_handler(GtkWidget * widget, GdkEventKey * event,
			     struct city_dialog *pdialog);

static GtkWidget *create_city_info_table(GtkWidget **info_label);
static void create_and_append_overview_page(struct city_dialog *pdialog);
static void create_and_append_units_page(struct city_dialog *pdialog);
static void create_and_append_worklist_page(struct city_dialog *pdialog);
static void create_and_append_happiness_page(struct city_dialog *pdialog);
static void create_and_append_cma_page(struct city_dialog *pdialog);
static void create_and_append_trade_page(struct city_dialog *pdialog);
static void create_and_append_misc_page(struct city_dialog *pdialog);

static struct city_dialog *create_city_dialog(struct city *pcity,
					      bool make_modal);

static void city_dialog_update_title(struct city_dialog *pdialog);
static void city_dialog_update_citizens(struct city_dialog *pdialog);
static void city_dialog_update_information(GtkWidget **info_label,
                                           struct city_dialog *pdialog);
static void city_dialog_update_map(struct city_dialog *pdialog);
static void city_dialog_update_building(struct city_dialog *pdialog);
static void city_dialog_update_improvement_list(struct city_dialog
						*pdialog);
static void city_dialog_update_supported_units(struct city_dialog
					       *pdialog);
static void city_dialog_update_present_units(struct city_dialog *pdialog);
static void city_dialog_update_tradelist(struct city_dialog *pdialog);
static void city_dialog_update_prev_next(void);

static void supported_units_page_pos_callback(GtkWidget * w,
					      gpointer data);
static void present_units_page_pos_callback(GtkWidget * w, gpointer data);

static void activate_all_units_callback(GtkWidget * w, gpointer data);
static void sentry_all_units_callback(GtkWidget * w, gpointer data);
static void show_units_callback(GtkWidget * w, gpointer data);

static gint supported_unit_callback(GtkWidget * w, GdkEventButton * ev,
				    gpointer data);
static gint present_unit_callback(GtkWidget * w, GdkEventButton * ev,
				  gpointer data);
static gint supported_unit_middle_callback(GtkWidget * w,
					   GdkEventButton * ev,
					   gpointer data);
static gint present_unit_middle_callback(GtkWidget * w,
					 GdkEventButton * ev,
					 gpointer data);

static void unit_activate_callback(gpointer data);
static void supported_unit_activate_close_callback(gpointer data);
static void present_unit_activate_close_callback(gpointer data);
static void unit_sentry_callback(gpointer data);
static void unit_fortify_callback(gpointer data);
static void unit_disband_callback(gpointer data);
static void unit_homecity_callback(gpointer data);
static void unit_upgrade_callback(gpointer data);
static void unit_upgrade_callback_yes(gpointer data);
static void unit_center_callback(gpointer data);

static void citizens_callback(GtkWidget * w, GdkEventButton * ev,
			      gpointer data);
static gint button_down_citymap(GtkWidget * w, GdkEventButton * ev);
static void draw_map_canvas(struct city_dialog *pdialog);
static gint city_map_canvas_expose(GtkWidget * w, GdkEventExpose * ev,
				   gpointer data);

static void change_callback(GtkWidget * w, gpointer data);
static gint change_deleted_callback(GtkWidget * w, GdkEvent * ev,
				    gpointer data);
static void change_no_callback(GtkWidget * w, gpointer data);
static void change_yes_callback(GtkWidget * w, gpointer data);
static void change_list_callback(GtkWidget * w, gint row, gint col,
				 GdkEvent * ev, gpointer data);
static void change_help_callback(GtkWidget * w, gpointer data);

static void buy_callback(GtkWidget * w, gpointer data);
static void buy_callback_yes(gpointer data);
static void buy_close_callback(gpointer data);

static void sell_callback(GtkWidget * w, gpointer data);
static void sell_close_callback(gpointer data);
static void sell_callback_yes(gpointer data);
static void select_impr_list_callback(GtkWidget * w, gint row, gint column,
				      GdkEventButton * event,
				      gpointer data);

static void switch_page_callback(GtkNotebook * notebook,
				 GtkNotebookPage * page, gint page_num,
				 gpointer data);
static void commit_city_worklist(struct worklist *pwl, void *data);

static void rename_callback(GtkWidget * w, gpointer data);
static void rename_callback_no(gpointer data);
static void rename_callback_yes(const char *input, gpointer data);

static void set_cityopt_values(struct city_dialog *pdialog);
static void cityopt_callback(GtkWidget * w, gpointer data);
static void misc_whichtab_callback(GtkWidget * w, gpointer data);

static gint city_dialog_delete_callback(GtkWidget * w, GdkEvent * ev,
					gpointer data);
static void close_city_dialog(struct city_dialog *pdialog);
static void close_callback(GtkWidget * w, gpointer data);
static void switch_city_callback(GtkWidget * w, gpointer data);

/****************************************************************
  Called to set the dimensions of the city dialog, both on
  startup and if the tileset is changed.
*****************************************************************/
static void init_citydlg_dimensions(void)
{
  canvas_width = get_citydlg_canvas_width();
  canvas_height = get_citydlg_canvas_height();

  if (is_isometric) {
    MAX_UNIT_ROWS = (int) (100 / (UNIT_TILE_HEIGHT));
  } else {
    MAX_UNIT_ROWS = (int) (100 / (UNIT_TILE_HEIGHT + 6));
  }
}

/****************************************************************
...
*****************************************************************/
static void initialize_city_dialogs(void)
{
  int i;
  GdkColor orange = { 0, 65535, 32768, 0 };	/* not currently used */
  GdkColor red = { 0, 65535, 0, 0 };

  assert(!city_dialogs_have_been_initialised);

  dialog_list_init(&dialog_list);
  init_citydlg_dimensions();

  NUM_UNITS_SHOWN = (int) (MAX_UNIT_ROWS * 500) / (UNIT_TILE_WIDTH);

  /* make the styles */

  for (i = 0; i < NUM_INFO_STYLES; i++) {
    info_label_style[i] = gtk_rc_style_new();
  }
  /* info_syle[NORMAL] is normal, don't change it */
  info_label_style[ORANGE]->color_flags[GTK_STATE_NORMAL] |= GTK_RC_FG;
  info_label_style[ORANGE]->fg[GTK_STATE_NORMAL] = orange;
  info_label_style[RED]->color_flags[GTK_STATE_NORMAL] |= GTK_RC_FG;
  info_label_style[RED]->fg[GTK_STATE_NORMAL] = red;

  city_dialogs_have_been_initialised = TRUE;
}

/****************************************************************
  Called when the tileset changes.
*****************************************************************/
void reset_city_dialogs(void)
{
  if (!city_dialogs_have_been_initialised) {
    return;
  }

  init_citydlg_dimensions();

  dialog_list_iterate(dialog_list, pdialog) {
    /* There's no reasonable way to resize a GtkPixcomm, so we don't try.
       Instead we just redraw the overview within the existing area.  The
       player has to close and reopen the dialog to fix this. */
    city_dialog_update_map(pdialog);
  } dialog_list_iterate_end;
}

/****************************************************************
...
*****************************************************************/
static struct city_dialog *get_city_dialog(struct city *pcity)
{
  if (!city_dialogs_have_been_initialised)
    initialize_city_dialogs();

  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pcity == pcity)
      return pdialog;
  }
  dialog_list_iterate_end;
  return NULL;
}

/****************************************************************
...
*****************************************************************/
void refresh_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog = get_city_dialog(pcity);

  if (city_owner(pcity) == game.player_ptr) {
    city_report_dialog_update_city(pcity);
    economy_report_dialog_update();
  }

  if (!pdialog)
    return;

  city_dialog_update_title(pdialog);
  city_dialog_update_citizens(pdialog);
  city_dialog_update_information(pdialog->overview.info_label, pdialog);
  city_dialog_update_map(pdialog);
  city_dialog_update_building(pdialog);
  city_dialog_update_improvement_list(pdialog);
  city_dialog_update_supported_units(pdialog);
  city_dialog_update_present_units(pdialog);
  city_dialog_update_tradelist(pdialog);

  if (city_owner(pcity) == game.player_ptr) {
    bool have_present_units =
	(unit_list_size(&pcity->tile->units) > 0);

    update_worklist_editor(pdialog->wl_editor);

    city_dialog_update_information(pdialog->happiness.info_label, pdialog);
    refresh_happiness_dialog(pdialog->pcity);

    refresh_cma_dialog(pdialog->pcity, REFRESH_ALL);

    gtk_widget_set_sensitive(pdialog->unit.activate_command,
			     can_client_issue_orders() && 
			     have_present_units);
    gtk_widget_set_sensitive(pdialog->unit.sentry_all_command,
			     can_client_issue_orders() && 
			     have_present_units);
    gtk_widget_set_sensitive(pdialog->unit.show_units_command,
			     can_client_issue_orders() && 
			     have_present_units);
    gtk_widget_set_sensitive(pdialog->overview.sell_command, FALSE);
    gtk_widget_set_sensitive(pdialog->overview.change_command,
			     can_client_issue_orders()); 
  } else {
    /* Set the buttons we do not want live while a Diplomat investigates */
    gtk_widget_set_sensitive(pdialog->overview.buy_command, FALSE);
    gtk_widget_set_sensitive(pdialog->overview.change_command, FALSE);
    gtk_widget_set_sensitive(pdialog->unit.activate_command, FALSE);
    gtk_widget_set_sensitive(pdialog->unit.sentry_all_command, FALSE);
    gtk_widget_set_sensitive(pdialog->unit.show_units_command, FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
void refresh_unit_city_dialogs(struct unit *punit)
{
  struct city *pcity_sup, *pcity_pre;
  struct city_dialog *pdialog;

  pcity_sup = find_city_by_id(punit->homecity);
  pcity_pre = map_get_city(punit->tile);

  if (pcity_sup && (pdialog = get_city_dialog(pcity_sup)))
    city_dialog_update_supported_units(pdialog);

  if (pcity_pre && (pdialog = get_city_dialog(pcity_pre)))
    city_dialog_update_present_units(pdialog);
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_city_dialog(struct city *pcity, bool make_modal)
{
  struct city_dialog *pdialog;

  if (!(pdialog = get_city_dialog(pcity))) {
    pdialog = create_city_dialog(pcity, make_modal);
  }
  gtk_set_relative_position(toplevel, pdialog->shell, 10, 10);

  gtk_widget_show(pdialog->shell);
  gdk_window_raise(pdialog->shell->window);
}

/****************************************************************
...
*****************************************************************/
bool city_dialog_is_open(struct city *pcity)
{
  return get_city_dialog(pcity) != NULL;
}

/****************************************************************
popdown the dialog 
*****************************************************************/
void popdown_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog = get_city_dialog(pcity);

  if (pdialog) {
    close_city_dialog(pdialog);
  }
}

/****************************************************************
popdown all dialogs
*****************************************************************/
void popdown_all_city_dialogs(void)
{
  if (!city_dialogs_have_been_initialised) {
    return;
  }
  while (dialog_list_size(&dialog_list)) {
    close_city_dialog(dialog_list_get(&dialog_list, 0));
  }
}

/**************************************************************************
...
**************************************************************************/
static gint keyboard_handler(GtkWidget * widget, GdkEventKey * event,
			     struct city_dialog *pdialog)
{
  int page;

  for (page = 0; page < NUM_PAGES - 1; page++) {
    if ((event->keyval == notebook_tab_accels[page]) &&
	(!meta_accelerators || (event->state & GDK_MOD1_MASK))) {
      gtk_notebook_set_page(GTK_NOTEBOOK(pdialog->notebook), page);
      return TRUE;
    }
  }

  page = gtk_notebook_get_current_page(GTK_NOTEBOOK(pdialog->notebook));

  switch (event->keyval) {
  case GDK_Left:
    gtk_notebook_set_page(GTK_NOTEBOOK(pdialog->notebook),
			  (page = (page > 0) ? page - 1 : NUM_PAGES - 2));
    break;
  case GDK_Right:
    gtk_notebook_set_page(GTK_NOTEBOOK(pdialog->notebook),
			  (page = (page < NUM_PAGES - 2) ? page + 1 : 0));
    break;
  default:
    return FALSE;
  }

  return TRUE;
}

/****************************************************************
 used once in the overview page and once in the happiness page
 **info_label points to the info_label in the respective struct
****************************************************************/
static GtkWidget *create_city_info_table(GtkWidget **info_label)
{
  int i;
  GtkWidget *hbox, *table, *label;

  const char *output_label[NUM_INFO_FIELDS] = { N_("Food:"),
    N_("Prod:"),
    N_("Trade:"),
    N_("Gold:"),
    N_("Luxury:"),
    N_("Science:"),
    N_("Granary:"),
    N_("Change in:"),
    N_("Corruption:"),
    N_("Waste:"),
    N_("Pollution:")
  };

  char **output_intl_label = NULL;

  hbox = gtk_hbox_new(TRUE, 0);	/* to give the table padding inside the frame */

  table = gtk_table_new(NUM_INFO_FIELDS, 2, FALSE);
  gtk_table_set_row_spacing(GTK_TABLE(table), 2, 10);
  gtk_table_set_row_spacing(GTK_TABLE(table), 5, 10);
  gtk_table_set_row_spacing(GTK_TABLE(table), 7, 10);
  gtk_table_set_col_spacing(GTK_TABLE(table), 0, 5);
  gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 4);

  output_intl_label = intl_slist(NUM_INFO_FIELDS, output_label);

  for (i = 0; i < NUM_INFO_FIELDS; i++) {
    label = gtk_label_new(output_intl_label[i]);
    gtk_widget_set_name(label, "city label");	/* for font style? */
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, i, i + 1, GTK_FILL, 0,
		     0, 0);

    label = gtk_label_new("");
    info_label[i] = label;
    gtk_widget_set_name(label, "city label");	/* ditto */
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

    gtk_table_attach(GTK_TABLE(table), label, 1, 2, i, i + 1, GTK_FILL, 0,
		     0, 0);
  }

  gtk_widget_show_all(hbox);
  return hbox;
}

/****************************************************************
                  **** Overview page **** 
*****************************************************************/
static void create_and_append_overview_page(struct city_dialog *pdialog)
{
  int i;
  GtkWidget *halves, *hbox, *vbox, *page, *align;
  GtkWidget *frame, *table, *label, *scrolledwin;

  const char *improvement_title[] = { N_("City improvements"),
    N_("Upkeep")
  };

  char *tab_title = _("City _Overview");

  char **improvement_clist_title = NULL;

  page = gtk_vbox_new(FALSE, 1);

  label = gtk_label_new(tab_title);
  notebook_tab_accels[OVERVIEW_PAGE] =
      gtk_label_parse_uline(GTK_LABEL(label), tab_title);

  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  /* the left half gets: info, map, supported, present units */
  /* the right half gets: currently building , impr, sell  */

  halves = gtk_hbox_new(FALSE, 4);	/* left and right halves */
  gtk_box_pack_start(GTK_BOX(page), halves, TRUE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 2);	/* the left half */
  gtk_box_pack_start(GTK_BOX(halves), vbox, TRUE, TRUE, 0);

  hbox = gtk_hbox_new(FALSE, 0);	/* top of left half: info, map */
  gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 2);

  frame = gtk_frame_new(_("City info"));
  gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, TRUE, 4);

  table = create_city_info_table(pdialog->overview.info_label);
  gtk_container_add(GTK_CONTAINER(frame), table);

  frame = gtk_frame_new(_("City map"));
  gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, FALSE, 0);

  align = gtk_alignment_new(0.5, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(frame), align);

  pdialog->overview.map_canvas = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(align), pdialog->overview.map_canvas);
  gtk_widget_set_events(pdialog->overview.map_canvas,
			GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);

  pdialog->overview.map_canvas_pixmap = gtk_pixcomm_new(root_window,
							canvas_width,
							canvas_height);
  gtk_container_add(GTK_CONTAINER(pdialog->overview.map_canvas),
		    pdialog->overview.map_canvas_pixmap);

  /* present and supported units (overview page) */

  /* TODO: smarter size VALUE */
  MINI_NUM_UNITS = 225 / UNIT_TILE_WIDTH;

  pdialog->overview.present_unit_boxes =
      fc_malloc(MINI_NUM_UNITS * sizeof(GtkWidget *));
  pdialog->overview.supported_unit_boxes =
      fc_malloc(MINI_NUM_UNITS * sizeof(GtkWidget *));
  pdialog->overview.present_unit_pixmaps =
      fc_malloc(MINI_NUM_UNITS * sizeof(GtkWidget *));
  pdialog->overview.supported_unit_pixmaps =
      fc_malloc(MINI_NUM_UNITS * sizeof(GtkWidget *));
  pdialog->overview.present_unit_ids =
      fc_malloc(MINI_NUM_UNITS * sizeof(int));
  pdialog->overview.supported_unit_ids =
      fc_malloc(MINI_NUM_UNITS * sizeof(int));

  pdialog->overview.supported_units_frame = gtk_frame_new("");
  gtk_box_pack_start(GTK_BOX(vbox),
		     pdialog->overview.supported_units_frame, FALSE, TRUE,
		     0);

  pdialog->overview.present_units_frame = gtk_frame_new("");
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->overview.present_units_frame,
		     FALSE, TRUE, 0);


  /* supported mini units box */

  hbox = gtk_hbox_new(FALSE, 0);	/* for unit pics & arrow buttons */
  gtk_container_add(GTK_CONTAINER(pdialog->overview.supported_units_frame),
		    hbox);

  for (i = 0; i < MINI_NUM_UNITS; i++) {
    int unit_height = (is_isometric) ?
	UNIT_TILE_HEIGHT : UNIT_TILE_HEIGHT + UNIT_TILE_HEIGHT / 2;

    pdialog->overview.supported_unit_boxes[i] = gtk_event_box_new();
    gtk_widget_set_events(pdialog->overview.supported_unit_boxes[i],
			  GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_box_pack_start(GTK_BOX(hbox),
		       pdialog->overview.supported_unit_boxes[i], TRUE,
		       TRUE, 0);

    pdialog->overview.supported_unit_pixmaps[i] =
	gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH, unit_height);
    gtk_container_add(GTK_CONTAINER
		      (pdialog->overview.supported_unit_boxes[i]),
		      pdialog->overview.supported_unit_pixmaps[i]);

    pdialog->overview.supported_unit_ids[i] = -1;

    if (pdialog->pcity->owner != game.player_idx) {
      gtk_widget_set_sensitive(pdialog->overview.supported_unit_boxes[i],
			       FALSE);
    }
  }

  pdialog->overview.supported_unit_pos = 0;

  vbox = gtk_vbox_new(FALSE, 1);	/* contains arrow buttons */
  gtk_box_pack_end(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

  pdialog->overview.supported_unit_button[0] =
      gtk_button_new_with_label(_("<"));
  gtk_box_pack_start(GTK_BOX(vbox),
		     pdialog->overview.supported_unit_button[0], TRUE,
		     TRUE, 0);

  pdialog->overview.supported_unit_button[1] =
      gtk_button_new_with_label(_(">"));
  gtk_box_pack_start(GTK_BOX(vbox),
		     pdialog->overview.supported_unit_button[1], TRUE,
		     TRUE, 0);

  gtk_signal_connect(GTK_OBJECT
		     (pdialog->overview.supported_unit_button[0]),
		     "clicked",
		     GTK_SIGNAL_FUNC(supported_units_page_pos_callback),
		     pdialog);
  gtk_signal_connect(GTK_OBJECT
		     (pdialog->overview.supported_unit_button[1]),
		     "clicked",
		     GTK_SIGNAL_FUNC(supported_units_page_pos_callback),
		     pdialog);

  /* present mini units box */

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(pdialog->overview.present_units_frame),
		    hbox);


  for (i = 0; i < MINI_NUM_UNITS; i++) {
    pdialog->overview.present_unit_boxes[i] = gtk_event_box_new();
    gtk_widget_set_events(pdialog->overview.present_unit_boxes[i],
			  GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_box_pack_start(GTK_BOX(hbox),
		       pdialog->overview.present_unit_boxes[i], TRUE, TRUE,
		       0);

    pdialog->overview.present_unit_pixmaps[i] =
	gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
    gtk_container_add(GTK_CONTAINER
		      (pdialog->overview.present_unit_boxes[i]),
		      pdialog->overview.present_unit_pixmaps[i]);

    pdialog->overview.present_unit_ids[i] = -1;

    if (pdialog->pcity->owner != game.player_idx) {
      gtk_widget_set_sensitive(pdialog->overview.present_unit_boxes[i],
			       FALSE);
    }
  }
  pdialog->overview.present_unit_pos = 0;

  vbox = gtk_vbox_new(FALSE, 1);	/* contains arrow buttons */
  gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

  pdialog->overview.present_unit_button[0] =
      gtk_button_new_with_label(_("<"));
  gtk_box_pack_start(GTK_BOX(vbox),
		     pdialog->overview.present_unit_button[0], TRUE, TRUE,
		     0);

  pdialog->overview.present_unit_button[1] =
      gtk_button_new_with_label(_(">"));
  gtk_box_pack_start(GTK_BOX(vbox),
		     pdialog->overview.present_unit_button[1], TRUE, TRUE,
		     0);

  gtk_signal_connect(GTK_OBJECT(pdialog->overview.present_unit_button[0]),
		     "clicked",
		     GTK_SIGNAL_FUNC(present_units_page_pos_callback),
		     pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->overview.present_unit_button[1]),
		     "clicked",
		     GTK_SIGNAL_FUNC(present_units_page_pos_callback),
		     pdialog);

  /* start the right half of the page */

  vbox = gtk_vbox_new(FALSE, 0);	/* the right half */
  gtk_box_pack_start(GTK_BOX(halves), vbox, TRUE, TRUE, 0);

  /* stuff that's being currently built */

  /* The label is set in city_dialog_update_building() */
  pdialog->overview.currently_building_frame = gtk_frame_new("");
  gtk_box_pack_start(GTK_BOX(vbox),
		     pdialog->overview.currently_building_frame, FALSE,
		     FALSE, 0);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER
		    (pdialog->overview.currently_building_frame), hbox);

  pdialog->overview.progress_label = gtk_progress_bar_new();
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->overview.progress_label,
		     TRUE, TRUE, 0);
  gtk_progress_configure(GTK_PROGRESS(pdialog->overview.progress_label),
			 50, 0, 100);
  gtk_progress_set_show_text(GTK_PROGRESS
			     (pdialog->overview.progress_label), TRUE);
  gtk_progress_set_format_string(GTK_PROGRESS
				 (pdialog->overview.progress_label),
				 _("%d/%d %d turns"));

  pdialog->overview.buy_command =
      gtk_accelbutton_new(_("_Buy"), pdialog->accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->overview.buy_command,
		     TRUE, TRUE, 0);

  pdialog->overview.change_command =
      gtk_accelbutton_new(_("_Change"), pdialog->accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->overview.change_command,
		     TRUE, TRUE, 0);

  /* city improvements */

  scrolledwin = gtk_scrolled_window_new(NULL, NULL);
  gtk_box_pack_start(GTK_BOX(vbox), scrolledwin, TRUE, TRUE, 0);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwin),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  if (!improvement_clist_title) {
    improvement_clist_title = intl_slist(2, improvement_title);
  }
  pdialog->overview.improvement_list =
      gtk_clist_new_with_titles(2, improvement_clist_title);
  gtk_clist_column_titles_passive(GTK_CLIST
				  (pdialog->overview.improvement_list));
  gtk_clist_set_column_justification(GTK_CLIST
				     (pdialog->overview.improvement_list),
				     1, GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_auto_resize(GTK_CLIST
				   (pdialog->overview.improvement_list), 0,
				   TRUE);
  gtk_clist_set_column_auto_resize(GTK_CLIST
				   (pdialog->overview.improvement_list), 1,
				   TRUE);
  gtk_container_add(GTK_CONTAINER(scrolledwin),
		    pdialog->overview.improvement_list);
  gtk_clist_column_titles_show(GTK_CLIST
			       (pdialog->overview.improvement_list));

  gtk_signal_connect(GTK_OBJECT(pdialog->overview.improvement_list),
		     "select_row",
		     GTK_SIGNAL_FUNC(select_impr_list_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->overview.improvement_list),
		     "unselect_row",
		     GTK_SIGNAL_FUNC(select_impr_list_callback), pdialog);

  /* and the sell button */

  pdialog->overview.sell_command = gtk_button_new_with_label(_("Sell"));
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->overview.sell_command,
		     FALSE, FALSE, 0);

  gtk_signal_connect(GTK_OBJECT(pdialog->overview.sell_command), "clicked",
		     GTK_SIGNAL_FUNC(sell_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->overview.buy_command), "clicked",
		     GTK_SIGNAL_FUNC(buy_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->overview.change_command),
		     "clicked", GTK_SIGNAL_FUNC(change_callback), pdialog);

  /* in terms of structural flow, should be put these above? */

  gtk_signal_connect(GTK_OBJECT(pdialog->overview.map_canvas),
		     "expose_event",
		     GTK_SIGNAL_FUNC(city_map_canvas_expose), pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->overview.map_canvas),
		     "button_press_event",
		     GTK_SIGNAL_FUNC(button_down_citymap), NULL);

  gtk_widget_show_all(page);
}

/****************************************************************
                      **** Units page **** 
*****************************************************************/
static void create_and_append_units_page(struct city_dialog *pdialog)
{
  int i, num;
  GtkWidget *hbox, **hbox_row, *vbox, *page;
  GtkWidget *label;
  char *tab_title = _("_Units");

  page = gtk_vbox_new(FALSE, 0);

  label = gtk_label_new(tab_title);
  notebook_tab_accels[UNITS_PAGE] =
      gtk_label_parse_uline(GTK_LABEL(label), tab_title);

  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  if (is_isometric)
    MAX_UNIT_ROWS = (int) (100 / (UNIT_TILE_HEIGHT));
  else
    MAX_UNIT_ROWS = (int) (100 / (UNIT_TILE_HEIGHT + 6));

  NUM_UNITS_SHOWN = (int) (MAX_UNIT_ROWS * 500) / (UNIT_TILE_WIDTH);

  /* TODO: One problem. If we use these "intelligent" calculations which
   * provide us with suitable amount of units with different sizes of
   * window, we cannot resize dialog then any more. Solution? */

  hbox_row = fc_malloc(MAX_UNIT_ROWS * sizeof(GtkWidget *));

  pdialog->unit.supported_unit_boxes =
      fc_malloc(NUM_UNITS_SHOWN * sizeof(GtkWidget *));
  pdialog->unit.present_unit_boxes =
      fc_malloc(NUM_UNITS_SHOWN * sizeof(GtkWidget *));
  pdialog->unit.supported_unit_pixmaps =
      fc_malloc(NUM_UNITS_SHOWN * sizeof(GtkWidget *));
  pdialog->unit.present_unit_pixmaps =
      fc_malloc(NUM_UNITS_SHOWN * sizeof(GtkWidget *));
  pdialog->unit.supported_unit_ids =
      fc_malloc(NUM_UNITS_SHOWN * sizeof(int));
  pdialog->unit.present_unit_ids =
      fc_malloc(NUM_UNITS_SHOWN * sizeof(int));

  /* create the units' frames */

  vbox = gtk_vbox_new(TRUE, 0);
  gtk_box_pack_start(GTK_BOX(page), vbox, TRUE, TRUE, 0);

  pdialog->unit.supported_units_frame =
      gtk_frame_new(_("Supported units"));
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->unit.supported_units_frame,
		     TRUE, TRUE, 0);

  pdialog->unit.present_units_frame = gtk_frame_new(_("Units present"));
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->unit.present_units_frame,
		     TRUE, TRUE, 0);

  /* now fill the frames */

  /* supported units */

  hbox = gtk_hbox_new(FALSE, 0);	/* unit lists */
  gtk_container_add(GTK_CONTAINER(pdialog->unit.supported_units_frame),
		    hbox);

  vbox = gtk_vbox_new(TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

  for (i = 0; i < MAX_UNIT_ROWS; i++) {
    hbox_row[i] = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_row[i], TRUE, TRUE, 0);
  }

  for (num = 0, i = 0; i < NUM_UNITS_SHOWN; i++) {
    pdialog->unit.supported_unit_boxes[i] = gtk_event_box_new();
    gtk_widget_set_events(pdialog->unit.supported_unit_boxes[i],
			  GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_box_pack_start(GTK_BOX(hbox_row[num++]),
		       pdialog->unit.supported_unit_boxes[i], TRUE, TRUE,
		       0);

    if (num >= MAX_UNIT_ROWS)
      num = 0;

    pdialog->unit.supported_unit_pixmaps[i] =
	gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH,
			NORMAL_TILE_HEIGHT + NORMAL_TILE_HEIGHT / 2);
    gtk_container_add(GTK_CONTAINER(pdialog->unit.supported_unit_boxes[i]),
		      pdialog->unit.supported_unit_pixmaps[i]);
    pdialog->unit.supported_unit_ids[i] = -1;

    if (pdialog->pcity->owner != game.player_idx)
      gtk_widget_set_sensitive(pdialog->unit.supported_unit_boxes[i],
			       FALSE);
  }
  pdialog->unit.supported_unit_pos = 0;

  vbox = gtk_vbox_new(FALSE, 1);	/* prev/next buttons */
  gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

  pdialog->unit.supported_unit_button[0] =
      gtk_button_new_with_label(_("<"));
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->unit.supported_unit_button[0],
		     TRUE, TRUE, 0);

  pdialog->unit.supported_unit_button[1] =
      gtk_button_new_with_label(_(">"));
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->unit.supported_unit_button[1],
		     TRUE, TRUE, 0);

  gtk_signal_connect(GTK_OBJECT(pdialog->unit.supported_unit_button[0]),
		     "clicked",
		     GTK_SIGNAL_FUNC(supported_units_page_pos_callback),
		     pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->unit.supported_unit_button[1]),
		     "clicked",
		     GTK_SIGNAL_FUNC(supported_units_page_pos_callback),
		     pdialog);

  /* present units */

  hbox = gtk_hbox_new(FALSE, 0);	/* unit lists */
  gtk_container_add(GTK_CONTAINER(pdialog->unit.present_units_frame),
		    hbox);

  vbox = gtk_vbox_new(TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

  for (i = 0; i < MAX_UNIT_ROWS; i++) {
    hbox_row[i] = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_row[i], TRUE, TRUE, 0);
  }

  for (num = 0, i = 0; i < NUM_UNITS_SHOWN; i++) {
    pdialog->unit.present_unit_boxes[i] = gtk_event_box_new();
    gtk_widget_set_events(pdialog->unit.present_unit_boxes[i],
			  GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_box_pack_start(GTK_BOX(hbox_row[num++]),
		       pdialog->unit.present_unit_boxes[i], TRUE, TRUE, 0);

    if (num >= MAX_UNIT_ROWS)
      num = 0;


    pdialog->unit.present_unit_pixmaps[i] =
	gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
    gtk_container_add(GTK_CONTAINER(pdialog->unit.present_unit_boxes[i]),
		      pdialog->unit.present_unit_pixmaps[i]);
    pdialog->unit.present_unit_ids[i] = -1;

    if (pdialog->pcity->owner != game.player_idx)
      gtk_widget_set_sensitive(pdialog->unit.present_unit_boxes[i], FALSE);
  }
  pdialog->unit.present_unit_pos = 0;

  free(hbox_row);

  vbox = gtk_vbox_new(FALSE, 1);	/* prev/next buttons */
  gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

  pdialog->unit.present_unit_button[0] = gtk_button_new_with_label(_("<"));
  gtk_box_pack_start(GTK_BOX(vbox),
		     pdialog->unit.present_unit_button[0], TRUE, TRUE, 0);

  pdialog->unit.present_unit_button[1] = gtk_button_new_with_label(_(">"));
  gtk_box_pack_start(GTK_BOX(vbox),
		     pdialog->unit.present_unit_button[1], TRUE, TRUE, 0);

  gtk_signal_connect(GTK_OBJECT(pdialog->unit.present_unit_button[0]),
		     "clicked",
		     GTK_SIGNAL_FUNC(present_units_page_pos_callback),
		     pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->unit.present_unit_button[1]),
		     "clicked",
		     GTK_SIGNAL_FUNC(present_units_page_pos_callback),
		     pdialog);

  /* box for the buttons on the bottom */

  hbox = gtk_hbox_new(TRUE, 0);
  gtk_box_pack_start(GTK_BOX(page), hbox, FALSE, TRUE, 0);

  pdialog->unit.activate_command =
      gtk_accelbutton_new(_("Acti_vate present units"), pdialog->accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->unit.activate_command, FALSE,
		     TRUE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->unit.activate_command, GTK_CAN_DEFAULT);

  pdialog->unit.sentry_all_command =
      gtk_accelbutton_new(_("_Sentry idle units"), pdialog->accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->unit.sentry_all_command,
		     FALSE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->unit.sentry_all_command, GTK_CAN_DEFAULT);

  pdialog->unit.show_units_command =
      gtk_accelbutton_new(_("_List present units"), pdialog->accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->unit.show_units_command,
		     FALSE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->unit.show_units_command, GTK_CAN_DEFAULT);

  gtk_signal_connect(GTK_OBJECT(pdialog->unit.activate_command), "clicked",
		     GTK_SIGNAL_FUNC(activate_all_units_callback),
		     pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->unit.sentry_all_command),
		     "clicked", GTK_SIGNAL_FUNC(sentry_all_units_callback),
		     pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->unit.show_units_command),
		     "clicked", GTK_SIGNAL_FUNC(show_units_callback),
		     pdialog);

  gtk_widget_show_all(page);
}

/****************************************************************
                    **** Worklists Page **** 
*****************************************************************/
static void create_and_append_worklist_page(struct city_dialog *pdialog)
{
  char *tab_title = _("_Worklist");
  GtkWidget *label = gtk_label_new(tab_title);

  notebook_tab_accels[WORKLIST_PAGE] =
      gtk_label_parse_uline(GTK_LABEL(label), tab_title);

  pdialog->wl_editor =
      create_worklist_editor(&pdialog->pcity->worklist, pdialog->pcity,
			     (void *) pdialog, commit_city_worklist, NULL, 1);
  gtk_signal_connect(GTK_OBJECT(pdialog->shell), "key_press_event",
		     GTK_SIGNAL_FUNC(pdialog->wl_editor->keyboard_handler),
		     pdialog->wl_editor);
  gtk_widget_show(GTK_WIDGET(pdialog->wl_editor->shell));
  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook),
			   pdialog->wl_editor->shell, label);
}

/****************************************************************
                     **** Happiness Page **** 
*****************************************************************/
static void create_and_append_happiness_page(struct city_dialog *pdialog)
{
  GtkWidget *page, *vbox, *label, *table, *align;
  char *tab_title = _("_Happiness");

  page = gtk_hbox_new(FALSE, 0);

  label = gtk_label_new(tab_title);
  notebook_tab_accels[HAPPINESS_PAGE] =
      gtk_label_parse_uline(GTK_LABEL(label), tab_title);

  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  pdialog->happiness.widget = gtk_vbox_new(FALSE, 0);

  gtk_box_pack_start(GTK_BOX(page), pdialog->happiness.widget, TRUE, TRUE,
		     0);

  gtk_box_pack_start(GTK_BOX(pdialog->happiness.widget),
		     get_top_happiness_display(pdialog->pcity), TRUE, TRUE,
		     0);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page), vbox, FALSE, FALSE, 0);

  align = gtk_alignment_new(0.5, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(vbox), align);

  pdialog->happiness.map_canvas = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(align), pdialog->happiness.map_canvas);
  gtk_widget_set_events(pdialog->happiness.map_canvas,
			GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);

  pdialog->happiness.map_canvas_pixmap = gtk_pixcomm_new(root_window,
							 canvas_width,
							 canvas_height);
  gtk_container_add(GTK_CONTAINER(pdialog->happiness.map_canvas),
		    pdialog->happiness.map_canvas_pixmap);
  gtk_signal_connect(GTK_OBJECT(pdialog->happiness.map_canvas),
		     "expose_event",
		     GTK_SIGNAL_FUNC(city_map_canvas_expose), pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->happiness.map_canvas),
		     "button_press_event",
		     GTK_SIGNAL_FUNC(button_down_citymap), NULL);

  table = create_city_info_table(pdialog->happiness.info_label);
  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);

  gtk_widget_show_all(page);
}

/****************************************************************
            **** Citizen Management Agent (CMA) Page ****
*****************************************************************/
static void create_and_append_cma_page(struct city_dialog *pdialog)
{
  GtkWidget *page, *label;
  char *tab_title = _("CM_A");

  page = gtk_vbox_new(FALSE, 0);

  label = gtk_label_new(tab_title);
  notebook_tab_accels[CMA_PAGE] =
      gtk_label_parse_uline(GTK_LABEL(label), tab_title);

  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  pdialog->cma_editor = create_cma_dialog(pdialog->pcity, pdialog->accel);
  gtk_box_pack_start(GTK_BOX(page), pdialog->cma_editor->shell, TRUE, TRUE, 0);

  gtk_widget_show(page);
}

/****************************************************************
                       **** Trade Page **** 
*****************************************************************/
static void create_and_append_trade_page(struct city_dialog *pdialog)
{
  GtkWidget *page, *frame, *label;
  char *tab_title = _("_Trade Routes");

  page = gtk_hbox_new(TRUE, 0);

  label = gtk_label_new(tab_title);
  notebook_tab_accels[TRADE_PAGE] =
      gtk_label_parse_uline(GTK_LABEL(label), tab_title);

  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  frame = gtk_frame_new(_("Established trade routes"));

  pdialog->overview.tradelist = gtk_label_new("Uninitialized.");
  gtk_widget_set_name(pdialog->overview.tradelist, "city label");
  gtk_label_set_justify(GTK_LABEL(pdialog->overview.tradelist),
			GTK_JUSTIFY_LEFT);
  gtk_container_add(GTK_CONTAINER(frame), pdialog->overview.tradelist);
  gtk_box_pack_start(GTK_BOX(page), frame, TRUE, TRUE, 0);

  gtk_widget_show_all(page);
}

/****************************************************************
                    **** Misc. Settings Page **** 
*****************************************************************/
static void create_and_append_misc_page(struct city_dialog *pdialog)
{
  int i;
  int per_row = 2;
  GtkWidget *hbox, *vbox, *page, *table, *frame, *label;
  GSList *group = NULL;

  char *tab_title = _("_Misc. Settings");

  const char *new_citizens_label[] = {
    N_("Entertainers"),
    N_("Scientists"),
    N_("Taxmen")
  };

  const char *city_opts_label[NUM_CITY_OPTS] = {
    N_("Land units"),
    N_("Sea units"),
    N_("Helicopters"),
    N_("Air units"),
    N_("Disband if build settler at size 1")
  };

  const char *misc_whichtab_label[NUM_PAGES] = {
    N_("City Overview page"),
    N_("Units page"),
    N_("Worklist page"),
    N_("Happiness page"),
    N_("CMA page"),
    N_("Trade Routes page"),
    N_("This Misc. Settings page"),
    N_("Last active page")
  };

  char **new_citizens_intl_label = NULL;
  char **city_opts_intl_label = NULL;
  char **misc_whichtab_intl_label = NULL;

  /* initialize signal_blocker */
  pdialog->misc.block_signal = 0;

  page = gtk_hbox_new(TRUE, 0);

  label = gtk_label_new(tab_title);
  notebook_tab_accels[MISC_PAGE] =
      gtk_label_parse_uline(GTK_LABEL(label), tab_title);

  gtk_notebook_append_page(GTK_NOTEBOOK(pdialog->notebook), page, label);

  frame = gtk_frame_new(_("City options"));
  gtk_box_pack_start(GTK_BOX(page), frame, FALSE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  hbox = gtk_hbox_new(TRUE, 0);	/* new citizens and rename */
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

  frame = gtk_frame_new(_("Auto attack vs"));
  gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

  /* auto-attack table */

  city_opts_intl_label = intl_slist(NUM_CITY_OPTS, city_opts_label);

  table = gtk_table_new(2, 2, FALSE);
  gtk_container_add(GTK_CONTAINER(frame), table);

  for(i = 0; i < NUM_CITY_OPTS - 1; i++){
    pdialog->misc.city_opts[i] = 
                      gtk_check_button_new_with_label(city_opts_intl_label[i]);
    gtk_table_attach(GTK_TABLE(table), pdialog->misc.city_opts[i],
		     i%per_row, i%per_row+1, i/per_row, i/per_row+1,
                     GTK_FILL, 0, 0, 0);

    gtk_signal_connect(GTK_OBJECT(pdialog->misc.city_opts[i]), "toggled",
                       GTK_SIGNAL_FUNC(cityopt_callback), pdialog);

  }

  /* the disband-if-size-1 button */

  pdialog->misc.city_opts[NUM_CITY_OPTS - 1] =
      gtk_check_button_new_with_label(city_opts_intl_label[NUM_CITY_OPTS - 1]);
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->misc.city_opts[NUM_CITY_OPTS - 1], 
                     FALSE, FALSE, 0);

  gtk_signal_connect(GTK_OBJECT(pdialog->misc.city_opts[NUM_CITY_OPTS - 1]), 
                     "toggled", GTK_SIGNAL_FUNC(cityopt_callback), pdialog);

  frame = gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 4);

  /* now we go back and fill the hbox with new_citizens radio and rename */

  frame = gtk_frame_new(_("New citizens are"));
  gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 0);	/* new_citizens radio box */
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  new_citizens_intl_label = intl_slist(3, new_citizens_label);
  for (i = 0; i < 3; i++) {
    pdialog->misc.new_citizens_radio[i] =
	gtk_radio_button_new_with_label(group, new_citizens_intl_label[i]);
    group =
	gtk_radio_button_group(GTK_RADIO_BUTTON
			       (pdialog->misc.new_citizens_radio[i]));
    gtk_box_pack_start(GTK_BOX(vbox), pdialog->misc.new_citizens_radio[i],
		       FALSE, FALSE, 0);
    gtk_signal_connect(GTK_OBJECT(pdialog->misc.new_citizens_radio[i]),
		       "toggled", GTK_SIGNAL_FUNC(cityopt_callback),
		       pdialog);
  }

  frame = gtk_frame_new(_("City name"));
  gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 0);	/* rename button box */
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  pdialog->misc.rename_command = gtk_button_new_with_label(_("Rename"));
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->misc.rename_command, FALSE,
		     TRUE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->misc.rename_command, GTK_CAN_DEFAULT);

  gtk_signal_connect(GTK_OBJECT(pdialog->misc.rename_command), "clicked",
		     GTK_SIGNAL_FUNC(rename_callback), pdialog);

  gtk_widget_set_sensitive(pdialog->misc.rename_command,
                           can_client_issue_orders());
  /* next is the next-time-open radio group in the right column */

  frame = gtk_frame_new(_("Next time open"));
  gtk_box_pack_start(GTK_BOX(page), frame, FALSE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  group = NULL;			/* reinitialize group for next radio set */

  misc_whichtab_intl_label = intl_slist(NUM_PAGES, misc_whichtab_label);
  for (i = 0; i < NUM_PAGES; i++) {
    pdialog->misc.whichtab_radio[i] =
	gtk_radio_button_new_with_label(group,
					misc_whichtab_intl_label[i]);
    group =
	gtk_radio_button_group(GTK_RADIO_BUTTON
			       (pdialog->misc.whichtab_radio[i]));
    gtk_box_pack_start(GTK_BOX(vbox), pdialog->misc.whichtab_radio[i],
		       FALSE, FALSE, 0);
    gtk_signal_connect(GTK_OBJECT(pdialog->misc.whichtab_radio[i]),
		       "toggled", GTK_SIGNAL_FUNC(misc_whichtab_callback),
		       GINT_TO_POINTER(i));
  }

  /* we choose which page to popup by default */

  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
			      (pdialog->
			       misc.whichtab_radio[new_dialog_def_page]),
			      TRUE);

  set_cityopt_values(pdialog);

  gtk_widget_show_all(page);

  if (new_dialog_def_page == (NUM_PAGES - 1)) {
    gtk_notebook_set_page(GTK_NOTEBOOK(pdialog->notebook), last_page);
  } else {
    gtk_notebook_set_page(GTK_NOTEBOOK(pdialog->notebook),
			  new_dialog_def_page);
  }

}

/****************************************************************
...
*****************************************************************/
static struct city_dialog *create_city_dialog(struct city *pcity,
					      bool make_modal)
{
  struct city_dialog *pdialog;

  GtkWidget *close_command;
  GtkWidget *hbox, *vbox, *ebox;

  if (!city_dialogs_have_been_initialised)
    initialize_city_dialogs();

  pdialog = fc_malloc(sizeof(struct city_dialog));
  pdialog->pcity = pcity;
  pdialog->change_shell = NULL;
  pdialog->buy_shell = NULL;
  pdialog->sell_shell = NULL;
  pdialog->rename_shell = NULL;
  pdialog->happiness.map_canvas = NULL;         /* make sure NULL if spy */
  pdialog->happiness.map_canvas_pixmap = NULL;  /* ditto */


  pdialog->shell = gtk_dialog_new();
  pdialog->accel = gtk_accel_group_new();
  gtk_signal_connect(GTK_OBJECT(pdialog->shell), "delete_event",
		     GTK_SIGNAL_FUNC(city_dialog_delete_callback),
		     (gpointer) pdialog);
  gtk_window_set_position(GTK_WINDOW(pdialog->shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(pdialog->accel, GTK_OBJECT(pdialog->shell));
  gtk_widget_set_name(pdialog->shell, "Freeciv");

  gtk_window_set_title(GTK_WINDOW(pdialog->shell), pcity->name);

  gtk_widget_realize(pdialog->shell);

  if (!icon_bitmap) {
    icon_bitmap = gdk_bitmap_create_from_data(root_window, cityicon_bits,
					      cityicon_width,
					      cityicon_height);
  }
  gdk_window_set_icon(pdialog->shell->window, NULL, icon_bitmap,
		      icon_bitmap);

  gtk_window_set_title(GTK_WINDOW(pdialog->shell), _("City of %s"));

  /* Set old size. The size isn't saved in any way. */
  if (citydialog_width && citydialog_height) {
    gtk_window_set_default_size(GTK_WINDOW(pdialog->shell),
				citydialog_width, citydialog_height);
  }

  pdialog->title_frame = gtk_frame_new(_("City of %s"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->vbox),
		     pdialog->title_frame, TRUE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(pdialog->title_frame), vbox);

  /**** Citizens bar here ****/

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

  ebox = gtk_event_box_new();
  gtk_widget_set_events(ebox, GDK_BUTTON_PRESS_MASK);
  gtk_box_pack_start(GTK_BOX(hbox), ebox, FALSE, FALSE, 0);
  pdialog->citizen_pixmap =
      gtk_pixcomm_new(root_window, SMALL_TILE_WIDTH * NUM_CITIZENS_SHOWN,
		      SMALL_TILE_HEIGHT);
  gtk_container_add(GTK_CONTAINER(ebox), pdialog->citizen_pixmap);
  gtk_signal_connect(GTK_OBJECT(ebox), "button_press_event",
		     GTK_SIGNAL_FUNC(citizens_callback), pdialog);

  /**** -Start of Notebook- ****/

  pdialog->notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(pdialog->notebook),
			   GTK_POS_BOTTOM);
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->notebook, TRUE, TRUE, 0);

  create_and_append_overview_page(pdialog);
  create_and_append_units_page(pdialog);

  /* only create these tabs if not a spy */
  if (pcity->owner == game.player_idx) {

    create_and_append_worklist_page(pdialog);

    create_and_append_happiness_page(pdialog);

    create_and_append_cma_page(pdialog);

  }

  create_and_append_trade_page(pdialog);

  if (pcity->owner == game.player_idx) {
    create_and_append_misc_page(pdialog);
  } else {
    gtk_notebook_set_page(GTK_NOTEBOOK(pdialog->notebook), OVERVIEW_PAGE);
  }

  gtk_signal_connect(GTK_OBJECT(pdialog->notebook), "switch-page",
		     GTK_SIGNAL_FUNC(switch_page_callback), pdialog);

  /**** End of Notebook ****/

  /* bottom buttons */

  close_command = gtk_button_new_with_label(_("Close dialog"));
  GTK_WIDGET_SET_FLAGS(close_command, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(close_command);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
		     close_command, TRUE, TRUE, 0);

  pdialog->prev_command =
      gtk_accelbutton_new(_("_Prev city"), pdialog->accel);
  GTK_WIDGET_SET_FLAGS(pdialog->prev_command, GTK_CAN_DEFAULT);
  if (pcity->owner != game.player_idx)
    gtk_widget_set_sensitive(pdialog->prev_command, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
		     pdialog->prev_command, TRUE, TRUE, 0);

  pdialog->next_command =
      gtk_accelbutton_new(_("_Next city"), pdialog->accel);
  GTK_WIDGET_SET_FLAGS(pdialog->next_command, GTK_CAN_DEFAULT);
  if (pcity->owner != game.player_idx)
    gtk_widget_set_sensitive(pdialog->next_command, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
		     pdialog->next_command, TRUE, TRUE, 0);

  gtk_signal_connect(GTK_OBJECT(close_command), "clicked",
		     GTK_SIGNAL_FUNC(close_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->prev_command), "clicked",
		     GTK_SIGNAL_FUNC(switch_city_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->next_command), "clicked",
		     GTK_SIGNAL_FUNC(switch_city_callback), pdialog);

  gtk_widget_add_accelerator(close_command, "clicked",
			     pdialog->accel, GDK_Return, 0, 0);
  gtk_widget_add_accelerator(close_command, "clicked",
			     pdialog->accel, GDK_Escape, 0, 0);

  /* some other things we gotta do */

  gtk_signal_connect(GTK_OBJECT(pdialog->shell), "key_press_event",
		     GTK_SIGNAL_FUNC(keyboard_handler), pdialog);

  pdialog->map_canvas_store = gdk_pixmap_new(root_window, canvas_width,
					     canvas_height, -1);

  dialog_list_insert(&dialog_list, pdialog);

  impr_type_iterate(i) {
    pdialog->last_improvlist_seen[i] = 0;
  } impr_type_iterate_end;

  refresh_city_dialog(pdialog->pcity);

  /* need to do this every time a new dialog is opened. */
  city_dialog_update_prev_next();

  if (make_modal)
    gtk_widget_set_sensitive(top_vbox, FALSE);

  pdialog->is_modal = make_modal;

  gtk_widget_show_all(GTK_DIALOG(pdialog->shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(pdialog->shell)->action_area);

  return pdialog;
}

/*********** Functions to update parts of the dialog ************/
/****************************************************************
...
*****************************************************************/
static void city_dialog_update_title(struct city_dialog *pdialog)
{
  char buf[512];
  char *now;

  my_snprintf(buf, sizeof(buf), _("%s - %s citizens"),
	      pdialog->pcity->name,
	      population_to_text(city_population(pdialog->pcity)));

  if (city_unhappy(pdialog->pcity)) {
    mystrlcat(buf, _(" - DISORDER"), sizeof(buf));
  } else if (city_celebrating(pdialog->pcity)) {
    mystrlcat(buf, _(" - celebrating"), sizeof(buf));
  } else if (city_happy(pdialog->pcity)) {
    mystrlcat(buf, _(" - happy"), sizeof(buf));
  }

  now = GTK_WINDOW(pdialog->shell)->title;
  if (strcmp(now, buf) != 0) {
    gtk_frame_set_label(GTK_FRAME(pdialog->title_frame), buf);
    gtk_window_set_title(GTK_WINDOW(pdialog->shell), buf);
  }
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_citizens(struct city_dialog *pdialog)
{
  int i;
  struct city *pcity = pdialog->pcity;
  int width;
  struct citizen_type citizens[MAX_CITY_SIZE];

  /* If there is not enough space we stack the icons. We draw from left to */
  /* right. width is how far we go to the right for each drawn pixmap. The */
  /* last icon is always drawn in full, and so has reserved                */
  /* SMALL_TILE_WIDTH pixels.                                              */

  if (pcity->size > 1) {
    width = MIN(SMALL_TILE_WIDTH,
		((NUM_CITIZENS_SHOWN - 1) * SMALL_TILE_WIDTH) /
		(pcity->size - 1));
  } else {
    width = SMALL_TILE_WIDTH;
  }
  pdialog->cwidth = width;

  gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->citizen_pixmap), TRUE);

  get_city_citizen_types(pcity, 4, citizens);

  for (i = 0; i < pcity->size; i++) {
    gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmap),
		       get_citizen_sprite(citizens[i], i, pcity),
		       i * width, 0, TRUE);
  }

/*  gtk_widget_set_sensitive(pdialog->citizen_pixmap,*/
/*                           !cma_is_city_under_agent(pcity, NULL));*/
}
/****************************************************************
...
*****************************************************************/
static void city_dialog_update_information(GtkWidget **info_label,
                                           struct city_dialog *pdialog)
{
  int i, style;
  char buf[NUM_INFO_FIELDS][512];
  struct city *pcity = pdialog->pcity;
  int granaryturns;
  enum { FOOD, SHIELD, TRADE, GOLD, LUXURY, SCIENCE, 
	 GRANARY, GROWTH, CORRUPTION, WASTE, POLLUTION 
  };

  /* fill the buffers with the necessary info */

  my_snprintf(buf[FOOD], sizeof(buf[FOOD]), "%2d (%+2d)",
	      pcity->food_prod, pcity->food_surplus);
  my_snprintf(buf[SHIELD], sizeof(buf[SHIELD]), "%2d (%+2d)",
	      pcity->shield_prod + pcity->shield_waste,
	      pcity->shield_surplus);
  my_snprintf(buf[TRADE], sizeof(buf[TRADE]), "%2d (%+2d)",
	      pcity->trade_prod + pcity->corruption, pcity->trade_prod);
  my_snprintf(buf[GOLD], sizeof(buf[GOLD]), "%2d (%+2d)",
	      pcity->tax_total, city_gold_surplus(pcity, pcity->tax_total));
  my_snprintf(buf[LUXURY], sizeof(buf[LUXURY]), "%2d      ",
	      pcity->luxury_total);

  my_snprintf(buf[SCIENCE], sizeof(buf[SCIENCE]), "%2d",
	      pcity->science_total);

  my_snprintf(buf[GRANARY], sizeof(buf[GRANARY]), "%d/%-d",
	      pcity->food_stock, city_granary_size(pcity->size));
	
  granaryturns = city_turns_to_grow(pcity);
  if (granaryturns == 0) {
    my_snprintf(buf[GROWTH], sizeof(buf[GROWTH]), _("blocked"));
  } else if (granaryturns == FC_INFINITY) {
    my_snprintf(buf[GROWTH], sizeof(buf[GROWTH]), _("never"));
  } else {
    /* A negative value means we'll have famine in that many turns.
       But that's handled down below. */
    my_snprintf(buf[GROWTH], sizeof(buf[GROWTH]),
		PL_("%d turn", "%d turns", abs(granaryturns)),
		abs(granaryturns));
  }

  my_snprintf(buf[CORRUPTION], sizeof(buf[CORRUPTION]), "%2d",
	      pcity->corruption);
  my_snprintf(buf[WASTE], sizeof(buf[WASTE]), "%2d",
	      pcity->shield_waste);
  my_snprintf(buf[POLLUTION], sizeof(buf[POLLUTION]), "%2d",
	      pcity->pollution);

  /* stick 'em in the labels */

  for (i = 0; i < NUM_INFO_FIELDS; i++) {
    gtk_set_label(info_label[i], buf[i]);
  }

  /* 
   * Special style stuff for granary, growth and pollution below. The
   * "4" below is arbitrary. 3 turns should be enough of a warning.
   */
  style = (granaryturns > -4 && granaryturns < 0) ? RED : NORMAL;
  gtk_widget_modify_style(info_label[GRANARY], info_label_style[style]);

  style = (granaryturns == 0 || pcity->food_surplus < 0) ? RED : NORMAL;
  gtk_widget_modify_style(info_label[GROWTH], info_label_style[style]);

  /* someone could add the info_label_style[ORANGE]
   * style for better granularity here */

  style = (pcity->pollution >= 10) ? RED : NORMAL;
  gtk_widget_modify_style(info_label[POLLUTION], info_label_style[style]);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_map(struct city_dialog *pdialog)
{
  struct canvas store = {pdialog->map_canvas_store};

  city_dialog_redraw_map(pdialog->pcity, &store);

  /* draw to real window */
  draw_map_canvas(pdialog);

  if(cma_is_city_under_agent(pdialog->pcity, NULL)) {
    gtk_widget_set_sensitive(pdialog->overview.map_canvas, FALSE);
    if (pdialog->happiness.map_canvas) {
      gtk_widget_set_sensitive(pdialog->happiness.map_canvas, FALSE);
    }
  } else {
    gtk_widget_set_sensitive(pdialog->overview.map_canvas, TRUE);
    if (pdialog->happiness.map_canvas) {
      gtk_widget_set_sensitive(pdialog->happiness.map_canvas, TRUE);
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_building(struct city_dialog *pdialog)
{
  char buf[32], buf2[200];
  const char *descr;
  struct city *pcity = pdialog->pcity;
  gfloat pct;
  int cost;

  gtk_widget_set_sensitive(pdialog->overview.buy_command,
			   city_can_buy(pcity));
  gtk_widget_set_sensitive(pdialog->overview.sell_command,
			   can_client_issue_orders() && !pcity->did_sell);
			
  get_city_dialog_production(pcity, buf, sizeof(buf));

  if (pcity->is_building_unit) {
    cost = unit_build_shield_cost(pcity->currently_building);
    descr = get_unit_type(pcity->currently_building)->name;
  } else {
    cost = impr_build_shield_cost(pcity->currently_building);;
    descr = get_impr_name_ex(pcity, pcity->currently_building);
  }

  if (cost > 0) {
    pct = (gfloat) pcity->shield_stock / (gfloat) cost;
    pct = CLAMP(pct, 0.0, 1.0);
  } else {
    pct = 1.0;
  }
  
  my_snprintf(buf2, sizeof(buf2), "%s%s", descr,
	      worklist_is_empty(&pcity->worklist) ? "" : _(" (worklist)"));
  gtk_frame_set_label(GTK_FRAME
		      (pdialog->overview.currently_building_frame), buf2);
  gtk_progress_set_percentage(GTK_PROGRESS
			      (pdialog->overview.progress_label), pct);
  gtk_progress_set_format_string(GTK_PROGRESS
				 (pdialog->overview.progress_label), buf);

}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_improvement_list(struct city_dialog
						*pdialog)
{
  int changed, total, item, cids_used;
  cid cids[U_LAST + B_LAST];
  struct item items[U_LAST + B_LAST];
  char buf[100];

  /* Test if the list improvements of pcity has changed */
  changed = 0;
  impr_type_iterate(i) {
    if (pdialog->pcity->improvements[i] !=
	pdialog->last_improvlist_seen[i]) {
      changed = 1;
      break;
    }
  } impr_type_iterate_end;

  if (!changed) {
    gtk_widget_set_sensitive(pdialog->overview.sell_command, FALSE);
    return;
  }

  /* Update pdialog->last_improvlist_seen */
  impr_type_iterate(i) {
    pdialog->last_improvlist_seen[i] = pdialog->pcity->improvements[i];
  } impr_type_iterate_end;

  cids_used = collect_cids5(cids, pdialog->pcity);
  name_and_sort_items(cids, cids_used, items, FALSE, pdialog->pcity);

  gtk_clist_freeze(GTK_CLIST(pdialog->overview.improvement_list));
  gtk_clist_clear(GTK_CLIST(pdialog->overview.improvement_list));

  total = 0;
  for (item = 0; item < cids_used; item++) {
    char *strings[2];
    int row, id = cid_id(items[item].cid);

    strings[0] = items[item].descr;
    strings[1] = buf;

    /* This takes effects (like Adam Smith's) into account. */
    my_snprintf(buf, sizeof(buf), "%d",
		improvement_upkeep(pdialog->pcity, id));

    row = gtk_clist_append(GTK_CLIST(pdialog->overview.improvement_list),
			   strings);
    gtk_clist_set_row_data(GTK_CLIST(pdialog->overview.improvement_list),
			   row, GINT_TO_POINTER(id));

    total += improvement_upkeep(pdialog->pcity, id);
  }
  gtk_clist_thaw(GTK_CLIST(pdialog->overview.improvement_list));

  my_snprintf(buf, sizeof(buf), _("Upkeep (Total: %d)"), total);
  gtk_clist_set_column_title(GTK_CLIST(pdialog->overview.improvement_list),
			     1, buf);

  gtk_set_label(GTK_BUTTON(pdialog->overview.sell_command)->child,
		_("Sell"));
  gtk_widget_set_sensitive(pdialog->overview.sell_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_supported_units(struct city_dialog *pdialog)
{
  int i, j;
  struct unit_list *plist;
  int size, mini_size;
  char buf[30];

  if (pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_supported);
  } else {
    plist = &(pdialog->pcity->units_supported);
  }

  size = (plist->list.nelements - 1) / NUM_UNITS_SHOWN;
  size = MAX(0, size);
  if (size == 0 || pdialog->unit.supported_unit_pos > size) {
    pdialog->unit.supported_unit_pos = 0;
  }

  gtk_widget_set_sensitive(pdialog->unit.supported_unit_button[0],
			   (pdialog->unit.supported_unit_pos > 0));
  gtk_widget_set_sensitive(pdialog->unit.supported_unit_button[1],
			   (pdialog->unit.supported_unit_pos < size));

  mini_size = (plist->list.nelements - 1) / MINI_NUM_UNITS;
  mini_size = MAX(0, mini_size);
  if (mini_size == 0 || pdialog->overview.supported_unit_pos > mini_size) {
    pdialog->overview.supported_unit_pos = 0;
  }

  gtk_widget_set_sensitive(pdialog->overview.supported_unit_button[0],
			   (pdialog->overview.supported_unit_pos > 0));
  gtk_widget_set_sensitive(pdialog->overview.supported_unit_button[1],
			   (pdialog->overview.supported_unit_pos <
			    mini_size));

  /* mini */

  i = 0; /* number of displayed units */
  j = 0; /* index into list */
  unit_list_iterate(*plist, punit) {
    if (j++ < pdialog->overview.supported_unit_pos * MINI_NUM_UNITS) {
      continue;
    }
    if (i >= MINI_NUM_UNITS) {
      break;
    }

    gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->
				  overview.supported_unit_pixmaps[i]),
		      FALSE);

    put_unit_gpixmap(punit,
		     GTK_PIXCOMM(pdialog->
				 overview.supported_unit_pixmaps[i]));
    put_unit_gpixmap_city_overlays(punit,
				   GTK_PIXCOMM(pdialog->
					       overview.supported_unit_pixmaps
					       [i]));

    gtk_pixcomm_changed(GTK_PIXCOMM
			(pdialog->overview.supported_unit_pixmaps[i]));

    pdialog->overview.supported_unit_ids[i] = punit->id;

    gtk_signal_handlers_destroy(GTK_OBJECT
				(pdialog->
				 overview.supported_unit_boxes[i]));
    gtk_signal_connect(GTK_OBJECT
		       (pdialog->overview.supported_unit_boxes[i]),
		       "button_press_event",
		       GTK_SIGNAL_FUNC(supported_unit_callback),
		       GINT_TO_POINTER(punit->id));
    gtk_signal_connect(GTK_OBJECT
		       (pdialog->overview.supported_unit_boxes[i]),
		       "button_release_event",
		       GTK_SIGNAL_FUNC(present_unit_middle_callback),
		       GINT_TO_POINTER(punit->id));
    gtk_widget_set_sensitive(pdialog->overview.supported_unit_boxes[i],
			     TRUE);
    i++;
  } unit_list_iterate_end;

  /* Disable any empty slots */
  for (; i < MINI_NUM_UNITS; i++) {
    gtk_pixcomm_clear(GTK_PIXCOMM
		      (pdialog->overview.supported_unit_pixmaps[i]), TRUE);
    pdialog->overview.supported_unit_ids[i] = 0;
    gtk_widget_set_sensitive(pdialog->overview.supported_unit_boxes[i],
			     FALSE);
  }

  /* normal */

  i = 0; /* number of displayed units */
  j = 0; /* index into list */
  unit_list_iterate(*plist, punit) {
    if (j++ < pdialog->overview.supported_unit_pos * NUM_UNITS_SHOWN) {
      continue;
    }
    if (i >= NUM_UNITS_SHOWN) {
      break;
    }

    gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->unit.supported_unit_pixmaps[i]),
		      FALSE);

    put_unit_gpixmap(punit,
		     GTK_PIXCOMM(pdialog->unit.supported_unit_pixmaps[i]));
    put_unit_gpixmap_city_overlays(punit,
				   GTK_PIXCOMM(pdialog->
					       unit.supported_unit_pixmaps
					       [i]));

    gtk_pixcomm_changed(GTK_PIXCOMM
			(pdialog->unit.supported_unit_pixmaps[i]));

    pdialog->unit.supported_unit_ids[i] = punit->id;

    gtk_signal_handlers_destroy(GTK_OBJECT
				(pdialog->unit.supported_unit_boxes[i]));
    gtk_signal_connect(GTK_OBJECT(pdialog->unit.supported_unit_boxes[i]),
		       "button_press_event",
		       GTK_SIGNAL_FUNC(supported_unit_callback),
		       GINT_TO_POINTER(punit->id));
    gtk_signal_connect(GTK_OBJECT(pdialog->unit.supported_unit_boxes[i]),
		       "button_release_event",
		       GTK_SIGNAL_FUNC(supported_unit_middle_callback),
		       GINT_TO_POINTER(punit->id));
    gtk_widget_set_sensitive(pdialog->unit.supported_unit_boxes[i], TRUE);
    i++;
  } unit_list_iterate_end;

  for (; i < NUM_UNITS_SHOWN; i++) {
    gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->unit.supported_unit_pixmaps[i]),
		      TRUE);
    pdialog->unit.supported_unit_ids[i] = 0;
    gtk_widget_set_sensitive(pdialog->unit.supported_unit_boxes[i], FALSE);
  }

  my_snprintf(buf, sizeof(buf), _("Supported units (%d)"),
	      num_supported_units_in_city(pdialog->pcity));
  gtk_frame_set_label(GTK_FRAME(pdialog->overview.supported_units_frame),
		      buf);
  gtk_frame_set_label(GTK_FRAME(pdialog->unit.supported_units_frame), buf);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_present_units(struct city_dialog *pdialog)
{
  int i, j;
  struct unit_list *plist;
  int size, mini_size;
  char buf[30];

  if (pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_present);
  } else {
    plist = &(pdialog->pcity->tile->units);
  }

  size = (plist->list.nelements - 1) / NUM_UNITS_SHOWN;
  size = MAX(0, size);
  if (size == 0 || pdialog->unit.present_unit_pos > size) {
    pdialog->unit.present_unit_pos = 0;
  }

  gtk_widget_set_sensitive(pdialog->unit.present_unit_button[0],
			   (pdialog->unit.present_unit_pos > 0));
  gtk_widget_set_sensitive(pdialog->unit.present_unit_button[1],
			   (pdialog->unit.present_unit_pos < size));

  mini_size = (plist->list.nelements - 1) / MINI_NUM_UNITS;
  mini_size = MAX(mini_size, 0);
  if (mini_size == 0 || pdialog->overview.present_unit_pos > mini_size) {
    pdialog->overview.present_unit_pos = 0;
  }

  gtk_widget_set_sensitive(pdialog->overview.present_unit_button[0],
			   pdialog->overview.present_unit_pos > 0);
  gtk_widget_set_sensitive(pdialog->overview.present_unit_button[1],
			   pdialog->overview.present_unit_pos < mini_size);

  /* mini */

  i = 0; /* number of displayed units */
  j = 0; /* index into list */
  unit_list_iterate(*plist, punit) {
    if (j++ < pdialog->overview.present_unit_pos * MINI_NUM_UNITS) {
      continue;
    }
    if (i >= MINI_NUM_UNITS) {
      break;
    }

    gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->overview.present_unit_pixmaps[i]),
		      FALSE);

    put_unit_gpixmap(punit,
		     GTK_PIXCOMM(pdialog->
				 overview.present_unit_pixmaps[i]));

    gtk_pixcomm_changed(GTK_PIXCOMM
			(pdialog->overview.present_unit_pixmaps[i]));

    pdialog->overview.present_unit_ids[i] = punit->id;

    gtk_signal_handlers_destroy(GTK_OBJECT
				(pdialog->overview.present_unit_boxes[i]));
    gtk_signal_connect(GTK_OBJECT
		       (pdialog->overview.present_unit_boxes[i]),
		       "button_press_event",
		       GTK_SIGNAL_FUNC(present_unit_callback),
		       GINT_TO_POINTER(punit->id));
    gtk_signal_connect(GTK_OBJECT
		       (pdialog->overview.present_unit_boxes[i]),
		       "button_release_event",
		       GTK_SIGNAL_FUNC(present_unit_middle_callback),
		       GINT_TO_POINTER(punit->id));
    gtk_widget_set_sensitive(pdialog->overview.present_unit_boxes[i],
			     TRUE);
    i++;
  } unit_list_iterate_end;

  for (; i < MINI_NUM_UNITS; i++) {
    gtk_pixcomm_clear(GTK_PIXCOMM
		      (pdialog->overview.present_unit_pixmaps[i]), TRUE);
    pdialog->overview.present_unit_ids[i] = 0;
    gtk_widget_set_sensitive(pdialog->overview.present_unit_boxes[i],
			     FALSE);
  }

  /* normal */

  i = 0; /* number of displayed units */
  j = 0; /* index into list */
  unit_list_iterate(*plist, punit) {
    if (j++ < pdialog->overview.present_unit_pos * NUM_UNITS_SHOWN) {
      continue;
    }
    if (i >= NUM_UNITS_SHOWN) {
      break;
    }

    gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->unit.present_unit_pixmaps[i]),
		      FALSE);

    put_unit_gpixmap(punit,
		     GTK_PIXCOMM(pdialog->unit.present_unit_pixmaps[i]));

    gtk_pixcomm_changed(GTK_PIXCOMM
			(pdialog->unit.present_unit_pixmaps[i]));

    pdialog->unit.present_unit_ids[i] = punit->id;

    gtk_signal_handlers_destroy(GTK_OBJECT
				(pdialog->unit.present_unit_boxes[i]));
    gtk_signal_connect(GTK_OBJECT(pdialog->unit.present_unit_boxes[i]),
		       "button_press_event",
		       GTK_SIGNAL_FUNC(present_unit_callback),
		       GINT_TO_POINTER(punit->id));
    gtk_signal_connect(GTK_OBJECT(pdialog->unit.present_unit_boxes[i]),
		       "button_release_event",
		       GTK_SIGNAL_FUNC(present_unit_middle_callback),
		       GINT_TO_POINTER(punit->id));
    gtk_widget_set_sensitive(pdialog->unit.present_unit_boxes[i], TRUE);
    i++;
  } unit_list_iterate_end;

  for (; i < NUM_UNITS_SHOWN; i++) {
    gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->unit.present_unit_pixmaps[i]),
		      TRUE);
    pdialog->unit.present_unit_ids[i] = 0;
    gtk_widget_set_sensitive(pdialog->unit.present_unit_boxes[i], FALSE);
  }

  my_snprintf(buf, sizeof(buf), _("Present units (%d)"),
	      num_present_units_in_city(pdialog->pcity));
  gtk_frame_set_label(GTK_FRAME(pdialog->overview.present_units_frame),
		      buf);
  gtk_frame_set_label(GTK_FRAME(pdialog->unit.present_units_frame), buf);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_tradelist(struct city_dialog *pdialog)
{
  int i, x = 0, total = 0;

  char cityname[64], buf[512];

  buf[0] = '\0';

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    if (pdialog->pcity->trade[i]) {
      struct city *pcity;
      x = 1;
      total += pdialog->pcity->trade_value[i];

      if ((pcity = find_city_by_id(pdialog->pcity->trade[i]))) {
	my_snprintf(cityname, sizeof(cityname), "%s", pcity->name);
      } else {
	my_snprintf(cityname, sizeof(cityname), _("%s"), _("Unknown"));
      }
      my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		  _("Trade with %s gives %d trade.\n"),
		  cityname, pdialog->pcity->trade_value[i]);
    }
  }
  if (!x) {
    my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		_("No trade routes exist."));
  } else {
    my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		_("Total trade from trade routes: %d"), total);
  }
  gtk_label_set_text(GTK_LABEL(pdialog->overview.tradelist), buf);
}

/****************************************************************
 updates the sensitivity of the the prev and next buttons.
 this does not need pdialog as a parameter, since it iterates
 over all the open dialogs.
 note: we still need the sensitivity code in create_city_dialog()
 for the spied dialogs.
*****************************************************************/
static void city_dialog_update_prev_next()
{
  int count = 0;
  int city_number = city_list_size(&game.player_ptr->cities);

  /* the first time, we see if all the city dialogs are open */

  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pcity->owner == game.player_idx)
      count++;
  }
  dialog_list_iterate_end;

  if (count == city_number) {	/* all are open, shouldn't prev/next */
    dialog_list_iterate(dialog_list, pdialog) {
      gtk_widget_set_sensitive(pdialog->prev_command, FALSE);
      gtk_widget_set_sensitive(pdialog->next_command, FALSE);
    }
    dialog_list_iterate_end;
  } else {
    dialog_list_iterate(dialog_list, pdialog) {
      if (pdialog->pcity->owner == game.player_idx) {
	gtk_widget_set_sensitive(pdialog->prev_command, TRUE);
	gtk_widget_set_sensitive(pdialog->next_command, TRUE);
      }
    }
    dialog_list_iterate_end;
  }
}

/****************************************************************
 callback for the "<,>" buttons that switch the page of the supported units 
*****************************************************************/
static void supported_units_page_pos_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;

  if (w == pdialog->unit.supported_unit_button[0]) {
    pdialog->unit.supported_unit_pos--;
  } else if (w == pdialog->unit.supported_unit_button[1]) {
    pdialog->unit.supported_unit_pos++;
  } else if (w == pdialog->overview.supported_unit_button[0]) {
    pdialog->overview.supported_unit_pos--;
  } else {			/* pdialog->overview.supported_unit_button[1] */

    pdialog->overview.supported_unit_pos++;
  }

  if (pdialog->unit.supported_unit_pos < 0)
    pdialog->unit.supported_unit_pos = 0;

  if (pdialog->overview.supported_unit_pos < 0)
    pdialog->overview.supported_unit_pos = 0;

  city_dialog_update_supported_units(pdialog);
}

/****************************************************************
 callback for the "<,>" buttons that switch the page of the present units 
*****************************************************************/
static void present_units_page_pos_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;

  if (w == pdialog->unit.present_unit_button[0]) {
    pdialog->unit.present_unit_pos--;
  } else if (w == pdialog->unit.present_unit_button[1]) {
    pdialog->unit.present_unit_pos++;
  } else if (w == pdialog->overview.present_unit_button[0]) {
    pdialog->overview.present_unit_pos--;
  } else {			/* pdialog->overview.present_unit_button[1] */

    pdialog->overview.present_unit_pos++;
  }

  if (pdialog->unit.present_unit_pos < 0)
    pdialog->unit.present_unit_pos = 0;

  if (pdialog->overview.present_unit_pos < 0)
    pdialog->overview.present_unit_pos = 0;

  city_dialog_update_present_units(pdialog);
}

/****************************************************************
...
*****************************************************************/
static void activate_all_units_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog = data;

  activate_all_units(pdialog->pcity->tile);
}

/****************************************************************
 doesn't _actually_ sentry all: only the idle ones not under ai control.
*****************************************************************/
static void sentry_all_units_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  struct tile *ptile = pdialog->pcity->tile;
  struct unit_list *punit_list = &ptile->units;

  unit_list_iterate(*punit_list, punit) {
    if (game.player_idx == punit->owner) {
      if ((punit->activity == ACTIVITY_IDLE) &&
	  !punit->ai.control &&
	  can_unit_do_activity(punit, ACTIVITY_SENTRY)) {
	request_new_unit_activity(punit, ACTIVITY_SENTRY);
      }
    }
  }
  unit_list_iterate_end;
}

/****************************************************************
...
*****************************************************************/
static void show_units_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  struct tile *ptile = pdialog->pcity->tile;

  if (unit_list_size(&ptile->units))
    popup_unit_select_dialog(ptile);
}

/****************************************************************
Pop-up menu to change attributes of supported units
*****************************************************************/
static gint supported_unit_callback(GtkWidget * w, GdkEventButton * ev,
				    gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;
  GtkWidget *wd;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data)) &&
      (pcity = find_city_by_id(punit->homecity)) &&
      (pdialog = get_city_dialog(pcity))) {

    if (ev->button == 2 || ev->button == 3 || !can_client_issue_orders()) {
      return FALSE;
    }

    wd = popup_message_dialog(pdialog->shell,	/* "supported unit popup" */
			 _("Unit Commands"), unit_description(punit),
			 dummy_close_callback, NULL, 
			 _("Cen_ter"), unit_center_callback, punit->id,
			 _("_Activate unit"), unit_activate_callback,
			 punit->id, _("Activate unit, _close dialog"),
			 supported_unit_activate_close_callback, punit->id,
			 _("_Disband unit"), unit_disband_callback,
			 punit->id, _("_Cancel"), NULL, 0,
			 0);
    if (unit_flag(punit, F_UNDISBANDABLE)) {
      message_dialog_button_set_sensitive(wd, "button3", FALSE);
    }
  }
  return TRUE;
}

/****************************************************************
Pop-up menu to change attributes of units, ex. change homecity.
*****************************************************************/
static gint present_unit_callback(GtkWidget * w, GdkEventButton * ev,
				  gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;
  GtkWidget *wd;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data)) &&
      (pcity = map_get_city(punit->tile)) &&
      (pdialog = get_city_dialog(pcity))) {

    if (ev->button == 2 || ev->button == 3 || !can_client_issue_orders()) {
      return FALSE;
    }

    wd = popup_message_dialog(pdialog->shell,	/* "present unit popup" */
			      _("Unit Commands"), unit_description(punit),
			      dummy_close_callback, NULL, 
			      _("_Activate unit"), unit_activate_callback,
			      punit->id, _("Activate unit, _close dialog"),
			      present_unit_activate_close_callback,
			      punit->id, _("_Sentry unit"),
			      unit_sentry_callback, punit->id,
			      _("_Fortify unit"), unit_fortify_callback,
			      punit->id, _("_Disband unit"),
			      unit_disband_callback, punit->id,
			      _("Make new _homecity"),
			      unit_homecity_callback, punit->id,
			      _("_Upgrade unit"), unit_upgrade_callback,
			      punit->id, _("_Cancel"),
			      NULL, 0, NULL);
    if (punit->activity == ACTIVITY_SENTRY
	|| !can_unit_do_activity(punit, ACTIVITY_SENTRY)) {
      message_dialog_button_set_sensitive(wd, "button2", FALSE);
    }
    if (punit->activity == ACTIVITY_FORTIFYING
	|| !can_unit_do_activity(punit, ACTIVITY_FORTIFYING)) {
      message_dialog_button_set_sensitive(wd, "button3", FALSE);
    }
    if (unit_flag(punit, F_UNDISBANDABLE)) {
      message_dialog_button_set_sensitive(wd, "button4", FALSE);
    }
    if (punit->homecity == pcity->id) {
      message_dialog_button_set_sensitive(wd, "button5", FALSE);
    }
    if (can_upgrade_unittype(game.player_ptr, punit->type) == -1) {
      message_dialog_button_set_sensitive(wd, "button6", FALSE);
    }
  }
  return TRUE;
}

/****************************************************************
 if user middle-clicked on a unit, activate it and close dialog
*****************************************************************/
static gint present_unit_middle_callback(GtkWidget * w,
					 GdkEventButton * ev,
					 gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data)) &&
      (pcity = map_get_city(punit->tile)) &&
      (pdialog = get_city_dialog(pcity)) && can_client_issue_orders() && 
      (ev->button == 2 || ev->button == 3)) {
    set_unit_focus(punit);
    if (ev->button == 2)
      close_city_dialog(pdialog);
  }

  return TRUE;
}

/****************************************************************
 if user middle-clicked on a unit, activate it and close dialog
*****************************************************************/
static gint supported_unit_middle_callback(GtkWidget * w,
					   GdkEventButton * ev,
					   gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data)) &&
      (pcity = find_city_by_id(punit->homecity)) &&
      (pdialog = get_city_dialog(pcity)) && can_client_issue_orders() &&
      (ev->button == 2 || ev->button == 3)) {
    set_unit_focus(punit);
    if (ev->button == 2)
      close_city_dialog(pdialog);
  }

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static void unit_activate_callback(gpointer data)
{
  struct unit *punit;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data)))
    set_unit_focus(punit);
}

/****************************************************************
...
*****************************************************************/
static void supported_unit_activate_close_callback(gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data))) {
    set_unit_focus(punit);
    if ((pcity = player_find_city_by_id(game.player_ptr, punit->homecity)))
      if ((pdialog = get_city_dialog(pcity)))
	close_city_dialog(pdialog);
  }
}

/****************************************************************
...
*****************************************************************/
static void present_unit_activate_close_callback(gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data))) {
    set_unit_focus(punit);
    if ((pcity = map_get_city(punit->tile)))
      if ((pdialog = get_city_dialog(pcity)))
	close_city_dialog(pdialog);
  }
}

/****************************************************************
...
*****************************************************************/
static void unit_sentry_callback(gpointer data)
{
  struct unit *punit;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data)))
    request_unit_sentry(punit);
}

/****************************************************************
...
*****************************************************************/
static void unit_fortify_callback(gpointer data)
{
  struct unit *punit;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data)))
    request_unit_fortify(punit);
}

/****************************************************************
...
*****************************************************************/
static void unit_disband_callback(gpointer data)
{
  struct unit *punit;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data)))
    request_unit_disband(punit);
}

/****************************************************************
...
*****************************************************************/
static void unit_homecity_callback(gpointer data)
{
  struct unit *punit;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data)))
    request_unit_change_homecity(punit);
}

/****************************************************************
...
*****************************************************************/
static void unit_upgrade_callback(gpointer data)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr, (size_t) data);
  char buf[512];

  if (!punit) {
    return;
  }

  if (get_unit_upgrade_info(buf, sizeof(buf), punit) == UR_OK) {
    popup_message_dialog(top_vbox,
			 _("Upgrade Obsolete Units"), buf,
			 dummy_close_callback, NULL,
			 _("_Yes"), unit_upgrade_callback_yes,
			 GINT_TO_POINTER(punit->id), _("_No"),
			 NULL, 0, NULL);
  } else {
    popup_message_dialog(top_vbox,
			 _("Upgrade Unit!"), buf,
			 dummy_close_callback, NULL,
			 _("Darn"), NULL, 0, NULL);
  }
}

/****************************************************************
...
*****************************************************************/
static void unit_upgrade_callback_yes(gpointer data)
{
  struct unit *punit;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data))) {
    request_unit_upgrade(punit);
  }
}

/****************************************************************
...
*****************************************************************/
static void unit_center_callback(gpointer data)
{
  struct unit *punit;

  if ((punit = player_find_unit_by_id(game.player_ptr, (size_t) data))) {
    center_tile_mapcanvas(punit->tile);
  }
}

/*** Callbacks for citizen bar, map funcs that are not update ***/
/****************************************************************
Somebody clicked our list of citizens. If they clicked a specialist
then change the type of him, else do nothing.
*****************************************************************/
static void citizens_callback(GtkWidget * w, GdkEventButton * ev,
			      gpointer data)
{
  struct city_dialog *pdialog = data;
  struct city *pcity = pdialog->pcity;
  int citnum;

  if (ev->x > (pcity->size - 1) * pdialog->cwidth + SMALL_TILE_WIDTH)
    return;			/* no citizen that far to the right */

  citnum = MIN(pcity->size - 1, ev->x / pdialog->cwidth);

  city_rotate_specialist(pcity, citnum);
}

/**************************************************************************
...
**************************************************************************/
static gint button_down_citymap(GtkWidget * w, GdkEventButton * ev)
{
  struct city *pcity = NULL;;

  dialog_list_iterate(dialog_list, pdialog) {
#ifdef DEBUG
    {
      gint x, y, width, height, depth;

      gdk_window_get_geometry(pdialog->overview.map_canvas->window, &x, &y,
			      &width, &height, &depth);
      freelog(LOG_DEBUG, "%d x %d at (%d,%d)", width, height, x, y);

      gdk_window_get_geometry(pdialog->overview.map_canvas_pixmap->window,
			      &x, &y, &width, &height, &depth);
      freelog(LOG_DEBUG, "%d x %d at (%d,%d)", width, height, x, y);
    }
#endif
    if (pdialog->overview.map_canvas == w
	|| (pdialog->happiness.map_canvas
            && pdialog->happiness.map_canvas == w)) {
      pcity = pdialog->pcity;
      break;
    }
  } dialog_list_iterate_end;

  if (pcity) {
    int xtile, ytile;

    if (canvas_to_city_pos(&xtile, &ytile, ev->x, ev->y)) {
      city_toggle_worker(pcity, xtile, ytile);
    }
  }
  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static void draw_map_canvas(struct city_dialog *pdialog)
{
  struct Sprite tmp;

  gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->overview.map_canvas_pixmap),
		    TRUE);
  if (pdialog->happiness.map_canvas_pixmap) {	/* in case of spy */
    gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->happiness.map_canvas_pixmap),
		      TRUE);
  }

  tmp.pixmap = pdialog->map_canvas_store;
  tmp.has_mask = 0;
  tmp.width = canvas_width;
  tmp.height = canvas_height;

  gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->overview.map_canvas_pixmap),
		     &tmp, 0, 0, TRUE);
  if (pdialog->happiness.map_canvas_pixmap) {	/* in case of spy */
    gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->happiness.map_canvas_pixmap),
		       &tmp, 0, 0, TRUE);
  }
}

/****************************************************************
...
*****************************************************************/
static gint city_map_canvas_expose(GtkWidget * w, GdkEventExpose * ev,
				   gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;

  draw_map_canvas(pdialog);

  return TRUE;
}

/********* Callbacks for Buy, Change, Sell, Worklist ************/
/****************************************************************
...
*****************************************************************/
static void buy_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog;
  int value;
  const char *name;
  char buf[512];

  pdialog = (struct city_dialog *) data;

  if (pdialog->pcity->is_building_unit) {
    name = get_unit_type(pdialog->pcity->currently_building)->name;
  } else {
    name =
	get_impr_name_ex(pdialog->pcity,
			 pdialog->pcity->currently_building);
  }
  value = city_buy_cost(pdialog->pcity);

  if (game.player_ptr->economic.gold >= value) {
    my_snprintf(buf, sizeof(buf),
		_("Buy %s for %d gold?\nTreasury contains %d gold."),
		name, value, game.player_ptr->economic.gold);

    pdialog->buy_shell =
	popup_message_dialog(pdialog->shell, _("Buy It!"),buf,
			     buy_close_callback, pdialog,
			     _("_Yes"), buy_callback_yes, pdialog,
			     _("_No"), NULL, pdialog, 0);
  } else {
    my_snprintf(buf, sizeof(buf),
		_("%s costs %d gold.\nTreasury contains %d gold."),
		name, value, game.player_ptr->economic.gold);

    pdialog->buy_shell = popup_message_dialog(pdialog->shell,
					      _("Buy It!"), buf,
					      buy_close_callback, pdialog,
					      _("Darn"), 
					      NULL, pdialog, 0);
  }
}

/****************************************************************
...
*****************************************************************/
static void buy_close_callback(gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;

  pdialog->buy_shell = NULL;
}

/****************************************************************
...
*****************************************************************/
static void buy_callback_yes(gpointer data)
{
  struct city_dialog *pdialog = data;

  city_buy_production(pdialog->pcity);
}

/****************************************************************
...
*****************************************************************/
static void change_callback(GtkWidget * w, gpointer data)
{
  GtkWidget *cshell, *button, *scrolled;
  struct city_dialog *pdialog;
  int i;
  static const gchar *title_[4] =
      { N_("Type"), N_("Info"), N_("Cost"), N_("Turns") };
  static gchar **title = NULL;
  GtkAccelGroup *accel = gtk_accel_group_new();
  char *row[4];
  char buf[4][64];
  cid cids[U_LAST + B_LAST];
  int item, cids_used;
  struct item items[U_LAST + B_LAST];

  if (!title) {
    title = intl_slist(4, title_);
  }

  pdialog = (struct city_dialog *) data;

  cshell = gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(cshell), "delete_event",
		     GTK_SIGNAL_FUNC(change_deleted_callback), pdialog);
  pdialog->change_shell = cshell;

  gtk_window_set_position(GTK_WINDOW(cshell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(cshell));
  gtk_window_set_title(GTK_WINDOW(cshell), _("Change Production"));

  pdialog->change_list = gtk_clist_new_with_titles(4, title);
  gtk_clist_column_titles_passive(GTK_CLIST(pdialog->change_list));
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), pdialog->change_list);

  for (i = 0; i < 4; i++)
    gtk_clist_set_column_auto_resize(GTK_CLIST(pdialog->change_list), i,
				     TRUE);

  gtk_clist_set_column_justification(GTK_CLIST(pdialog->change_list), 2,
				     GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_justification(GTK_CLIST(pdialog->change_list), 3,
				     GTK_JUSTIFY_RIGHT);

  /* Set up the doubleclick-on-list-item handler */
  gtk_signal_connect(GTK_OBJECT(pdialog->change_list), "select_row",
		     GTK_SIGNAL_FUNC(change_list_callback), pdialog);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(scrolled, -2, 350);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(cshell)->vbox), scrolled,
		     TRUE, TRUE, 0);

  button = gtk_accelbutton_new(_("_Change"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(cshell)->action_area), button,
		     TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(change_yes_callback), pdialog);

  gtk_widget_add_accelerator(button, "clicked", accel, GDK_Return, 0, 0);

  button = gtk_accelbutton_new(_("Ca_ncel"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(cshell)->action_area), button,
		     TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(change_no_callback), pdialog);

  gtk_widget_add_accelerator(button, "clicked", accel, GDK_Escape, 0, 0);

  button = gtk_accelbutton_new(_("_Help"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(cshell)->action_area),
		     button, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(change_help_callback), pdialog);

  gtk_widget_set_sensitive(pdialog->shell, FALSE);

  for (i = 0; i < 4; i++)
    row[i] = buf[i];

  gtk_clist_freeze(GTK_CLIST(pdialog->change_list));
  gtk_clist_clear(GTK_CLIST(pdialog->change_list));

  cids_used = collect_cids4(cids, pdialog->pcity, 0);
  name_and_sort_items(cids, cids_used, items, TRUE, pdialog->pcity);

  for (item = 0; item < cids_used; item++) {
    cid cid = items[item].cid;
    int row_no;

    get_city_dialog_production_row(row, sizeof(buf[0]),
    	                           cid_id(cid), cid_is_unit(cid),
    	                           pdialog->pcity);
    row_no = gtk_clist_append(GTK_CLIST(pdialog->change_list), row);
    gtk_clist_set_row_data(GTK_CLIST(pdialog->change_list),
			   row_no, GINT_TO_POINTER(cid));
  }

  gtk_clist_thaw(GTK_CLIST(pdialog->change_list));

  gtk_widget_show_all(GTK_DIALOG(cshell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(cshell)->action_area);
  gtk_widget_show(cshell);
}

/****************************************************************
...
*****************************************************************/
static gint change_deleted_callback(GtkWidget * w, GdkEvent * ev,
				    gpointer data)
{
  struct city_dialog *pdialog;

  pdialog = (struct city_dialog *) data;

  pdialog->change_shell = NULL;
  gtk_widget_set_sensitive(pdialog->shell, TRUE);
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
static void change_no_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog;

  pdialog = (struct city_dialog *) data;

  gtk_widget_destroy(w->parent->parent->parent);
  pdialog->change_shell = NULL;
  gtk_widget_set_sensitive(pdialog->shell, TRUE);
}

/****************************************************************
...
*****************************************************************/
static void change_yes_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  GList *selection = GTK_CLIST(pdialog->change_list)->selection;

  if (selection) {
    cid cid = GPOINTER_TO_INT(
                  gtk_clist_get_row_data(GTK_CLIST(pdialog->change_list),
					 GPOINTER_TO_INT(selection->data)));

    city_change_production(pdialog->pcity, cid_is_unit(cid), cid_id(cid));
  }
  gtk_widget_destroy(w->parent->parent->parent);
  pdialog->change_shell = NULL;
  gtk_widget_set_sensitive(pdialog->shell, TRUE);
}

/****************************************************************
...
*****************************************************************/
static void change_list_callback(GtkWidget * w, gint row, gint col,
				 GdkEvent * ev, gpointer data)
{
  /* Allows new production options to be selected via a double-click */
  if (ev && ev->type == GDK_2BUTTON_PRESS)
    change_yes_callback(w, data);
}

/****************************************************************
...
*****************************************************************/
static void change_help_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  GList *selection = GTK_CLIST(pdialog->change_list)->selection;

  if (selection) {
    cid cid = GPOINTER_TO_INT(
                  gtk_clist_get_row_data(GTK_CLIST(pdialog->change_list),
					 GPOINTER_TO_INT(selection->data)));
    int id = cid_id(cid);

    if (cid_is_unit(cid)) {
      popup_help_dialog_typed(get_unit_type(id)->name, HELP_UNIT);
    } else if (is_wonder(id)) {
      popup_help_dialog_typed(get_improvement_name(id), HELP_WONDER);
    } else {
      popup_help_dialog_typed(get_improvement_name(id), HELP_IMPROVEMENT);
    }
  } else {
    popup_help_dialog_string(HELP_IMPROVEMENTS_ITEM);
  }
}

/****************************************************************
...
*****************************************************************/
static void sell_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  GList *selection;
  int id;
  char buf[512];

  selection = GTK_CLIST(pdialog->overview.improvement_list)->selection;
  if (!selection)
    return;

  id = GPOINTER_TO_INT(gtk_clist_get_row_data
		       (GTK_CLIST(pdialog->overview.improvement_list),
			GPOINTER_TO_INT(selection->data)));
  assert(city_got_building(pdialog->pcity, id));
  if (is_wonder(id))
    return;

  pdialog->sell_id = id;
  my_snprintf(buf, sizeof(buf), _("Sell %s for %d gold?"),
	      get_impr_name_ex(pdialog->pcity, id), impr_sell_gold(id));

  pdialog->sell_shell = popup_message_dialog(pdialog->shell,
					     _("Sell It!"), buf,
					     sell_close_callback, pdialog,
					     _("_Yes"), sell_callback_yes,
					     pdialog, _("_No"),
					     NULL, pdialog, 0);
}

/****************************************************************
...
*****************************************************************/
static void sell_close_callback(gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;

  pdialog->sell_shell = NULL;
}

/****************************************************************
...
*****************************************************************/
static void sell_callback_yes(gpointer data)
{
  struct city_dialog *pdialog = data;

  city_sell_improvement(pdialog->pcity, pdialog->sell_id);
}

/****************************************************************
 this is here because it's closely related to the sell stuff
*****************************************************************/
static void select_impr_list_callback(GtkWidget * w, gint row, gint column,
				      GdkEventButton * event,
				      gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  GList *selection =
      GTK_CLIST(pdialog->overview.improvement_list)->selection;

  if (!selection || pdialog->pcity->did_buy || pdialog->pcity->did_sell ||
      pdialog->pcity->owner != game.player_idx) {
    gtk_set_label(GTK_BUTTON(pdialog->overview.sell_command)->child,
		  _("Sell"));
    gtk_widget_set_sensitive(pdialog->overview.sell_command, FALSE);
  } else {
    int id = GPOINTER_TO_INT(gtk_clist_get_row_data
			     (GTK_CLIST(pdialog->overview.improvement_list),
			      GPOINTER_TO_INT(selection->data)));
    assert(city_got_building(pdialog->pcity, id));

    if (!is_wonder(id)) {
      char buf[64];
      my_snprintf(buf, sizeof(buf), _("Sell (worth %d gold)"),
		  impr_sell_gold(id));
      gtk_set_label(GTK_BUTTON(pdialog->overview.sell_command)->child,
		    buf);
      gtk_widget_set_sensitive(pdialog->overview.sell_command,
		               can_client_issue_orders());
    }
  }
}

/****************************************************************
 If switching away from worklist, we commit it.
*****************************************************************/
static void switch_page_callback(GtkNotebook * notebook,
				 GtkNotebookPage * page, gint page_num,
				 gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;

  /* gtk_notebook_get_current_page() is actually the */
  /* page from which we switched.                    */
  if (gtk_notebook_get_current_page(notebook) == WORKLIST_PAGE) {
    if (pdialog->pcity->owner == game.player_idx &&
	pdialog->wl_editor->changed) {
      commit_worklist(pdialog->wl_editor);
    }
  }
}

/****************************************************************
  Commit the changes to the worklist for the city.
*****************************************************************/
static void commit_city_worklist(struct worklist *pwl, void *data)
{
  struct city_dialog *pdialog = data;
  int k, id;
  bool is_unit;

  /* Update the worklist.  Remember, though -- the current build
     target really isn't in the worklist; don't send it to the server
     as part of the worklist.  Of course, we have to search through
     the current worklist to find the first _now_available_ build
     target (to cope with players who try mean things like adding a
     Battleship to a city worklist when the player doesn't even yet
     have the Map Making tech).  */

  for (k = 0; k < MAX_LEN_WORKLIST; k++) {
    int same_as_current_build;
    if (!worklist_peek_ith(pwl, &id, &is_unit, k))
      break;

    same_as_current_build = id == pdialog->pcity->currently_building
	&& is_unit == pdialog->pcity->is_building_unit;

    /* Very special case: If we are currently building a wonder we
       allow the construction to continue, even if we the wonder is
       finished elsewhere, ie unbuildable. */
    if (k == 0 && !is_unit && is_wonder(id) && same_as_current_build) {
      worklist_remove(pwl, k);
      break;
    }

    /* If it can be built... */
    if ((is_unit && can_build_unit(pdialog->pcity, id)) ||
	(!is_unit && can_build_improvement(pdialog->pcity, id))) {
      /* ...but we're not yet building it, then switch. */
      if (!same_as_current_build) {
	/* Change the current target */
	city_change_production(pdialog->pcity, is_unit, id);
      }

      /* This item is now (and may have always been) the current
         build target.  Drop it out of the worklist. */
      worklist_remove(pwl, k);
      break;
    }
  }

  /* Send the rest of the worklist on its way. */
  city_set_worklist(pdialog->pcity, pwl);
}

/******* Callbacks for stuff on the Misc. Settings page *********/
/****************************************************************
...
*****************************************************************/
static void rename_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog;

  pdialog = (struct city_dialog *) data;

  pdialog->rename_shell = input_dialog_create(pdialog->shell,
					      /*"shellrenamecity" */
					      _("Rename City"),
					      _
					      ("What should we rename the city to?"),
					      pdialog->pcity->name,
					      rename_callback_yes,
					      (gpointer) pdialog,
					      rename_callback_no,
					      (gpointer) pdialog);
}

/****************************************************************
...
*****************************************************************/
static void rename_callback_no(gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;

  pdialog->rename_shell = NULL;
}

/****************************************************************
...
*****************************************************************/
static void rename_callback_yes(const char *input, gpointer data)
{
  struct city_dialog *pdialog = data;

  city_rename(pdialog->pcity, input);

  pdialog->rename_shell = NULL;
}

/****************************************************************
 Sets which page will be set on reopen of dialog
*****************************************************************/
static void misc_whichtab_callback(GtkWidget * w, gpointer data)
{
  new_dialog_def_page = GPOINTER_TO_INT(data);
}

/**************************************************************************
City options callbacks
**************************************************************************/
static void cityopt_callback(GtkWidget * w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;

  if(!pdialog->misc.block_signal){
    int i, new_options = 0;
    struct city *pcity = pdialog->pcity;

    for(i = 0; i < NUM_CITY_OPTS; i++){
      if(GTK_TOGGLE_BUTTON(pdialog->misc.city_opts[i])->active)
        new_options |= (1 << i);
    }

    if (GTK_TOGGLE_BUTTON(pdialog->misc.new_citizens_radio[1])->active) {
      new_options |= (1 << CITYO_NEW_EINSTEIN);
    } else if(GTK_TOGGLE_BUTTON(pdialog->misc.new_citizens_radio[2])->active) {
      new_options |= (1 << CITYO_NEW_TAXMAN);
    }

    dsend_packet_city_options_req(&aconnection, pcity->id, new_options);
  }
}

/**************************************************************************
 refresh the city options (auto_[land, air, sea, helicopter] and 
 disband-is-size-1) in the misc page.
**************************************************************************/
static void set_cityopt_values(struct city_dialog *pdialog)
{
  struct city *pcity = pdialog->pcity;
  int i;

  pdialog->misc.block_signal = 1;
  for (i = 0; i < NUM_CITY_OPTS; i++) {
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
				(pdialog->misc.city_opts[i]),
				is_city_option_set(pcity, i));
  }

  if (is_city_option_set(pcity, CITYO_NEW_EINSTEIN)) {
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
                                (pdialog->misc.new_citizens_radio[1]), TRUE);
  } else if (is_city_option_set(pcity, CITYO_NEW_TAXMAN)) {
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
                                (pdialog->misc.new_citizens_radio[2]), TRUE);
  } else {
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
                                (pdialog->misc.new_citizens_radio[0]), TRUE);
  }
  pdialog->misc.block_signal = 0;
}

/*************** Callbacks for: Close, Prev, Next. **************/
/****************************************************************
...
*****************************************************************/
static gint city_dialog_delete_callback(GtkWidget * w, GdkEvent * ev,
					gpointer data)
{
  close_city_dialog((struct city_dialog *) data);
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
static void close_callback(GtkWidget * w, gpointer data)
{
  close_city_dialog((struct city_dialog *) data);
}

/****************************************************************
...
*****************************************************************/
static void close_city_dialog(struct city_dialog *pdialog)
{
  gtk_widget_hide(pdialog->shell);

  if (pdialog->pcity->owner == game.player_idx) {
    if(pdialog->wl_editor->changed){
      commit_worklist(pdialog->wl_editor);
    }

    close_worklist_editor(pdialog->wl_editor);
    close_happiness_dialog(pdialog->pcity);
    close_cma_dialog(pdialog->pcity);
  }

  citydialog_height = pdialog->shell->allocation.height;
  citydialog_width = pdialog->shell->allocation.width;

  last_page =
      gtk_notebook_get_current_page(GTK_NOTEBOOK(pdialog->notebook));

  /* else this will be called NUM_PAGES times as the pages are destroyed */

  gtk_signal_disconnect_by_func(GTK_OBJECT(pdialog->notebook),
				GTK_SIGNAL_FUNC(switch_page_callback),
				pdialog);

  support_frame_height =
      pdialog->unit.supported_units_frame->allocation.height;
  support_frame_width =
      pdialog->unit.supported_units_frame->allocation.width;

  dialog_list_unlink(&dialog_list, pdialog);

  free(pdialog->unit.supported_unit_boxes);
  free(pdialog->unit.supported_unit_pixmaps);
  free(pdialog->unit.supported_unit_ids);

  free(pdialog->unit.present_unit_boxes);
  free(pdialog->unit.present_unit_pixmaps);
  free(pdialog->unit.present_unit_ids);

  free(pdialog->overview.present_unit_boxes);
  free(pdialog->overview.present_unit_pixmaps);
  free(pdialog->overview.present_unit_ids);

  free(pdialog->overview.supported_unit_boxes);
  free(pdialog->overview.supported_unit_pixmaps);
  free(pdialog->overview.supported_unit_ids);

  if (pdialog->is_modal)
    gtk_widget_set_sensitive(top_vbox, TRUE);

  if (pdialog->change_shell)
    gtk_widget_destroy(pdialog->change_shell);

  if (pdialog->buy_shell)
    gtk_widget_destroy(pdialog->buy_shell);
  if (pdialog->sell_shell)
    gtk_widget_destroy(pdialog->sell_shell);
  if (pdialog->rename_shell)
    gtk_widget_destroy(pdialog->rename_shell);

  gdk_pixmap_unref(pdialog->map_canvas_store);

  unit_list_iterate(pdialog->pcity->info_units_supported, psunit) {
    free(psunit);
  }
  unit_list_iterate_end;

  unit_list_unlink_all(&(pdialog->pcity->info_units_supported));

  unit_list_iterate(pdialog->pcity->info_units_present, psunit) {
    free(psunit);
  }
  unit_list_iterate_end;

  unit_list_unlink_all(&(pdialog->pcity->info_units_present));

  gtk_widget_destroy(pdialog->shell);
  free(pdialog);

  /* need to do this every time a new dialog is closed. */
  city_dialog_update_prev_next();
}

/************************************************************************
  Helper for switch_city_callback.
*************************************************************************/
static int city_comp_by_turn_founded(const void *a, const void *b)
{
  struct city *pcity1 = *((struct city **) a);
  struct city *pcity2 = *((struct city **) b);

  return pcity1->turn_founded - pcity2->turn_founded;
}

/************************************************************************
  Callback for the prev/next buttons. Switches to the previous/next
  city.
*************************************************************************/
static void switch_city_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *) data;
  int i, dir, non_open_size, size = city_list_size(&game.player_ptr->cities);
  struct city *new_pcity = NULL;
  struct city **array;

  assert(city_dialogs_have_been_initialised);
  assert(size >= 1);
  assert(pdialog->pcity->owner == game.player_idx);

  if (size == 1) {
    return;
  }

  /* dir = 1 will advance to the city, dir = -1 will get previous */
  if (w == pdialog->next_command) {
    dir = 1;
  } else if (w == pdialog->prev_command) {
    dir = -1;
  } else {
    assert(0);
    dir = 1;
  }

  array = fc_malloc(size * sizeof(struct city *));

  non_open_size = 0;
  for (i = 0; i < size; i++) {
    struct city *other_pcity = city_list_get(&game.player_ptr->cities, i);
    if (other_pcity == pdialog->pcity || !get_city_dialog(other_pcity)) {
      array[non_open_size] = other_pcity;
      non_open_size++;
    }
  }

  assert(non_open_size > 0);

  if (non_open_size == 1) {
    free(array);
    return;
  }

  qsort(array, non_open_size, sizeof(struct city *),
	city_comp_by_turn_founded);

  for (i = 0; i < non_open_size; i++) {
    if (pdialog->pcity == array[i]) {
      break;
    }
  }

  assert(i < non_open_size);

  new_pcity = array[(i + dir + non_open_size) % non_open_size];
  free(array);

  /* cleanup worklist and happiness dialogs */
  if(pdialog->wl_editor->changed){
    commit_worklist(pdialog->wl_editor);
  }
  close_happiness_dialog(pdialog->pcity);

  pdialog->pcity = new_pcity;

  /* reinitialize happiness, worklist, and cma dialogs */
  gtk_box_pack_start(GTK_BOX(pdialog->happiness.widget),
		     get_top_happiness_display(pdialog->pcity), TRUE, TRUE, 0);
  pdialog->cma_editor->pcity = new_pcity;
  pdialog->wl_editor->pcity = new_pcity;
  pdialog->wl_editor->pwl = &new_pcity->worklist;
  pdialog->wl_editor->user_data = (void *) pdialog;

  center_tile_mapcanvas(pdialog->pcity->tile);
  set_cityopt_values(pdialog);	/* need not be in refresh_city_dialog */
  refresh_city_dialog(pdialog->pcity);
  select_impr_list_callback(NULL, 0, 0, NULL, pdialog); /* unselects clist */
}
