/**********************************************************************
 Freeciv - Copyright (C) 2006 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <SDL/SDL.h>

/* gui-sdl */
#include "colors.h"
#include "graphics.h"
#include "gui_tilespec.h"
#include "themespec.h"

#include "gui_stuff.h"

/**************************************************************************
  ...
**************************************************************************/
struct widget *create_checkbox(SDL_Surface *pDest, bool state, Uint32 flags)
{
  struct widget *pCBox = fc_calloc(1, sizeof(struct widget));
  struct CHECKBOX *pTmp = fc_calloc(1, sizeof(struct CHECKBOX));
    
  if (state) {
    pCBox->theme = pTheme->CBOX_Sell_Icon;
  } else {
    pCBox->theme = pTheme->CBOX_Unsell_Icon;
  }

  set_wflag(pCBox, (WF_FREE_STRING | WF_FREE_GFX | WF_FREE_PRIVATE_DATA | flags));
  set_wstate(pCBox, FC_WS_DISABLED);
  set_wtype(pCBox, WT_CHECKBOX);
  pCBox->mod = KMOD_NONE;
  pCBox->dst = pDest;
  pTmp->state = state;
  pTmp->pTRUE_Theme = pTheme->CBOX_Sell_Icon;
  pTmp->pFALSE_Theme = pTheme->CBOX_Unsell_Icon;
  pCBox->private_data.cbox = pTmp;
  
  pCBox->size.w = pCBox->theme->w / 4;
  pCBox->size.h = pCBox->theme->h;

  return pCBox;
}

/**************************************************************************
  ...
**************************************************************************/
struct widget * create_textcheckbox(SDL_Surface *pDest, bool state,
		  SDL_String16 *pStr, Uint32 flags)
{
  struct widget *pCBox;
  struct CHECKBOX *pTmp;
  SDL_Surface *pSurf, *pIcon;

  if (!pStr) {
    return create_checkbox(pDest, state, flags);
  }
  
  pTmp = fc_calloc(1, sizeof(struct CHECKBOX));
    
  if (state) {
    pSurf = pTheme->CBOX_Sell_Icon;
  } else {
    pSurf = pTheme->CBOX_Unsell_Icon;
  }
    
  pIcon = create_icon_from_theme(pSurf, 0);
  pCBox = create_iconlabel(pIcon, pDest, pStr, (flags | WF_FREE_PRIVATE_DATA));

  pStr->style &= ~SF_CENTER;

  pCBox->theme = pSurf;
  FREESURFACE(pIcon);

  set_wtype(pCBox, WT_TCHECKBOX);
  pTmp->state = state;
  pTmp->pTRUE_Theme = pTheme->CBOX_Sell_Icon;
  pTmp->pFALSE_Theme = pTheme->CBOX_Unsell_Icon;
  pCBox->private_data.cbox = pTmp;
  
  return pCBox;
}

int set_new_checkbox_theme(struct widget *pCBox ,
				SDL_Surface *pTrue, SDL_Surface *pFalse)
{
  struct CHECKBOX *pTmp;
  
  if(!pCBox || (get_wtype(pCBox) != WT_CHECKBOX)) {
    return -1;
  }
  
  if(!pCBox->private_data.cbox) {
    pCBox->private_data.cbox = fc_calloc(1, sizeof(struct CHECKBOX));
    set_wflag(pCBox, WF_FREE_PRIVATE_DATA);
    pCBox->private_data.cbox->state = FALSE;
  }
  
  pTmp = pCBox->private_data.cbox;
  pTmp->pTRUE_Theme = pTrue;
  pTmp->pFALSE_Theme = pFalse;
  if(pTmp->state) {
    pCBox->theme = pTrue;
  } else {
    pCBox->theme = pFalse;
  }
  return 0;
}

void togle_checkbox(struct widget *pCBox)
{
  if(pCBox->private_data.cbox->state) {
    pCBox->theme = pCBox->private_data.cbox->pFALSE_Theme;
    pCBox->private_data.cbox->state = FALSE;
  } else {
    pCBox->theme = pCBox->private_data.cbox->pTRUE_Theme;
    pCBox->private_data.cbox->state = TRUE;
  }
}

bool get_checkbox_state(struct widget *pCBox)
{
  return pCBox->private_data.cbox->state;
}

int redraw_textcheckbox(struct widget *pCBox)
{
  int ret;
  SDL_Surface *pTheme_Surface, *pIcon;
  SDL_Rect dest;

  if(!pCBox->string16) {
    return redraw_icon(pCBox);
  }

  pTheme_Surface = pCBox->theme;
  pIcon = create_icon_from_theme(pTheme_Surface, get_wstate(pCBox));
  
  if (!pIcon) {
    return -3;
  }
  
  pCBox->theme = pIcon;

  /* if label transparen then clear background under widget or save this background */
  if (get_wflags(pCBox) & WF_RESTORE_BACKGROUND) {
    dest = pCBox->size;
    fix_rect(pCBox->dst, &dest);
    if (pCBox->gfx) {
      clear_surface(pCBox->dst, &dest);
      blit_entire_src(pCBox->gfx, pCBox->dst, dest.x, dest.y);
    } else {
      pCBox->gfx = crop_rect_from_surface(pCBox->dst, &dest);
    }
  }

  /* redraw icon label */
  ret = redraw_iconlabel(pCBox);

  FREESURFACE(pIcon);
  pCBox->theme = pTheme_Surface;

  return ret;
}
