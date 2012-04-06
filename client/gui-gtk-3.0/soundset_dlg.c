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

/* common */
#include "game.h"

/* client */
#include "audio.h"
#include "client_main.h"

#include "dialogs_g.h"

static void soundset_suggestion_callback(GtkWidget *dlg, gint arg);

/****************************************************************
  Callback either loading suggested soundset or doing nothing
*****************************************************************/
static void soundset_suggestion_callback(GtkWidget *dlg, gint arg)
{
  if (arg == GTK_RESPONSE_YES) {
    /* User accepted soundset loading */

    audio_stop(); /* Fade down old one */

    sz_strlcpy(sound_set_name, game.control.prefered_soundset);
    audio_real_init(sound_set_name, sound_plugin_name);
  }
}

/****************************************************************
  Popup dialog asking if ruleset suggested soundset should be
  used.
*****************************************************************/
void popup_soundset_suggestion_dialog(void)
{
  GtkWidget *dialog, *label;
  char buf[1024];

  dialog = gtk_dialog_new_with_buttons(_("Preferred soundset"),
                                       NULL,
                                       0,
                                       _("Load soundset"),
                                       GTK_RESPONSE_YES,
                                       _("Keep current soundset"),
                                       GTK_RESPONSE_NO,
                                       NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);

  sprintf(buf,
          _("Modpack suggests using %s soundset.\n"
            "It might not work with other soundsets.\n"
            "You are currently using soundset %s."),
          game.control.prefered_soundset, sound_set_name);

  label = gtk_label_new(buf);
  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
  gtk_widget_show(label);

  g_signal_connect(dialog, "response",
                   G_CALLBACK(soundset_suggestion_callback), NULL);

  /* In case incoming rulesets are incompatible with current soundset
   * we need to block their receive before user has accepted loading
   * of the correct soundset. */
  gtk_dialog_run(GTK_DIALOG(dialog));

  gtk_widget_destroy(dialog);
}
