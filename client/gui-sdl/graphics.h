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

/**********************************************************************
                          graphics.h  -  description
                             -------------------
    begin                : Mon Jul 1 2002
    copyright            : (C) 2000 by Michael Speck
			 : (C) 2002 by Rafał Bursig
    email                : Michael Speck <kulkanie@gmx.net>
			 : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__GRAPHICS_H
#define FC__GRAPHICS_H

#include "graphics_g.h"

#define	RECT_LIMIT	80
/* #define	HAVE_MMX1 */

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
{ int n = (width+7)/8;							\
	switch (width & 7) {						\
	case 0: do {	pixel_copy_increment;				\
	case 7:		pixel_copy_increment;				\
	case 6:		pixel_copy_increment;				\
	case 5:		pixel_copy_increment;				\
	case 4:		pixel_copy_increment;				\
	case 3:		pixel_copy_increment;				\
	case 2:		pixel_copy_increment;				\
	case 1:		pixel_copy_increment;				\
		} while ( --n > 0 );					\
	}								\
}

/* 4-times unrolled loop */
#define DUFFS_LOOP4(pixel_copy_increment, width)			\
{ int n = (width+3)/4;							\
	switch (width & 3) {						\
	case 0: do {	pixel_copy_increment;				\
	case 3:		pixel_copy_increment;				\
	case 2:		pixel_copy_increment;				\
	case 1:		pixel_copy_increment;				\
		} while ( --n > 0 );					\
	}								\
}

/* 2 - times unrolled loop */
#define DUFFS_LOOP_DOUBLE2(pixel_copy_increment,			\
				double_pixel_copy_increment, width)	\
{ int n, w = width;							\
	if( w & 1 ) {							\
	    pixel_copy_increment;					\
	    w--;							\
	}								\
	if ( w > 0 ) {							\
	    n = ( w + 2 ) / 4;						\
	    switch( w & 2 ) {						\
	    case 0: do {	double_pixel_copy_increment;		\
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
        if(w & 1) {							\
	  pixel_copy_increment;						\
	  w--;								\
	}								\
	if(w & 2) {							\
	  double_pixel_copy_increment;					\
	  w -= 2;							\
	}								\
	if ( w > 0 ) {							\
	    n = ( w + 7 ) / 8;						\
	    switch( w & 4 ) {						\
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
    if( n & 1 ) {							\
	pixel_copy_increment;						\
	n--;								\
    }									\
    for(; n > 0; --n) {   						\
	double_pixel_copy_increment;					\
        n--;								\
    }									\
}

/* Don't use Duff's device to unroll loops */
#define DUFFS_LOOP_QUATRO2(pixel_copy_increment,			\
				double_pixel_copy_increment,		\
				quatro_pixel_copy_increment, width)	\
{ int n = width;							\
        if(n & 1) {							\
	  pixel_copy_increment;						\
	  n--;								\
	}								\
	if(n & 2) {							\
	  double_pixel_copy_increment;					\
	  n -= 2;							\
	}								\
	for(; n > 0; --n) {   						\
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

struct Sprite {
  struct SDL_Surface *psurface;
};

#define GET_SURF(m_sprite)	(m_sprite->psurface)

struct canvas {
  int rects_count;		/* update rect. array counter */
  int guis_count;		/* gui buffers array counter */
  SDL_Rect rects[RECT_LIMIT];	/* update rect. list */
  SDL_Surface *screen;		/* main screen buffer */
  SDL_Surface *map;		/* map buffer */
  SDL_Surface *text;		/* city descriptions buffer */
  SDL_Surface *gui;		/* gui buffer */
  SDL_Surface **guis;		/* gui buffers used by sdlclient widgets window menager */
  SDL_Event event;		/* main event struct */
};

SDL_Surface *load_surf(const char *pFname);
SDL_Surface *load_surf_with_flags(const char *pFname, int iFlags);

SDL_Surface *create_surf_with_format(SDL_PixelFormat *pSpf,
				     int w, int h, Uint32 f);

SDL_Surface *create_filled_surface(Uint16 w, Uint16 h, Uint32 iFlags,
				   SDL_Color *pColor);

SDL_Surface *crop_rect_from_surface(SDL_Surface *pSource,
				    SDL_Rect *pRect);

int blit_entire_src(SDL_Surface *pSrc,
		    SDL_Surface *pDest, Sint16 iDest_x, Sint16 iDest_y);

int center_main_window_on_screen(void);

Uint32 getpixel(SDL_Surface *pSurface, Sint16 x, Sint16 y);
Uint32 get_first_pixel(SDL_Surface *pSurface);

void * my_memset8 (void *dst_mem, Uint8  var, size_t lenght);
void * my_memset16(void *dst_mem, Uint16 var, size_t lenght);
void * my_memset24(void *dst_mem, Uint32 var, size_t lenght);
void * my_memset32(void *dst_mem, Uint32 var, size_t lenght);

void putline(SDL_Surface *pDest, Sint16 x0, Sint16 y0, Sint16 x1,
	     Sint16 y1, Uint32 color);
void putframe(SDL_Surface *pDest, Sint16 x0, Sint16 y0, Sint16 x1,
	      Sint16 y1, Uint32 color);

/* SDL */
void init_sdl(int f);
void quit_sdl(void);
int set_video_mode(int iWidth, int iHeight, int iFlags);

/* Rect */
bool correct_rect_region(SDL_Rect *pRect);
bool is_in_rect_area(int x, int y, SDL_Rect rect);

int SDL_FillRectAlpha(SDL_Surface *pSurface, SDL_Rect *pRect,
		      SDL_Color *pColor);

/* ================================================================= */
extern SDL_Cursor *pStd_Cursor;
extern SDL_Cursor *pGoto_Cursor;
extern SDL_Cursor *pDrop_Cursor;
extern SDL_Cursor *pNuke_Cursor;
extern SDL_Cursor *pPatrol_Cursor;

void unload_cursors(void);

SDL_Surface *get_logo_gfx(void);
SDL_Surface *get_intro_gfx(void);
SDL_Surface *get_city_gfx(void);
void draw_intro_gfx(void);

SDL_Surface *make_flag_surface_smaler(SDL_Surface *pSrc);
SDL_Rect get_smaller_surface_rect(SDL_Surface *pSrc);

#define create_surf(w, h, f) \
	create_surf_with_format(Main.screen->format , w , h, f)

#define crop_rect_from_screen(rect) \
		crop_rect_from_surface(Main.screen, &rect)

/* free surface with check and clear pointer */
#define FREESURFACE(ptr)		\
do {					\
  if (ptr) {				\
    SDL_FreeSurface(ptr);		\
    ptr = NULL;				\
  }					\
} while(0)

/*
 *  lock surface
 */
#define lock_surf(pSurf)	\
do {				\
  if (SDL_MUSTLOCK(pSurf)) {	\
    SDL_LockSurface(pSurf);	\
  }				\
} while(0)


/*
 *   unlock surface
 */
#define unlock_surf(pSurf)		\
do {					\
    if (SDL_MUSTLOCK(pSurf)) {		\
	SDL_UnlockSurface(pSurf);	\
    }                                   \
} while(0)

/*
 *	lock screen surface
 */
#define lock_screen()	lock_surf(Main.screen)

/*
 *	unlock screen surface
 */
#define unlock_screen()	unlock_surf(Main.screen)

#define putpixel(pSurface, x, y, pixel)					  \
do {									  \
    Uint8 *buf_ptr = ((Uint8 *)pSurface->pixels + (y * pSurface->pitch)); \
    switch(pSurface->format->BytesPerPixel) {				  \
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
			if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {		  \
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
} while(0)

#endif				/* FC__GRAPHICS_H */
