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
                          colors.h  -  description
                             -------------------
    begin                : Mon Jul 15 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__COLORS_H
#define FC__COLORS_H

#include "colors_g.h"

enum SDLClient_std_color {
  COLOR_STD_RACE14 = COLOR_STD_LAST,
  COLOR_STD_RACE15,
  COLOR_STD_RACE16,
  COLOR_STD_RACE17,
  COLOR_STD_RACE18,
  COLOR_STD_RACE19,
  COLOR_STD_RACE20,
  COLOR_STD_RACE21,
  COLOR_STD_RACE22,
  COLOR_STD_RACE23,
  COLOR_STD_RACE24,
  COLOR_STD_RACE25,
  COLOR_STD_RACE26,
  COLOR_STD_RACE27,
  COLOR_STD_RACE28,
  COLOR_STD_RACE29,
  COLOR_STD_RACE30,
  COLOR_STD_RACE31,
  COLOR_STD_GROUND_FOGED,
  COLOR_STD_BACKGROUND_BROWN,	/* Background2 (brown) */
  COLOR_STD_GRID,		/* Grid line color */
  COLOR_STD_QUICK_INFO,		/* Quick info Background color */
  COLOR_STD_FOG_OF_WAR,		/* FOG OF WAR color */
  COLOR_STD_DISABLED,		/* disable color */
  COLOR_STD_CITY_PROD,		/* city production color */
  COLOR_STD_CITY_SUPPORT,	/* city units support color */
  COLOR_STD_CITY_TRADE,		/* city trade color */
  COLOR_STD_CITY_GOLD,		/* city gold color */
  COLOR_STD_CITY_LUX,		/* city luxuries color */
  COLOR_STD_CITY_FOOD_SURPLUS,	/* city food surplus color */
  COLOR_STD_CITY_UNKEEP,	/* city unkeep color */
  COLOR_STD_CITY_SCIENCE,	/* city science color */
  COLOR_STD_CITY_HAPPY,		/* city happy color */
  COLOR_STD_CITY_CELEB,		/* city celebrating color */
  COLOR_STD_RED_DISABLED,	/* player at war but can't meet or get intel. data */
  SDLCLIENT_STD_COLOR_LAST
};

SDL_Color * get_game_colorRGB(Uint32 color_offset);
Uint32 get_game_color(Uint32 color_offset, SDL_Surface *pDest);

/* Blend the RGB values of two pixels based on a source alpha value */
#define ALPHA_BLEND(sR, sG, sB, A, dR, dG, dB)	\
do {						\
	dR = (((sR-dR)*(A))>>8)+dR;		\
	dG = (((sG-dG)*(A))>>8)+dG;		\
	dB = (((sB-dB)*(A))>>8)+dB;		\
} while(0)

#define ALPHA_BLEND128(sR, sG, sB, dR, dG, dB)	\
do {						\
  Uint32 __Src = (sR << 16 | sG << 8 | sB);	\
  Uint32 __Dst = (dR << 16 | dG << 8 | dB);	\
  __Dst = ((((__Src & 0x00fefefe) + (__Dst & 0x00fefefe)) >> 1)		\
			       + (__Src & __Dst & 0x00010101)) | 0xFF000000; \
  dR = (__Dst >> 16) & 0xFF;			\
  dG = (__Dst >> 8 ) & 0xFF;			\
  dB = (__Dst      ) & 0xFF;			\
} while(0)

#endif	/* FC__COLORS_H */
