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

#include "fcintl.h"
#include "mem.h"

#include "gui_main.h"

#include "gui_stuff.h"

/**************************************************************************
...
**************************************************************************/
void gtk_expose_now(GtkWidget *w)
{
  gtk_widget_queue_draw(w);
}

/**************************************************************************
...
**************************************************************************/
void gtk_set_relative_position(GtkWidget *ref, GtkWidget *w, int px, int py)
{
  gint x=0, y=0;
  
  if (!GTK_WIDGET_REALIZED (ref))
    gtk_widget_realize (ref);
  if (!GTK_WIDGET_NO_WINDOW (ref))
    gdk_window_get_root_origin (ref->window, &x, &y);

  x=x+px*ref->requisition.width/100;
  y=y+py*ref->requisition.height/100;

  if (!GTK_WIDGET_REALIZED (w))
    gtk_widget_realize (w);
  if (GTK_IS_WINDOW (w))
    gtk_widget_set_uposition (w, x, y);
}


/**************************************************************************
...
**************************************************************************/
void gtk_set_bitmap(GtkWidget *w, GdkPixmap *pm)
{
  if (!GTK_IS_PIXMAP(w))
    return;
  gtk_pixmap_set(GTK_PIXMAP(w), pm, NULL);
}


/**************************************************************************
...
**************************************************************************/
void gtk_set_label(GtkWidget *w, char *text)
{
  char *str;

  if (!GTK_IS_LABEL(w))
    return;
  gtk_label_get(GTK_LABEL(w), &str);
  if(strcmp(str, text)) {
    gtk_label_set_text(GTK_LABEL(w), text);
    gtk_expose_now(w);
  }
}


/**************************************************************************
...
**************************************************************************/
GtkWidget *gtk_accelbutton_new(const gchar *label, GtkAccelGroup *accel)
{
  GtkWidget *button;
  GtkWidget *accel_label;
  guint accel_key;

  button = gtk_button_new ();

  accel_label = gtk_widget_new (GTK_TYPE_ACCEL_LABEL,
				"GtkWidget::visible", TRUE,
				"GtkWidget::parent", button,
				"GtkAccelLabel::accel_widget", button,
				NULL);
  accel_key = gtk_label_parse_uline (GTK_LABEL (accel_label), label);

  if ((accel_key != GDK_VoidSymbol) && accel)
    gtk_widget_add_accelerator (button, "clicked", accel,
				accel_key, GDK_MOD1_MASK, GTK_ACCEL_LOCKED);
  return button;
}


/**************************************************************************
...
**************************************************************************/
GdkPixmap *
gtk_scale_pixmap(GdkPixmap *src, int src_w, int src_h, int dst_w, int dst_h)
{
  GdkPixmap *dst;
  GdkImage *xi_src, *xi_dst;
  int xoffset_table[4096];
  int x, xoffset, xadd, xremsum, xremadd;
  int y, yoffset, yadd, yremsum, yremadd;

  xi_src=gdk_image_get(src, 0, 0, src_w, src_h);
  xi_dst=gdk_image_new(GDK_IMAGE_FASTEST, gdk_window_get_visual (root_window),
		       dst_w, dst_h);

  /* for each pixel in dst, calculate pixel offset in src */
  xadd=src_w/dst_w;
  xremadd=src_w%dst_w;
  xoffset=0;
  xremsum=dst_w/2;

  for(x=0; x<dst_w; ++x) {
    xoffset_table[x]=xoffset;
    xoffset+=xadd;
    xremsum+=xremadd;
    if(xremsum>=dst_w) {
      xremsum-=dst_w;
      ++xoffset;
    }
  }

  yadd=src_h/dst_h;
  yremadd=src_h%dst_h;
  yoffset=0;
  yremsum=dst_h/2; 

  for(y=0; y<dst_h; ++y) {
    for(x=0; x<dst_w; ++x) {
      guint32 pixel;

      pixel=gdk_image_get_pixel(xi_src, xoffset_table[x], yoffset);
      gdk_image_put_pixel(xi_dst, x, y, pixel);
    }

    yoffset+=yadd;
    yremsum+=yremadd;
    if(yremsum>=dst_h) {
      yremsum-=dst_h;
      ++yoffset;
    }
  }

  dst=gdk_pixmap_new(root_window, dst_w, dst_h, -1);
  gdk_draw_image(dst, civ_gc, xi_dst, 0, 0, 0, 0, dst_w, dst_h);
  gdk_image_destroy(xi_src);
  gdk_image_destroy(xi_dst);

  return dst;
}

/**************************************************************************
  Returns gettext-converted list of n strings.  Allocates the space
  for the returned list, but the individual strings in the list are
  as returned by gettext().  In case of no NLS, the strings will be
  the original strings, so caller should ensure that the originals
  persist for as long as required.  (For no NLS, still allocate the
  list, for consistency.)

  (This is not directly gui/gtk related, but it fits in here
  because so far it is used for doing i18n for gtk titles...)
**************************************************************************/
char **intl_slist(int n, char **s)
{
  char **ret = fc_malloc(n * sizeof(char*));
  int i;

  for(i=0; i<n; i++) {
    ret[i] = _(s[i]);
  }
  return ret;
}

