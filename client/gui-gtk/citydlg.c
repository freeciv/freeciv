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
#include <ctype.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "cityrep.h"
#include "colors.h"
#include "control.h"
#include "clinet.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapview.h"
#include "options.h"
#include "repodlgs.h"
#include "tilespec.h"
#include "wldlg.h"

#include "citydlg.h"

#include "cityicon.ico"

#define NUM_CITIZENS_SHOWN 25

int CITY_DIALOG_HEIGHT;
int CITY_DIALOG_WIDTH;
int NUM_UNITS_SHOWN;


struct city_dialog {
  struct city *pcity;
  GtkWidget *shell;
  GtkWidget *main_form;
  GtkWidget *cityname_label;
  GtkWidget *citizen_box;
  GtkWidget *citizen_pixmap;
  GtkWidget *production_label;
  GtkWidget *output_label;
  GtkWidget *storage_label;
  GtkWidget *pollution_label;
  GtkWidget *sub_form;
  GtkWidget *map_canvas;
  GdkPixmap *map_canvas_store;
  GtkWidget *building_label; 
  GtkWidget *progress_label; 
  GtkWidget *buy_command; 
  GtkWidget *change_command;
  GtkWidget *sell_command;
  GtkWidget *worklist_command; 
  GtkWidget *worklist_label;
  GtkWidget *improvement_viewport, *improvement_list;
  GtkWidget *support_unit_label;
  GtkWidget **support_unit_boxes;
  GtkWidget **support_unit_pixmaps;
  GtkWidget *support_unit_button	[2];
  GtkWidget *present_unit_label;
  GtkWidget **present_unit_boxes;
  GtkWidget **present_unit_pixmaps;
  GtkWidget *present_unit_button	[2];
  GtkWidget *close_command;
  GtkWidget *rename_command; 
  GtkWidget *trade_command; 
  GtkWidget *activate_command;
  GtkWidget *show_units_command; 
  GtkWidget *cityopt_command;

  GtkWidget *worklist_shell;
  GtkWidget *buy_shell, *sell_shell;
  GtkWidget *change_shell, *change_list;
  GtkWidget *rename_shell, *rename_input;
  
  Impr_Type_id sell_id;
  
  int first_elvis, first_scientist, first_taxman;
  int cwidth;
  int *support_unit_ids;
  int support_unit_pos;
  int *present_unit_ids;
  int present_unit_pos;
  char improvlist_names[B_LAST+1][64];
  char *improvlist_names_ptrs[B_LAST+1];
  
  int change_list_ids[B_LAST+1+U_LAST+1];
  int change_list_num_improvements;

  int is_modal;
};

static GdkBitmap *icon_bitmap;

static struct genlist dialog_list;
static int city_dialogs_have_been_initialised;
static int canvas_width, canvas_height;

static struct city_dialog *get_city_dialog(struct city *pcity);
static struct city_dialog *create_city_dialog(struct city *pcity, int make_modal);
static void close_city_dialog(struct city_dialog *pdialog);
static void initialize_city_dialogs(void);

static void city_dialog_update_title(struct city_dialog *pdialog);
static void city_dialog_update_citizens(struct city_dialog *pdialog);
static void city_dialog_update_map(struct city_dialog *pdialog);
static void city_dialog_update_production(struct city_dialog *pdialog);
static void city_dialog_update_output(struct city_dialog *pdialog);
static void city_dialog_update_storage(struct city_dialog *pdialog);
static void city_dialog_update_pollution(struct city_dialog *pdialog);
static void city_dialog_update_building(struct city_dialog *pdialog);
static void city_dialog_update_improvement_list(struct city_dialog *pdialog);
static void city_dialog_update_supported_units(struct city_dialog *pdialog, int id);
static void city_dialog_update_present_units(struct city_dialog *pdialog, int id);

static gint city_map_canvas_expose(GtkWidget *w, GdkEventExpose *ev, gpointer data);
static void city_get_canvas_xy(int map_x, int map_y, int *canvas_x, int *canvas_y);
static void city_get_map_xy(int canvas_x, int canvas_y, int *map_x, int *map_y);
static void city_dialog_update_map_iso(struct city_dialog *pdialog);
static void city_dialog_update_map_ovh(struct city_dialog *pdialog);

static void buy_callback	(GtkWidget *w, gpointer data);
static void change_callback	(GtkWidget *w, gpointer data);
static void sell_callback	(GtkWidget *w, gpointer data);
static void worklist_callback	(GtkWidget *w, gpointer data);
static void close_callback	(GtkWidget *w, gpointer data);
static void rename_callback	(GtkWidget *w, gpointer data);
static void trade_callback	(GtkWidget *w, gpointer data);
static void activate_callback	(GtkWidget *w, gpointer data);
static void show_units_callback (GtkWidget *w, gpointer data);
static void cityopt_callback    (GtkWidget *w, gpointer data);

static void citizens_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);

static void buy_callback_no(GtkWidget *w, gpointer data);
static void buy_callback_yes(GtkWidget *w, gpointer data);
static void sell_callback_no(GtkWidget *w, gpointer data);
static void sell_callback_yes(GtkWidget *w, gpointer data);
static void change_no_callback(GtkWidget *w, gpointer data);
static void change_to_callback(GtkWidget *w, gpointer data);
static void change_help_callback(GtkWidget *w, gpointer data);
static void change_list_callback(GtkWidget *w, gint row, gint col, GdkEvent *ev, gpointer data);
static void commit_city_worklist(struct worklist *pwl, void *data);
static void cancel_city_worklist(void *data);

static gint support_units_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);
static gint present_units_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);
static gint s_units_middle_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);
static gint p_units_middle_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);
static void city_dialog_support_unit_pos_callback(GtkWidget *w, gpointer data);
static void city_dialog_present_unit_pos_callback(GtkWidget *w, gpointer data);
static void supported_units_activate_close_callback (GtkWidget *w, gpointer data);
static void present_units_activate_callback         (GtkWidget *w, gpointer data);
static void present_units_activate_close_callback   (GtkWidget *w, gpointer data);
static void present_units_sentry_callback           (GtkWidget *w, gpointer data);
static void present_units_fortify_callback          (GtkWidget *w, gpointer data);
static void present_units_disband_callback          (GtkWidget *w, gpointer data);
static void present_units_homecity_callback         (GtkWidget *w, gpointer data);
static void present_units_cancel_callback           (GtkWidget *w, gpointer data);
static void upgrade_callback(GtkWidget *w, gpointer data);
static void unitupgrade_callback_yes	(GtkWidget *w, gpointer data);
static void unitupgrade_callback_no	(GtkWidget *w, gpointer data);
static void activate_unit(struct unit *punit);

static void rename_city_callback(GtkWidget *w, gpointer data);
static void trade_message_dialog_callback(GtkWidget *w, gpointer data);

static void popdown_cityopt_dialog(void);

static gint city_dialog_delete_callback(GtkWidget *w, GdkEvent *ev, gpointer data);
static gint change_deleted_callback(GtkWidget *w, GdkEvent *ev, gpointer data);
static gint buy_callback_delete(GtkWidget *widget, GdkEvent *event, gpointer data);
static gint sell_callback_delete(GtkWidget *widget, GdkEvent *event, gpointer data);
static gint rename_callback_delete(GtkWidget *widget, GdkEvent *event, gpointer data);

/****************************************************************
...
*****************************************************************/
static void initialize_city_dialogs(void)
{
  assert(!city_dialogs_have_been_initialised);

  genlist_init(&dialog_list);
  if (is_isometric) {
    canvas_width = 4 * NORMAL_TILE_WIDTH;
    canvas_height = 4 * NORMAL_TILE_HEIGHT;
  } else {
    canvas_width = 5 * NORMAL_TILE_WIDTH;
    canvas_height = 5 * NORMAL_TILE_HEIGHT;
  }
  CITY_DIALOG_WIDTH = canvas_width + 440;
  CITY_DIALOG_HEIGHT = 2*SMALL_TILE_HEIGHT + canvas_height
	+ 3*NORMAL_TILE_HEIGHT + 160;

  if (is_isometric) {
    NUM_UNITS_SHOWN = CITY_DIALOG_WIDTH/UNIT_TILE_WIDTH-1;
  } else {
    NUM_UNITS_SHOWN = CITY_DIALOG_WIDTH/(UNIT_TILE_WIDTH + UNIT_TILE_WIDTH/2)-1;
  }

  city_dialogs_have_been_initialised=1;
}

/****************************************************************
...
*****************************************************************/
static struct city_dialog *get_city_dialog(struct city *pcity)
{
  struct genlist_iterator myiter;

  if (!city_dialogs_have_been_initialised)
     initialize_city_dialogs();
  
  genlist_iterator_init(&myiter, &dialog_list, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct city_dialog *)ITERATOR_PTR(myiter))->pcity==pcity)
      return ITERATOR_PTR(myiter);

  return 0;
}

/****************************************************************
...
*****************************************************************/
int city_dialog_is_open(struct city *pcity)
{
  if (get_city_dialog(pcity)) {
    return 1;
  } else {
    return 0;
  }
}

/****************************************************************
...
*****************************************************************/
void refresh_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog;
  
  if((pdialog=get_city_dialog(pcity))) {
    city_dialog_update_improvement_list(pdialog);
    city_dialog_update_title(pdialog);
    city_dialog_update_supported_units(pdialog, 0);
    city_dialog_update_present_units(pdialog, 0);
    city_dialog_update_citizens(pdialog);
    city_dialog_update_map(pdialog);
    city_dialog_update_production(pdialog);
    city_dialog_update_output(pdialog);
    city_dialog_update_building(pdialog);
    city_dialog_update_storage(pdialog);
    city_dialog_update_pollution(pdialog);

    gtk_widget_set_sensitive(pdialog->trade_command,
			city_num_trade_routes(pcity)?TRUE:FALSE);
    gtk_widget_set_sensitive(pdialog->activate_command,
			unit_list_size(&map_get_tile(pcity->x,pcity->y)->units)
			?TRUE:FALSE);
    gtk_widget_set_sensitive(pdialog->show_units_command,
			unit_list_size(&map_get_tile(pcity->x,pcity->y)->units)
			?TRUE:FALSE);
    gtk_widget_set_sensitive(pdialog->cityopt_command, TRUE);
  }
  if(pcity->owner == game.player_idx)  {
    city_report_dialog_update_city(pcity);
    economy_report_dialog_update();
  } else {
    if(pdialog)  {
      /* Set the buttons we do not want live while a Diplomat investigates */
      gtk_widget_set_sensitive(pdialog->buy_command, FALSE);
      gtk_widget_set_sensitive(pdialog->change_command, FALSE);
      gtk_widget_set_sensitive(pdialog->worklist_command, FALSE);
      gtk_widget_set_sensitive(pdialog->sell_command, FALSE);
      gtk_widget_set_sensitive(pdialog->rename_command, FALSE);
      gtk_widget_set_sensitive(pdialog->activate_command, FALSE);
      gtk_widget_set_sensitive(pdialog->show_units_command, FALSE);
      gtk_widget_set_sensitive(pdialog->cityopt_command, FALSE);
    }
  }
}

/****************************************************************
...
*****************************************************************/
void refresh_unit_city_dialogs(struct unit *punit)
{
  struct city *pcity_sup, *pcity_pre;
  struct city_dialog *pdialog;

  pcity_sup=player_find_city_by_id(game.player_ptr, punit->homecity);
  pcity_pre=map_get_city(punit->x, punit->y);
  
  if(pcity_sup && (pdialog=get_city_dialog(pcity_sup)))
    city_dialog_update_supported_units(pdialog, 0);
  
  if(pcity_pre && (pdialog=get_city_dialog(pcity_pre)))
    city_dialog_update_present_units(pdialog, 0);
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_city_dialog(struct city *pcity, int make_modal)
{
  struct city_dialog *pdialog;
  
  if(!(pdialog=get_city_dialog(pcity)))
    pdialog=create_city_dialog(pcity, make_modal);
  gtk_set_relative_position(toplevel, pdialog->shell, 10, 10);

  gtk_widget_show(pdialog->shell);
}

/****************************************************************
popdown the dialog 
*****************************************************************/
void popdown_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog;
  
  if((pdialog=get_city_dialog(pcity)))
    close_city_dialog(pdialog);
}

/****************************************************************
popdown all dialogs
*****************************************************************/
void popdown_all_city_dialogs(void)
{
  if(!city_dialogs_have_been_initialised) {
    return;
  }
  while(genlist_size(&dialog_list)) {
    close_city_dialog(genlist_get(&dialog_list,0));
  }
  popdown_cityopt_dialog();
}


/****************************************************************
...
*****************************************************************/
static gint city_map_canvas_expose(GtkWidget *w, GdkEventExpose *ev,
				   gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;
  gdk_draw_pixmap(pdialog->map_canvas->window, civ_gc, pdialog->map_canvas_store,
		  0, 0, 0, 0, canvas_width, canvas_height);
  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static gint city_dialog_delete_callback(GtkWidget *w, GdkEvent *ev,
					gpointer data)
{
  close_city_dialog((struct city_dialog *)data);
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_support_unit_pos_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog=((struct city_dialog *)data);

  if (w==pdialog->support_unit_button[0]) {
    pdialog->support_unit_pos--;
  } else {
    pdialog->support_unit_pos++;
  }
  if (pdialog->support_unit_pos<0) {
    pdialog->support_unit_pos=0;
  }
  city_dialog_update_supported_units(pdialog, 0);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_present_unit_pos_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog=((struct city_dialog *)data);

  if (w==pdialog->present_unit_button[0]) {
    pdialog->present_unit_pos--;
  } else {
    pdialog->present_unit_pos++;
  }
  if (pdialog->present_unit_pos<0) {
    pdialog->present_unit_pos=0;
  }
  city_dialog_update_present_units(pdialog, 0);
}

/****************************************************************
...
*****************************************************************/
static struct city_dialog *create_city_dialog(struct city *pcity, int make_modal)
{
  int i;
  struct city_dialog *pdialog;
  GtkWidget *box, *frame, *vbox, *scrolled, *hbox, *vbox2;
  GtkAccelGroup *accel=gtk_accel_group_new();

  if (!city_dialogs_have_been_initialised)
    initialize_city_dialogs();

  pdialog=fc_malloc(sizeof(struct city_dialog));
  pdialog->pcity=pcity;
  pdialog->change_shell=0;

  pdialog->support_unit_boxes=fc_calloc(NUM_UNITS_SHOWN, sizeof(GtkWidget *));
  pdialog->support_unit_pixmaps=fc_calloc(NUM_UNITS_SHOWN, sizeof(GtkWidget *));
  pdialog->present_unit_boxes=fc_calloc(NUM_UNITS_SHOWN, sizeof(GtkWidget *));
  pdialog->present_unit_pixmaps=fc_calloc(NUM_UNITS_SHOWN, sizeof(GtkWidget *));

  pdialog->support_unit_ids=fc_calloc(NUM_UNITS_SHOWN, sizeof(int));
  pdialog->present_unit_ids=fc_calloc(NUM_UNITS_SHOWN, sizeof(int));


  pdialog->shell=gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(pdialog->shell),"delete_event",
	GTK_SIGNAL_FUNC(city_dialog_delete_callback),(gpointer)pdialog);
  gtk_window_set_position(GTK_WINDOW(pdialog->shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(pdialog->shell));
  gtk_widget_set_name(pdialog->shell, "Freeciv");

  gtk_window_set_title(GTK_WINDOW(pdialog->shell), pcity->name);
  gtk_widget_set_usize(pdialog->shell, CITY_DIALOG_WIDTH, CITY_DIALOG_HEIGHT);

  gtk_widget_realize(pdialog->shell);

  if(!icon_bitmap)
    icon_bitmap=gdk_bitmap_create_from_data(root_window, cityicon_bits,
					    cityicon_width, cityicon_height);
  gdk_window_set_icon(pdialog->shell->window, NULL, icon_bitmap, icon_bitmap);

  gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(pdialog->shell)->vbox),5);
  gtk_widget_realize(GTK_DIALOG(pdialog->shell)->vbox);

  /* "cityname" frame */
  pdialog->cityname_label=gtk_frame_new("");
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->vbox),
	pdialog->cityname_label, TRUE, TRUE, 0);
  gtk_widget_realize(pdialog->cityname_label);

  vbox=gtk_vbox_new(FALSE, 5);
  gtk_container_add(GTK_CONTAINER(pdialog->cityname_label), vbox);
  gtk_widget_realize(vbox);

  gtk_container_border_width(GTK_CONTAINER(vbox), 5);

  /* "citizens" box */
  box=gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, FALSE, 0);

  pdialog->citizen_box = gtk_event_box_new();
  gtk_widget_set_events(pdialog->citizen_box, GDK_BUTTON_PRESS_MASK);
  gtk_box_pack_start(GTK_BOX(box), pdialog->citizen_box, FALSE, FALSE, 0);
  pdialog->citizen_pixmap = gtk_pixcomm_new(root_window,
					    SMALL_TILE_WIDTH * NUM_CITIZENS_SHOWN,
					    SMALL_TILE_HEIGHT);
  gtk_container_add(GTK_CONTAINER(pdialog->citizen_box),
		    pdialog->citizen_pixmap);
  gtk_signal_connect(GTK_OBJECT(pdialog->citizen_box),
		     "button_press_event", GTK_SIGNAL_FUNC(citizens_callback), pdialog);


  pdialog->sub_form=gtk_hbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->sub_form, TRUE, TRUE, 0);

  /* "city status" vbox */
  box=gtk_vbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(pdialog->sub_form), box, FALSE, FALSE, 15);

  pdialog->production_label=gtk_label_new("\n\n");
  gtk_box_pack_start(GTK_BOX(box),pdialog->production_label, TRUE, TRUE, 0);
  gtk_widget_set_name(pdialog->production_label, "city label");
  gtk_label_set_justify (GTK_LABEL (pdialog->production_label),
			 GTK_JUSTIFY_LEFT);

  pdialog->output_label=gtk_label_new("\n\n");
  gtk_box_pack_start(GTK_BOX(box),pdialog->output_label, TRUE, TRUE, 0);
  gtk_widget_set_name(pdialog->output_label, "city label");
  gtk_label_set_justify (GTK_LABEL (pdialog->output_label),
			 GTK_JUSTIFY_LEFT);

  pdialog->storage_label=gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(box),pdialog->storage_label, TRUE, TRUE, 0);
  gtk_widget_set_name(pdialog->storage_label, "city label");
  gtk_label_set_justify (GTK_LABEL (pdialog->storage_label),
			 GTK_JUSTIFY_LEFT);

  pdialog->pollution_label=gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(box),pdialog->pollution_label, TRUE, TRUE, 0);
  gtk_widget_set_name(pdialog->pollution_label, "city label");
  gtk_label_set_justify (GTK_LABEL (pdialog->pollution_label),
			 GTK_JUSTIFY_LEFT);
  
  /* "city map canvas" */
  box=gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(pdialog->sub_form), box, FALSE, FALSE, 5);

  frame=gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(box), frame, TRUE, FALSE, 0);

  pdialog->map_canvas = gtk_drawing_area_new();
  pdialog->map_canvas_store = gdk_pixmap_new(root_window,
					     canvas_width, canvas_height, -1);

  gtk_widget_set_events(pdialog->map_canvas,
	GDK_EXPOSURE_MASK|GDK_BUTTON_PRESS_MASK);

  gtk_drawing_area_size(GTK_DRAWING_AREA(pdialog->map_canvas),
			canvas_width, canvas_height);
  gtk_container_add(GTK_CONTAINER(frame), pdialog->map_canvas);
  
  gtk_widget_realize (pdialog->map_canvas);

  /* "production queue" vbox */
  box=gtk_vbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(pdialog->sub_form), box, TRUE, TRUE, 0);

  pdialog->building_label=gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(box), pdialog->building_label, FALSE, FALSE, 0);
  
  hbox=gtk_hbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(pdialog->building_label), hbox);

  pdialog->progress_label=gtk_progress_bar_new();
  gtk_progress_bar_set_bar_style(GTK_PROGRESS_BAR (pdialog->progress_label),
				 GTK_PROGRESS_CONTINUOUS);
  gtk_progress_set_show_text(GTK_PROGRESS (pdialog->progress_label), TRUE);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->progress_label, FALSE, FALSE, 1);

  pdialog->buy_command=gtk_accelbutton_new(_("_Buy"), accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->buy_command, TRUE, TRUE, 0);

  pdialog->change_command=gtk_accelbutton_new(_("_Change"), accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->change_command, TRUE, TRUE, 0);

  pdialog->buy_shell = NULL;
  pdialog->sell_shell = NULL;
  pdialog->rename_shell = NULL;

  pdialog->improvement_list=gtk_clist_new(1);
  gtk_clist_set_column_width(GTK_CLIST(pdialog->improvement_list), 0,
	GTK_CLIST(pdialog->improvement_list)->clist_window_width);
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), pdialog->improvement_list);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(box),scrolled, TRUE, TRUE, 0);

  hbox = gtk_hbox_new(TRUE, 0);
  pdialog->sell_command=gtk_accelbutton_new(_("_Sell"), accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->sell_command, FALSE, TRUE, 0);

  pdialog->worklist_command=gtk_accelbutton_new(_("_Worklist"), accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->worklist_command, 
			    FALSE, TRUE, 0);

  pdialog->worklist_label=gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->worklist_label, 
			    FALSE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);
  pdialog->worklist_shell = NULL;

  /* "supported units" frame */
  pdialog->support_unit_label=gtk_frame_new(_("Supported units"));
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->support_unit_label,
	FALSE, FALSE, 0);
  gtk_widget_realize(pdialog->support_unit_label);

  box=gtk_hbox_new(TRUE, 1);
  gtk_container_add(GTK_CONTAINER(pdialog->support_unit_label),box);
  gtk_widget_realize(box);

  for(i=0; i<NUM_UNITS_SHOWN; i++) {
    pdialog->support_unit_boxes[i]=gtk_event_box_new();
    gtk_widget_set_events(pdialog->support_unit_boxes[i],
	GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);

    gtk_box_pack_start(GTK_BOX(box), pdialog->support_unit_boxes[i],
		       TRUE, FALSE, 0);
    gtk_widget_show(pdialog->support_unit_boxes[i]);

    pdialog->support_unit_pixmaps[i]=
      gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH,
		      NORMAL_TILE_HEIGHT+NORMAL_TILE_HEIGHT/2);
    gtk_container_add(GTK_CONTAINER(pdialog->support_unit_boxes[i]),
	pdialog->support_unit_pixmaps[i]);

    pdialog->support_unit_ids[i]=-1;

    if(pcity->owner != game.player_idx)
      gtk_widget_set_sensitive(pdialog->support_unit_boxes[i], FALSE);    
  }

  vbox2=gtk_vbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(box), vbox2, TRUE, FALSE, 0);

  pdialog->support_unit_button[0]=gtk_button_new_with_label("<");
  gtk_box_pack_start(GTK_BOX(vbox2),
  	pdialog->support_unit_button[0], TRUE, TRUE, 0);

  pdialog->support_unit_button[1]=gtk_button_new_with_label(">");
  gtk_box_pack_start(GTK_BOX(vbox2),
  	pdialog->support_unit_button[1], TRUE, TRUE, 0);

  gtk_signal_connect(GTK_OBJECT(pdialog->support_unit_button[0]), "clicked",
	GTK_SIGNAL_FUNC(city_dialog_support_unit_pos_callback), pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->support_unit_button[1]), "clicked",
	GTK_SIGNAL_FUNC(city_dialog_support_unit_pos_callback), pdialog);

  pdialog->support_unit_pos=0;


  /* "present units" frame */
  pdialog->present_unit_label=gtk_frame_new(_("Units present"));
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->present_unit_label,
	FALSE, FALSE, 0);
  gtk_widget_realize(pdialog->present_unit_label);

  box=gtk_hbox_new(TRUE, 1);
  gtk_container_add(GTK_CONTAINER(pdialog->present_unit_label),box);
  gtk_widget_realize(box);

  for(i=0; i<NUM_UNITS_SHOWN; i++) {
    pdialog->present_unit_boxes[i]=gtk_event_box_new();
    gtk_widget_set_events(pdialog->present_unit_boxes[i],
	GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);

    gtk_box_pack_start(GTK_BOX(box), pdialog->present_unit_boxes[i],
		       TRUE, FALSE, 0);
    gtk_widget_show(pdialog->present_unit_boxes[i]);

    pdialog->present_unit_pixmaps[i]=
     gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
    gtk_container_add(GTK_CONTAINER(pdialog->present_unit_boxes[i]),
	pdialog->present_unit_pixmaps[i]);

    pdialog->present_unit_ids[i]=-1;

    if(pcity->owner != game.player_idx)
      gtk_widget_set_sensitive(pdialog->present_unit_boxes[i], FALSE);    
  }

  vbox2=gtk_vbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(box), vbox2, TRUE, FALSE, 0);

  pdialog->present_unit_button[0]=gtk_button_new_with_label("<");
  gtk_box_pack_start(GTK_BOX(vbox2),
  	pdialog->present_unit_button[0], TRUE, TRUE, 0);

  pdialog->present_unit_button[1]=gtk_button_new_with_label(">");
  gtk_box_pack_start(GTK_BOX(vbox2),
  	pdialog->present_unit_button[1], TRUE, TRUE, 0);

  gtk_signal_connect(GTK_OBJECT(pdialog->present_unit_button[0]), "clicked",
	GTK_SIGNAL_FUNC(city_dialog_present_unit_pos_callback), pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->present_unit_button[1]), "clicked",
	GTK_SIGNAL_FUNC(city_dialog_present_unit_pos_callback), pdialog);

  pdialog->present_unit_pos=0;


  /* "action area" buttons */
  pdialog->close_command=gtk_accelbutton_new(_("C_lose"), accel);
  GTK_WIDGET_SET_FLAGS(pdialog->close_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
	pdialog->close_command, TRUE, TRUE, 0);

  pdialog->rename_command=gtk_accelbutton_new(_("_Rename"), accel);
  GTK_WIDGET_SET_FLAGS(pdialog->rename_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
        pdialog->rename_command, TRUE, TRUE, 0);

  pdialog->trade_command=gtk_accelbutton_new(_("_Trade"), accel);
  GTK_WIDGET_SET_FLAGS(pdialog->trade_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
        pdialog->trade_command, TRUE, TRUE, 0);

  pdialog->activate_command=gtk_accelbutton_new(_("_Activate Units"), accel);
  GTK_WIDGET_SET_FLAGS(pdialog->activate_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
        pdialog->activate_command, TRUE, TRUE, 0);

  pdialog->show_units_command=gtk_accelbutton_new(_("_Unit List"), accel);
  GTK_WIDGET_SET_FLAGS(pdialog->show_units_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
        pdialog->show_units_command, TRUE, TRUE, 0);

  pdialog->cityopt_command=gtk_accelbutton_new(_("Con_figure"), accel);
  GTK_WIDGET_SET_FLAGS(pdialog->cityopt_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
        pdialog->cityopt_command, TRUE, TRUE, 0);

  gtk_signal_connect(GTK_OBJECT(pdialog->sell_command), "clicked",
	GTK_SIGNAL_FUNC(sell_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->buy_command), "clicked",
	GTK_SIGNAL_FUNC(buy_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->change_command), "clicked",
	GTK_SIGNAL_FUNC(change_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->worklist_command), "clicked",
	GTK_SIGNAL_FUNC(worklist_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->close_command), "clicked",
	GTK_SIGNAL_FUNC(close_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->rename_command), "clicked",
	GTK_SIGNAL_FUNC(rename_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->trade_command), "clicked",
	GTK_SIGNAL_FUNC(trade_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->activate_command), "clicked",
	GTK_SIGNAL_FUNC(activate_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->show_units_command), "clicked",
	GTK_SIGNAL_FUNC(show_units_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->cityopt_command), "clicked",
	GTK_SIGNAL_FUNC(cityopt_callback), pdialog);

  genlist_insert(&dialog_list, pdialog, 0);

  for(i=0; i<B_LAST+1; i++)
      pdialog->improvlist_names_ptrs[i]=0;

  refresh_city_dialog(pdialog->pcity);

  if(make_modal)
    gtk_widget_set_sensitive(top_vbox, FALSE);
  
  pdialog->is_modal=make_modal;
  gtk_widget_grab_default(pdialog->close_command);

  gtk_widget_show_all(GTK_DIALOG(pdialog->shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(pdialog->shell)->action_area);

  gtk_signal_connect(GTK_OBJECT(pdialog->map_canvas), "expose_event",
	GTK_SIGNAL_FUNC(city_map_canvas_expose), pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->map_canvas), "button_press_event",
	GTK_SIGNAL_FUNC(button_down_citymap), NULL);

  gtk_widget_add_accelerator(pdialog->close_command, "clicked",
	accel, GDK_Escape, 0, 0);

  return pdialog;
}

/****************************************************************
...
*****************************************************************/
static void activate_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;
  int x=pdialog->pcity->x,y=pdialog->pcity->y;
  struct unit_list *punit_list = &map_get_tile(x,y)->units;
  struct unit *pmyunit = NULL;

  if(unit_list_size(punit_list))  {
    unit_list_iterate((*punit_list), punit) {
      if(game.player_idx==punit->owner) {
	pmyunit = punit;
	request_new_unit_activity(punit, ACTIVITY_IDLE);
      }
    } unit_list_iterate_end;
    if (pmyunit)
      set_unit_focus(pmyunit);
  }
}

/****************************************************************
...
*****************************************************************/
static void show_units_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;
  struct tile *ptile = map_get_tile(pdialog->pcity->x, pdialog->pcity->y);

  if(unit_list_size(&ptile->units))
    popup_unit_select_dialog(ptile);
}

#ifdef UNUSED
/****************************************************************
...
*****************************************************************/
static void present_units_ok_callback(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}
#endif

/****************************************************************
...
*****************************************************************/
static void activate_unit(struct unit *punit)
{
  if((punit->activity!=ACTIVITY_IDLE || punit->ai.control)
     && can_unit_do_activity(punit, ACTIVITY_IDLE))
    request_new_unit_activity(punit, ACTIVITY_IDLE);
  set_unit_focus(punit);
}

/****************************************************************
...
*****************************************************************/
static void present_units_activate_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    activate_unit(punit);
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void present_units_activate_close_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  destroy_message_dialog(w);

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data))) {
    activate_unit(punit);
    if((pcity=map_get_city(punit->x, punit->y)))
      if((pdialog=get_city_dialog(pcity)))
       close_city_dialog(pdialog);
  }
}

/****************************************************************
...
*****************************************************************/
static void present_units_sentry_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    request_unit_sentry(punit);
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void present_units_fortify_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    request_unit_fortify(punit);
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void supported_units_activate_close_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  destroy_message_dialog(w);

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data))) {
    activate_unit(punit);
    if((pcity=player_find_city_by_id(game.player_ptr, punit->homecity)))
      if((pdialog=get_city_dialog(pcity)))
	close_city_dialog(pdialog);
  }
}

/****************************************************************
...
*****************************************************************/
static void present_units_disband_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    request_unit_disband(punit);

  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void present_units_homecity_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    request_unit_change_homecity(punit);

  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void present_units_cancel_callback(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static gint p_units_middle_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)) &&
     (pcity=map_get_city(punit->x, punit->y)) &&
     (pdialog=get_city_dialog(pcity)) && (ev->button==2 || ev->button==3)) {
    activate_unit(punit);
    if (ev->button==2)
      close_city_dialog(pdialog);
  }

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static gint s_units_middle_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)) &&
     (pcity=find_city_by_id(punit->homecity)) &&
     (pdialog=get_city_dialog(pcity)) && (ev->button==2 || ev->button==3)) {
    activate_unit(punit);
    if (ev->button==2)
      close_city_dialog(pdialog);
  }

  return TRUE;
}

/****************************************************************
Pop-up menu to change attributes of units, ex. change homecity.
*****************************************************************/
static gint present_units_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;
  GtkWidget *wd;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)) &&
     (pcity=map_get_city(punit->x, punit->y)) &&
     (pdialog=get_city_dialog(pcity))) {

    if (ev->button==2 || ev->button==3)
      return FALSE;

    wd=popup_message_dialog(pdialog->shell, 
			   /*"presentunitsdialog"*/_("Unit Commands"), 
			   unit_description(punit),
			   _("_Activate unit"),
			     present_units_activate_callback, punit->id,
			   _("Activate unit, _close dialog"),
			     present_units_activate_close_callback, punit->id,
			   _("_Sentry unit"),
			     present_units_sentry_callback, punit->id,
			   _("_Fortify unit"),
			     present_units_fortify_callback, punit->id,
			   _("_Disband unit"),
			     present_units_disband_callback, punit->id,
			   _("Make new _homecity"),
			     present_units_homecity_callback, punit->id,
			   _("_Upgrade unit"),
			     upgrade_callback, punit->id,
			   _("_Cancel"),
			     present_units_cancel_callback, 0, 
			   NULL);
    if (punit->activity == ACTIVITY_SENTRY
	|| !can_unit_do_activity(punit, ACTIVITY_SENTRY)) {
      message_dialog_button_set_sensitive(wd, "button2", FALSE);
    }
    if (punit->activity == ACTIVITY_FORTIFYING
	|| !can_unit_do_activity(punit, ACTIVITY_FORTIFYING)) {
      message_dialog_button_set_sensitive(wd, "button3", FALSE);
    }
    if (punit->homecity == pcity->id) {
      message_dialog_button_set_sensitive(wd, "button5", FALSE);
    }
    if (can_upgrade_unittype(game.player_ptr,punit->type) == -1) {
      message_dialog_button_set_sensitive(wd, "button6", FALSE);
    }
  }
  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static void rename_city_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;
  struct packet_city_request packet;

  if (pdialog) {
    packet.city_id=pdialog->pcity->id;
    packet.worklist.name[0] = '\0';
    sz_strlcpy(packet.name, input_dialog_get_input(w));
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_RENAME);

    pdialog->rename_shell = NULL;
  }

  input_dialog_destroy(w);
}

/****************************************************************
...
*****************************************************************/
static gint rename_callback_delete(GtkWidget *widget, GdkEvent *event,
				   gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;
  pdialog->rename_shell = NULL;
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
static void rename_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;

  pdialog=(struct city_dialog *)data;

  pdialog->rename_shell =
    input_dialog_create(pdialog->shell, 
			/*"shellrenamecity"*/_("Rename City"), 
			_("What should we rename the city to?"),
			pdialog->pcity->name,
			(void*)rename_city_callback, (gpointer)pdialog,
			(void*)rename_city_callback, (gpointer)pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->rename_shell), "delete_event",
    		     GTK_SIGNAL_FUNC(rename_callback_delete),
		     data);
}

/****************************************************************
...
*****************************************************************/
static void trade_message_dialog_callback(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void trade_callback(GtkWidget *w, gpointer data)
{
  int i;
  int x=0,total=0;
  char buf[512], *bptr=buf;
  int nleft = sizeof(buf);
  struct city_dialog *pdialog;

  pdialog=(struct city_dialog *)data;

  my_snprintf(buf, sizeof(buf),
	      _("These trade routes have been established with %s:\n"),
	      pdialog->pcity->name);
  bptr = end_of_strn(bptr, &nleft);
  
  for(i=0; i<4; i++)
    if(pdialog->pcity->trade[i]) {
      struct city *pcity;
      x=1;
      total+=pdialog->pcity->trade_value[i];
      if((pcity=find_city_by_id(pdialog->pcity->trade[i]))) {
	my_snprintf(bptr, nleft, _("%32s: %2d Trade/Year\n"),
		      pcity->name, pdialog->pcity->trade_value[i]);
	bptr = end_of_strn(bptr, &nleft);
      }
      else {
	my_snprintf(bptr, nleft, _("%32s: %2d Trade/Year\n"), _("Unknown"),
		      pdialog->pcity->trade_value[i]);
	bptr = end_of_strn(bptr, &nleft);
      }

    }
  if(!x) {
    mystrlcpy(bptr, _("No trade routes exist.\n"), nleft);
  } else {
    my_snprintf(bptr, nleft, _("\nTotal trade %d Trade/Year\n"), total);
  }

  popup_message_dialog(pdialog->shell, 
		       /*"citytradedialog"*/ _("Trade Routes"), 
		       buf, 
		       _("Done"), trade_message_dialog_callback, 0,
		       0);
}


/****************************************************************
...
*****************************************************************/
static void city_dialog_update_pollution(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  GtkStyle *s;
  int col;

  my_snprintf(buf, sizeof(buf), _("Pollution:   %3d"), pcity->pollution);

  gtk_set_label(pdialog->pollution_label, buf);

  s = gtk_style_copy (pdialog->pollution_label->style);

  col = COLOR_STD_BLACK;
  if (pcity->pollution > 0)
    col = COLOR_STD_RACE6;
  if (pcity->pollution >= 10)
    col = COLOR_STD_RED;

  s->fg[GTK_STATE_NORMAL] = *colors_standard[col];
  gtk_widget_set_style (pdialog->pollution_label, s);
}


/****************************************************************
...
*****************************************************************/
static void city_dialog_update_storage(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  
  my_snprintf(buf, sizeof(buf), _("Granary: %3d/%-3d"), pcity->food_stock,
	  city_granary_size(pcity->size));

  gtk_set_label(pdialog->storage_label, buf);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_building(struct city_dialog *pdialog)
{
  char buf[32], buf2[64];
  int turns;
  struct city *pcity=pdialog->pcity;
  gfloat pct;
  
  gtk_widget_set_sensitive(pdialog->buy_command, !pcity->did_buy);
  gtk_widget_set_sensitive(pdialog->sell_command, !pcity->did_sell);

  if(pcity->is_building_unit) {
    turns = city_turns_to_build (pcity, pcity->currently_building, TRUE);
    my_snprintf(buf, sizeof(buf),
 		concise_city_production ? "%3d/%3d:%3d" :
		  turns == 1 ? _("%3d/%3d %3d turn") : _("%3d/%3d %3d turns"),
 		pcity->shield_stock,
 		get_unit_type(pcity->currently_building)->build_cost,
 		turns);
    sz_strlcpy(buf2, get_unit_type(pcity->currently_building)->name);
    pct = (gfloat)pcity->shield_stock /
	  (get_unit_type(pcity->currently_building)->build_cost + 0.1);
    pct = CLAMP(pct, 0.0, 1.0);		
  } else {
    if(pcity->currently_building==B_CAPITAL)  {
      /* Capitalization is special, you can't buy it or finish making it */
      my_snprintf(buf, sizeof(buf),
		  concise_city_production ? "%3d/XXX:XXX" :
		    _("%3d/XXX XXX turns"),
		  pcity->shield_stock);
      gtk_widget_set_sensitive(pdialog->buy_command, FALSE);
      pct = 1.0;
    } else {
      turns = city_turns_to_build (pcity, pcity->currently_building, FALSE);
      my_snprintf(buf, sizeof(buf),
		  concise_city_production ? "%3d/%3d:%3d" :
		    turns == 1 ? _("%3d/%3d %3d turn") : _("%3d/%3d %3d turns"),
		  pcity->shield_stock, 
		  get_improvement_type(pcity->currently_building)->build_cost,
		  turns);
      pct = (gfloat)pcity->shield_stock /
	    (get_improvement_type(pcity->currently_building)->build_cost + 0.1);
      pct = CLAMP(pct, 0.0, 1.0);		
    }
    sz_strlcpy(buf2, get_impr_name_ex(pcity, pcity->currently_building));
  }    

  gtk_frame_set_label(GTK_FRAME(pdialog->building_label), buf2);
  gtk_progress_set_percentage(GTK_PROGRESS (pdialog->progress_label), pct);
  gtk_progress_set_format_string(GTK_PROGRESS (pdialog->progress_label), buf);

  gtk_set_label(pdialog->worklist_label,
		worklist_is_empty(pcity->worklist) ?
		_("(list empty)") :
		_("(in progress)"));
}


/****************************************************************
...
*****************************************************************/
static void city_dialog_update_production(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  
  my_snprintf(buf, sizeof(buf),
	  _("Food:    %2d (%+2d)\nProd:    %2d (%+2d)\nTrade:   %2d (%+2d)"),
	  pcity->food_prod, pcity->food_surplus,
	  pcity->shield_prod, pcity->shield_surplus,
	  pcity->trade_prod+pcity->corruption, pcity->trade_prod);

  gtk_set_label(pdialog->production_label, buf);
}
/****************************************************************
...
*****************************************************************/
static void city_dialog_update_output(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  
  my_snprintf(buf, sizeof(buf),
	  _("Gold:    %2d (%+2d)\nLuxury:  %2d\nScience: %2d"),
	  pcity->tax_total, city_gold_surplus(pcity),
	  pcity->luxury_total,
	  pcity->science_total);

  gtk_set_label(pdialog->output_label, buf);
}

/**************************************************************************
...
**************************************************************************/
static void city_get_canvas_xy(int map_x, int map_y, int *canvas_x, int *canvas_y)
{
  if (is_isometric) {
    int diff_xy;

    /* The line at y=0 isometric has constant x+y=1(tiles) */
    diff_xy = (map_x + map_y) - (1);
    *canvas_y = diff_xy/2 * NORMAL_TILE_HEIGHT + (diff_xy%2) * (NORMAL_TILE_HEIGHT/2);

    /* The line at x=0 isometric has constant x-y=-3(tiles) */
    diff_xy = map_x - map_y;
    *canvas_x = (diff_xy + 3) * NORMAL_TILE_WIDTH/2;
  } else {
    *canvas_x = map_x * NORMAL_TILE_WIDTH;
    *canvas_y = map_y * NORMAL_TILE_HEIGHT;
  }
}

/**************************************************************************
...
**************************************************************************/
static void city_get_map_xy(int canvas_x, int canvas_y, int *map_x, int *map_y)
{
  if (is_isometric) {
    *map_x = -2;
    *map_y = 2;

    /* first find an equivalent position on the left side of the screen. */
    *map_x += canvas_x/NORMAL_TILE_WIDTH;
    *map_y -= canvas_x/NORMAL_TILE_WIDTH;
    canvas_x %= NORMAL_TILE_WIDTH;

    /* Then move op to the top corner. */
    *map_x += canvas_y/NORMAL_TILE_HEIGHT;
    *map_y += canvas_y/NORMAL_TILE_HEIGHT;
    canvas_y %= NORMAL_TILE_HEIGHT;

    assert(NORMAL_TILE_WIDTH == 2*NORMAL_TILE_HEIGHT);
    canvas_y *= 2; /* now we have a square. */
    if (canvas_x + canvas_y > NORMAL_TILE_WIDTH/2) (*map_x)++;
    if (canvas_x + canvas_y > 3 * NORMAL_TILE_WIDTH/2) (*map_x)++;
    if (canvas_x - canvas_y > NORMAL_TILE_WIDTH/2)  (*map_y)--;
    if (canvas_y - canvas_x > NORMAL_TILE_WIDTH/2)  (*map_y)++;
  } else {
    *map_x = canvas_x/NORMAL_TILE_WIDTH;
    *map_y = canvas_y/NORMAL_TILE_HEIGHT;
  }
}

/****************************************************************
Isometric.
*****************************************************************/
static void city_dialog_update_map_iso(struct city_dialog *pdialog)
{
  struct city *pcity = pdialog->pcity;

  gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_BLACK]);

  /* First make it all black. */
  gdk_draw_rectangle(pdialog->map_canvas_store, fill_bg_gc, TRUE,
		     0, 0, canvas_width, canvas_height);

  /* This macro happens to iterate correct to draw the top tiles first,
   so getting the overlap right.
   Furthermore, we don't have to redraw fog on the part of a fogged tile
   that overlaps another fogged tile, as on the main map, as no tiles in
   the city radius can be fogged. */
  city_map_iterate(x, y) {
    int map_x = pcity->x + x - CITY_MAP_SIZE/2;
    int map_y = pcity->y + y - CITY_MAP_SIZE/2;
    if (normalize_map_pos(&map_x, &map_y)
	&& tile_is_known(map_x, map_y)) {
      int canvas_x, canvas_y;
      city_get_canvas_xy(x, y, &canvas_x, &canvas_y);
      put_one_tile_full(pdialog->map_canvas_store, map_x, map_y, 
			canvas_x, canvas_y, 1);
    }
  } city_map_iterate_end;
  /* We have to put the output afterwards or it will be covered. */
  city_map_iterate(x, y) {
    int map_x = pcity->x + x - 2;
    int map_y = pcity->y + y - 2;
    if (normalize_map_pos(&map_x, &map_y)
	&& tile_is_known(map_x, map_y)) {
      int canvas_x, canvas_y;
      city_get_canvas_xy(x, y, &canvas_x, &canvas_y);
      if (pcity->city_map[x][y]==C_TILE_WORKER) {
	put_city_tile_output(pdialog->map_canvas_store,
			     canvas_x, canvas_y,
			     city_get_food_tile(x, y, pcity),
			     city_get_shields_tile(x, y, pcity), 
			     city_get_trade_tile(x, y, pcity));
      }
    }
  } city_map_iterate_end;

  /* This sometimes will draw one of the lines on top of a city or
     unit pixmap. This should maybe be moved to put_one_tile_pixmap()
     to fix this, but maybe it wouldn't be a good idea because the
     lines would get obscured. */
  city_map_iterate(x, y) {
    int map_x = pcity->x + x - 2;
    int map_y = pcity->y + y - 2;
    if (normalize_map_pos(&map_x, &map_y)
	&& tile_is_known(map_x, map_y)) {
      int canvas_x, canvas_y;
      city_get_canvas_xy(x, y, &canvas_x, &canvas_y);
      if (pcity->city_map[x][y]==C_TILE_UNAVAILABLE) {
	pixmap_frame_tile_red(pdialog->map_canvas_store,
				     canvas_x, canvas_y);
      }
    }
  } city_map_iterate_end;

  /* draw to real window */
  gdk_draw_pixmap(pdialog->map_canvas->window, civ_gc, pdialog->map_canvas_store,
		  0, 0, 0, 0, canvas_width, canvas_height);
}

/****************************************************************
Non-isometric
*****************************************************************/
static void city_dialog_update_map_ovh(struct city_dialog *pdialog)
{
  int x, y;
  struct city *pcity=pdialog->pcity;
  for(y=0; y<CITY_MAP_SIZE; y++)
    for(x=0; x<CITY_MAP_SIZE; x++) {
      if(!(x==0 && y==0) && !(x==0 && y==CITY_MAP_SIZE-1) &&
	 !(x==CITY_MAP_SIZE-1 && y==0) && 
	 !(x==CITY_MAP_SIZE-1 && y==CITY_MAP_SIZE-1) &&
	 tile_is_known(pcity->x+x-CITY_MAP_SIZE/2, 
		       pcity->y+y-CITY_MAP_SIZE/2)) {
	pixmap_put_tile(pdialog->map_canvas_store,
			pcity->x+x-CITY_MAP_SIZE/2,
			pcity->y+y-CITY_MAP_SIZE/2,
			x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT, 1);
	if(pcity->city_map[x][y]==C_TILE_WORKER)
	  put_city_tile_output(pdialog->map_canvas_store,
			       x * NORMAL_TILE_WIDTH, y *NORMAL_TILE_HEIGHT,
			       city_get_food_tile(x, y, pcity),
			       city_get_shields_tile(x, y, pcity), 
			       city_get_trade_tile(x, y, pcity));
	else if(pcity->city_map[x][y]==C_TILE_UNAVAILABLE)
	  pixmap_frame_tile_red(pdialog->map_canvas_store,
				x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT);
      }
      else {
	pixmap_put_black_tile(pdialog->map_canvas_store,
			      x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT);
      }
    }

  /* draw to real window */
  gdk_draw_pixmap(pdialog->map_canvas->window, civ_gc, pdialog->map_canvas_store,
		  0, 0, 0, 0, canvas_width, canvas_height);
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_map(struct city_dialog *pdialog)
{
  if (is_isometric) {
    city_dialog_update_map_iso(pdialog);
  } else {
    city_dialog_update_map_ovh(pdialog);
  }
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_citizens(struct city_dialog *pdialog)
{
  int i, n;
  struct city *pcity = pdialog->pcity;
  int width;
  /* If there is not enough space we stack the icons. We draw from left to right.
     width is how far we go to the right for each drawn pixmap. The last icons is
     always drawn in full, ans so has reserved SMALL_TILE_WIDTH pixels. */
  if (pcity->size > 1) {
    width = MIN(SMALL_TILE_WIDTH,
		((NUM_CITIZENS_SHOWN-1) * SMALL_TILE_WIDTH)/(pcity->size-1));
  } else {
    width = SMALL_TILE_WIDTH;
  }
  pdialog->cwidth = width;

  gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->citizen_pixmap), TRUE);

  i = 0; /* tracks the number of the citizen we are currently placing. */
  for (n=0; n<pcity->ppl_happy[4]; n++, i++) {
    gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmap),
		       get_citizen_sprite(5 + i%2),
		       i*width, 0, TRUE);
  }
 
  for (n=0; n<pcity->ppl_content[4]; n++, i++) {
    gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmap),
		       get_citizen_sprite(3 + i%2),
		       i*width, 0, TRUE);
  }

  for (n=0; n<pcity->ppl_unhappy[4]; n++, i++) {
    gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmap),
		       get_citizen_sprite(7 + i%2),
		       i*width, 0, TRUE);
  }

  pdialog->first_elvis = i;
  for (n=0; n<pcity->ppl_elvis; n++, i++) {
    gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmap),
		       get_citizen_sprite(0),
		       i*width, 0, TRUE);
  }

  pdialog->first_scientist = i;
  for (n=0; n<pcity->ppl_scientist; n++, i++) {
    gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmap),
		       get_citizen_sprite(1),
		       i*width, 0, TRUE);
  }

  pdialog->first_taxman = i;
  for (n=0; n<pcity->ppl_taxman; n++, i++) {
    gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmap),
		       get_citizen_sprite(2),
		       i*width, 0, TRUE);
  }
}

/****************************************************************
Somebody clicked our list of citizens. If they clicked a specialist
then change the type of him, else do nothing.
*****************************************************************/
static void citizens_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;
  struct city *pcity = pdialog->pcity;
  struct packet_city_request packet;  
  int citnum;
  enum specialist_type type;

  if (ev->x > (pcity->size-1) * pdialog->cwidth + SMALL_TILE_WIDTH)
    return; /* no citizen that far to the right */

  citnum = MIN(pcity->size-1, ev->x/pdialog->cwidth);

  if (citnum >= pdialog->first_taxman) {
    type = SP_TAXMAN;
  } else if (citnum >= pdialog->first_scientist) {
    type = SP_SCIENTIST;
  } else if (citnum >= pdialog->first_elvis) {
    type = SP_ELVIS;
  } else {
    return;
  }
 
  packet.city_id = pdialog->pcity->id;
  packet.name[0] = '\0';
  packet.worklist.name[0] = '\0';
  packet.specialist_from = type;

  switch (type) {
  case SP_ELVIS:
    packet.specialist_to = SP_SCIENTIST;
    break;
  case SP_SCIENTIST:
    packet.specialist_to = SP_TAXMAN;
    break;
  case SP_TAXMAN:
    packet.specialist_to = SP_ELVIS;
    break;
  }

  send_packet_city_request(&aconnection, &packet,
 			   PACKET_CITY_CHANGE_SPECIALIST);
}

/****************************************************************
...
*****************************************************************/
static gint support_units_callback(GtkWidget *w, GdkEventButton *ev,
				   gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)) &&
     (pcity=find_city_by_id(punit->homecity)) &&
     (pdialog=get_city_dialog(pcity))) {

    if (ev->button==2 || ev->button==3)
      return FALSE;

    popup_message_dialog(pdialog->shell,
    	   /*"supportunitsdialog"*/ _("Unit Commands"), 
    	   unit_description(punit),
    	   _("_Activate unit"),
    	     present_units_activate_callback, punit->id,
    	   _("Activate unit, _close dialog"),
    	     supported_units_activate_close_callback, punit->id, /* act+c */
    	   _("_Disband unit"),
    	     present_units_disband_callback, punit->id,
    	   _("_Cancel"),
    	     present_units_cancel_callback, 0, 0);
    }
  return TRUE;
}


/****************************************************************
...
*****************************************************************/
static void city_dialog_update_supported_units(struct city_dialog *pdialog, 
					       int unitid)
{
  int i;
  struct unit_list *plist;
  struct genlist_iterator myiter;
  struct unit *punit;
  int size;

  if(unitid) {
    for(i=0; i<NUM_UNITS_SHOWN; i++)
      if(pdialog->support_unit_ids[i]==unitid)
	break;
    if(i==NUM_UNITS_SHOWN)
      unitid=0;
  }

  if(pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_supported);
  } else {
    plist = &(pdialog->pcity->units_supported);
  }

  size=plist->list.nelements/NUM_UNITS_SHOWN;
  if (size<=0 || pdialog->support_unit_pos>size) {
    pdialog->support_unit_pos=0;
  }

  gtk_widget_set_sensitive(pdialog->support_unit_button[0],
  	(pdialog->support_unit_pos>0));
  gtk_widget_set_sensitive(pdialog->support_unit_button[1],
  	(pdialog->support_unit_pos<size));

  genlist_iterator_init(&myiter, &(plist->list),
  	pdialog->support_unit_pos*NUM_UNITS_SHOWN);

  for(i=0;i<NUM_UNITS_SHOWN&&ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter),i++) {
    punit=(struct unit*)ITERATOR_PTR(myiter);
        
    if(unitid && punit->id!=unitid)
      continue;

    if (flags_are_transparent)
      gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->support_unit_pixmaps[i]), FALSE);

    put_unit_gpixmap(punit, GTK_PIXCOMM(pdialog->support_unit_pixmaps[i]));
    put_unit_gpixmap_city_overlays(punit, 
				  GTK_PIXCOMM(pdialog->support_unit_pixmaps[i]));

    gtk_pixcomm_changed(GTK_PIXCOMM(pdialog->support_unit_pixmaps[i]));

    pdialog->support_unit_ids[i]=punit->id;

    gtk_signal_handlers_destroy(GTK_OBJECT(pdialog->support_unit_boxes[i]));
    gtk_signal_connect(GTK_OBJECT(pdialog->support_unit_boxes[i]),
	"button_press_event",
	GTK_SIGNAL_FUNC(support_units_callback), (gpointer)punit->id);
    gtk_signal_connect(GTK_OBJECT(pdialog->support_unit_boxes[i]),
	"button_release_event",
	GTK_SIGNAL_FUNC(s_units_middle_callback), (gpointer)punit->id);
    gtk_widget_set_sensitive(pdialog->support_unit_boxes[i], TRUE);
  }
    
  for(; i<NUM_UNITS_SHOWN; i++) {
    gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->support_unit_pixmaps[i]), TRUE);
    pdialog->support_unit_ids[i]=0;
    gtk_widget_set_sensitive(pdialog->support_unit_boxes[i], FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_present_units(struct city_dialog *pdialog, int unitid)
{
  int i;
  struct unit_list *plist;
  struct genlist_iterator myiter;
  struct unit *punit;
  int size;
  
  if(unitid) {
    for(i=0; i<NUM_UNITS_SHOWN; i++)
      if(pdialog->present_unit_ids[i]==unitid)
	break;
    if(i==NUM_UNITS_SHOWN)
      unitid=0;
  }

  if(pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_present);
  } else {
    plist = &(map_get_tile(pdialog->pcity->x, pdialog->pcity->y)->units);
  }

  size=plist->list.nelements/NUM_UNITS_SHOWN;
  if (size<=0 || pdialog->present_unit_pos>size) {
    pdialog->present_unit_pos=0;
  }

  gtk_widget_set_sensitive(pdialog->present_unit_button[0],
  	(pdialog->present_unit_pos>0));
  gtk_widget_set_sensitive(pdialog->present_unit_button[1],
  	(pdialog->present_unit_pos<size));

  genlist_iterator_init(&myiter, &(plist->list),
  	pdialog->present_unit_pos*NUM_UNITS_SHOWN);

  for(i=0; i<NUM_UNITS_SHOWN&&ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter),i++) {
    punit=(struct unit*)ITERATOR_PTR(myiter);
    
    if(unitid && punit->id!=unitid)
      continue;

    if (flags_are_transparent)
      gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->present_unit_pixmaps[i]), FALSE);

    put_unit_gpixmap(punit, GTK_PIXCOMM(pdialog->present_unit_pixmaps[i]));

    gtk_pixcomm_changed(GTK_PIXCOMM(pdialog->present_unit_pixmaps[i]));

    pdialog->present_unit_ids[i]=punit->id;

    gtk_signal_handlers_destroy(GTK_OBJECT(pdialog->present_unit_boxes[i]));
    gtk_signal_connect(GTK_OBJECT(pdialog->present_unit_boxes[i]),
	"button_press_event",
	GTK_SIGNAL_FUNC(present_units_callback), (gpointer)punit->id);
    gtk_signal_connect(GTK_OBJECT(pdialog->present_unit_boxes[i]),
	"button_release_event",
	GTK_SIGNAL_FUNC(p_units_middle_callback), (gpointer)punit->id);
    gtk_widget_set_sensitive(pdialog->present_unit_boxes[i], TRUE);
  }
  for(; i<NUM_UNITS_SHOWN; i++) {
    gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->present_unit_pixmaps[i]), TRUE);
    pdialog->present_unit_ids[i]=0;
    gtk_widget_set_sensitive(pdialog->present_unit_boxes[i], FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_title(struct city_dialog *pdialog)
{
  char buf[512];
  char *now;
  
  my_snprintf(buf, sizeof(buf), _("%s - %s citizens"),
	  pdialog->pcity->name, int_to_text(city_population(pdialog->pcity)));

  now=GTK_FRAME(pdialog->cityname_label)->label;
  if (strcmp(now, buf)) {
    gtk_frame_set_label(GTK_FRAME(pdialog->cityname_label), buf);
    gtk_window_set_title(GTK_WINDOW(pdialog->shell), buf);
  }
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_improvement_list(struct city_dialog *pdialog)
{
  int i, n, flag;

  for(i=0, n=0, flag=0; i<game.num_impr_types; ++i)
    if(pdialog->pcity->improvements[i]) {
      if(!pdialog->improvlist_names_ptrs[n] ||
	 strcmp(pdialog->improvlist_names_ptrs[n],
		get_impr_name_ex(pdialog->pcity, i)))
	flag=1;
      sz_strlcpy(pdialog->improvlist_names[n],
		 get_impr_name_ex(pdialog->pcity, i));
      pdialog->improvlist_names_ptrs[n]=pdialog->improvlist_names[n];
      n++;
    }
  
  if(pdialog->improvlist_names_ptrs[n]!=0) {
    pdialog->improvlist_names_ptrs[n]=0;
    flag=1;
  }
  
  if(flag || n==0) {
    gtk_clist_freeze(GTK_CLIST(pdialog->improvement_list));
    gtk_clist_clear(GTK_CLIST(pdialog->improvement_list));

    for (i=0; i<n; i++)
      gtk_clist_append(GTK_CLIST(pdialog->improvement_list),
	&pdialog->improvlist_names_ptrs[i]);

    gtk_clist_thaw(GTK_CLIST(pdialog->improvement_list));
  }
}

/**************************************************************************
...
**************************************************************************/
gint button_down_citymap(GtkWidget *w, GdkEventButton *ev)
{
  struct genlist_iterator myiter;
  struct city *pcity;

  genlist_iterator_init(&myiter, &dialog_list, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct city_dialog *)ITERATOR_PTR(myiter))->map_canvas==w)
      break;

  if((pcity=((struct city_dialog *)ITERATOR_PTR(myiter))->pcity)) {
    int xtile, ytile;
    struct packet_city_request packet;

    city_get_map_xy(ev->x, ev->y, &xtile, &ytile);
    packet.city_id=pcity->id;
    packet.worker_x=xtile;
    packet.worker_y=ytile;
    packet.name[0]='\0';
    packet.worklist.name[0] = '\0';
    
    if(pcity->city_map[xtile][ytile]==C_TILE_WORKER)
      send_packet_city_request(&aconnection, &packet, 
			       PACKET_CITY_MAKE_SPECIALIST);
    else if(pcity->city_map[xtile][ytile]==C_TILE_EMPTY)
      send_packet_city_request(&aconnection, &packet, PACKET_CITY_MAKE_WORKER);
  }
  return TRUE;
}


/****************************************************************
...
*****************************************************************/
static void buy_callback_yes(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;

  pdialog=(struct city_dialog *)data;

  packet.city_id=pdialog->pcity->id;
  packet.name[0]='\0';
  packet.worklist.name[0] = '\0';
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_BUY);

  destroy_message_dialog(w);
  pdialog->buy_shell=NULL;
}

/****************************************************************
...
*****************************************************************/
static void buy_callback_no(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog=(struct city_dialog *)data;
  destroy_message_dialog(w);
  pdialog->buy_shell=NULL;
}

/****************************************************************
...
*****************************************************************/
static gint buy_callback_delete(GtkWidget *widget, GdkEvent *event,
				gpointer data)
{
  struct city_dialog *pdialog=(struct city_dialog *)data;
  pdialog->buy_shell=NULL;
  return FALSE;
}


/****************************************************************
...
*****************************************************************/
static void buy_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  int value;
  char *name;
  char buf[512];
  
  pdialog=(struct city_dialog *)data;

  if(pdialog->pcity->is_building_unit) {
    name=get_unit_type(pdialog->pcity->currently_building)->name;
  }
  else {
    name=get_impr_name_ex(pdialog->pcity, pdialog->pcity->currently_building);
  }
  value=city_buy_cost(pdialog->pcity);

  if(game.player_ptr->economic.gold>=value) {
    my_snprintf(buf, sizeof(buf),
	    _("Buy %s for %d gold?\nTreasury contains %d gold."), 
	    name, value, game.player_ptr->economic.gold);

    pdialog->buy_shell=
    popup_message_dialog(pdialog->shell, /*"buydialog"*/ _("Buy It!"), buf,
			 _("_Yes"), buy_callback_yes, pdialog,
			 _("_No"), buy_callback_no, pdialog, 0);
  }
  else {
    my_snprintf(buf, sizeof(buf),
	    _("%s costs %d gold.\nTreasury contains %d gold."), 
	    name, value, game.player_ptr->economic.gold);

    pdialog->buy_shell=
    popup_message_dialog(pdialog->shell, /*"buynodialog"*/ _("Buy It!"), buf,
			 _("Darn"), buy_callback_no, pdialog, 0);
  }

  gtk_signal_connect(GTK_OBJECT(pdialog->buy_shell), "delete_event",
    		     GTK_SIGNAL_FUNC(buy_callback_delete),
		     data);
}


/****************************************************************
...
*****************************************************************/
static void unitupgrade_callback_yes(GtkWidget *w, gpointer data)
{
  struct unit *punit;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data))) {
    request_unit_upgrade(punit);
  }
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void unitupgrade_callback_no(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void upgrade_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  char buf[512];
  int ut1,ut2;
  int value;

  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data))) {
    ut1=punit->type;
    /* printf("upgrade_callback for %s\n", unit_types[ut1].name); */

    ut2=can_upgrade_unittype(game.player_ptr,ut1);

    if (ut2==-1) {
      /* this shouldn't generally happen, but it is conceivable */
      my_snprintf(buf, sizeof(buf),
		  _("Sorry: cannot upgrade %s."), unit_types[ut1].name);
      popup_message_dialog(top_vbox, /*"upgradenodialog"*/_("Upgrade Unit!"), buf,
			   _("Darn"), unitupgrade_callback_no, 0,
			   NULL);
    } else {
      value=unit_upgrade_price(game.player_ptr, ut1, ut2);

      if(game.player_ptr->economic.gold>=value) {
	my_snprintf(buf, sizeof(buf), _("Upgrade %s to %s for %d gold?\n"
	       "Treasury contains %d gold."),
	       unit_types[ut1].name, unit_types[ut2].name,
	       value, game.player_ptr->economic.gold);
	popup_message_dialog(top_vbox, 
			     /*"upgradedialog"*/_("Upgrade Obsolete Units"), buf,
			     _("_Yes"),
			       unitupgrade_callback_yes, (gpointer)(punit->id),
			     _("_No"),
			       unitupgrade_callback_no, 0,
			     NULL);
      } else {
	my_snprintf(buf, sizeof(buf), _("Upgrading %s to %s costs %d gold.\n"
	       "Treasury contains %d gold."),
	       unit_types[ut1].name, unit_types[ut2].name,
	       value, game.player_ptr->economic.gold);
	popup_message_dialog(top_vbox,
			     /*"upgradenodialog"*/_("Upgrade Unit!"), buf,
			     _("Darn"), unitupgrade_callback_no, 0,
			     NULL);
      }
    }
    destroy_message_dialog(w);
  }
}


/****************************************************************
...
*****************************************************************/
static void change_to_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  GList *selection;

  pdialog=(struct city_dialog *)data;

  if((selection=GTK_CLIST(pdialog->change_list)->selection)) {
    struct packet_city_request packet;
    gint row=(gint)selection->data;

    packet.city_id=pdialog->pcity->id;
    packet.name[0]='\0';
    packet.worklist.name[0] = '\0';
    packet.build_id=pdialog->change_list_ids[row];
    packet.is_build_id_unit_id=
      (row >= pdialog->change_list_num_improvements);
    
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
  }
  gtk_widget_destroy(w->parent->parent->parent);
  pdialog->change_shell=0;
  gtk_widget_set_sensitive(pdialog->shell, TRUE);
}

/****************************************************************
...
*****************************************************************/
static void change_no_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  
  pdialog=(struct city_dialog *)data;
  
  gtk_widget_destroy(w->parent->parent->parent);
  pdialog->change_shell=0;
  gtk_widget_set_sensitive(pdialog->shell, TRUE);
}

/****************************************************************
...
*****************************************************************/
static void change_help_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  GList *selection;
  
  pdialog=(struct city_dialog *)data;

  if((selection=GTK_CLIST(pdialog->change_list)->selection)) {
    char *text;
    gint row = (gint)selection->data;
    int idx = pdialog->change_list_ids[row];
    int is_unit = (row >= pdialog->change_list_num_improvements);
    
    gtk_clist_get_text(GTK_CLIST(pdialog->change_list), row, 0, &text);

    if (is_unit) {
      popup_help_dialog_typed(get_unit_type(idx)->name, HELP_UNIT);
    } else if(is_wonder(idx)) {
      popup_help_dialog_typed(get_improvement_name(idx), HELP_WONDER);
    } else {
      popup_help_dialog_typed(get_improvement_name(idx), HELP_IMPROVEMENT);
    }
  }
  else
    popup_help_dialog_string(HELP_IMPROVEMENTS_ITEM);
}

/****************************************************************
...
*****************************************************************/
static gint change_deleted_callback(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  struct city_dialog *pdialog;
  
  pdialog=(struct city_dialog *)data;
  
  pdialog->change_shell=0;
  gtk_widget_set_sensitive(pdialog->shell, TRUE);
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
static void change_list_callback(GtkWidget *w, gint row, gint col, GdkEvent *ev, gpointer data)
     /* Allows new production options to be selected via a double-click */
{
  if(ev && ev->type==GDK_2BUTTON_PRESS)
    change_to_callback(w, data);
}

/****************************************************************
...
*****************************************************************/
static void change_callback(GtkWidget *w, gpointer data)
{
  GtkWidget *cshell, *button, *scrolled;
  struct city_dialog *pdialog;
  int i, n, turns;
  static gchar *title_[4] = { N_("Type"), N_("Info"), N_("Cost"), N_("Turns") };
  static gchar **title = NULL;
  GtkAccelGroup *accel=gtk_accel_group_new();
  char *row   [4];
  char  buf   [4][64];

  if (!title) title = intl_slist(4, title_);
  
  pdialog=(struct city_dialog *)data;
  
  cshell=gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(cshell),"delete_event",
	GTK_SIGNAL_FUNC(change_deleted_callback),pdialog);
  pdialog->change_shell=cshell;

  gtk_window_set_position (GTK_WINDOW(cshell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(cshell));
  gtk_window_set_title(GTK_WINDOW(cshell), _("Change Production"));

  pdialog->change_list=gtk_clist_new_with_titles(4, title);
  gtk_clist_column_titles_passive(GTK_CLIST(pdialog->change_list));
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), pdialog->change_list);

  for (i=0; i<4; i++)
    gtk_clist_set_column_auto_resize(GTK_CLIST (pdialog->change_list), i, TRUE);

  gtk_clist_set_column_justification(GTK_CLIST (pdialog->change_list), 2,
				     GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_justification(GTK_CLIST (pdialog->change_list), 3,
				     GTK_JUSTIFY_RIGHT);

  /* Set up the doubleclick-on-list-item handler */
  gtk_signal_connect(GTK_OBJECT(pdialog->change_list), "select_row",
		     GTK_SIGNAL_FUNC(change_list_callback), pdialog);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(scrolled, 310, 350);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(cshell)->vbox), scrolled,
	TRUE, TRUE, 0);
  
  button=gtk_accelbutton_new(_("_Change"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(cshell)->action_area), button,
	TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
	GTK_SIGNAL_FUNC(change_to_callback), pdialog);

  gtk_widget_add_accelerator(button, "clicked", accel,
			     GDK_Return, 0, 0);

  button=gtk_accelbutton_new(_("Ca_ncel"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(cshell)->action_area), button,
	TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
	GTK_SIGNAL_FUNC(change_no_callback), pdialog);

  gtk_widget_add_accelerator(button, "clicked", accel,
			     GDK_Escape, 0, 0);

  button=gtk_accelbutton_new(_("_Help"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(cshell)->action_area),
	button, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
	GTK_SIGNAL_FUNC(change_help_callback), pdialog);

  gtk_widget_set_sensitive(pdialog->shell, FALSE);

  for(i=0; i<4; i++)
    row[i]=buf[i];

  gtk_clist_freeze(GTK_CLIST(pdialog->change_list));
  gtk_clist_clear(GTK_CLIST(pdialog->change_list));

  for(i=0, n=0; i<game.num_impr_types; i++)
    if(can_build_improvement(pdialog->pcity, i)) {
      if (i==B_CAPITAL) { /* Total & turns left meaningless on capitalization */
	my_snprintf(buf[0], sizeof(buf[0]), get_improvement_type(i)->name);
	buf[1][0]='\0';
	my_snprintf(buf[2], sizeof(buf[2]), "--");
	my_snprintf(buf[3], sizeof(buf[3]), "--");
      } else {
	turns = city_turns_to_build (pdialog->pcity, i, FALSE);
	my_snprintf(buf[0], sizeof(buf[0]), get_improvement_type(i)->name);

        /* from city.c get_impr_name_ex() */
        if (wonder_replacement(pdialog->pcity, i))
        {
	  my_snprintf(buf[1], sizeof(buf[1]), "*");
        }
        else
        {
	  char *state = "";

          if (is_wonder(i))
          {
	    state = _("Wonder");
            if (game.global_wonders[i])	state = _("Built");
            if (wonder_obsolete(i))	state = _("Obsolete");
          }
	  my_snprintf(buf[1], sizeof(buf[1]), state);
        }

	my_snprintf(buf[2], sizeof(buf[2]), "%d",
		    get_improvement_type(i)->build_cost);
	my_snprintf(buf[3], sizeof(buf[3]), "%d",
		    turns);
      }
      gtk_clist_append(GTK_CLIST(pdialog->change_list), row);
      pdialog->change_list_ids[n++]=i;
    }
  
  pdialog->change_list_num_improvements=n;


  for(i=0; i<game.num_unit_types; i++)
    if(can_build_unit(pdialog->pcity, i)) {
      struct unit_type *ptype;

      turns = city_turns_to_build (pdialog->pcity, i, TRUE);
      my_snprintf(buf[0], sizeof(buf[0]), unit_name(i));
      
      /* from unit.h get_unit_name() */
      ptype = get_unit_type(i);
      if (ptype->fuel)
	my_snprintf(buf[1], sizeof(buf[1]), "%d/%d/%d(%d)",
		    ptype->attack_strength, ptype->defense_strength,
		    ptype->move_rate / 3, (ptype->move_rate / 3) * ptype->fuel);
      else
        my_snprintf(buf[1], sizeof(buf[1]), "%d/%d/%d",
		    ptype->attack_strength, ptype->defense_strength,
		    ptype->move_rate / 3);
      my_snprintf(buf[2], sizeof(buf[2]), "%d", get_unit_type(i)->build_cost);
      my_snprintf(buf[3], sizeof(buf[3]), "%d", turns);
      gtk_clist_append(GTK_CLIST(pdialog->change_list), row);
      pdialog->change_list_ids[n++]=i;
    }

  gtk_clist_thaw(GTK_CLIST(pdialog->change_list));

  gtk_widget_show_all(GTK_DIALOG(cshell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(cshell)->action_area);

  gtk_widget_show(cshell);
}


/****************************************************************
  Display the city's worklist.
*****************************************************************/
static void worklist_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  
  pdialog = (struct city_dialog *)data;

  if (pdialog->worklist_shell) {
    gtk_set_relative_position(pdialog->shell, pdialog->worklist_shell,
			      20, 20);
    gtk_widget_show(pdialog->worklist_shell);
  } else {
    pdialog->worklist_shell = 
      popup_worklist(pdialog->pcity->worklist, pdialog->pcity, pdialog->shell, 
		     (void *)pdialog, commit_city_worklist, 
		     cancel_city_worklist);
  }
}

/****************************************************************
  Commit the changes to the worklist for the city.
*****************************************************************/
static void commit_city_worklist(struct worklist *pwl, void *data)
{
  struct packet_city_request packet;
  struct city_dialog *pdialog = (struct city_dialog *)data;
  int i, k, id, is_unit;

  /* Update the worklist.  Remember, though -- the current build 
     target really isn't in the worklist; don't send it to the 
     server as part of the worklist.  Of course, we have to
     search through the current worklist to find the first
     _now_available_ build target (to cope with players who try
     mean things like adding a Battleship to a city worklist when
     the player doesn't even yet have the Map Making tech).  */

  for (k = 0; k < MAX_LEN_WORKLIST; k++) {
    int same_as_current_build;
    if (! worklist_peek_ith(pwl, &id, &is_unit, k))
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
    if (( is_unit && can_build_unit(pdialog->pcity, id)) ||
	(!is_unit && can_build_improvement(pdialog->pcity, id))) {
      /* ...but we're not yet building it, then switch. */
      if (!same_as_current_build) {

	/* Change the current target */
	packet.city_id=pdialog->pcity->id;
	packet.name[0] = '\0';
	packet.worklist.name[0] = '\0';
	packet.build_id = id;
	packet.is_build_id_unit_id = is_unit;
	send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
      }

      /* This item is now (and may have always been) the current
	 build target.  Drop it out of the worklist. */
      worklist_remove(pwl, k);
      break;
    }
  }

  /* Send the rest of the worklist on its way. */
  packet.city_id=pdialog->pcity->id;
  packet.name[0] = '\0';
  packet.worklist.name[0] = '\0';
  packet.worklist.is_valid = 1;
  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    packet.worklist.wlefs[i] = pwl->wlefs[i];
    packet.worklist.wlids[i] = pwl->wlids[i];
  }
    
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_WORKLIST);

  pdialog->worklist_shell = NULL;
}


/****************************************************************
...
*****************************************************************/
static void cancel_city_worklist(void *data) {
  struct city_dialog *pdialog = (struct city_dialog *)data;
  pdialog->worklist_shell = NULL;
}


/****************************************************************
...
*****************************************************************/
static void sell_callback_yes(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;

  pdialog=(struct city_dialog *)data;

  packet.city_id=pdialog->pcity->id;
  packet.build_id=pdialog->sell_id;
  packet.name[0]='\0';
  packet.worklist.name[0] = '\0';
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_SELL);

  destroy_message_dialog(w);
  pdialog->sell_shell=NULL;
}

/****************************************************************
...
*****************************************************************/
static void sell_callback_no(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog=(struct city_dialog *)data;
  destroy_message_dialog(w);
  pdialog->sell_shell=NULL;
}

/****************************************************************
...
*****************************************************************/
static gint sell_callback_delete(GtkWidget *widget, GdkEvent *event,
				gpointer data)
{
  struct city_dialog *pdialog=(struct city_dialog *)data;
  pdialog->sell_shell=NULL;
  return FALSE;
}


/****************************************************************
...
*****************************************************************/
static void sell_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  GList *selection;
  pdialog=(struct city_dialog *)data;

  if((selection=GTK_CLIST(pdialog->improvement_list)->selection)) {
    int i, n;
    gint row=(gint)selection->data;

    for(i=0, n=0; i<game.num_impr_types; i++)
      if(pdialog->pcity->improvements[i]) {
	if(n==row) {
	  char buf[512];
	  
	  if(is_wonder(i))
	    return;
	  
	  pdialog->sell_id=i;
	  my_snprintf(buf, sizeof(buf), _("Sell %s for %d gold?"), 
		  get_impr_name_ex(pdialog->pcity, i),
		  improvement_value(i));

	  pdialog->sell_shell=
	  popup_message_dialog(pdialog->shell, /*"selldialog"*/ _("Sell It!"), buf,
			       _("_Yes"), sell_callback_yes, pdialog,
			       _("_No"), sell_callback_no, pdialog, 0);
	  gtk_signal_connect(GTK_OBJECT(pdialog->sell_shell), "delete_event",
    		     GTK_SIGNAL_FUNC(sell_callback_delete),
		     data);
	  return;
	}
	n++;
      }
  }
}

/****************************************************************
...
*****************************************************************/
static void close_city_dialog(struct city_dialog *pdialog)
{
  gtk_widget_hide(pdialog->shell);
  genlist_unlink(&dialog_list, pdialog);

  if(pdialog->is_modal)
    gtk_widget_set_sensitive(top_vbox, TRUE);

  if (pdialog->worklist_shell)
    gtk_widget_destroy(pdialog->worklist_shell);

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
  } unit_list_iterate_end;
  unit_list_unlink_all(&(pdialog->pcity->info_units_supported));
  unit_list_iterate(pdialog->pcity->info_units_present, psunit) {
    free(psunit);
  } unit_list_iterate_end;
  unit_list_unlink_all(&(pdialog->pcity->info_units_present));

  gtk_widget_destroy(pdialog->shell);

  free(pdialog->support_unit_boxes);
  free(pdialog->support_unit_pixmaps);
  free(pdialog->present_unit_boxes);
  free(pdialog->present_unit_pixmaps);

  free(pdialog->support_unit_ids);
  free(pdialog->present_unit_ids);

  free(pdialog);
}

/****************************************************************
...
*****************************************************************/
static void close_callback(GtkWidget *w, gpointer data)
{
  close_city_dialog((struct city_dialog *)data);
}


/****************************************************************
								
 City Options dialog:  (current only auto-attack options)
 
Note, there can only be one such dialog at a time, because
I'm lazy.  That could be fixed, similar to way you can have
multiple city dialogs.

*****************************************************************/
								 
#define NUM_CITYOPT_TOGGLES 5

static GtkWidget *create_cityopt_dialog(char *city_name);
static void cityopt_ok_command_callback(GtkWidget *w, gpointer data);
static void cityopt_cancel_command_callback(GtkWidget *w, gpointer data);
static void cityopt_newcit_radio_callback(GtkWidget *w, gpointer data);

static char *ncitizen_labels[] = { N_("Elvises"), N_("Scientists"), N_("Taxmen") };

static GtkWidget *cityopt_shell = 0;
static GtkWidget *cityopt_radio[3];	/* cityopt_ncitizen_radio */
static GtkWidget *cityopt_toggles[NUM_CITYOPT_TOGGLES];
static int cityopt_city_id = 0;
static int ncitizen_idx;

/****************************************************************
...
*****************************************************************/
static void cityopt_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;
  struct city *pcity = pdialog->pcity;
  int i, state;

  if(cityopt_shell) {
    gtk_widget_destroy(cityopt_shell);
  }
  cityopt_shell=create_cityopt_dialog(pcity->name);

  for(i=0; i<NUM_CITYOPT_TOGGLES; i++) {
    state = (pcity->city_options & (1<<i));
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(cityopt_toggles[i]),
	state);
  }
  if (pcity->city_options & (1<<CITYO_NEW_EINSTEIN)) {
    ncitizen_idx = 1;
  } else if (pcity->city_options & (1<<CITYO_NEW_TAXMAN)) {
    ncitizen_idx = 2;
  } else {
    ncitizen_idx = 0;
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cityopt_radio[ncitizen_idx]),
			       TRUE);
  
  cityopt_city_id = pcity->id;

/*  gtk_set_relative_position(toplevel, cityopt_shell, 15, 15);*/
  gtk_widget_show(cityopt_shell);
}


/**************************************************************************
...
**************************************************************************/
static GtkWidget *create_cityopt_dialog(char *city_name)
{
  GtkWidget *shell, *label, *frame, *vbox, *ok, *cancel;
  GtkAccelGroup *accel=gtk_accel_group_new();
  GSList *group=NULL;
  int i;

  shell = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (shell), _("City Options"));
  gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(shell));

  label = gtk_label_new (city_name);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), label, FALSE, FALSE, 0);


  frame = gtk_frame_new(_("New citizens are"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), frame, FALSE, FALSE, 4);
  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  for (i=0; i<3; i++) {
    cityopt_radio[i]=gtk_radio_button_new_with_label(group,
						_(ncitizen_labels[i]));
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(cityopt_radio[i]), FALSE);
    group=gtk_radio_button_group(GTK_RADIO_BUTTON(cityopt_radio[i]));
    gtk_box_pack_start(GTK_BOX(vbox), cityopt_radio[i],
		       FALSE, FALSE, 0);
  }

  /* NOTE: the ordering here is deliberately out of order;
     want toggles[] to be in enum city_options order, but
     want display in different order. --dwp
     - disband and workers options at top
     - helicopters (special case air) at bottom
  */

  cityopt_toggles[4] = gtk_check_button_new_with_label
					(_("Disband if build settler at size 1"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(shell)->vbox),
	cityopt_toggles[4], FALSE, FALSE, 0);

  cityopt_toggles[0] = gtk_check_button_new_with_label
					(_("Auto-attack vs land units"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(shell)->vbox),
	cityopt_toggles[0], FALSE, FALSE, 0);

  cityopt_toggles[1] = gtk_check_button_new_with_label
					(_("Auto-attack vs sea units"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(shell)->vbox),
	cityopt_toggles[1], FALSE, FALSE, 0);

  cityopt_toggles[3] = gtk_check_button_new_with_label
					(_("Auto-attack vs air units"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(shell)->vbox),
	cityopt_toggles[3], FALSE, FALSE, 0);

  cityopt_toggles[2] = gtk_check_button_new_with_label
					(_("Auto-attack vs helicopters"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(shell)->vbox),
	cityopt_toggles[2], FALSE, FALSE, 0);

  ok = gtk_button_new_with_label (_("Ok"));
  gtk_widget_add_accelerator(ok, "clicked", accel, GDK_Return, 0, 0);
  cancel = gtk_button_new_with_label (_("Cancel"));
  gtk_widget_add_accelerator(cancel, "clicked", accel, GDK_Escape, 0, 0);

  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(shell)->action_area),
	ok, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(shell)->action_area),
	cancel, TRUE, TRUE, 0);

  gtk_signal_connect(GTK_OBJECT(ok), "clicked",
	GTK_SIGNAL_FUNC(cityopt_ok_command_callback), shell);
  gtk_signal_connect(GTK_OBJECT(cancel), "clicked",
	GTK_SIGNAL_FUNC(cityopt_cancel_command_callback), shell);

  for (i=0; i<3; i++) {
    gtk_signal_connect(GTK_OBJECT(cityopt_radio[i]), "toggled",
	GTK_SIGNAL_FUNC(cityopt_newcit_radio_callback), GINT_TO_POINTER(i));
  }
 
  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(shell)->action_area);

  return shell;
}
  
/**************************************************************************
...
**************************************************************************/
static void cityopt_cancel_command_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_destroy(cityopt_shell);
  cityopt_shell = 0;
}

/**************************************************************************
...
**************************************************************************/
static void cityopt_ok_command_callback(GtkWidget *w, gpointer data)
{
  struct city *pcity = find_city_by_id(cityopt_city_id);

  if (pcity) {
    struct packet_generic_values packet;
    int i, new_options;
    
    new_options = 0;
    for(i=0; i<NUM_CITYOPT_TOGGLES; i++)  {
      if (GTK_TOGGLE_BUTTON(cityopt_toggles[i])->active) new_options |= (1<<i);
    }
    if (ncitizen_idx == 1) {
      new_options |= (1<<CITYO_NEW_EINSTEIN);
    } else if (ncitizen_idx == 2) {
      new_options |= (1<<CITYO_NEW_TAXMAN);
    }
    packet.value1 = cityopt_city_id;
    packet.value2 = new_options;
    send_packet_generic_values(&aconnection, PACKET_CITY_OPTIONS,
			      &packet);
  }
  gtk_widget_destroy(cityopt_shell);
  cityopt_shell = 0;
}

/**************************************************************************
 Sets newcitizen_index based on the selected radio button.
**************************************************************************/
static void cityopt_newcit_radio_callback(GtkWidget *w, gpointer data)
{
  ncitizen_idx = GPOINTER_TO_INT(data);
}

/**************************************************************************
...
**************************************************************************/
static void popdown_cityopt_dialog(void)
{
  if(cityopt_shell) {
    gtk_widget_destroy(cityopt_shell);
    cityopt_shell = 0;
  }
}
