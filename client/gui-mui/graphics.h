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
#ifndef FC__GRAPHICS_H
#define FC__GRAPHICS_H

//#include <gtk/gtk.h>

#include "graphics_g.h"

struct Sprite
{
	int width;
	int height;
	int hasmask;

	int offx,offy;
  int reference;
	int numsprites; // contains the number of references

  void *bitmap;
	void *mask;
	void *picture;

	struct Sprite *parent;

//	int dtobj;			// TRUE if picture is datatypes object
};
/*
GdkPixmap *	create_overlay_unit	(int i);
*/
#endif  /* FC__GRAPHICS_H */
