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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mem.h"
#include "packets.h"
#include "worklist.h"

#include "wldlg.h"

extern struct connection aconnection;
extern GtkWidget *toplevel;

#define WORKLIST_ADVANCED_TARGETS  1
#define WORKLIST_CURRENT_TARGETS   0
#define WORKLIST_LIST_WIDTH   220
#define WORKLIST_LIST_HEIGHT  150

/*
  The Worklist Report dialog shows all the global worklists that the
  player has defined.  There can be at most MAX_NUM_WORKLISTS global
  worklists.
  */
struct worklist_report_dialog {
  GtkWidget *list;
  struct player *pplr;
  char worklist_names[MAX_NUM_WORKLISTS][MAX_LEN_NAME];
  char *worklist_names_ptrs[MAX_NUM_WORKLISTS+1];
  struct worklist *worklist_ptr[MAX_NUM_WORKLISTS];
  int wl_idx;
};

/*
  The Worklist dialog is the dialog with which the player edits a 
  particular worklist.  The worklist dialog is popped-up from either
  the Worklist Report dialog or from a City dialog.
  */
struct worklist_dialog {
  GtkWidget *worklist, *avail;
  GtkWidget *btn_prepend, *btn_insert, *btn_delete, *btn_up, *btn_down;
  GtkWidget *toggle_show_advanced;

  GtkWidget *shell;

  struct city *pcity;
  struct worklist *pwl;

  void *parent_data;
  WorklistOkCallback ok_callback;
  WorklistCancelCallback cancel_callback;
  
  char *worklist_names_ptrs[MAX_LEN_WORKLIST+1];
  char worklist_names[MAX_LEN_WORKLIST][200];
  int worklist_ids[MAX_LEN_WORKLIST];
  char *worklist_avail_names_ptrs[B_LAST+1+U_LAST+1+1];
  char worklist_avail_names[B_LAST+1+U_LAST+1][200];
  int worklist_avail_ids[B_LAST+1+U_LAST+1+MAX_NUM_WORKLISTS+1];
  int worklist_avail_num_improvements;
  int worklist_avail_num_targets;
};

static GtkWidget *worklist_report_shell = NULL;
static struct worklist_report_dialog *report_dialog;


static void worklist_id_to_name(char buf[], int id, int is_unit, 
				struct city *pcity);

static void rename_worklist_callback(GtkWidget *w, gpointer data);
static void rename_worklist_sub_callback(GtkWidget *w, gpointer data);
static void insert_worklist_callback(GtkWidget *w, gpointer data);
static void delete_worklist_callback(GtkWidget *w, gpointer data);
static void edit_worklist_callback(GtkWidget *w, gpointer data);
static void commit_player_worklist(struct worklist *pwl, void *data);
static void close_worklistreport_callback(GtkWidget *w, gpointer data);
static void update_clist(GtkWidget *list, char *names[]);
static void populate_worklist_report_list(struct worklist_report_dialog *pdialog);

/* Callbacks for the worklist dialog */
static void worklist_list_callback(GtkWidget *w, gint row, gint column,
			    GdkEventButton *e, gpointer data);
static void worklist_avail_callback(GtkWidget *w, gint row, gint column,
			    GdkEventButton *e, gpointer data);
static void insert_into_worklist(struct worklist_dialog *pdialog, 
				 int before, int id);
static void worklist_prepend_callback(GtkWidget *w, gpointer data);
static void worklist_insert_callback(GtkWidget *w, gpointer data);
static void worklist_insert_common_callback(struct worklist_dialog *pdialog,
					    GList *availSelection,
					    int where);
static void worklist_delete_callback(GtkWidget *w, gpointer data);
static void worklist_swap_entries(int i, int j, 
				  struct worklist_dialog *pdialog);
static void worklist_up_callback(GtkWidget *w, gpointer data);
static void worklist_down_callback(GtkWidget *w, gpointer data);
static void worklist_ok_callback(GtkWidget *w, gpointer data);
static void worklist_no_callback(GtkWidget *w, gpointer data);
static void worklist_worklist_help_callback(GtkWidget *w, gpointer data);
static void worklist_avail_help_callback(GtkWidget *w, gpointer data);
static void worklist_help(int id, int is_unit);
static void worklist_show_advanced_callback(GtkWidget *w, gpointer data);
static void worklist_populate_worklist(struct worklist_dialog *pdialog);
static void worklist_populate_targets(struct worklist_dialog *pdialog);




/****************************************************************
...
*****************************************************************/
static gint worklists_dialog_delete_callback(GtkWidget *w, GdkEvent *ev, 
					     gpointer data)
{
  struct worklist_report_dialog *pdialog;
  
  pdialog=(struct worklist_report_dialog *)data;
  
  worklist_report_shell = NULL;

  return FALSE;
}


/****************************************************************
  Bring up the worklist report.
*****************************************************************/
void popup_worklists_dialog(struct player *pplr)
{
  GtkWidget *wshell, *button, *scrolled;
  GtkAccelGroup *accel = gtk_accel_group_new();
  struct worklist_report_dialog *pdialog;
  char *title[1] = {N_("Available worklists")};
  char **clist_title = NULL;


  pdialog = fc_malloc(sizeof(struct worklist_report_dialog));
  pdialog->pplr = pplr;

  /* Report window already open */
  if (worklist_report_shell)
    return;

  wshell = gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(wshell), "delete_event",
		     GTK_SIGNAL_FUNC(worklists_dialog_delete_callback),
		     (gpointer)pdialog);

  gtk_window_set_title(GTK_WINDOW(wshell), _("Edit worklists"));

  gtk_window_set_position(GTK_WINDOW(wshell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(wshell));

  /* Create the list of available worklists. */

  /*   - First, create the column selection widget.  Label the
         columns. */
  clist_title = intl_slist(1, title);
  pdialog->list = gtk_clist_new_with_titles(1, clist_title);
  gtk_clist_column_titles_passive(GTK_CLIST(pdialog->list));
  
  /*   - Make a scrolling window container for the list.  We want
         the scrolly thing because the list may be too long for the
	 fixed-sized window. */
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), pdialog->list);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  /* gtk_widget_set_usize(scrolled, 220, 350); */

  /*   - Place the scrolly thing into the window. */
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wshell)->vbox), scrolled,
		     TRUE, TRUE, 0);


  /*   - Now, let's create some command buttons. */

  /*     + Close */
  button = gtk_accelbutton_new(_("Close"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wshell)->action_area),
		     button, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(close_worklistreport_callback),
		     pdialog);
  gtk_widget_add_accelerator(button, "clicked", accel, GDK_Escape, 0, 0);

  /*     + Edit */
  button = gtk_accelbutton_new(_("Edit"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wshell)->action_area),
		     button, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(edit_worklist_callback),
		     pdialog);

  /*     + Rename */
  button = gtk_accelbutton_new(_("Rename"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wshell)->action_area),
		     button, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(rename_worklist_callback),
		     pdialog);

  /*     + Insert */
  button = gtk_accelbutton_new(_("Insert"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wshell)->action_area),
		     button, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(insert_worklist_callback),
		     pdialog);

  /*     + Delete */
  button = gtk_accelbutton_new(_("Delete"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wshell)->action_area),
		     button, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(delete_worklist_callback),
		     pdialog);


  /*  - Fill in the list of available worklists. */
  populate_worklist_report_list(pdialog);

  /*  - Update the display. */
  update_clist(pdialog->list, pdialog->worklist_names_ptrs);

  /*  - Finally, show the dialog. */
  gtk_widget_show_all(GTK_DIALOG(wshell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(wshell)->action_area);
  gtk_widget_show(wshell);

  /* Keep some data around for later. */
  worklist_report_shell = wshell;
  report_dialog = pdialog;
}



/****************************************************************
...
*****************************************************************/
static gint worklist_dialog_delete_callback(GtkWidget *w, GdkEvent *ev, 
					    gpointer data)
{
  struct worklist_dialog *pdialog;
  
  pdialog=(struct worklist_dialog *)data;
  
  pdialog->worklist = NULL;
  return FALSE;
}


/*************************************************************************
  Bring up a dialog box to edit the given worklist.  If pcity is
  non-NULL, then use pcity to determine the set of units and improvements
  that can be made.  Otherwise, just list everything that technology
  will allow.
*************************************************************************/
GtkWidget* popup_worklist(struct worklist *pwl, struct city *pcity,
			  GtkWidget *parent, void *parent_data,
			  WorklistOkCallback ok_cb,
			  WorklistCancelCallback cancel_cb)
{
  struct worklist_dialog *pdialog;
  
  GtkWidget *wshell, *button, *scrolled, *dialog_hbox;
  GtkWidget *hbox, *vbox;
  GtkAccelGroup *accel = gtk_accel_group_new();

  /* Stuff for the current worklist. */
  char *wl_title[1] = {N_("Current worklist")};
  char **wl_clist_title = NULL;

  /* Stuff for the available targets. */
  char *a_title[1] = {N_("Available targets")};
  char **a_clist_title = NULL;

  pdialog = fc_malloc(sizeof(struct worklist_dialog));

  pdialog->pcity = pcity;
  pdialog->pwl = pwl;
  pdialog->shell = parent;
  pdialog->parent_data = parent_data;
  pdialog->ok_callback = ok_cb;
  pdialog->cancel_callback = cancel_cb;
  pdialog->toggle_show_advanced = NULL;

  wshell = gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(wshell), "delete_event",
		     GTK_SIGNAL_FUNC(worklist_dialog_delete_callback),
		     (gpointer)pdialog);

  gtk_window_set_title(GTK_WINDOW(wshell), _("Production worklist"));

  gtk_set_relative_position(parent, wshell, 20, 20);
  gtk_accel_group_attach(accel, GTK_OBJECT(wshell));

  /* Make an hbox to stick all the window's dialog's widgets into. */
  dialog_hbox = gtk_hbox_new(FALSE, 0);

  /* Display the current worklist. */
  /*   - Make a box to put the worklist and help button into. */
  vbox = gtk_vbox_new(FALSE, 0);

  /*   - Make the list for current worklist. */
  wl_clist_title = intl_slist(1, wl_title);
  pdialog->worklist = gtk_clist_new_with_titles(1, wl_clist_title);
  gtk_clist_column_titles_passive(GTK_CLIST(pdialog->worklist));

  /*   - Fill in the list for the worklist. */
  worklist_populate_worklist(pdialog);
  update_clist(pdialog->worklist, pdialog->worklist_names_ptrs);

  /*   - Do something when a row is (un)selected. */
  gtk_signal_connect(GTK_OBJECT(pdialog->worklist), "select_row",
		     GTK_SIGNAL_FUNC(worklist_list_callback),
		     pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->worklist), "unselect_row",
		     GTK_SIGNAL_FUNC(worklist_list_callback),
		     pdialog);

  /*   - Make a scrolling window for the worklist. */
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), pdialog->worklist);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(scrolled, WORKLIST_LIST_WIDTH, WORKLIST_LIST_HEIGHT);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  /*   - Add a help button near the bottom. */
  hbox = gtk_hbox_new(FALSE, 0);
  button = gtk_button_new_with_label(_("Help"));
  gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 5);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(worklist_worklist_help_callback),
		     pdialog);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(dialog_hbox), vbox, TRUE, TRUE, 10);

  /* Add a column of buttons to manipulate the worklist. */
  vbox = gtk_vbox_new(FALSE, 5);

  button = gtk_button_new_with_label(_("Prepend"));
  pdialog->btn_prepend = button;
  gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(worklist_prepend_callback),
		     pdialog);

  button = gtk_button_new_with_label(_("Insert"));
  pdialog->btn_insert = button;
  gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(worklist_insert_callback),
		     pdialog);

  button = gtk_button_new_with_label(_("Delete"));
  pdialog->btn_delete = button;
  gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(worklist_delete_callback),
		     pdialog);

  button = gtk_button_new_with_label(_("Up"));
  pdialog->btn_up = button;
  gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(worklist_up_callback),
		     pdialog);

  button = gtk_button_new_with_label(_("Down"));
  pdialog->btn_down = button;
  gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(worklist_down_callback),
		     pdialog);

  gtk_widget_set_sensitive(pdialog->btn_prepend, FALSE);
  gtk_widget_set_sensitive(pdialog->btn_insert, FALSE);
  gtk_widget_set_sensitive(pdialog->btn_delete, FALSE);
  gtk_widget_set_sensitive(pdialog->btn_up, FALSE);
  gtk_widget_set_sensitive(pdialog->btn_down, FALSE);

  gtk_box_pack_start(GTK_BOX(dialog_hbox), vbox, FALSE, FALSE, 10);
  
  
  /* Display the list of available targets. */
  /*   - Make a box to put the worklist and help button into. */
  vbox = gtk_vbox_new(FALSE, 0);

  /*   - Make the list for available targets. */
  a_clist_title = intl_slist(1, a_title);
  pdialog->avail = gtk_clist_new_with_titles(1, a_clist_title);
  gtk_clist_column_titles_passive(GTK_CLIST(pdialog->avail));

  /*   - Fill in the list of available build targets. */
  worklist_populate_targets(pdialog);
  update_clist(pdialog->avail, pdialog->worklist_avail_names_ptrs);

  /*   - Do something when a row is (un)selected. */
  gtk_signal_connect(GTK_OBJECT(pdialog->avail), "select_row",
		     GTK_SIGNAL_FUNC(worklist_avail_callback),
		     pdialog);
  gtk_signal_connect(GTK_OBJECT(pdialog->avail), "unselect_row",
		     GTK_SIGNAL_FUNC(worklist_avail_callback),
		     pdialog);

  /*   - Make a scrolling window for the worklist. */
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), pdialog->avail);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(scrolled, WORKLIST_LIST_WIDTH, WORKLIST_LIST_HEIGHT);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  /*   - Add a help button near the bottom. */
  hbox = gtk_hbox_new(FALSE, 0);
  button = gtk_button_new_with_label(_("Help"));
  gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 5);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(worklist_avail_help_callback),
		     pdialog);

  /*   - A checkbox to filter advanced tech targets. */
  button = gtk_check_button_new_with_label(_("Show future targets"));
  pdialog->toggle_show_advanced = button;
  gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 5);
  gtk_signal_connect(GTK_OBJECT(button), "toggled",
		     GTK_SIGNAL_FUNC(worklist_show_advanced_callback),
		     pdialog);

  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(dialog_hbox), vbox, TRUE, TRUE, 10);

  

  /* Add the dialog command buttons (Ok, cancel). */
  button = gtk_button_new_with_label(_("Ok"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wshell)->action_area), 
			     button, FALSE, FALSE, 5);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(worklist_ok_callback),
		     pdialog);

  button = gtk_button_new_with_label(_("Cancel"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wshell)->action_area), 
			     button, FALSE, FALSE, 5);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(worklist_no_callback),
		     pdialog);
  

  /* Add the interesting widgets to the dialog. */
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wshell)->vbox), dialog_hbox, 
		     TRUE, TRUE, 10);

  /* Make all the widgets appear. */
  gtk_widget_show_all(GTK_DIALOG(wshell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(wshell)->action_area);
  gtk_widget_show(wshell);

  /* All done! */
  return wshell;
}


/****************************************************************

*****************************************************************/
void update_worklist_report_dialog(void)
{
  /* If the worklist report is open, force its contents to be 
     update. */
  if (report_dialog) {
    populate_worklist_report_list(report_dialog);
    update_clist(report_dialog->list, report_dialog->worklist_names_ptrs);
  }
}

/****************************************************************

*****************************************************************/
void worklist_id_to_name(char buf[], int id, int is_unit, 
			 struct city *pcity)
{
  if (is_unit)
    sprintf(buf, "%s (%d)",
	    get_unit_name(id), get_unit_type(id)->build_cost);
  else if (pcity)
    sprintf(buf, "%s (%d)",
	    get_imp_name_ex(pcity, id), get_improvement_type(id)->build_cost);
  else
    sprintf(buf, "%s (%d)",
	    get_improvement_name(id), get_improvement_type(id)->build_cost);
}



/****************************************************************

*****************************************************************/
void rename_worklist_callback(GtkWidget *w, gpointer data)
{
  struct worklist_report_dialog *pdialog;
  GList *selection;

  pdialog = (struct worklist_report_dialog *)data;
  selection = GTK_CLIST(pdialog->list)->selection;

  if (! selection)
    return;

  pdialog->wl_idx = (int)selection->data;

  input_dialog_create(worklist_report_shell,
		      _("Rename Worklist"),
		      _("What should the new name be?"),
		      pdialog->pplr->worklists[pdialog->wl_idx].name,
		      (void *)rename_worklist_sub_callback,
		      (gpointer)pdialog,
		      (void *)rename_worklist_sub_callback,
		      (gpointer)NULL);
}


/****************************************************************

*****************************************************************/
void rename_worklist_sub_callback(GtkWidget *w, gpointer data)
{
  struct worklist_report_dialog *pdialog;
  struct packet_player_request packet;

  pdialog = (struct worklist_report_dialog *)data;

  if (pdialog) {
    packet.wl_idx = pdialog->wl_idx;
    copy_worklist(&packet.worklist, 
		  &pdialog->pplr->worklists[pdialog->wl_idx]);
    strncpy(packet.worklist.name, input_dialog_get_input(w), MAX_LEN_NAME);
    packet.worklist.name[MAX_LEN_NAME-1] = '\0';
    send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_WORKLIST);
  }
  
  input_dialog_destroy(w);
}

/****************************************************************
  Create a new worklist.
*****************************************************************/
void insert_worklist_callback(GtkWidget *w, gpointer data)
{
  struct worklist_report_dialog *pdialog;
  struct packet_player_request packet;
  int j;

  pdialog = (struct worklist_report_dialog *)data;

  /* Find the next free worklist for this player */

  for (j = 0; j < MAX_NUM_WORKLISTS; j++)
    if (!pdialog->pplr->worklists[j].is_valid)
      break;

  /* No more worklist slots free.  (!!!Maybe we should tell the user?) */
  if (j == MAX_NUM_WORKLISTS)
    return;

  /* Validate this slot. */
  init_worklist(&packet.worklist);
  packet.worklist.is_valid = 1;
  strcpy(packet.worklist.name, _("empty worklist"));
  packet.wl_idx = j;

  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_WORKLIST);
}

/****************************************************************
  Remove the current worklist.  This request is made by sliding
  up all lower worklists to fill in the slot that's being deleted.
*****************************************************************/
void delete_worklist_callback(GtkWidget *w, gpointer data)
{
  struct worklist_report_dialog *pdialog;
  struct packet_player_request packet;
  GList *selection;
  int i, j;

  pdialog = (struct worklist_report_dialog *)data;
  selection = GTK_CLIST(pdialog->list)->selection;

  if (! selection)
    return;

  /* Look for the last free worklist */
  for (i = 0; i < MAX_NUM_WORKLISTS; i++)
    if (!pdialog->pplr->worklists[i].is_valid)
      break;

  for (j = (int)selection->data; j < i-1; j++) {
    copy_worklist(&packet.worklist, &pdialog->pplr->worklists[j+1]);
    packet.wl_idx = j;

    send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_WORKLIST);
  }

  /* The last worklist in the set is no longer valid -- it's been slid up
     one slot. */
  packet.worklist.name[0] = '\0';
  packet.worklist.is_valid = 0;
  packet.wl_idx = i-1;
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_WORKLIST);
}

/****************************************************************

*****************************************************************/
void edit_worklist_callback(GtkWidget *w, gpointer data)
{
  struct worklist_report_dialog *pdialog;
  GList *selection;

  pdialog = (struct worklist_report_dialog *)data;

  selection = GTK_CLIST(pdialog->list)->selection;

  if (! selection)
    return;

  pdialog->wl_idx = (int)selection->data;

  popup_worklist(pdialog->worklist_ptr[pdialog->wl_idx], NULL, 
		 worklist_report_shell,
		 pdialog, commit_player_worklist, NULL);
}

/****************************************************************
  Commit the changes to the worklist for this player.
*****************************************************************/
void commit_player_worklist(struct worklist *pwl, void *data)
{
  struct worklist_report_dialog *pdialog;
  struct packet_player_request packet;

  pdialog = (struct worklist_report_dialog *)data;

  copy_worklist(&packet.worklist, pwl);
  packet.wl_idx = pdialog->wl_idx;

  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_WORKLIST);
}


/****************************************************************

*****************************************************************/
static void close_worklistreport_callback(GtkWidget *w, gpointer data)
{
  struct worklist_report_dialog *pdialog;

  pdialog = (struct worklist_report_dialog *)data;

  /* w                          is the close button
     w->parent                  is the action_area
     w->parent->parent          is the dialog shell
     w->parent->parent->parent  is the dialog itself
     */
  gtk_widget_destroy(w->parent->parent->parent);
  worklist_report_shell = NULL;

  free(report_dialog);
  report_dialog = NULL;
}


void update_clist(GtkWidget *list, char *names[])
{
  int i;

  gtk_clist_freeze(GTK_CLIST(list));
  gtk_clist_clear(GTK_CLIST(list));

  i = 0;
  while (names[i] != NULL) {
    gtk_clist_append(GTK_CLIST(list), &names[i]);
    i++;
  }

  gtk_clist_thaw(GTK_CLIST(list));
}



/****************************************************************
  Fill in the worklist arrays in the pdialog.
*****************************************************************/
void populate_worklist_report_list(struct worklist_report_dialog *pdialog)
{
  int i, n;

  for (i = 0, n = 0; i < MAX_NUM_WORKLISTS; i++) {
    if (pdialog->pplr->worklists[i].is_valid) {
      strcpy(pdialog->worklist_names[n], pdialog->pplr->worklists[i].name);
      pdialog->worklist_names_ptrs[n] = pdialog->worklist_names[n];
      pdialog->worklist_ptr[n] = &pdialog->pplr->worklists[i];

      n++;
    }
  }
  
  /* Terminators */
  pdialog->worklist_names_ptrs[n] = NULL;
}



/****************************************************************
  User selected one of the worklist items
*****************************************************************/
void worklist_list_callback(GtkWidget *w, gint row, gint column,
			    GdkEventButton *e, gpointer data)
{
  GList *selection;
  struct worklist_dialog *pdialog;
  
  pdialog=(struct worklist_dialog *)data;
  
  selection = GTK_CLIST(pdialog->worklist)->selection;
  if (! selection) {
    /* Deselected */
    gtk_widget_set_sensitive(pdialog->btn_delete, FALSE);
    gtk_widget_set_sensitive(pdialog->btn_up, FALSE);
    gtk_widget_set_sensitive(pdialog->btn_down, FALSE);
  } else {
    gtk_widget_set_sensitive(pdialog->btn_delete, TRUE);
    gtk_widget_set_sensitive(pdialog->btn_up, TRUE);
    gtk_widget_set_sensitive(pdialog->btn_down, TRUE);
  }
}


/****************************************************************
  User selected one of the available items
*****************************************************************/
void worklist_avail_callback(GtkWidget *w, gint row, gint column,
			    GdkEventButton *e, gpointer data)
{
  struct worklist_dialog *pdialog;
  GList *selection;
  
  pdialog=(struct worklist_dialog *)data;
  
  selection = GTK_CLIST(pdialog->avail)->selection;

  if (! selection) {
    /* Deselected */
    gtk_widget_set_sensitive(pdialog->btn_prepend, FALSE);
    gtk_widget_set_sensitive(pdialog->btn_insert, FALSE);
  } else {
    gtk_widget_set_sensitive(pdialog->btn_prepend, TRUE);
    gtk_widget_set_sensitive(pdialog->btn_insert, TRUE);
  }
}

/****************************************************************

*****************************************************************/
void insert_into_worklist(struct worklist_dialog *pdialog, 
			  int before, int id)
{
  int i, first_free;
  int target, is_unit;

  /* Find the first free element in the worklist */
  for (first_free = 0; first_free < MAX_LEN_WORKLIST; first_free++)
    if (pdialog->worklist_ids[first_free] == WORKLIST_END)
      break;

  if (first_free == MAX_LEN_WORKLIST)
    /* No room left in the worklist! */
    return;

  if (first_free < before && before != MAX_LEN_WORKLIST)
    /* True weirdness. */
    return;

  if (before < MAX_LEN_WORKLIST) {
    /* Slide all the later elements in the worklist down. */
    for (i = first_free; i > before; i--) {
      pdialog->worklist_ids[i] = pdialog->worklist_ids[i-1];
      strcpy(pdialog->worklist_names[i], pdialog->worklist_names[i-1]);
      pdialog->worklist_names_ptrs[i] = pdialog->worklist_names[i];
    }
  } else {
    /* Append the new id, not insert. */
    before = first_free;
  }

  first_free++;
  pdialog->worklist_ids[first_free] = WORKLIST_END;
  pdialog->worklist_names_ptrs[first_free] = NULL;

  pdialog->worklist_ids[before] = id;
  
  if (id >= B_LAST) {
    target = id - B_LAST;
    is_unit = 1;
  } else {
    target = id;
    is_unit = 0;
  }

  worklist_id_to_name(pdialog->worklist_names[before],
		      target, is_unit, pdialog->pcity);
  pdialog->worklist_names_ptrs[before] = pdialog->worklist_names[before];
}

/****************************************************************
  Prepend the selected build target into the worklist.
*****************************************************************/
void worklist_prepend_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog = (struct worklist_dialog *)data;
  GList *availSelection = GTK_CLIST(pdialog->avail)->selection;

  worklist_insert_common_callback(pdialog, availSelection, 0);

  if (pdialog->worklist_ids[1] != WORKLIST_END) {
    gtk_widget_set_sensitive(pdialog->btn_delete, TRUE);
    gtk_widget_set_sensitive(pdialog->btn_up, TRUE);
    gtk_widget_set_sensitive(pdialog->btn_down, TRUE);
  }
}

/****************************************************************
  Insert the selected build target into the worklist.
*****************************************************************/
void worklist_insert_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog = (struct worklist_dialog *)data;
  GList *listSelection = GTK_CLIST(pdialog->worklist)->selection;
  GList *availSelection = GTK_CLIST(pdialog->avail)->selection;
  int where;

  if (! listSelection)
    where = MAX_LEN_WORKLIST;
  else
    where = (int)listSelection->data;

  worklist_insert_common_callback(pdialog, availSelection, where);
}

/****************************************************************
  Do the actual UI work of inserting a target into the worklist.
*****************************************************************/
void worklist_insert_common_callback(struct worklist_dialog *pdialog,
				     GList *availSelection,
				     int where)
{
  int target;
  int i, idx, len;

  /* Is there anything selected to insert? */
  if (! availSelection)
    return;

  idx = (int)availSelection->data;

  /* Pick out the target and its type. */
  target = pdialog->worklist_avail_ids[idx];

  if (idx >= pdialog->worklist_avail_num_targets) {
    /* target is a global worklist id */
    struct player *pplr = city_owner(pdialog->pcity);
    int wl_idx = pdialog->worklist_avail_ids[idx];
    struct worklist *pwl = &pplr->worklists[wl_idx];

    for (i = 0; i < MAX_LEN_WORKLIST && pwl->ids[i] != WORKLIST_END; i++) {
      insert_into_worklist(pdialog, where, pwl->ids[i]);
      if (where < MAX_LEN_WORKLIST)
	where++;
    }
  } else if (idx >= pdialog->worklist_avail_num_improvements) {
    /* target is an improvement or wonder */
    insert_into_worklist(pdialog, where, target+B_LAST);
    where++;
  } else {
    /* target is a unit */
    insert_into_worklist(pdialog, where, target);
    where++;
  }

  /* Update the list with the actual data */
  update_clist(pdialog->worklist, pdialog->worklist_names_ptrs);

  /* How long is the new worklist? */
  for (len = 0; len < MAX_LEN_WORKLIST; len++)
    if (pdialog->worklist_ids[len] == WORKLIST_END)
      break;

  /* Re-select the item that was previously selected. */
  if (where < len)
    gtk_clist_select_row((GtkCList *)pdialog->worklist, where, 0);
}

/****************************************************************
  Remove the selected target in the worklist.
*****************************************************************/
void worklist_delete_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog = (struct worklist_dialog *)data;
  GList *selection = GTK_CLIST(pdialog->worklist)->selection;
  int i, j, k;

  if (! selection)
    return;

  k = (int)selection->data;

  /* Find the last element in the worklist */
  for (i = 0; i < MAX_LEN_WORKLIST; i++)
    if (pdialog->worklist_ids[i] == WORKLIST_END)
      break;

  /* Slide all the later elements in the worklist up. */
  for (j = k; j < i; j++) {
    pdialog->worklist_ids[j] = pdialog->worklist_ids[j+1];
    strcpy(pdialog->worklist_names[j], pdialog->worklist_names[j+1]);
    pdialog->worklist_names_ptrs[j] = pdialog->worklist_names[j];
  }

  i--;
  pdialog->worklist_ids[i] = WORKLIST_END;
  pdialog->worklist_names_ptrs[i] = 0;

  if (i == 0 || k >= i) {
    gtk_widget_set_sensitive(pdialog->btn_delete, FALSE);
    gtk_widget_set_sensitive(pdialog->btn_up, FALSE);
    gtk_widget_set_sensitive(pdialog->btn_down, FALSE);
  }    

  /* Update the list with the actual data */
  update_clist(pdialog->worklist, pdialog->worklist_names_ptrs);

  /* Select the item immediately after the item we just deleted,
     if there is such an item. */
  if (k < i)
    gtk_clist_select_row((GtkCList *)pdialog->worklist, k, 0);
}

/****************************************************************

*****************************************************************/
void worklist_swap_entries(int i, int j, struct worklist_dialog *pdialog)
{
  int id;
  char name[200];

  id = pdialog->worklist_ids[i];
  strcpy(name, pdialog->worklist_names[i]);

  pdialog->worklist_ids[i] = pdialog->worklist_ids[j];
  strcpy(pdialog->worklist_names[i], pdialog->worklist_names[j]);
  pdialog->worklist_names_ptrs[i] = pdialog->worklist_names[i];

  pdialog->worklist_ids[j] = id;
  strcpy(pdialog->worklist_names[j], name);
  pdialog->worklist_names_ptrs[j] = pdialog->worklist_names[j];
}  

/****************************************************************
  Swap the selected element with its upward neighbor
*****************************************************************/
void worklist_up_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog = (struct worklist_dialog *)data;
  GList *selection = GTK_CLIST(pdialog->worklist)->selection;
  int idx;

  if (! selection)
    return;

  idx = (int)selection->data;

  if (idx == 0)
    return;

  worklist_swap_entries(idx, idx-1, pdialog);

  update_clist(pdialog->worklist, pdialog->worklist_names_ptrs);
  gtk_clist_select_row((GtkCList *)pdialog->worklist, idx-1, 0);
}

/****************************************************************
 Swap the selected element with its downward neighbor
*****************************************************************/
void worklist_down_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog = (struct worklist_dialog *)data;
  GList *selection = GTK_CLIST(pdialog->worklist)->selection;
  int idx;

  if (! selection)
    return;

  idx = (int)selection->data;

  if (idx == MAX_LEN_WORKLIST-1 ||
      pdialog->worklist_ids[idx+1] == WORKLIST_END)
    return;

  worklist_swap_entries(idx, idx+1, pdialog);

  update_clist(pdialog->worklist,  pdialog->worklist_names_ptrs);
  gtk_clist_select_row((GtkCList *)pdialog->worklist, idx+1, 0);
}

/****************************************************************
  User wants to save the worklist.
*****************************************************************/
void worklist_ok_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog;
  struct worklist wl;
  int i;
  
  pdialog=(struct worklist_dialog *)data;
  
  /* Fill in this worklist with the parameters set in the worklist 
     dialog. */
  init_worklist(&wl);
  
  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    wl.ids[i] = pdialog->worklist_ids[i];
  }
  strcpy(wl.name, pdialog->pwl->name);
  wl.is_valid = pdialog->pwl->is_valid;
  
  /* Invoke the dialog's parent-specified callback */
  if (pdialog->ok_callback)
    (*pdialog->ok_callback)(&wl, pdialog->parent_data);

  /* Cleanup. */
  gtk_widget_destroy(w->parent->parent->parent);
  gtk_widget_set_sensitive(pdialog->shell, TRUE);

  pdialog->worklist = NULL;
  free(pdialog);
}

/****************************************************************
  User cancelled from the Worklist dialog.
*****************************************************************/
void worklist_no_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog;
  
  pdialog=(struct worklist_dialog *)data;
  
  /* Invoke the dialog's parent-specified callback */
  if (pdialog->cancel_callback)
    (*pdialog->cancel_callback)(pdialog->parent_data);

  gtk_widget_destroy(w->parent->parent->parent);
  gtk_widget_set_sensitive(pdialog->shell, TRUE);

  pdialog->worklist = NULL;
  free(pdialog);
}

/****************************************************************
  User asked for help from the Worklist dialog.  If there's 
  something highlighted, bring up the help for that item.  Else,
  bring up help for improvements.
*****************************************************************/
void worklist_worklist_help_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog;
  GList *selection;
  int id, is_unit = 0;

  pdialog=(struct worklist_dialog *)data;

  selection = GTK_CLIST(pdialog->worklist)->selection;
  if(selection) {
    id = pdialog->worklist_ids[(int)selection->data];
    if (id >= B_LAST) {
      id -= B_LAST;
      is_unit = 1;
    } else {
      is_unit = 0;
    }
  } else {
    id = -1;
  }

  worklist_help(id, is_unit);
}

void worklist_avail_help_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog;
  GList *selection;
  int id, is_unit = 0;

  pdialog=(struct worklist_dialog *)data;

  selection = GTK_CLIST(pdialog->avail)->selection;
  if(selection) {
    id = pdialog->worklist_avail_ids[(int)selection->data];
    is_unit = (int)selection->data >= pdialog->worklist_avail_num_improvements;
  } else {
    id = -1;
  }

  worklist_help(id, is_unit);
}

void worklist_help(int id, int is_unit)
{
  if(id >= 0) {
    if (is_unit) {
      popup_help_dialog_typed(get_unit_type(id)->name, HELP_UNIT);
    } else if(is_wonder(id)) {
      popup_help_dialog_typed(get_improvement_name(id), HELP_WONDER);
    } else {
      popup_help_dialog_typed(get_improvement_name(id), HELP_IMPROVEMENT);
    }
  }
  else
    popup_help_dialog_string(HELP_IMPROVEMENTS_ITEM);
}

/**************************************************************************
  Change the label of the Show Advanced toggle.  Also updates the list
  of available targets.
**************************************************************************/
void worklist_show_advanced_callback(GtkWidget *w, gpointer data)
{
  struct worklist_dialog *pdialog = (struct worklist_dialog *)data;

  worklist_populate_targets(pdialog);
  update_clist(pdialog->avail, pdialog->worklist_avail_names_ptrs);
}


/****************************************************************
  Fill in the worklist arrays in the pdialog.
*****************************************************************/
void worklist_populate_worklist(struct worklist_dialog *pdialog)
{
  int i, n;
  int id;
  int target, is_unit;

  n = 0;
  if (pdialog->pcity) {
    /* First element is the current build target of the city. */
    id = pdialog->pcity->currently_building;

    worklist_id_to_name(pdialog->worklist_names[n],
			id, pdialog->pcity->is_building_unit, pdialog->pcity);

    if (pdialog->pcity->is_building_unit)
      id += B_LAST;
    pdialog->worklist_names_ptrs[n] = pdialog->worklist_names[n];
    pdialog->worklist_ids[n] = id;
    n++;
  }

  /* Fill in the rest of the worklist list */
  for (i = 0; n < MAX_LEN_WORKLIST &&
	 pdialog->pwl->ids[i] != WORKLIST_END; i++, n++) {
    worklist_peek_ith(pdialog->pwl, &target, &is_unit, i);
    id = worklist_peek_id_ith(pdialog->pwl, i);

    worklist_id_to_name(pdialog->worklist_names[n],
			target, is_unit, pdialog->pcity);

    pdialog->worklist_names_ptrs[n] = pdialog->worklist_names[n];
    pdialog->worklist_ids[n] = id;
  }
  
  /* Terminators */
  pdialog->worklist_names_ptrs[n] = NULL;
  while (n != MAX_LEN_WORKLIST)
    pdialog->worklist_ids[n++] = WORKLIST_END;
}

/****************************************************************
  Fill in the target arrays in the pdialog.
*****************************************************************/
void worklist_populate_targets(struct worklist_dialog *pdialog)
{
  int i, n;
  struct player *pplr = game.player_ptr;
  int advanced_tech;
  int can_build, can_eventually_build;
  

  n = 0;

  /* Is the worklist limited to just the current targets, or
     to any available and future targets? */
  if (pdialog->toggle_show_advanced != NULL &&
      GTK_TOGGLE_BUTTON(pdialog->toggle_show_advanced)->active ==
      WORKLIST_ADVANCED_TARGETS)
    advanced_tech = TRUE;
  else
    advanced_tech = FALSE;
 
  /*     + First, improvements and Wonders. */
  for(i=0; i<B_LAST; i++) {
    /* Can the player (eventually) build this improvement? */
    can_build = could_player_build_improvement(pplr,i);
    can_eventually_build = could_player_eventually_build_improvement(pplr,i);

    /* If there's a city, can the city build the improvement? */
    if (pdialog->pcity) {
      can_build = can_build && can_build_improvement(pdialog->pcity, i);
    /* !!! Note, this is an "issue":
       I'm not performing this check right now b/c I've heard that the
       can_build_* stuff in common/city.c is in flux. */
      /*
      can_eventually_build = can_eventually_build &&
	can_eventually_build_improvement(pdialog->pcity, id);
	*/
    }
    
    if (( advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build)) {
      worklist_id_to_name(pdialog->worklist_avail_names[n],
			  i, 0, pdialog->pcity);
      pdialog->worklist_avail_names_ptrs[n]=pdialog->worklist_avail_names[n];
      pdialog->worklist_avail_ids[n++]=i;
    }
  }
  pdialog->worklist_avail_num_improvements=n;

  /*     + Second, units. */
  for(i=0; i<game.num_unit_types; i++) {
    /* Can the player (eventually) build this improvement? */
    can_build = can_player_build_unit(pplr,i);
    can_eventually_build = can_player_eventually_build_unit(pplr,i);

    /* If there's a city, can the city build the improvement? */
    if (pdialog->pcity) {
      can_build = can_build && can_build_unit(pdialog->pcity, i);
    /* !!! Note, this is another "issue" (same as above). */
      /*
      can_eventually_build = can_eventually_build &&
	can_eventually_build_unit(pdialog->pcity, id);
	*/
    }

    if (( advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build)) {
      worklist_id_to_name(pdialog->worklist_avail_names[n],
			  i, 1, pdialog->pcity);
      pdialog->worklist_avail_names_ptrs[n]=pdialog->worklist_avail_names[n];
      pdialog->worklist_avail_ids[n++]=i;
    }
  }
  pdialog->worklist_avail_num_targets=n;

  /*     + Finally, the global worklists. */
  if (pdialog->pcity) {
    /* Now fill in the global worklists. */
    for (i = 0; i < MAX_NUM_WORKLISTS; i++)
      if (pplr->worklists[i].is_valid) {
	strcpy(pdialog->worklist_avail_names[n], pplr->worklists[i].name);
	pdialog->worklist_avail_names_ptrs[n]=pdialog->worklist_avail_names[n];
	pdialog->worklist_avail_ids[n++]=i;
      }
  }

  pdialog->worklist_avail_names_ptrs[n]=0;
}
