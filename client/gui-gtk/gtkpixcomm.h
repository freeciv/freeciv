/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
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

#ifndef __GTK_PIXCOMM_H__
#define __GTK_PIXCOMM_H__

#include <gtk/gtkmisc.h>

#include "graphics.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GTK_TYPE_PIXCOMM		 (gtk_pixcomm_get_type ())
#define GTK_PIXCOMM(obj)		 (GTK_CHECK_CAST ((obj), GTK_TYPE_PIXCOMM, GtkPixcomm))
#define GTK_PIXCOMM_CLASS(klass)	 (GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_PIXCOMM, GtkPixcommClass))
#define GTK_IS_PIXCOMM(obj)		 (GTK_CHECK_TYPE ((obj), GTK_TYPE_PIXCOMM))
#define GTK_IS_PIXCOMM_CLASS(klass)	 (GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_PIXCOMM))


typedef struct _GtkPixcomm	GtkPixcomm;
typedef struct _GtkPixcommClass	GtkPixcommClass;

struct _GtkPixcomm
{
  GtkMisc misc;
  
  GdkPixmap *pixmap;
  GdkBitmap *mask;
  GdkPixmap *pixmap_insensitive;

  GdkGC     *fg_gc;
  GdkGC     *mask_fg_gc;
  GdkGC     *mask_bg_gc;
};

struct _GtkPixcommClass
{
  GtkMiscClass parent_class;
};


GtkType	   gtk_pixcomm_get_type	 (void);
GtkWidget* gtk_pixcomm_new	 (GdkWindow *window, gint width, gint height);
void       gtk_pixcomm_copyto	 (GtkPixcomm *pixcomm, SPRITE *src,
				  gint x, gint y, gboolean refresh);
void       gtk_pixcomm_changed	 (GtkPixcomm *pixcomm);
void       gtk_pixcomm_clear	 (GtkPixcomm *pixcomm, gboolean refresh);
void	   gtk_pixcomm_fill	 (GtkPixcomm *pixcomm, GdkColor *color,
				  gboolean refresh);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GTK_PIXCOMM_H__ */
