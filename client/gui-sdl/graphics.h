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
			 : (C) 2002 by Rafa³ Bursig
    email                : Michael Speck <kulkanie@gmx.net>
			 : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__GRAPHICS_H
#define FC__GRAPHICS_H

#include "graphics_g.h"

#define	RECT_LIMIT	200

struct Sprite {
  struct SDL_Surface;
};

struct RectList {
  struct RectList *prev;
  SDL_Rect area;
};

struct Sdl {
  int rects_count;		/* update rect. list counter */
  struct RectList *rects;	/* update rect. list */
  SDL_Surface *screen;		/* main screen surface */
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

Uint32 getpixel(SDL_Surface *pSurface, Sint16 x, Sint16 y);
void putline(SDL_Surface *pDest, Sint16 x0, Sint16 y0, Sint16 x1,
	     Sint16 y1, Uint32 color);
void putframe(SDL_Surface *pDest, Sint16 x0, Sint16 y0, Sint16 x1,
	      Sint16 y1, Uint32 color);

/* SDL */
void init_sdl(int f);
void quit_sdl(void);
int set_video_mode(int iWidth, int iHeight, int iFlags);
Uint16 **get_list_modes(Uint32 flags);

/* Rect */
void refresh_screen(Sint16 x, Sint16 y, Uint16 w, Uint16 h);
void refresh_rect(SDL_Rect Rect);
void refresh_fullscreen(void);

void refresh_rects(void);
void add_refresh_region(Sint16 x, Sint16 y, Uint16 w, Uint16 h);
void add_refresh_rect(SDL_Rect Rect);
int correct_rect_region(SDL_Rect *pRect);
int detect_rect_colisions(SDL_Rect *pMaster, SDL_Rect *pSlave);
int cmp_rect(SDL_Rect *pMaster, SDL_Rect *pSlave);


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

#define create_surf( w, h, f) \
	create_surf_with_format(Main.screen->format , w , h, f)

#define crop_rect_from_screen(rect) \
		crop_rect_from_surface(Main.screen, &rect)

/* free surface with check and clear pointer */
#define FREESURFACE( ptr )			\
do {						\
	if (ptr) {				\
		SDL_FreeSurface( ptr );		\
		ptr = NULL;			\
	}					\
} while(0)

/*
 *  lock surface
 */
#define lock_surf( pSurf )	\
do {				\
    if (SDL_MUSTLOCK(pSurf)) {	\
        SDL_LockSurface(pSurf);	\
    }                           \
} while(0)


/*
 *   unlock surface
 */
#define unlock_surf( pSurf )		\
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

#define putpixel( pSurface, x, y, pixel )				  \
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
