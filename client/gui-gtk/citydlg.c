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

#include "cityrep.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapview.h"
#include "repodlgs.h"
#include "tilespec.h"

#include "citydlg.h"

#include "cityicon.ico"

extern GtkWidget *toplevel, *map_canvas;
extern GdkWindow *root_window;
extern struct connection aconnection;
extern int map_view_x0, map_view_y0;
extern int flags_are_transparent;
extern GdkGC *fill_bg_gc;
extern GdkGC *civ_gc;

extern GtkStyle *city_dialog_style;

#define NUM_UNITS_SHOWN  12
#define NUM_CITIZENS_SHOWN 25


struct city_dialog {
  struct city *pcity;
  GtkWidget *shell;
  GtkWidget *main_form;
  GtkWidget *cityname_label;
  GtkWidget *citizen_boxes		[NUM_CITIZENS_SHOWN];
  GtkWidget *citizen_pixmaps		[NUM_CITIZENS_SHOWN];
  GtkWidget *production_label;
  GtkWidget *output_label;
  GtkWidget *storage_label;
  GtkWidget *pollution_label;
  GtkWidget *sub_form;
  GtkWidget *map_canvas;
  GtkWidget *sell_command;
  GtkWidget *close_command, *rename_command, *trade_command, *activate_command;
  GtkWidget *show_units_command, *cityopt_command;
  GtkWidget *building_label, *progress_label, *buy_command, *change_command;
  GtkWidget *improvement_viewport, *improvement_list;
  GtkWidget *support_unit_label;
  GtkWidget *support_unit_boxes		[NUM_UNITS_SHOWN];
  GtkWidget *support_unit_pixmaps	[NUM_UNITS_SHOWN];
  GtkWidget *present_unit_label;
  GtkWidget *present_unit_boxes		[NUM_UNITS_SHOWN];
  GtkWidget *present_unit_pixmaps	[NUM_UNITS_SHOWN];
  GtkWidget *change_list;
  GtkWidget *rename_input;
  
  enum improvement_type_id sell_id;
  
  int citizen_type[NUM_CITIZENS_SHOWN];
  int support_unit_ids[NUM_UNITS_SHOWN];
  int present_unit_ids[NUM_UNITS_SHOWN];
  char improvlist_names[B_LAST+1][64];
  char *improvlist_names_ptrs[B_LAST+1];
  
  char *change_list_names_ptrs[B_LAST+1+U_LAST+1+1];
  char change_list_names[B_LAST+1+U_LAST+1][200];
  int change_list_ids[B_LAST+1+U_LAST+1];
  int change_list_num_improvements;

  int is_modal;
};

static struct genlist dialog_list;
static int dialog_list_has_been_initialised;

struct city_dialog *get_city_dialog(struct city *pcity);
struct city_dialog *create_city_dialog(struct city *pcity, int make_modal);
void close_city_dialog(struct city_dialog *pdialog);

void city_dialog_update_improvement_list(struct city_dialog *pdialog);
void city_dialog_update_title(struct city_dialog *pdialog);
void city_dialog_update_supported_units(struct city_dialog *pdialog, int id);
void city_dialog_update_present_units(struct city_dialog *pdialog, int id);
void city_dialog_update_citizens(struct city_dialog *pdialog);
void city_dialog_update_map(struct city_dialog *pdialog);
void city_dialog_update_food(struct city_dialog *pdialog);
void city_dialog_update_production(struct city_dialog *pdialog);
void city_dialog_update_output(struct city_dialog *pdialog);
void city_dialog_update_building(struct city_dialog *pdialog);
void city_dialog_update_storage(struct city_dialog *pdialog);
void city_dialog_update_pollution(struct city_dialog *pdialog);

void sell_callback	(GtkWidget *w, gpointer data);
void buy_callback	(GtkWidget *w, gpointer data);
void change_callback	(GtkWidget *w, gpointer data);
void close_callback	(GtkWidget *w, gpointer data);
void rename_callback	(GtkWidget *w, gpointer data);
void trade_callback	(GtkWidget *w, gpointer data);
void activate_callback	(GtkWidget *w, gpointer data);
void show_units_callback(GtkWidget *w, gpointer data);
void unitupgrade_callback_yes	(GtkWidget *w, gpointer data);
void unitupgrade_callback_no	(GtkWidget *w, gpointer data);
void upgrade_callback	(GtkWidget *w, gpointer data);

gint elvis_callback	(GtkWidget *w, GdkEventButton *ev, gpointer data);
gint scientist_callback	(GtkWidget *w, GdkEventButton *ev, gpointer data);
gint taxman_callback	(GtkWidget *w, GdkEventButton *ev, gpointer data);
void rename_ok_return_action(GtkWidget *w);

gint present_units_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);
gint p_units_middle_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);
gint s_units_middle_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);
void cityopt_callback(GtkWidget *w, gpointer data);
void popdown_cityopt_dialog(void);

GdkBitmap *icon_bitmap;

/****************************************************************
...
*****************************************************************/
struct city_dialog *get_city_dialog(struct city *pcity)
{
  struct genlist_iterator myiter;

  if(!dialog_list_has_been_initialised) {
    genlist_init(&dialog_list);
    dialog_list_has_been_initialised=1;
  }
  
  genlist_iterator_init(&myiter, &dialog_list, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct city_dialog *)ITERATOR_PTR(myiter))->pcity==pcity)
      return ITERATOR_PTR(myiter);

  return 0;
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
    trade_report_dialog_update();
  } else {
    if(pdialog)  {
      /* Set the buttons we do not want live while a Diplomat investigates */
      gtk_widget_set_sensitive(pdialog->buy_command, FALSE);
      gtk_widget_set_sensitive(pdialog->change_command, FALSE);
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

  pcity_sup=city_list_find_id(&game.player_ptr->cities, punit->homecity);
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
  if(!dialog_list_has_been_initialised) {
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
  city_dialog_update_map((struct city_dialog *)data);
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
struct city_dialog *create_city_dialog(struct city *pcity, int make_modal)
{
  int i;
  struct city_dialog *pdialog;
  GtkWidget *box, *table, *frame, *vbox, *scrolled;
  GtkAccelGroup *accel=gtk_accel_group_new();

  pdialog=fc_malloc(sizeof(struct city_dialog));
  pdialog->pcity=pcity;

  pdialog->shell=gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(pdialog->shell),"delete_event",
	GTK_SIGNAL_FUNC(city_dialog_delete_callback),(gpointer)pdialog);
  gtk_window_set_position(GTK_WINDOW(pdialog->shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(pdialog->shell));

  gtk_window_set_title(GTK_WINDOW(pdialog->shell), pcity->name);

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
  
  for(i=0; i<NUM_CITIZENS_SHOWN; i++) {
    pdialog->citizen_boxes[i]=gtk_event_box_new();
    gtk_widget_set_events(pdialog->citizen_boxes[i], GDK_BUTTON_PRESS_MASK);
    gtk_box_pack_start(GTK_BOX(box), pdialog->citizen_boxes[i], FALSE, FALSE,0);

    pdialog->citizen_pixmaps[i]= gtk_pixcomm_new (root_window,
					SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);
    gtk_container_add(GTK_CONTAINER(pdialog->citizen_boxes[i]),
	pdialog->citizen_pixmaps[i]);
  }
  pdialog->sub_form=gtk_hbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(vbox), pdialog->sub_form, TRUE, TRUE, 0);

  /* "city status" vbox */
  box=gtk_vbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(pdialog->sub_form), box, FALSE, FALSE, 15);

  pdialog->production_label=gtk_label_new("\n\n");
  gtk_box_pack_start(GTK_BOX(box),pdialog->production_label, TRUE, TRUE, 0);
  gtk_widget_set_style (pdialog->production_label, city_dialog_style);
  gtk_label_set_justify (GTK_LABEL (pdialog->production_label),
			 GTK_JUSTIFY_LEFT);

  pdialog->output_label=gtk_label_new("\n\n");
  gtk_box_pack_start(GTK_BOX(box),pdialog->output_label, TRUE, TRUE, 0);
  gtk_widget_set_style (pdialog->output_label, city_dialog_style);
  gtk_label_set_justify (GTK_LABEL (pdialog->output_label),
			 GTK_JUSTIFY_LEFT);

  pdialog->storage_label=gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(box),pdialog->storage_label, TRUE, TRUE, 0);
  gtk_widget_set_style (pdialog->storage_label, city_dialog_style);
  gtk_label_set_justify (GTK_LABEL (pdialog->storage_label),
			 GTK_JUSTIFY_LEFT);

  pdialog->pollution_label=gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(box),pdialog->pollution_label, TRUE, TRUE, 0);
  gtk_widget_set_style (pdialog->pollution_label, city_dialog_style);
  gtk_label_set_justify (GTK_LABEL (pdialog->pollution_label),
			 GTK_JUSTIFY_LEFT);
  
  /* "city map canvas" */
  box=gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(pdialog->sub_form), box, FALSE, FALSE, 5);

  frame=gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(box), frame, TRUE, FALSE, 0);

  pdialog->map_canvas=gtk_drawing_area_new();
  gtk_widget_set_events(pdialog->map_canvas,
	GDK_EXPOSURE_MASK|GDK_BUTTON_PRESS_MASK);

  gtk_drawing_area_size(GTK_DRAWING_AREA(pdialog->map_canvas),
			NORMAL_TILE_WIDTH*5, NORMAL_TILE_HEIGHT*5);
  gtk_container_add(GTK_CONTAINER(frame), pdialog->map_canvas);
  
  gtk_widget_realize (pdialog->map_canvas);

  /* "production queue" vbox */
  box=gtk_vbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(pdialog->sub_form), box, TRUE, TRUE, 0);

  pdialog->building_label=gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(box), pdialog->building_label, FALSE, FALSE, 0);
  
  table=gtk_table_new(3, 1, FALSE);
  gtk_container_add(GTK_CONTAINER(pdialog->building_label), table);

  pdialog->progress_label=gtk_label_new("");
  gtk_table_attach_defaults(GTK_TABLE(table), pdialog->progress_label,0,1,1,2);

  pdialog->buy_command=gtk_accelbutton_new(_("_Buy"), accel);
  gtk_table_attach_defaults(GTK_TABLE(table), pdialog->buy_command,1,2,1,2);

  pdialog->change_command=gtk_accelbutton_new(_("_Change"), accel);
  gtk_table_attach_defaults(GTK_TABLE(table), pdialog->change_command,2,3,1,2);
  
  pdialog->improvement_list=gtk_clist_new(1);
  gtk_clist_set_column_width(GTK_CLIST(pdialog->improvement_list), 0,
	GTK_CLIST(pdialog->improvement_list)->clist_window_width);
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), pdialog->improvement_list);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(box),scrolled, TRUE, TRUE, 0);

  pdialog->sell_command=gtk_accelbutton_new(_("_Sell"), accel);
  gtk_box_pack_start(GTK_BOX(box), pdialog->sell_command, FALSE, FALSE, 0);


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
     gtk_pixcomm_new(root_window, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT+NORMAL_TILE_HEIGHT/2);
    gtk_container_add(GTK_CONTAINER(pdialog->support_unit_boxes[i]),
	pdialog->support_unit_pixmaps[i]);

    pdialog->support_unit_ids[i]=-1;

    if(pcity->owner != game.player_idx)
      gtk_widget_set_sensitive(pdialog->support_unit_boxes[i], FALSE);    
  }


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
     gtk_pixcomm_new(root_window, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    gtk_container_add(GTK_CONTAINER(pdialog->present_unit_boxes[i]),
	pdialog->present_unit_pixmaps[i]);

    pdialog->present_unit_ids[i]=-1;

    if(pcity->owner != game.player_idx)
      gtk_widget_set_sensitive(pdialog->present_unit_boxes[i], FALSE);    
  }

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

  for(i=0; i<NUM_CITIZENS_SHOWN; i++)
    pdialog->citizen_type[i]=-1;

  refresh_city_dialog(pdialog->pcity);

  if(make_modal)
    gtk_widget_set_sensitive(toplevel, FALSE);
  
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
void activate_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog = (struct city_dialog *)data;
  int x=pdialog->pcity->x,y=pdialog->pcity->y;
  struct unit_list *punit_list = &map_get_tile(x,y)->units;

  if(unit_list_size(punit_list))  {
    unit_list_iterate((*punit_list), punit) {
      if(game.player_idx==punit->owner) {
       request_new_unit_activity(punit, ACTIVITY_IDLE);
      }
    } unit_list_iterate_end;
    set_unit_focus(unit_list_get(punit_list, 0));
  }
}

/****************************************************************
...
*****************************************************************/
void show_units_callback(GtkWidget *w, gpointer data)
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

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data)))
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

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data)))  {
    activate_unit(punit);
    if((pcity=map_get_city(punit->x, punit->y)))
      if((pdialog=get_city_dialog(pcity)))
       close_city_dialog(pdialog);
  }
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

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data)))  {
    activate_unit(punit);
    if((pcity=city_list_find_id(&game.player_ptr->cities, punit->homecity)))
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

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data)))
    request_unit_disband(punit);

  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
static void present_units_homecity_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  
  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data)))
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
gint p_units_middle_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data)) &&
     (pcity=map_get_city(punit->x, punit->y)) &&
     (pdialog=get_city_dialog(pcity)) && ev->button==2) {
    activate_unit(punit);
    close_city_dialog(pdialog);
  }

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
gint s_units_middle_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data)) &&
     (pcity=find_city_by_id(punit->homecity)) &&
     (pdialog=get_city_dialog(pcity)) && ev->button==2) {
    activate_unit(punit);
    close_city_dialog(pdialog);
  }

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
gint present_units_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;
  GtkWidget *wd;

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data)) &&
     (pcity=map_get_city(punit->x, punit->y)) &&
     (pdialog=get_city_dialog(pcity))) {

    if (ev->button==2)
      return FALSE;

    wd=popup_message_dialog(pdialog->shell, 
			   /*"presentunitsdialog"*/_("Unit Commands"), 
			   unit_description(punit),
			   _("Activate unit"),
			     present_units_activate_callback, punit->id,
			   _("Activate unit, close dialog"),
			     present_units_activate_close_callback, punit->id,
			   _("Disband unit"),
			     present_units_disband_callback, punit->id,
			   _("Make new homecity"),
			     present_units_homecity_callback, punit->id,
			   _("Upgrade unit"),
			     upgrade_callback, punit->id,
			   _("Cancel"),
			     present_units_cancel_callback, 0, 
			   NULL);
    if (can_upgrade_unittype(game.player_ptr,punit->type) == -1) {
      message_dialog_button_set_sensitive(wd, "button4", FALSE);
    }
  }
  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static void rename_city_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;

  if((pdialog=(struct city_dialog *)data)) {
    packet.city_id=pdialog->pcity->id;
    strncpy(packet.name, input_dialog_get_input(w), MAX_LEN_NAME);
    packet.name[MAX_LEN_NAME-1]='\0';
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_RENAME);
  }

  input_dialog_destroy(w);
}

/****************************************************************
...
*****************************************************************/
void rename_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;

  pdialog=(struct city_dialog *)data;

  input_dialog_create(pdialog->shell, 
		      /*"shellrenamecity"*/_("Rename City"), 
		      _("What should we rename the city to?"),
		      pdialog->pcity->name,
		      (void*)rename_city_callback, (gpointer)pdialog,
		      (void*)rename_city_callback, (gpointer)0);
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
void trade_callback(GtkWidget *w, gpointer data)
{
  int i;
  int x=0,total=0;
  char buf[512],*bptr=buf;
  struct city_dialog *pdialog;

  pdialog=(struct city_dialog *)data;

  sprintf(buf, _("These trade routes have been established with %s:\n"),
	  pdialog->pcity->name);
  bptr += strlen(bptr);
  
  for(i=0; i<4; i++)
    if(pdialog->pcity->trade[i]) {
      struct city *pcity;
      x=1;
      total+=pdialog->pcity->trade_value[i];
      if((pcity=find_city_by_id(pdialog->pcity->trade[i]))) {
	sprintf(bptr, _("%32s: %2d Gold/Year\n"),
		      pcity->name, pdialog->pcity->trade_value[i]);
	bptr += strlen(bptr);
      }
      else {
	sprintf(bptr, _("%32s: %2d Gold/Year\n"), _("Unknown"),
		      pdialog->pcity->trade_value[i]);
	bptr += strlen(bptr);
      }

    }
  if(!x)
    sprintf(bptr, _("No trade routes exist.\n"));
  else
    sprintf(bptr, _("\nTotal trade %d Gold/Year\n"),total);

  popup_message_dialog(pdialog->shell, 
		       /*"citytradedialog"*/ _("Trade Routes"), 
		       buf, 
		       _("Done"), trade_message_dialog_callback, 0,
		       0);
}


/****************************************************************
...
*****************************************************************/
void city_dialog_update_pollution(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  GtkStyle *s;
  int col;

  sprintf(buf, _("Pollution:   %3d"), pcity->pollution);

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
void city_dialog_update_storage(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  
  sprintf(buf, _("Granary: %3d/%-3d"), pcity->food_stock,
	  game.foodbox*pcity->size);

  gtk_set_label(pdialog->storage_label, buf);
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_building(struct city_dialog *pdialog)
{
  char buf[32], buf2[64];
  struct city *pcity=pdialog->pcity;
  
  gtk_widget_set_sensitive(pdialog->buy_command, !pcity->did_buy);
  gtk_widget_set_sensitive(pdialog->sell_command, !pcity->did_sell);

  if(pcity->is_building_unit) {
    sprintf(buf, "%3d/%3d", pcity->shield_stock, 
	    get_unit_type(pcity->currently_building)->build_cost);
    sprintf(buf2, "%s", get_unit_type(pcity->currently_building)->name);
  } else {
    if(pcity->currently_building==B_CAPITAL)  {
      /* Capitalization is special, you can't buy it or finish making it */
      sprintf(buf,"%3d/XXX", pcity->shield_stock);
      gtk_widget_set_sensitive(pdialog->buy_command, FALSE);
    } else {
      sprintf(buf, "%3d/%3d", pcity->shield_stock, 
	     get_improvement_type(pcity->currently_building)->build_cost);
    }
    sprintf(buf2, "%s", 
	    get_imp_name_ex(pcity, pcity->currently_building));
  }    
  gtk_frame_set_label(GTK_FRAME(pdialog->building_label), buf2);
  gtk_set_label(pdialog->progress_label, buf);
}


/****************************************************************
...
*****************************************************************/
void city_dialog_update_production(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  
  sprintf(buf, _("Food:    %2d (%+2d)\nProd:    %2d (%+2d)\nTrade:   %2d (%+2d)"),
	  pcity->food_prod, pcity->food_surplus,
	  pcity->shield_prod, pcity->shield_surplus,
	  pcity->trade_prod+pcity->corruption, pcity->trade_prod);

  gtk_set_label(pdialog->production_label, buf);
}
/****************************************************************
...
*****************************************************************/
void city_dialog_update_output(struct city_dialog *pdialog)
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  
  sprintf(buf, _("Gold:    %2d (%+2d)\nLuxury:  %2d\nScience: %2d"),
	  pcity->tax_total, city_gold_surplus(pcity),
	  pcity->luxury_total,
	  pcity->science_total);

  gtk_set_label(pdialog->output_label, buf);
}


/****************************************************************
...
*****************************************************************/
void city_dialog_update_map(struct city_dialog *pdialog)
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
	pixmap_put_tile(pdialog->map_canvas->window, x, y, 
	                pcity->x+x-CITY_MAP_SIZE/2,
			pcity->y+y-CITY_MAP_SIZE/2, 1);
	if(pcity->city_map[x][y]==C_TILE_WORKER)
	  put_city_tile_output(pdialog->map_canvas->window, x, y, 
			       get_food_tile(x, y, pcity),
			       get_shields_tile(x, y, pcity), 
			       get_trade_tile(x, y, pcity));
	else if(pcity->city_map[x][y]==C_TILE_UNAVAILABLE)
	  pixmap_frame_tile_red(pdialog->map_canvas->window, x, y);
      }
      else {
	pixmap_put_black_tile(pdialog->map_canvas->window, x, y);
      }
    }
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_citizens(struct city_dialog *pdialog)
{
  int i, n;
  struct city *pcity=pdialog->pcity;

  for(i=0, n=0; n<pcity->ppl_happy[4] && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=5 && pdialog->citizen_type[i]!=6) {
      pdialog->citizen_type[i]=5+i%2;

      gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmaps[i]),
	get_citizen_sprite(pdialog->citizen_type[i]), 0, 0, TRUE);

      gtk_widget_set_sensitive(pdialog->citizen_boxes[i], FALSE);
      gtk_signal_handlers_destroy(GTK_OBJECT(pdialog->citizen_boxes[i]));
    }

  for(n=0; n<pcity->ppl_content[4] && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=3 && pdialog->citizen_type[i]!=4) {
      pdialog->citizen_type[i]=3+i%2;

      gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmaps[i]),
	get_citizen_sprite(pdialog->citizen_type[i]), 0, 0, TRUE);

      gtk_widget_set_sensitive(pdialog->citizen_boxes[i], FALSE);
      gtk_signal_handlers_destroy(GTK_OBJECT(pdialog->citizen_boxes[i]));
    }

  for(n=0; n<pcity->ppl_unhappy[4] && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=7 && pdialog->citizen_type[i]!=8) {
      pdialog->citizen_type[i]=7+i%2;

      gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmaps[i]),
	get_citizen_sprite(pdialog->citizen_type[i]), 0, 0, TRUE);

      gtk_signal_handlers_destroy(GTK_OBJECT(pdialog->citizen_boxes[i]));
      gtk_widget_set_sensitive(pdialog->citizen_boxes[i], FALSE);
    }
      
  for(n=0; n<pcity->ppl_elvis && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=0) {

      gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmaps[i]),
	get_citizen_sprite(0), 0, 0, TRUE);

      pdialog->citizen_type[i]=0;

      gtk_signal_handlers_destroy(GTK_OBJECT(pdialog->citizen_boxes[i]));
      gtk_signal_connect(GTK_OBJECT(pdialog->citizen_boxes[i]),
	"button_press_event", GTK_SIGNAL_FUNC(elvis_callback), pdialog);

      gtk_widget_set_sensitive(pdialog->citizen_boxes[i], TRUE);
    }
  
  for(n=0; n<pcity->ppl_scientist && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=1) {

      gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmaps[i]),
	get_citizen_sprite(1), 0, 0, TRUE);

      pdialog->citizen_type[i]=1;

      gtk_signal_handlers_destroy(GTK_OBJECT(pdialog->citizen_boxes[i]));
      gtk_signal_connect(GTK_OBJECT(pdialog->citizen_boxes[i]),
	"button_press_event", GTK_SIGNAL_FUNC(scientist_callback), pdialog);

      gtk_widget_set_sensitive(pdialog->citizen_boxes[i], TRUE);
    }
  
  for(n=0; n<pcity->ppl_taxman && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=2) {

      gtk_pixcomm_copyto(GTK_PIXCOMM(pdialog->citizen_pixmaps[i]),
	get_citizen_sprite(2), 0, 0, TRUE);

      pdialog->citizen_type[i]=2;

      gtk_signal_handlers_destroy(GTK_OBJECT(pdialog->citizen_boxes[i]));
      gtk_signal_connect(GTK_OBJECT(pdialog->citizen_boxes[i]),
	"button_press_event", GTK_SIGNAL_FUNC(taxman_callback), pdialog);

      gtk_widget_set_sensitive(pdialog->citizen_boxes[i], TRUE);
    }

  for(; i<NUM_CITIZENS_SHOWN; i++) {
      gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->citizen_pixmaps[i]), TRUE);

      gtk_widget_set_sensitive(pdialog->citizen_boxes[i], FALSE);
      gtk_signal_handlers_destroy(GTK_OBJECT(pdialog->citizen_boxes[i]));
  }
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

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data)) &&
     (pcity=find_city_by_id(punit->homecity)) &&
     (pdialog=get_city_dialog(pcity))) {

    if (ev->button==2)
      return FALSE;

    popup_message_dialog(pdialog->shell,
    	   /*"supportunitsdialog"*/ _("Unit Commands"), 
    	   unit_description(punit),
    	   _("Activate unit"),
    	     present_units_activate_callback, punit->id,
    	   _("Activate unit, close dialog"),
    	     supported_units_activate_close_callback, punit->id, /* act+c */
    	   _("Disband unit"),
    	     present_units_disband_callback, punit->id,
    	   _("Cancel"),
    	     present_units_cancel_callback, 0, 0);
    }
  return TRUE;
}


/****************************************************************
...
*****************************************************************/
void city_dialog_update_supported_units(struct city_dialog *pdialog, 
					int unitid)
{
  int i;
  struct genlist_iterator myiter;
  struct unit *punit;

  if(unitid) {
    for(i=0; i<NUM_UNITS_SHOWN; i++)
      if(pdialog->support_unit_ids[i]==unitid)
	break;
    if(i==NUM_UNITS_SHOWN)
      unitid=0;
  }
  
  genlist_iterator_init(&myiter, &pdialog->pcity->units_supported.list, 0);

  for(i=0; i<NUM_UNITS_SHOWN&&ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter),i++) {
    punit=(struct unit*)ITERATOR_PTR(myiter);
        
    if(unitid && punit->id!=unitid)
      continue;

    if (flags_are_transparent)
      gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->support_unit_pixmaps[i]), FALSE);

    put_unit_gpixmap(punit, GTK_PIXCOMM(pdialog->support_unit_pixmaps[i]), 0,0);
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
void city_dialog_update_present_units(struct city_dialog *pdialog, int unitid)
{
  int i;
  struct genlist_iterator myiter;
  struct unit *punit;
  
  if(unitid) {
    for(i=0; i<NUM_UNITS_SHOWN; i++)
      if(pdialog->present_unit_ids[i]==unitid)
	break;
    if(i==NUM_UNITS_SHOWN)
      unitid=0;
  }

  genlist_iterator_init(&myiter, 
	&map_get_tile(pdialog->pcity->x, pdialog->pcity->y)->units.list, 0);
  
  for(i=0; i<NUM_UNITS_SHOWN&&ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter),i++) {
    punit=(struct unit*)ITERATOR_PTR(myiter);
    
    if(unitid && punit->id!=unitid)
      continue;

    if (flags_are_transparent)
      gtk_pixcomm_clear(GTK_PIXCOMM(pdialog->present_unit_pixmaps[i]), FALSE);

    put_unit_gpixmap(punit, GTK_PIXCOMM(pdialog->present_unit_pixmaps[i]), 0,0);
    put_unit_gpixmap_city_overlays(punit, 
				  GTK_PIXCOMM(pdialog->present_unit_pixmaps[i]));

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
void city_dialog_update_title(struct city_dialog *pdialog)
{
  char buf[512];
  char *now;
  
  sprintf(buf, _("%s - %s citizens"),
	  pdialog->pcity->name, int_to_text(city_population(pdialog->pcity)));

  now=GTK_FRAME(pdialog->cityname_label)->label;
  if(strcmp(now, buf)) {
    gtk_frame_set_label(GTK_FRAME(pdialog->cityname_label), buf);
  }
}

/****************************************************************
...
*****************************************************************/
void city_dialog_update_improvement_list(struct city_dialog *pdialog)
{
  int i, n, flag;

  for(i=0, n=0, flag=0; i<B_LAST; ++i)
    if(pdialog->pcity->improvements[i]) {
      if(!pdialog->improvlist_names_ptrs[n] ||
	 strcmp(pdialog->improvlist_names_ptrs[n],
		get_imp_name_ex(pdialog->pcity, i)))
	flag=1;
      strcpy(pdialog->improvlist_names[n], get_imp_name_ex(pdialog->pcity, i));
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

    xtile=ev->x/NORMAL_TILE_WIDTH;
    ytile=ev->y/NORMAL_TILE_HEIGHT;
    packet.city_id=pcity->id;
    packet.worker_x=xtile;
    packet.worker_y=ytile;
    packet.name[0]='\0';
    
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
gint elvis_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;
  
  pdialog=(struct city_dialog *)data;

  packet.city_id=pdialog->pcity->id;
  packet.name[0]='\0';
  packet.specialist_from=SP_ELVIS;
  packet.specialist_to=SP_SCIENTIST;
  
  send_packet_city_request(&aconnection, &packet, 
			   PACKET_CITY_CHANGE_SPECIALIST);
  return TRUE;
}

/****************************************************************
...
*****************************************************************/
gint scientist_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;
  
  pdialog=(struct city_dialog *)data;

  packet.city_id=pdialog->pcity->id;
  packet.name[0]='\0';
  packet.specialist_from=SP_SCIENTIST;
  packet.specialist_to=SP_TAXMAN;
  
  send_packet_city_request(&aconnection, &packet, 
			   PACKET_CITY_CHANGE_SPECIALIST);
  return TRUE;
}

/****************************************************************
...
*****************************************************************/
gint taxman_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;
  
  pdialog=(struct city_dialog *)data;

  packet.city_id=pdialog->pcity->id;
  packet.name[0]='\0';
  packet.specialist_from=SP_TAXMAN;
  packet.specialist_to=SP_ELVIS;
  
  send_packet_city_request(&aconnection, &packet, 
			   PACKET_CITY_CHANGE_SPECIALIST);
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
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_BUY);

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void buy_callback_no(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void buy_callback(GtkWidget *w, gpointer data)
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
    name=get_imp_name_ex(pdialog->pcity, pdialog->pcity->currently_building);
  }
  value=city_buy_cost(pdialog->pcity);

  if(game.player_ptr->economic.gold>=value) {
    sprintf(buf, _("Buy %s for %d gold?\nTreasury contains %d gold."), 
	    name, value, game.player_ptr->economic.gold);

    popup_message_dialog(pdialog->shell, /*"buydialog"*/ _("Buy It!"), buf,
			 _("Yes"), buy_callback_yes, pdialog,
			 _("No"), buy_callback_no, 0, 0);
  }
  else {
    sprintf(buf, _("%s costs %d gold.\nTreasury contains %d gold."), 
	    name, value, game.player_ptr->economic.gold);

    popup_message_dialog(pdialog->shell, /*"buynodialog"*/ _("Buy It!"), buf,
			 _("Darn"), buy_callback_no, 0, 0);
  }
}


/****************************************************************
...
*****************************************************************/
void unitupgrade_callback_yes(GtkWidget *w, gpointer data)
{
  struct unit *punit;

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data))) {
    request_unit_upgrade(punit);
  }
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void unitupgrade_callback_no(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void upgrade_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  char buf[512];
  int ut1,ut2;
  int value;

  if((punit=unit_list_find(&game.player_ptr->units, (size_t)data))) {
    ut1=punit->type;
    /* printf("upgrade_callback for %s\n", unit_types[ut1].name); */

    ut2=can_upgrade_unittype(game.player_ptr,ut1);

    if (ut2==-1) {
      /* this shouldn't generally happen, but it is conceivable */
      sprintf(buf, _("Sorry: cannot upgrade %s."), unit_types[ut1].name);
      popup_message_dialog(toplevel, /*"upgradenodialog"*/_("Upgrade Unit!"), buf,
			   _("Darn"), unitupgrade_callback_no, 0,
			   NULL);
    } else {
      value=unit_upgrade_price(game.player_ptr, ut1, ut2);

      if(game.player_ptr->economic.gold>=value) {
	sprintf(buf, _("Upgrade %s to %s for %d gold?\n"
	       "Treasury contains %d gold."),
	       unit_types[ut1].name, unit_types[ut2].name,
	       value, game.player_ptr->economic.gold);
	popup_message_dialog(toplevel, 
			     /*"upgradedialog"*/_("Upgrade Obsolete Units"), buf,
			     _("Yes"),
			       unitupgrade_callback_yes, (gpointer)(punit->id),
			     _("No"),
			       unitupgrade_callback_no, 0,
			     NULL);
      } else {
	sprintf(buf, _("Upgrading %s to %s costs %d gold.\n"
	       "Treasury contains %d gold."),
	       unit_types[ut1].name, unit_types[ut2].name,
	       value, game.player_ptr->economic.gold);
	popup_message_dialog(toplevel,
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
    packet.build_id=pdialog->change_list_ids[row];
    packet.is_build_id_unit_id=
      (row >= pdialog->change_list_num_improvements);
    
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
  }
  gtk_widget_destroy(w->parent->parent->parent);
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
  
  gtk_widget_set_sensitive(pdialog->shell, TRUE);
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
void change_callback(GtkWidget *w, gpointer data)
{
  GtkWidget *cshell, *button, *scrolled;
  struct city_dialog *pdialog;
  int i, n;
  static gchar *title_[1] = { N_("Select new production") };
  static gchar **title = NULL;
  GtkAccelGroup *accel=gtk_accel_group_new();

  if (!title) title = intl_slist(1, title_);
  
  pdialog=(struct city_dialog *)data;
  
  cshell=gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(cshell),"delete_event",
	GTK_SIGNAL_FUNC(change_deleted_callback),pdialog);

  gtk_window_set_position (GTK_WINDOW(cshell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(cshell));
  gtk_window_set_title(GTK_WINDOW(cshell), _("Change Production"));

  pdialog->change_list=gtk_clist_new_with_titles(1, title);
  gtk_clist_column_titles_passive(GTK_CLIST(pdialog->change_list));
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), pdialog->change_list);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(scrolled, 220, 350);
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

  for(i=0, n=0; i<B_LAST; i++)
    if(can_build_improvement(pdialog->pcity, i)) {
      sprintf(pdialog->change_list_names[n], "%s (%d)",
	get_imp_name_ex(pdialog->pcity, i),get_improvement_type(i)->build_cost);
      
      pdialog->change_list_names_ptrs[n]=pdialog->change_list_names[n];
      pdialog->change_list_ids[n++]=i;
    }
  
  pdialog->change_list_num_improvements=n;


  for(i=0; i<game.num_unit_types; i++)
    if(can_build_unit(pdialog->pcity, i)) {
      sprintf(pdialog->change_list_names[n],"%s (%d)",
	get_unit_name(i), get_unit_type(i)->build_cost);

      pdialog->change_list_names_ptrs[n]=pdialog->change_list_names[n];
      pdialog->change_list_ids[n++]=i;
    }
  
  pdialog->change_list_names_ptrs[n]=0;

  gtk_clist_freeze(GTK_CLIST(pdialog->change_list));
  gtk_clist_clear(GTK_CLIST(pdialog->change_list));

  for(i=0; i<n; i++)
    gtk_clist_append(GTK_CLIST(pdialog->change_list),
		     &pdialog->change_list_names_ptrs[i]);

  gtk_clist_thaw(GTK_CLIST(pdialog->change_list));

  gtk_widget_show_all(GTK_DIALOG(cshell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(cshell)->action_area);

  gtk_widget_show(cshell);
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
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_SELL);

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
static void sell_callback_no(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void sell_callback(GtkWidget *w, gpointer data)
{
  struct city_dialog *pdialog;
  GList *selection;
  pdialog=(struct city_dialog *)data;

  if((selection=GTK_CLIST(pdialog->improvement_list)->selection)) {
    int i, n;
    gint row=(gint)selection->data;

    for(i=0, n=0; i<B_LAST; i++)
      if(pdialog->pcity->improvements[i]) {
	if(n==row) {
	  char buf[512];
	  
	  if(is_wonder(i))
	    return;
	  
	  pdialog->sell_id=i;
	  sprintf(buf, _("Sell %s for %d gold?"), 
		  get_imp_name_ex(pdialog->pcity, i),
		  improvement_value(i));

	  popup_message_dialog(pdialog->shell, /*"selldialog"*/ _("Sell It!"), buf,
			       _("Yes"), sell_callback_yes, pdialog,
			       _("No"), sell_callback_no, pdialog, 0);
	  return;
	}
	n++;
      }
  }
}

/****************************************************************
...
*****************************************************************/
void close_city_dialog(struct city_dialog *pdialog)
{
  gtk_widget_hide(pdialog->shell);
  genlist_unlink(&dialog_list, pdialog);

  if(pdialog->is_modal)
    gtk_widget_set_sensitive(toplevel, TRUE);

  gtk_widget_destroy(pdialog->shell);
  free(pdialog);
}

/****************************************************************
...
*****************************************************************/
void close_callback(GtkWidget *w, gpointer data)
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

GtkWidget *create_cityopt_dialog(char *city_name);
void cityopt_ok_command_callback(GtkWidget *w, gpointer data);
void cityopt_cancel_command_callback(GtkWidget *w, gpointer data);
void cityopt_newcit_triggle_callback(GtkWidget *w, gpointer data);

char *newcitizen_labels[] = { N_("Workers"), N_("Scientists"), N_("Taxmen") };

static GtkWidget *cityopt_shell = 0;
static GtkWidget *cityopt_triggle;
static GtkWidget *cityopt_toggles[NUM_CITYOPT_TOGGLES];
static int cityopt_city_id = 0;
static int newcitizen_index;

/****************************************************************
...
*****************************************************************/
void cityopt_callback(GtkWidget *w, gpointer data)
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
    newcitizen_index = 1;
  } else if (pcity->city_options & (1<<CITYO_NEW_TAXMAN)) {
    newcitizen_index = 2;
  } else {
    newcitizen_index = 0;
  }
  gtk_label_set_text (GTK_LABEL (GTK_BIN (cityopt_triggle)->child),
	_(newcitizen_labels[newcitizen_index]));
  
  cityopt_city_id = pcity->id;

/*  gtk_set_relative_position(toplevel, cityopt_shell, 15, 15);*/
  gtk_widget_show(cityopt_shell);
}


/**************************************************************************
...
**************************************************************************/
GtkWidget *create_cityopt_dialog(char *city_name)
{
  GtkWidget *shell, *label, *ok, *cancel;
  GtkAccelGroup *accel=gtk_accel_group_new();

  shell = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (shell), _("City Options"));
  gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(shell));

  label = gtk_label_new (city_name);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(shell)->vbox),
	label, FALSE, FALSE, 0);


  cityopt_triggle = gtk_check_button_new_with_label (_("Scientists"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(shell)->vbox),
	cityopt_triggle, FALSE, FALSE, 0);

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

  gtk_signal_connect(GTK_OBJECT(cityopt_triggle), "toggled",
	GTK_SIGNAL_FUNC(cityopt_newcit_triggle_callback), NULL);
 
  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(shell)->action_area);

  return shell;
}
  
/**************************************************************************
...
**************************************************************************/
void cityopt_cancel_command_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_destroy(cityopt_shell);
  cityopt_shell = 0;
}

/**************************************************************************
...
**************************************************************************/
void cityopt_ok_command_callback(GtkWidget *w, gpointer data)
{
  struct city *pcity = find_city_by_id(cityopt_city_id);

  if (pcity) {
    struct packet_generic_values packet;
    int i, new;
    
    new = 0;
    for(i=0; i<NUM_CITYOPT_TOGGLES; i++)  {
      if (GTK_TOGGLE_BUTTON(cityopt_toggles[i])->active) new |= (1<<i);
    }
    if (newcitizen_index == 1) {
      new |= (1<<CITYO_NEW_EINSTEIN);
    } else if (newcitizen_index == 2) {
      new |= (1<<CITYO_NEW_TAXMAN);
    }
    packet.value1 = cityopt_city_id;
    packet.value2 = new;
    send_packet_generic_values(&aconnection, PACKET_CITY_OPTIONS,
			      &packet);
  }
  gtk_widget_destroy(cityopt_shell);
  cityopt_shell = 0;
}

/**************************************************************************
 Changes the label of the toggle widget to between newcitizen_labels
 and increments (mod 3) newcitizen_index.
**************************************************************************/
void cityopt_newcit_triggle_callback(GtkWidget *w, gpointer data)
{
  newcitizen_index++;
  if (newcitizen_index>=3) {
    newcitizen_index = 0;
  }
  gtk_label_set_text (GTK_LABEL (GTK_BIN (cityopt_triggle)->child),
	_(newcitizen_labels[newcitizen_index]));
}

/**************************************************************************
...
**************************************************************************/
void popdown_cityopt_dialog(void)
{
  if(cityopt_shell) {
    gtk_widget_destroy(cityopt_shell);
    cityopt_shell = 0;
  }
}
