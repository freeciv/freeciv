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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <gtk/gtk.h>

#include "game.h"
#include "genlist.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"

#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "repodlgs.h"
#include "spaceship.h"

#include "spaceshipdlg.h"

extern int SPACE_TILES;
extern GdkGC *civ_gc, *fill_bg_gc;

extern GtkWidget *toplevel;
extern GdkWindow *root_window;
extern struct connection aconnection;

/* These are the order of the tiles in space.xpm: */
enum spaceship_pix { SSHIP_SOLAR_PANELS, SSHIP_LIFE_SUPPORT, SSHIP_HABITATION,
		    SSHIP_STRUCTURAL, SSHIP_FUEL, SSHIP_PROPULSION };

struct spaceship_dialog {
  struct player *pplayer;
  GtkWidget *shell;
  GtkWidget *main_form;
  GtkWidget *player_label;
  GtkWidget *info_label;
  GtkWidget *image_canvas;
  GtkWidget *launch_command;
  GtkWidget *close_command;
};

#define FIXED_12_BFONT  "-b&h-lucidatypewriter-bold-r-normal-*-12-*-*-*-*-*-*-*"


static struct genlist dialog_list;
static int dialog_list_has_been_initialised;

struct spaceship_dialog *get_spaceship_dialog(struct player *pplayer);
struct spaceship_dialog *create_spaceship_dialog(struct player *pplayer);
void close_spaceship_dialog(struct spaceship_dialog *pdialog);

void spaceship_dialog_update_image(struct spaceship_dialog *pdialog);
void spaceship_dialog_update_info(struct spaceship_dialog *pdialog);

void spaceship_close_callback(GtkWidget *w, gpointer data);
void spaceship_launch_callback(GtkWidget *w, gpointer data);

/****************************************************************
...
*****************************************************************/
struct spaceship_dialog *get_spaceship_dialog(struct player *pplayer)
{
  struct genlist_iterator myiter;

  if(!dialog_list_has_been_initialised) {
    genlist_init(&dialog_list);
    dialog_list_has_been_initialised=1;
  }
  
  genlist_iterator_init(&myiter, &dialog_list, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct spaceship_dialog *)ITERATOR_PTR(myiter))->pplayer==pplayer)
      return ITERATOR_PTR(myiter);

  return 0;
}

/****************************************************************
...
*****************************************************************/
void refresh_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;
  struct player_spaceship *pship;

  if(!(pdialog=get_spaceship_dialog(pplayer)))
    return;

  pship=&(pdialog->pplayer->spaceship);

  if(game.spacerace
     && pplayer->player_no == game.player_idx
     && pship->state == SSHIP_STARTED
     && pship->success_rate > 0) {
    gtk_widget_set_sensitive(pdialog->launch_command, TRUE);
  } else {
    gtk_widget_set_sensitive(pdialog->launch_command, FALSE);
  }
  
  spaceship_dialog_update_info(pdialog);
  spaceship_dialog_update_image(pdialog);
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;
  
  if(!(pdialog=get_spaceship_dialog(pplayer)))
    pdialog=create_spaceship_dialog(pplayer);

  gtk_set_relative_position(toplevel, pdialog->shell, 10, 10);
  gtk_widget_show(pdialog->shell);
}

/****************************************************************
popdown the dialog 
*****************************************************************/
void popdown_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;
  
  if((pdialog=get_spaceship_dialog(pplayer)))
    close_spaceship_dialog(pdialog);
}



/****************************************************************
...
*****************************************************************/
gint spaceship_image_canvas_expose(GtkWidget *widget, GdkEventExpose *ev,
				   gpointer data)
{
  struct spaceship_dialog *pdialog;
  
  pdialog=(struct spaceship_dialog *)data;
  spaceship_dialog_update_image(pdialog);
  return TRUE;
}


/****************************************************************
...
*****************************************************************/
struct spaceship_dialog *create_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;
  GtkWidget *hbox, *frame;
  GtkStyle *style;
  
  pdialog=fc_malloc(sizeof(struct spaceship_dialog));
  pdialog->pplayer=pplayer;

  pdialog->shell=gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (pdialog->shell), pplayer->name);
  gtk_widget_realize(pdialog->shell);
  gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(pdialog->shell)->vbox),5);

  pdialog->player_label=gtk_frame_new (pplayer->name);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (pdialog->shell)->vbox),
	pdialog->player_label);
  hbox=gtk_hbox_new(FALSE, 5);
  gtk_container_add (GTK_CONTAINER (pdialog->player_label), hbox);

  frame=gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 0);

  pdialog->image_canvas=gtk_drawing_area_new();
  gtk_drawing_area_size(GTK_DRAWING_AREA(pdialog->image_canvas),
                        get_tile_sprite(SPACE_TILES)->width*7,
			get_tile_sprite(SPACE_TILES)->height*7);
  gtk_widget_set_events(pdialog->image_canvas, GDK_EXPOSURE_MASK);
  gtk_signal_connect(GTK_OBJECT(pdialog->image_canvas), "expose_event",
        GTK_SIGNAL_FUNC(spaceship_image_canvas_expose), pdialog);
  gtk_container_add(GTK_CONTAINER (frame), pdialog->image_canvas);
  gtk_widget_realize(pdialog->image_canvas);

  style=gtk_style_copy (GTK_WIDGET (pdialog->shell)->style);
  gdk_font_unref (style->font);
  style->font=gdk_font_load (FIXED_12_BFONT);
  gdk_font_ref (style->font);

  pdialog->info_label=gtk_label_new ("Population:       1234\n"
				     "Support:           100 %\n"
				     "Energy:            100 %\n"
				     "Mass:            12345 tons\n"
				     "Travel time:      1234 years\n"
				     "Success prob.:     100 %\n"
				     "Year of arrival:  1234 AD");
  gtk_label_set_justify (GTK_LABEL (pdialog->info_label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment(GTK_MISC(pdialog->info_label), 0.0, 0.0);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->info_label, FALSE, FALSE, 0);
  gtk_widget_set_style (pdialog->info_label, style);

  
  pdialog->close_command=gtk_button_new_with_label ("Close");

  pdialog->launch_command=gtk_button_new_with_label ("Launch");

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
        pdialog->close_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->close_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->shell)->action_area),
        pdialog->launch_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->launch_command, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(pdialog->close_command);

  gtk_signal_connect(GTK_OBJECT(pdialog->close_command), "clicked",
        GTK_SIGNAL_FUNC(spaceship_close_callback), pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->launch_command), "clicked",
        GTK_SIGNAL_FUNC(spaceship_launch_callback), pdialog);

  genlist_insert(&dialog_list, pdialog, 0);

  gtk_widget_show_all(GTK_DIALOG(pdialog->shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(pdialog->shell)->action_area);

  refresh_spaceship_dialog(pdialog->pplayer);
/*
  XSetWMProtocols(display, XtWindow(pdialog->shell), &wm_delete_window, 1);
  XtOverrideTranslations(pdialog->shell, 
    XtParseTranslationTable ("<Message>WM_PROTOCOLS: close-spaceshipdialog()"));

  textfieldtranslations = 
    XtParseTranslationTable("<Key>Return: spaceship-dialog-returnkey()");
  XtOverrideTranslations(pdialog->close_command, textfieldtranslations);
  XtSetKeyboardFocus(pdialog->shell, pdialog->close_command);
*/
  return pdialog;
}

/****************************************************************
...
*****************************************************************/
void spaceship_dialog_update_info(struct spaceship_dialog *pdialog)
{
  char buf[512], arrival[16] = "-   ";
  struct player_spaceship *pship=&(pdialog->pplayer->spaceship);

  if (pship->state == SSHIP_LAUNCHED) {
    strcpy(arrival, textyear((int) (pship->launch_year
				    + (int) pship->travel_time)));
  }
  sprintf(buf,
	  "Population:      %5d\n"
	  "Support:         %5d %%\n"
	  "Energy:          %5d %%\n"
	  "Mass:            %5d tons\n"
	  "Travel time:     %5.1f years\n"
	  "Success prob.:   %5d %%\n"
	  "Year of arrival: %8s",
	  pship->population,
	  (int) (pship->support_rate * 100.0),
	  (int) (pship->energy_rate * 100.0),
	  pship->mass,
	  (float) (0.1 * ((int) (pship->travel_time * 10.0))),
	  (int) (pship->success_rate * 100.0),
	  arrival);

  gtk_set_label(pdialog->info_label, buf);
}

/****************************************************************
...
Should also check connectedness, and show non-connected
parts differently.
*****************************************************************/
void spaceship_dialog_update_image(struct spaceship_dialog *pdialog)
{
  int i, j, k, x, y;  
  struct Sprite *sprite=get_tile_sprite(SPACE_TILES);
  struct player_spaceship *ship = &pdialog->pplayer->spaceship;

  gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_BLACK]);
  gdk_draw_rectangle(pdialog->image_canvas->window, fill_bg_gc, TRUE,
		0, 0, sprite->width * 7, sprite->height * 7);

  for (i=0; i < NUM_SS_MODULES; i++) {
    j = i/3;
    k = i%3;
    if ((k==0 && j >= ship->habitation)
	|| (k==1 && j >= ship->life_support)
	|| (k==2 && j >= ship->solar_panels)) {
      continue;
    }
    x = modules_info[i].x * sprite->width  / 4 - sprite->width / 2;
    y = modules_info[i].y * sprite->height / 4 - sprite->height / 2;

    sprite = get_tile_sprite(SPACE_TILES +
			    (k==0?SSHIP_HABITATION:
			     k==1?SSHIP_LIFE_SUPPORT:
			     SSHIP_SOLAR_PANELS));
    gdk_gc_set_clip_origin(civ_gc, x, y);
    gdk_gc_set_clip_mask(civ_gc, sprite->mask);
    gdk_draw_pixmap(pdialog->image_canvas->window, civ_gc, sprite->pixmap, 
	      0, 0,
	      x, y,
	      sprite->width, sprite->height);
    gdk_gc_set_clip_mask(civ_gc,NULL);
  }

  for (i=0; i < NUM_SS_COMPONENTS; i++) {
    j = i/2;
    k = i%2;
    if ((k==0 && j >= ship->fuel)
	|| (k==1 && j >= ship->propulsion)) {
      continue;
    }
    x = components_info[i].x * sprite->width  / 4 - sprite->width / 2;
    y = components_info[i].y * sprite->height / 4 - sprite->height / 2;

    sprite = get_tile_sprite(SPACE_TILES + (k==0?SSHIP_FUEL:SSHIP_PROPULSION));

    gdk_gc_set_clip_origin(civ_gc, x, y);
    gdk_gc_set_clip_mask(civ_gc, sprite->mask);
    gdk_draw_pixmap(pdialog->image_canvas->window, civ_gc, sprite->pixmap,
	      0, 0,
	      x, y,
	      sprite->width, sprite->height);
    gdk_gc_set_clip_mask(civ_gc,NULL);
  }

  sprite = get_tile_sprite(SPACE_TILES + SSHIP_STRUCTURAL);

  for (i=0; i < NUM_SS_STRUCTURALS; i++) {
    if (!ship->structure[i])
      continue;
    x = structurals_info[i].x * sprite->width  / 4 - sprite->width / 2;
    y = structurals_info[i].y * sprite->height / 4 - sprite->height / 2;

    gdk_gc_set_clip_origin(civ_gc, x, y);
    gdk_gc_set_clip_mask(civ_gc, sprite->mask);
    gdk_draw_pixmap(pdialog->image_canvas->window, civ_gc, sprite->pixmap,
	      0, 0,
	      x, y,
	      sprite->width, sprite->height);
    gdk_gc_set_clip_mask(civ_gc,NULL);
  }
}


/****************************************************************
...
*****************************************************************/
void close_spaceship_dialog(struct spaceship_dialog *pdialog)
{
  gtk_widget_destroy(pdialog->shell);
  genlist_unlink(&dialog_list, pdialog);

  free(pdialog);
}

/****************************************************************
...
*****************************************************************/
void spaceship_dialog_returnkey(GtkWidget *w, gpointer data)
{
  close_spaceship_dialog_action(w->parent->parent, 0);
}

/****************************************************************
...
*****************************************************************/
void close_spaceship_dialog_action(GtkWidget *w, gpointer data)
{
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &dialog_list, 0);
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct spaceship_dialog *)ITERATOR_PTR(myiter))->shell==w) {
      close_spaceship_dialog((struct spaceship_dialog *)ITERATOR_PTR(myiter));
      return;
    }
}


/****************************************************************
...
*****************************************************************/
void spaceship_close_callback(GtkWidget *w, gpointer data)
{
  close_spaceship_dialog((struct spaceship_dialog *)data);
}


/****************************************************************
...
*****************************************************************/
void spaceship_launch_callback(GtkWidget *w, gpointer data)
{
  struct packet_spaceship_action packet;

  packet.action = SSHIP_ACT_LAUNCH;
  packet.num = 0;
  send_packet_spaceship_action(&aconnection, &packet);
  /* close_spaceship_dialog((struct spaceship_dialog *)data); */
}
