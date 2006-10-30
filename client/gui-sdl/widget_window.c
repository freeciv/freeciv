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
#include "gui_id.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"

#include "widget_p.h"

#include "widget.h"

struct MOVE {
  bool moved;
  struct widget *pWindow;
};

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
struct widget * create_window(SDL_Surface *pDest, SDL_String16 *pTitle, 
  			Uint16 w, Uint16 h, Uint32 flags)
{
  struct widget *pWindow = fc_calloc(1, sizeof(struct widget));

  pWindow->string16 = pTitle;
  set_wflag(pWindow, WF_FREE_STRING | WF_FREE_GFX | WF_FREE_THEME |
				  WF_DRAW_FRAME_AROUND_WIDGET| flags);
  set_wstate(pWindow, FC_WS_DISABLED);
  set_wtype(pWindow, WT_WINDOW);
  pWindow->mod = KMOD_NONE;
  if(pDest) {
    pWindow->dst = pDest;
  } else {
    pWindow->dst = get_buffer_layer(w, h);
  }

  if (pTitle) {
    SDL_Rect size = str16size(pTitle);
    w = MAX(w, size.w + adj_size(10));
    h = MAX(h, size.h);
    h = MAX(h, WINDOW_TITLE_HEIGHT + 1);
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
int resize_window(struct widget *pWindow,
		  SDL_Surface * pBcgd,
		  SDL_Color * pColor, Uint16 new_w, Uint16 new_h)
{
  struct gui_layer *gui_layer;  
  struct widget *pWidget;

  /* window */
  
  pWindow->size.w = new_w;
  pWindow->size.h = new_h;

  refresh_widget_background(pWindow);

  /* allocate new buffer */
  if (pWindow->dst != Main.gui) {
    gui_layer = get_gui_layer(pWindow->dst);
    FREESURFACE(gui_layer->surface);
    gui_layer->surface = create_surf_alpha(/*Main.screen->w*/pWindow->size.w,
                                           /*Main.screen->h*/pWindow->size.h,
                                                               SDL_SWSURFACE);
    /* assign new buffer to all widgets on this window */
    pWidget = pWindow;
    while(pWidget) {
      pWidget->dst = gui_layer->surface;
      pWidget = pWidget->prev;
    }
  }

  if (pWindow->theme != pBcgd) {
    FREESURFACE(pWindow->theme);
  }

  if (pBcgd) {
    if (pBcgd->w != new_w || pBcgd->h != new_h) {
      pWindow->theme = ResizeSurface(pBcgd, new_w, new_h, 2);
      return 1;
    } else {
      pWindow->theme = pBcgd;
      return 0;
    }
  }

  pBcgd = create_surf_alpha(new_w, new_h, SDL_SWSURFACE);
  
  pWindow->theme = pBcgd;
  
  if (!pColor) {
    SDL_Color color = { 255, 255, 255, 128 };
    pColor = &color;
  }

  SDL_FillRect(pWindow->theme, NULL, map_rgba(pWindow->theme->format, *pColor));

  return 1;
}

void set_window_pos(struct widget *pWindow, int x, int y)
{
  struct gui_layer *gui_layer;
  
  pWindow->size.x = x;
  pWindow->size.y = y;
  
  if (pWindow->dst != Main.gui) {
    gui_layer = get_gui_layer(pWindow->dst);
    gui_layer->dest_rect = pWindow->size;
  }
}

/**************************************************************************
...
**************************************************************************/
static Uint16 move_window_motion(SDL_MouseMotionEvent *pMotionEvent, void *pData)
{
  struct MOVE *pMove = (struct MOVE *)pData;
  
  if (!pMove->moved) {
    pMove->moved = TRUE;
  }
  
  sdl_dirty_rect(pMove->pWindow->size);
  
  pMove->pWindow->size.x += pMotionEvent->xrel;
  pMove->pWindow->size.y += pMotionEvent->yrel;
  set_window_pos(pMove->pWindow, pMove->pWindow->size.x,
                                 pMove->pWindow->size.y);
  
  sdl_dirty_rect(pMove->pWindow->size);
  flush_dirty();
  
  return ID_ERROR;
}

/**************************************************************************
...
**************************************************************************/
static Uint16 move_window_button_up(SDL_MouseButtonEvent * pButtonEvent, void *pData)
{
  struct MOVE *pMove = (struct MOVE *)pData;

  if (pMove && pMove->moved) {
    return (Uint16)ID_MOVED_WINDOW;
  }
  
  return (Uint16)ID_WINDOW;
}


/**************************************************************************
  ...
**************************************************************************/
bool move_window(struct widget *pWindow)
{
  bool ret;
  struct MOVE pMove;
  pMove.pWindow = pWindow;
  pMove.moved = FALSE;
  /* Filter mouse motion events */
  SDL_SetEventFilter(FilterMouseMotionEvents);
  ret = (gui_event_loop((void *)&pMove, NULL, NULL, NULL, NULL,
	  move_window_button_up, move_window_motion) == ID_MOVED_WINDOW);
  /* Turn off Filter mouse motion events */
  SDL_SetEventFilter(NULL);
  return ret;
}

/**************************************************************************
  Redraw Window Graphic ( without other Widgets )
**************************************************************************/
int redraw_window(struct widget *pWindow)
{
  SDL_Color title_bg_color = {255, 255, 255, 200};
  
  SDL_Surface *pTmp = NULL;
  SDL_Rect dst = pWindow->size;

  fix_rect(pWindow->dst, &dst);

  /* Draw theme */
  clear_surface(pWindow->dst, &dst);
  alphablit(pWindow->theme, NULL, pWindow->dst, &dst);

  /* window has title string == has title bar */
  if (pWindow->string16) {
    /* Draw Window's TitelBar */
    dst.h = WINDOW_TITLE_HEIGHT;
    SDL_FillRectAlpha(pWindow->dst, &dst, &title_bg_color);
    
    /* Draw Text on Window's TitelBar */
    pTmp = create_text_surf_from_str16(pWindow->string16);
    dst.x += 10;
    if(pTmp) {
      dst.y += ((WINDOW_TITLE_HEIGHT - pTmp->h) / 2);
      alphablit(pTmp, NULL, pWindow->dst, &dst);
      FREESURFACE(pTmp);
    }

    dst = pWindow->size;    
    fix_rect(pWindow->dst, &dst);
    
    putline(pWindow->dst, dst.x + pTheme->FR_Left->w,
	  dst.y + WINDOW_TITLE_HEIGHT,
	  dst.x + pWindow->size.w - pTheme->FR_Right->w,
	  dst.y + WINDOW_TITLE_HEIGHT, 
          map_rgba(pWindow->dst->format, 
           *get_game_colorRGB(COLOR_THEME_WINDOW_TITLEBAR_SEPARATOR)));    
  }
  
  return 0;
}
