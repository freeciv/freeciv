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
#include <gtk/gtk.h>

#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "support.h"
#include "unit.h"

#include "chatline.h"
#include "citydlg.h"
#include "civclient.h"
#include "clinet.h"
#include "climisc.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "goto.h"
#include "graphics.h"
#include "gui_main.h"
#include "inputdlg.h"
#include "mapview.h"
#include "menu.h"
#include "tilespec.h"
#include "cma_core.h"

#include "mapctrl.h"

/* Update the workers for a city on the map, when the update is received */
struct city *city_workers_display = NULL;
/* Color to use to display the workers */
int city_workers_color=COLOR_STD_WHITE;

/**************************************************************************
...
**************************************************************************/
static gint popit_button_release(GtkWidget *w, GdkEventButton *event)
{
  gtk_grab_remove(w);
  gdk_pointer_ungrab(GDK_CURRENT_TIME);
  gtk_widget_destroy(w);
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static void popit(GdkEventButton *event, int xtile, int ytile)
{
  GtkWidget *p, *b;
  static struct map_position cross_list[2 + 1];
  struct map_position *cross_head = cross_list;
  int i, count = 0;
  int popx, popy;
  char s[512];
  struct city *pcity;
  struct unit *punit;
  struct tile *ptile = map_get_tile(xtile, ytile);

  if(tile_get_known(xtile, ytile) >= TILE_KNOWN_FOGGED) {
    p=gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_app_paintable(p, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(p), 4);
    b=gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(p), b);

#ifdef DEBUG    
    my_snprintf(s, sizeof(s), _("Location: (%d, %d)"), xtile, ytile);
    gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				    "GtkLabel::label", s, NULL);
    count++;
#endif /* DEBUG */

    my_snprintf(s, sizeof(s), _("Terrain: %s"),
		map_get_tile_info_text(xtile, ytile));
    gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				   "GtkLabel::label", s, NULL);
    count++;

    my_snprintf(s, sizeof(s), _("Food/Prod/Trade: %s"),
		map_get_tile_fpt_text(xtile, ytile));
    gtk_widget_new(GTK_TYPE_LABEL,  "GtkWidget::parent", b,
				    "GtkLabel::label", s, NULL);
    count++;

    if (tile_has_special(ptile, S_HUT)) {
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label",
				     _("Minor Tribe Village"), NULL);
      count++;
    }
    
    if((pcity = map_get_city(xtile, ytile))) {
      my_snprintf(s, sizeof(s), _("City: %s(%s)"), pcity->name,
		  get_nation_name(city_owner(pcity)->nation));
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label", s, NULL);
      count++;

      if (city_got_citywalls(pcity)) {
        gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
		       "GtkLabel::label", _("with City Walls"), NULL);
	count++;
      }
    }

    if(get_tile_infrastructure_set(ptile)) {
      sz_strlcpy(s, _("Infrastructure: "));
      sz_strlcat(s, map_get_infrastructure_text(ptile->special));
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label", s, NULL);
      count++;
    }
    
    sz_strlcpy(s, _("Activity: "));
    if (concat_tile_activity_text(s, sizeof(s), xtile, ytile)) {
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
		     "GtkLabel::label", s, NULL);
      count++;
    }
    
    if((punit = find_visible_unit(ptile)) && !pcity) {
      char cn[64];
      struct unit_type *ptype = unit_type(punit);
      cn[0] = '\0';
      if(punit->owner == game.player_idx) {
	struct city *pcity;
	pcity=player_find_city_by_id(game.player_ptr, punit->homecity);
	if(pcity)
	  my_snprintf(cn, sizeof(cn), "/%s", pcity->name);
      }
      my_snprintf(s, sizeof(s), _("Unit: %s(%s%s)"), ptype->name,
		  get_nation_name(unit_owner(punit)->nation), cn);
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label", s, NULL);
      count++;

      if(punit->owner == game.player_idx)  {
	char uc[64] = "";
	if(unit_list_size(&ptile->units) >= 2) {
	  my_snprintf(uc, sizeof(uc), _("  (%d more)"),
		      unit_list_size(&ptile->units) - 1);
	}
        my_snprintf(s, sizeof(s), _("A:%d D:%d FP:%d HP:%d/%d%s%s"),
		    ptype->attack_strength, 
		    ptype->defense_strength, ptype->firepower, punit->hp, 
		    ptype->hp, punit->veteran ? _(" V") : "", uc);

        if(punit->activity == ACTIVITY_GOTO || punit->connecting)  {
	  cross_head->x = punit->goto_dest_x;
	  cross_head->y = punit->goto_dest_y;
	  cross_head++;
        }
      } else {
        my_snprintf(s, sizeof(s), _("A:%d D:%d FP:%d HP:%d0%%"),
		    ptype->attack_strength, 
		    ptype->defense_strength, ptype->firepower, 
		    (punit->hp * 100 / ptype->hp + 9) / 10 );
      }
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label", s, NULL);
      count++;
    }

    cross_head->x = xtile;
    cross_head->y = ytile;
    cross_head++;

    gtk_widget_show_all(b);

    cross_head->x = -1;
    for (i = 0; cross_list[i].x >= 0; i++) {
      put_cross_overlay_tile(cross_list[i].x, cross_list[i].y);
    }
    gtk_signal_connect(GTK_OBJECT(p),"destroy",
		       GTK_SIGNAL_FUNC(popupinfo_popdown_callback),
		       cross_list);

    /* displace popup so as not to obscure it by the mouse cursor */
    popx= event->x_root + 16;
    popy= event->y_root - (8 * count);
    if (popy < 0)
      popy = 0;      
    gtk_window_move(GTK_WINDOW(p), popx, popy);
    gtk_widget_show(p);
    gdk_pointer_grab(p->window, TRUE, GDK_BUTTON_RELEASE_MASK,
		     NULL, NULL, event->time);
    gtk_grab_add(p);

    gtk_signal_connect_after(GTK_OBJECT(p), "button_release_event",
                             GTK_SIGNAL_FUNC(popit_button_release), NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
void popupinfo_popdown_callback(GtkWidget *w, gpointer data)
{
  struct map_position *cross_list=(struct map_position *)data;

  while (cross_list->x >= 0) {
    refresh_tile_mapcanvas(cross_list->x, cross_list->y, TRUE);
    cross_list++;
  }
}

 /**************************************************************************
...
**************************************************************************/
static void name_new_city_callback(GtkWidget *w, gpointer data)
{
  size_t unit_id;
  
  if((unit_id = (size_t)data)) {
    struct packet_unit_request req;
    req.unit_id = unit_id;
    sz_strlcpy(req.name, input_dialog_get_input(w));
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
  }
  input_dialog_destroy(w);
}

/**************************************************************************
 Popup dialog where the user choose the name of the new city
 punit = (settler) unit which builds the city
 suggestname = suggetion of the new city's name
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, char *suggestname)
{
  input_dialog_create(toplevel, /*"shellnewcityname" */
		     _("Build New City"),
		     _("What should we call our new city?"), suggestname,
		     name_new_city_callback, GINT_TO_POINTER(punit->id),
		     name_new_city_callback, GINT_TO_POINTER(0));
}

/**************************************************************************
 Enable or disable the turn done button.
 Should probably some where else.
**************************************************************************/
void set_turn_done_button_state(bool state)
{
  gtk_widget_set_sensitive(turn_done_button, state);
}


/**************************************************************************
(RP:) wake up my own sentried units on the tile that was clicked
**************************************************************************/
gint butt_down_wakeup(GtkWidget *w, GdkEventButton *ev)
{
  int xtile, ytile;

  /* when you get a <SHIFT>+<LMB> pow! */
  if (get_client_state() != CLIENT_GAME_RUNNING_STATE
      || !(ev->state & GDK_SHIFT_MASK)) {
    return TRUE;
  }

  get_map_xy(ev->x, ev->y, &xtile, &ytile);

  wakeup_sentried_units(xtile, ytile);
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
gint butt_down_mapcanvas(GtkWidget *w, GdkEventButton *ev)
{
  int xtile, ytile;
  
  if(get_client_state() != CLIENT_GAME_RUNNING_STATE)
    return TRUE;
  
  if ((ev->button == 1) && (ev->state & GDK_SHIFT_MASK)) {
    adjust_workers(w, ev);
    return TRUE;
  }

  get_map_xy(ev->x, ev->y, &xtile, &ytile);

  if (ev->button == 1) {
    do_map_click(xtile, ytile);
    gtk_widget_grab_focus(turn_done_button);
  } else if ((ev->button == 2) || (ev->state & GDK_CONTROL_MASK)) {
    popit(ev, xtile, ytile);
  } else {
    center_tile_mapcanvas(xtile, ytile);
  }
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  int x, y;

  gdk_window_get_pointer(map_canvas->window, &x, &y, 0);

  update_line(x, y);
}

/**************************************************************************
...
**************************************************************************/
void update_line(int window_x, int window_y)
{
  int x, y, old_x, old_y;

  if ((hover_state == HOVER_GOTO || hover_state == HOVER_PATROL)
      && draw_goto_line) {
    get_map_xy(window_x, window_y, &x, &y);

    get_line_dest(&old_x, &old_y);
    if (old_x != x || old_y != y) {
      draw_line(x, y);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
gint move_mapcanvas(GtkWidget *widget, GdkEventButton *event)
{
  update_line(event->x, event->y);
  return TRUE;
}

/**************************************************************************
  Adjust the position of city workers from the mapcanvas
**************************************************************************/
void adjust_workers(GtkWidget *widget, GdkEventButton *ev)
{
  int x, y, map_x, map_y, is_valid;
  struct city *pcity;
  struct packet_city_request packet;
  enum city_tile_type wrk;

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return;
  }

  get_map_xy(ev->x, ev->y, &map_x, &map_y);

  pcity = find_city_near_tile(map_x, map_y);
  if (!pcity) {
    return;
  }

  if (cma_is_city_under_agent(pcity, NULL)) {
    return;
  }

  is_valid = map_to_city_map(&x, &y, pcity, map_x, map_y);
  assert(is_valid);

  packet.city_id = pcity->id;
  packet.worker_x = x;
  packet.worker_y = y;
  
  wrk = get_worker_city(pcity, x, y);
  if(wrk == C_TILE_WORKER)
    send_packet_city_request(&aconnection, &packet, 
			    PACKET_CITY_MAKE_SPECIALIST);
  else if(wrk == C_TILE_EMPTY)
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_MAKE_WORKER);

  /* When the city info packet is received, update the workers on the map*/
  city_workers_display = pcity;
}

/**************************************************************************
...
**************************************************************************/
gint butt_down_overviewcanvas(GtkWidget *w, GdkEventButton *ev)
{
  int xtile, ytile;

  if (ev->type != GDK_BUTTON_PRESS)
    return TRUE; /* Double-clicks? Triple-clicks? No thanks! */

  if (is_isometric) {
    xtile = ev->x / 2 - (map.xsize / 2 -
			 (map_view_x0 +
			  (map_canvas_store_twidth +
			   map_canvas_store_theight) / 2));
  } else {
    xtile = ev->x / 2 - (map.xsize / 2 -
			 (map_view_x0 + map_canvas_store_twidth / 2));
  }
  ytile = ev->y / 2;
  
  if(get_client_state() != CLIENT_GAME_RUNNING_STATE)
     return TRUE;

  if (ev->button == 1) {
    do_unit_goto(xtile, ytile);
  } else if (ev->button == 3) {
    center_tile_mapcanvas(xtile, ytile);
  }
  return TRUE;
}

/**************************************************************************
  Draws the on the map the tiles the given city is using
**************************************************************************/
void center_on_unit(void)
{
  request_center_focus_unit();
}

/**************************************************************************
  Draws the on the map the tiles the given city is using
**************************************************************************/
void key_city_workers(GtkWidget *w, GdkEventKey *ev)
{
  int x,y;
  struct city *pcity;

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return;
  }
  
  gdk_window_get_pointer(map_canvas->window, &x, &y, NULL);
  get_map_xy(x, y, &x, &y);

  pcity = find_city_near_tile(x, y);
  if (!pcity) {
    return;
  }

  /* Shade tiles on usage */
  city_workers_color = (city_workers_color % 3) + 1;
  put_city_workers(pcity, city_workers_color);
}


/**************************************************************************
...
**************************************************************************/
void focus_to_next_unit(void)
{
  advance_unit_focus();
}
