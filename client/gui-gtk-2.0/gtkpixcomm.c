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
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

/*
 * Based code for GtkPixcomm off GtkImage from the standard GTK+ distribution.
 * This widget will have a built-in X window for capturing "click" events, so
 * that we no longer need to insert it inside a GtkEventBox. -vasc
 */

#include "gui_main.h"
#include "gtkpixcomm.h"

static void gtk_pixcomm_class_init (GtkPixcommClass *klass);
static void gtk_pixcomm_init       (GtkPixcomm *pixcomm);
static gint gtk_pixcomm_expose     (GtkWidget *widget, GdkEventExpose *event);
static void gtk_pixcomm_destroy    (GtkObject *object);
static void build_insensitive_pixbuf (GtkPixcomm *pixcomm);

static GtkMiscClass *parent_class;

GtkType
gtk_pixcomm_get_type(void)
{
  static GtkType pixcomm_type = 0;

  if (!pixcomm_type) {
    static const GtkTypeInfo pixcomm_info = {
      "GtkPixcomm",
      sizeof (GtkPixcomm),
      sizeof (GtkPixcommClass),
      (GtkClassInitFunc) gtk_pixcomm_class_init,
      (GtkObjectInitFunc) gtk_pixcomm_init,
      /* reserved_1 */ NULL,
      /* reserved_2 */ NULL,
      (GtkClassInitFunc) NULL,
    };

    pixcomm_type=gtk_type_unique(GTK_TYPE_MISC, &pixcomm_info);
  }

  return pixcomm_type;
}

static void
gtk_pixcomm_class_init(GtkPixcommClass *klass)
{
  GObjectClass *gobject_class;
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;

  parent_class = gtk_type_class(GTK_TYPE_MISC);
  gobject_class = G_OBJECT_CLASS(klass);
  object_class = GTK_OBJECT_CLASS(klass);
  widget_class = GTK_WIDGET_CLASS(klass);

  object_class->destroy = gtk_pixcomm_destroy;
  widget_class->expose_event = gtk_pixcomm_expose;
}

static void
gtk_pixcomm_init(GtkPixcomm *pixcomm)
{
  GTK_WIDGET_SET_FLAGS(pixcomm, GTK_NO_WINDOW);

  pixcomm->pixmap = NULL;
  pixcomm->mask = NULL;
}

static void
gtk_pixcomm_destroy(GtkObject *object)
{
  GtkPixcomm *pixcomm = GTK_PIXCOMM(object);

  g_object_freeze_notify(G_OBJECT(pixcomm));
  if (pixcomm->pixmap)
    g_object_unref(G_OBJECT(pixcomm->pixmap));
  pixcomm->pixmap = NULL;
  if (pixcomm->mask)
    g_object_unref(G_OBJECT(pixcomm->mask));
  pixcomm->mask = NULL;
  g_object_thaw_notify(G_OBJECT(pixcomm));

  GTK_OBJECT_CLASS(parent_class)->destroy(object);
}

GtkWidget*
gtk_pixcomm_new(GdkWindow *window, gint width, gint height)
{
  GtkPixcomm *p;

  g_return_val_if_fail(GDK_IS_WINDOW(window), NULL);
  
  p = gtk_type_new(GTK_TYPE_PIXCOMM);
  p->w = width; p->h = height;
  p->pixmap = gdk_pixmap_new(root_window, p->w, p->h, -1);
  p->mask = gdk_pixmap_new(root_window, p->w, p->h, 1);
  GTK_WIDGET(p)->requisition.width = p->w + GTK_MISC(p)->xpad * 2;
  GTK_WIDGET(p)->requisition.height = p->h + GTK_MISC(p)->ypad * 2;

  return GTK_WIDGET(p);
}

void
gtk_pixcomm_changed(GtkPixcomm *pixcomm)
{
  g_return_if_fail(GTK_IS_PIXCOMM(pixcomm));

  if (GTK_WIDGET_VISIBLE(pixcomm))
    gtk_widget_queue_clear(GTK_WIDGET(pixcomm));
}

void
gtk_pixcomm_clear(GtkPixcomm *p, gboolean refresh)
{
  g_return_if_fail(GTK_IS_PIXCOMM(p));

  gdk_draw_rectangle(p->mask, mask_bg_gc, TRUE, 0, 0, -1, -1);

  if (refresh)
    gtk_pixcomm_changed(p);
}

void
gtk_pixcomm_fill(GtkPixcomm *p, GdkColor *color, gboolean refresh)
{
  g_return_if_fail(GTK_IS_PIXCOMM(p));
  g_return_if_fail(color != NULL);

  gdk_gc_set_foreground(civ_gc, color);

  gdk_draw_rectangle(p->pixmap, civ_gc, TRUE, 0, 0, -1, -1);
  gdk_draw_rectangle(p->mask, mask_fg_gc, TRUE, 0, 0, -1, -1);

  if (refresh)
    gtk_pixcomm_changed(p);
}

void
gtk_pixcomm_copyto(GtkPixcomm *p, SPRITE *src, gint x, gint y,
		   gboolean refresh)
{
  g_return_if_fail(GTK_IS_PIXCOMM(p));
  g_return_if_fail(src != NULL);

  if (src->has_mask)
  {
    gdk_gc_set_clip_origin(civ_gc, x, y);
    gdk_gc_set_clip_mask(civ_gc, src->mask);
  }

  gdk_draw_drawable(p->pixmap, civ_gc, src->pixmap,
                    0, 0, x, y, src->width, src->height);

  if (src->has_mask)
  {
    gdk_gc_set_clip_origin(mask_fg_gc, x, y);
    gdk_draw_drawable(p->mask, mask_fg_gc, src->mask,
                      0, 0, x, y, src->width, src->height);
    gdk_gc_set_clip_mask(civ_gc, NULL);
    gdk_gc_set_clip_origin(civ_gc, 0, 0);
    gdk_gc_set_clip_origin(mask_fg_gc, 0, 0);
  }
  else
  {
    gdk_draw_rectangle(p->mask, mask_fg_gc, TRUE,
                       0, 0, src->width, src->height);
  }

  if (refresh)
    gtk_pixcomm_changed(p);
}

static gint
gtk_pixcomm_expose(GtkWidget *widget, GdkEventExpose *event)
{
  g_return_val_if_fail(GTK_IS_PIXCOMM(widget), FALSE);
  g_return_val_if_fail(event!=NULL, FALSE);

  if (GTK_WIDGET_VISIBLE(widget) && GTK_WIDGET_MAPPED(widget)) {
    GtkPixcomm *pixcomm;
    GtkMisc *misc;
    gint x, y;

    pixcomm=GTK_PIXCOMM(widget);
    misc=GTK_MISC(widget);

    x=(widget->allocation.x * (1.0 - misc->xalign) +
      (widget->allocation.x + widget->allocation.width
      - (widget->requisition.width  - misc->xpad * 2)) * misc->xalign) + 0.5;
    y=(widget->allocation.y * (1.0 - misc->yalign) +
      (widget->allocation.y + widget->allocation.height
      - (widget->requisition.height - misc->ypad * 2)) * misc->yalign) + 0.5;

    gdk_gc_set_clip_mask(civ_gc, pixcomm->mask);
    gdk_gc_set_clip_origin(civ_gc, x, y);
    gdk_draw_drawable(widget->window, civ_gc, pixcomm->pixmap, 0, 0, x, y, -1, -1);
    gdk_gc_set_clip_mask(civ_gc, NULL);
    gdk_gc_set_clip_origin(civ_gc, 0, 0);
  }
  return FALSE;
}

static void
build_insensitive_pixbuf(GtkPixcomm *pixcomm)
{
  /* gdk_pixbuf_composite_color_simple() */
}
