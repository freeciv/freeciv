/**********************************************************************
 Freeciv - Copyright (C) 2001 - R. Falke, M. Kaufman
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

#include <gdk/gdkkeysyms.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "mem.h"
#include "support.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "civclient.h"
#include "cma_fec.h"
#include "messagewin_g.h"

#include "cityrep.h"
#include "dialogs.h"
#include "gui_stuff.h"
#include "inputdlg.h"

#include "cma_fe.h"

#define BUFFER_SIZE             64

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct cma_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct cma_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list dialog_list;
static bool dialog_list_has_been_initialised = FALSE;

static int allow_refreshes = 1;

static struct cma_dialog *get_cma_dialog(struct city *pcity);

static void update_cma_preset_list(struct cma_dialog *pdialog);

static gboolean cma_preset_key_pressed_callback(GtkWidget *w, GdkEventKey *ev,
						gpointer data);
static void cma_del_preset_callback(GtkWidget *w, gpointer data);
static void cma_preset_remove(struct cma_dialog *pdialog, int preset_index);
static void cma_preset_remove_callback_yes(gpointer data);
static void cma_preset_remove_close_callback(gpointer data);

static void cma_add_preset_callback(GtkWidget *w, gpointer data);
static void cma_preset_add_callback_yes(const char *input, gpointer data);
static void cma_preset_add_callback_no(gpointer data);

static void cma_change_to_callback(GtkWidget *w, gpointer data);
static void cma_change_permanent_to_callback(GtkWidget *w, gpointer data);
static void cma_release_callback(GtkWidget *w, gpointer data);
static void cma_select_preset_callback(GtkWidget *w, gint row, gint col,
				       GdkEvent *ev, gpointer data);

static void hscale_changed(GtkAdjustment *get, gpointer data);
static void set_hscales(const struct cm_parameter *const parameter,
			struct cma_dialog *pdialog);

/**************************************************************************
...
**************************************************************************/
static void ensure_initialised_dialog_list(void)
{
  if (!dialog_list_has_been_initialised) {
    dialog_list_init(&dialog_list);
    dialog_list_has_been_initialised = TRUE;
  }
}

/**************************************************************************
 only called when the city dialog is closed.
**************************************************************************/
void close_cma_dialog(struct city *pcity)
{
  struct cma_dialog *pdialog = get_cma_dialog(pcity);

  dialog_list_unlink(&dialog_list, pdialog);

  if (pdialog->name_shell) {
    gtk_widget_destroy(pdialog->name_shell);
  }

  if (pdialog->preset_remove_shell) {
    gtk_widget_destroy(pdialog->preset_remove_shell);
  }

  gtk_widget_destroy(pdialog->shell);
  free(pdialog);
}

/****************************************************************
 return the cma_dialog for a given city.
*****************************************************************/
struct cma_dialog *get_cma_dialog(struct city *pcity)
{
  ensure_initialised_dialog_list();

  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pcity == pcity) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/**************************************************************************
 instantiates a new struct for each city_dialog window that is open.
**************************************************************************/
struct cma_dialog *create_cma_dialog(struct city *pcity, GtkAccelGroup *accel)
{
  struct cma_dialog *pdialog;
  struct cm_parameter param;
  GtkWidget *frame, *page, *hbox, *label, *table;
  GtkWidget *vbox, *scrolledwindow, *hscale;
  static const char *preset_title_[] = { N_("Presets") };
  static gchar **preset_title = NULL;
  int i;

  cmafec_get_fe_parameter(pcity, &param);
  pdialog = fc_malloc(sizeof(struct cma_dialog));
  pdialog->pcity = pcity;
  pdialog->shell = gtk_vbox_new(FALSE, 0);
  pdialog->name_shell = NULL;
  pdialog->preset_remove_shell = NULL;

  if (!preset_title) {
    preset_title = intl_slist(1, preset_title_);
  }

  frame = gtk_frame_new(_("Citizen Management Agent"));
  gtk_box_pack_start(GTK_BOX(pdialog->shell), frame, TRUE, TRUE, 0);

  page = gtk_hbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), page);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page), vbox, TRUE, TRUE, 0);

  scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
  gtk_box_pack_start(GTK_BOX(vbox), scrolledwindow, TRUE, TRUE, 0);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  pdialog->preset_list = gtk_clist_new_with_titles(1, preset_title);
  gtk_container_add(GTK_CONTAINER(scrolledwindow), pdialog->preset_list);
  gtk_clist_set_column_auto_resize(GTK_CLIST(pdialog->preset_list), 0, TRUE);
  gtk_clist_column_titles_show(GTK_CLIST(pdialog->preset_list));
  gtk_clist_column_titles_passive(GTK_CLIST(pdialog->preset_list));

  gtk_signal_connect(GTK_OBJECT(pdialog->preset_list), "select_row",
		     GTK_SIGNAL_FUNC(cma_select_preset_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->preset_list), "key-press-event",
		     GTK_SIGNAL_FUNC(cma_preset_key_pressed_callback),
		     pdialog);


  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  pdialog->add_preset_command = gtk_button_new_with_label(_("Add preset"));
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->add_preset_command,
		     TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->add_preset_command, GTK_CAN_DEFAULT);
  gtk_signal_connect(GTK_OBJECT(pdialog->add_preset_command), "clicked",
		     GTK_SIGNAL_FUNC(cma_add_preset_callback), pdialog);

  pdialog->del_preset_command = gtk_button_new_with_label(_("Delete preset"));
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->del_preset_command,
		     TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->del_preset_command, GTK_CAN_DEFAULT);
  gtk_signal_connect(GTK_OBJECT(pdialog->del_preset_command), "clicked",
		     GTK_SIGNAL_FUNC(cma_del_preset_callback), pdialog);

  /* the right-hand side */

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page), vbox, FALSE, FALSE, 2);

  /* Result */

  frame = gtk_frame_new(_("Results"));
  gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, FALSE, 0);

  pdialog->result_label =
      gtk_label_new("food\n prod\n trade\n\n people\n grow\n prod\n name");
  gtk_widget_set_name(pdialog->result_label, "city label");
  gtk_container_add(GTK_CONTAINER(frame), pdialog->result_label);
  gtk_label_set_justify(GTK_LABEL(pdialog->result_label), GTK_JUSTIFY_LEFT);

  /* Minimal Surplus and Factor */

  table = gtk_table_new(NUM_STATS + 2, 3, FALSE);
  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 2);

  label = gtk_label_new(_("Minimal Surplus"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.1, 0.5);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 0, 1);
  label = gtk_label_new(_("Factor"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.1, 0.5);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 2, 3, 0, 1);

  for (i = 0; i < NUM_STATS; i++) {
    label = gtk_label_new(cm_get_stat_name(i));
    gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, i + 1, i + 2);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

    pdialog->minimal_surplus[i] =
	GTK_ADJUSTMENT(gtk_adjustment_new(-20, -20, 20, 1, 1, 0));

    hscale = gtk_hscale_new(GTK_ADJUSTMENT(pdialog->minimal_surplus[i]));
    gtk_table_attach_defaults(GTK_TABLE(table), hscale, 1, 2, i + 1, i + 2);
    gtk_scale_set_digits(GTK_SCALE(hscale), 0);
    gtk_scale_set_value_pos(GTK_SCALE(hscale), GTK_POS_LEFT);

    gtk_signal_connect(GTK_OBJECT(pdialog->minimal_surplus[i]),
		       "value_changed", GTK_SIGNAL_FUNC(hscale_changed),
		       pdialog);

    pdialog->factor[i] =
	GTK_ADJUSTMENT(gtk_adjustment_new(1, 0, 25, 1, 1, 0));

    hscale = gtk_hscale_new(GTK_ADJUSTMENT(pdialog->factor[i]));
    gtk_table_attach_defaults(GTK_TABLE(table), hscale, 2, 3, i + 1, i + 2);
    gtk_scale_set_digits(GTK_SCALE(hscale), 0);
    gtk_scale_set_value_pos(GTK_SCALE(hscale), GTK_POS_LEFT);

    gtk_signal_connect(GTK_OBJECT(pdialog->factor[i]), "value_changed",
		       GTK_SIGNAL_FUNC(hscale_changed), pdialog);
  }

  /* Happy Surplus and Factor */

  label = gtk_label_new(_("Celebrate"));
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1,
			    NUM_STATS + 1, NUM_STATS + 2);
  gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_table_attach_defaults(GTK_TABLE(table), hbox, 1, 2,
			    NUM_STATS + 1, NUM_STATS + 2);

  pdialog->happy_button = gtk_check_button_new();
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->happy_button, FALSE, FALSE,
		     20);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pdialog->happy_button),
			       FALSE);

  gtk_signal_connect(GTK_OBJECT(pdialog->happy_button), "toggled",
		     GTK_SIGNAL_FUNC(hscale_changed), pdialog);

  pdialog->factor[NUM_STATS] =
      GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 50, 1, 0, 0));

  hscale = gtk_hscale_new(GTK_ADJUSTMENT(pdialog->factor[NUM_STATS]));
  gtk_table_attach_defaults(GTK_TABLE(table), hscale, 2, 3,
			    NUM_STATS + 1, NUM_STATS + 2);
  gtk_scale_set_digits(GTK_SCALE(hscale), 0);
  gtk_scale_set_value_pos(GTK_SCALE(hscale), GTK_POS_LEFT);

  gtk_signal_connect(GTK_OBJECT(pdialog->factor[NUM_STATS]),
		     "value_changed", GTK_SIGNAL_FUNC(hscale_changed),
		     pdialog);

  /* buttons */

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  pdialog->change_command = gtk_accelbutton_new(_("Apply onc_e"), accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->change_command, TRUE, FALSE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->change_command, GTK_CAN_DEFAULT);
  gtk_signal_connect(GTK_OBJECT(pdialog->change_command), "clicked",
		     GTK_SIGNAL_FUNC(cma_change_to_callback), pdialog);

  pdialog->perm_command = gtk_accelbutton_new(_("Control c_ity"), accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->perm_command, TRUE, FALSE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->perm_command, GTK_CAN_DEFAULT);
  gtk_signal_connect(GTK_OBJECT(pdialog->perm_command), "clicked",
		     GTK_SIGNAL_FUNC(cma_change_permanent_to_callback),
		     pdialog);

  pdialog->release_command = gtk_accelbutton_new(_("_Release city"), accel);
  gtk_box_pack_start(GTK_BOX(hbox), pdialog->release_command, TRUE, FALSE, 0);
  GTK_WIDGET_SET_FLAGS(pdialog->release_command, GTK_CAN_DEFAULT);
  gtk_signal_connect(GTK_OBJECT(pdialog->release_command), "clicked",
		     GTK_SIGNAL_FUNC(cma_release_callback), pdialog);

  gtk_widget_show_all(pdialog->shell);

  ensure_initialised_dialog_list();

  dialog_list_insert(&dialog_list, pdialog);

  update_cma_preset_list(pdialog);

  /* refresh is done in refresh_city_dialog */

  return pdialog;
}

/**************************************************************************
 refreshes the cma dialog
**************************************************************************/
void refresh_cma_dialog(struct city *pcity, enum cma_refresh refresh)
{
  struct cm_result result;
  struct cm_parameter param;
  struct cma_dialog *pdialog = get_cma_dialog(pcity);
  int controlled = cma_is_city_under_agent(pcity, NULL);
  int preset_index;

  cmafec_get_fe_parameter(pcity, &param);

  /* fill in result label */
  cm_copy_result_from_city(pcity, &result);
  gtk_set_label(pdialog->result_label,
		(char *) cmafec_get_result_descr(pcity, &result, &param));

  /* if called from a hscale, we _don't_ want to do this */
  if (refresh != DONT_REFRESH_HSCALES) {
    set_hscales(&param, pdialog);
  }

  if (refresh != DONT_REFRESH_SELECT) {
    /* highlight preset if parameter matches */
    preset_index = cmafec_preset_get_index_of_parameter(&param);
    if (preset_index != -1) {
      allow_refreshes = 0;
      gtk_clist_select_row(GTK_CLIST(pdialog->preset_list), preset_index, 0);
      allow_refreshes = 1;
    } else {
      gtk_clist_unselect_all(GTK_CLIST(pdialog->preset_list));
    }
  }

  gtk_widget_set_sensitive(GTK_WIDGET(pdialog->change_command),
			   can_client_issue_orders() &&
			   result.found_a_valid && !controlled);
  gtk_widget_set_sensitive(GTK_WIDGET(pdialog->perm_command),
			   can_client_issue_orders() &&
			   result.found_a_valid && !controlled);
  gtk_widget_set_sensitive(GTK_WIDGET(pdialog->release_command),
			   can_client_issue_orders() &&
			   controlled);
}

/**************************************************************************
 fills in the preset list
**************************************************************************/
static void update_cma_preset_list(struct cma_dialog *pdialog)
{
  int i;
  char *row[1];
  char buf[1][BUFFER_SIZE];

  row[0] = buf[0];

  /* Fill preset list */
  gtk_clist_freeze(GTK_CLIST(pdialog->preset_list));
  gtk_clist_clear(GTK_CLIST(pdialog->preset_list));

  /* if there are no presets preset, print informational message else 
   * append the presets */
  if (cmafec_preset_num()) {
    for (i = 0; i < cmafec_preset_num(); i++) {
      mystrlcpy(buf[0], cmafec_preset_get_descr(i), BUFFER_SIZE);
      gtk_clist_insert(GTK_CLIST(pdialog->preset_list), i, row);
    }
  } else {
    static const char *info_message_[4] = {
      N_("For information on:"),
      N_("CMA and presets"),
      N_("including sample presets,"),
      N_("see README.cma.")
    };
    static char **info_message = NULL;

    if (!info_message) {
      info_message = intl_slist(4, info_message_);
    }

    for (i = 0; i < 4; i++) {
      mystrlcpy(buf[0], info_message[i], BUFFER_SIZE);
      gtk_clist_insert(GTK_CLIST(pdialog->preset_list), i, row);
      gtk_clist_set_selectable(GTK_CLIST(pdialog->preset_list), i, FALSE);
    }
  }
  gtk_clist_thaw(GTK_CLIST(pdialog->preset_list));
}

/****************************************************************
 callback for selecting a preset from the preset clist
*****************************************************************/
static void cma_select_preset_callback(GtkWidget *w, gint row, gint col,
				       GdkEvent *ev, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  GList *selection = GTK_CLIST(pdialog->preset_list)->selection;
  int preset_index;
  const struct cm_parameter *pparam;

  if (!selection) {
    return;
  }

  preset_index = GPOINTER_TO_INT(selection->data);

  if (ev && ev->type == GDK_2BUTTON_PRESS) {
    /* Double-click to remove preset from list */
    cma_preset_remove(pdialog, preset_index);
    return;
  }

  pparam = cmafec_preset_get_parameter(preset_index);

  /* save the change */
  cmafec_set_fe_parameter(pdialog->pcity, pparam);

  if (allow_refreshes) {
    if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
      cma_release_city(pdialog->pcity);
      cma_put_city_under_agent(pdialog->pcity, pparam);

      /* unfog the city map if we were unable to put back under */
      if (!cma_is_city_under_agent(pdialog->pcity, NULL)) {
	refresh_city_dialog(pdialog->pcity);
	return;			/* refreshing the city, refreshes cma */
      } else {
        city_report_dialog_update_city(pdialog->pcity);
      }
    }
    refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_SELECT);
  }
}

/**************************************************************************
 pops up a dialog to allow to name your new preset
**************************************************************************/
static void cma_add_preset_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  GList *selection = GTK_CLIST(pdialog->preset_list)->selection;
  char *default_name;

  if (selection) {
    default_name = cmafec_preset_get_descr(GPOINTER_TO_INT(selection->data));
  } else {
    default_name = _("new preset");
  }

  pdialog->name_shell = input_dialog_create(pdialog->shell,
				    _("Name new preset"),
				    _("What should we name the preset?"),
				    default_name,
				    cma_preset_add_callback_yes,
				    (gpointer) pdialog,
				    cma_preset_add_callback_no,
				    (gpointer) pdialog);
}

/****************************************************************
 callback for the add_preset popup (don't add it)
*****************************************************************/
static void cma_preset_add_callback_no(gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  pdialog->name_shell = NULL;
}

/****************************************************************
 callback for the add_preset popup (add it)
*****************************************************************/
static void cma_preset_add_callback_yes(const char *input, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  struct cm_parameter param;

  cmafec_get_fe_parameter(pdialog->pcity, &param);
  cmafec_preset_add(input, &param);
  update_cma_preset_list(pdialog);
  refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_HSCALES);
  /* if this or other cities have this set as "custom" */
  city_report_dialog_update();
  pdialog->name_shell = NULL;
}

/****************************************************************
  Key pressed in preset list
*****************************************************************/
static gboolean cma_preset_key_pressed_callback(GtkWidget *w, GdkEventKey *ev,
						gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  GList *selection = GTK_CLIST(pdialog->preset_list)->selection;

  if (!selection) {
    return FALSE;
  }

  if (ev->type == GDK_KEY_PRESS) {
    switch (ev->keyval) {
    case GDK_Delete:
      cma_preset_remove(pdialog, GPOINTER_TO_INT(selection->data));
      break;
    default:
      return FALSE;
    }
    return TRUE;
  }
  return FALSE;
}


/**************************************************************************
 callback for del_preset 
**************************************************************************/
static void cma_del_preset_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  GList *selection = GTK_CLIST(pdialog->preset_list)->selection;

  if (!selection) {
    return;
  }

  cma_preset_remove(pdialog, GPOINTER_TO_INT(selection->data));
}

/**************************************************************************
 pops up a dialog to remove a preset
**************************************************************************/
static void cma_preset_remove(struct cma_dialog *pdialog, int preset_index)
{
  pdialog->id = preset_index;
  pdialog->preset_remove_shell = popup_message_dialog(pdialog->shell,
					cmafec_preset_get_descr(preset_index),
					_("Remove this preset?"),
					cma_preset_remove_close_callback, 
						      pdialog,
					_("_Yes"),
					cma_preset_remove_callback_yes,
					pdialog, 
					_("_No"), NULL, NULL, 0);
}

/****************************************************************
 callback for the remove_preset popup (delete popup)
*****************************************************************/
static void cma_preset_remove_close_callback(gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  pdialog->preset_remove_shell = NULL;
}

/****************************************************************
 callback for the remove_preset popup (remove it)
*****************************************************************/
static void cma_preset_remove_callback_yes(gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  cmafec_preset_remove(pdialog->id);
  pdialog->id = -1;
  update_cma_preset_list(pdialog);
  refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_HSCALES);
  /* if this or other cities have this set, reset to "custom" */
  city_report_dialog_update();
  pdialog->preset_remove_shell = NULL;
}

/**************************************************************************
 changes the workers of the city to the cma parameters
**************************************************************************/
static void cma_change_to_callback(GtkWidget *w, gpointer data)
{
  struct cm_result result;
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  struct cm_parameter param;

  cmafec_get_fe_parameter(pdialog->pcity, &param);
  cm_query_result(pdialog->pcity, &param, &result);
  cma_apply_result(pdialog->pcity, &result);
}

/**************************************************************************
 changes the workers of the city to the cma parameters and puts the
 city under agent control
**************************************************************************/
static void cma_change_permanent_to_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  struct cm_parameter param;

  cmafec_get_fe_parameter(pdialog->pcity, &param);
  cma_put_city_under_agent(pdialog->pcity, &param);
  refresh_city_dialog(pdialog->pcity);
}

/**************************************************************************
 releases the city from agent control
**************************************************************************/
static void cma_release_callback(GtkWidget *w, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;

  cma_release_city(pdialog->pcity);
  refresh_city_dialog(pdialog->pcity);
}

/****************************************************************
 called to adjust the sliders when a preset is selected
 notice that we don't want to call update_result here. 
*****************************************************************/
static void set_hscales(const struct cm_parameter *const parameter,
			struct cma_dialog *pdialog)
{
  int i;

  allow_refreshes = 0;
  for (i = 0; i < NUM_STATS; i++) {
    gtk_adjustment_set_value(pdialog->minimal_surplus[i],
			     parameter->minimal_surplus[i]);
    gtk_adjustment_set_value(pdialog->factor[i], parameter->factor[i]);
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pdialog->happy_button),
			       parameter->require_happy);
  gtk_adjustment_set_value(pdialog->factor[NUM_STATS],
			   parameter->happy_factor);
  allow_refreshes = 1;
}

/************************************************************************
 callback if we moved the sliders.
*************************************************************************/
static void hscale_changed(GtkAdjustment *get, gpointer data)
{
  struct cma_dialog *pdialog = (struct cma_dialog *) data;
  struct cm_parameter param;
  int i;

  if (!allow_refreshes) {
    return;
  }

  cmafec_get_fe_parameter(pdialog->pcity, &param);
  for (i = 0; i < NUM_STATS; i++) {
    param.minimal_surplus[i] = (int) (pdialog->minimal_surplus[i]->value);
    param.factor[i] = (int) (pdialog->factor[i]->value);
  }
  param.require_happy =
      (GTK_TOGGLE_BUTTON(pdialog->happy_button)->active ? 1 : 0);
  param.happy_factor = (int) (pdialog->factor[NUM_STATS]->value);

  /* save the change */
  cmafec_set_fe_parameter(pdialog->pcity, &param);

  /* refreshes the cma */
  if (cma_is_city_under_agent(pdialog->pcity, NULL)) {
    cma_release_city(pdialog->pcity);
    cma_put_city_under_agent(pdialog->pcity, &param);

    /* unfog the city map if we were unable to put back under */
    if (!cma_is_city_under_agent(pdialog->pcity, NULL)) {
      refresh_city_dialog(pdialog->pcity);
      return;			/* refreshing city refreshes cma */
    } else {
      city_report_dialog_update_city(pdialog->pcity);
    }
  }

  refresh_cma_dialog(pdialog->pcity, DONT_REFRESH_HSCALES);
}
