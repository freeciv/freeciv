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
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "mem.h"
#include "packets.h"
#include "support.h"
#include "worklist.h"

#include "chatline_common.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"

#include "citydlg.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"

#include "wldlg.h"

#define WORKLIST_ADVANCED_TARGETS  	1
#define WORKLIST_CURRENT_TARGETS   	0
#define COLUMNS				4
#define BUFFER_SIZE			100
#define NUM_TARGET_TYPES		3

/*
 * The Worklist Report dialog shows all the global worklists that the
 * player has defined.  There can be at most MAX_NUM_WORKLISTS global
 * worklists.
 */
struct worklist_report {
  GtkWidget *list, *shell, *btn_edit, *btn_rename, *btn_delete;
  struct player *pplr;
  char worklist_names[MAX_NUM_WORKLISTS][MAX_LEN_NAME];
  char *worklist_names_ptrs[MAX_NUM_WORKLISTS + 1];
  struct worklist *worklist_ptr[MAX_NUM_WORKLISTS];
  int wl_idx;
};

static struct worklist_report *report_dialog;

/* whether global worklists are first or last in the targets list */
static int are_worklists_first = 1;

/* Callbacks for the global worklist report dialog */

static void global_dialog_cleanup(struct worklist_report *preport);
static gint global_dialog_delete_callback(GtkWidget * w, GdkEvent * ev,
					  gpointer data);

static void global_close_report_callback(GtkWidget * w, gpointer data);
static void global_edit_callback(GtkWidget * w, gpointer data);
static void global_rename_callback(GtkWidget * w, gpointer data);
static void global_rename_sub_callback(const char *input, gpointer data);
static void global_insert_callback(GtkWidget * w, gpointer data);
static void global_delete_callback(GtkWidget * w, gpointer data);
static void global_select_list_callback(GtkWidget * w, gint row,
					gint column, GdkEvent * ev,
					gpointer data);

static void global_commit_worklist(struct worklist *pwl, void *data);

static void global_list_update(struct worklist_report *preport);

/* Callbacks for the worklist editor */

static void copy_editor_to_worklist(struct worklist_editor *peditor,
				    struct worklist *pwl);
static void copy_worklist_to_editor(struct worklist *pwl,
				    struct worklist_editor *peditor,
				    int where);

static gboolean keyboard_handler(GtkWidget * widget, GdkEventKey * event,
				 gpointer * data);
static gboolean worklist_key_pressed_callback(GtkWidget * w,
					      GdkEventKey * ev,
					      gpointer data);
static gboolean targets_key_pressed_callback(GtkWidget * w,
					     GdkEventKey * ev,
					     gpointer data);

static void worklist_select_callback(GtkWidget * w, gint row, gint column,
				     GdkEvent * ev, gpointer data);
static void targets_select_callback(GtkWidget * w, gint row, gint column,
				    GdkEvent * ev, gpointer data);
static void targets_type_rotate_callback(GtkWidget * w, int col,
					 gpointer data);

static void worklist_prep(struct worklist_editor *peditor);
static void worklist_insert_item(struct worklist_editor *peditor);
static void worklist_really_insert_item(struct worklist_editor *peditor,
					int before, int wid);
static void worklist_remove_item(struct worklist_editor *peditor);

static void worklist_swap_entries(int i, int j,
				  struct worklist_editor *peditor);
static void worklist_swap_up_callback(GtkWidget * w, gpointer data);
static void worklist_swap_down_callback(GtkWidget * w, gpointer data);

static void worklist_ok_callback(GtkWidget * w, gpointer data);
static void worklist_no_callback(GtkWidget * w, gpointer data);
static void targets_show_advanced_callback(GtkWidget * w, gpointer data);

static void worklist_list_update(struct worklist_editor *peditor);
static void targets_list_update(struct worklist_editor *peditor);
static void update_changed_sensitive(struct worklist_editor *peditor);
static void targets_help_callback(GtkWidget * w, gpointer data);
static void worklist_help(int id, bool is_unit);
static void cleanup_worklist_editor(struct worklist_editor *peditor);
static gint worklist_editor_delete_callback(GtkWidget * w, GdkEvent * ev,
					    gpointer data);

/****************************************************************
  Bring up the global worklist report.
*****************************************************************/
void popup_worklists_report(struct player *pplr)
{
  GtkWidget *button, *scrolled;
  GtkAccelGroup *accel;
  const char *title[1] = { N_("Available worklists") };
  static char **clist_title = NULL;

  /* Report window already open */
  if (report_dialog && report_dialog->shell)
    return;

  accel = gtk_accel_group_new();

  assert(!report_dialog);
  assert(pplr != NULL);

  report_dialog = fc_malloc(sizeof(struct worklist_report));
  report_dialog->pplr = pplr;

  report_dialog->shell = gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(report_dialog->shell), "delete_event",
		     GTK_SIGNAL_FUNC(global_dialog_delete_callback), NULL);

  gtk_window_set_title(GTK_WINDOW(report_dialog->shell),
		       _("Edit worklists"));

  gtk_window_set_position(GTK_WINDOW(report_dialog->shell),
			  GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(report_dialog->shell));

  /* Create the list of global worklists. */

  /* - First, create the column selection widget.  Label the
     columns. */
  if (!clist_title) {
    clist_title = intl_slist(1, title);
  }
  report_dialog->list = gtk_clist_new_with_titles(1, clist_title);
  gtk_clist_column_titles_passive(GTK_CLIST(report_dialog->list));

  gtk_signal_connect(GTK_OBJECT(report_dialog->list), "select_row",
		     GTK_SIGNAL_FUNC(global_select_list_callback),
		     report_dialog);
  gtk_signal_connect(GTK_OBJECT(report_dialog->list), "unselect_row",
		     GTK_SIGNAL_FUNC(global_select_list_callback),
		     report_dialog);

  /* - Make a scrolling window container for the list.  We want
     the scrolly thing because the list may be too long for the
     fixed-sized window. */
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), report_dialog->list);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC,
				 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(scrolled, 220, 250); 

  /* - Place the scrolly thing into the window. */
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(report_dialog->shell)->vbox),
		     scrolled, TRUE, TRUE, 0);

  /* - Now, let's create some command buttons. */

  /*     + Close */
  button = gtk_accelbutton_new(_("Close"), accel);
  gtk_box_pack_start(GTK_BOX
		     (GTK_DIALOG(report_dialog->shell)->action_area),
		     button, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(global_close_report_callback),
		     report_dialog);
  gtk_widget_add_accelerator(button, "clicked", accel, GDK_Return, 0, 0);
  gtk_widget_add_accelerator(button, "clicked", accel, GDK_Escape, 0, 0);

  /*     + Edit */
  report_dialog->btn_edit = gtk_accelbutton_new(_("Edit"), accel);
  gtk_box_pack_start(GTK_BOX
		     (GTK_DIALOG(report_dialog->shell)->action_area),
		     report_dialog->btn_edit, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(report_dialog->btn_edit), "clicked",
		     GTK_SIGNAL_FUNC(global_edit_callback), report_dialog);
  gtk_widget_set_sensitive(report_dialog->btn_edit, FALSE);

  /*     + Rename */
  report_dialog->btn_rename = gtk_accelbutton_new(_("Rename"), accel);
  gtk_box_pack_start(GTK_BOX
		     (GTK_DIALOG(report_dialog->shell)->action_area),
		     report_dialog->btn_rename, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(report_dialog->btn_rename), "clicked",
		     GTK_SIGNAL_FUNC(global_rename_callback),
		     report_dialog);
  gtk_widget_set_sensitive(report_dialog->btn_rename, FALSE);

  /*     + Insert */
  button = gtk_accelbutton_new(_("Insert"), accel);
  gtk_box_pack_start(GTK_BOX
		     (GTK_DIALOG(report_dialog->shell)->action_area),
		     button, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(global_insert_callback),
		     report_dialog);

  /*     + Delete */
  report_dialog->btn_delete = gtk_accelbutton_new(_("Delete"), accel);
  gtk_box_pack_start(GTK_BOX
		     (GTK_DIALOG(report_dialog->shell)->action_area),
		     report_dialog->btn_delete, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(report_dialog->btn_delete), "clicked",
		     GTK_SIGNAL_FUNC(global_delete_callback),
		     report_dialog);

  /*  - Update the worklists and clist. */
  global_list_update(report_dialog);

  /*  - Finally, show the dialog. */
  gtk_widget_show_all(GTK_DIALOG(report_dialog->shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(report_dialog->shell)->action_area);
  gtk_widget_show(report_dialog->shell);
}

/*************************************************************************
   Bring up a dialog box to edit the given worklist.  The dialog is
   just a shell for the embedded worklist editor.
*************************************************************************/
GtkWidget *popup_worklist(struct worklist *pwl, struct city *pcity,
			  GtkWidget * parent, void *user_data,
			  WorklistOkCallback ok_cb,
			  WorklistCancelCallback cancel_cb)
{
  GtkWidget *dialog;
  struct worklist_editor *peditor;

  dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(GTK_WINDOW(dialog), _("Production worklist"));
  gtk_set_relative_position(parent, dialog, 20, 20);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 360);

  peditor =
      create_worklist_editor(pwl, pcity, user_data, ok_cb, cancel_cb, 0);
  update_worklist_editor(peditor);
  gtk_container_add(GTK_CONTAINER(dialog), peditor->shell);

  gtk_signal_connect(GTK_OBJECT(dialog), "key_press_event",
		     GTK_SIGNAL_FUNC(peditor->keyboard_handler), peditor);

  gtk_widget_show_all(dialog);

  return dialog;
}

/*************************************************************************
   Create a worklist editor and return it. If pcity is
   non-NULL, then use pcity to determine the set of units and
   improvements that can be made.  Otherwise, just list everything that
   technology will allow.
*************************************************************************/
struct worklist_editor *create_worklist_editor(struct worklist *pwl,
					       struct city *pcity,
					       void *user_data,
					       WorklistOkCallback ok_cb,
					       WorklistCancelCallback
					       cancel_cb,
					       int embedded_in_city)
{
  int i;
  struct worklist_editor *peditor;
  GtkWidget *action_area;
  GtkWidget *button, *scrolled, *dialog_hbox, *hbox, *vbox, *frame;
  GtkAccelGroup *accel = gtk_accel_group_new();

  static char **wl_clist_titles = NULL;
  const char *wl_titles[] = { N_("Type"),
    N_("Info"),
    N_("Cost")
  };

  static char **avail_clist_titles = NULL;
  const char *avail_titles[] = { N_("Type"),
    N_("Info"),
    N_("Cost"),
    N_("Turns")
  };

  if (!wl_clist_titles)
    wl_clist_titles = intl_slist(3, wl_titles);
  if (!avail_clist_titles)
    avail_clist_titles = intl_slist(4, avail_titles);

  peditor = fc_malloc(sizeof(struct worklist_editor));

  peditor->pcity = pcity;
  peditor->pwl = pwl;
  peditor->user_data = user_data;
  peditor->ok_callback = ok_cb;
  peditor->cancel_callback = cancel_cb;
  peditor->embedded_in_city = embedded_in_city;
  peditor->changed = 0;
  peditor->keyboard_handler = keyboard_handler;

  peditor->shell = gtk_vbox_new(FALSE, 0);
  gtk_object_set_user_data(GTK_OBJECT(peditor->shell), peditor);

  gtk_accel_group_attach(accel, GTK_OBJECT(peditor->shell));

  /* Make an hbox to stick the dialog's clists and their buttons into. */

  dialog_hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(peditor->shell), dialog_hbox, TRUE, TRUE, 10);

  /*** the current worklist. ***/

  frame = gtk_frame_new(_("Current worklist"));
  gtk_box_pack_start(GTK_BOX(dialog_hbox), frame, FALSE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  /* Make a scrolling window for the worklist. */

  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  /* Make the list for current worklist. */

  peditor->worklist = gtk_clist_new_with_titles(3, wl_clist_titles);
  gtk_container_add(GTK_CONTAINER(scrolled), peditor->worklist);

  for (i = 0; i < 3; i++) {
    gtk_clist_set_column_auto_resize(GTK_CLIST(peditor->worklist), i,
				     TRUE);
  }
  gtk_clist_set_column_justification(GTK_CLIST(peditor->worklist), 2,
				     GTK_JUSTIFY_RIGHT);
  gtk_clist_column_titles_passive(GTK_CLIST(peditor->worklist));

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

  /* Add Up/Down buttons to manipulate the worklist. */

  peditor->btn_up = gtk_button_new_with_label(_("Up"));
  gtk_box_pack_start(GTK_BOX(hbox), peditor->btn_up, TRUE, TRUE, 0);
  gtk_widget_set_sensitive(peditor->btn_up, FALSE);

  peditor->btn_down = gtk_button_new_with_label(_("Down"));
  gtk_box_pack_start(GTK_BOX(hbox), peditor->btn_down, TRUE, TRUE, 0);
  gtk_widget_set_sensitive(peditor->btn_down, FALSE);

  /* signals for the current worklist */

  gtk_signal_connect(GTK_OBJECT(peditor->worklist), "select_row",
		     GTK_SIGNAL_FUNC(worklist_select_callback), peditor);

  gtk_signal_connect(GTK_OBJECT(peditor->worklist), "unselect_row",
		     GTK_SIGNAL_FUNC(worklist_select_callback), peditor);

  gtk_signal_connect(GTK_OBJECT(peditor->worklist), "key-press-event",
		     GTK_SIGNAL_FUNC(worklist_key_pressed_callback),
		     peditor);

  gtk_signal_connect(GTK_OBJECT(peditor->btn_up), "clicked",
		     GTK_SIGNAL_FUNC(worklist_swap_up_callback), peditor);

  gtk_signal_connect(GTK_OBJECT(peditor->btn_down), "clicked",
		     GTK_SIGNAL_FUNC(worklist_swap_down_callback),
		     peditor);

  /* Display the list of available targets. */

  frame = gtk_frame_new(_("Available items"));
  gtk_box_pack_start(GTK_BOX(dialog_hbox), frame, TRUE, TRUE, 0);

  /* Make a box to put the worklist and help button into. */

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  /* Make a scrolling window for the worklist. */

  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC,
				 GTK_POLICY_AUTOMATIC);

  /* Make the list for available targets. */

  peditor->avail = gtk_clist_new_with_titles(4, avail_clist_titles);
  gtk_container_add(GTK_CONTAINER(scrolled), peditor->avail);

  for (i = 0; i < 4; i++) {
    gtk_clist_set_column_auto_resize(GTK_CLIST(peditor->avail), i, TRUE);
  }
  gtk_clist_set_column_justification(GTK_CLIST(peditor->avail), 2,
				     GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_justification(GTK_CLIST(peditor->avail), 3,
				     GTK_JUSTIFY_RIGHT);
  gtk_clist_column_titles_passive(GTK_CLIST(peditor->avail));

  /* signals for the available targets list */

  gtk_signal_connect(GTK_OBJECT(peditor->avail), "select_row",
		     GTK_SIGNAL_FUNC(targets_select_callback), peditor);

  gtk_signal_connect(GTK_OBJECT(peditor->avail), "unselect_row",
		     GTK_SIGNAL_FUNC(targets_select_callback), peditor);

  gtk_signal_connect(GTK_OBJECT(peditor->avail), "key-press-event",
		     GTK_SIGNAL_FUNC(targets_key_pressed_callback),
		     peditor);

  gtk_signal_connect(GTK_OBJECT(peditor->avail), "click-column",
		     GTK_SIGNAL_FUNC(targets_type_rotate_callback),
		     peditor);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

  /* A checkbox to filter advanced tech targets. */

  peditor->toggle_show_advanced =
      gtk_check_button_new_with_label(_("Show future targets"));
  gtk_box_pack_start(GTK_BOX(hbox), peditor->toggle_show_advanced, FALSE,
		     TRUE, 5);
  gtk_signal_connect(GTK_OBJECT(peditor->toggle_show_advanced), "toggled",
		     GTK_SIGNAL_FUNC(targets_show_advanced_callback),
		     peditor);

  /* if it's a city editor, add an "undo" button, else add action area */

  if (peditor->embedded_in_city) {
    peditor->btn_cancel = gtk_button_new_with_label(_("Undo"));
    gtk_box_pack_end(GTK_BOX(hbox), peditor->btn_cancel, TRUE, TRUE, 5);
    gtk_signal_connect(GTK_OBJECT(peditor->btn_cancel), "clicked",
		       GTK_SIGNAL_FUNC(worklist_no_callback), peditor);
  } else {
    action_area = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(peditor->shell), action_area, FALSE, FALSE,
		       0);

    peditor->btn_ok = gtk_button_new_with_label(_("Ok"));
    gtk_box_pack_start(GTK_BOX(action_area), peditor->btn_ok, FALSE, TRUE,
		       5);
    gtk_signal_connect(GTK_OBJECT(peditor->btn_ok), "clicked",
		       GTK_SIGNAL_FUNC(worklist_ok_callback), peditor);

    peditor->btn_cancel = gtk_button_new_with_label(_("Cancel"));
    gtk_box_pack_start(GTK_BOX(action_area), peditor->btn_cancel, FALSE,
		       TRUE, 5);
    gtk_signal_connect(GTK_OBJECT(peditor->btn_cancel), "clicked",
		       GTK_SIGNAL_FUNC(worklist_no_callback), peditor);

    gtk_widget_show_all(action_area);
  }

  /* Add a help button near the bottom. */
  button = gtk_button_new_with_label(_("Help"));
  gtk_box_pack_end(GTK_BOX(hbox), button, TRUE, TRUE, 5);

  gtk_signal_connect(GTK_OBJECT(button), "clicked",
		     GTK_SIGNAL_FUNC(targets_help_callback), peditor);

  gtk_signal_connect(GTK_OBJECT(peditor->shell), "delete_event",
		     GTK_SIGNAL_FUNC(worklist_editor_delete_callback),
		     (gpointer) peditor);

  update_changed_sensitive(peditor);

  /* note: this create function does _not_ update itself             */
  /* this is to prevent refresh_city_dialog() updating it twice -mck */

  /* Make all the widgets appear. */
  gtk_widget_show_all(dialog_hbox);
  gtk_widget_show(peditor->shell);

  return peditor;
}

/*****************************************************************/
/**** callbacks and functions for the global worklists report ****/
/****************************************************************
 If the worklist report is open, force its contents to be updated. 
*****************************************************************/
void update_worklist_report_dialog(void)
{
  if (report_dialog) {
    global_list_update(report_dialog);
  }
}

/****************************************************************
 helper function for the next two functions.
*****************************************************************/
static void global_dialog_cleanup(struct worklist_report *preport)
{
  assert(preport != NULL);

  gtk_widget_destroy(preport->shell);
  memset(preport, 0, sizeof(*preport));
  free(preport);
}

/****************************************************************
...
*****************************************************************/
static gint global_dialog_delete_callback(GtkWidget * w, GdkEvent * ev,
					  gpointer data)
{
  global_dialog_cleanup(report_dialog);
  report_dialog = NULL;

  return FALSE;
}

/****************************************************************

*****************************************************************/
static void global_close_report_callback(GtkWidget * w, gpointer data)
{
  struct worklist_report *preport = (struct worklist_report *) data;

  global_dialog_cleanup(preport);
  report_dialog = NULL;
}

/****************************************************************

*****************************************************************/
static void global_edit_callback(GtkWidget * w, gpointer data)
{
  struct worklist_report *preport = (struct worklist_report *) data;
  GList *selection = GTK_CLIST(preport->list)->selection;

  if (!selection)
    return;

  preport->wl_idx = GPOINTER_TO_INT(selection->data);

  popup_worklist(preport->worklist_ptr[preport->wl_idx], NULL,
		 report_dialog->shell,
		 preport, global_commit_worklist, NULL);
}

/****************************************************************

*****************************************************************/
static void global_rename_callback(GtkWidget * w, gpointer data)
{
  struct worklist_report *preport = (struct worklist_report *) data;
  GList *selection = GTK_CLIST(preport->list)->selection;

  if (!selection)
    return;

  preport->wl_idx = GPOINTER_TO_INT(selection->data);

  input_dialog_create(report_dialog->shell,
		      _("Rename Worklist"),
		      _("What should the new name be?"),
		      preport->pplr->worklists[preport->wl_idx].name,
		      global_rename_sub_callback,
		      (gpointer) preport,
		      NULL, NULL);
}

/****************************************************************

*****************************************************************/
static void global_rename_sub_callback(const char *input, gpointer data)
{
  struct worklist_report *preport = (struct worklist_report *) data;

  strncpy(preport->pplr->worklists[preport->wl_idx].name, input,
	  MAX_LEN_NAME);
  preport->pplr->worklists[preport->wl_idx].name[MAX_LEN_NAME - 1] = '\0';

  global_list_update(preport);
}

/****************************************************************
  Create a new worklist.
*****************************************************************/
static void global_insert_callback(GtkWidget * w, gpointer data)
{
  struct worklist_report *preport = (struct worklist_report *) data;
  int j;

  /* Find the next free worklist for this player */

  for (j = 0; j < MAX_NUM_WORKLISTS; j++)
    if (!preport->pplr->worklists[j].is_valid)
      break;

  /* No more worklist slots free.  (!!!Maybe we should tell the user?) */
  if (j == MAX_NUM_WORKLISTS)
    return;

  /* Validate this slot. */
  init_worklist(&preport->pplr->worklists[j]);
  preport->pplr->worklists[j].is_valid = TRUE;
  strcpy(preport->pplr->worklists[j].name, _("empty worklist"));

  global_list_update(preport);  
}

/****************************************************************
  Remove the current worklist.  This request is made by sliding
  up all lower worklists to fill in the slot that's being deleted.
*****************************************************************/
static void global_delete_callback(GtkWidget * w, gpointer data)
{
  struct worklist_report *preport = (struct worklist_report *) data;
  GList *selection = GTK_CLIST(preport->list)->selection;
  int i, j;

  if (!selection)
    return;

  /* Look for the last free worklist */
  for (i = 0; i < MAX_NUM_WORKLISTS; i++)
    if (!preport->pplr->worklists[i].is_valid)
      break;

  for (j = GPOINTER_TO_INT(selection->data); j < i - 1; j++) {
    copy_worklist(&preport->pplr->worklists[j], 
                  &preport->pplr->worklists[j + 1]);
  }

  /* The last worklist in the set is no longer valid -- it's been slid up
   * one slot. */
  preport->pplr->worklists[i-1].is_valid = FALSE;
  strcpy(preport->pplr->worklists[i-1].name, "\0");

  global_list_update(preport);
}

/****************************************************************

*****************************************************************/
static void global_select_list_callback(GtkWidget * w, gint row,
					gint column, GdkEvent * ev,
					gpointer data)
{
  struct worklist_report *preport = (struct worklist_report *) data;
  int row_selected = (GTK_CLIST(preport->list)->selection != NULL);

  gtk_widget_set_sensitive(report_dialog->btn_edit, row_selected);
  gtk_widget_set_sensitive(report_dialog->btn_rename, row_selected);
  gtk_widget_set_sensitive(report_dialog->btn_delete, row_selected);
}

/****************************************************************
  Commit the changes to the worklist for this player.
*****************************************************************/
static void global_commit_worklist(struct worklist *pwl, void *data)
{
  struct worklist_report *preport = (struct worklist_report *) data;

  copy_worklist(&preport->pplr->worklists[preport->wl_idx], pwl);
}

/****************************************************************

*****************************************************************/
static void global_list_update(struct worklist_report *preport)
{
  int i, n;

  for (i = 0, n = 0; i < MAX_NUM_WORKLISTS; i++) {
    if (preport->pplr->worklists[i].is_valid) {
      strcpy(preport->worklist_names[n], preport->pplr->worklists[i].name);
      preport->worklist_names_ptrs[n] = preport->worklist_names[n];
      preport->worklist_ptr[n] = &preport->pplr->worklists[i];

      n++;
    }
  }

  /* Terminators */
  preport->worklist_names_ptrs[n] = NULL;

  /* now fill the list */

  n = 0;
  gtk_clist_freeze(GTK_CLIST(preport->list));
  gtk_clist_clear(GTK_CLIST(preport->list));

  while (preport->worklist_names_ptrs[n]) {
    gtk_clist_append(GTK_CLIST(preport->list),
		     &preport->worklist_names_ptrs[n]);
    n++;
  }

  gtk_clist_thaw(GTK_CLIST(preport->list));
}

/*****************************************************************/
/******** callbacks and functions for the worklist editor ********/
/*****************************************************************
 copies a worklist to the editor for editing
******************************************************************/
static void copy_worklist_to_editor(struct worklist *pwl,
				    struct worklist_editor *peditor,
				    int where)
{
  int i;

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    int target;
    bool is_unit;

    /* end of list */
    if (!worklist_peek_ith(pwl, &target, &is_unit, i)) {
      break;
    }

    worklist_really_insert_item(peditor, where,
				wid_encode(is_unit, FALSE, target));
    if (where < MAX_LEN_WORKLIST)
      where++;
  }
  /* Terminators */
  while (where < MAX_LEN_WORKLIST) {
    peditor->worklist_wids[where++] = WORKLIST_END;
  }
}

/****************************************************************
 copies a worklist back from the editor
*****************************************************************/
static void copy_editor_to_worklist(struct worklist_editor *peditor,
				    struct worklist *pwl)
{
  int i;

  /* Fill in this worklist with the parameters set in the worklist dialog. */
  init_worklist(pwl);

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    if (peditor->worklist_wids[i] == WORKLIST_END) {
      pwl->wlefs[i] = WEF_END;
      pwl->wlids[i] = 0;
      break;
    } else {
      wid wid = peditor->worklist_wids[i];

      assert(!wid_is_worklist(wid));

      pwl->wlefs[i] = wid_is_unit(wid) ? WEF_UNIT : WEF_IMPR;
      pwl->wlids[i] = wid_id(wid);
    }
  }
  strcpy(pwl->name, peditor->pwl->name);
  pwl->is_valid = peditor->pwl->is_valid;
}

/****************************************************************
  allows user to select one of the clists using only keyboard
*****************************************************************/
static gboolean keyboard_handler(GtkWidget * widget, GdkEventKey * event,
				 gpointer * data)
{
  struct worklist_editor *peditor;
  peditor = (struct worklist_editor *) data;
  switch (event->keyval) {
  case GDK_Home:
    gtk_widget_grab_focus(peditor->worklist);
    if (!GTK_CLIST(peditor->worklist)->selection) {
      gtk_clist_select_row(GTK_CLIST(peditor->worklist), 0, 0);
    }
    break;
  case GDK_End:
    gtk_widget_grab_focus(peditor->avail);
    if (!GTK_CLIST(peditor->avail)->selection) {
      gtk_clist_select_row(GTK_CLIST(peditor->avail), 0, 0);
    }
    break;
  default:
    return FALSE;
  }

  return TRUE;
}

/****************************************************************
  Key pressed in worklist
*****************************************************************/
static gboolean worklist_key_pressed_callback(GtkWidget * w,
					      GdkEventKey * ev,
					      gpointer data)
{
  struct worklist_editor *peditor = (struct worklist_editor *) data;
  GList *selection = GTK_CLIST(peditor->worklist)->selection;
  int row;

  if (!selection)
    return FALSE;

  row = GPOINTER_TO_INT(selection->data);

  if (ev->type == GDK_KEY_PRESS) {
    switch (ev->keyval) {
    case GDK_Up:
      gtk_clist_select_row(GTK_CLIST(peditor->worklist), row - 1, 0);
      break;
    case GDK_Down:
      gtk_clist_select_row(GTK_CLIST(peditor->worklist), row + 1, 0);
      break;
    case GDK_Page_Up:
      worklist_swap_up_callback(w, data);
      break;
    case GDK_Page_Down:
      worklist_swap_down_callback(w, data);
      break;
    case GDK_Delete:
      worklist_remove_item(peditor);
      break;
    default:
      return FALSE;
    }
    return TRUE;
  }
  return FALSE;
}

/****************************************************************
  Key pressed in available targets list
*****************************************************************/
static gboolean targets_key_pressed_callback(GtkWidget * w,
					     GdkEventKey * ev,
					     gpointer data)
{
  struct worklist_editor *peditor = (struct worklist_editor *) data;
  GList *selection = GTK_CLIST(peditor->avail)->selection;
  int row;

  if (!selection)
    return FALSE;

  row = GPOINTER_TO_INT(selection->data);

  if (ev->type == GDK_KEY_PRESS) {
    switch (ev->keyval) {
    case GDK_Up:
      gtk_clist_select_row(GTK_CLIST(peditor->avail), row - 1, 0);
      break;
    case GDK_Down:
      gtk_clist_select_row(GTK_CLIST(peditor->avail), row + 1, 0);
      break;
    case GDK_Insert:
      worklist_insert_item(peditor);
      gtk_widget_hide(peditor->worklist);
      gtk_widget_show(peditor->worklist);
      break;
    default:
      return FALSE;
    }
    return TRUE;
  }
  return FALSE;
}

/****************************************************************

*****************************************************************/
static void worklist_select_callback(GtkWidget * w, gint row, gint column,
				     GdkEvent * ev, gpointer data)
{
  int row_selected;
  struct worklist_editor *peditor = (struct worklist_editor *) data;

  if (can_client_issue_orders() && ev && ev->type == GDK_2BUTTON_PRESS) {
    /* Double-click to remove item from worklist */
    worklist_remove_item(peditor);
    return;
  }

  row_selected = (GTK_CLIST(peditor->worklist)->selection != NULL);

  gtk_widget_set_sensitive(peditor->btn_up, can_client_issue_orders() &&
			   row_selected && row > 0 );
  gtk_widget_set_sensitive(peditor->btn_down, can_client_issue_orders() &&
			   row_selected &&
			   row < GTK_CLIST(peditor->worklist)->rows - 1);
}

/****************************************************************
  User selected one of the available items
*****************************************************************/
static void targets_select_callback(GtkWidget * w, gint row, gint column,
				    GdkEvent * ev, gpointer data)
{
  if (can_client_issue_orders() && ev && ev->type == GDK_2BUTTON_PRESS) {
    struct worklist_editor *peditor = (struct worklist_editor *) data;
    /* Double-click to insert item in worklist */
    worklist_insert_item(peditor);
    gtk_widget_hide(peditor->worklist);	/* done on purpose */
    gtk_widget_show(peditor->worklist);
    return;
  }
}

/****************************************************************
...
*****************************************************************/
void update_worklist_editor(struct worklist_editor *peditor)
{
  worklist_prep(peditor);
  worklist_list_update(peditor);
  targets_list_update(peditor);
}


/****************************************************************
 rotate worklists and the rest in available items list
*****************************************************************/
static void targets_type_rotate_callback(GtkWidget * w, int col,
					 gpointer data)
{
  if (game.player_ptr->worklists[0].is_valid) {
    are_worklists_first ^= 1;
  }

  targets_list_update((struct worklist_editor *) data);
}

/****************************************************************
 sets aside the first space for "currently_building" if in city
*****************************************************************/
static void worklist_prep(struct worklist_editor *peditor)
{
  if (peditor->embedded_in_city) {
    peditor->worklist_wids[0] =
	wid_encode(peditor->pcity->is_building_unit, FALSE,
		   peditor->pcity->currently_building);
    peditor->worklist_wids[1] = WORKLIST_END;
    copy_worklist_to_editor(&peditor->pcity->worklist, peditor,
			    MAX_LEN_WORKLIST);
  } else {
    peditor->worklist_wids[0] = WORKLIST_END;
    copy_worklist_to_editor(peditor->pwl, peditor, MAX_LEN_WORKLIST);
  }
}

/****************************************************************
 does the UI work of inserting a target into the worklist
 also inserts a global worklist straight in.
*****************************************************************/
static void worklist_insert_item(struct worklist_editor *peditor)
{
  GList *listSelection = GTK_CLIST(peditor->worklist)->selection;
  GList *availSelection = GTK_CLIST(peditor->avail)->selection;
  int where, index, len;
  wid wid;

  if (!listSelection) {
    where = MAX_LEN_WORKLIST;
  } else {
    where = GPOINTER_TO_INT(listSelection->data);
    if(peditor->pcity->did_buy && where == 0) {
      append_output_window(_("Game: You have bought this turn, can't change."));
      where = 1;
    }
  }

  /* Is there anything selected to insert? */
  if (!availSelection) {
    return;
  }

  index = GPOINTER_TO_INT(availSelection->data);
  wid = peditor->worklist_avail_wids[index];

  /* target is a global worklist id */
  if (wid_is_worklist(wid)) {
    struct player *pplr = city_owner(peditor->pcity);
    struct worklist *pwl = &pplr->worklists[wid_id(wid)];

    copy_worklist_to_editor(pwl, peditor, where);
    where += worklist_length(pwl);
  } else {
    worklist_really_insert_item(peditor, where, wid);
    where++;
  }

  /* Update the list with the actual data */
  worklist_list_update(peditor);

  /* How long is the new worklist? */
  for (len = 0; len < MAX_LEN_WORKLIST; len++)
    if (peditor->worklist_wids[len] == WORKLIST_END)
      break;

  /* Re-select the item that was previously selected. */
  if (where < len) {
    gtk_clist_select_row(GTK_CLIST(peditor->worklist), where, 0);
  }
  peditor->changed = 1;
  update_changed_sensitive(peditor);
}

/****************************************************************
 does the heavy lifting for inserting an item (not a global worklist) 
 into a worklist.
*****************************************************************/
static void worklist_really_insert_item(struct worklist_editor *peditor,
					int before, wid wid)
{
  int i, first_free;
  int target = wid_id(wid);
  bool is_unit = wid_is_unit(wid);

  assert(!wid_is_worklist(wid));

  /* If this worklist is a city worklist, double check that the city
     really can (eventually) build the target.  We've made sure that
     the list of available targets is okay for this city, but a global
     worklist may try to insert an odd-ball unit or target. */
  if (peditor->pcity &&
      ((is_unit && !can_eventually_build_unit(peditor->pcity, target)) ||
       (!is_unit
	&& !can_eventually_build_improvement(peditor->pcity, target)))) {
    /* Nope, this city can't build this target, ever.  Don't put it into
       the worklist. */
    return;
  }

  /* Find the first free element in the worklist */
  for (first_free = 0; first_free < MAX_LEN_WORKLIST; first_free++)
    if (peditor->worklist_wids[first_free] == WORKLIST_END)
      break;

  if (first_free >= MAX_LEN_WORKLIST - 1) {
    /* No room left in the worklist! (remember, we need to keep space
       open for the WORKLIST_END sentinel.) */
    return;
  }

  if (first_free < before && before != MAX_LEN_WORKLIST) {
    /* True weirdness. */
    return;
  }

  if (before < MAX_LEN_WORKLIST) {
    /* Slide all the later elements in the worklist down. */
    for (i = first_free; i > before; i--) {
      peditor->worklist_wids[i] = peditor->worklist_wids[i - 1];
    }
  } else {
    /* Append the new id, not insert. */
    before = first_free;
  }
  first_free++;
  peditor->worklist_wids[first_free] = WORKLIST_END;
  peditor->worklist_wids[before] = wid;
}

/****************************************************************
  Remove the selected target in the worklist.
*****************************************************************/
static void worklist_remove_item(struct worklist_editor *peditor)
{
  GList *selection = GTK_CLIST(peditor->worklist)->selection;
  int row_selected;
  int i, j, row;

  if (!selection)
    return;

  row = GPOINTER_TO_INT(selection->data);

  /* Find the last element in the worklist */
  for (i = 0; i < MAX_LEN_WORKLIST; i++)
    if (peditor->worklist_wids[i] == WORKLIST_END)
      break;

  /* Slide all the later elements in the worklist up. */
  for (j = row; j < i; j++) {
    peditor->worklist_wids[j] = peditor->worklist_wids[j + 1];
  }

  i--;
  peditor->worklist_wids[i] = WORKLIST_END;

  /* Update the list with the actual data */
  worklist_list_update(peditor);

  /* Select the item immediately after the item we just deleted,
     if there is such an item. */
  if (row < i) {
    gtk_clist_select_row(GTK_CLIST(peditor->worklist), row, 0);
  }

  row_selected = (GTK_CLIST(peditor->worklist)->selection != NULL);

  gtk_widget_set_sensitive(peditor->btn_up, row_selected && row > 0);
  gtk_widget_set_sensitive(peditor->btn_down, row_selected &&
			   row < GTK_CLIST(peditor->worklist)->rows - 1);

  peditor->changed = 1;
  update_changed_sensitive(peditor);
}

/****************************************************************

*****************************************************************/
static void worklist_swap_entries(int i, int j,
				  struct worklist_editor *peditor)
{
  int id = peditor->worklist_wids[i];

  peditor->worklist_wids[i] = peditor->worklist_wids[j];
  peditor->worklist_wids[j] = id;
}

/****************************************************************
  Swap the selected element with its upward neighbor
*****************************************************************/
static void worklist_swap_up_callback(GtkWidget * w, gpointer data)
{
  struct worklist_editor *peditor = (struct worklist_editor *) data;
  GList *selection = GTK_CLIST(peditor->worklist)->selection;
  int idx;

  if (!selection)
    return;

  idx = GPOINTER_TO_INT(selection->data);

  if (idx == 0)
    return;

  if(idx == 1 && peditor->pcity->did_buy) {
    /* Refuse to swap the first item if we've bought. */
    append_output_window(_("Game: You have bought this turn, can't change."));
    return;
  }

  worklist_swap_entries(idx, idx - 1, peditor);
  worklist_list_update(peditor);
  gtk_clist_select_row(GTK_CLIST(peditor->worklist), idx - 1, 0);
  peditor->changed = 1;
  update_changed_sensitive(peditor);
}

/****************************************************************
 Swap the selected element with its downward neighbor
*****************************************************************/
static void worklist_swap_down_callback(GtkWidget * w, gpointer data)
{
  struct worklist_editor *peditor = (struct worklist_editor *) data;
  GList *selection = GTK_CLIST(peditor->worklist)->selection;
  int idx;

  if (!selection)
    return;

  idx = GPOINTER_TO_INT(selection->data);

  if(idx == 0 && peditor->pcity->did_buy) {
    /* Refuse to swap the first item if we've bought. */
    append_output_window(_("Game: You have bought this turn, can't change."));
    return;
  }

  if (idx == MAX_LEN_WORKLIST - 1 ||
      peditor->worklist_wids[idx + 1] == WORKLIST_END) return;

  worklist_swap_entries(idx, idx + 1, peditor);
  worklist_list_update(peditor);
  gtk_clist_select_row(GTK_CLIST(peditor->worklist), idx + 1, 0);

  peditor->changed = 1;
  update_changed_sensitive(peditor);
}

/****************************************************************
 this is a hack. struct worklist editor was written too generally.
 it required an ok_callback and no_callback as parameters to the 
 create_editor(). this is stupid. only two things will call an 
 ok_callback, and they're both taken care of with worklist_ok_callback()
 the only advantage that I can see is that some city specific functions 
 are left in citydlg.c in commit_city_worklist()
*****************************************************************/
void commit_worklist(struct worklist_editor *peditor)
{
  worklist_ok_callback(NULL, peditor);
}

/****************************************************************
  User wants to save the worklist.
*****************************************************************/
static void worklist_ok_callback(GtkWidget * w, gpointer data)
{
  struct worklist_editor *peditor;
  struct worklist wl;

  peditor = (struct worklist_editor *) data;

  copy_editor_to_worklist(peditor, &wl);

  /* Invoke the dialog's parent-specified callback */
  if (peditor->ok_callback)
    (*peditor->ok_callback) (&wl, peditor->user_data);

  peditor->changed = 0;
  update_changed_sensitive(peditor);

  if (!peditor->embedded_in_city) {
    cleanup_worklist_editor(peditor);
  }
}

/****************************************************************
  User cancelled from the Worklist dialog or hit Undo.
*****************************************************************/
static void worklist_no_callback(GtkWidget * w, gpointer data)
{
  struct worklist_editor *peditor;

  peditor = (struct worklist_editor *) data;

  /* Invoke the dialog's parent-specified callback */
  if (peditor->cancel_callback)
    (*peditor->cancel_callback) (peditor->user_data);

  peditor->changed = 0;
  update_changed_sensitive(peditor);

  if (!peditor->embedded_in_city) {
    cleanup_worklist_editor(peditor);
  } else {
    worklist_prep(peditor);
    worklist_list_update(peditor);
  }
}

/**************************************************************************
  Change the label of the Show Advanced toggle.  Also updates the list
  of available targets.
**************************************************************************/
static void targets_show_advanced_callback(GtkWidget * w, gpointer data)
{
  targets_list_update((struct worklist_editor *) data);
}

/****************************************************************

*****************************************************************/
static void worklist_list_update(struct worklist_editor *peditor)
{
  int i, n;
  char *row[COLUMNS];
  char buf[COLUMNS][BUFFER_SIZE];

  for (i = 0; i < COLUMNS; i++)
    row[i] = buf[i];

  gtk_clist_freeze(GTK_CLIST(peditor->worklist));
  gtk_clist_clear(GTK_CLIST(peditor->worklist));

  n = 0;

  /* Fill in the rest of the worklist list */
  for (i = 0; n < MAX_LEN_WORKLIST; i++, n++) {
    wid wid = peditor->worklist_wids[i];

    if (wid == WORKLIST_END) {
      break;
    }
    assert(!wid_is_worklist(wid));

    get_city_dialog_production_row(row, BUFFER_SIZE,
                                   wid_id(wid), wid_is_unit(wid),
                                   peditor->pcity);
    gtk_clist_append(GTK_CLIST(peditor->worklist), row);
  }

  gtk_clist_thaw(GTK_CLIST(peditor->worklist));
}

/****************************************************************
  Fill in the target arrays in the peditor.
*****************************************************************/
static void targets_list_update(struct worklist_editor *peditor)
{
  int i = 0, wids_used = 0;
  struct player *pplr = game.player_ptr;
  int advanced_tech;
  char *row[COLUMNS];
  char buf[COLUMNS][BUFFER_SIZE];

  /* Is the worklist limited to just the current targets, or */
  /* to any available and future targets?                    */
  advanced_tech = (peditor->toggle_show_advanced &&
		   GTK_TOGGLE_BUTTON(peditor->
				     toggle_show_advanced)->active ==
		   WORKLIST_ADVANCED_TARGETS);

  wids_used = collect_wids1(peditor->worklist_avail_wids, peditor->pcity,
			    are_worklists_first, advanced_tech);
  peditor->worklist_avail_wids[wids_used] = WORKLIST_END;

  if (!game.player_ptr->worklists[0].is_valid)
    gtk_clist_column_title_passive(GTK_CLIST(peditor->avail), 0);
  else
    gtk_clist_column_title_active(GTK_CLIST(peditor->avail), 0);

  /* fill the gui list */
  for (i = 0; i < COLUMNS; i++)
    row[i] = buf[i];

  gtk_clist_freeze(GTK_CLIST(peditor->avail));
  gtk_clist_clear(GTK_CLIST(peditor->avail));

  for (i = 0;; i++) {
    wid wid = peditor->worklist_avail_wids[i];

    if (wid == WORKLIST_END) {
      break;
    }

    if (wid_is_worklist(wid)) {
      my_snprintf(buf[0], BUFFER_SIZE, "%s",
		  pplr->worklists[wid_id(wid)].name);
      my_snprintf(buf[1], BUFFER_SIZE, _("Worklist"));
      my_snprintf(buf[2], BUFFER_SIZE, "---");
      my_snprintf(buf[3], BUFFER_SIZE, "---");
    } else {
      get_city_dialog_production_row(row, BUFFER_SIZE,
                                     wid_id(wid), wid_is_unit(wid),
                                     peditor->pcity);
    }
    gtk_clist_append(GTK_CLIST(peditor->avail), row);
  }
  gtk_clist_thaw(GTK_CLIST(peditor->avail));
}

/****************************************************************

*****************************************************************/
static void update_changed_sensitive(struct worklist_editor *peditor)
{
  if (!peditor->embedded_in_city) {
    gtk_widget_set_sensitive(peditor->btn_ok, peditor->changed);
  }
  if (!peditor->embedded_in_city) {
    gtk_widget_set_sensitive(peditor->btn_cancel, TRUE);
  } else {
    gtk_widget_set_sensitive(peditor->btn_cancel, peditor->changed);
  }
}

/****************************************************************
  User asked for help from the Worklist dialog.  If there's 
  something highlighted, bring up the help for that item.  Else,
  bring up help for improvements.
*****************************************************************/
static void targets_help_callback(GtkWidget * w, gpointer data)
{
  struct worklist_editor *peditor;
  GList *selection;
  int id;
  bool is_unit = FALSE;

  peditor = (struct worklist_editor *) data;

  selection = GTK_CLIST(peditor->avail)->selection;

  if (selection) {
    wid wid =
	peditor->worklist_avail_wids[GPOINTER_TO_INT(selection->data)];

    if (wid_is_worklist(wid)) {
      id = -1;
    } else {
      id = wid_id(wid);
      is_unit = wid_is_unit(wid);
    }
  } else {
    id = -1;
  }

  worklist_help(id, is_unit);
}

/****************************************************************
 ...
*****************************************************************/
static void worklist_help(int id, bool is_unit)
{
  if (id >= 0) {
    if (is_unit) {
      popup_help_dialog_typed(get_unit_type(id)->name, HELP_UNIT);
    } else if (is_wonder(id)) {
      popup_help_dialog_typed(get_improvement_name(id), HELP_WONDER);
    } else {
      popup_help_dialog_typed(get_improvement_name(id), HELP_IMPROVEMENT);
    }
  } else
    popup_help_dialog_string(HELP_WORKLIST_EDITOR_ITEM);
}

/****************************************************************
...
*****************************************************************/
static void cleanup_worklist_editor(struct worklist_editor *peditor)
{
  assert(peditor != NULL);

  if (peditor->embedded_in_city) {
    gtk_widget_destroy(peditor->shell);
  } else {
    gtk_widget_destroy(peditor->shell->parent);
  }

  memset(peditor, 0, sizeof(*peditor));
  free(peditor);
}

/****************************************************************
...
*****************************************************************/
void close_worklist_editor(struct worklist_editor *peditor)
{
  cleanup_worklist_editor(peditor);
}

/****************************************************************
...
*****************************************************************/
static gint worklist_editor_delete_callback(GtkWidget * w, GdkEvent * ev,
					    gpointer data)
{
  struct worklist_editor *peditor;

  peditor = (struct worklist_editor *) data;
  cleanup_worklist_editor(peditor);
  report_dialog = NULL;

  return FALSE;
}
