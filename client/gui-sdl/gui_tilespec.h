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
                          gui_tilespec.h  -  description
                             -------------------
    begin                : Dec. 2 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafal Bursig <bursig@poczta.fm>
 **********************************************************************/
 
#ifndef FC__GUI_TILESPEC_H
#define FC__GUI_TILESPEC_H

#include "citydlg_common.h"	/* enum citizen_type */
  
struct Theme {
	SDL_Surface *Button;
	SDL_Surface *Edit;
	SDL_Surface *CBOX_Sell_Icon;
	SDL_Surface *CBOX_Unsell_Icon;
	SDL_Surface *OK_Icon;
	SDL_Surface *CANCEL_Icon;
	SDL_Surface *FORWARD_Icon;
	SDL_Surface *BACK_Icon;
	SDL_Surface *META_Icon;
	SDL_Surface *INFO_Icon;
	SDL_Surface *UP_Icon;
	SDL_Surface *DOWN_Icon;
	SDL_Surface *LEFT_Icon;
	SDL_Surface *RIGHT_Icon;
	SDL_Surface *Vertic;
	SDL_Surface *Horiz;
	SDL_Surface *FR_Vert;
	SDL_Surface *FR_Hor;
	SDL_Surface *R_ARROW_Icon;
	SDL_Surface *L_ARROW_Icon;
	SDL_Surface *LOCK_Icon;
	SDL_Surface *UNLOCK_Icon;
	
	SDL_Surface *Options_Icon;
	SDL_Surface *Block;
	SDL_Surface *UNITS_Icon;
	SDL_Surface *MAP_Icon;
	SDL_Surface *LOG_Icon;
	SDL_Surface *PLAYERS_Icon;
	SDL_Surface *UNITS2_Icon;
	SDL_Surface *FindCity_Icon;
	SDL_Surface *NEW_TURN_Icon;
	SDL_Surface *SAVE_Icon;
	SDL_Surface *LOAD_Icon;
	SDL_Surface *DELETE_Icon;
	
	/* city icons */
	SDL_Surface *Army_Icon;
	SDL_Surface *Support_Icon;
	SDL_Surface *Happy_Icon;
	SDL_Surface *CMA_Icon;
	SDL_Surface *PROD_Icon;
	SDL_Surface *QPROD_Icon;
	SDL_Surface *Buy_PROD_Icon;

        /* diplomacy */
        SDL_Surface *OK_PACT_Icon;
	SDL_Surface *CANCEL_PACT_Icon;
	
	/* orders icons */
	SDL_Surface *Order_Icon;
	SDL_Surface *ODisband_Icon;
	SDL_Surface *OWait_Icon;
	SDL_Surface *ODone_Icon;
	SDL_Surface *OAutoAtt_Icon;
	SDL_Surface *OAutoExp_Icon;
	SDL_Surface *OAutoSett_Icon;
	SDL_Surface *OAutoConnect_Icon;
	SDL_Surface *OUnload_Icon;
	SDL_Surface *OBuildCity_Icon;
	SDL_Surface *OGotoCity_Icon;
	SDL_Surface *OGoto_Icon;
	SDL_Surface *OHomeCity_Icon;
	SDL_Surface *OPatrol_Icon;
	SDL_Surface *OMine_Icon;
	SDL_Surface *OPlantForest_Icon;
	SDL_Surface *OCutDownForest_Icon;
	SDL_Surface *OFortify_Icon;
	SDL_Surface *OSentry_Icon;
	SDL_Surface *OIrrigation_Icon;
	SDL_Surface *ORoad_Icon;
	SDL_Surface *ORailRoad_Icon;
	SDL_Surface *OPillage_Icon;
	SDL_Surface *OParaDrop_Icon;
	SDL_Surface *ONuke_Icon;
	SDL_Surface *OFortress_Icon;
	SDL_Surface *OFallout_Icon;
	SDL_Surface *OPollution_Icon;
	SDL_Surface *OAirBase_Icon;
	SDL_Surface *OTransform_Icon;
	SDL_Surface *OAddCity_Icon;
	SDL_Surface *OWonder_Icon;
	SDL_Surface *OTrade_Icon;
	SDL_Surface *OSpy_Icon;
	SDL_Surface *OWakeUp_Icon;
			
} *pTheme;

void tilespec_setup_theme(void);
void tilespec_unload_theme(void);

struct Animation {
  SDL_Surface *Focus[4];
} *pAnim;

void tilespec_setup_anim(void);
void tilespec_free_anim(void);

struct City_Icon {
  int style;
  SDL_Surface *pBIG_Food_Corr;
  SDL_Surface *pBIG_Shield_Corr;
  SDL_Surface *pBIG_Trade_Corr;
  SDL_Surface *pBIG_Food;
  SDL_Surface *pBIG_Shield;
  SDL_Surface *pBIG_Trade;
  SDL_Surface *pBIG_Luxury;
  SDL_Surface *pBIG_Coin;
  SDL_Surface *pBIG_Colb;
  /*SDL_Surface *pBIG_Face;*/
  SDL_Surface *pBIG_Coin_Corr;
  SDL_Surface *pBIG_Coin_UpKeep;
  SDL_Surface *pBIG_Shield_Surplus;
  SDL_Surface *pBIG_Food_Surplus;
  
  SDL_Surface *pFood;
  SDL_Surface *pShield;
  SDL_Surface *pTrade;
  SDL_Surface *pLuxury;
  SDL_Surface *pCoin;
  SDL_Surface *pColb;
  SDL_Surface *pFace;
  /*SDL_Surface *pDark_Face;*/
	
  SDL_Surface *pPollution;
  SDL_Surface *pPolice;
  SDL_Surface *pWorklist;
  
  /* Small Citizens */
  SDL_Surface *pMale_Happy;
  SDL_Surface *pFemale_Happy;
  SDL_Surface *pMale_Content;
  SDL_Surface *pFemale_Content;
  SDL_Surface *pMale_Unhappy;
  SDL_Surface *pFemale_Unhappy;
  SDL_Surface *pMale_Angry;
  SDL_Surface *pFemale_Angry;
	
  SDL_Surface *pSpec_Lux; /* Elvis */
  SDL_Surface *pSpec_Tax; /* TaxMan */
  SDL_Surface *pSpec_Sci; /* Scientist */

} *pIcons;

void tilespec_setup_city_icons(void);
void tilespec_free_city_icons(void);
void reload_citizens_icons(int style);
SDL_Surface * get_citizen_surface(enum citizen_type type, int citizen_index);
void unload_unused_graphics(void);

#endif  /* FC__GUI_TILESPEC_H */
