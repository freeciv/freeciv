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

/***************************************************************************
                          gui_string.c  -  description
                             -------------------
    begin                : June 30 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "gui_main.h"
#include "gui_mem.h"
#include "graphics.h"
#include "unistring.h"
#include "gui_string.h"

#define DEFAULT_PTSIZE	18
#define FONT_WITH_PATH "theme/tahoma.ttf"

extern char *pDataPath;
extern struct Sdl Main;

/* =================================================== */

static struct TTF_Font_Chain {
  struct TTF_Font_Chain *next;
  TTF_Font *font;
  Uint16 ptsize;		/* size of font */
  Uint16 count;			/* number of strings alliased with this font */
} *Font_TAB;

static unsigned int Sizeof_Font_TAB;

static TTF_Font *load_font(Uint16 ptsize);
/*static void unload_font(Uint16 ptsize);*/

static SDL_Surface *create_str16_surf(SDL_String16 * pString);
static SDL_Surface *create_str16_multi_surf(SDL_String16 * pString);

/**************************************************************************
  ...
**************************************************************************/
SDL_Rect str16size(SDL_String16 * pString16)
{
  SDL_Rect Ret = { 0, 0, 0, 0 };
  SDL_Surface *pText;
  SDL_bool empty = SDL_FALSE;

  if (!pString16) {
    return Ret;
  }

  if (pString16->text == NULL) {
    /* FIXME: is this correct?  We temporarily set the wide string
     * to containe " ".  Then we reset it so as not to leave a
     * dangling pointer. */
    Uint16 text[2];
    text[0] = ' ';
    text[1] = '\0';
    pString16->text = text;
    empty = SDL_TRUE;
    pText = create_str16_surf(pString16);
    pString16->text = NULL;
  } else {
    pText = create_text_surf_from_str16(pString16);
  }

  Ret.w = pText->w;
  Ret.h = pText->h;

  FREESURFACE(pText);

  if (empty) {
    Ret.w = 0;
  }

  return Ret;
}

/**************************************************************************
  Create string16 struct with ptsize font.
  Font will be loaded or aliased with existing font of that size.
  pInTextString must be allocated in memory (MALLOC/CALLOC)
**************************************************************************/
SDL_String16 *create_string16(Uint16 * pInTextString, Uint16 ptsize)
{
  SDL_String16 *str = MALLOC(sizeof(SDL_String16));

  if (!ptsize) {
    ptsize = DEFAULT_PTSIZE;
  }

  str->ptsize = ptsize;

  if ((str->font = load_font(ptsize)) == NULL) {
    freelog(LOG_ERROR, _("Error in create_string16: Aborting ..."));
    FREE(str);
    return str;
  }

  str->style = TTF_STYLE_NORMAL;
  str->backcol.r = 0xff;
  str->backcol.g = 0xff;
  str->backcol.b = 0xff;
  str->backcol.unused = 0xff;
  str->forecol.r = 0x00;
  str->forecol.g = 0x00;
  str->forecol.b = 0x00;
  str->forecol.unused = 0xff;
  str->render = 2;		/* oh... alpha :) */

  /* pInTextString must be allocated in memory (MALLOC/CALLOC) */
  str->text = pInTextString;

  return str;
}

/**************************************************************************
  ...
**************************************************************************/
SDL_String16 *clone_string16(SDL_String16 * pString16)
{
  SDL_String16 *str16 = NULL;

#if 0
  if (load_font(pString16->ptsize) == NULL) {
    fprintf(stderr, "Error in clone_string16: Aborting ...\n");
    return NULL;
  }
#endif

  str16 = MALLOC(sizeof(SDL_String16));
  memcpy(str16, pString16, sizeof(SDL_String16));
  str16->text = unistrclon(pString16->text);
  return str16;
}

/**************************************************************************
  ...
**************************************************************************/
int write_text16(SDL_Surface * pDest, Sint16 x, Sint16 y,
		 SDL_String16 * pString)
{
  SDL_Rect dst_rect = { x, y, 0, 0 };
  SDL_Surface *pText = create_text_surf_from_str16(pString);

  if (SDL_BlitSurface(pText, NULL, pDest, &dst_rect) < 0) {
    freelog(LOG_ERROR, _("Couldn't blit text to display: %s"),
	    SDL_GetError());
    FREESURFACE(pText);
    return -1;
  }

  FREESURFACE(pText);
  return 0;
}

/**************************************************************************
  Create Text Surface from SDL_String16
**************************************************************************/
static SDL_Surface *create_str16_surf(SDL_String16 * pString)
{
  SDL_Surface *pText = NULL; /* FIXME: possibly uninitialized */

  if (!pString) {
    return NULL;
  }

  if (!((pString->style & 0x0F) & TTF_STYLE_NORMAL)) {
    TTF_SetFontStyle(pString->font, (pString->style & 0x0F));
  }

  switch (pString->render) {
  case 0:
    pText = TTF_RenderUNICODE_Shaded(pString->font,
				     pString->text, pString->forecol,
				     pString->backcol);
#if 0
    if ((pText = SDL_DisplayFormat(pTmp)) == NULL) {
      freelog(LOG_ERROR, _("Error in SDL_create_str16_surf: "
			   "Couldn't convert text to display format: %s"),
	      SDL_GetError());
      pText = pTmp;
      goto END;
    }
#endif

    break;
  case 1:
    pText = TTF_RenderUNICODE_Solid(pString->font,
				    pString->text, pString->forecol);
#if 0
    if ((pText = SDL_DisplayFormat(pTmp)) == NULL) {
      freelog(LOG_ERROR,
	      _("Error in SDL_create_str16_surf: Couldn't convert text "
		"to display format: %s"), SDL_GetError());
      pText = pTmp;
      goto END;
    }
#endif

    break;
  case 2:
    pText = TTF_RenderUNICODE_Blended(pString->font,
				      pString->text, pString->forecol);
    SDL_SetAlpha(pText, SDL_SRCALPHA, 255);
    break;
  }


  freelog(LOG_DEBUG,
	  _("SDL_create_str16_surf: Font is generally %d big, and "
	    "string is %hd big"), TTF_FontHeight(pString->font), pText->h);
  freelog(LOG_DEBUG, _("SDL_create_str16_surf: String is %d lenght"),
	  pText->w);


  /*FREESURFACE( pTmp );

     END:
   */
  if (!((pString->style & 0x0F) & TTF_STYLE_NORMAL)) {
    TTF_SetFontStyle(pString->font, TTF_STYLE_NORMAL);
  }

  return pText;
}

/**************************************************************************
  ...
**************************************************************************/
static SDL_Surface *create_str16_multi_surf(SDL_String16 * pString)
{
  SDL_Rect des = { 0, 0, 0, 0 };
  SDL_Surface *pText = NULL, *pNew = NULL, **pTmp = NULL;
  Uint16 i, w = 0, count = 0;
  Uint32 color;
  Uint16 *pBuf = pString->text;
  Uint16 **UniTexts = create_new_line_unistrings(pString->text);

  while (UniTexts[count]) {
    count++;
  }

  pTmp = CALLOC(count, sizeof(SDL_Surface *));

  for (i = 0; i < count; i++) {
    pString->text = UniTexts[i];
    pTmp[i] = create_str16_surf(pString);
  }

  pString->text = pBuf;

  /* find max len */
  for (i = 0; i < count; i++) {
    if (pTmp[i]->w > w) {
      w = pTmp[i]->w;
    }
  }

  /* create and fill surface */
  pText = create_surf(w, count * pTmp[0]->h, SDL_SWSURFACE);
  color = getpixel(pTmp[0], 0, 0);

  switch (pString->render) {
  case 1:
    SDL_FillRect(pText, NULL, color);
    SDL_SetColorKey(pText, SDL_SRCCOLORKEY, color);
    break;
  case 2:
    {
      pNew = pText;
      pText = SDL_ConvertSurface(pNew, pTmp[0]->format, pTmp[0]->flags);
      FREESURFACE(pNew);

      SDL_FillRect(pText, NULL, color);
      /*SDL_SetColorKey( pText , COLORKEYFLAG , color ); */
      SDL_SetAlpha(pText, SDL_SRCALPHA, 255);
    }
    break;
  default:
    SDL_FillRect(pText, NULL, color);
    break;
  }

  /* blit (default: center left) */
  for (i = 0; i < count; i++) {
    if (pString->style & SF_CENTER) {
      des.x = (w - pTmp[i]->w) / 2;
    } else {
      if (pString->style & SF_CENTER_RIGHT) {
	des.x = w - pTmp[i]->w;
      } else {
	des.x = 0;
      }
    }

    SDL_SetAlpha(pTmp[i], 0, 0);
    SDL_BlitSurface(pTmp[i], NULL, pText, &des);
    des.y += pTmp[i]->h;
  }


  /* Free Memmory */
  for (i = 0; i < count; i++) {
    FREE(UniTexts[i]);
    SDL_FreeSurface(pTmp[i]);
  }

  FREE(UniTexts);
  FREE(pTmp);

  return pText;
}

/**************************************************************************
  ...
**************************************************************************/
SDL_Surface *create_text_surf_from_str16(SDL_String16 * pString)
{
  Uint16 *pStr16 = NULL;

  if (!pString || !pString->text)
    return NULL;

  pStr16 = pString->text;

  /* find '\n' */
  while (*pStr16 != 0) {
    if (*pStr16 == 10)
      goto NEWLINE;
    pStr16++;
  }

  return create_str16_surf(pString);

NEWLINE:
  return create_str16_multi_surf(pString);
}

/**************************************************************************
  ...
**************************************************************************/
void change_ptsize16(SDL_String16 * pString, Uint16 new_ptsize)
{
  TTF_Font *pBuf;

  if ((pBuf = load_font(new_ptsize)) == NULL) {
    freelog(LOG_ERROR, _("Error in change_ptsize: Change ptsize failed"));
    return;
  }

  unload_font(pString->ptsize);
  pString->ptsize = new_ptsize;
  pString->font = pBuf;
}

/* =================================================== */

/**************************************************************************
  ...
**************************************************************************/
static TTF_Font *load_font(Uint16 ptsize)
{
  struct TTF_Font_Chain *Font_TAB_TMP = Font_TAB;
  TTF_Font *font_tmp = NULL;
  char *pFont_with_FullPath = NULL;
  size_t size;

  /* fint existing font and return pointer to him */
  if (Sizeof_Font_TAB) {
    /*for( i=0; i<Sizeof_Font_TAB; i++){ */
    while (Font_TAB_TMP) {
      if (Font_TAB_TMP->ptsize == ptsize) {
	Font_TAB_TMP->count++;
	return Font_TAB_TMP->font;
      }
      Font_TAB_TMP = Font_TAB_TMP->next;
    }
  }

  size = strlen(pDataPath) + strlen(FONT_WITH_PATH) + 1;
  pFont_with_FullPath = MALLOC(size);

  my_snprintf(pFont_with_FullPath, size, "%s%s", pDataPath,
	      FONT_WITH_PATH);

  /* Load Font */
  if ((font_tmp = TTF_OpenFont(pFont_with_FullPath, ptsize)) == NULL) {
    freelog(LOG_ERROR,
	    _("Error in load_font2: Couldn't load %d pt font from %s: %s"),
	    ptsize, pFont_with_FullPath, SDL_GetError());
    FREE( pFont_with_FullPath );  
    return font_tmp;
  }

  FREE( pFont_with_FullPath );
  
  /* add new font to list */
  if (Sizeof_Font_TAB == 0) {
    Sizeof_Font_TAB++;
    Font_TAB_TMP = MALLOC(sizeof(struct TTF_Font_Chain));
    Font_TAB = Font_TAB_TMP;
  } else {
    /* Go to end of chain */
    Font_TAB_TMP = Font_TAB;
    while (Font_TAB_TMP->next)
      Font_TAB_TMP = Font_TAB_TMP->next;

    Sizeof_Font_TAB++;
    Font_TAB_TMP->next = MALLOC(sizeof(struct TTF_Font_Chain));
    Font_TAB_TMP = Font_TAB_TMP->next;
  }

  Font_TAB_TMP->ptsize = ptsize;
  Font_TAB_TMP->count = 1;
  Font_TAB_TMP->font = font_tmp;

  return font_tmp;
}

/**************************************************************************
  ...
**************************************************************************/
void unload_font(Uint16 ptsize)
{
  int index;

  struct TTF_Font_Chain *Font_TAB_PREV = NULL;
  struct TTF_Font_Chain *Font_TAB_TMP = Font_TAB;

  if (Sizeof_Font_TAB == 0) {
    freelog(LOG_ERROR,
	    _
	    ("Error in unload_font2: Trying unload from empty Font ARRAY"));
    return;
  }

  for (index = 0; index < Sizeof_Font_TAB; index++) {
    if (Font_TAB_TMP->ptsize == ptsize) {
      break;
    }
    Font_TAB_PREV = Font_TAB_TMP;
    Font_TAB_TMP = Font_TAB_TMP->next;
  }

  if (index == Sizeof_Font_TAB) {
    freelog(LOG_ERROR,
	    _("Error in unload_font2: Trying unload Font which is "
	      "not include in Font ARRAY"));
    return;
  }

  Font_TAB_TMP->count--;

  if (Font_TAB_TMP->count) {
    return;
  }

  TTF_CloseFont(Font_TAB_TMP->font);
  Font_TAB_TMP->font = NULL;
  if (Font_TAB_TMP == Font_TAB) {
    Font_TAB = Font_TAB_TMP->next;
  } else {
    Font_TAB_PREV->next = Font_TAB_TMP->next;
  }
  Font_TAB_TMP->next = NULL;
  Sizeof_Font_TAB--;
  FREE(Font_TAB_TMP);
}
