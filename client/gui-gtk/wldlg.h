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
#ifndef FC__WLDLG_H
#define FC__WLDLG_H

#include <gtk/gtk.h>

#include "improvement.h"
#include "unittype.h"
#include "worklist.h"

#include "climisc.h"
#include "wldlg_g.h"

typedef void (*WorklistOkCallback) (struct worklist * pwl, void *data);
typedef void (*WorklistCancelCallback) (void *data);
typedef void (*WorklistDeleteCallback) (GtkWidget * parent);

/*
 * The worklist editor is a widget with which the player edits a
 * particular worklist.  The worklist editor is embedded into the city
 * dialog or popped-up from the global Worklist Report dialog.
 * this struct is here in so that citydlg.c can use it.
 */
struct worklist_editor {
  GtkWidget *shell, *worklist, *avail;
  GtkWidget *btn_ok, *btn_cancel;
  GtkWidget *btn_up, *btn_down;
  GtkWidget *toggle_show_advanced;

  struct city *pcity;
  struct worklist *pwl;

  void *user_data;
  int embedded_in_city, changed;
   gboolean(*keyboard_handler) (GtkWidget *, GdkEventKey *, gpointer *);
  WorklistOkCallback ok_callback;
  WorklistCancelCallback cancel_callback;

  wid worklist_wids[MAX_LEN_WORKLIST];
  /* maps from slot to wid; last one contains WORKLIST_END */
  wid worklist_avail_wids[B_LAST + U_LAST + MAX_NUM_WORKLISTS + 1];
};

/* The global worklist view */
void popup_worklists_report(struct player *pplr);

/* An individual worklist */
struct worklist_editor *create_worklist_editor(struct worklist *pwl,
					       struct city *pcity,
					       void *user_data,
					       WorklistOkCallback ok_cb,
					       WorklistCancelCallback
					       cancel_cb,
					       int embedded_in_city);
void close_worklist_editor(struct worklist_editor *peditor);

GtkWidget *popup_worklist(struct worklist *pwl, struct city *pcity,
			  GtkWidget * parent, void *user_data,
			  WorklistOkCallback ok_cb,
			  WorklistCancelCallback cancel_cb);

void update_worklist_editor(struct worklist_editor *peditor);
void commit_worklist(struct worklist_editor *peditor);

#endif				/* FC__WLDLG_H */
