/***********************************************************************
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

/***********************************************************************
                          graphics.h  -  description
                             -------------------
    begin                : Mon Jul 1 2002
    copyright            : (C) 2000 by Michael Speck
                         : (C) 2002 by Rafał Bursig
    email                : Michael Speck <kulkanie@gmx.net>
                         : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifndef FC__GRAPHICS_H
#define FC__GRAPHICS_H

/* SDL3 */
#include "SDL3_gfx/SDL3_rotozoom.h"

/* client */
#include "graphics_g.h"

/* gui-sdl3 */
#include "canvas.h"
#include "gui_main.h"

#define RECT_LIMIT  80

/* DUFFS LOOPs come from SDL lib (LGPL) */
/* DUFFS_LOOP_DOUBLE2 and DUFFS_LOOP_QUATRO2 was created by Rafal Bursig under GPL */

/* This is a very useful loop for optimizing blitters */
#if defined(_MSC_VER) && (_MSC_VER == 1300)
/* There's a bug in the Visual C++ 7 optimizer when compiling this code */
#else
#define USE_DUFFS_LOOP
#endif

#ifdef USE_DUFFS_LOOP

/* 8-times unrolled loop */
#define DUFFS_LOOP8(pixel_copy_increment, width)			\
{ int n = (width + 7) / 8;						\
	switch (width & 7) {						\
	case 0: do {	pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 7:		pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 6:		pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 5:		pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 4:		pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 3:		pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 2:		pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 1:		pixel_copy_increment;				\
		} while ( --n > 0 );					\
	}								\
}

/* 4-times unrolled loop */
#define DUFFS_LOOP4(pixel_copy_increment, width)			\
{ int n = (width + 3) / 4;						\
	switch (width & 3) {						\
	case 0: do {	pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 3:		pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 2:		pixel_copy_increment;				\
                        fc__fallthrough;                                \
	case 1:		pixel_copy_increment;				\
		} while ( --n > 0 );					\
	}								\
}

/* 2 - times unrolled loop */
#define DUFFS_LOOP_DOUBLE2(pixel_copy_increment,			\
				double_pixel_copy_increment, width)	\
{ int n, w = width;							\
	if ( w & 1 ) {							\
	    pixel_copy_increment;					\
	    w--;							\
	}								\
	if ( w > 0 ) {							\
	    n = ( w + 2 ) / 4;						\
	    switch ( w & 2 ) {						\
	    case 0: do {	double_pixel_copy_increment;		\
                                fc__fallthrough;                        \
	    case 2:		double_pixel_copy_increment;		\
		    } while ( --n > 0 );				\
	    }								\
	}								\
}

/* 2 - times unrolled loop 4 pixels */
#define DUFFS_LOOP_QUATRO2(pixel_copy_increment,			\
				double_pixel_copy_increment,		\
				quatro_pixel_copy_increment, width)	\
{ int n, w = width;							\
        if (w & 1) {							\
	  pixel_copy_increment;						\
	  w--;								\
	}								\
	if (w & 2) {							\
	  double_pixel_copy_increment;					\
	  w -= 2;							\
	}								\
	if ( w > 0 ) {							\
	    n = ( w + 7 ) / 8;						\
	    switch ( w & 4 ) {						\
	    case 0: do {	quatro_pixel_copy_increment;		\
	    case 4:		quatro_pixel_copy_increment;		\
		    } while ( --n > 0 );				\
	    }								\
	}								\
}

/* Use the 8-times version of the loop by default */
#define DUFFS_LOOP(pixel_copy_increment, width)				\
	DUFFS_LOOP8(pixel_copy_increment, width)

#else

/* Don't use Duff's device to unroll loops */
#define DUFFS_LOOP_DOUBLE2(pixel_copy_increment,			\
			 double_pixel_copy_increment, width)		\
{ int n = width;							\
    if ( n & 1 ) {							\
	pixel_copy_increment;						\
	n--;								\
    }									\
    for (; n > 0; --n) {   						\
	double_pixel_copy_increment;					\
        n--;								\
    }									\
}

/* Don't use Duff's device to unroll loops */
#define DUFFS_LOOP_QUATRO2(pixel_copy_increment,			\
				double_pixel_copy_increment,		\
				quatro_pixel_copy_increment, width)	\
{ int n = width;							\
        if (n & 1) {							\
	  pixel_copy_increment;						\
	  n--;								\
	}								\
	if (n & 2) {							\
	  double_pixel_copy_increment;					\
	  n -= 2;							\
	}								\
	for (; n > 0; --n) {   						\
	  quatro_pixel_copy_increment;					\
	  n -= 3;							\
        }								\
}

/* Don't use Duff's device to unroll loops */
#define DUFFS_LOOP(pixel_copy_increment, width)				\
{ int n;								\
	for ( n=width; n > 0; --n ) {					\
		pixel_copy_increment;					\
	}								\
}

#define DUFFS_LOOP8(pixel_copy_increment, width)			\
	DUFFS_LOOP(pixel_copy_increment, width)
#define DUFFS_LOOP4(pixel_copy_increment, width)			\
	DUFFS_LOOP(pixel_copy_increment, width)

#endif /* USE_DUFFS_LOOP */

/* Shrink surface on 320x240 screen */
#ifdef GUI_SDL3_SMALL_SCREEN
#define DEFAULT_ZOOM 0.5
#define adj_surf(surf) zoomSurface((surf), DEFAULT_ZOOM, DEFAULT_ZOOM, 0)
#else
#define DEFAULT_ZOOM 1.0
/* Cannot return the original as callers free what we return */
#define adj_surf(surf) copy_surface(surf)
#endif

struct gui_layer;

struct sdl3_data {
  int rects_count;		/* Update rect. array counter */
  int guis_count;		/* Gui buffers array counter */
  SDL_Rect rects[RECT_LIMIT];	/* Update rect. list */
  SDL_Window *screen;           /* Main screen buffer */
  SDL_Surface *map;		/* Map buffer */
  SDL_Surface *dummy;           /* Dummy surface for missing sprites */
  SDL_Texture *maintext;
  SDL_Renderer *renderer;
  struct canvas map_canvas;
  struct gui_layer *gui;        /* Gui buffer */
  struct gui_layer **guis;      /* Gui buffers used by sdl3-client widgets window menager */
  SDL_Event event;		/* Main event struct */
};

extern struct sdl3_data main_data;

/* GUI layer */
/* A gui_layer is a surface with its own origin. Each widget belongs
 * to a gui_layer. gui_layers are stored in an array main_data.guis
 * (a "window manager"). */

struct gui_layer {
  SDL_Rect dest_rect;  /* origin: only x and y are used */
  SDL_Surface *surface;
};

struct gui_layer *gui_layer_new(int x, int y, SDL_Surface *surface);
void gui_layer_destroy(struct gui_layer **gui_layer);

struct gui_layer *get_gui_layer(SDL_Surface *surface);

struct gui_layer *add_gui_layer(int width, int height);
void remove_gui_layer(struct gui_layer *gui_layer);

void screen_rect_to_layer_rect(struct gui_layer *gui_layer, SDL_Rect *dest_rect);
void layer_rect_to_screen_rect(struct gui_layer *gui_layer, SDL_Rect *dest_rect);

/* ---------- */

int alphablit(SDL_Surface *src, SDL_Rect *srcrect,
              SDL_Surface *dst, SDL_Rect *dstrect,
              unsigned char alpha_mod);
int screen_blit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Rect *dstrect,
                unsigned char alpha_mod);

SDL_Surface *load_surf(const char *fname);

SDL_Surface *create_surf_with_format(SDL_PixelFormat pf,
                                     int width, int height);
SDL_Surface *create_surf(int width, int height);
SDL_Surface *convert_surf(SDL_Surface *surf_in);

SDL_Surface *create_filled_surface(Uint16 w, Uint16 h, SDL_Color *pcolor);

SDL_Surface *crop_rect_from_surface(SDL_Surface *psource,
                                    SDL_Rect *prect);

SDL_Surface *mask_surface(SDL_Surface *src, SDL_Surface *mask,
                          int mask_offset_x, int mask_offset_y);

SDL_Surface *copy_surface(SDL_Surface *src);

int blit_entire_src(SDL_Surface *psrc,
                    SDL_Surface *pdest, Sint16 dest_x, Sint16 dest_y);

Uint32 get_pixel(SDL_Surface *surf, int x, int y);
Uint32 get_first_pixel(SDL_Surface *surf);

void create_frame(SDL_Surface *dest, Sint16 left, Sint16 top,
                  Sint16 right, Sint16 bottom,
                  SDL_Color *pcolor);

void create_line(SDL_Surface *dest, Sint16 x0, Sint16 y0, Sint16 x1, Sint16 y1,
                 SDL_Color *pcolor);

/* SDL */
void init_sdl(int f);
void quit_sdl(void);
bool set_video_mode(unsigned width, unsigned height, unsigned flags);
bool create_surfaces(int width, int height);

void update_main_screen(void);

int main_window_width(void);
int main_window_height(void);

/* Rect */
bool correct_rect_region(SDL_Rect *prect);
bool is_in_rect_area(int x, int y, const SDL_Rect *rect);

int fill_rect_alpha(SDL_Surface *surf, SDL_Rect *prect,
                    SDL_Color *pcolor);

int clear_surface(SDL_Surface *surf, SDL_Rect *dstrect);

/* ================================================================= */

SDL_Surface *resize_surface(const SDL_Surface *psrc, Uint16 new_width,
                            Uint16 new_height, int smooth);

SDL_Surface *resize_surface_box(const SDL_Surface *psrc,
                                Uint16 new_width, Uint16 new_height,
                                int smooth, bool scale_up,
                                bool absolute_dimensions);

SDL_Surface *crop_visible_part_from_surface(SDL_Surface *psrc);
void get_smaller_surface_rect(SDL_Surface *surf, SDL_Rect *rect);

#define map_rgba_details(details, color) \
  SDL_MapRGBA(details, NULL, (color).r, (color).g, (color).b, (color).a)
#define map_rgba(format, color) \
  map_rgba_details(SDL_GetPixelFormatDetails(format), color)

#define crop_rect_from_screen(rect) \
  crop_rect_from_surface(main_data.screen, &rect)

/* Free surface with check and clear pointer */
#define FREESURFACE(ptr)                \
do {                                    \
  if (ptr) {                            \
    SDL_DestroySurface(ptr);            \
    ptr = NULL;                         \
  }                                     \
} while (FALSE)

/*
 * Lock surface
 */
#define lock_surf(surf)         \
do {                            \
  if (SDL_MUSTLOCK(surf)) {     \
    SDL_LockSurface(surf);      \
  }                             \
} while (FALSE)


/*
 * Unlock surface
 */
#define unlock_surf(surf)               \
do {                                    \
  if (SDL_MUSTLOCK(surf)) {             \
    SDL_UnlockSurface(surf);            \
  }                                     \
} while (FALSE)

/*
 *  Lock screen surface
 */
#define lock_screen() lock_surf(main_data.screen)

/*
 *  Unlock screen surface
 */
#define unlock_screen() unlock_surf(main_data.screen)

#define putpixel(surf, x, y, pixel)					  \
do {									  \
    Uint8 *buf_ptr = ((Uint8 *)surf->pixels + (y * surf->pitch)); \
    switch (surf->format->BytesPerPixel) {				  \
		case 1:							  \
			buf_ptr += x;					  \
			*(Uint8 *)buf_ptr = pixel;			  \
		break;							  \
		case 2:							  \
			buf_ptr += x << 1;				  \
			*((Uint16 *)buf_ptr) = pixel;			  \
		break;							  \
		case 3:							  \
			buf_ptr += (x << 1) + x;			  \
			if (is_bigendian()) {                           \
				buf_ptr[0] = (pixel >> 16) & 0xff;	\
				buf_ptr[1] = (pixel >> 8) & 0xff;	\
				buf_ptr[2] = pixel & 0xff;		\
			} else {					\
				buf_ptr[0] = pixel & 0xff;		\
				buf_ptr[1] = (pixel >> 8) & 0xff;	\
				buf_ptr[2] = (pixel >> 16) & 0xff;	\
			}						\
		break;							\
		case 4:							\
			buf_ptr += x << 2;				\
			*((Uint32 *)buf_ptr) = pixel;			\
		break;							\
    }									\
} while (FALSE)

/* Blend the RGB values of two pixels based on a source alpha value */
#define ALPHA_BLEND(sR, sG, sB, A, dR, dG, dB)	\
do {						\
	dR = (((sR-dR)*(A))>>8)+dR;		\
	dG = (((sG-dG)*(A))>>8)+dG;		\
	dB = (((sB-dB)*(A))>>8)+dB;		\
} while (FALSE)

#define ALPHA_BLEND128(sR, sG, sB, dR, dG, dB)	\
do {						\
  Uint32 __Src = (sR << 16 | sG << 8 | sB);	\
  Uint32 __Dst = (dR << 16 | dG << 8 | dB);	\
  __Dst = ((((__Src & 0x00fefefe) + (__Dst & 0x00fefefe)) >> 1)		\
			       + (__Src & __Dst & 0x00010101)) | 0xFF000000; \
  dR = (__Dst >> 16) & 0xFF;			\
  dG = (__Dst >> 8 ) & 0xFF;			\
  dB = (__Dst      ) & 0xFF;			\
} while (FALSE)

#endif /* FC__GRAPHICS_H */
