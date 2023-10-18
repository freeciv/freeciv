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
#include "mem.h"
#include "support.h"

/* client */
#include "options.h"
#include "voteinfo.h"
#include "update_queue.h"

/* client/gui-gtk-5.0 */
#include "chatline.h"

#include "voteinfo_bar.h"

/* A set of widgets. */
struct voteinfo_bar {
  GtkWidget *box;
  GtkWidget *next_button;
  GtkWidget *label;
  GtkWidget *yes_button;
  GtkWidget *no_button;
  GtkWidget *abstain_button;
  GtkWidget *yes_count_label;
  GtkWidget *no_count_label;
  GtkWidget *abstain_count_label;
  GtkWidget *voter_count_label;
};

GtkWidget *pregame_votebar = NULL;      /* PAGE_START voteinfo bar. */
GtkWidget *ingame_votebar = NULL;       /* PAGE_GAME voteinfo bar. */

/**********************************************************************//**
  Called after a click on a vote button.
**************************************************************************/
static void voteinfo_bar_do_vote_callback(GtkWidget *w, gpointer userdata)
{
  enum client_vote_type vote;
  struct voteinfo *vi;

  vote = GPOINTER_TO_INT(userdata);
  vi = voteinfo_queue_get_current(NULL);

  if (vi == NULL) {
    return;
  }

  voteinfo_do_vote(vi->vote_no, vote);
}

/**********************************************************************//**
  Switch to the next vote.
**************************************************************************/
static void voteinfo_bar_next_callback(GtkWidget *w, gpointer userdata)
{
  voteinfo_queue_next();
  voteinfo_gui_update();
}

/**********************************************************************//**
  Destroy the voteinfo_bar data structure.
**************************************************************************/
static void voteinfo_bar_destroy(GtkWidget *w, gpointer userdata)
{
  free((struct voteinfo_bar *) userdata);
}

/**********************************************************************//**
  Create a voteinfo_bar structure. "split_bar" controls whether to split
  voteinfo bar over two lines (for narrow windows) or put on a single line
  to save vertical space.
**************************************************************************/
GtkWidget *voteinfo_bar_new(bool split_bar)
{
  GtkWidget *label, *button, *vgrid, *hgrid;
  struct voteinfo_bar *vib;
  const int BUTTON_HEIGHT = 12;
  int grid_row = 0;
  int grid_col = 0;

  vib = fc_calloc(1, sizeof(struct voteinfo_bar));

  if (!split_bar) {
    hgrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(hgrid), 4);
    g_object_set_data(G_OBJECT(hgrid), "voteinfo_bar", vib);
    g_signal_connect(hgrid, "destroy", G_CALLBACK(voteinfo_bar_destroy), vib);
    vib->box = hgrid;
    vgrid = NULL;        /* The compiler may require it. */
  } else {
    vgrid = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(vgrid), TRUE);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_grid_set_row_spacing(GTK_GRID(vgrid), 4);
    g_object_set_data(G_OBJECT(vgrid), "voteinfo_bar", vib);
    g_signal_connect(vgrid, "destroy", G_CALLBACK(voteinfo_bar_destroy), vib);
    vib->box = vgrid;
    hgrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(hgrid), 4);
    gtk_grid_attach(GTK_GRID(vgrid), hgrid, 0, grid_row++, 1, 1);
  }

  label = gtk_label_new("");
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(label, 8);
  gtk_widget_set_margin_end(label, 8);
  gtk_widget_set_margin_top(label, 4);
  gtk_widget_set_margin_bottom(label, 4);
  gtk_label_set_max_width_chars(GTK_LABEL(label), 80);
  gtk_grid_attach(GTK_GRID(hgrid), label, grid_col++, 0, 1, 1);
  gtk_widget_set_name(label, "vote label");
  vib->label = label;

  if (split_bar) {
    hgrid = gtk_grid_new();
    grid_col = 0;
    gtk_grid_set_column_spacing(GTK_GRID(hgrid), 4);
    gtk_grid_attach(GTK_GRID(vgrid), hgrid, 0, grid_row++, 1, 1);
  }

  button = gtk_button_new();
  gtk_widget_set_margin_end(button, 16);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(voteinfo_bar_next_callback), NULL);
  gtk_button_set_icon_name(GTK_BUTTON(button), "media-seek-backward");
  gtk_widget_set_size_request(button, -1, BUTTON_HEIGHT);
  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_widget_set_focus_on_click(button, FALSE);
  gtk_grid_attach(GTK_GRID(hgrid), button, grid_col++, 0, 1, 1);
  vib->next_button = button;

  button = gtk_button_new_with_mnemonic(_("_YES"));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(voteinfo_bar_do_vote_callback),
                   GINT_TO_POINTER(CVT_YES));
  gtk_widget_set_focus_on_click(button, FALSE);
  gtk_grid_attach(GTK_GRID(hgrid), button, grid_col++, 0, 1, 1);
  gtk_widget_set_name(button, "vote yes button");
  vib->yes_button = button;

  label = gtk_label_new("0");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(hgrid), label, grid_col++, 0, 1, 1);
  vib->yes_count_label = label;

  button = gtk_button_new_with_mnemonic(_("_NO"));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(voteinfo_bar_do_vote_callback),
                   GINT_TO_POINTER(CVT_NO));
  gtk_widget_set_focus_on_click(button, FALSE);
  gtk_grid_attach(GTK_GRID(hgrid), button, grid_col++, 0, 1, 1);
  gtk_widget_set_name(button, "vote no button");
  vib->no_button = button;

  label = gtk_label_new("0");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(hgrid), label, grid_col++, 0, 1, 1);
  vib->no_count_label = label;

  button = gtk_button_new_with_mnemonic(_("_ABSTAIN"));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(voteinfo_bar_do_vote_callback),
                   GINT_TO_POINTER(CVT_ABSTAIN));
  gtk_widget_set_focus_on_click(button, FALSE);
  gtk_grid_attach(GTK_GRID(hgrid), button, grid_col++, 0, 1, 1);
  gtk_widget_set_name(button, "vote abstain button");
  vib->abstain_button = button;

  label = gtk_label_new("0");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(hgrid), label, grid_col++, 0, 1, 1);
  vib->abstain_count_label = label;

  label = gtk_label_new("/0");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(hgrid), label, grid_col++, 0, 1, 1);
  vib->voter_count_label = label;

  return vib->box;
}

/**********************************************************************//**
  Refresh all vote related GUI widgets. Called by the voteinfo module when
  the client receives new vote information from the server.
**************************************************************************/
void voteinfo_gui_update(void)
{
  int vote_count, index;
  struct voteinfo_bar *vib = NULL;
  struct voteinfo *vi = NULL;
  char buf[1024], status[1024], ordstr[128], color[32];
  bool running, need_scroll;
  gchar *escaped_desc, *escaped_user;

  if (get_client_page() == PAGE_START && NULL != pregame_votebar) {
    vib = g_object_get_data(G_OBJECT(pregame_votebar), "voteinfo_bar");
  } else if (get_client_page() == PAGE_GAME && NULL != ingame_votebar) {
    vib = g_object_get_data(G_OBJECT(ingame_votebar), "voteinfo_bar");
  }

  if (vib == nullptr) {
    return;
  }

  if (!voteinfo_bar_can_be_shown()) {
    gtk_widget_set_visible(vib->box, FALSE);
    return;
  }

  vote_count = voteinfo_queue_size();
  vi = voteinfo_queue_get_current(&index);

  if (vi != NULL && vi->resolved && vi->passed) {
    /* TRANS: Describing a vote that passed. */
    fc_snprintf(status, sizeof(status), _("[passed]"));
    sz_strlcpy(color, "green");
  } else if (vi != NULL && vi->resolved && !vi->passed) {
    /* TRANS: Describing a vote that failed. */
    fc_snprintf(status, sizeof(status), _("[failed]"));
    sz_strlcpy(color, "red");
  } else if (vi != NULL && vi->remove_time > 0) {
    /* TRANS: Describing a vote that was removed. */
    fc_snprintf(status, sizeof(status), _("[removed]"));
    sz_strlcpy(color, "grey");
  } else {
    status[0] = '\0';
  }

  if (vote_count > 1) {
    fc_snprintf(ordstr, sizeof(ordstr),
                "<span weight=\"bold\">(%d/%d)</span> ",
                index + 1, vote_count);
  } else {
    ordstr[0] = '\0';
  }

  if (status[0] != '\0') {
    fc_snprintf(buf, sizeof(buf),
        "<span weight=\"bold\" background=\"%s\">%s</span> ",
        color, status);
    sz_strlcpy(status, buf);
  }

  if (vi != NULL) {
    escaped_desc = g_markup_escape_text(vi->desc, -1);
    escaped_user = g_markup_escape_text(vi->user, -1);
    /* TRANS: "Vote" as a process */
    fc_snprintf(buf, sizeof(buf), _("%sVote %d by %s: %s%s"),
                ordstr, vi->vote_no, escaped_user, status,
                escaped_desc);
    g_free(escaped_desc);
    g_free(escaped_user);
  } else {
    buf[0] = '\0';
  }
  gtk_label_set_markup(GTK_LABEL(vib->label), buf);

  if (vi != NULL)  {
    fc_snprintf(buf, sizeof(buf), "%d", vi->yes);
    gtk_label_set_text(GTK_LABEL(vib->yes_count_label), buf);
    fc_snprintf(buf, sizeof(buf), "%d", vi->no);
    gtk_label_set_text(GTK_LABEL(vib->no_count_label), buf);
    fc_snprintf(buf, sizeof(buf), "%d", vi->abstain);
    gtk_label_set_text(GTK_LABEL(vib->abstain_count_label), buf);
    fc_snprintf(buf, sizeof(buf), "/%d", vi->num_voters);
    gtk_label_set_text(GTK_LABEL(vib->voter_count_label), buf);
  } else {
    gtk_label_set_text(GTK_LABEL(vib->yes_count_label), "-");
    gtk_label_set_text(GTK_LABEL(vib->no_count_label), "-");
    gtk_label_set_text(GTK_LABEL(vib->abstain_count_label), "-");
    gtk_label_set_text(GTK_LABEL(vib->voter_count_label), "/-");
  }

  running = vi != NULL && !vi->resolved && vi->remove_time == 0;

  gtk_widget_set_sensitive(vib->yes_button, running);
  gtk_widget_set_sensitive(vib->no_button, running);
  gtk_widget_set_sensitive(vib->abstain_button, running);

  need_scroll = !gtk_widget_get_visible(vib->box)
    && chatline_is_scrolled_to_bottom();

  gtk_widget_set_visible(vib->box, TRUE);

  if (vote_count <= 1) {
    gtk_widget_set_visible(vib->next_button, FALSE);
  }

  if (need_scroll) {
    /* Showing the votebar when it was hidden
     * previously makes the chatline scroll up. */
    chatline_scroll_to_bottom(TRUE);
  }
}
