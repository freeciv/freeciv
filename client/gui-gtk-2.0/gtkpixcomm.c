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

enum op_t {
  OP_FILL,
  OP_COPY,
  OP_END
};

struct op {
  enum op_t type;

  /* OP_FILL */
  GdkColor *color;

  /* OP_COPY */
  SPRITE *src;
  gint x, y;
};

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

  pixcomm->actions = NULL;
  pixcomm->freeze_count = 0;
}

static void
gtk_pixcomm_destroy(GtkObject *object)
{
  GtkPixcomm *p = GTK_PIXCOMM(object);

  g_object_freeze_notify(G_OBJECT(p));
  
  if (p->actions) {
    g_array_free(p->actions, FALSE);
  }
  p->actions = NULL;

  g_object_thaw_notify(G_OBJECT(p));

  GTK_OBJECT_CLASS(parent_class)->destroy(object);
}

GtkWidget*
gtk_pixcomm_new(gint width, gint height)
{
  GtkPixcomm *p;

  p = gtk_type_new(GTK_TYPE_PIXCOMM);
  p->w = width; p->h = height;

  p->actions = g_array_new(FALSE, FALSE, sizeof(struct op));

  GTK_WIDGET(p)->requisition.width = p->w + GTK_MISC(p)->xpad * 2;
  GTK_WIDGET(p)->requisition.height = p->h + GTK_MISC(p)->ypad * 2;

  return GTK_WIDGET(p);
}

static void
refresh(GtkPixcomm *p)
{
  if (p->freeze_count == 0) {
    gtk_widget_queue_draw(GTK_WIDGET(p));
  }
}


void
gtk_pixcomm_clear(GtkPixcomm *p)
{
  g_return_if_fail(GTK_IS_PIXCOMM(p));

  g_array_set_size(p->actions, 0);
  refresh(p);
}

void
gtk_pixcomm_fill(GtkPixcomm *p, GdkColor *color)
{
  struct op v;

  g_return_if_fail(GTK_IS_PIXCOMM(p));
  g_return_if_fail(color != NULL);

  g_array_set_size(p->actions, 0);

  v.type	= OP_FILL;
  v.color	= color;
  g_array_append_val(p->actions, v);
  refresh(p);
}

void
gtk_pixcomm_copyto(GtkPixcomm *p, SPRITE *src, gint x, gint y)
{
  struct op v;

  g_return_if_fail(GTK_IS_PIXCOMM(p));
  g_return_if_fail(src != NULL);

  v.type	= OP_COPY;
  v.src		= src;
  v.x		= x;
  v.y		= y;
  g_array_append_val(p->actions, v);
  refresh(p);
}

static gint
gtk_pixcomm_expose(GtkWidget *widget, GdkEventExpose *event)
{
  g_return_val_if_fail(GTK_IS_PIXCOMM(widget), FALSE);
  g_return_val_if_fail(event!=NULL, FALSE);

  if (GTK_WIDGET_VISIBLE(widget) && GTK_WIDGET_MAPPED(widget)) {
    GtkPixcomm *p;
    GtkMisc *misc;
    gint x, y;
    guint i;

    p = GTK_PIXCOMM(widget);
    misc = GTK_MISC(widget);

    if (p->actions->len <= 0)
      return FALSE;

    x=(widget->allocation.x * (1.0 - misc->xalign) +
      (widget->allocation.x + widget->allocation.width
      - (widget->requisition.width  - misc->xpad * 2)) * misc->xalign) + 0.5;
    y=(widget->allocation.y * (1.0 - misc->yalign) +
      (widget->allocation.y + widget->allocation.height
      - (widget->requisition.height - misc->ypad * 2)) * misc->yalign) + 0.5;

    /* draw! */
    for (i = 0; i < p->actions->len; i++) {
      const struct op *rop = &g_array_index(p->actions, struct op, i);

      switch (rop->type) {
      case OP_FILL:
        gdk_gc_set_foreground(civ_gc, rop->color);
        gdk_draw_rectangle(widget->window, civ_gc, TRUE, 0, 0, -1, -1);
        break;

      case OP_COPY:
        if (rop->src->has_mask) {
          gdk_gc_set_clip_origin(civ_gc, x + rop->x, y + rop->y);
          gdk_gc_set_clip_mask(civ_gc, rop->src->mask);

          gdk_draw_drawable(widget->window, civ_gc, rop->src->pixmap,
                            0, 0,
			    x + rop->x, y + rop->y,
			    rop->src->width, rop->src->height);

          gdk_gc_set_clip_mask(civ_gc, NULL);
          gdk_gc_set_clip_origin(civ_gc, 0, 0);
        } else {
          gdk_draw_drawable(widget->window, civ_gc, rop->src->pixmap,
                          0, 0,
			  x + rop->x, y + rop->y,
			  rop->src->width, rop->src->height);
	}
        break;

      default:
        break;
      }
    }
  }
  return FALSE;
}

void
gtk_pixcomm_freeze(GtkPixcomm *p)
{
  g_return_if_fail(GTK_IS_PIXCOMM(p));

  p->freeze_count++;
}

void
gtk_pixcomm_thaw(GtkPixcomm *p)
{
  g_return_if_fail(GTK_IS_PIXCOMM(p));

  if (p->freeze_count > 0) {
    p->freeze_count--;

    refresh(p);
  }
}

static void
build_insensitive_pixbuf(GtkPixcomm *pixcomm)
{
  /* gdk_pixbuf_composite_color_simple() */
}
