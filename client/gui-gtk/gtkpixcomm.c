/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * Insensitive pixcomm building code by Eckehard Berns from GNOME Stock
 * Copyright (C) 1997, 1998 Free Software Foundation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-1999.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

/*
 * Based code for GtkPixcomm off GtkPixmap from the standard GTK+ distribution.
 * This widget will have a built-in X window for capturing "click" events, so
 * that we no longer need to insert it inside a GtkEventBox. -vasc
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gtkpixcomm.h"

static void gtk_pixcomm_class_init (GtkPixcommClass *cclass);
static void gtk_pixcomm_init       (GtkPixcomm *pixcomm);
static gint gtk_pixcomm_expose     (GtkWidget *widget, GdkEventExpose *event);
static void gtk_pixcomm_destroy    (GtkObject *object);
static void build_insensitive_pixmap (GtkPixcomm *pixcomm);

static GtkWidgetClass *parent_class;

GtkType
gtk_pixcomm_get_type (void)
{
  static GtkType pixcomm_type = 0;

  if (!pixcomm_type)
    {
      static const GtkTypeInfo pixcomm_info =
      {
	(char *)"GtkPixcomm",
	sizeof (GtkPixcomm),
	sizeof (GtkPixcommClass),
	(GtkClassInitFunc) gtk_pixcomm_class_init,
	(GtkObjectInitFunc) gtk_pixcomm_init,
	/* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      pixcomm_type = gtk_type_unique (GTK_TYPE_MISC, &pixcomm_info);
    }

  return pixcomm_type;
}

static void
gtk_pixcomm_class_init (GtkPixcommClass *cclass)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = (GtkObjectClass*) cclass;
  widget_class = (GtkWidgetClass*) cclass;
  parent_class = gtk_type_class (GTK_TYPE_MISC);

  object_class->destroy = gtk_pixcomm_destroy;
  widget_class->expose_event = gtk_pixcomm_expose;
}

static void
gtk_pixcomm_init (GtkPixcomm *pixcomm)
{
  GTK_WIDGET_SET_FLAGS (pixcomm, GTK_NO_WINDOW);

  pixcomm->pixmap= NULL;
  pixcomm->mask  = NULL;
  pixcomm->pixmap_insensitive= NULL;
}

static void
gtk_pixcomm_destroy (GtkObject *object)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_PIXCOMM (object));

  gdk_pixmap_unref (GTK_PIXCOMM (object)->pixmap);
  gdk_pixmap_unref (GTK_PIXCOMM (object)->mask);
  if (GTK_PIXCOMM (object)->pixmap_insensitive)
    gdk_pixmap_unref (GTK_PIXCOMM (object)->pixmap_insensitive);
  gdk_gc_destroy (GTK_PIXCOMM (object)->fg_gc);
  gdk_gc_destroy (GTK_PIXCOMM (object)->mask_fg_gc);
  gdk_gc_destroy (GTK_PIXCOMM (object)->mask_bg_gc);

  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

GtkWidget*
gtk_pixcomm_new (GdkWindow *window, gint width, gint height)
{
  GtkPixcomm *pixcomm;
  GdkColor pixel;
   
  g_return_val_if_fail (window != NULL, NULL);
  
  pixcomm = gtk_type_new (GTK_TYPE_PIXCOMM);

  pixcomm->pixmap=gdk_pixmap_new(window, width, height,-1);
  pixcomm->mask  =gdk_pixmap_new(window, width, height, 1);

  pixcomm->fg_gc =gdk_gc_new(window);
  pixcomm->mask_fg_gc =gdk_gc_new(pixcomm->mask);
  pixel.pixel=1;
  gdk_gc_set_foreground(pixcomm->mask_fg_gc, &pixel);
  gdk_gc_set_function(pixcomm->mask_fg_gc, GDK_OR);
  pixcomm->mask_bg_gc =gdk_gc_new(pixcomm->mask);
  pixel.pixel=0;
  gdk_gc_set_foreground(pixcomm->mask_bg_gc, &pixel);

  GTK_WIDGET (pixcomm)->requisition.width =
				width + GTK_MISC (pixcomm)->xpad * 2;
  GTK_WIDGET (pixcomm)->requisition.height =
				height+ GTK_MISC (pixcomm)->ypad * 2;
  return GTK_WIDGET (pixcomm);
}

void
gtk_pixcomm_changed (GtkPixcomm *pixcomm)
{
  g_return_if_fail (pixcomm != NULL);
  g_return_if_fail (GTK_IS_PIXCOMM (pixcomm));

  if (pixcomm->pixmap_insensitive)
  {
    gdk_pixmap_unref (pixcomm->pixmap_insensitive);
    pixcomm->pixmap_insensitive=NULL;
  }

  if (GTK_WIDGET_VISIBLE (pixcomm))
    gtk_widget_queue_clear (GTK_WIDGET (pixcomm));
}

void
gtk_pixcomm_clear (GtkPixcomm *pixcomm, gboolean refresh)
{
  g_return_if_fail (pixcomm != NULL);
  g_return_if_fail (GTK_IS_PIXCOMM (pixcomm));

  gdk_draw_rectangle (pixcomm->mask, pixcomm->mask_bg_gc, TRUE, 0, 0,
	GTK_WIDGET (pixcomm)->requisition.width,
	GTK_WIDGET (pixcomm)->requisition.height);

  if (refresh)
    gtk_pixcomm_changed(pixcomm);
}

void
gtk_pixcomm_fill (GtkPixcomm *pixcomm, GdkColor *color, gboolean refresh)
{
  g_return_if_fail (pixcomm != NULL);
  g_return_if_fail (GTK_IS_PIXCOMM (pixcomm));
  g_return_if_fail (color != NULL);

  gdk_gc_set_foreground (pixcomm->fg_gc, color);

  gdk_draw_rectangle (pixcomm->pixmap, pixcomm->fg_gc, TRUE, 0, 0,
	GTK_WIDGET (pixcomm)->requisition.width,
	GTK_WIDGET (pixcomm)->requisition.height);
  gdk_draw_rectangle (pixcomm->mask, pixcomm->mask_fg_gc, TRUE, 0, 0,
	GTK_WIDGET (pixcomm)->requisition.width,
	GTK_WIDGET (pixcomm)->requisition.height);

  if (refresh)
    gtk_pixcomm_changed(pixcomm);
}

void
gtk_pixcomm_copyto (GtkPixcomm *pixcomm, SPRITE *src,
			gint x, gint y, gboolean refresh)
{
  g_return_if_fail (pixcomm != NULL);
  g_return_if_fail (GTK_IS_PIXCOMM (pixcomm));
  g_return_if_fail (src != NULL);

  if (src->has_mask)
  {
    gdk_gc_set_clip_origin(pixcomm->fg_gc, x, y);
    gdk_gc_set_clip_mask(pixcomm->fg_gc, src->mask);
  }

  gdk_draw_pixmap (pixcomm->pixmap, pixcomm->fg_gc, src->pixmap,
		   0, 0, x, y, src->width, src->height);

  if (src->has_mask)
  {
    gdk_gc_set_clip_origin(pixcomm->mask_fg_gc, x, y);
    gdk_draw_pixmap (pixcomm->mask, pixcomm->mask_fg_gc, src->mask,
		     0, 0, x, y, src->width, src->height);
    gdk_gc_set_clip_mask(pixcomm->fg_gc, NULL);
    gdk_gc_set_clip_origin(pixcomm->fg_gc, 0, 0);
    gdk_gc_set_clip_origin(pixcomm->mask_fg_gc, 0, 0);
  }
  else
  {
    gdk_draw_rectangle (pixcomm->mask, pixcomm->mask_fg_gc, TRUE,
			x, y, src->width, src->height);
  }

  if (refresh)
    gtk_pixcomm_changed(pixcomm);
}

static gint
gtk_pixcomm_expose (GtkWidget *widget, GdkEventExpose *event)
{
  GtkPixcomm *pixcomm;
  GtkMisc *misc;
  gint x, y;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_PIXCOMM (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  if (GTK_WIDGET_DRAWABLE (widget))
    {
      pixcomm = GTK_PIXCOMM (widget);
      misc = GTK_MISC (widget);

      x = (widget->allocation.x * (1.0 - misc->xalign) +
	   (widget->allocation.x + widget->allocation.width
	    - (widget->requisition.width - misc->xpad * 2)) *
	   misc->xalign) + 0.5;
      y = (widget->allocation.y * (1.0 - misc->yalign) +
	   (widget->allocation.y + widget->allocation.height
	    - (widget->requisition.height - misc->ypad * 2)) *
	   misc->yalign) + 0.5;

      gdk_gc_set_clip_mask (widget->style->black_gc, pixcomm->mask);
      gdk_gc_set_clip_origin (widget->style->black_gc, x, y);

      if (GTK_WIDGET_STATE (widget) == GTK_STATE_INSENSITIVE)
        {
          if (!pixcomm->pixmap_insensitive)
            build_insensitive_pixmap (pixcomm);
          gdk_draw_pixmap (widget->window,
	   	           widget->style->black_gc,
		           pixcomm->pixmap_insensitive,
		           0, 0, x, y, -1, -1);
        }
      else
	{
          gdk_draw_pixmap (widget->window,
	   	           widget->style->black_gc,
		           pixcomm->pixmap,
		           0, 0, x, y, -1, -1);
	}

	gdk_gc_set_clip_mask (widget->style->black_gc, NULL);
	gdk_gc_set_clip_origin (widget->style->black_gc, 0, 0);
    }
  return FALSE;
}

static void
build_insensitive_pixmap(GtkPixcomm *pixcomm)
{
  GdkPixmap *pixmap = pixcomm->pixmap;
  GdkPixmap *insensitive;
  gint w, h, x, y;
  GdkGCValues vals;
  GdkVisual *visual;
  GdkImage *image;
  GdkColorContext *cc;
  GdkColor color;
  GdkColormap *cmap;
  gint32 red, green, blue;
  GtkStyle *style;
  GtkWidget *window;
  GdkColor c;
  int failed;

  window = GTK_WIDGET (pixcomm);

  g_return_if_fail(window != NULL);
  g_return_if_fail(pixcomm->pixmap_insensitive == NULL);

  gdk_window_get_size(pixmap, &w, &h);
  image = gdk_image_get(pixmap, 0, 0, w, h);
  insensitive=gdk_pixmap_new(GTK_WIDGET (pixcomm)->window, w, h, -1);

  visual = gtk_widget_get_visual(GTK_WIDGET(pixcomm));
  cmap = gtk_widget_get_colormap(GTK_WIDGET(pixcomm));
  cc = gdk_color_context_new(visual, cmap);

  if ((cc->mode != GDK_CC_MODE_TRUE) && (cc->mode != GDK_CC_MODE_MY_GRAY)) 
    {
      gdk_draw_image(insensitive, pixcomm->fg_gc, image, 0, 0, 0, 0, w, h);

      style = gtk_widget_get_style(window);
      color = style->bg[0];
      gdk_gc_set_foreground (pixcomm->fg_gc, &color);
      for (y = 0; y < h; y++) 
        {
          for (x = y % 2; x < w; x += 2) 
	    {
              gdk_draw_point(insensitive, pixcomm->fg_gc, x, y);
            }
        }
    }
  else
    {
      gdk_gc_get_values(pixcomm->fg_gc, &vals);
      style = gtk_widget_get_style(window);

      color = style->bg[0];
      red = color.red;
      green = color.green;
      blue = color.blue;

      for (y = 0; y < h; y++) 
	{
	  for (x = 0; x < w; x++) 
	    {
	      c.pixel = gdk_image_get_pixel(image, x, y);
	      gdk_color_context_query_color(cc, &c);
	      c.red = (((gint32)c.red - red) >> 1) + red;
	      c.green = (((gint32)c.green - green) >> 1) + green;
	      c.blue = (((gint32)c.blue - blue) >> 1) + blue;
	      c.pixel = gdk_color_context_get_pixel(cc, c.red, c.green, c.blue,
						    &failed);
	      gdk_image_put_pixel(image, x, y, c.pixel);
	    }
	}

      for (y = 0; y < h; y++) 
	{
	  for (x = y % 2; x < w; x += 2) 
	    {
	      c.pixel = gdk_image_get_pixel(image, x, y);
	      gdk_color_context_query_color(cc, &c);
	      c.red = (((gint32)c.red - red) >> 1) + red;
	      c.green = (((gint32)c.green - green) >> 1) + green;
	      c.blue = (((gint32)c.blue - blue) >> 1) + blue;
	      c.pixel = gdk_color_context_get_pixel(cc, c.red, c.green, c.blue,
						    &failed);
	      gdk_image_put_pixel(image, x, y, c.pixel);
	    }
	}

      gdk_draw_image(insensitive, pixcomm->fg_gc, image, 0, 0, 0, 0, w, h);
    }
  pixcomm->pixmap_insensitive = insensitive;

  gdk_image_destroy(image);
  gdk_color_context_free(cc);
}
