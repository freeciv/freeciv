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
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <log.h>

extern GdkWindow *root_window;

extern GdkGC *mask_fg_gc;
extern GdkGC *mask_bg_gc;
extern GdkGC *civ_gc;

void put_line_8 (char *psrc, char *pdst,  int dst_w, int xoffset_table[]);
void put_line_16(char *psrc, char *pdst,  int dst_w, int xoffset_table[]);
void put_line_24(char *psrc, char *pdst,  int dst_w, int xoffset_table[]);
void put_line_32(char *psrc, char *pdst,  int dst_w, int xoffset_table[]);

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
void gtk_changed_pixmap(GtkWidget *p)	/* fooling GTK+ -vasco */
{
  if (!GTK_IS_PIXMAP(p))
    return;

  if (GTK_PIXMAP(p)->pixmap_insensitive) {
    gdk_pixmap_unref (GTK_PIXMAP(p)->pixmap_insensitive);
    GTK_PIXMAP(p)->pixmap_insensitive=NULL;
  }
}


/**************************************************************************
...
**************************************************************************/
void gtk_clear_pixmap(GtkWidget *w)
{
  if (!GTK_IS_PIXMAP(w))
    return;
  gdk_draw_rectangle(GTK_PIXMAP(w)->mask, mask_bg_gc, TRUE, 0, 0, -1, -1);
  gtk_changed_pixmap(w);
}


/**************************************************************************
...
**************************************************************************/
GtkWidget *gtk_new_pixmap(gint width, gint height)
{
  return gtk_pixmap_new(gdk_pixmap_new(root_window, width, height,-1),
			gdk_pixmap_new(root_window, width, height, 1));
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
gtk_scale_pixmap(GdkPixmap *src, int src_w, int src_h, int dst_w, int dst_h, 
		 GdkWindow *root)
{
  GdkPixmap *dst;
  GdkImage *xi_src, *xi_dst;
  int xoffset_table[4096];
  int x, xoffset, xadd, xremsum, xremadd;
  int y, yoffset, yadd, yremsum, yremadd;
  char *pdst_data;
  char *psrc_data;

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
    psrc_data=((char *)xi_src->mem) + (yoffset * xi_src->bpl);
    pdst_data=((char *)xi_dst->mem) + (y * xi_dst->bpl);

    switch(xi_src->bpp) {
     case 8:
      put_line_8 (psrc_data, pdst_data, dst_w, xoffset_table);
      break;
     case 16:
      put_line_16(psrc_data, pdst_data, dst_w, xoffset_table);
      break;
     case 24:
      put_line_24(psrc_data, pdst_data, dst_w, xoffset_table);
      break;
     case 32:
      put_line_32(psrc_data, pdst_data, dst_w, xoffset_table);
      break;
     default:
      memcpy(pdst_data, psrc_data, (src_w<dst_w) ? src_w : dst_w);
      break;
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

  return dst;
}

void put_line_8(char *psrc, char *pdst,  int dst_w, int xoffset_table[])
{
  int x;
  for(x=0; x<dst_w; ++x)
    *pdst++=*(psrc+xoffset_table[x]+0);
}

void put_line_16(char *psrc, char *pdst,  int dst_w, int xoffset_table[])
{
  int x;
  for(x=0; x<dst_w; ++x) {
    *pdst++=*(psrc+2*xoffset_table[x]+0);
    *pdst++=*(psrc+2*xoffset_table[x]+1);
  }
}

void put_line_24(char *psrc, char *pdst,  int dst_w, int xoffset_table[])
{
  int x;
  for(x=0; x<dst_w; ++x) {
    *pdst++=*(psrc+3*xoffset_table[x]+0);
    *pdst++=*(psrc+3*xoffset_table[x]+1);
    *pdst++=*(psrc+3*xoffset_table[x]+1);
  }
}

void put_line_32(char *psrc, char *pdst,  int dst_w, int xoffset_table[])
{
  int x;
  for(x=0; x<dst_w; ++x) {
    *pdst++=*(psrc+4*xoffset_table[x]+0);
    *pdst++=*(psrc+4*xoffset_table[x]+1);
    *pdst++=*(psrc+4*xoffset_table[x]+2);
    *pdst++=*(psrc+4*xoffset_table[x]+3);
  }
}
