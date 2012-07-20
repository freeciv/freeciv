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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <math.h>

#include <gtk/gtk.h>

#include "gui_main.h"
#include "gtkpixcomm.h"


#if !GTK_CHECK_VERSION(3, 0, 0)
static gboolean gtk_pixcomm_expose     (GtkWidget *widget, GdkEventExpose *ev);
static void	gtk_pixcomm_size_request (GtkWidget *widget, GtkRequisition *requisition);
static void	gtk_pixcomm_destroy    (GtkObject *object);
#else  /* GTK 3 */
static gboolean gtk_pixcomm_draw     (GtkWidget *widget, cairo_t *cr);
static void	gtk_pixcomm_destroy    (GtkWidget *object);
static void
gtk_pixcomm_get_preferred_width(GtkWidget *widget, gint *minimal_width,
                                gint *natural_width);
static void
gtk_pixcomm_get_preferred_height(GtkWidget *widget, gint *minimal_height,
                                 gint *natural_height);
#endif /* GTK 3 */

static GtkMiscClass *parent_class;

typedef struct _GtkPixcommPrivate GtkPixcommPrivate;
struct _GtkPixcommPrivate
{
  cairo_surface_t *surface;
};

#define GTK_PIXCOMM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GTK_TYPE_PIXCOMM, GtkPixcommPrivate))

G_DEFINE_TYPE (GtkPixcomm, gtk_pixcomm, GTK_TYPE_MISC)

/***************************************************************************
  Initialize pixcomm class
***************************************************************************/
static void
gtk_pixcomm_class_init(GtkPixcommClass *klass)
{
#if !GTK_CHECK_VERSION(3, 0, 0)
  GtkObjectClass *object_class = GTK_OBJECT_CLASS(klass);
#endif
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  parent_class = g_type_class_peek_parent(klass);

#if !GTK_CHECK_VERSION(3, 0, 0)
  object_class->destroy = gtk_pixcomm_destroy;
  widget_class->expose_event = gtk_pixcomm_expose;
  widget_class->size_request = gtk_pixcomm_size_request;
#else
  widget_class->destroy = gtk_pixcomm_destroy;
  widget_class->draw = gtk_pixcomm_draw;
  widget_class->get_preferred_width = gtk_pixcomm_get_preferred_width;
  widget_class->get_preferred_height = gtk_pixcomm_get_preferred_height;
#endif
  g_type_class_add_private (widget_class, sizeof(GtkPixcommPrivate));
}

/***************************************************************************
  Initialize pixcomm instance
***************************************************************************/
static void
gtk_pixcomm_init(GtkPixcomm *pixcomm)
{
  GtkPixcommPrivate *priv = GTK_PIXCOMM_GET_PRIVATE(pixcomm);
  gtk_widget_set_has_window(GTK_WIDGET(pixcomm), FALSE);

  priv->surface = NULL;
}

#if !GTK_CHECK_VERSION(3, 0, 0)
static void
gtk_pixcomm_size_request (GtkWidget *p, GtkRequisition *requisition)
{
  int xpad, ypad;

  gtk_misc_get_padding(GTK_MISC(p), &xpad, &ypad);
  requisition->width = GTK_PIXCOMM(p)->w + xpad * 2;
  requisition->height = GTK_PIXCOMM(p)->h + ypad * 2;
  GTK_WIDGET_CLASS (gtk_pixcomm_parent_class)->size_request (p, requisition);
}
#endif

/***************************************************************************
  Destroy pixcomm instance
***************************************************************************/
#if !GTK_CHECK_VERSION(3, 0, 0)
static void
gtk_pixcomm_destroy(GtkObject *object)
{
  GtkPixcomm *p = GTK_PIXCOMM(object);
  GtkPixcommPrivate *priv = GTK_PIXCOMM_GET_PRIVATE(p);

  g_object_freeze_notify(G_OBJECT(p));
  
  cairo_surface_destroy(priv->surface);
  priv->surface = NULL;

  g_object_thaw_notify(G_OBJECT(p));

  if (GTK_OBJECT_CLASS(parent_class)->destroy) {
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(object);
  }
}

#else /* GTK 3 */

static void
gtk_pixcomm_destroy(GtkWidget *object)
{
  GtkPixcomm *p = GTK_PIXCOMM(object);
  GtkPixcommPrivate *priv = GTK_PIXCOMM_GET_PRIVATE(p);

  g_object_freeze_notify(G_OBJECT(p));

  cairo_surface_destroy(priv->surface);
  priv->surface = NULL;

  g_object_thaw_notify(G_OBJECT(p));

  if (GTK_WIDGET_CLASS(parent_class)->destroy) {
    (*GTK_WIDGET_CLASS(parent_class)->destroy)(object);
  }
}
#endif /* GTK 3 */

/***************************************************************************
  Create new pixcomm instance
***************************************************************************/
GtkWidget*
gtk_pixcomm_new(gint width, gint height)
{
  GtkPixcomm *p;
  GtkPixcommPrivate *priv;
  cairo_t *cr;
  int xpad, ypad;

  p = g_object_new(gtk_pixcomm_get_type(), NULL);
  priv = GTK_PIXCOMM_GET_PRIVATE(p);
  p->w = width; p->h = height;
  gtk_misc_get_padding(GTK_MISC(p), &xpad, &ypad);
  gtk_widget_set_size_request(GTK_WIDGET(p), width + xpad * 2, height + ypad * 2);
#if GTK_CHECK_VERSION(3, 0, 0)
  gtk_widget_set_halign(GTK_WIDGET(p), GTK_ALIGN_CENTER);
  gtk_widget_set_valign(GTK_WIDGET(p), GTK_ALIGN_CENTER);
#endif

  p->is_scaled = FALSE;
  p->scale = 1.0;

  priv->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create(priv->surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_destroy(cr);

  return GTK_WIDGET(p);
}

/****************************************************************************
  Set the scaling on the pixcomm.  All operations drawn on the pixcomm
  (before or after this function is called) will simply be scaled
  by this amount.
****************************************************************************/
void gtk_pixcomm_set_scale(GtkPixcomm *pixcomm, gdouble scale)
{
  fc_assert_ret(GTK_IS_PIXCOMM(pixcomm));
  fc_assert_ret(scale > 0.0);

  if (scale == 1.0) {
    pixcomm->is_scaled = FALSE;
    pixcomm->scale = 1.0;
  } else {
    pixcomm->is_scaled = TRUE;
    pixcomm->scale = scale;
  }
}

/***************************************************************************
  Get cairo surface from pixcomm.
***************************************************************************/
cairo_surface_t *gtk_pixcomm_get_surface(GtkPixcomm *pixcomm)
{
  fc_assert_ret_val(GTK_IS_PIXCOMM(pixcomm), NULL);
  return GTK_PIXCOMM_GET_PRIVATE(pixcomm)->surface;
}

/***************************************************************************
  Clear pixcomm
***************************************************************************/
void
gtk_pixcomm_clear(GtkPixcomm *p)
{
  fc_assert_ret(GTK_IS_PIXCOMM(p));
  GtkPixcommPrivate *priv = GTK_PIXCOMM_GET_PRIVATE(p);

  cairo_t *cr = cairo_create(priv->surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_destroy(cr);
  gtk_widget_queue_draw(GTK_WIDGET(p));
}

/***************************************************************************
  Copy sprite to pixcomm
***************************************************************************/
#if !GTK_CHECK_VERSION(3, 0, 0)
void gtk_pixcomm_copyto(GtkPixcomm *p, struct sprite *src, gint x, gint y)
{
  fc_assert_ret(GTK_IS_PIXCOMM(p));
  fc_assert_ret(src != NULL);
  GtkWidget *widget = GTK_WIDGET(p);
  GtkMisc *misc = GTK_MISC(p);
  GtkPixcommPrivate *priv = GTK_PIXCOMM_GET_PRIVATE(p);
  int width, height, offset_x, offset_y;
  gfloat xalign;
  cairo_t *cr = cairo_create(priv->surface);

  if (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_LTR) {
    xalign = misc->xalign;
  } else {
    xalign = 1.0 - misc->xalign;
  }
 
  offset_x = floor(widget->allocation.x + misc->xpad
             + ((widget->allocation.width - widget->requisition.width) *
             xalign));
  offset_y = floor(widget->allocation.y + misc->ypad 
             + ((widget->allocation.height - widget->requisition.height) *
             misc->yalign));

  get_sprite_dimensions(src, &width, &height);
  cairo_rectangle(cr, x, y, width, height);
  cairo_set_source_surface(cr, src->surface, x, y);
  cairo_paint(cr);
  cairo_destroy(cr);
  gtk_widget_queue_draw_area(GTK_WIDGET(p), offset_x, offset_y, width, height);
}

/***************************************************************************
  Draw pixcomm
***************************************************************************/
static gboolean
gtk_pixcomm_expose(GtkWidget *widget, GdkEventExpose *ev)
{
  GtkPixcommPrivate *priv;
  fc_assert_ret_val(GTK_IS_PIXCOMM(widget), FALSE);
  fc_assert_ret_val(ev!=NULL, FALSE);
  priv = GTK_PIXCOMM_GET_PRIVATE(GTK_PIXCOMM(widget));

  if (gtk_widget_is_drawable(widget)) {
    GtkPixcomm *p;
    GtkMisc *misc;
    gint x, y;
    gfloat xalign;

    p = GTK_PIXCOMM(widget);
    misc = GTK_MISC(widget);

    if (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_LTR) {
      xalign = misc->xalign;
    } else {
      xalign = 1.0 - misc->xalign;
    }

    x = floor(widget->allocation.x + misc->xpad
	+ ((widget->allocation.width - widget->requisition.width) *
	  xalign));
    y = floor(widget->allocation.y + misc->ypad 
	+ ((widget->allocation.height - widget->requisition.height) *
	  misc->yalign));

    {
      cairo_t *cr = gdk_cairo_create(widget->window);

      gdk_cairo_region(cr, ev->region);
      cairo_clip(cr);
      cairo_translate(cr, x, y);
      if (p->is_scaled) cairo_scale(cr, p->scale, p->scale);
      cairo_set_source_surface(cr, priv->surface, 0, 0);
      cairo_paint(cr);
      cairo_destroy(cr);
    }
  }
  return FALSE;
}

#else /* GTK 3 */

void gtk_pixcomm_copyto(GtkPixcomm *p, struct sprite *src, gint x, gint y)
{
  GtkMisc *misc = GTK_MISC(p);
  GtkPixcommPrivate *priv = GTK_PIXCOMM_GET_PRIVATE(p);
  int width, height, xpad, ypad;
  GtkAllocation allocation;
  cairo_t *cr = cairo_create(priv->surface);

  gtk_misc_get_padding(misc, &xpad, &ypad);
  gtk_widget_get_allocation(GTK_WIDGET(p), &allocation);

  fc_assert_ret(GTK_IS_PIXCOMM(p));
  fc_assert_ret(src != NULL);

  get_sprite_dimensions(src, &width, &height);
  cairo_rectangle(cr, x, y, width, height);
  cairo_set_source_surface(cr, src->surface, x, y);
  cairo_paint(cr);
  cairo_destroy(cr);
  gtk_widget_queue_draw_area(GTK_WIDGET(p),
      allocation.x + x + xpad,
      allocation.y + y + ypad,
      width, height);
}

static gboolean
gtk_pixcomm_draw(GtkWidget *widget, cairo_t *cr)
{
  GtkPixcommPrivate *priv;
  GtkPixcomm *p;
  GtkMisc *misc;
  gint xpad, ypad;

  fc_assert_ret_val(GTK_IS_PIXCOMM(widget), FALSE);

  priv = GTK_PIXCOMM_GET_PRIVATE(GTK_PIXCOMM(widget));

  p = GTK_PIXCOMM(widget);
  misc = GTK_MISC(widget);
  gtk_misc_get_padding(misc, &xpad, &ypad);

  cairo_translate(cr, xpad, ypad);

  if (p->is_scaled) {
    cairo_scale(cr, p->scale, p->scale);
  }
  cairo_set_source_surface(cr, priv->surface, 0, 0);
  cairo_paint(cr);

  return FALSE;
}

static void
gtk_pixcomm_get_preferred_width(GtkWidget *widget, gint *minimal_width,
                                gint *natural_width)
{
  int xpad;

  gtk_misc_get_padding(GTK_MISC(widget), &xpad, NULL);
  *minimal_width = *natural_width = GTK_PIXCOMM(widget)->w + xpad * 2;
}

static void
gtk_pixcomm_get_preferred_height(GtkWidget *widget, gint *minimal_height,
                                gint *natural_height)
{
  int ypad;

  gtk_misc_get_padding(GTK_MISC(widget), NULL, &ypad);
  *minimal_height = *natural_height = GTK_PIXCOMM(widget)->h + ypad * 2;
}
#endif /* GTK 3 */

/***************************************************************************
  Create new gtkpixcomm from sprite.
***************************************************************************/
GtkWidget*
gtk_pixcomm_new_from_sprite(struct sprite *sprite)
{
  GtkPixcomm *p;
  GtkPixcommPrivate *priv;
  cairo_t *cr;
  int xpad, ypad;

  p = g_object_new(gtk_pixcomm_get_type(), NULL);
  priv = GTK_PIXCOMM_GET_PRIVATE(p);
  get_sprite_dimensions(sprite, &p->w, &p->h);
  gtk_misc_get_padding(GTK_MISC(p), &xpad, &ypad);
  gtk_widget_set_size_request(GTK_WIDGET(p), p->w + xpad * 2, p->h + ypad * 2);
#if GTK_CHECK_VERSION(3, 0, 0)
  gtk_widget_set_halign(GTK_WIDGET(p), GTK_ALIGN_CENTER);
  gtk_widget_set_valign(GTK_WIDGET(p), GTK_ALIGN_CENTER);
#endif

  p->is_scaled = FALSE;
  p->scale = 1.0;

  priv->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, p->w, p->h);
  cr = cairo_create(priv->surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, sprite->surface, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);

  return GTK_WIDGET(p);
}

/***************************************************************************
  Draw sprite to gtkpixcomm. Grkpixcomm dimensions may change.
***************************************************************************/
void gtk_pixcomm_set_from_sprite(GtkPixcomm *p, struct sprite *sprite)
{
  GtkPixcommPrivate *priv;
  cairo_t *cr;
  int width, height;
  get_sprite_dimensions(sprite, &width, &height);
  priv = GTK_PIXCOMM_GET_PRIVATE(p);

  cairo_surface_destroy(priv->surface);
  priv->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create(priv->surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, sprite->surface, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  p->w = width;
  p->h = height;
  gtk_widget_queue_resize(GTK_WIDGET(p));
}
