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
                          gui_stuff.c  -  description
                             -------------------
    begin                : June 30 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "fcintl.h"
#include "support.h"
#include "log.h"

#include "options.h"
#include "colors.h"

#include "graphics.h"
#include "unistring.h"
#include "gui_string.h"
#include "gui_zoom.h"
#include "gui_tilespec.h"
#include "gui_main.h"
#include "mapview.h"

#include "gui_id.h"
#include "gui_stuff.h"

struct GUI *pSellected_Widget;

extern char *pDataPath;

extern Uint32 widget_info_counter;

static SDL_Rect *pInfo_Area = NULL;
static SDL_Surface *pLocked_buffer = NULL;

static struct GUI *pBeginMainWidgetList;
/* static struct GUI *pEndMainWidgetList; */

/*
 *	Set new pixel and return old pixel value in 'old_pixel'
 */
#define putpixel_and_save_bcg(pSurface, x, y, new_pixel, old_pixel)	\
do {									\
  Uint8 *buf_ptr = (Uint8 *)pSurface->pixels + (y * pSurface->pitch);	\
  switch(pSurface->format->BytesPerPixel) {				\
  case 1:								\
    buf_ptr += x;							\
    old_pixel = *(Uint8 *)buf_ptr;					\
    *(Uint8 *)buf_ptr = new_pixel;					\
    break;								\
  case 2:								\
    buf_ptr += 2 * x;							\
    old_pixel = *(Uint16 *)buf_ptr;					\
    *(Uint16 *)buf_ptr = new_pixel;					\
    break;								\
  case 3:								\
    buf_ptr += 3 * x;							\
    if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {				\
      old_pixel =  (buf_ptr[0] << 16)					\
        | (buf_ptr[1] << 8) | buf_ptr[2];				\
      buf_ptr[0] = (new_pixel >> 16) & 0xff;				\
      buf_ptr[1] = (new_pixel >> 8) & 0xff;				\
      buf_ptr[2] = new_pixel & 0xff;					\
    } else {								\
      old_pixel = buf_ptr[0] | (buf_ptr[1] << 8)			\
        | (buf_ptr[2] << 16);						\
      buf_ptr[0] = new_pixel & 0xff;					\
      buf_ptr[1] = (new_pixel >> 8) & 0xff;				\
      buf_ptr[2] = (new_pixel >> 16) & 0xff;				\
    }									\
    break;								\
  case 4:								\
    buf_ptr += 4 * x;							\
    old_pixel = *(Uint32 *)buf_ptr;					\
    *(Uint32 *)buf_ptr = new_pixel;					\
    break;								\
  }									\
} while(0)

#define UpperAdd(pNew_Widget, pAdd_Dock)	\
do {						\
  pNew_Widget->prev = pAdd_Dock;		\
  pNew_Widget->next = pAdd_Dock->next;		\
  if(pAdd_Dock->next) {			\
    pAdd_Dock->next->prev = pNew_Widget;	\
  }						\
  pAdd_Dock->next = pNew_Widget;		\
} while(0)

#define DownAdd(pNew_Widget, pAdd_Dock)		\
do {						\
  pNew_Widget->next = pAdd_Dock;		\
  pNew_Widget->prev = pAdd_Dock->prev;		\
  if(pAdd_Dock->prev) {				\
    pAdd_Dock->prev->next = pNew_Widget;	\
  }						\
  pAdd_Dock->prev = pNew_Widget;		\
  if(pAdd_Dock == pBeginMainWidgetList) {	\
    pBeginMainWidgetList = pNew_Widget;		\
  }						\
} while(0)

/**************************************************************************
  ...
**************************************************************************/
struct UniChar {
  struct UniChar *next;
  struct UniChar *prev;
  Uint16 chr[2];
  SDL_Surface *pTsurf;
};

static size_t chainlen(const struct UniChar *pChain);
static void del_chain(struct UniChar *pChain);
static struct UniChar * text2chain(const Uint16 *pInText);
static Uint16 * chain2text(const struct UniChar *pInChain, size_t len);
		       
static void correct_size_bcgnd_surf(SDL_Surface *pTheme,
				    Uint16 *pWidth, Uint16 *pHigh);
#if 0
static SDL_Surface * create_bcgnd_surf(SDL_Surface *pTheme,
				      SDL_bool transp, Uint8 state,
				      Uint16 Width, Uint16 High);
#endif
static SDL_Surface * create_vertical_surface(SDL_Surface *pVert_theme,
					    Uint8 state, Uint16 High);

static void remove_buffer_layer(SDL_Surface *pBuffer);
static int redraw_iconlabel(struct GUI *pLabel);
  
/**************************************************************************
  Correct backgroud size ( set min size ). Used in create widget
  functions.
**************************************************************************/
static void correct_size_bcgnd_surf(SDL_Surface * pTheme,
				    Uint16 * pWidth, Uint16 * pHigh)
{
  *pWidth = MAX(*pWidth, 2 * (pTheme->w / 16));
  *pHigh = MAX(*pHigh, 2 * (pTheme->h / 16));
}

/**************************************************************************
  Create background image for buttons, iconbuttons and edit fields
  then return  pointer to this image.

  Graphic is taken from pTheme surface and blit to new created image.

  Length and height depend of iText_with, iText_high parameters.

  Type of image depend of "state" parameter.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled

  if 'transp' is TRUE returned surface has set Alpha = 128
**************************************************************************/
SDL_Surface *create_bcgnd_surf(SDL_Surface * pTheme, SDL_bool transp,
			       Uint8 state, Uint16 Width, Uint16 High)
{
  int iTile_width_len_end, iTile_width_len_mid, iTile_count_len_mid;
  int iTile_width_high_end, iTile_width_high_mid, iTile_count_high_mid;
  int i, j;

  SDL_Rect src, des;
  SDL_Surface *pBackground = NULL;

  int iStart_y = (pTheme->h / 4) * state;	/*(pTheme->h/4)*state */

  iTile_width_len_end = pTheme->w / 16;	/* pTheme->w/16 */
  iTile_width_len_mid = pTheme->w - (iTile_width_len_end * 2);

  iTile_count_len_mid =
      (Width - (iTile_width_len_end * 2)) / iTile_width_len_mid;

  /* corrections I */
  if (((iTile_count_len_mid *
	iTile_width_len_mid) + (iTile_width_len_end * 2)) < Width) {
    iTile_count_len_mid++;
  }

  iTile_width_high_end = pTheme->h / 16;	/* pTheme->h/16 */
  iTile_width_high_mid = (pTheme->h / 4) - (iTile_width_high_end * 2);
  iTile_count_high_mid =
      (High - (iTile_width_high_end * 2)) / iTile_width_high_mid;

  /* corrections II */
  if (((iTile_count_high_mid *
	iTile_width_high_mid) + (iTile_width_high_end * 2)) < High) {
    iTile_count_high_mid++;
  }

  Width = MAX(iTile_width_len_end * 2, Width);
  High = MAX(iTile_width_high_end * 2, High);

  /* now allocate memory */
  pBackground = create_surf(Width, High, SDL_SWSURFACE);

  /* copy left end */

  /* copy left top end */
  src.x = 0;
  src.y = iStart_y;
  src.w = iTile_width_len_end;
  src.h = iTile_width_high_end;

  des.x = 0;
  des.y = 0;

  SDL_BlitSurface(pTheme, &src, pBackground, NULL);

  /* copy left middels parts */
  src.y = iStart_y + iTile_width_high_end;
  src.h = iTile_width_high_mid;
  for (i = 0; i < iTile_count_high_mid; i++) {
    des.y = iTile_width_high_end + i * iTile_width_high_mid;
    SDL_BlitSurface(pTheme, &src, pBackground, &des);
  }

  /* copy left boton end */
  src.y = iStart_y + ((pTheme->h / 4) - iTile_width_high_end);
  src.h = iTile_width_high_end;
  des.y = pBackground->h - iTile_width_high_end;
  SDL_BlitSurface(pTheme, &src, pBackground, &des);

  /* copy middle parts without right end part */

  src.x = iTile_width_len_end;
  src.y = iStart_y;
  src.w = iTile_width_len_mid;

  for (i = 0; i < iTile_count_len_mid; i++) {

    /* top */
    des.x = iTile_width_len_end + i * iTile_width_len_mid;
    des.y = 0;
    src.y = iStart_y;
    SDL_BlitSurface(pTheme, &src, pBackground, &des);

    /*  middels */
    src.y = iStart_y + iTile_width_high_end;
    src.h = iTile_width_high_mid;
    for (j = 0; j < iTile_count_high_mid; j++) {
      des.y = iTile_width_high_end + j * iTile_width_high_mid;
      SDL_BlitSurface(pTheme, &src, pBackground, &des);
    }

    /* bottom */
    src.y = iStart_y + ((pTheme->h / 4) - iTile_width_high_end);
    src.h = iTile_width_high_end;
    des.y = pBackground->h - iTile_width_high_end;
    SDL_BlitSurface(pTheme, &src, pBackground, &des);
  }

  /* copy right end */
  src.x = pTheme->w - iTile_width_len_end;
  src.y = iStart_y;
  src.w = iTile_width_len_end;

  des.x = pBackground->w - iTile_width_len_end;
  des.y = 0;

  SDL_BlitSurface(pTheme, &src, pBackground, &des);

  /*  middels */
  src.y = iStart_y + iTile_width_high_end;
  src.h = iTile_width_high_mid;
  for (i = 0; i < iTile_count_high_mid; i++) {
    des.y = iTile_width_high_end + i * iTile_width_high_mid;
    SDL_BlitSurface(pTheme, &src, pBackground, &des);
  }

  /*boton */
  src.y = iStart_y + ((pTheme->h / 4) - iTile_width_high_end);
  src.h = iTile_width_high_end;
  des.y = pBackground->h - iTile_width_high_end;
  SDL_BlitSurface(pTheme, &src, pBackground, &des);
  
  /* set transparency 50% */
  if (transp) {
    SDL_SetAlpha(pBackground, SDL_SRCALPHA, 128);
  }

  return pBackground;
}

/* =================================================== */
/* ===================== GUI LIST ==================== */
/* =================================================== */

/**************************************************************************
  Simple "Search and De..." no search in 'pGUI_List' == "Widget's list" and
  return ( not Disabled and not Hiden ) widget under cursor 'pPosition'.
**************************************************************************/
struct GUI * WidgetListScaner(const struct GUI *pGUI_List,
			     SDL_MouseMotionEvent *pPosition)
{
  while (pGUI_List) {
    if ((pPosition->x >= pGUI_List->size.x) &&
	(pPosition->x <= pGUI_List->size.x + pGUI_List->size.w) &&
	(pPosition->y >= pGUI_List->size.y) &&
	(pPosition->y <= pGUI_List->size.y + pGUI_List->size.h)) {
      if (!((get_wstate(pGUI_List) == WS_DISABLED) ||
	    (get_wflags(pGUI_List) & WF_HIDDEN))) {
	return (struct GUI *) pGUI_List;
      }
    }

    pGUI_List = pGUI_List->next;
  }
  return NULL;
}

/**************************************************************************
  Search in 'pGUI_List' == "Widget's list" and
  return ( not Disabled and not Hiden ) widget with 'Key' allias.
  NOTE: This function ignores CapsLock and NumLock Keys.
**************************************************************************/
struct GUI *WidgetListKeyScaner(const struct GUI *pGUI_List, SDL_keysym Key)
{
  Key.mod &= ~(KMOD_NUM | KMOD_CAPS);
  while (pGUI_List) {
    if ((pGUI_List->key == Key.sym) &&
	((pGUI_List->mod & Key.mod) || (pGUI_List->mod == Key.mod))) {
      if (!((get_wstate(pGUI_List) == WS_DISABLED) ||
	    (get_wflags(pGUI_List) & WF_HIDDEN))) {
	return (struct GUI *) pGUI_List;
      }
    }
    pGUI_List = pGUI_List->next;
  }
  return NULL;
}

/**************************************************************************
  Pointer to Main Widget list is declared staric in 'gui_stuff.c'
  This function only calls 'WidgetListScaner' in Main list
  'pBeginMainWidgetList'
**************************************************************************/
struct GUI *MainWidgetListScaner(SDL_MouseMotionEvent * pPosition)
{
  return WidgetListScaner(pBeginMainWidgetList, pPosition);
}

/**************************************************************************
  ...
**************************************************************************/
struct GUI *MainWidgetListKeyScaner(SDL_keysym Key)
{
  return WidgetListKeyScaner(pBeginMainWidgetList, Key);
}

/**************************************************************************
  Do default Widget action when pressed, and then call widget callback
  function.

  example for Button:
    set flags FW_Pressed
    redraw button ( pressed )
    refresh screen ( to see result )
    wait 300 ms	( to see result :)
    If exist (button callback function) then
      call (button callback function)

    Function normal return Widget ID.
    NOTE: NOZERO return of this function deterninate exit of
        MAIN_SDL_GAME_LOOP
    if ( pWidget->action )
      if ( pWidget->action(pWidget)  ) ID = 0;
    if widget callback function return = 0 then return NOZERO
    I default return (-1) from Widget callback functions.
**************************************************************************/
Uint16 widget_pressed_action(struct GUI * pWidget)
{
  Uint16 ID = 0;

  if (!pWidget) {
    return 0;
  }
  
  widget_info_counter = 0;
  if (pInfo_Area) {
    sdl_dirty_rect(*pInfo_Area);
    FREE(pInfo_Area);
  }
  
  switch (get_wtype(pWidget)) {
  case WT_TI_BUTTON:
    set_wstate(pWidget, WS_PRESSED);
    ID = pWidget->ID;
    real_redraw_tibutton(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
    set_wstate(pWidget, WS_SELLECTED);
    SDL_Delay(300);
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_I_BUTTON:
    set_wstate(pWidget, WS_PRESSED);
    ID = pWidget->ID;
    real_redraw_ibutton(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
    set_wstate(pWidget, WS_SELLECTED);
    SDL_Delay(300);
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_ICON:
    set_wstate(pWidget, WS_PRESSED);
    ID = pWidget->ID;
    real_redraw_icon(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
    set_wstate(pWidget, WS_SELLECTED);
    SDL_Delay(300);
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_ICON2:
    set_wstate(pWidget, WS_PRESSED);
    ID = pWidget->ID;
    real_redraw_icon2(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
    set_wstate(pWidget, WS_SELLECTED);
    SDL_Delay(300);
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_EDIT:
    edit_field(pWidget);
    redraw_edit(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    ID = 0;
    break;
  case WT_VSCROLLBAR:
    set_wstate(pWidget, WS_PRESSED);
    ID = pWidget->ID;
    redraw_vert(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_HSCROLLBAR:
    set_wstate(pWidget, WS_PRESSED);
    ID = pWidget->ID;
    redraw_horiz(pWidget);
    flush_rect(pWidget->size);
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_CHECKBOX:
    set_wstate(pWidget, WS_PRESSED);
    ID = pWidget->ID;
    real_redraw_icon(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
    set_wstate(pWidget, WS_SELLECTED);
    togle_checkbox(pWidget);
    SDL_Delay(300);
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_TCHECKBOX:
    set_wstate(pWidget, WS_PRESSED);
    ID = pWidget->ID;
    redraw_textcheckbox(pWidget);
    flush_rect(pWidget->size);
    set_wstate(pWidget, WS_SELLECTED);
    togle_checkbox(pWidget);
    SDL_Delay(300);
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  default:
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  }

  return ID;
}

/**************************************************************************
  Unsellect (sellected) widget and redraw this widget;
**************************************************************************/
void unsellect_widget_action(void)
{
  if (pSellected_Widget) {
    if (get_wstate(pSellected_Widget) == WS_DISABLED) {
      goto End;
    }

    set_wstate(pSellected_Widget, WS_NORMAL);

    if (get_wflags(pSellected_Widget) & WF_HIDDEN) {
      goto End;
    }

    switch (get_wtype(pSellected_Widget)) {
    case WT_TI_BUTTON:
      real_redraw_tibutton(pSellected_Widget);
      break;
    case WT_I_BUTTON:
      real_redraw_ibutton(pSellected_Widget);
      break;
    case WT_ICON:
    case WT_CHECKBOX:
      real_redraw_icon(pSellected_Widget);
      break;
    case WT_ICON2:
      real_redraw_icon2(pSellected_Widget);
      break;
    case WT_TCHECKBOX:
      redraw_textcheckbox(pSellected_Widget);
      break;
    case WT_I_LABEL:
    case WT_T2_LABEL:  
      redraw_label(pSellected_Widget);
      break;
    case WT_VSCROLLBAR:
      redraw_vert(pSellected_Widget);
      break;
    case WT_HSCROLLBAR:
      redraw_horiz(pSellected_Widget);
      break;
    default:
      break;
    }

    flush_rect(pSellected_Widget->size);

    /* turn off quick info timer/counter */ 
    widget_info_counter = 0;

  }

End:

  if (pInfo_Area) {
    flush_rect(*pInfo_Area);
    FREE(pInfo_Area);
  }

  pSellected_Widget = NULL;
}

/**************************************************************************
  Sellect widget.  Redraw this widget;
**************************************************************************/
void widget_sellected_action(struct GUI *pWidget)
{
  if (!pWidget || pWidget == pSellected_Widget) {
    return;
  }

  if (pSellected_Widget) {
    unsellect_widget_action();
  }

  switch (get_wtype(pWidget)) {
  case WT_TI_BUTTON:
    set_wstate(pWidget, WS_SELLECTED);
    real_redraw_tibutton(pWidget);
    break;
  case WT_I_BUTTON:
    set_wstate(pWidget, WS_SELLECTED);
    real_redraw_ibutton(pWidget);
    break;
  case WT_ICON:
  case WT_CHECKBOX:
    set_wstate(pWidget, WS_SELLECTED);
    real_redraw_icon(pWidget);
    break;
  case WT_ICON2:
    set_wstate(pWidget, WS_SELLECTED);
    real_redraw_icon2(pWidget);
    break;
  case WT_TCHECKBOX:
    set_wstate(pWidget, WS_SELLECTED);
    redraw_textcheckbox(pWidget);
    break;
  case WT_I_LABEL:
  case WT_T2_LABEL:  
    set_wstate(pWidget, WS_SELLECTED);
    redraw_label(pWidget);
    break;
  case WT_VSCROLLBAR:
    set_wstate(pWidget, WS_SELLECTED);
    redraw_vert(pWidget);
    break;
  case WT_HSCROLLBAR:
    set_wstate(pWidget, WS_SELLECTED);
    redraw_horiz(pWidget);
    break;
  default:
    break;
  }

  if (get_wstate(pWidget) == WS_SELLECTED) {
    flush_rect(pWidget->size);
    pSellected_Widget = pWidget;
    if (get_wflags(pWidget) & WF_WIDGET_HAS_INFO_LABEL) {
      widget_info_counter = 1;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void draw_widget_info_label(void)
{
  SDL_Surface *pText, *pBcgd;
  SDL_Rect dest;
  SDL_Color color;

  struct GUI *pWidget = pSellected_Widget;

  if (!pWidget) {
    return;
  }

  pInfo_Area = MALLOC(sizeof(SDL_Rect));

  /*pWidget->string16->render = 3;*/
  
  /* create string and bcgd theme */
  pText = create_text_surf_from_str16(pWidget->string16);
  /*SDL_SetAlpha(pText, 0x0, 0x0);*/

  color = *get_game_colorRGB(QUICK_INFO);
  color.unused = 150;


  pBcgd = create_filled_surface(pText->w + 10, pText->h + 6, SDL_SWSURFACE,
				get_game_colorRGB(QUICK_INFO));


  /* callculate start position */
  if (pWidget->size.y - pBcgd->h - 6 < 0) {
    pInfo_Area->y = pWidget->size.y + pWidget->size.h + 3;
  } else {
    pInfo_Area->y = pWidget->size.y - pBcgd->h - 5;
  }

  if (pWidget->size.x + pBcgd->w + 5 > Main.screen->w) {
    pInfo_Area->x = pWidget->size.x - pBcgd->w - 5;
  } else {
    pInfo_Area->x = pWidget->size.x + 3;
  }

  pInfo_Area->w = pBcgd->w + 2;
  pInfo_Area->h = pBcgd->h + 2;
  
  /* draw theme and text */
  dest = *pInfo_Area;
  dest.x += 1;
  dest.y += 1;
  SDL_BlitSurface(pBcgd, NULL, Main.screen, &dest);
  /*
  SDL_FillRect(Main.gui, &dest , 
     SDL_MapRGBA(Main.gui->format, color.r, color.g, color.b, color.unused));
  */
  dest.x += 5;
  dest.y += 3;
  SDL_BlitSurface(pText, NULL, Main.screen, &dest);

  /* draw frame */
  putline(Main.screen, pInfo_Area->x + 1, pInfo_Area->y,
	  pInfo_Area->x + pInfo_Area->w - 2, pInfo_Area->y, 0x0);

  putline(Main.screen, pInfo_Area->x, pInfo_Area->y + 1,
	  pInfo_Area->x, pInfo_Area->y + pInfo_Area->h - 2, 0x0);

  putline(Main.screen, pInfo_Area->x + 1, pInfo_Area->y + pInfo_Area->h - 2,
	  pInfo_Area->x + pInfo_Area->w - 2,
	  pInfo_Area->y + pInfo_Area->h - 2, 0x0);

  putline(Main.screen, pInfo_Area->x + pInfo_Area->w - 1, pInfo_Area->y + 1,
	  pInfo_Area->x + pInfo_Area->w - 1,
	  pInfo_Area->y + pInfo_Area->h - 2, 0x0);


  /*flush_rect(*pInfo_Area);*/
  if (correct_rect_region(pInfo_Area)) {
    SDL_UpdateRect(Main.screen, pInfo_Area->x, pInfo_Area->y,
				    pInfo_Area->w, pInfo_Area->h);
  }

  FREESURFACE(pText);
  FREESURFACE(pBcgd);

  return;
}


/**************************************************************************
  Find ID in Widget's List ('pGUI_List') and return pointer to this
  Widgets.
**************************************************************************/
struct GUI *get_widget_pointer_form_ID(const struct GUI *pGUI_List,
				       Uint16 ID)
{
  while (pGUI_List) {
    if (pGUI_List->ID == ID) {
      return (struct GUI *) pGUI_List;
    }

    pGUI_List = pGUI_List->next;
  }

  return NULL;
}

/**************************************************************************
  Find ID in MAIN Widget's List ( pBeginWidgetList ) and return pointer to
  this Widgets.
**************************************************************************/
struct GUI *get_widget_pointer_form_main_list(Uint16 ID)
{
  return get_widget_pointer_form_ID(pBeginMainWidgetList, ID);
}

/**************************************************************************
  INIT Main Widget's List ( pBeginWidgetList )
**************************************************************************/
void init_gui_list(Uint16 ID, struct GUI *pGUI)
{
  /* pEndWidgetList = pBeginWidgetList = pGUI; */
  pBeginMainWidgetList = pGUI;
  pBeginMainWidgetList->ID = ID;
}

/**************************************************************************
  Add Widget to Main Widget's List ( pBeginWidgetList )
**************************************************************************/
void add_to_gui_list(Uint16 ID, struct GUI *pGUI)
{
  pBeginMainWidgetList->prev = pGUI;
  pBeginMainWidgetList->prev->next = pBeginMainWidgetList;
  pBeginMainWidgetList->prev->ID = ID;
  pBeginMainWidgetList = pBeginMainWidgetList->prev;
}

/**************************************************************************
  Delete Widget from Main Widget's List ( pBeginWidgetList )

  NOTE: This function does not destroy Widget, only remove his pointer from
  list. To destroy this Widget totaly ( free mem... ) call macro:
  del_widget_from_gui_list( pWidget ).  This macro call this function.
**************************************************************************/
void del_widget_pointer_from_gui_list(struct GUI *pGUI)
{
  if (!pGUI) {
    return;
  }

  if (pGUI == pBeginMainWidgetList && pBeginMainWidgetList->next) {
    pBeginMainWidgetList = pBeginMainWidgetList->next;
  }

  if (pGUI->prev && pGUI->next) {
    pGUI->prev->next = pGUI->next;
    pGUI->next->prev = pGUI->prev;
  } else {
    if (pGUI->prev) {
      pGUI->prev->next = NULL;
    }

    if (pGUI->next) {
      pGUI->next->prev = NULL;
    }

  }

  if (pSellected_Widget == pGUI) {
    pSellected_Widget = NULL;
  }
}

/**************************************************************************
  Determinate if 'pGui' is first on WidgetList

  NOTE: This is used by My (move) GUI Window mechanism.  Return FALSE if is
  first.
**************************************************************************/
bool is_this_widget_first_on_list(struct GUI *pGUI)
{
  return pGUI != pBeginMainWidgetList;
}

/**************************************************************************
  Move pointer to Widget to list begin.

  NOTE: This is used by My GUI Window mechanism.
**************************************************************************/
void move_widget_to_front_of_gui_list(struct GUI *pGUI)
{
  if (!pGUI || pGUI == pBeginMainWidgetList) {
    return;
  }

  /* pGUI->prev always exists because
     we don't do this to pBeginMainWidgetList */
  if (pGUI->next) {
    pGUI->prev->next = pGUI->next;
    pGUI->next->prev = pGUI->prev;
  } else {
    pGUI->prev->next = NULL;
  }

  pGUI->next = pBeginMainWidgetList;
  pBeginMainWidgetList->prev = pGUI;
  pBeginMainWidgetList = pGUI;
  pGUI->prev = NULL;
}

/**************************************************************************
  Delete Main Widget's List.
**************************************************************************/
void del_main_list(void)
{
  del_gui_list(pBeginMainWidgetList);
}

/**************************************************************************
   Delete Wideget's List ('pGUI_List').
**************************************************************************/
void del_gui_list(struct GUI *pGUI_List)
{
  while (pGUI_List) {
    if (pGUI_List->next) {
      pGUI_List = pGUI_List->next;
      FREEWIDGET(pGUI_List->prev);
    } else {
      FREEWIDGET(pGUI_List);
    }
  }
}

/* ===================================================================== */
/* ================================ Universal ========================== */
/* ===================================================================== */

/**************************************************************************
  Universal redraw Group of Widget function.  Function is optimized to
  WindowGroup: start draw from 'pEnd' and stop on 'pBegin', in theory
  'pEnd' is window widget;
**************************************************************************/
Uint16 redraw_group(const struct GUI *pBeginGroupWidgetList,
		    const struct GUI *pEndGroupWidgetList,
		      int add_to_update)
{
  Uint16 count = 0;
  struct GUI *pTmpWidget = (struct GUI *) pEndGroupWidgetList;

  while (pTmpWidget) {

    if (!(get_wflags(pTmpWidget) & WF_HIDDEN)) {

      if (!(pTmpWidget->gfx) &&
	  (get_wflags(pTmpWidget) & WF_DRAW_THEME_TRANSPARENT)) {
	refresh_widget_background(pTmpWidget);
      }

      redraw_widget(pTmpWidget);

      if (get_wflags(pTmpWidget) & WF_DRAW_FRAME_AROUND_WIDGET) {
	if(get_wtype(pTmpWidget) & WT_WINDOW) {
	  draw_frame_inside_widget_on_surface(pTmpWidget, pTmpWidget->dst);
	} else {
	  draw_frame_around_widget_on_surface(pTmpWidget, pTmpWidget->dst);
	}
      }

      if (add_to_update) {
	sdl_dirty_rect(pTmpWidget->size);
      }

      count++;
    }

    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pTmpWidget->prev;

  }

  return count;
}

/**************************************************************************
  ...
**************************************************************************/
void set_new_group_start_pos(const struct GUI *pBeginGroupWidgetList,
			     const struct GUI *pEndGroupWidgetList,
			     Sint16 Xrel, Sint16 Yrel)
{
  struct GUI *pTmpWidget = (struct GUI *) pEndGroupWidgetList;

  while (pTmpWidget) {

    pTmpWidget->size.x += Xrel;
    pTmpWidget->size.y += Yrel;

    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pTmpWidget->prev;
  }
}

/**************************************************************************
  Move group of Widget's pointers to begin of main list.
  Move group destination buffer to end of buffer array.
  NOTE: This is used by My GUI Window(group) mechanism.
**************************************************************************/
void move_group_to_front_of_gui_list(struct GUI *pBeginGroupWidgetList,
				     struct GUI *pEndGroupWidgetList)
{
  struct GUI *pTmpWidget = pEndGroupWidgetList , *pPrev = NULL;
  SDL_Surface *pBuffer = pEndGroupWidgetList->dst;
  
  /* Widget Pointer Menagment */
  while (pTmpWidget) {
    
    pPrev = pTmpWidget->prev;
    
    /* pTmpWidget->prev always exists because we
       don't do this to pBeginMainWidgetList */
    if (pTmpWidget->next) {
      pTmpWidget->prev->next = pTmpWidget->next;
      pTmpWidget->next->prev = pTmpWidget->prev;
    } else {
      pTmpWidget->prev->next = NULL;
    }

    pTmpWidget->next = pBeginMainWidgetList;
    pBeginMainWidgetList->prev = pTmpWidget;
    pBeginMainWidgetList = pTmpWidget;
    pBeginMainWidgetList->prev = NULL;
    
    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pPrev;
  }
  
  /* Window Buffer Menagment */
  if(pBuffer != Main.gui) {
    int i = 0;  
    while(Main.guis && Main.guis[i] && i < Main.guis_count - 1) {
      if (Main.guis[i] && Main.guis[i + 1] && Main.guis[i] == pBuffer) {
	Main.guis[i] = Main.guis[i + 1];
	Main.guis[i + 1] = pBuffer;
      }
      i++;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void del_group_of_widgets_from_gui_list(struct GUI *pBeginGroupWidgetList,
					struct GUI *pEndGroupWidgetList)
{
  struct GUI *pBufWidget = NULL;
  struct GUI *pTmpWidget = pEndGroupWidgetList;

  if (pBeginGroupWidgetList == pEndGroupWidgetList) {
    del_widget_from_gui_list(pTmpWidget);
    return;
  }

  pTmpWidget = pTmpWidget->prev;

  while (pTmpWidget) {

    pBufWidget = pTmpWidget->next;
    del_widget_from_gui_list(pBufWidget);

    if (pTmpWidget == pBeginGroupWidgetList) {
      del_widget_from_gui_list(pTmpWidget);
      break;
    }

    pTmpWidget = pTmpWidget->prev;
  }

}

/**************************************************************************
  ...
**************************************************************************/
void set_group_state(struct GUI *pBeginGroupWidgetList,
		     struct GUI *pEndGroupWidgetList, enum WState state)
{
  struct GUI *pTmpWidget = pEndGroupWidgetList;
  while (pTmpWidget) {
    set_wstate(pTmpWidget, state);
    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }
    pTmpWidget = pTmpWidget->prev;
  }
}

/**************************************************************************
  ...
**************************************************************************/
void hide_group(struct GUI *pBeginGroupWidgetList,
		struct GUI *pEndGroupWidgetList)
{
  struct GUI *pTmpWidget = pEndGroupWidgetList;

  while (pTmpWidget) {
    set_wflag(pTmpWidget, WF_HIDDEN);

    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pTmpWidget->prev;
  }
}

/**************************************************************************
  ...
**************************************************************************/
void show_group(struct GUI *pBeginGroupWidgetList,
		struct GUI *pEndGroupWidgetList)
{
  struct GUI *pTmpWidget = pEndGroupWidgetList;

  while (pTmpWidget) {
    clear_wflag(pTmpWidget, WF_HIDDEN);

    if (pTmpWidget == pBeginGroupWidgetList) {
      break;
    }

    pTmpWidget = pTmpWidget->prev;
  }
}

/**************************************************************************
  Universal redraw Widget function.
**************************************************************************/
int redraw_widget(struct GUI *pWidget)
{
  if (!pWidget) {
    return -1;
  }

  switch (get_wtype(pWidget)) {
  case WT_TI_BUTTON:
    return real_redraw_tibutton(pWidget);
  case WT_I_BUTTON:
    return real_redraw_ibutton(pWidget);
  case WT_ICON:
  case WT_CHECKBOX:
    return real_redraw_icon(pWidget);
  case WT_ICON2:
    return real_redraw_icon2(pWidget);
  case WT_TCHECKBOX:
    return redraw_textcheckbox(pWidget);
  case WT_I_LABEL:
  case WT_T_LABEL:
  case WT_T2_LABEL:  
  case WT_TI_LABEL:
    return redraw_label(pWidget);
  case WT_WINDOW:
    return redraw_window(pWidget);
  case WT_EDIT:
    return redraw_edit(pWidget);
  case WT_VSCROLLBAR:
    return redraw_vert(pWidget);
  case WT_HSCROLLBAR:
    return redraw_horiz(pWidget);
  default:
    return 0;
  }
  return 0;
}

/* ===================================================================== *
 * =========================== Window Group ============================ *
 * ===================================================================== */

/*
 *	Window Group  -	group with 'pBegin' and 'pEnd' where
 *	windowed type widget is last on list ( 'pEnd' ).
 */

/**************************************************************************
  Undraw and destroy Window Group  The Trick is simple. We undraw only
  last member of group: the window.
**************************************************************************/
void popdown_window_group_dialog(struct GUI *pBeginGroupWidgetList,
				 struct GUI *pEndGroupWidgetList)
{
  if ((pBeginGroupWidgetList) && (pEndGroupWidgetList)) {
    if(pEndGroupWidgetList->dst == Main.gui ||
	      pEndGroupWidgetList->dst == pLocked_buffer) {
      /* undraw window */
      blit_entire_src(pEndGroupWidgetList->gfx, pEndGroupWidgetList->dst,
		    pEndGroupWidgetList->size.x,
		    pEndGroupWidgetList->size.y);
    } else {
      remove_buffer_layer(pEndGroupWidgetList->dst);
    }

    sdl_dirty_rect(pEndGroupWidgetList->size);

    del_group(pBeginGroupWidgetList, pEndGroupWidgetList);
  }
}

/**************************************************************************
  Sellect Window Group. (move widget group up the widgets list)
  Function return TRUE when group was sellected.
**************************************************************************/
bool sellect_window_group_dialog(struct GUI *pBeginWidgetList,
							 struct GUI *pWindow)
{
  if (is_this_widget_first_on_list(pBeginWidgetList)) {
    move_group_to_front_of_gui_list(pBeginWidgetList, pWindow);
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
  Move Window Group.  The Trick is simple.  We move only last member of
  group: the window , and then set new position to all members of group.

  Function return 1 when group was moved.
**************************************************************************/
bool move_window_group_dialog(struct GUI *pBeginGroupWidgetList,
			     struct GUI *pEndGroupWidgetList)
{
  bool ret = FALSE;
  Sint16 oldX = pEndGroupWidgetList->size.x,
      oldY = pEndGroupWidgetList->size.y;
  
  if (move_window(pEndGroupWidgetList)) {
    set_new_group_start_pos(pBeginGroupWidgetList,
			    pEndGroupWidgetList->prev,
			    pEndGroupWidgetList->size.x - oldX,
			    pEndGroupWidgetList->size.y - oldY);
    ret = TRUE;
  }

  return ret;
}

/**************************************************************************
  Standart Window Group Widget Callback (window)
  When Pressed check mouse move;
  if move then move window and redraw else
  if not on fron then move window up to list and redraw;  
**************************************************************************/
int std_move_window_group_callback(struct GUI *pBeginWidgetList,
							 struct GUI *pWindow)
{
  if (move_window_group_dialog(pBeginWidgetList, pWindow)) {
    sellect_window_group_dialog(pBeginWidgetList, pWindow);
    redraw_group(pBeginWidgetList, pWindow, 0);
    flush_rect(pWindow->size);
  } else {
    if(sellect_window_group_dialog(pBeginWidgetList, pWindow)) {
      flush_rect(pWindow->size);
    }      
  }
  return (-1);
}


/* =================================================== */
/* ============ Vertical Scroll Group List =========== */
/* =================================================== */

/**************************************************************************
  scroll pointers on list.
  dir == directions: up == -1, down == 1.
**************************************************************************/
static struct GUI *vertical_scroll_widget_list(struct GUI *pBeginActiveWidgetLIST,
				      struct GUI *pBeginWidgetLIST,
				      struct GUI *pEndWidgetLIST,
				      int active, int step, int dir)
{
  struct GUI *pBegin = pBeginActiveWidgetLIST;
  struct GUI *pBuf = pBeginActiveWidgetLIST;
  struct GUI *pTmp = NULL;  
  int count = active;
  int count_step = step;
    
  if (dir < 0) {
    bool real = TRUE;
    /* up */
    if (pBuf != pEndWidgetLIST) {
      /*
       move pointers to positions and unhidde scrolled widgets
       B = pBuf
       T = pTmp
       [B] [ ] [ ]
       -----------
       [T] [ ] [ ]
       [ ] [ ] [ ]
       -----------
       [ ] [ ] [ ]
    */
      pTmp = pBuf;
      while(count_step) {
      	pBuf = pBuf->next;
	clear_wflag(pBuf, WF_HIDDEN);
	count_step--;
      }
      count_step = step;
      
      /* setup new ActiveWidget pointer */
      pBegin = pBuf;
      
      /*
       scroll pointers up
       B = pBuf
       T = pTmp
       [B0] [B1] [B2]
       -----------
       [T0] [T1] [T2]   => B position = T position
       [  ] [  ] [  ]
       -----------
       [  ] [  ] [  ]
    */
      while (count) {
	if(real) {
	  pBuf->gfx = pTmp->gfx;
	  pBuf->size.x = pTmp->size.x;
	  pBuf->size.y = pTmp->size.y;
	  pTmp->gfx = NULL;
	  if(count == 1) {
	    set_wflag(pTmp, WF_HIDDEN);
	  }
	  if(pTmp == pBeginWidgetLIST) {
	    real = FALSE;
	  }
	  pTmp = pTmp->prev;
	} else {
	  pBuf->size.y += pBuf->size.h;
	  pBuf->gfx = NULL;
	}
	
	pBuf = pBuf->prev;
	count_step--;
	if(!count_step) {
	  count_step = step;
	  count--;
	}
      }
      
    }
  } else {
    SDL_Rect dst;
    /* down */
    count = active * step;
    
    /*
       find end
       B = pBuf
       A - start (pBeginActiveWidgetLIST)
       [ ] [ ] [ ]
       -----------
       [A] [ ] [ ]
       [ ] [ ] [ ]
       -----------
       [B] [ ] [ ]
    */
    while (count && pBuf != pBeginWidgetLIST->prev) {
      pBuf = pBuf->prev;
      count--;
    }

    if (!count && pBuf != pBeginWidgetLIST->prev) {
      /*
       move pointers to positions and unhidde scrolled widgets
       B = pBuf
       T = pTmp
       A - start (pBeginActiveWidgetLIST)
       [ ] [ ] [ ]
       -----------
       [A] [ ] [ ]
       [ ] [ ] [T]
       -----------
       [ ] [ ] [B]
    */
      pTmp = pBuf->next;
      count_step = step - 1;
      while(count_step && pBuf != pBeginWidgetLIST) {
	clear_wflag(pBuf, WF_HIDDEN);
	pBuf = pBuf->prev;
	count_step--;
      }
      clear_wflag(pBuf, WF_HIDDEN);

      /*
       correct pTmp and undraw empty fields
       B = pBuf
       T = pTmp
       A - start (pBeginActiveWidgetLIST)
       [ ] [ ] [ ]
       -----------
       [A] [ ] [ ]
       [ ] [T] [ ]
       -----------
       [ ] [B]
    */
      count = count_step;
      while(count) {
	dst = pTmp->size;
	SDL_BlitSurface(pTmp->gfx, NULL, pTmp->dst, &dst);
	sdl_dirty_rect(dst);
	FREESURFACE(pTmp->gfx);
	pTmp = pTmp->next;
	count--;
      }
      
      /* reset counters */
      count = active;
      if(count_step) {
        count_step = step - count_step;
      } else {
	count_step = step;
      }
      
      /*
       scroll pointers down
       B = pBuf
       T = pTmp
       [  ] [  ] [  ]
       -----------
       [  ] [  ] [  ]
       [T2] [T1] [T0]   => B position = T position
       -----------
       [B2] [B1] [B0]
    */
      while (count) {
	pBuf->gfx = pTmp->gfx;
	pBuf->size.y = pTmp->size.y;
	pBuf->size.x = pTmp->size.x;
        pTmp->gfx = NULL;
	if(count == 1) {
	  set_wflag(pTmp, WF_HIDDEN);
	}
	pTmp = pTmp->next;
	pBuf = pBuf->next;
	count_step--;
	if(!count_step) {
	  count_step = step;
	  count--;
	}
      }
      /* setup new ActiveWidget pointer */
      pBegin = pBuf->prev;
    }
  }

  return pBegin;
}


static inline int get_step(struct ScrollBar *pScroll)
{
  float step = pScroll->max - pScroll->min;
  step *= (float) (1.0 - (float) (pScroll->active * pScroll->step) /
						  (float)pScroll->count);
  step /= (float)(pScroll->count - pScroll->active * pScroll->step);
  step *= (float)pScroll->step;
  step++;
  return (int)step;
}

static int get_position(struct ADVANCED_DLG *pDlg)
{
  struct GUI *pBuf = pDlg->pActiveWidgetList;
  int count = pDlg->pScroll->active * pDlg->pScroll->step - 1;
  int step = get_step(pDlg->pScroll);
  
  /* find last seen widget */
  while(count) {
    if(pBuf == pDlg->pBeginActiveWidgetList) {
      break;
    }
    count--;
    pBuf = pBuf->prev;
  }
  
  count = 0;
  if(pBuf != pDlg->pBeginActiveWidgetList) {
    do {
      count++;
      pBuf = pBuf->prev;
    } while (pBuf != pDlg->pBeginActiveWidgetList);
  }
 
  return pDlg->pScroll->max - pDlg->pScroll->pScrollBar->size.h -
					count * (float)step / pDlg->pScroll->step;
}

/**************************************************************************
  ...
**************************************************************************/
struct GUI *down_scroll_widget_list(struct ScrollBar *pVscroll,
				    struct GUI *pBeginActiveWidgetLIST,
				    struct GUI *pBeginWidgetLIST,
				    struct GUI *pEndWidgetLIST)
{

  struct GUI *pBegin = pBeginActiveWidgetLIST;
  struct GUI *pEnd = pBeginActiveWidgetLIST;
  int step = pVscroll->active * pVscroll->step - 1;

  while (step--) {
    pEnd = pEnd->prev;
  }

  step = get_step(pVscroll);
  
  do {
    if (pEnd != pBeginWidgetLIST) {
      if (pVscroll->pScrollBar &&
	  pVscroll->pScrollBar->size.y <
	  pVscroll->max - pVscroll->pScrollBar->size.h) {

	/* draw bcgd */
	blit_entire_src(pVscroll->pScrollBar->gfx, pVscroll->pScrollBar->dst,
			pVscroll->pScrollBar->size.x,
			pVscroll->pScrollBar->size.y);

	sdl_dirty_rect(pVscroll->pScrollBar->size);

	if (pVscroll->pScrollBar->size.y + step >
	    pVscroll->max - pVscroll->pScrollBar->size.h) {
	  pVscroll->pScrollBar->size.y =
	      pVscroll->max - pVscroll->pScrollBar->size.h;
	} else {
	  pVscroll->pScrollBar->size.y += step;
	}
      }

      pBegin = vertical_scroll_widget_list(pBegin,
			  pBeginWidgetLIST, pEndWidgetLIST,
			  pVscroll->active, pVscroll->step, 1);

      pEnd = pEnd->prev;

      redraw_group(pBeginWidgetLIST, pEndWidgetLIST, TRUE);
  
      if (pVscroll->pScrollBar) {
	/* redraw scroolbar */
	refresh_widget_background(pVscroll->pScrollBar);
	redraw_vert(pVscroll->pScrollBar);
	sdl_dirty_rect(pVscroll->pScrollBar->size);
      }

      flush_dirty();

    }
    
    SDL_PollEvent(&Main.event);
  } while (Main.event.type != SDL_MOUSEBUTTONUP);

  return pBegin;
}

/**************************************************************************
  ...
**************************************************************************/
struct GUI *up_scroll_widget_list(struct ScrollBar *pVscroll,
				  struct GUI *pBeginActiveWidgetLIST,
				  struct GUI *pBeginWidgetLIST,
				  struct GUI *pEndWidgetLIST)
{
  struct GUI *pBegin = pBeginActiveWidgetLIST;
  int step = get_step(pVscroll);

  while (1) {
    if (pBegin != pEndWidgetLIST) {

      if (pVscroll->pScrollBar &&
	(pVscroll->pScrollBar->size.y > pVscroll->min)) {

	/* draw bcgd */
	blit_entire_src(pVscroll->pScrollBar->gfx, pVscroll->pScrollBar->dst,
			pVscroll->pScrollBar->size.x,
			pVscroll->pScrollBar->size.y);
	sdl_dirty_rect(pVscroll->pScrollBar->size);

        if (((pVscroll->pScrollBar->size.y - step) < pVscroll->min)) {
	  pVscroll->pScrollBar->size.y = pVscroll->min;
	} else {
	  pVscroll->pScrollBar->size.y -= step;
	}
      }

      pBegin = vertical_scroll_widget_list(pBegin,
			pBeginWidgetLIST, pEndWidgetLIST,
			pVscroll->active, pVscroll->step, -1);

      redraw_group(pBeginWidgetLIST, pEndWidgetLIST, TRUE);

      if (pVscroll->pScrollBar) {
	/* redraw scroolbar */
	refresh_widget_background(pVscroll->pScrollBar);
	redraw_vert(pVscroll->pScrollBar);
	sdl_dirty_rect(pVscroll->pScrollBar->size);
      }

      flush_dirty();
    }

    if(SDL_PollEvent(&Main.event) == 1 && Main.event.type == SDL_MOUSEBUTTONUP) {
        break;
    }
  }

  return pBegin;
}

/**************************************************************************
  FIXME
**************************************************************************/
struct GUI *vertic_scroll_widget_list(struct ScrollBar *pVscroll,
				      struct GUI *pBeginActiveWidgetLIST,
				      struct GUI *pBeginWidgetLIST,
				      struct GUI *pEndWidgetLIST)
{
  struct GUI *pBegin = pBeginActiveWidgetLIST;
  int ret = 1;
  div_t tmp;
  int count, old_y;
  int step = get_step(pVscroll);

  old_y = pVscroll->pScrollBar->size.y;

  while (ret) {
    SDL_WaitEvent(&Main.event);
    switch (Main.event.type) {
    case SDL_MOUSEBUTTONUP:
      ret = 0;
      break;
    case SDL_MOUSEMOTION:
      if ((Main.event.motion.yrel) &&
	  (pVscroll->pScrollBar->size.y >= pVscroll->min) &&
	  (pVscroll->pScrollBar->size.y <= pVscroll->max) &&
	  (Main.event.motion.y >= pVscroll->min) &&
	  (Main.event.motion.y <= pVscroll->max)) {

	/* draw bcgd */
	blit_entire_src(pVscroll->pScrollBar->gfx, pVscroll->pScrollBar->dst,
			pVscroll->pScrollBar->size.x,
			pVscroll->pScrollBar->size.y);
	sdl_dirty_rect(pVscroll->pScrollBar->size);


	if ((pVscroll->pScrollBar->size.y + Main.event.motion.yrel) >
	    pVscroll->max - pVscroll->pScrollBar->size.h) {
	  pVscroll->pScrollBar->size.y =
	      pVscroll->max - pVscroll->pScrollBar->size.h;
	} else {
	  if ((pVscroll->pScrollBar->size.y + Main.event.motion.yrel) <
	      pVscroll->min) {
	    pVscroll->pScrollBar->size.y = pVscroll->min;
	  } else {
	    pVscroll->pScrollBar->size.y += Main.event.motion.yrel;
	  }
	}

	tmp = div((pVscroll->pScrollBar->size.y - old_y), step);
	count = tmp.quot;

	if (count) {

	  /* correct div */
	  if (tmp.rem) {
	    if (count > 0) {
	      count++;
	    } else {
	      count--;
	    }
	  }

	  while (count) {

	    pBegin = vertical_scroll_widget_list(pBegin,
				pBeginWidgetLIST, pEndWidgetLIST,
				pVscroll->active, pVscroll->step, count);

	    if (count > 0) {
	      count--;
	    } else {
	      count++;
	    }

	  }			/* while (count) */

	  old_y = pVscroll->pScrollBar->size.y;
	  redraw_group(pBeginWidgetLIST, pEndWidgetLIST, TRUE);
	}

	/* redraw scroolbar */
	refresh_widget_background(pVscroll->pScrollBar);
	redraw_vert(pVscroll->pScrollBar);
	sdl_dirty_rect(pVscroll->pScrollBar->size);

	flush_dirty();
      }				/* if (count) */
      break;
    default:
      break;
    }				/* switch */
  }				/* while */

  return pBegin;
}


/* ==================================================================== */

/**************************************************************************
  Add new widget to srolled list and set draw position of all changed widgets.
  dir :
    TRUE - upper add => pAdd_Dock->next = pNew_Widget.
    FALSE - down add => pAdd_Dock->prev = pNew_Widget.
  start_x, start_y - positions of first seen widget (pActiveWidgetList).
  pDlg->pScroll ( scrollbar ) must exist.
  It isn't full secure to multi widget list.
**************************************************************************/
bool add_widget_to_vertical_scroll_widget_list(struct ADVANCED_DLG *pDlg,
				      struct GUI *pNew_Widget,
				      struct GUI *pAdd_Dock, bool dir,
					Sint16 start_x, Sint16 start_y)
{
  struct GUI *pBuf = NULL;
  struct GUI *pEnd = NULL,*pOld_End = NULL;
  int count = 0;
  bool last = FALSE;
  assert(pNew_Widget != NULL);
  assert(pDlg != NULL);
  assert(pDlg->pScroll != NULL);
  
  if(!pAdd_Dock) {
    pAdd_Dock = pDlg->pBeginWidgetList;
  }
  
  pDlg->pScroll->count++;
  
  if(pDlg->pScroll->count > pDlg->pScroll->active * pDlg->pScroll->step) {
    if(pDlg->pActiveWidgetList) {
      /* find last active widget */
      pOld_End = pAdd_Dock;
      while(pOld_End != pDlg->pActiveWidgetList) {
        pOld_End = pOld_End->next;
      }
      count = pDlg->pScroll->active * pDlg->pScroll->step - 1;
      while(count) {
	pOld_End = pOld_End->prev;
	count--;
      }
      if(pOld_End == pAdd_Dock) {
	last = TRUE;
      }
    } else {
      last = TRUE;
      pDlg->pActiveWidgetList = pDlg->pEndActiveWidgetList;
      show_scrollbar(pDlg->pScroll);
    }
  }

  count = 0;
  /* add Pointer to list */
  if(dir) {
    /* upper add */
    UpperAdd(pNew_Widget, pAdd_Dock);
    
    if(pAdd_Dock == pDlg->pEndWidgetList) {
      pDlg->pEndWidgetList = pNew_Widget;
    }
    if(pAdd_Dock == pDlg->pEndActiveWidgetList) {
      pDlg->pEndActiveWidgetList = pNew_Widget;
    }
    if(pAdd_Dock == pDlg->pActiveWidgetList) {
      pDlg->pActiveWidgetList = pNew_Widget;
    }
  } else {
    /* down add */
    DownAdd(pNew_Widget, pAdd_Dock);
        
    if(pAdd_Dock == pDlg->pBeginWidgetList) {
      pDlg->pBeginWidgetList = pNew_Widget;
    }
    
    if(pAdd_Dock == pDlg->pBeginActiveWidgetList) {
      pDlg->pBeginActiveWidgetList = pNew_Widget;
    }
  }
  
  /* setup draw positions */
  if(!pDlg->pBeginActiveWidgetList) {
    /* first element ( active list empty ) */
    if(dir) {
      freelog(LOG_FATAL, "Forbided List Operation");
      abort();
    }
    pNew_Widget->size.x = start_x;
    pNew_Widget->size.y = start_y;
    pDlg->pBeginActiveWidgetList = pNew_Widget;
    pDlg->pEndActiveWidgetList = pNew_Widget;
    if(!pDlg->pBeginWidgetList) {
      pDlg->pBeginWidgetList = pNew_Widget;
      pDlg->pEndWidgetList = pNew_Widget;
    }
  } else { /* there are some elements on local active list */
    if(last) {
      /* We add to last seen position */
      if(dir) {
	/* only swap pAdd_Dock with pNew_Widget on last seen positions */
	pNew_Widget->size.x = pAdd_Dock->size.x;
	pNew_Widget->size.y = pAdd_Dock->size.y;
	pNew_Widget->gfx = pAdd_Dock->gfx;
	pAdd_Dock->gfx = NULL;
	set_wflag(pAdd_Dock, WF_HIDDEN);
      } else {
	/* repositon all widgets */
	pBuf = pNew_Widget;
        do {
	  pBuf->size.x = pBuf->next->size.x;
	  pBuf->size.y = pBuf->next->size.y;
	  pBuf->gfx = pBuf->next->gfx;
	  pBuf->next->gfx = NULL;
	  pBuf = pBuf->next;
        } while(pBuf != pDlg->pActiveWidgetList);
        pBuf->gfx = NULL;
        set_wflag(pBuf, WF_HIDDEN);
	pDlg->pActiveWidgetList = pDlg->pActiveWidgetList->prev;
      }
    } else {
      pBuf = pNew_Widget;
      /* find last seen widget */
      if(pDlg->pActiveWidgetList) {
	pEnd = pDlg->pActiveWidgetList;
	count = pDlg->pScroll->active * pDlg->pScroll->step - 1;
        while(count && pEnd != pDlg->pBeginActiveWidgetList) {
	  pEnd = pEnd->prev;
	  count--;
        }
      }
      while(pBuf) {
        if(pBuf == pDlg->pBeginActiveWidgetList) {
	  struct GUI *pTmp = pBuf;
	  count = pDlg->pScroll->step;
	  while(count) {
	    pTmp = pTmp->next;
	    count--;
	  }
	  pBuf->size.x = pTmp->size.x;
	  pBuf->size.y = pTmp->size.y + pTmp->size.h;
	  /* break when last active widget or last seen widget */
	  break;
        } else {
	  pBuf->size.x = pBuf->prev->size.x;
	  pBuf->size.y = pBuf->prev->size.y;
	  pBuf->gfx = pBuf->prev->gfx;
	  pBuf->prev->gfx = NULL;
	  if(pBuf == pEnd) {
	    break;
	  } 
        }
        pBuf = pBuf->prev;
      }
      if(pOld_End && pBuf->prev == pOld_End) {
        set_wflag(pOld_End, WF_HIDDEN);
      }
    }
  }

  if(pDlg->pActiveWidgetList) {
    pDlg->pScroll->pScrollBar->size.h = scrollbar_size(pDlg->pScroll);
    if(last) {
      pDlg->pScroll->pScrollBar->size.y = get_position(pDlg);
    }
  }

  return last;  
}

/**************************************************************************
  Del widget from srolled list and set draw position of all changed widgets
  Don't free pDlg and pDlg->pScroll (if exist)
  It is full secure to multi widget list.
**************************************************************************/
void del_widget_from_vertical_scroll_widget_list(struct ADVANCED_DLG *pDlg, 
  						struct GUI *pWidget)
{
  int count = 0;
  struct GUI *pBuf = pWidget;
  assert(pWidget != NULL);
  assert(pDlg != NULL);
  
  /* if begin == end -> size = 1 */
  if (pDlg->pBeginActiveWidgetList ==
      pDlg->pEndActiveWidgetList) {
    pDlg->pBeginActiveWidgetList = NULL;
    pDlg->pActiveWidgetList = NULL;
    pDlg->pEndActiveWidgetList = NULL;
    if(pDlg->pScroll) {
      pDlg->pScroll->count = 0;
      if(!pDlg->pScroll->pUp_Left_Button && 
	!pDlg->pScroll->pScrollBar &&
        !pDlg->pScroll->pDown_Right_Button) {
        pDlg->pBeginWidgetList = NULL;
        pDlg->pEndWidgetList = NULL;
      }
    } else {
      pDlg->pBeginWidgetList = NULL;
      pDlg->pEndWidgetList = NULL;
    }
    SDL_BlitSurface(pWidget->gfx, NULL, pWidget->dst, &pWidget->size);
    sdl_dirty_rect(pWidget->size);
    del_widget_from_gui_list(pWidget);
    return;
  }
    
  if (pDlg->pScroll && pDlg->pActiveWidgetList) {
    /* scrollbar exist and active, start mod. from last seen label */
    
    struct GUI *pLast;
      
    /* this is always true becouse no-scrolbar case (active*step < count)
       will be suported in other part of code (see "else" part) */
    count = pDlg->pScroll->active * pDlg->pScroll->step;
        
    /* find last */
    pBuf = pDlg->pActiveWidgetList;
    while (count) {
      pBuf = pBuf->prev;
      count--;
    }

    if(!pBuf) {
      pLast = pDlg->pBeginActiveWidgetList;
    } else {
      pLast = pBuf->next;
    }
    
    if(pLast == pDlg->pBeginActiveWidgetList) {
      if(pDlg->pScroll->step == 1) {
        pBuf = pDlg->pActiveWidgetList->next;
        pDlg->pActiveWidgetList = pBuf;
        clear_wflag(pBuf, WF_HIDDEN);
	while (pBuf != pWidget) {
          pBuf->gfx = pBuf->prev->gfx;
          pBuf->prev->gfx = NULL;
          pBuf->size.x = pBuf->prev->size.x;
          pBuf->size.y = pBuf->prev->size.y;
          pBuf = pBuf->prev;
        }
      } else {
	pBuf = pLast;
	/* undraw last widget */
        blit_entire_src(pBuf->gfx, pBuf->dst,
				    pBuf->size.x, pBuf->size.y);
        sdl_dirty_rect(pBuf->size);
        FREESURFACE(pBuf->gfx);
	goto STD;
      }  
    } else {
      clear_wflag(pBuf, WF_HIDDEN);
STD:  while (pBuf != pWidget) {
        pBuf->gfx = pBuf->next->gfx;
        pBuf->next->gfx = NULL;
        pBuf->size.x = pBuf->next->size.x;
        pBuf->size.y = pBuf->next->size.y;
        pBuf = pBuf->next;
      }
    }
    
    if((pDlg->pScroll->count - 1 <=
	      (pDlg->pScroll->active * pDlg->pScroll->step))) {
      hide_scrollbar(pDlg->pScroll);
      pDlg->pActiveWidgetList = NULL;
    }
    pDlg->pScroll->count--;
    
    if(pDlg->pActiveWidgetList) {
      pDlg->pScroll->pScrollBar->size.h = scrollbar_size(pDlg->pScroll);
    }
  } else { /* no scrollbar */
    pBuf = pDlg->pBeginActiveWidgetList;
    
    /* undraw last widget */
    blit_entire_src(pBuf->gfx, pBuf->dst,
				    pBuf->size.x, pBuf->size.y);
    sdl_dirty_rect(pBuf->size);
    FREESURFACE(pBuf->gfx);

    while (pBuf != pWidget) {
      pBuf->gfx = pBuf->next->gfx;
      pBuf->next->gfx = NULL;
      pBuf->size.x = pBuf->next->size.x;
      pBuf->size.y = pBuf->next->size.y;
      pBuf = pBuf->next;
    }

    if (pDlg->pScroll) {
      pDlg->pScroll->count--;
    }
        
  }

  if (pWidget == pDlg->pBeginActiveWidgetList) {
    pDlg->pBeginActiveWidgetList = pWidget->next;
  }

  if (pWidget == pDlg->pBeginWidgetList) {
    pDlg->pBeginWidgetList = pWidget->next;
  }
  
  if (pWidget == pDlg->pEndActiveWidgetList) {

    if (pDlg->pActiveWidgetList == pWidget) {
      pDlg->pActiveWidgetList = pWidget->prev;
    }

    if (pWidget == pDlg->pEndWidgetList) {
      pDlg->pEndWidgetList = pWidget->prev;
    }
    
    pDlg->pEndActiveWidgetList = pWidget->prev;

  }

  if (pDlg->pActiveWidgetList && pDlg->pActiveWidgetList == pWidget) {
    pDlg->pActiveWidgetList = pWidget->prev;
  }

  del_widget_from_gui_list(pWidget);
  
  if(pDlg->pScroll && pDlg->pActiveWidgetList) {
    pDlg->pScroll->pScrollBar->size.y = get_position(pDlg);
  }
  
}

/**************************************************************************
  			Vertical ScrollBar
**************************************************************************/

/**************************************************************************
  ...
**************************************************************************/
static int std_up_advanced_dlg(struct GUI *pWidget)
{
  struct ADVANCED_DLG *pDlg = (struct ADVANCED_DLG *)pWidget->data;
  struct GUI *pBegin = up_scroll_widget_list(
			pDlg->pScroll,
			pDlg->pActiveWidgetList,
			pDlg->pBeginActiveWidgetList,
			pDlg->pEndActiveWidgetList);

  if (pBegin) {
    pDlg->pActiveWidgetList = pBegin;
  }
  
  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  redraw_tibutton(pWidget);
  flush_rect(pWidget->size);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int std_down_advanced_dlg(struct GUI *pWidget)
{
  struct ADVANCED_DLG *pDlg = (struct ADVANCED_DLG *)pWidget->data;
  struct GUI *pBegin = down_scroll_widget_list(
			pDlg->pScroll,
			pDlg->pActiveWidgetList,
			pDlg->pBeginActiveWidgetList,
			pDlg->pEndActiveWidgetList);

  if (pBegin) {
    pDlg->pActiveWidgetList = pBegin;
  }

  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  redraw_tibutton(pWidget);
  flush_rect(pWidget->size);
  return -1;
}

/**************************************************************************
  FIXME : fix main funct : vertic_scroll_widget_list(...)
**************************************************************************/
static int std_vscroll_advanced_dlg(struct GUI *pScrollBar)
{
  struct ADVANCED_DLG *pDlg = (struct ADVANCED_DLG *)pScrollBar->data;
  struct GUI *pBegin = vertic_scroll_widget_list(
			pDlg->pScroll,
			pDlg->pActiveWidgetList,
			pDlg->pBeginActiveWidgetList,
			pDlg->pEndActiveWidgetList);

  if (pBegin) {
    pDlg->pActiveWidgetList = pBegin;
  }
  unsellect_widget_action();
  set_wstate(pScrollBar, WS_SELLECTED);
  pSellected_Widget = pScrollBar;
  redraw_vert(pScrollBar);
  flush_rect(pScrollBar->size);
  return -1;
}


/**************************************************************************
  ...
**************************************************************************/
Uint32 create_vertical_scrollbar(struct ADVANCED_DLG *pDlg,
		  Uint8 step, Uint8 active,
		  bool create_scrollbar, bool create_buttons)
{
  Uint16 count = 0;
  struct GUI *pBuf = NULL, *pWindow = NULL;
    
  assert(pDlg != NULL);
  
  pWindow = pDlg->pEndWidgetList;
  
  if(!pDlg->pScroll) {
    pDlg->pScroll = MALLOC(sizeof(struct ScrollBar));
    
    pDlg->pScroll->active = active;
    pDlg->pScroll->step = step;
    
    pBuf = pDlg->pEndActiveWidgetList;
    while(pBuf && pBuf != pDlg->pBeginActiveWidgetList->prev) {
      pBuf = pBuf->prev;
      count++;
    }
  
    pDlg->pScroll->count = count;
  }
  
  if(create_buttons) {
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->data = (void *)pDlg;
    pBuf->action = std_up_advanced_dlg;
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    set_wstate(pBuf, WS_NORMAL);
    
    pDlg->pScroll->pUp_Left_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
    count = pBuf->size.w;
    
    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->data = (void *)pDlg;
    pBuf->action = std_down_advanced_dlg;
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    set_wstate(pBuf, WS_NORMAL);
    
    pDlg->pScroll->pDown_Right_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
  }
  
  if(create_scrollbar) {
    /* create vsrollbar */
    pBuf = create_vertical(pTheme->Vertic, pWindow->dst,
				10, WF_DRAW_THEME_TRANSPARENT);
    
    pBuf->ID = ID_SCROLLBAR;
    pBuf->data = (void *)pDlg;
    pBuf->action = std_vscroll_advanced_dlg;
    set_wstate(pBuf, WS_NORMAL);
  
    pDlg->pScroll->pScrollBar = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
    if(!count) {
      count = pBuf->size.w;
    }
  }
  
  return count;
}

/**************************************************************************
  ...
**************************************************************************/
void setup_vertical_scrollbar_area(struct ScrollBar *pScroll,
	Sint16 start_x, Sint16 start_y, Uint16 hight, bool swap_start_x)
{
  bool buttons_exist;
  
  assert(pScroll != NULL);
  
  buttons_exist = (pScroll->pDown_Right_Button && pScroll->pUp_Left_Button);
  
  if(buttons_exist) {
    /* up */
    pScroll->pUp_Left_Button->size.y = start_y;
    if(swap_start_x) {
      pScroll->pUp_Left_Button->size.x = start_x - 
    					pScroll->pUp_Left_Button->size.w;
    } else {
      pScroll->pUp_Left_Button->size.x = start_x;
    }
    pScroll->min = start_y + pScroll->pUp_Left_Button->size.h;
    /* -------------------------- */
    /* down */
    pScroll->pDown_Right_Button->size.y = start_y + hight -
				  pScroll->pDown_Right_Button->size.h;
    if(swap_start_x) {
      pScroll->pDown_Right_Button->size.x = start_x -
				pScroll->pDown_Right_Button->size.w;
    } else {
      pScroll->pDown_Right_Button->size.x = start_x;
    }
    pScroll->max = pScroll->pDown_Right_Button->size.y;
  }
  /* --------------- */
  /* scrollbar */
  if (pScroll->pScrollBar) {
    
    if(swap_start_x) {
      pScroll->pScrollBar->size.x = start_x - pScroll->pScrollBar->size.w + 2;
    } else {
      pScroll->pScrollBar->size.x = start_x;
    } 

    if(buttons_exist) {
      pScroll->pScrollBar->size.y = start_y +
				      pScroll->pUp_Left_Button->size.h;
      if(pScroll->count > pScroll->active * pScroll->step) {
	pScroll->pScrollBar->size.h = scrollbar_size(pScroll);
      } else {
	pScroll->pScrollBar->size.h = pScroll->max - pScroll->min;
      }
    } else {
      pScroll->pScrollBar->size.y = start_y;
      pScroll->pScrollBar->size.h = hight;
      pScroll->min = start_y;
      pScroll->max = start_y + hight;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void setup_vertical_vidgets_position(int step,
	Sint16 start_x, Sint16 start_y, Uint16 w, Uint16 h,
				struct GUI *pBegin, struct GUI *pEnd)
{
  struct GUI *pBuf = pEnd;
  register int count = 0;
  register int real_start_x = start_x;
    
  while(pBuf) {
    pBuf->size.x = real_start_x;
    pBuf->size.y = start_y;
    
    real_start_x += pBuf->size.w;
    
    if(w) {
      pBuf->size.w = w;
    }
    
    if(h) {
      pBuf->size.h = h;
    }
    
    if(!((count + 1) % step)) {
      real_start_x = start_x;
      start_y += pBuf->size.h;
    }
    
    if(pBuf == pBegin) {
      break;
    }
    count++;
    pBuf = pBuf->prev;
  }
}


/**************************************************************************
  ...
**************************************************************************/
void setup_vertical_scrollbar_default_callbacks(struct ScrollBar *pScroll)
{
  assert(pScroll != NULL);
  if(pScroll->pUp_Left_Button) {
    pScroll->pUp_Left_Button->action = std_up_advanced_dlg;
  }
  if(pScroll->pDown_Right_Button) {
    pScroll->pDown_Right_Button->action = std_down_advanced_dlg;
  }
  if(pScroll->pScrollBar) {
    pScroll->pScrollBar->action = std_vscroll_advanced_dlg;
  }
}

/**************************************************************************
  			Horizontal Scrollbar
**************************************************************************/


/**************************************************************************
  ...
**************************************************************************/
Uint32 create_horizontal_scrollbar(struct ADVANCED_DLG *pDlg,
		  Sint16 start_x, Sint16 start_y, Uint16 width, Uint16 active,
		  bool create_scrollbar, bool create_buttons, bool swap_start_y)
{
  Uint16 count = 0;
  struct GUI *pBuf = NULL, *pWindow = NULL;
    
  assert(pDlg != NULL);
  
  pWindow = pDlg->pEndWidgetList;
  
  if(!pDlg->pScroll) {
    pDlg->pScroll = MALLOC(sizeof(struct ScrollBar));
    
    pDlg->pScroll->active = active;
    
    pBuf = pDlg->pEndActiveWidgetList;
    while(pBuf && pBuf != pDlg->pBeginActiveWidgetList->prev) {
      pBuf = pBuf->prev;
      count++;
    }
  
    pDlg->pScroll->count = count;
  }
  
  if(create_buttons) {
    /* create up button */
    pBuf = create_themeicon_button(pTheme->LEFT_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->data = (void *)pDlg;
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    set_wstate(pBuf, WS_NORMAL);
    
    pBuf->size.x = start_x;
    if(swap_start_y) {
      pBuf->size.y = start_y - pBuf->size.h;
    } else {
      pBuf->size.y = start_y;
    }
    
    pDlg->pScroll->min = start_x + pBuf->size.w;
    pDlg->pScroll->pUp_Left_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
    
    count = pBuf->size.h;
    
    /* create down button */
    pBuf = create_themeicon_button(pTheme->RIGHT_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->data = (void *)pDlg;
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    set_wstate(pBuf, WS_NORMAL);
    
    pBuf->size.x = start_x + width - pBuf->size.w;
    if(swap_start_y) {
      pBuf->size.y = start_y - pBuf->size.h;
    } else {
      pBuf->size.y = start_y;
    }
    
    pDlg->pScroll->max = pBuf->size.x;
    pDlg->pScroll->pDown_Right_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
  }
  
  if(create_scrollbar) {
    /* create vsrollbar */
    pBuf = create_horizontal(pTheme->Horiz, pWindow->dst,
				width, WF_DRAW_THEME_TRANSPARENT);
    
    pBuf->ID = ID_SCROLLBAR;
    pBuf->data = (void *)pDlg;
    set_wstate(pBuf, WS_NORMAL);
     
    if(swap_start_y) {
      pBuf->size.y = start_y - pBuf->size.h;
    } else {
      pBuf->size.y = start_y;
    } 

    if(create_buttons) {
      pBuf->size.x = start_x + pDlg->pScroll->pUp_Left_Button->size.w;
      if(pDlg->pScroll->count > pDlg->pScroll->active) {
	pBuf->size.w = scrollbar_size(pDlg->pScroll);
      } else {
	pBuf->size.w = pDlg->pScroll->max - pDlg->pScroll->min;
      }
    } else {
      pBuf->size.x = start_x;
      pDlg->pScroll->min = start_x;
      pDlg->pScroll->max = start_x + width;
    }
    
    pDlg->pScroll->pScrollBar = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
    if(!count) {
      count = pBuf->size.h;
    }
    
  }
  
  return count;
}


/* =================================================== */
/* ===================== ICON ======================== */
/* =================================================== */

/**************************************************************************
  set new theme and callculate new size.
**************************************************************************/
void set_new_icon_theme(struct GUI *pIcon_Widget, SDL_Surface * pNew_Theme)
{
  if ((pNew_Theme) && (pIcon_Widget)) {
    FREESURFACE(pIcon_Widget->theme);
    pIcon_Widget->theme = pNew_Theme;
    pIcon_Widget->size.w = pNew_Theme->w / 4;
    pIcon_Widget->size.h = pNew_Theme->h;
  }
}

/**************************************************************************
  Ugly hack to create 4-state icon theme from static icon;
**************************************************************************/
SDL_Surface *create_icon_theme_surf(SDL_Surface * pIcon)
{
  Uint32 color;
  SDL_Color RGB_Color = { 255, 255, 255, 128 };
  SDL_Rect dest, src = get_smaller_surface_rect(pIcon);
  SDL_Surface *pTheme = create_surf((src.w + 4) * 4, src.h + 4,
				    SDL_SWSURFACE);

  dest.x = 2;
  dest.y = (pTheme->h - src.h) / 2;

  /* normal */
  SDL_BlitSurface(pIcon, &src, pTheme, &dest);

  /* sellected */
  dest.x += (src.w + 4);
  SDL_BlitSurface(pIcon, &src, pTheme, &dest);
  /* draw sellect frame */
  color = SDL_MapRGB(pTheme->format, 254, 254, 254);
  putframe(pTheme, dest.x - 1, dest.y - 1, dest.x + (src.w),
	   dest.y + src.h, color);

  /* pressed */
  dest.x += (src.w + 4);
  SDL_BlitSurface(pIcon, &src, pTheme, &dest);
  /* draw sellect frame */
  putframe(pTheme, dest.x - 1, dest.y - 1, dest.x + src.w,
	   dest.y + src.h, color);
  /* draw press frame */
  color = SDL_MapRGB(pTheme->format, 246, 254, 2);
  putframe(pTheme, dest.x - 2, dest.y - 2, dest.x + (src.w + 1),
	   dest.y + (src.h + 1), color);

  /* disabled */
  dest.x += (src.w + 4);
  SDL_BlitSurface(pIcon, &src, pTheme, &dest);
  dest.w = src.w;
  dest.h = src.h;

  SDL_FillRectAlpha(pTheme, &dest, &RGB_Color);

  SDL_SetColorKey(pTheme, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  return pTheme;
}

/**************************************************************************
  Create ( malloc ) Icon Widget ( flat Button )
**************************************************************************/
struct GUI * create_themeicon(SDL_Surface *pIcon_theme, SDL_Surface *pDest,
							  Uint32 flags)
{
  struct GUI *pIcon_Widget = MALLOC(sizeof(struct GUI));

  pIcon_Widget->theme = pIcon_theme;

  set_wflag(pIcon_Widget, (WF_FREE_STRING | flags));
  set_wstate(pIcon_Widget, WS_DISABLED);
  set_wtype(pIcon_Widget, WT_ICON);
  pIcon_Widget->mod = KMOD_NONE;
  pIcon_Widget->dst = pDest;
  
  if (pIcon_theme) {
    pIcon_Widget->size.w = pIcon_theme->w / 4;
    pIcon_Widget->size.h = pIcon_theme->h;
  }

  return pIcon_Widget;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_icon(struct GUI *pIcon, Sint16 start_x, Sint16 start_y)
{
  pIcon->size.x = start_x;
  pIcon->size.y = start_y;

  if (get_wflags(pIcon) & WF_DRAW_THEME_TRANSPARENT) {
    refresh_widget_background(pIcon);
  }

  return draw_icon_from_theme(pIcon->theme, get_wstate(pIcon), pIcon->dst,
			      pIcon->size.x, pIcon->size.y);
}

/**************************************************************************
  ...
**************************************************************************/
int real_redraw_icon(struct GUI *pIcon)
{
  SDL_Rect src, area = pIcon->size;

  if (!pIcon->theme) {
    return -3;
  }

  if (pIcon->gfx) {
    SDL_BlitSurface(pIcon->gfx, NULL, pIcon->dst, &area);
  }

  src.x = (pIcon->theme->w / 4) * (Uint8) (get_wstate(pIcon));
  src.y = 0;
  src.w = (pIcon->theme->w / 4);
  src.h = pIcon->theme->h;

  if (pIcon->size.w != src.w) {
    area.x += (pIcon->size.w - src.w) / 2;
  }

  if (pIcon->size.h != src.h) {
    area.y += (pIcon->size.h - src.h) / 2;
  }

  return SDL_BlitSurface(pIcon->theme, &src, pIcon->dst, &area);
}

/**************************************************************************
  Blit Icon image to pDest(ination) on positon
  start_x , start_y. WARRING: pDest must exist.

  Graphic is taken from pIcon_theme surface.

  Type of Icon depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled

  Function return: -3 if pIcon_theme is NULL. 
  std return of SDL_BlitSurface(...) function.
**************************************************************************/
int draw_icon_from_theme(SDL_Surface * pIcon_theme, Uint8 state,
			 SDL_Surface * pDest, Sint16 start_x, Sint16 start_y)
{
  SDL_Rect src, des = {start_x, start_y, 0, 0};

  if (!pIcon_theme) {
    return -3;
  }
  /* src.x = 0 + pIcon_theme->w/4 * state */
  src.x = 0 + (pIcon_theme->w / 4) * state;
  src.y = 0;
  src.w = pIcon_theme->w / 4;
  src.h = pIcon_theme->h;
  return SDL_BlitSurface(pIcon_theme, &src, pDest, &des);
}

/**************************************************************************
  Create Icon image then return pointer to this image.
  Transparent color is 0.

  Graphic is taken from pIcon_theme surface and blit to new created image.

  Type of Icon depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled

  Function return NULL if pIcon_theme is NULL or blit fail. 
**************************************************************************/
SDL_Surface *create_icon_from_theme(SDL_Surface * pIcon_theme, Uint8 state)
{
  SDL_Surface *pIcon = NULL;
  SDL_Rect src;
  int w, h;

  if (!pIcon_theme) {
    return pIcon;
  }
  w = pIcon_theme->w / 4;
  h = pIcon_theme->h;
  pIcon = create_surf(w, h, SDL_SWSURFACE);
  src.x = 0 + w * state;
  src.y = 0;
  src.w = w;
  src.h = h;
  if (SDL_BlitSurface(pIcon_theme, &src, pIcon, NULL) != 0) {
    FREESURFACE(pIcon);
  }
  SDL_SetColorKey(pIcon, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  return pIcon;
}

/* =================================================== */
/* ===================== ICON2 ======================== */
/* =================================================== */

/**************************************************************************
  set new theme and callculate new size.
**************************************************************************/
void set_new_icon2_theme(struct GUI *pIcon_Widget, SDL_Surface *pNew_Theme,
			  bool free_old_theme)
{
  if ((pNew_Theme) && (pIcon_Widget)) {
    if(free_old_theme) {
      FREESURFACE(pIcon_Widget->theme);
    }
    pIcon_Widget->theme = pNew_Theme;
    pIcon_Widget->size.w = pNew_Theme->w + 4;
    pIcon_Widget->size.h = pNew_Theme->h + 4;
  }
}

/**************************************************************************
  Create ( malloc ) Icon2 Widget ( flat Button )
**************************************************************************/
struct GUI * create_icon2(SDL_Surface *pIcon, SDL_Surface *pDest, Uint32 flags)
{

  struct GUI *pIcon_Widget = MALLOC(sizeof(struct GUI));

  pIcon_Widget->theme = pIcon;

  set_wflag(pIcon_Widget, (WF_FREE_STRING | flags));
  set_wstate(pIcon_Widget, WS_DISABLED);
  set_wtype(pIcon_Widget, WT_ICON2);
  pIcon_Widget->mod = KMOD_NONE;
  pIcon_Widget->dst = pDest;
  
  if (pIcon) {
    pIcon_Widget->size.w = pIcon->w + 4;
    pIcon_Widget->size.h = pIcon->h + 4;
  }

  return pIcon_Widget;
}

/**************************************************************************
  ...
**************************************************************************/
int real_redraw_icon2(struct GUI *pIcon)
{
  int ret;
  SDL_Rect dest;
  Uint32 state;

  if(!pIcon) {
    return -3;
  }
  
  if(!pIcon->theme) {
    return -4;
  }
  
  state = get_wstate(pIcon);
    
  if (pIcon->gfx) {
    dest.x = pIcon->size.x;
    dest.y = pIcon->size.y;  
    ret = SDL_BlitSurface(pIcon->gfx, NULL, pIcon->dst, &dest);
    if (ret) {
      return ret;
    }
  }

  dest.x = pIcon->size.x;
  dest.y = pIcon->size.y;
  dest.w = pIcon->theme->w;
  dest.h = pIcon->theme->h;

  if (state == WS_SELLECTED) {
    putframe(pIcon->dst, dest.x + 1, dest.y + 1,
	     dest.x + dest.w + 2, dest.y + dest.h + 2,
	     SDL_MapRGB(pIcon->dst->format, 254, 254, 254));
  }

  if (state == WS_PRESSED) {
    putframe(pIcon->dst, dest.x + 1, dest.y + 1,
	     dest.x + dest.w + 2, dest.y + dest.h + 2,
	     SDL_MapRGB(pIcon->dst->format, 254, 254, 254));

    putframe(pIcon->dst, dest.x, dest.y,
	     dest.x + dest.w + 3, dest.y + dest.h + 3,
	     SDL_MapRGB(pIcon->dst->format, 246, 254, 2));
  }

  dest.x += 2;
  dest.y += 2;
  ret = SDL_BlitSurface(pIcon->theme, NULL, pIcon->dst, &dest);
  if (ret) {
    return ret;
  }

  if (state == WS_DISABLED) {
    SDL_Color RGBA_Color = { 255, 255, 255, 128 };
    dest.w = pIcon->theme->w;
    dest.h = pIcon->theme->h;
    SDL_FillRectAlpha(pIcon->dst, &dest, &RGBA_Color);
  }

  return 0;
}

/* =================================================== */
/* ================== ICON BUTTON ==================== */
/* =================================================== */

/**************************************************************************
  Create ( malloc ) Icon (theme)Button Widget structure.

  Icon graphic is taken from 'pIcon' surface (don't change with button
  changes );  Button Theme graphic is taken from pTheme->Button surface;
  Text is taken from 'pString16'.

  This function determinate future size of Button ( width, high ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Button Widget.
**************************************************************************/
struct GUI * create_icon_button(SDL_Surface *pIcon, SDL_Surface *pDest,
			  SDL_String16 *pStr, Uint32 flags)
{
  SDL_Rect buf = {0, 0, 0, 0};
  Uint16 w = 0, h = 0;
  struct GUI *pButton;

  if (!pIcon && !pStr) {
    return NULL;
  }

  pButton = MALLOC(sizeof(struct GUI));

  pButton->theme = pTheme->Button;
  pButton->gfx = pIcon;
  pButton->string16 = pStr;
  set_wflag(pButton, (WF_FREE_STRING | WF_DRAW_FRAME_AROUND_WIDGET | flags));
  set_wstate(pButton, WS_DISABLED);
  set_wtype(pButton, WT_I_BUTTON);
  pButton->mod = KMOD_NONE;
  pButton->dst = pDest;
  
  if (pStr) {
    pButton->string16->style |= SF_CENTER;
    /* if BOLD == true then longest wight */
    if (!(pStr->style & TTF_STYLE_BOLD)) {
      pStr->style |= TTF_STYLE_BOLD;
      buf = str16size(pStr);
      pStr->style &= ~TTF_STYLE_BOLD;
    } else {
      buf = str16size(pStr);
    }

    w = MAX(w, buf.w);
    h = MAX(h, buf.h);
  }

  if (pIcon) {
    if (pStr) {
      if ((flags & WF_ICON_UNDER_TEXT) || (flags & WF_ICON_ABOVE_TEXT)) {
	w = MAX(w, pIcon->w + 2);
	h = MAX(h, buf.h + pIcon->h + 4);
      } else {
	w = MAX(w, buf.w + pIcon->w + 20);
	h = MAX(h, pIcon->h + 2);
      }
    } else {
      w = MAX(w, pIcon->w + 2);
      h = MAX(h, pIcon->h + 2);
    }
  } else {
    w += 10;
    h += 2;
  }

  correct_size_bcgnd_surf(pTheme->Button, &w, &h);

  pButton->size.w = w;
  pButton->size.h = h;

  return pButton;
}

/**************************************************************************
  Create ( malloc ) Theme Icon (theme)Button Widget structure.

  Icon Theme graphic is taken from 'pIcon_theme' surface ( change with
  button changes ); Button Theme graphic is taken from pTheme->Button
  surface; Text is taken from 'pString16'.

  This function determinate future size of Button ( width, high ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Button Widget.
**************************************************************************/
struct GUI * create_themeicon_button(SDL_Surface *pIcon_theme,
		SDL_Surface *pDest, SDL_String16 *pString16, Uint32 flags)
{
  SDL_Surface *pIcon = create_icon_from_theme(pIcon_theme, 1);
  struct GUI *pButton = create_icon_button(pIcon, pDest, pString16, flags);

  FREESURFACE(pButton->gfx);	/* pButton->gfx == pIcon */
  pButton->gfx = pIcon_theme;
  set_wtype(pButton, WT_TI_BUTTON);

  return pButton;
}

/**************************************************************************
  Steate Button image with text and Icon.  Then blit to Main.screen on
  positon start_x , start_y.

  Text with atributes is taken from pButton->string16 parameter.

  Graphic for button is taken from pButton->theme surface and blit to new
  created image.

  Graphic for Icon theme is taken from pButton->gfx surface and blit to
  new created image.

  function return (-1) if there are no Icon and Text.
  Else return 0.
**************************************************************************/
int draw_tibutton(struct GUI *pButton, Sint16 start_x, Sint16 start_y)
{
  pButton->size.x = start_x;
  pButton->size.y = start_y;
  return real_redraw_tibutton(pButton);
}

/**************************************************************************
  Create Button image with text and Icon.
  Then blit to Main.screen on positon start_x , start_y.

   Text with atributes is taken from pButton->string16 parameter.

   Graphic for button is taken from pButton->theme surface 
   and blit to new created image.

  Graphic for Icon is taken from pButton->gfx surface 
  and blit to new created image.

  function return (-1) if there are no Icon and Text.
  Else return 0.
**************************************************************************/
int draw_ibutton(struct GUI *pButton, Sint16 start_x, Sint16 start_y)
{
  pButton->size.x = start_x;
  pButton->size.y = start_y;
  return real_redraw_ibutton(pButton);
}

/**************************************************************************
  Create Icon Button image with text and Icon then blit to Dest(ination)
  on positon pIButton->size.x , pIButton->size.y.
  WARRING: pDest must exist.

  Text with atributes is taken from pIButton->string16 parameter.

  Graphic for button is taken from pIButton->theme surface 
  and blit to new created image.

  Graphic for Icon is taken from pIButton->gfx surface and blit to new
  created image.

  function return (-1) if there are no Icon and Text.
  Else return 0.
**************************************************************************/
int real_redraw_ibutton(struct GUI *pIButton)
{
  SDL_Rect dest = { 0, 0, 0, 0 };
  SDL_String16 TMPString;
  SDL_Surface *pButton = NULL, *pBuf = NULL, *pText = NULL, *pIcon =
      pIButton->gfx;
  Uint16 Ix, Iy, x;
  Uint16 y = 0; /* FIXME: possibly uninitialized */
  int ret;

  if (pIButton->string16) {

    /* make copy of string16 */
    TMPString = *pIButton->string16;

    if (get_wstate(pIButton) == WS_SELLECTED) {
      TMPString.style |= TTF_STYLE_BOLD;
    }

    pText = create_text_surf_from_str16(&TMPString);
  }

  if (!pText && !pIcon) {
    return -1;
  }

  /* create Button graphic */
  if (get_wflags(pIButton) & WF_DRAW_THEME_TRANSPARENT) {
    pBuf =
	create_bcgnd_surf(pIButton->theme, SDL_TRUE, get_wstate(pIButton),
			  pIButton->size.w, pIButton->size.h + 1);
  } else {
    pBuf =
	create_bcgnd_surf(pIButton->theme, SDL_FALSE, get_wstate(pIButton),
			  pIButton->size.w, pIButton->size.h + 1);
  }

  /* make AA on Button Sufrace */
  pButton = ResizeSurface(pBuf, pIButton->size.w, pIButton->size.h, 2);

  FREESURFACE(pBuf);

  dest.x = pIButton->size.x;
  dest.y = pIButton->size.y;
  SDL_BlitSurface(pButton, NULL, pIButton->dst, &dest);
  FREESURFACE(pButton);

  if (pIcon) {			/* Icon */
    if (pText) {
      if (get_wflags(pIButton) & WF_ICON_CENTER_RIGHT) {
	Ix = pIButton->size.w - pIcon->w - 5;
      } else {
	if (get_wflags(pIButton) & WF_ICON_CENTER) {
	  Ix = (pIButton->size.w - pIcon->w) / 2;
	} else {
	  Ix = 5;
	}
      }

      if (get_wflags(pIButton) & WF_ICON_ABOVE_TEXT) {
	Iy = 3;
	y = 3 + pIcon->h + 3 + (pIButton->size.h -
				(pIcon->h + 6) - pText->h) / 2;
      } else {
	if (get_wflags(pIButton) & WF_ICON_UNDER_TEXT) {
	  y = 3 + (pIButton->size.h - (pIcon->h + 3) - pText->h) / 2;
	  Iy = y + pText->h + 3;
	} else {		/* center */
	  Iy = (pIButton->size.h - pIcon->h) / 2;
	  y = (pIButton->size.h - pText->h) / 2;
	}
      }
    } else {			/* no text */
      Iy = (pIButton->size.h - pIcon->h) / 2;
      Ix = (pIButton->size.w - pIcon->w) / 2;
    }

    if (get_wstate(pIButton) == WS_PRESSED) {
      Ix += 1;
      Iy += 1;
    }


    dest.x = pIButton->size.x + Ix;
    dest.y = pIButton->size.y + Iy;
    ret = SDL_BlitSurface(pIcon, NULL, pIButton->dst, &dest);
    if (ret) {
      FREESURFACE(pText);
      return ret - 10;
    }
  }

  if (pText) {

    if (pIcon) {
      if (!(get_wflags(pIButton) & WF_ICON_ABOVE_TEXT) &&
	  !(get_wflags(pIButton) & WF_ICON_UNDER_TEXT)) {
	if (get_wflags(pIButton) & WF_ICON_CENTER_RIGHT) {
	  if (pIButton->string16->style & SF_CENTER) {
	    x = (pIButton->size.w - (pIcon->w + 5) - pText->w) / 2;
	  } else {
	    if (pIButton->string16->style & SF_CENTER_RIGHT) {
	      x = pIButton->size.w - (pIcon->w + 7) - pText->w;
	    } else {
	      x = 5;
	    }
	  }
	} /* end WF_ICON_CENTER_RIGHT */
	else {
	  if (get_wflags(pIButton) & WF_ICON_CENTER) {
	    /* text is blit on icon */
	    goto Alone;
	  } /* end WF_ICON_CENTER */
	  else {		/* icon center left - default */
	    if (pIButton->string16->style & SF_CENTER) {
	      x = 5 + pIcon->w + ((pIButton->size.w -
				   (pIcon->w + 5) - pText->w) / 2);
	    } else {
	      if (pIButton->string16->style & SF_CENTER_RIGHT) {
		x = pIButton->size.w - pText->w - 5;
	      } else {		/* text center left */
		x = 5 + pIcon->w + 3;
	      }
	    }

	  }			/* end icon center left - default */

	}
	/* 888888888888888888 */
      } else {
	goto Alone;
      }
    } else {
      /* !pIcon */
      y = (pIButton->size.h - pText->h) / 2;
    Alone:
      if (pIButton->string16->style & SF_CENTER) {
	x = (pIButton->size.w - pText->w) / 2;
      } else {
	if (pIButton->string16->style & SF_CENTER_RIGHT) {
	  x = pIButton->size.w - pText->w - 5;
	} else {
	  x = 5;
	}
      }
    }

    if (get_wstate(pIButton) == WS_PRESSED) {
      x += 1;
    } else {
      y -= 1;
    }

    dest.x = pIButton->size.x + x;
    dest.y = pIButton->size.y + y;
    ret = SDL_BlitSurface(pText, NULL, pIButton->dst, &dest);
  }

  FREESURFACE(pText);

  return 0;
}

/**************************************************************************
  Create Icon Button image with text and Icon then blit to Dest(ination)
  on positon pTIButton->size.x , pTIButton->size.y. WARRING: pDest must
  exist.

  Text with atributes is taken from pTIButton->string16 parameter.

  Graphic for button is taken from pTIButton->theme surface 
  and blit to new created image.

  Graphic for Icon Theme is taken from pTIButton->gfx surface 
  and blit to new created image.

  function return (-1) if there are no Icon and Text.  Else return 0.
**************************************************************************/
int real_redraw_tibutton(struct GUI *pTIButton)
{
  int iRet = 0;
  SDL_Surface *pIcon = create_icon_from_theme(pTIButton->gfx,
					      get_wstate(pTIButton));
  SDL_Surface *pCopy_Of_Icon_Theme = pTIButton->gfx;

  pTIButton->gfx = pIcon;

  iRet = real_redraw_ibutton(pTIButton);

  FREESURFACE(pTIButton->gfx);
  pTIButton->gfx = pCopy_Of_Icon_Theme;

  return iRet;
}

/* =================================================== */
/* ===================== EDIT ======================== */
/* =================================================== */

/**************************************************************************
  Return length of UniChar chain.
  WARRNING: if struct UniChar has 1 member and UniChar->chr == 0 then
  this function return 1 ( not 0 like in strlen )
**************************************************************************/
static size_t chainlen(const struct UniChar *pChain)
{
  size_t length = 0;

  if (pChain) {
    while (1) {
      length++;
      if (pChain->next == NULL) {
	break;
      }
      pChain = pChain->next;
    }
  }

  return length;
}

/**************************************************************************
  Delete UniChar structure.
**************************************************************************/
static void del_chain(struct UniChar *pChain)
{
  int i, len = 0;

  if (!pChain) {
    return;
  }

  len = chainlen(pChain);

  if (len > 1) {
    pChain = pChain->next;
    for (i = 0; i < len - 1; i++) {
      FREESURFACE(pChain->prev->pTsurf);
      FREE(pChain->prev);
      pChain = pChain->next;
    }
  }

  FREE(pChain);
}

/**************************************************************************
  Convert Unistring ( Uint16[] ) to UniChar structure.
  Memmory alocation -> after all use need call del_chain(...) !
**************************************************************************/
static struct UniChar *text2chain(const Uint16 * pInText)
{
  int i, len;
  struct UniChar *pOutChain = NULL;
  struct UniChar *chr_tmp = NULL;

  len = unistrlen(pInText);

  if (len == 0) {
    return pOutChain;
  }

  pOutChain = MALLOC(sizeof(struct UniChar));
  pOutChain->chr[0] = pInText[0];
  pOutChain->chr[1] = 0;
  chr_tmp = pOutChain;

  for (i = 1; i < len; i++) {
    chr_tmp->next = MALLOC(sizeof(struct UniChar));
    chr_tmp->next->chr[0] = pInText[i];
    chr_tmp->next->chr[1] = 0;
    chr_tmp->next->prev = chr_tmp;
    chr_tmp = chr_tmp->next;
  }

  return pOutChain;
}

/**************************************************************************
  Convert UniChar structure to Unistring ( Uint16[] ).
  WARRING: Do not free UniChar structure but allocate new Unistring.   
**************************************************************************/
static Uint16 *chain2text(const struct UniChar *pInChain, size_t len)
{
  int i;
  Uint16 *pOutText = NULL;

  if (!(len && pInChain)) {
    return pOutText;
  }

  pOutText = CALLOC(len + 1, sizeof(Uint16));
  for (i = 0; i < len; i++) {
    pOutText[i] = pInChain->chr[0];
    pInChain = pInChain->next;
  }

  return pOutText;
}

/* =================================================== */

/**************************************************************************
  Create ( malloc ) Edit Widget structure.

  Edit Theme graphic is taken from pTheme->Edit surface;
  Text is taken from 'pString16'.

  'length' parametr determinate width of Edit rect.

  This function determinate future size of Edit ( width, high ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Edit Widget.
**************************************************************************/
struct GUI * create_edit(SDL_Surface *pBackground, SDL_Surface *pDest,
		SDL_String16 *pString16, Uint16 length, Uint32 flags)
{
  SDL_Rect buf = { 0, 0, 0, 0 };

  struct GUI *pEdit = MALLOC(sizeof(struct GUI));

  pEdit->theme = pTheme->Edit;
  pEdit->gfx = pBackground;
  pEdit->string16 = pString16;
  set_wflag(pEdit, (WF_FREE_STRING | WF_FREE_GFX | flags));
  set_wstate(pEdit, WS_DISABLED);
  set_wtype(pEdit, WT_EDIT);
  pEdit->mod = KMOD_NONE;
  pEdit->dst = pDest;
  
  if (pString16) {
    pEdit->string16->style |= SF_CENTER;
    buf = str16size(pString16);
    buf.h += 4;
  }

  length = MAX(length, buf.w + 10);

  correct_size_bcgnd_surf(pTheme->Edit, &length, &buf.h);

  pEdit->size.w = length;
  pEdit->size.h = buf.h;

  return pEdit;
}

/**************************************************************************
  set new x, y position and redraw edit.
**************************************************************************/
int draw_edit(struct GUI *pEdit, Sint16 start_x, Sint16 start_y)
{
  pEdit->size.x = start_x;
  pEdit->size.y = start_y;

  if (get_wflags(pEdit) & WF_DRAW_THEME_TRANSPARENT) {
    refresh_widget_background(pEdit);
  }

  return redraw_edit(pEdit);
}

/**************************************************************************
  Create Edit Field surface ( with Text) and blit them to Main.screen,
  on position 'pEdit_Widget->size.x , pEdit_Widget->size.y'

  Graphic is taken from 'pEdit_Widget->theme'
  Text is taken from	'pEdit_Widget->sting16'

  if flag 'FW_DRAW_THEME_TRANSPARENT' is set theme will be blit
  transparent ( Alpha = 128 )

  function return Hight of created surfaces or (-1) if theme surface can't
  be created.
**************************************************************************/
int redraw_edit(struct GUI *pEdit_Widget)
{
  int iRet = 0;
  SDL_Rect rDest = {pEdit_Widget->size.x, pEdit_Widget->size.y, 0, 0};
  SDL_Surface *pEdit = NULL;
  SDL_Surface *pText = create_text_surf_from_str16(pEdit_Widget->string16);

  if (get_wflags(pEdit_Widget) & WF_DRAW_THEME_TRANSPARENT) {

    pEdit = create_bcgnd_surf(pEdit_Widget->theme, SDL_TRUE,
			      get_wstate(pEdit_Widget),
			      pEdit_Widget->size.w, pEdit_Widget->size.h);

    if (!pEdit) {
      return -1;
    }

    /* blit background */
    SDL_BlitSurface(pEdit_Widget->gfx, NULL, pEdit_Widget->dst, &rDest);
  } else {
    pEdit = create_bcgnd_surf(pEdit_Widget->theme, SDL_FALSE,
			      get_wstate(pEdit_Widget),
			      pEdit_Widget->size.w, pEdit_Widget->size.h);

    if (!pEdit) {
      return -1;
    }
  }

  /* blit theme */
  SDL_BlitSurface(pEdit, NULL, pEdit_Widget->dst, &rDest);

  /* set position and blit text */
  if (pText) {
    rDest.y += (pEdit->h - pText->h) / 2;
    /* blit centred text to botton */
    if (pEdit_Widget->string16->style & SF_CENTER) {
      rDest.x += (pEdit->w - pText->w) / 2;
    } else {
      if (pEdit_Widget->string16->style & SF_CENTER_RIGHT) {
	rDest.x += pEdit->w - pText->w - 5;
      } else {
	rDest.x += 5;		/* cennter left */
      }
    }

    SDL_BlitSurface(pText, NULL, pEdit_Widget->dst, &rDest);
  }
  /* pText */
  iRet = pEdit->h;

  /* Free memory */
  FREESURFACE(pText);
  FREESURFACE(pEdit);

  return iRet;
}

/**************************************************************************
  This function is pure madness :)
  Create Edit Field surface ( with Text) and blit them to Main.screen,
  on position 'pEdit_Widget->size.x , pEdit_Widget->size.y'

  Main role of this function is been text input to GUI.
  This code allow you to add, del unichar from unistring.

  Graphic is taken from 'pEdit_Widget->theme'
  OldText is taken from	'pEdit_Widget->sting16'

  NewText is returned to 'pEdit_Widget->sting16' ( after free OldText )

  if flag 'FW_DRAW_THEME_TRANSPARENT' is set theme will be blit
  transparent ( Alpha = 128 )

  NOTE: This function can return NULL in 'pEdit_Widget->sting16->text' but
        never free 'pEdit_Widget->sting16' struct.
**************************************************************************/
void edit_field(struct GUI *pEdit_Widget)
{
  char chr;
  SDL_Rect rDest;
  SDL_Surface *pEdit = NULL;
  Uint16 *pUniChar = NULL;

  int iChainLen = 0, iRet = 0, iTruelength = 0;
  int iRedraw = 1;
  int iStart_X = 5, iInputChain_X = 0;
  int iStart_Mod_X;

  struct UniChar ___last;
  struct UniChar *pBeginTextChain = NULL;
  struct UniChar *pEndTextChain = NULL;
  struct UniChar *pInputChain = NULL;
  struct UniChar *pInputChain_TMP = NULL;

  SDL_EnableUNICODE(1);

  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

  if (get_wflags(pEdit_Widget) & WF_DRAW_THEME_TRANSPARENT) {
    pEdit = create_bcgnd_surf(pEdit_Widget->theme, SDL_TRUE,
			      2, pEdit_Widget->size.w, pEdit_Widget->size.h);
  } else {
    pEdit = create_bcgnd_surf(pEdit_Widget->theme, SDL_FALSE,
			      2, pEdit_Widget->size.w, pEdit_Widget->size.h);
  }

  /* Creating Chain */
  pBeginTextChain = text2chain(pEdit_Widget->string16->text);


  /* Creating Empty (Last) pice of Chain */
  pInputChain = &___last;
  pEndTextChain = pInputChain;
  pEndTextChain->chr[0] = 32;	/*spacebar */
  pEndTextChain->chr[1] = 0;	/*spacebar */
  pEndTextChain->next = NULL;

  /* set font style (if any ) */
  if (!((pEdit_Widget->string16->style & 0x0F) & TTF_STYLE_NORMAL)) {
    TTF_SetFontStyle(pEdit_Widget->string16->font,
		     (pEdit_Widget->string16->style & 0x0F));
  }


  pEndTextChain->pTsurf =
      TTF_RenderUNICODE_Blended(pEdit_Widget->string16->font,
			      pEndTextChain->chr,
			      pEdit_Widget->string16->forecol);
  
  /* create surface for each font in chain and find chain length */
  if (pBeginTextChain) {
    pInputChain_TMP = pBeginTextChain;
    while (TRUE) {
      iChainLen++;

      pInputChain_TMP->pTsurf =
	  TTF_RenderUNICODE_Blended(pEdit_Widget->string16->font,
				    pInputChain_TMP->chr,
				    pEdit_Widget->string16->forecol);

      iTruelength += pInputChain_TMP->pTsurf->w;

      if (pInputChain_TMP->next == NULL) {
	break;
      }

      pInputChain_TMP = pInputChain_TMP->next;
    }
    /* set terminator of list */
    pInputChain_TMP->next = pInputChain;
    pInputChain->prev = pInputChain_TMP;
    pInputChain_TMP = NULL;
    pInputChain = pBeginTextChain;
  } else {
    pBeginTextChain = pInputChain;
  }

  /* main local loop */
  while (!iRet) {
    if (iRedraw) {
      rDest.x = pEdit_Widget->size.x;
      rDest.y = pEdit_Widget->size.y;

      /* blit backgroud ( if any ) */
      if (get_wflags(pEdit_Widget) & WF_DRAW_THEME_TRANSPARENT) {
	SDL_BlitSurface(pEdit_Widget->gfx, NULL, pEdit_Widget->dst, &rDest);
      }

      /* blit theme */
      SDL_BlitSurface(pEdit, NULL, pEdit_Widget->dst, &rDest);

      /* set start parametrs */
      pInputChain_TMP = pBeginTextChain;
      iStart_Mod_X = 0;

      rDest.y += (pEdit->h - pInputChain_TMP->pTsurf->h) / 2;
      rDest.x += iStart_X;

      /* draw loop */
      while (pInputChain_TMP) {
	rDest.x += iStart_Mod_X;
	/* chech if we draw inside of edit rect */
	if (rDest.x > pEdit_Widget->size.x + pEdit->w - 4) {
	  break;
	}

	if (rDest.x > pEdit_Widget->size.x) {
	  SDL_BlitSurface(pInputChain_TMP->pTsurf, NULL,
			  pEdit_Widget->dst, &rDest);
	}

	iStart_Mod_X = pInputChain_TMP->pTsurf->w;

	/* draw cursor */
	if (pInputChain_TMP == pInputChain) {
	  putline(pEdit_Widget->dst, rDest.x - 1,
		  rDest.y + (pEdit->h / 8),
		  rDest.x - 1, rDest.y + pEdit->h - (pEdit->h / 4), 0xff0088ff);
	  /* save active element position */
	  iInputChain_X = rDest.x;
	}
	
	pInputChain_TMP = pInputChain_TMP->next;
      }				/* while - draw loop */

      flush_rect(pEdit_Widget->size);
      iRedraw = 0;
    }
    /* iRedraw */
    SDL_WaitEvent(&Main.event);
    switch (Main.event.type) {
    case SDL_QUIT:
      /* just in case */
      iRet = -1;
      break;
    case SDL_MOUSEBUTTONDOWN:
      if (!(Main.event.motion.x >= pEdit_Widget->size.x &&
	    Main.event.motion.x <= pEdit_Widget->size.x + pEdit->w &&
	    Main.event.motion.y >= pEdit_Widget->size.y &&
	    Main.event.motion.y <= pEdit_Widget->size.y + pEdit->h)) {
	/* exit from loop */
	iRet = 1;
      }
      break;
    case SDL_KEYUP:
      /* obsoleted */
      break;
    case SDL_KEYDOWN:
      /* find which key is pressed */
      switch (Main.event.key.keysym.sym) {
      case SDLK_RETURN:
	/* exit from loop */
	iRet = 1;
	break;
      case SDLK_KP_ENTER:
	/* exit from loop */
	iRet = 1;
	break;
      case SDLK_RIGHT:
	/* move cursor right */
	if (pInputChain->next) {
	  if (iInputChain_X >= (pEdit_Widget->size.x + pEdit->w - 10)) {
	    iStart_X -= pInputChain->pTsurf->w -
		(pEdit_Widget->size.x + pEdit->w - 5 - iInputChain_X);
	  }

	  pInputChain = pInputChain->next;
	  iRedraw = 1;
	}
	break;
      case SDLK_LEFT:
	/* move cursor left */
	if (pInputChain->prev) {
	  pInputChain = pInputChain->prev;
	  if ((iInputChain_X <=
	       (pEdit_Widget->size.x + 9)) && (iStart_X != 5)) {
	    if (iInputChain_X != (pEdit_Widget->size.x + 5)) {
	      iStart_X += (pEdit_Widget->size.x - iInputChain_X + 5);
	    }

	    iStart_X += (pInputChain->pTsurf->w);
	  }
	  iRedraw = 1;
	}
	break;
      case SDLK_HOME:
	/* move cursor to begin of chain (and edit field) */
	pInputChain = pBeginTextChain;
	iRedraw = 1;
	iStart_X = 5;

	break;
      case SDLK_END:
	/* move cursor to end of chain (and edit field) */
	pInputChain = pEndTextChain;
	iRedraw = 1;

	if (pEdit_Widget->size.w - iTruelength < 0) {
	  iStart_X = pEdit_Widget->size.w - iTruelength - 5;
	}
	break;
      case SDLK_BACKSPACE:
	/* del element of chain (and move cursor left) */
	if (pInputChain->prev) {

	  if ((iInputChain_X <=
	       (pEdit_Widget->size.x + 9)) && (iStart_X != 5)) {
	    if (iInputChain_X != (pEdit_Widget->size.x + 5)) {
	      iStart_X += (pEdit_Widget->size.x - iInputChain_X + 5);
	    }
	    iStart_X += (pInputChain->prev->pTsurf->w);
	  }

	  if (pInputChain->prev->prev) {
	    pInputChain->prev->prev->next = pInputChain;
	    pInputChain_TMP = pInputChain->prev->prev;
	    iTruelength -= pInputChain->prev->pTsurf->w;
	    FREESURFACE(pInputChain->prev->pTsurf);
	    FREE(pInputChain->prev);
	    pInputChain->prev = pInputChain_TMP;
	  } else {
	    iTruelength -= pInputChain->prev->pTsurf->w;
	    FREESURFACE(pInputChain->prev->pTsurf);
	    FREE(pInputChain->prev);
	    pBeginTextChain = pInputChain;
	  }
	  iChainLen--;
	  iRedraw = 1;
	}

	break;
      case SDLK_DELETE:
	/* del element of chain */
	if (pInputChain->next && pInputChain->prev) {
	  pInputChain->prev->next = pInputChain->next;
	  pInputChain->next->prev = pInputChain->prev;
	  pInputChain_TMP = pInputChain->next;
	  iTruelength -= pInputChain->pTsurf->w;
	  FREESURFACE(pInputChain->pTsurf);
	  FREE(pInputChain);
	  pInputChain = pInputChain_TMP;
	  iChainLen--;
	  iRedraw = 1;
	}

	if (pInputChain->next && !pInputChain->prev) {
	  pInputChain = pInputChain->next;
	  iTruelength -= pInputChain->prev->pTsurf->w;
	  FREESURFACE(pInputChain->prev->pTsurf);
	  FREE(pInputChain->prev);
	  pBeginTextChain = pInputChain;
	  iChainLen--;
	  iRedraw = 1;
	}

	break;
      default:
	/* add new element of chain (and move cursor right) */
	if (Main.event.key.keysym.unicode) {
	  if (pInputChain != pBeginTextChain) {
	    pInputChain_TMP = pInputChain->prev;
	    pInputChain->prev = MALLOC(sizeof(struct UniChar));
	    pInputChain->prev->next = pInputChain;
	    pInputChain->prev->prev = pInputChain_TMP;
	    pInputChain_TMP->next = pInputChain->prev;
	  } else {
	    pInputChain->prev = MALLOC(sizeof(struct UniChar));
	    pInputChain->prev->next = pInputChain;
	    pBeginTextChain = pInputChain->prev;
	  }

	  /* convert and add to chain */
	  /* ugly fix */
	  if(Main.event.key.keysym.unicode < 0x80 &&
		    Main.event.key.keysym.unicode > 0) {
	    chr = (char)(Main.event.key.keysym.unicode);
	    pUniChar = convert_to_utf16(&chr);
          } else {
	    *pUniChar = Main.event.key.keysym.unicode;
          }
	  
	  pInputChain->prev->chr[0] = *pUniChar;
	  pInputChain->prev->chr[1] = 0;
	  FREE(pUniChar);


	  if (pInputChain->prev->chr) {
	    pInputChain->prev->pTsurf =
		TTF_RenderUNICODE_Blended(pEdit_Widget->string16->font,
					  pInputChain->prev->chr,
					  pEdit_Widget->string16->forecol);

	    iTruelength += pInputChain->prev->pTsurf->w;
	  }

	  if (iInputChain_X >= pEdit_Widget->size.x + pEdit->w - 10) {
	    if (pInputChain == pEndTextChain) {
	      iStart_X = pEdit->w - 5 - iTruelength;
	    } else {
	      iStart_X -= pInputChain->prev->pTsurf->w -
		  (pEdit_Widget->size.x + pEdit->w - 5 - iInputChain_X);
	    }
	  }
	  iChainLen++;
	  iRedraw = 1;
	}
	break;
      }				/* key pressed switch */

      break;

    default:

      break;
    }				/* event switch */

  }				/* while */

  /* reset font settings */
  if (!((pEdit_Widget->string16->style & 0x0F) & TTF_STYLE_NORMAL)) {
    TTF_SetFontStyle(pEdit_Widget->string16->font, TTF_STYLE_NORMAL);
  }

  if (pBeginTextChain == pEndTextChain) {
    pBeginTextChain = NULL;
  }

  FREESURFACE(pEndTextChain->pTsurf);

  FREE(pEdit_Widget->string16->text);
  pEdit_Widget->string16->text = chain2text(pBeginTextChain, iChainLen);

  FREESURFACE(pEdit);
  del_chain(pBeginTextChain);

  /* disable repeate key */
  SDL_EnableKeyRepeat(0, SDL_DEFAULT_REPEAT_INTERVAL);

  /* disable Unicode */
  SDL_EnableUNICODE(0);
}

/* =================================================== */
/* ===================== VSCROOLBAR ================== */
/* =================================================== */

/*
 */
/**************************************************************************
  Create background image for vscrollbars
  then return pointer to this image.

  Graphic is taken from pVert_theme surface and blit to new created image.

  hight depend of 'High' parametr.

  Type of image depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled
**************************************************************************/
static SDL_Surface *create_vertical_surface(SDL_Surface * pVert_theme,
					    Uint8 state, Uint16 High)
{
  SDL_Surface *pVerSurf = NULL;
  SDL_Rect src, des;

  Uint16 i;
  Uint16 start_y;

  Uint16 tile_count_midd;
  Uint8 tile_len_end;
  Uint8 tile_len_midd;


  tile_len_end = pVert_theme->h / 16;

  start_y = 0 + state * (pVert_theme->h / 4);

  /* tile_len_midd = pVert_theme->h/4  - tile_len_end*2; */
  tile_len_midd = pVert_theme->h / 4 - tile_len_end * 2;

  tile_count_midd = (High - tile_len_end * 2) / tile_len_midd;

  /* correction */
  if (tile_len_midd * tile_count_midd + tile_len_end * 2 < High)
    tile_count_midd++;

  if (!tile_count_midd) {
    pVerSurf = create_surf(pVert_theme->w, tile_len_end * 2, SDL_SWSURFACE);
  } else {
    pVerSurf = create_surf(pVert_theme->w, High, SDL_SWSURFACE);
  }

  src.x = 0;
  src.y = start_y;
  src.w = pVert_theme->w;
  src.h = tile_len_end;
  SDL_BlitSurface(pVert_theme, &src, pVerSurf, NULL);

  src.y = start_y + tile_len_end;
  src.h = tile_len_midd;

  des.x = 0;

  for (i = 0; i < tile_count_midd; i++) {
    des.y = tile_len_end + i * tile_len_midd;
    SDL_BlitSurface(pVert_theme, &src, pVerSurf, &des);
  }

  src.y = start_y + tile_len_end + tile_len_midd;
  src.h = tile_len_end;
  des.y = pVerSurf->h - tile_len_end;
  SDL_BlitSurface(pVert_theme, &src, pVerSurf, &des);

  SDL_SetColorKey(pVerSurf, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  return pVerSurf;
}

/**************************************************************************
  Create ( malloc ) VSrcrollBar Widget structure.

  Theme graphic is taken from pVert_theme surface;

  This function determinate future size of VScrollBar
  ( width = 'pVert_theme->w' , high = 'high' ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Widget.
**************************************************************************/
struct GUI * create_vertical(SDL_Surface *pVert_theme, SDL_Surface *pDest,
  			Uint16 high, Uint32 flags)
{
  struct GUI *pVer = MALLOC(sizeof(struct GUI));

  pVer->theme = pVert_theme;
  pVer->size.w = pVert_theme->w;
  pVer->size.h = high;
  set_wflag(pVer, (WF_FREE_STRING | flags));
  set_wstate(pVer, WS_DISABLED);
  set_wtype(pVer, WT_VSCROLLBAR);
  pVer->mod = KMOD_NONE;
  pVer->dst = pDest;

  return pVer;
}

/**************************************************************************
  ...
**************************************************************************/
int redraw_vert(struct GUI *pVert)
{
  int ret;
  SDL_Surface *pVert_Surf = create_vertical_surface(pVert->theme,
						    get_wstate(pVert),
						    pVert->size.h);
  ret =
      blit_entire_src(pVert_Surf, pVert->dst, pVert->size.x, pVert->size.y);
  FREESURFACE(pVert_Surf);

  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_vert(struct GUI *pVert, Sint16 x, Sint16 y)
{
  pVert->size.x = x;
  pVert->size.y = y;
  pVert->gfx = crop_rect_from_surface(pVert->dst, &pVert->size);
  return redraw_vert(pVert);
}

/* =================================================== */
/* ===================== HSCROOLBAR ================== */
/* =================================================== */

/**************************************************************************
  Create background image for hscrollbars
  then return	pointer to this image.

  Graphic is taken from pHoriz_theme surface and blit to new created image.

  hight depend of 'Width' parametr.

  Type of image depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled
**************************************************************************/
static SDL_Surface *create_horizontal_surface(SDL_Surface * pHoriz_theme,
					      Uint8 state, Uint16 Width)
{
  SDL_Surface *pHorSurf = NULL;
  SDL_Rect src, des;

  Uint16 i;
  Uint16 start_x;

  Uint16 tile_count_midd;
  Uint8 tile_len_end;
  Uint8 tile_len_midd;

  tile_len_end = pHoriz_theme->w / 16;

  start_x = 0 + state * (pHoriz_theme->w / 4);

  tile_len_midd = pHoriz_theme->w / 4 - tile_len_end * 2;

  tile_count_midd = (Width - tile_len_end * 2) / tile_len_midd;

  /* correction */
  if (tile_len_midd * tile_count_midd + tile_len_end * 2 < Width) {
    tile_count_midd++;
  }

  if (!tile_count_midd) {
    pHorSurf = create_surf(tile_len_end * 2, pHoriz_theme->h, SDL_SWSURFACE);
  } else {
    pHorSurf = create_surf(Width, pHoriz_theme->h, SDL_SWSURFACE);
  }

  src.y = 0;
  src.x = start_x;
  src.h = pHoriz_theme->h;
  src.w = tile_len_end;
  SDL_BlitSurface(pHoriz_theme, &src, pHorSurf, NULL);

  src.x = start_x + tile_len_end;
  src.w = tile_len_midd;

  des.y = 0;

  for (i = 0; i < tile_count_midd; i++) {
    des.x = tile_len_end + i * tile_len_midd;
    SDL_BlitSurface(pHoriz_theme, &src, pHorSurf, &des);
  }

  src.x = start_x + tile_len_end + tile_len_midd;
  src.w = tile_len_end;
  des.x = pHorSurf->w - tile_len_end;
  SDL_BlitSurface(pHoriz_theme, &src, pHorSurf, &des);

  SDL_SetColorKey(pHorSurf, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);

  return pHorSurf;
}

/**************************************************************************
  Create ( malloc ) VSrcrollBar Widget structure.

  Theme graphic is taken from pVert_theme surface;

  This function determinate future size of VScrollBar
  ( width = 'pVert_theme->w' , high = 'high' ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Widget.
**************************************************************************/
struct GUI * create_horizontal(SDL_Surface *pHoriz_theme, SDL_Surface *pDest,
  		Uint16 width, Uint32 flags)
{
  struct GUI *pHor = MALLOC(sizeof(struct GUI));

  pHor->theme = pHoriz_theme;
  pHor->size.w = width;
  pHor->size.h = pHoriz_theme->h;
  set_wflag(pHor, WF_FREE_STRING | flags);
  set_wstate(pHor, WS_DISABLED);
  set_wtype(pHor, WT_HSCROLLBAR);
  pHor->mod = KMOD_NONE;
  pHor->dst = pDest;
  
  return pHor;
}

/**************************************************************************
  ...
**************************************************************************/
int redraw_horiz(struct GUI *pHoriz)
{
  SDL_Surface *pHoriz_Surf = create_horizontal_surface(pHoriz->theme,
						       get_wstate(pHoriz),
						       pHoriz->size.w);
  int ret = blit_entire_src(pHoriz_Surf, pHoriz->dst, pHoriz->size.x,
			    pHoriz->size.y);
  FREESURFACE(pHoriz_Surf);

  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_horiz(struct GUI *pHoriz, Sint16 x, Sint16 y)
{
  pHoriz->size.x = x;
  pHoriz->size.y = y;
  pHoriz->gfx = crop_rect_from_surface(pHoriz->dst, &pHoriz->size);
  return redraw_horiz(pHoriz);
}

/* =================================================== */
/* ======================= WINDOWs ==================== */
/* =================================================== */

/**************************************************************************
  Window Menager Mechanism.
  Idea is simple each window/dialog has own buffer layer which is draw
  on screen during flush operations.
  This consume lots of memory but is extremly effecive.

  Each widget has own "destination" parm what desice where ( on what buffer )
  will be draw.
**************************************************************************/

/**************************************************************************
  Buffer allocation function.
  This function is call by "create_window(...)" function and allocate 
  buffer layer for this function.

  "bool transparent" parm deside what buffer format will be created.
   transparent = TRUE -> created buffer is RGBA (32bit)
   transparent = FALSE -> created buffer is RGB (screen pixel format)

  Pointer for this buffer is put in buffer array on last position that 
  flush functions will draw this layer last.
**************************************************************************/
static SDL_Surface * get_buffer_layer(bool transparent)
{
  SDL_Surface *pBuffer;
  Uint32 colorkey;
  /* create buffer */
  if(transparent) {
    pBuffer = SDL_DisplayFormatAlpha(Main.screen);
    colorkey = 0x0;
  } else {
    pBuffer = SDL_DisplayFormat(Main.screen);
    colorkey = SDL_MapRGB(pBuffer->format, 255, 0, 255);
  }
  
  /* clear buffer and setup transparent pixels */
  SDL_FillRect(pBuffer, NULL, colorkey);
  SDL_SetColorKey(pBuffer, SDL_SRCCOLORKEY, colorkey);
  
  /* add to buffers array */
  if (Main.guis) {
    int i;
    /* find NULL element */
    for(i = 0; i < Main.guis_count; i++) {
      if(!Main.guis[i]) {
	Main.guis[i] = pBuffer;
	return pBuffer;
      }
    }
    Main.guis_count++;
    Main.guis = REALLOC(Main.guis, Main.guis_count * sizeof(SDL_Surface *));
    Main.guis[Main.guis_count - 1] = pBuffer;
  } else {
    Main.guis = MALLOC(sizeof(SDL_Surface *));
    Main.guis[0] = pBuffer;
    Main.guis_count = 1;
  }
  
  return pBuffer;
}

/**************************************************************************
  Free buffer layer ( call by popdown_window_group_dialog(...) funct )
  Funct. free buffer layer and cleare buffer array entry.
**************************************************************************/
static void remove_buffer_layer(SDL_Surface *pBuffer)
{
  int i;
  
  if (pBuffer == pLocked_buffer) {
    return;
  }
  
  for(i = 0; i < Main.guis_count - 1; i++) {
    if(Main.guis[i] == pBuffer) {
      FREESURFACE(Main.guis[i]);
      Main.guis[i] = Main.guis[i + 1];
      Main.guis[i + 1] = NULL;
    } else {
      if(!Main.guis[i]) {
	Main.guis[i] = Main.guis[i + 1];
        Main.guis[i + 1] = NULL;
      }
    }
  }
  
  FREESURFACE(Main.guis[Main.guis_count - 1]);
}

/**************************************************************************
  In some cases we want popdown_dialog but don't want destroy his buffer
  becouse we know that we popup new dialogs and don't want free and alloc 
  in the same time.

  This function simple block "remove_buffer_layer(...)" function.
  NOTE: we must "unlock" this buffer manualy.
**************************************************************************/
void lock_buffer(SDL_Surface *pBuffer)
{
  pLocked_buffer = pBuffer;
}

/**************************************************************************
 unlock buffer layer that it can be free automaticaly with popdown_dialogs
**************************************************************************/
void unlock_buffer(void)
{
  pLocked_buffer = NULL;
}

/**************************************************************************
  return pointer to "locked" buffer (SDL_Surface)
**************************************************************************/
SDL_Surface * get_locked_buffer(void)
{
  return pLocked_buffer;
}

/**************************************************************************
 unlock and free buffer layer
**************************************************************************/
void remove_locked_buffer(void)
{
  if (pLocked_buffer) {
    SDL_Surface *pBuffer = pLocked_buffer;
    pLocked_buffer = NULL;
    remove_buffer_layer(pBuffer);
  }
}

/**************************************************************************
	Window mechanism.

	Active Window schould be first on list (All Widgets on this
	Widndow that are on List must be above)

	LIST:

	*pFirst_Widget_on_Active_Window.

	*pN__Widget_on_Active_Window.
	*pActive_Window. <------
	*pRest_Widgets.

	This trick give us:
	-	if any Widget is under ( area of ) this Window and Mouse
		cursor is above them, 'WidgetListScaner(...)' return
		pointer to Active Window not to this Widget.
**************************************************************************/

/**************************************************************************
  Allocate Widow Widget Structute.
  Text to titelbar is taken from 'pTitle'.
**************************************************************************/
struct GUI * create_window(SDL_Surface *pDest, SDL_String16 *pTitle, 
  			Uint16 w, Uint16 h, Uint32 flags)
{
  struct GUI *pWindow = MALLOC(sizeof(struct GUI));

  pWindow->string16 = pTitle;
  set_wflag(pWindow, WF_FREE_STRING | WF_FREE_GFX | WF_FREE_THEME |
				  WF_DRAW_FRAME_AROUND_WIDGET| flags);
  set_wstate(pWindow, WS_DISABLED);
  set_wtype(pWindow, WT_WINDOW);
  pWindow->mod = KMOD_NONE;
  if(pDest) {
    pWindow->dst = pDest;
  } else {
    pWindow->dst = get_buffer_layer((flags & WF_DRAW_THEME_TRANSPARENT));
  }

  if (pTitle) {
    SDL_Rect size = str16size(pTitle);
    w = MAX(w, size.w + 10);
    h = MAX(h, size.h);
    h = MAX(h, WINDOW_TILE_HIGH + 1);
  }

  pWindow->size.w = w;
  pWindow->size.h = h;

  return pWindow;
}

/**************************************************************************
  Resize Window 'pWindow' to 'new_w' and 'new_h'.
  and refresh window background ( save screen under window ).

  If pBcgd == NULL then theme is set to
  white transparent ( ALPHA = 128 ).

  Return 1 if allocate new surface and 0 if used 'pBcgd' surface.

  Exp.
  if ( resize_window( pWindow , pBcgd , new_w , new_h ) ) {
    FREESURFACE( pBcgd );
  }
**************************************************************************/
int resize_window(struct GUI *pWindow,
		  SDL_Surface * pBcgd,
		  SDL_Color * pColor, Uint16 new_w, Uint16 new_h)
{
  pWindow->size.w = new_w;
  pWindow->size.h = new_h;

  refresh_widget_background(pWindow);

  if (pWindow->theme != pBcgd) {
    FREESURFACE(pWindow->theme);
  }

  if (pBcgd) {
    if (pBcgd->w != new_w && pBcgd->h != new_h) {
      pWindow->theme = ResizeSurface(pBcgd, new_w, new_h, 2);
      if (get_wflags(pWindow) & WF_DRAW_THEME_TRANSPARENT) {
	SDL_SetAlpha(pWindow->theme, SDL_SRCALPHA, 128);
      }
      return 1;
    } else {
      pWindow->theme = pBcgd;
      if (get_wflags(pWindow) & WF_DRAW_THEME_TRANSPARENT) {
	SDL_SetAlpha(pWindow->theme, SDL_SRCALPHA, 128);
      }
      return 0;
    }
  }

  pBcgd = create_surf(new_w, new_h, SDL_SWSURFACE);
  
  if ((get_wflags(pWindow) & WF_DRAW_THEME_TRANSPARENT)) {
    pWindow->theme = SDL_DisplayFormatAlpha(pBcgd);
    SDL_SetAlpha(pWindow->theme, 0x0, 0x0);
    FREESURFACE(pBcgd);
  } else {
    pWindow->theme = pBcgd;
  }
  
  if (!pColor) {
    SDL_Color color = { 255, 255, 255, 128 };
    pColor = &color;
  }

  SDL_FillRect(pWindow->theme, NULL,
	       SDL_MapRGBA(pWindow->theme->format, pColor->r,
			pColor->g, pColor->b, pColor->unused));
  

  return 1;
}

/**************************************************************************
  Draw window frame with 'color' and
  save old pixels in 'pPixelArray'
**************************************************************************/
static void make_copy_of_pixel_and_draw_frame_window(struct GUI *pWindow,
		Uint32 color, Uint32 *pPixelArray , SDL_Surface *pDest)
{
  register int x, y;
  Uint16 w, h;
  int x0, y0;
  int i = 0;

  if (pWindow->size.x > pDest->w - 1 || pWindow->size.y > pDest->h - 1 ||
    pWindow->size.x + pWindow->size.w < 0 ||
	  pWindow->size.y + pWindow->size.h < 0 ){
    return;
  }
  
  if(pWindow->size.x < 0) {
    x = 0;
    w = pWindow->size.w + pWindow->size.x;
  } else {
    x = pWindow->size.x;
    w = pWindow->size.w;
  }
  
  if(x + w > pDest->w) {
    w = pDest->w - x;
  }
  
  if(pWindow->size.y < 0) {
    y = 0;
    h = pWindow->size.h + pWindow->size.y;
  } else {
    y = pWindow->size.y;
    h = pWindow->size.h;
  }
    
  if(y + h > pDest->h) {
    h = pDest->h - y;
  }
  
  x0 = x;
  y0 = y;
  w = x + w;
  h = y + h;
  
  /*left */
  y++;
  if (x == pWindow->size.x) {
    for (; y < h; y++) {
      putpixel_and_save_bcg(pDest, x, y, color, pPixelArray[i++]);
    }
    y--;
  } else {
    y = h - 1;
  }

  /* botton */
  x++;
  if (pWindow->size.y + pWindow->size.h < pDest->h) {
    for (; x < w; x++) {
      putpixel_and_save_bcg(pDest, x, y, color, pPixelArray[i++]);
    }
    x--;
  } else {
    x = w - 1;
  }

  /*right */
  y--;
  if (pWindow->size.x + pWindow->size.w < pDest->w) {
    for (; y >= y0; y--) {
      putpixel_and_save_bcg(pDest, x, y, color, pPixelArray[i++]);
    }
    y++;
  } else {
    y = y0;
  }

  /* top */
  x--;
  if (y >= 0) {
    for (; x >= x0; x--) {
      putpixel_and_save_bcg(pDest, x, y, color, pPixelArray[i++]);
    }
  }

  y = (pWindow->size.y + WINDOW_TILE_HIGH);
  if (y >= 0 && y < pDest->h) {
    for (x = x0 + 1; x < w - 1; x++) {
      putpixel_and_save_bcg(pDest, x, y, color, pPixelArray[i++]);
    }
  }
}

/**************************************************************************
  Restore old pixel from 'pPixelArray'.
  ( Undraw Window Frame )
**************************************************************************/
static void draw_frame_of_window_from_array(struct GUI *pWindow,
				Uint32 *pPixelArray , SDL_Surface *pDest)
{
  register int x, y;
  int i = 0;
  Uint16 w, h;
  int x0, y0;

  if (pWindow->size.x > pDest->w - 1 || pWindow->size.y > pDest->h - 1 ||
    pWindow->size.x + pWindow->size.w < 0 ||
	  pWindow->size.y + pWindow->size.h < 0 ){
    return;
  }
  
  if(pWindow->size.x < 0) {
    x = 0;
    w = pWindow->size.w + pWindow->size.x;
  } else {
    x = pWindow->size.x;
    w = pWindow->size.w;
  }
  
  if(x + w > pDest->w) {
    w = pDest->w - x;
  }
  
  if(pWindow->size.y < 0) {
    y = 0;
    h = pWindow->size.h + pWindow->size.y;
  } else {
    y = pWindow->size.y;
    h = pWindow->size.h;
  }
    
  if(y + h > pDest->h) {
    h = pDest->h - y;
  }
  
  x0 = x;
  y0 = y;
  w = x + w;
  h = y + h;
  
  /*left */
  y++;
  if (x == pWindow->size.x) {
    for (; y < h; y++) {
      putpixel(pDest, x, y, pPixelArray[i++]);
    }
    y--;
  } else {
    y = h - 1;
  }

  /* botton */
  x++;
  if (pWindow->size.y + pWindow->size.h < Main.screen->h) {
    for (; x < w; x++) {
      putpixel(pDest, x, y, pPixelArray[i++]);
    }
    x--;
  } else {
    x = w - 1;
  }

  /*right */
  y--;
  if (pWindow->size.x + pWindow->size.w < Main.screen->w) {
    for (; y >= y0; y--) {
      putpixel(pDest, x, y, pPixelArray[i++]);
    }
    y++;
  } else {
    y = y0;
  }

  /* top */
  x--;
  if (y >= 0) {
    for (; x >= x0; x--) {
      putpixel(pDest, x, y, pPixelArray[i++]);
    }
  }

  
  y = pWindow->size.y + WINDOW_TILE_HIGH;
  if (y >= 0 && y < pDest->h) {
    for (x = x0 + 1; x < w - 1; x++) {
      putpixel(pDest, x, y, pPixelArray[i++]);
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
int move_window(struct GUI *pWindow)
{
  int ret = 1, create = 0;

  Uint32 *pPixelArray = NULL;

  SDL_Rect *pRect_tab = NULL;

  while (ret) {
    SDL_WaitEvent(&Main.event);
    switch (Main.event.type) {
    case SDL_MOUSEBUTTONUP:
      ret = 0;

      break;
    case SDL_MOUSEMOTION:
      if (!create) {
	pPixelArray = CALLOC(3 * pWindow->size.w +
			     2 * pWindow->size.h, sizeof(Uint32));

	pRect_tab = CALLOC(2, sizeof(SDL_Rect));

	/* undraw window */
	blit_entire_src(pWindow->gfx, pWindow->dst,
			pWindow->size.x, pWindow->size.y);

	make_copy_of_pixel_and_draw_frame_window(pWindow, 0,
					pPixelArray , Main.screen);

	/*refresh_rect(pWindow->size);*/
        flush_rect(pWindow->size);
	
	create = 1;
      } else {
        draw_frame_of_window_from_array(pWindow, pPixelArray , Main.screen);
      }

      pRect_tab[0] = pWindow->size;
      correct_rect_region(&pRect_tab[0]);
            
      pWindow->size.x += Main.event.motion.xrel;
      pWindow->size.y += Main.event.motion.yrel;

      make_copy_of_pixel_and_draw_frame_window(pWindow, 0, pPixelArray, Main.screen);


      pRect_tab[1] = pWindow->size;
      correct_rect_region(&pRect_tab[1]);
            
      SDL_UpdateRects(Main.screen, 2, pRect_tab);
      
      break;
    }				/* switch */
  }				/* while */

  if (create) {
    /* undraw frame */
    draw_frame_of_window_from_array(pWindow, pPixelArray, Main.screen);

    refresh_widget_background(pWindow);
  }

  FREE(pRect_tab);
  FREE(pPixelArray);

  return create;
}

/**************************************************************************
  Redraw Window Graphic ( without other Widgets )
**************************************************************************/
int redraw_window(struct GUI *pWindow)
{
  SDL_Surface *pTmp = NULL;
  SDL_Rect dst = pWindow->size;

  /* Draw theme */
  SDL_BlitSurface(pWindow->theme, NULL, pWindow->dst, &dst);

  /* window has title string == has title bar */
  if (pWindow->string16) {
    /* Draw Window's TitelBar */
    dst = pWindow->size;
    dst.h = WINDOW_TILE_HIGH;
    if(get_wflags(pWindow) & WF_DRAW_THEME_TRANSPARENT) {
      SDL_FillRect(pWindow->dst, &dst, SDL_MapRGBA(pWindow->dst->format,
      							255, 255, 255, 200));
    } else {
      SDL_Color color = {255, 255, 255, 136};  
      SDL_FillRectAlpha(pWindow->dst, &dst, &color);
    }
    
    /* Draw Text on Window's TitelBar */
    pTmp = create_text_surf_from_str16(pWindow->string16);
    dst.x += 10;
    if(pTmp) {
      dst.y += ((WINDOW_TILE_HIGH - pTmp->h) / 2);
      SDL_BlitSurface(pTmp, NULL, pWindow->dst, &dst);
      FREESURFACE(pTmp);
    }
  
    putline(pWindow->dst, pWindow->size.x + FRAME_WH,
	  pWindow->size.y + WINDOW_TILE_HIGH,
	  pWindow->size.x + pWindow->size.w - FRAME_WH,
	  pWindow->size.y + WINDOW_TILE_HIGH, 
    		SDL_MapRGB(pWindow->dst->format, 0, 0, 0));    
  }
  
  return 0;
}

/* =================================================== */
/* ======================== MISC ===================== */
/* =================================================== */

/**************************************************************************
  Draw Themed Frame.
**************************************************************************/
void draw_frame(SDL_Surface * pDest, Sint16 start_x, Sint16 start_y,
		Uint16 w, Uint16 h)
{
  SDL_Surface *pTmp_Vert =
      ResizeSurface(pTheme->FR_Vert, pTheme->FR_Vert->w, h - 2, 2);
  SDL_Surface *pTmp_Hor =
      ResizeSurface(pTheme->FR_Hor, w - 2, pTheme->FR_Hor->h, 1);
  SDL_Rect tmp,dst = {start_x, start_y, 0, 0};

  dst.y++;
  tmp = dst;
  SDL_BlitSurface(pTmp_Vert, NULL, pDest, &tmp);
  dst.x += w - pTmp_Vert->w;
  tmp = dst;
  SDL_BlitSurface(pTmp_Vert, NULL, pDest, &tmp);

  dst.x = start_x + 1;
  dst.y--;
  tmp = dst;
  SDL_BlitSurface(pTmp_Hor, NULL, pDest, &tmp);
  dst.y += h - pTmp_Hor->h;
  tmp = dst;
  SDL_BlitSurface(pTmp_Hor, NULL, pDest, &tmp);

  FREESURFACE(pTmp_Hor);
  FREESURFACE(pTmp_Vert);
}

/**************************************************************************
  ...
**************************************************************************/
void refresh_widget_background(struct GUI *pWidget)
{
  if (pWidget) {
    if (pWidget->gfx && pWidget->gfx->w == pWidget->size.w &&
      				pWidget->gfx->h == pWidget->size.h) {
      bool is_colorkey = (pWidget->dst->flags & SDL_SRCCOLORKEY) > 0;
      static Uint32 colorkey;
      if(pWidget->dst->format->Amask) {
	/* turn off alpha */
        SDL_SetAlpha(pWidget->dst, 0x0, 0x0);
      } else {
	/* turn off colorkey */
	if(is_colorkey) {
	  colorkey = pWidget->dst->format->colorkey;
	  SDL_SetColorKey(pWidget->dst, 0x0, colorkey);
	}
      }
      SDL_BlitSurface(pWidget->dst, &pWidget->size, pWidget->gfx, NULL);
      if(pWidget->dst->format->Amask) {
	/* turn on alpha */
       SDL_SetAlpha(pWidget->dst, SDL_SRCALPHA, 255);
      } else {
	/* turn on colorkey */
	if(is_colorkey) {
	  SDL_SetColorKey(pWidget->dst, SDL_SRCCOLORKEY, colorkey);
	}
      }
    } else {
      FREESURFACE(pWidget->gfx);
      pWidget->gfx = crop_rect_from_surface(pWidget->dst, &pWidget->size);
      if(pWidget->gfx->format->Amask) {
       SDL_SetAlpha(pWidget->gfx, 0x0, 0x0);
      } else {
	SDL_SetColorKey(pWidget->gfx, 0x0, 0x0);
      }
    }
  }
}

/* =================================================== */
/* ======================= LABELs ==================== */
/* =================================================== */

/**************************************************************************
  ...
**************************************************************************/
void remake_label_size(struct GUI *pLabel)
{
  SDL_Surface *pIcon = pLabel->theme;
  SDL_String16 *pText = pLabel->string16;
  Uint32 flags = get_wflags(pLabel);
  SDL_Rect buf = { 0, 0, 0, 0 };
  Uint16 w = 0, h = 0, space;

  if (flags & WF_DRAW_TEXT_LABEL_WITH_SPACE) {
    space = 10;
  } else {
    space = 0;
  }

  if (pText) {
    pLabel->string16->style |= SF_CENTER;
    buf = str16size(pText);

    w = MAX(w, buf.w + space);
    h = MAX(h, buf.h);
  }

  if (pIcon) {
    if (pText) {
      if ((flags & WF_ICON_UNDER_TEXT) || (flags & WF_ICON_ABOVE_TEXT)) {
	w = MAX(w, pIcon->w + space);
	h = MAX(h, buf.h + pIcon->h + 3);
      } else {
	if (flags & WF_ICON_CENTER) {
	  w = MAX(w, pIcon->w + space);
	  h = MAX(h, pIcon->h);
	} else {
	  w = MAX(w, buf.w + pIcon->w + 5 + space);
	  h = MAX(h, pIcon->h);
	}
      }
    } /* pText */
    else {
      w = MAX(w, pIcon->w + space);
      h = MAX(h, pIcon->h);
    }
  }

  /* pIcon */
  pLabel->size.w = w;
  pLabel->size.h = h;
}

/**************************************************************************
  ThemeLabel is String16 with Background ( pIcon ).
**************************************************************************/
struct GUI * create_themelabel(SDL_Surface *pIcon, SDL_Surface *pDest,
  		SDL_String16 *pText, Uint16 w, Uint16 h, Uint32 flags)
{
  struct GUI *pLabel = NULL;
  
  if (!pIcon && !pText) {
    return NULL;
  }

  pLabel = MALLOC(sizeof(struct GUI));
  pLabel->theme = pIcon;
  pLabel->string16 = pText;
  set_wflag(pLabel,
	    (WF_ICON_CENTER | WF_FREE_STRING | WF_FREE_GFX |
	     WF_DRAW_THEME_TRANSPARENT | flags));
  set_wstate(pLabel, WS_DISABLED);
  set_wtype(pLabel, WT_T_LABEL);
  pLabel->mod = KMOD_NONE;
  pLabel->dst = pDest;
  
  remake_label_size(pLabel);

  pLabel->size.w = MAX(pLabel->size.w, w);
  pLabel->size.h = MAX(pLabel->size.h, h);
  
  return pLabel;
}

/**************************************************************************
  this Label is String16 with Icon.
**************************************************************************/
struct GUI * create_iconlabel(SDL_Surface *pIcon, SDL_Surface *pDest,
  		SDL_String16 *pText, Uint32 flags)
{
  struct GUI *pILabel = NULL;

  pILabel = MALLOC(sizeof(struct GUI));

  pILabel->theme = pIcon;
  pILabel->string16 = pText;
  set_wflag(pILabel, WF_FREE_STRING | WF_FREE_GFX | flags);
  set_wstate(pILabel, WS_DISABLED);
  set_wtype(pILabel, WT_I_LABEL);
  pILabel->mod = KMOD_NONE;
  pILabel->dst = pDest;
  
  remake_label_size(pILabel);

  return pILabel;
}

/**************************************************************************
  ThemeLabel is String16 with Background ( pIcon ).
**************************************************************************/
struct GUI * create_themelabel2(SDL_Surface *pIcon, SDL_Surface *pDest,
  		SDL_String16 *pText, Uint16 w, Uint16 h, Uint32 flags)
{
  struct GUI *pLabel = NULL;
  SDL_Surface *pBuf = NULL, *pTheme = NULL;
  SDL_Rect area;
  SDL_Color store, color = {0,0,255,96};
  Uint32 colorkey;
  
  if (!pIcon && !pText) {
    return NULL;
  }

  pLabel = MALLOC(sizeof(struct GUI));
  pLabel->theme = pIcon;
  pLabel->string16 = pText;
  set_wflag(pLabel, (WF_FREE_THEME | WF_FREE_STRING | WF_FREE_GFX | flags));
  set_wstate(pLabel, WS_DISABLED);
  set_wtype(pLabel, WT_T2_LABEL);
  pLabel->mod = KMOD_NONE;
  
  
  remake_label_size(pLabel);

  pLabel->size.w = MAX(pLabel->size.w, w);
  pLabel->size.h = MAX(pLabel->size.h, h);
  
  pBuf = create_surf(pLabel->size.w, pLabel->size.h * 2, SDL_SWSURFACE);
    
  if(flags & WF_DRAW_THEME_TRANSPARENT) {
    pTheme = SDL_DisplayFormatAlpha(pBuf);
    FREESURFACE(pBuf);
  } else {
    pTheme = pBuf;
  }
  
  colorkey = SDL_MapRGBA(pTheme->format, pText->backcol.r,
  		pText->backcol.g, pText->backcol.b, pText->backcol.unused);
  SDL_FillRect(pTheme, NULL, colorkey);
    
#if 0  
  if(pDest->format->Amask) {
    SDL_SetAlpha(pTheme, 0x0, 0x0);
  }
#endif
  
  pLabel->size.x = 0;
  pLabel->size.y = 0;
  area = pLabel->size;
  pLabel->dst = pTheme;
  
  /* normal */
  redraw_iconlabel(pLabel);
  
  /* sellected */
  area.x = 0;
  area.y = pLabel->size.h;
    
  if(flags & WF_DRAW_THEME_TRANSPARENT) {
    if(!pText->backcol.unused) {
      SDL_SetColorKey(pTheme, SDL_SRCCOLORKEY|SDL_RLEACCEL, colorkey);
    }
    SDL_FillRect(pTheme, &area, SDL_MapRGBA(pTheme->format, 0, 0, 255, 96));
    store = pText->backcol;
    SDL_GetRGBA(getpixel(pTheme, area.x , area.y), pTheme->format,
	      &pText->backcol.r, &pText->backcol.g,
      		&pText->backcol.b, &pText->backcol.unused);
  } else {
    SDL_FillRectAlpha(pTheme, &area, &color);
  }
  
  pLabel->size.y = pLabel->size.h;
  redraw_iconlabel(pLabel);
  
  if(flags & WF_DRAW_THEME_TRANSPARENT) {
    pText->backcol = store;
  }
  
  pLabel->size.x = 0;
  pLabel->size.y = 0;
  if(flags & WF_FREE_THEME) {
    FREESURFACE(pLabel->theme);
  }
  pLabel->theme = pTheme;
  pLabel->dst = pDest;
  return pLabel;
}

struct GUI * convert_iconlabel_to_themeiconlabel2(struct GUI *pIconLabel)
{
  SDL_Rect start, area;
  SDL_Color store, color = {0,0,255,96};
  Uint32 colorkey, flags = get_wflags(pIconLabel);
  SDL_Surface *pDest, *pTheme, *pBuf = create_surf(pIconLabel->size.w,
				  pIconLabel->size.h * 2, SDL_SWSURFACE);
  
  if(flags & WF_DRAW_THEME_TRANSPARENT) {
    pTheme = SDL_DisplayFormatAlpha(pBuf);
    FREESURFACE(pBuf);
  } else {
    pTheme = pBuf;
  }
  
  colorkey = SDL_MapRGBA(pTheme->format, pIconLabel->string16->backcol.r,
  		pIconLabel->string16->backcol.g,
		pIconLabel->string16->backcol.b,
		pIconLabel->string16->backcol.unused);
  SDL_FillRect(pTheme, NULL, colorkey);
    
#if 0
  if(pDest->format->Amask) {
    SDL_SetAlpha(pTheme, 0x0, 0x0);
  }
#endif
  
  start = pIconLabel->size;
  pIconLabel->size.x = 0;
  pIconLabel->size.y = 0;
  area = start;
  pDest = pIconLabel->dst;
  pIconLabel->dst = pTheme;
  
  /* normal */
  redraw_iconlabel(pIconLabel);
  
  /* sellected */
  area.x = 0;
  area.y = pIconLabel->size.h;
    
  if(flags & WF_DRAW_THEME_TRANSPARENT) {
    if(!pIconLabel->string16->backcol.unused) {
      SDL_SetColorKey(pTheme, SDL_SRCCOLORKEY|SDL_RLEACCEL, colorkey);
    }
    SDL_FillRect(pTheme, &area, SDL_MapRGBA(pTheme->format, 0, 0, 255, 96));
    store = pIconLabel->string16->backcol;
    SDL_GetRGBA(getpixel(pTheme, area.x , area.y), pTheme->format,
	      &pIconLabel->string16->backcol.r, &pIconLabel->string16->backcol.g,
      		&pIconLabel->string16->backcol.b,
			&pIconLabel->string16->backcol.unused);
  } else {
    SDL_FillRectAlpha(pTheme, &area, &color);
  }
  
  pIconLabel->size.y = pIconLabel->size.h;
  redraw_iconlabel(pIconLabel);
  
  if(flags & WF_DRAW_THEME_TRANSPARENT) {
    pIconLabel->string16->backcol = store;
  }
  
  pIconLabel->size = start;
  if(flags & WF_FREE_THEME) {
    FREESURFACE(pIconLabel->theme);
  }
  pIconLabel->theme = pTheme;
  if(flags & WF_FREE_STRING) {
    FREESTRING16(pIconLabel->string16);
  }
  pIconLabel->dst = pDest;
  set_wtype(pIconLabel, WT_T2_LABEL);
  return pIconLabel;
}

#if 0
/**************************************************************************
  ...
**************************************************************************/
static int redraw_themelabel(struct GUI *pLabel)
{
  int ret;
  Sint16 x, y;
  SDL_Surface *pText = NULL;

  if (!pLabel) {
    return -3;
  }

  if ((pText = create_text_surf_from_str16(pLabel->string16)) == NULL) {
    return (-4);
  }

  if (pLabel->string16->style & SF_CENTER) {
    x = (pLabel->size.w - pText->w) / 2;
  } else {
    if (pLabel->string16->style & SF_CENTER_RIGHT) {
      x = pLabel->size.w - pText->w - 5;
    } else {
      x = 5;
    }
  }

  y = (pLabel->size.h - pText->h) / 2;

  /* redraw theme */
  if (pLabel->theme) {
    ret = blit_entire_src(pLabel->theme, pLabel->dst,
			  pLabel->size.x, pLabel->size.y);
    if (ret) {
      return ret;
    }
  }

  ret = blit_entire_src(pText, pLabel->dst, pLabel->size.x + x,
			pLabel->size.y + y);

  FREESURFACE(pText);

  return ret;
}
#endif

/**************************************************************************
  ...
**************************************************************************/
static inline int redraw_themelabel2(struct GUI *pLabel)
{
    
  SDL_Rect src = {0,0, pLabel->size.w, pLabel->size.h};
  SDL_Rect dst = {pLabel->size.x, pLabel->size.y, 0, 0};
/* 
  if (!pLabel) {
    return -3;
  }
*/
  if(get_wstate(pLabel) == WS_SELLECTED) {
    src.y = pLabel->size.h;
  }

  return SDL_BlitSurface(pLabel->theme, &src, pLabel->dst, &dst);
}

/**************************************************************************
  ...
**************************************************************************/
static int redraw_iconlabel(struct GUI *pLabel)
{
  int space, ret = 0; /* FIXME: possibly uninitialized */
  Sint16 x, xI, yI;
  Sint16 y = 0; /* FIXME: possibly uninitialized */
  SDL_Surface *pText;
  SDL_Rect dst;
  Uint32 flags;

  if (!pLabel) {
    return -3;
  }

  SDL_SetClipRect(pLabel->dst, &pLabel->size);
  
  flags = get_wflags(pLabel);

  if (flags & WF_DRAW_TEXT_LABEL_WITH_SPACE) {
    space = 5;
  } else {
    space = 0;
  }

  pText = create_text_surf_from_str16(pLabel->string16);

  if(pText && (pLabel->string16->render == 3) &&
    (flags & WF_DRAW_THEME_TRANSPARENT) &&
    ((pLabel->dst->format->BitsPerPixel == 32) && pLabel->dst->format->Amask))
  {
    SDL_SetAlpha(pText, 0x0, 0x0);
  }
  
  if (pLabel->theme) {		/* Icon */
    if (pText) {
      if (flags & WF_ICON_CENTER_RIGHT) {
	xI = pLabel->size.w - pLabel->theme->w - space;
      } else {
	if (flags & WF_ICON_CENTER) {
	  xI = (pLabel->size.w - pLabel->theme->w) / 2;
	} else {
	  xI = space;
	}
      }

      if (flags & WF_ICON_ABOVE_TEXT) {
	yI = 0;
	y = pLabel->theme->h + 3
	    + (pLabel->size.h - (pLabel->theme->h + 3) - pText->h) / 2;
      } else {
	if (flags & WF_ICON_UNDER_TEXT) {
	  y = (pLabel->size.h - (pLabel->theme->h + 3) - pText->h) / 2;
	  yI = y + pText->h + 3;
	} else {
	  yI = (pLabel->size.h - pLabel->theme->h) / 2;
	  y = (pLabel->size.h - pText->h) / 2;
	}
      }
    } /* pText */
    else {
#if 0
      yI = (pLabel->size.h - pLabel->theme->h) / 2;
      xI = (pLabel->size.w - pLabel->theme->w) / 2;
#endif
      yI = 0;
      xI = space;
    }

    dst.x = pLabel->size.x + xI;
    dst.y = pLabel->size.y + yI;
    ret = SDL_BlitSurface(pLabel->theme, NULL, pLabel->dst, &dst);
    
    if (ret) {
      return ret - 10;
    }
  }

  if (pText) {
    if (pLabel->theme) { /* Icon */
      if (!(flags & WF_ICON_ABOVE_TEXT) && !(flags & WF_ICON_UNDER_TEXT)) {
	if (flags & WF_ICON_CENTER_RIGHT) {
	  if (pLabel->string16->style & SF_CENTER) {
	    x = (pLabel->size.w - (pLabel->theme->w + 5 + space) -
		 pText->w) / 2;
	  } else {
	    if (pLabel->string16->style & SF_CENTER_RIGHT) {
	      x = pLabel->size.w - (pLabel->theme->w + 5 + space) - pText->w;
	    } else {
	      x = space;
	    }
	  }
	} /* WF_ICON_CENTER_RIGHT */
	else {
	  if (flags & WF_ICON_CENTER) {
	    /* text is blit on icon */
	    goto Alone;
	  } else {		/* WF_ICON_CENTER_LEFT */
	    if (pLabel->string16->style & SF_CENTER) {
	      x = space + pLabel->theme->w + 5 + ((pLabel->size.w -
						   (space +
						    pLabel->theme->w + 5) -
						   pText->w) / 2);
	    } else {
	      if (pLabel->string16->style & SF_CENTER_RIGHT) {
		x = pLabel->size.w - pText->w - space;
	      } else {
		x = space + pLabel->theme->w + 5;
	      }
	    }
	  }			/* WF_ICON_CENTER_LEFT */
	}
      } /* !WF_ICON_ABOVE_TEXT && !WF_ICON_UNDER_TEXT */
      else {
	goto Alone;
      }
    } /* pLabel->theme == Icon */
    else {
      y = (pLabel->size.h - pText->h) / 2;
    Alone:
      if (pLabel->string16->style & SF_CENTER) {
	x = (pLabel->size.w - pText->w) / 2;
      } else {
	if (pLabel->string16->style & SF_CENTER_RIGHT) {
	  x = pLabel->size.w - pText->w - space;
	} else {
	  x = space;
	}
      }
    }

    dst.x = pLabel->size.x + x;
    dst.y = pLabel->size.y + y;
    ret = SDL_BlitSurface(pText, NULL, pLabel->dst, &dst);
    FREESURFACE(pText);

  }

  SDL_SetClipRect(pLabel->dst, NULL);
  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int redraw_label(struct GUI *pLabel)
{
  int ret;
  SDL_Rect area = pLabel->size;
  SDL_Color bar_color = {0, 128, 255, 128};
  SDL_Color backup_color = {0, 0, 0, 0};
 
  /* if label transparen then clear background under widget
   * or save this background */
  if (get_wflags(pLabel) & WF_DRAW_THEME_TRANSPARENT) {
    if (pLabel->gfx) {
      SDL_BlitSurface(pLabel->gfx, NULL, pLabel->dst, &area);
    } else {
      pLabel->gfx = crop_rect_from_surface(pLabel->dst, &pLabel->size);
      if (pLabel->dst->format->Amask) {
	SDL_SetAlpha(pLabel->gfx, 0x0, 0x0);
      }
    }
  }

  if(get_wtype(pLabel) == WT_T2_LABEL) {
    return redraw_themelabel2(pLabel);
  }
  
  /* redraw sellect bar */
  if (get_wstate(pLabel) == WS_SELLECTED) {
    
    if(get_wflags(pLabel) & WF_SELLECT_WITHOUT_BAR) {
      if (pLabel->string16) {
        backup_color = pLabel->string16->forecol;
        pLabel->string16->forecol = bar_color;
	if(pLabel->string16->style & TTF_STYLE_BOLD) {
	  pLabel->string16->style |= TTF_STYLE_UNDERLINE;
	} else {
	  pLabel->string16->style |= TTF_STYLE_BOLD;
	}
      }
    } else {
      SDL_FillRectAlpha(pLabel->dst, &area, &bar_color);
            
      if (pLabel->string16 && (pLabel->string16->render == 3)) {
        backup_color = pLabel->string16->backcol;
        SDL_GetRGBA(getpixel(pLabel->dst, area.x , area.y), pLabel->dst->format,
	      &pLabel->string16->backcol.r, &pLabel->string16->backcol.g,
      		&pLabel->string16->backcol.b, &pLabel->string16->backcol.unused);
      }
    }
  }

  /* redraw icon label */
  ret = redraw_iconlabel(pLabel);
  
  if ((get_wstate(pLabel) == WS_SELLECTED) && (pLabel->string16)) {
    if(get_wflags(pLabel) & WF_SELLECT_WITHOUT_BAR) {
      if (pLabel->string16->style & TTF_STYLE_UNDERLINE) {
	pLabel->string16->style &= ~TTF_STYLE_UNDERLINE;
      } else {
	pLabel->string16->style &= ~TTF_STYLE_BOLD;
      }
      pLabel->string16->forecol = backup_color;
    } else {
      if(pLabel->string16->render == 3) {
	pLabel->string16->backcol = backup_color;
      }
    } 
  }
  
  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_label(struct GUI *pLabel, Sint16 start_x, Sint16 start_y)
{
  pLabel->size.x = start_x;
  pLabel->size.y = start_y;
  return redraw_label(pLabel);
}

/* =================================================== */
/* ======================= CHECK BOX ================= */
/* =================================================== */

/**************************************************************************
  ...
**************************************************************************/
struct GUI *create_checkbox(SDL_Surface *pDest, bool state, Uint32 flags)
{
  struct GUI *pCBox = MALLOC(sizeof(struct GUI));
  struct CHECKBOX *pTmp = MALLOC(sizeof(struct CHECKBOX));
    
  if (state) {
    pCBox->theme = pTheme->CBOX_Sell_Icon;
  } else {
    pCBox->theme = pTheme->CBOX_Unsell_Icon;
  }

  set_wflag(pCBox, (WF_FREE_STRING | WF_FREE_GFX | WF_FREE_DATA | flags));
  set_wstate(pCBox, WS_DISABLED);
  set_wtype(pCBox, WT_CHECKBOX);
  pCBox->mod = KMOD_NONE;
  pCBox->dst = pDest;
  pTmp->state = state;
  pTmp->pTRUE_Theme = pTheme->CBOX_Sell_Icon;
  pTmp->pFALSE_Theme = pTheme->CBOX_Unsell_Icon;
  pCBox->data = (void *)pTmp;
  
  pCBox->size.w = pCBox->theme->w / 4;
  pCBox->size.h = pCBox->theme->h;

  return pCBox;
}

/**************************************************************************
  ...
**************************************************************************/
struct GUI * create_textcheckbox(SDL_Surface *pDest, bool state,
		  SDL_String16 *pStr, Uint32 flags)
{
  struct GUI *pCBox;
  struct CHECKBOX *pTmp;
  SDL_Surface *pSurf, *pIcon;

  if (!pStr) {
    return create_checkbox(pDest, state, flags);
  }
  
  pTmp = MALLOC(sizeof(struct CHECKBOX));
    
  if (state) {
    pSurf = pTheme->CBOX_Sell_Icon;
  } else {
    pSurf = pTheme->CBOX_Unsell_Icon;
  }
    
  pIcon = create_icon_from_theme(pSurf, 0);
  pCBox = create_iconlabel(pIcon, pDest, pStr, (flags | WF_FREE_DATA));

  pStr->style &= ~SF_CENTER;

  pCBox->theme = pSurf;
  FREESURFACE(pIcon);

  set_wtype(pCBox, WT_TCHECKBOX);
  pTmp->state = state;
  pTmp->pTRUE_Theme = pTheme->CBOX_Sell_Icon;
  pTmp->pFALSE_Theme = pTheme->CBOX_Unsell_Icon;
  pCBox->data = (void *)pTmp;
  
  return pCBox;
}

int set_new_checkbox_theme(struct GUI *pCBox ,
				SDL_Surface *pTrue, SDL_Surface *pFalse)
{
  struct CHECKBOX *pTmp;
  
  if(!pCBox) {
    return -1;
  }
  
  if(!pCBox->data) {
    return -2;
  }
  
  pTmp = (struct CHECKBOX *)pCBox->data;
  pTmp->pTRUE_Theme = pTrue;
  pTmp->pFALSE_Theme = pFalse;
  if(pTmp->state) {
    pCBox->theme = pTrue;
  } else {
    pCBox->theme = pFalse;
  }
  return 0;
}

void togle_checkbox(struct GUI *pCBox)
{
  struct CHECKBOX *pTmp = (struct CHECKBOX *)pCBox->data;
  if(pTmp->state) {
    pCBox->theme = pTmp->pFALSE_Theme;
    pTmp->state = FALSE;
  } else {
    pCBox->theme = pTmp->pTRUE_Theme;
    pTmp->state = TRUE;
  }
}

bool get_checkbox_state(struct GUI *pCBox)
{
  return ((struct CHECKBOX *)pCBox->data)->state;
}

int redraw_textcheckbox(struct GUI *pCBox)
{
  int ret;
  SDL_Surface *pTheme_Surface, *pIcon;

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
  if (get_wflags(pCBox) & WF_DRAW_THEME_TRANSPARENT) {
    if (pCBox->gfx) {
      blit_entire_src(pCBox->gfx, pCBox->dst, pCBox->size.x, pCBox->size.y);
    } else {
      pCBox->gfx = crop_rect_from_surface(pCBox->dst, &pCBox->size);
    }
  }

  /* redraw icon label */
  ret = redraw_iconlabel(pCBox);

  FREESURFACE(pIcon);
  pCBox->theme = pTheme_Surface;

  return ret;
}
