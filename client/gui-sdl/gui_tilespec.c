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
                          gui_tilespec.c  -  description
                             -------------------
    begin                : Dec. 2 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafal Bursig <bursig@poczta.fm>
 **********************************************************************/
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "hash.h"
#include "support.h"

#include "tilespec.h"

#include "gui_mem.h"
#include "graphics.h"
#include "gui_zoom.h"

#include "gui_tilespec.h"

extern char *pDataPath;
extern SDL_Surface *pDitherMask;
/*******************************************************************************
 * reload small citizens "style" icons.
 *******************************************************************************/
static void reload_small_citizens_icons( int style , struct city *pCity )
{

  /* free info icons */
  FREESURFACE( pIcons->pMale_Content );
  FREESURFACE( pIcons->pFemale_Content );
  FREESURFACE( pIcons->pMale_Happy );
  FREESURFACE( pIcons->pFemale_Happy );
  FREESURFACE( pIcons->pMale_Unhappy );
  FREESURFACE( pIcons->pFemale_Unhappy );
  FREESURFACE( pIcons->pMale_Angry );
  FREESURFACE( pIcons->pFemale_Angry );
	
  FREESURFACE( pIcons->pSpec_Lux ); /* Elvis */
  FREESURFACE( pIcons->pSpec_Tax ); /* TaxMan */
  FREESURFACE( pIcons->pSpec_Sci ); /* Scientist */

  /* allocate icons */
  pIcons->pMale_Happy = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_HAPPY, 0, pCity ),
	                                                    15 , 26, 1 );
  SDL_SetColorKey( pIcons->pMale_Happy , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pMale_Happy , 0 ,0 ) );
    
  pIcons->pFemale_Happy = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_HAPPY, 1, pCity ) ,
                                                            15 , 26, 1 );
  SDL_SetColorKey( pIcons->pFemale_Happy , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pFemale_Happy , 0 ,0 ) );
    
  pIcons->pMale_Content = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_CONTENT, 0, pCity ) ,
                                                            15 , 26, 1 );
  SDL_SetColorKey( pIcons->pMale_Content , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pMale_Content , 0 ,0 ) );
    
  pIcons->pFemale_Content = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_CONTENT, 1, pCity ),
                                                             15 , 26, 1 );
  SDL_SetColorKey( pIcons->pFemale_Content , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pFemale_Content , 0 ,0 ) );
    
  pIcons->pMale_Unhappy = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_UNHAPPY, 0, pCity ),
                                                           15 , 26, 1 );
  SDL_SetColorKey( pIcons->pMale_Unhappy , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pMale_Unhappy , 0 ,0 ) );
    
  pIcons->pFemale_Unhappy = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_UNHAPPY, 1, pCity ),
                                                          15 , 26, 1 );
  SDL_SetColorKey( pIcons->pFemale_Unhappy , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pFemale_Unhappy , 0 ,0 ) );
    
  pIcons->pMale_Angry = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_ANGRY, 0, pCity ),
                                                           15 , 26, 1 );
  SDL_SetColorKey( pIcons->pMale_Angry , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pMale_Angry , 0 ,0 ) );
    
  pIcons->pFemale_Angry = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_ANGRY, 1, pCity ),
                                                           15 , 26, 1 );
  SDL_SetColorKey( pIcons->pFemale_Angry , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pFemale_Angry , 0 ,0 ) );
    
  pIcons->pSpec_Lux = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_ELVIS, 0, pCity ) ,
                                                            15 , 26, 1 );
  SDL_SetColorKey( pIcons->pSpec_Lux , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pSpec_Lux , 0 ,0 ) );
    
  pIcons->pSpec_Tax = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_TAXMAN, 0, pCity )
                                                          , 15 , 26, 1 );
  SDL_SetColorKey( pIcons->pSpec_Tax , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pSpec_Tax , 0 ,0 ) );
    
  pIcons->pSpec_Sci = ResizeSurface( 
    (SDL_Surface *)get_citizen_sprite( CITIZEN_SCIENTIST, 0, pCity ) ,
                                                            15 , 26, 1 );
  SDL_SetColorKey( pIcons->pSpec_Sci , SDL_SRCCOLORKEY|SDL_RLEACCEL , 
			    getpixel( pIcons->pSpec_Sci , 0 ,0 ) );
    
}
/* ================================================================================= */
/* ===================================== Public ==================================== */
/* ================================================================================= */

/**********************************************************************
  Set city citizens icons sprite value; should only happen after
  start of game (city style struct was filled ).
***********************************************************************/
void reload_citizens_icons( int style , struct city *pCity )
{
  reload_small_citizens_icons( style , pCity);
  pIcons->style = style;
}

/**********************************************************************
  Set city icons sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_city_icons(void)
{
  char cBuf[80]; /* I hope this is enought :) */
  SDL_Rect crop_rect = { 0, 0, 14, 14 };
  SDL_Surface *pBuf = NULL;

  pIcons = ( struct City_Icon *)MALLOC( sizeof( struct City_Icon ));
	  
  my_snprintf(cBuf, sizeof(cBuf) ,"%s%s", pDataPath, "theme/default/city_icons.png");

  pBuf = load_surf(cBuf);
  assert( pBuf );
    
  crop_rect.x = 1;
  crop_rect.y = 1;

  pIcons->pBIG_Food_Corr = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Food_Corr,
		  SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Food_Corr, 13, 0));

  crop_rect.x += 15;
  pIcons->pBIG_Shield_Corr = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Shield_Corr,
		  SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Shield_Corr, 0, 0));

  crop_rect.x += 15;
  pIcons->pBIG_Trade_Corr = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Trade_Corr,
		  SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Trade_Corr, 0, 0));

  crop_rect.x = 1;
  crop_rect.y += 15;
  pIcons->pBIG_Food = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Food, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Food, 13, 0));

  crop_rect.x += 15;
  pIcons->pBIG_Shield = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Shield, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Shield, 0, 0));

  crop_rect.x += 15;
  pIcons->pBIG_Trade = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Trade, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Trade, 0, 0));
  crop_rect.x = 1;
  crop_rect.y += 15;
  pIcons->pBIG_Luxury = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Luxury, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Luxury, 0, 0));
  crop_rect.x += 15;
  pIcons->pBIG_Coin = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Coin, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Coin, 0, 0));
  crop_rect.x += 15;
  pIcons->pBIG_Colb = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Colb, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Colb, 0, 0));

  crop_rect.x = 1 + 15;
  crop_rect.y += 15;
  pIcons->pBIG_Coin_Corr = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Coin_Corr,
		  SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Coin_Corr, 0, 0));

  crop_rect.x += 15;
  pIcons->pBIG_Coin_UpKeep = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pBIG_Coin_UpKeep,
		  SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pBIG_Coin_UpKeep, 0, 0));
		  
  /* small icon */
  crop_rect.x = 1;
  crop_rect.y = 72;
  crop_rect.w = 10;
  crop_rect.h = 10;
  pIcons->pFood = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pFood, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pFood, 0, 0));

  crop_rect.x += 11;
  pIcons->pShield = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pShield, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pShield, 0, 0));

  crop_rect.x += 11;
  pIcons->pTrade = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pTrade, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pTrade, 0, 0));

  crop_rect.x += 11;
  pIcons->pFace = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pFace, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pFace, 0, 0));

  crop_rect.x += 1;
  crop_rect.y += 11;
  pIcons->pLuxury = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pLuxury, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pLuxury, 0, 0));
  crop_rect.x += 11;
  pIcons->pCoin = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pCoin, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pCoin, 0, 0));
  crop_rect.x += 11;
  pIcons->pColb = crop_rect_from_surface(pBuf, &crop_rect);
  SDL_SetColorKey(pIcons->pColb, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pColb, 0, 0));

  FREESURFACE(pBuf);
  /* ================================================================= */
  my_snprintf(cBuf, sizeof(cBuf) ,"%s%s", pDataPath, "theme/default/city_pollution.png");

  pIcons->pPollution = load_surf(cBuf);
  assert( pIcons->pPollution );
  
  SDL_SetColorKey(pIcons->pPollution, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pPollution, 0, 0));
  /* ================================================================= */
  my_snprintf(cBuf, sizeof(cBuf) ,"%s%s", pDataPath, "theme/default/city_fist.png");

  pIcons->pPolice = load_surf(cBuf);

  assert( pIcons->pPolice );
  
  SDL_SetColorKey(pIcons->pPolice, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pIcons->pPolice, 1, 0));

  /* ================================================================= */

  /* force reload small citizens */
  pIcons->style = 999;
}
/* =================================================== */
/* ===================== THEME ======================= */
/* =================================================== */

/*
 *	Alloc and fill Theme struct
 */
void tilespec_setup_theme(void)
{
  int i;
  char buf[80];	/* I hope this is enought :) */

  pTheme = MALLOC(sizeof(struct Theme));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/menu_text_4button.png");
  pTheme->Button = load_surf(buf);
  /* SDL_SetColorKey( pTheme->Button , COLORKEYFLAG , 0x0 ); */

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/edit_theme.png");
  pTheme->Edit = load_surf(buf);
  /* SDL_SetColorKey( pTheme->Button , COLORKEYFLAG , 0x0 ); */

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/OK_buttons.png");
  pTheme->OK_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OK_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0 );


  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/FAIL_buttons.png");
  pTheme->CANCEL_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->CANCEL_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/bigOK_buttons.png");
  pTheme->FORWARD_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->FORWARD_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/bigFail_buttons.png");
  pTheme->BACK_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->BACK_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/scrollUP_buttons.png");
  pTheme->UP_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->UP_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/scrollDOWN_buttons.png");
  pTheme->DOWN_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->DOWN_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);
  
#if 0
  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/scrollLEFT_buttons.png");
  pTheme->LEFT_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->LEFT_Icon, COLORKEYFLAG, 0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/scrollRIGHT_buttons.png");
  pTheme->RIGHT_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->RIGHT_Icon, COLORKEYFLAG, 0x0);
#endif

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/vertic_button.png");
  pTheme->Vertic = load_surf(buf);
  SDL_SetColorKey(pTheme->Vertic, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/horiz_button.png");
  pTheme->Horiz = load_surf(buf);
  SDL_SetColorKey(pTheme->Horiz, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/sellected_buttons.png");
  pTheme->CBOX_Sell_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->CBOX_Sell_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->CBOX_Sell_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/unsellected_buttons.png");
  pTheme->CBOX_Unsell_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->CBOX_Unsell_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->CBOX_Unsell_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/fr_vert.png");
  pTheme->FR_Vert = load_surf(buf);
  /* SDL_SetColorKey( pTheme->FR_Vert , COLORKEYFLAG , 0x0 ); */

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/fr_hori.png");
  pTheme->FR_Hor = load_surf(buf);
  /* SDL_SetColorKey( pTheme->FR_Hor , COLORKEYFLAG , 0x0 ); */

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/menu_text_3vbutton.png");
  pTheme->Block = load_surf(buf);
  /* SDL_SetColorKey( pTheme->FR_Hor , COLORKEYFLAG , 0x0 ); */

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/RArrow_buttons.png");
  pTheme->R_ARROW_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->R_ARROW_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/LArrow_buttons.png");
  pTheme->L_ARROW_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->L_ARROW_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  0x0);


  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/M_buttons.png");
  pTheme->META_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->META_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->META_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/options_buttons.png");
  pTheme->Options_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->Options_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/unit_info_buttons.png");
  pTheme->UNITS_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->UNITS_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/map_buttons.png");
  pTheme->MAP_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->MAP_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/log_buttons.png");
  pTheme->LOG_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->LOG_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/find_city_buttons.png");
  pTheme->FindCity_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->FindCity_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->FindCity_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/new_turn_buttons.png");
  pTheme->NEW_TURN_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->NEW_TURN_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->NEW_TURN_Icon, 0, 0));

  /* ================================================================== */
  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/I_buttons.png");
  pTheme->INFO_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->INFO_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->INFO_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/happy_buttons.png");
  pTheme->Happy_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->Happy_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->Happy_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/army_buttons.png");
  pTheme->Army_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->Army_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->Army_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/supported_buttons.png");
  pTheme->Support_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->Support_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->Support_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/prod_buttons.png");
  pTheme->PROD_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->PROD_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->PROD_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/q_prod_buttons.png");
  pTheme->QPROD_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->QPROD_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->QPROD_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/buy_buttons.png");
  pTheme->Buy_PROD_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->Buy_PROD_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->Buy_PROD_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/cma_buttons.png");
  pTheme->CMA_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->CMA_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->CMA_Icon, 0, 0));

  /* ================================================================== */

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order2_buttons.png");
  pTheme->Order_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->Order_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->Order_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_disband2_buttons.png");
  pTheme->ODisband_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->ODisband_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->ODisband_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_wait_buttons.png");
  pTheme->OWait_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OWait_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OWait_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_done_buttons.png");
  pTheme->ODone_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->ODone_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->ODone_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_auto_att_buttons.png");
  pTheme->OAutoAtt_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OAutoAtt_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OAutoAtt_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_auto_sett_buttons.png");
  pTheme->OAutoSett_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OAutoSett_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OAutoSett_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_auto_cont_buttons.png");
  pTheme->OAutoConet_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OAutoConet_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OAutoConet_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_auto_exp_buttons.png");
  pTheme->OAutoExp_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OAutoExp_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OAutoExp_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_unload_buttons.png");
  pTheme->OUnload_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OUnload_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OUnload_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_buildcity_buttons.png");
  pTheme->OBuildCity_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OBuildCity_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OBuildCity_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_gotocity_buttons.png");
  pTheme->OGotoCity_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OGotoCity_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OGotoCity_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_home_buttons.png");
  pTheme->OHomeCity_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OHomeCity_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OHomeCity_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_goto_buttons.png");
  pTheme->OGoto_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OGoto_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OGoto_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_patrol_buttons.png");
  pTheme->OPatrol_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OPatrol_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OPatrol_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_mine_buttons.png");
  pTheme->OMine_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OMine_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OMine_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_forest_buttons.png");
  pTheme->OForest_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OForest_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OForest_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_crop_buttons.png");
  pTheme->OCropForest_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OCropForest_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OCropForest_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_fortify_buttons.png");
  pTheme->OFortify_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OFortify_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OFortify_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_sentry_buttons.png");
  pTheme->OSentry_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OSentry_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OSentry_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_irigation_buttons.png");
  pTheme->OIrigation_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->OIrigation_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->OIrigation_Icon, 0, 0));

  my_snprintf(buf, sizeof(buf), "%s%s", pDataPath,
	      "theme/default/order_road_buttons.png");
  pTheme->ORoad_Icon = load_surf(buf);
  SDL_SetColorKey(pTheme->ORoad_Icon, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  getpixel(pTheme->ORoad_Icon, 0, 0));


  /* ================================================== */
  /* Map Dithering */
  
  my_snprintf(buf,sizeof(buf), "%s/theme/default/dither_mask.png", pDataPath );	  
  pDitherMask = load_surf(buf);
  assert(pDitherMask);	  
  
  return;
}

/*
 *	Free memmory
 */
void tilespec_unload_theme(void)
{
    FREESURFACE( pTheme->Button);
    FREESURFACE( pTheme->Edit);
    FREESURFACE( pTheme->OK_Icon);
    FREESURFACE( pTheme->CANCEL_Icon);
    FREESURFACE( pTheme->FORWARD_Icon);
    FREESURFACE( pTheme->BACK_Icon);
    FREESURFACE( pTheme->META_Icon);
    FREESURFACE( pTheme->UP_Icon );
    FREESURFACE( pTheme->DOWN_Icon );
    FREESURFACE( pTheme->Vertic );
    FREESURFACE( pTheme->Options_Icon );
    FREESURFACE( pTheme->FR_Vert );
    FREESURFACE( pTheme->FR_Hor );

	/* TO DO ADD Rest */
	
    FREE(pTheme);
    return;
}
