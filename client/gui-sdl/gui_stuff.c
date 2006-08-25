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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <SDL/SDL.h>

#include "log.h"

/* gui-sdl */
#include "colors.h"
#include "graphics.h"
#include "gui_iconv.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"
#include "unistring.h"
#include "widget_p.h"

#include "gui_stuff.h"

struct widget *pSellected_Widget;
SDL_Rect *pInfo_Area = NULL;
SDL_Surface *pInfo_Label = NULL;

extern Uint32 widget_info_counter;

/* ================================ Private ============================ */

struct UP_DOWN {
  struct widget *pBegin;
  struct widget *pEnd;
  struct widget *pBeginWidgetLIST;
  struct widget *pEndWidgetLIST;
  struct ScrollBar *pVscroll;
  int old_y;
  int step;
};

static struct widget *pBeginMainWidgetList;
/* static struct widget *pEndMainWidgetList; */

#define UpperAdd(pNew_Widget, pAdd_Dock)	\
do {						\
  pNew_Widget->prev = pAdd_Dock;		\
  pNew_Widget->next = pAdd_Dock->next;		\
  if(pAdd_Dock->next) {			\
    pAdd_Dock->next->prev = pNew_Widget;	\
  }						\
  pAdd_Dock->next = pNew_Widget;		\
} while(0)

static void remove_buffer_layer(SDL_Surface *pBuffer);

/**************************************************************************
  Correct backgroud size ( set min size ). Used in create widget
  functions.
**************************************************************************/
void correct_size_bcgnd_surf(SDL_Surface * pTheme,
				    Uint16 * pWidth, Uint16 * pHigh)
{
  *pWidth = MAX(*pWidth, 2 * (pTheme->w / adj_size(16)));
  *pHigh = MAX(*pHigh, 2 * (pTheme->h / adj_size(16)));
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
**************************************************************************/
SDL_Surface *create_bcgnd_surf(SDL_Surface * pTheme, Uint8 state,
                               Uint16 Width, Uint16 High)
{
  bool zoom;
  int iTile_width_len_end, iTile_width_len_mid, iTile_count_len_mid;
  int iTile_width_high_end, iTile_width_high_mid, iTile_count_high_mid;
  int i, j;

  SDL_Rect src, des;
  SDL_Surface *pBackground = NULL;

  int iStart_y = (pTheme->h / 4) * state;

  iTile_width_len_end = pTheme->w / 16;
  iTile_width_len_mid = pTheme->w - (iTile_width_len_end * 2);

  iTile_count_len_mid =
      (Width - (iTile_width_len_end * 2)) / iTile_width_len_mid;

  /* corrections I */
  if (((iTile_count_len_mid *
	iTile_width_len_mid) + (iTile_width_len_end * 2)) < Width) {
    iTile_count_len_mid++;
  }

  iTile_width_high_end = pTheme->h / 16;
  iTile_width_high_mid = (pTheme->h / 4) - (iTile_width_high_end * 2);
  iTile_count_high_mid =
      (High - (iTile_width_high_end * 2)) / iTile_width_high_mid;

  /* corrections II */
  if (((iTile_count_high_mid *
	iTile_width_high_mid) + (iTile_width_high_end * 2)) < High) {
    iTile_count_high_mid++;
  }

  i = MAX(iTile_width_len_end * 2, Width);
  j = MAX(iTile_width_high_end * 2, High);
  zoom = ((i != Width) ||  (j != High));
  
  /* now allocate memory */
  pBackground = create_surf_alpha(i, j, SDL_SWSURFACE);

  /* copy left end */

  /* copy left top end */
  src.x = 0;
  src.y = iStart_y;
  src.w = iTile_width_len_end;
  src.h = iTile_width_high_end;

  des.x = 0;
  des.y = 0;
  alphablit(pTheme, &src, pBackground, &des);

  /* copy left middels parts */
  src.y = iStart_y + iTile_width_high_end;
  src.h = iTile_width_high_mid;
  for (i = 0; i < iTile_count_high_mid; i++) {
    des.y = iTile_width_high_end + i * iTile_width_high_mid;
    alphablit(pTheme, &src, pBackground, &des);
  }

  /* copy left boton end */
  src.y = iStart_y + ((pTheme->h / 4) - iTile_width_high_end);
  src.h = iTile_width_high_end;
  des.y = pBackground->h - iTile_width_high_end;
  clear_surface(pBackground, &des);
  alphablit(pTheme, &src, pBackground, &des);

  /* copy middle parts without right end part */

  src.x = iTile_width_len_end;
  src.y = iStart_y;
  src.w = iTile_width_len_mid;

  for (i = 0; i < iTile_count_len_mid; i++) {

    /* top */
    des.x = iTile_width_len_end + i * iTile_width_len_mid;
    des.y = 0;
    src.y = iStart_y;
    alphablit(pTheme, &src, pBackground, &des);

    /*  middels */
    src.y = iStart_y + iTile_width_high_end;
    src.h = iTile_width_high_mid;
    for (j = 0; j < iTile_count_high_mid; j++) {
      des.y = iTile_width_high_end + j * iTile_width_high_mid;
      alphablit(pTheme, &src, pBackground, &des);
    }

    /* bottom */
    src.y = iStart_y + ((pTheme->h / 4) - iTile_width_high_end);
    src.h = iTile_width_high_end;
    des.y = pBackground->h - iTile_width_high_end;
    clear_surface(pBackground, &des);    
    alphablit(pTheme, &src, pBackground, &des);
  }

  /* copy right end */
  src.x = pTheme->w - iTile_width_len_end;
  src.y = iStart_y;
  src.w = iTile_width_len_end;

  des.x = pBackground->w - iTile_width_len_end;
  des.y = 0;

  alphablit(pTheme, &src, pBackground, &des);

  /*  middels */
  src.y = iStart_y + iTile_width_high_end;
  src.h = iTile_width_high_mid;
  for (i = 0; i < iTile_count_high_mid; i++) {
    des.y = iTile_width_high_end + i * iTile_width_high_mid;
    alphablit(pTheme, &src, pBackground, &des);
  }

  /*boton */
  src.y = iStart_y + ((pTheme->h / 4) - iTile_width_high_end);
  src.h = iTile_width_high_end;
  des.y = pBackground->h - iTile_width_high_end;
  clear_surface(pBackground, &des);  
  alphablit(pTheme, &src, pBackground, &des);
  
  if (zoom)
  {
    SDL_Surface *pZoom = ResizeSurface(pBackground, Width, High, 1);
    FREESURFACE(pBackground);
    pBackground = pZoom;
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
struct widget * WidgetListScaner(const struct widget *pGUI_List, int x, int y)
{
  while (pGUI_List) {
    if (is_in_rect_area(x, y, pGUI_List->size)
       && !((get_wstate(pGUI_List) == FC_WS_DISABLED) ||
	    ((get_wflags(pGUI_List) & WF_HIDDEN) == WF_HIDDEN))) {
      return (struct widget *) pGUI_List;
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
struct widget *WidgetListKeyScaner(const struct widget *pGUI_List, SDL_keysym Key)
{
  Key.mod &= ~(KMOD_NUM | KMOD_CAPS);
  while (pGUI_List) {
    if ((pGUI_List->key == Key.sym ||
      (pGUI_List->key == SDLK_RETURN && Key.sym == SDLK_KP_ENTER) ||
      (pGUI_List->key == SDLK_KP_ENTER && Key.sym == SDLK_RETURN)) &&
	((pGUI_List->mod & Key.mod) || (pGUI_List->mod == Key.mod))) {
      if (!((get_wstate(pGUI_List) == FC_WS_DISABLED) ||
	    ((get_wflags(pGUI_List) & WF_HIDDEN) == WF_HIDDEN))) {
	return (struct widget *) pGUI_List;
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
struct widget *MainWidgetListScaner(int x, int y)
{
  return WidgetListScaner(pBeginMainWidgetList, x, y);
}

/**************************************************************************
  ...
**************************************************************************/
struct widget *MainWidgetListKeyScaner(SDL_keysym Key)
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
Uint16 widget_pressed_action(struct widget * pWidget)
{
  Uint16 ID = 0;

  if (!pWidget) {
    return 0;
  }
  
  widget_info_counter = 0;
  if (pInfo_Area) {
    sdl_dirty_rect(*pInfo_Area);
    FC_FREE(pInfo_Area);
    FREESURFACE(pInfo_Label);
  }
  
  switch (get_wtype(pWidget)) {
  case WT_TI_BUTTON:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_tibutton(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      SDL_Delay(300);
    }
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_I_BUTTON:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_ibutton(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      SDL_Delay(300);
    }
    ID = pWidget->ID;  
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_ICON:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_icon(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      SDL_Delay(300);
    }
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_ICON2:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_icon2(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      SDL_Delay(300);
    }
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_EDIT:
  {
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      bool ret, loop = ((get_wflags(pWidget) & WF_EDIT_LOOP) == WF_EDIT_LOOP);
      enum Edit_Return_Codes change;
      do {
        ret = FALSE;
        change = edit_field(pWidget);
        if (change != ED_FORCE_EXIT && (!loop || change != ED_RETURN)) {
          redraw_edit(pWidget);
          sdl_dirty_rect(pWidget->size);
          flush_dirty();
        }
        if (change != ED_FORCE_EXIT && change != ED_ESC && pWidget->action) {
          if (pWidget->action(pWidget)) {
            ID = 0;
          }
        }
        if (loop && change == ED_RETURN) {
          ret = TRUE;
        }
      } while(ret);
      ID = 0;
    }
    break;
  }
  case WT_VSCROLLBAR:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      redraw_vert(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
    }
    ID = pWidget->ID;
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_HSCROLLBAR:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {    
      set_wstate(pWidget, FC_WS_PRESSED);
      redraw_horiz(pWidget);
      flush_rect(pWidget->size, FALSE);
    }
    ID = pWidget->ID;  
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_CHECKBOX:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      real_redraw_icon(pWidget);
      sdl_dirty_rect(pWidget->size);
      flush_dirty();
      set_wstate(pWidget, FC_WS_SELLECTED);
      togle_checkbox(pWidget);
      SDL_Delay(300);
    }
    ID = pWidget->ID;  
    if (pWidget->action) {
      if (pWidget->action(pWidget)) {
	ID = 0;
      }
    }
    break;
  case WT_TCHECKBOX:
    if (Main.event.button.button == SDL_BUTTON_LEFT) {
      set_wstate(pWidget, FC_WS_PRESSED);
      redraw_textcheckbox(pWidget);
      flush_rect(pWidget->size, FALSE);
      set_wstate(pWidget, FC_WS_SELLECTED);
      togle_checkbox(pWidget);
      SDL_Delay(300);
    }
    ID = pWidget->ID;
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
    if (get_wstate(pSellected_Widget) == FC_WS_DISABLED) {
      goto End;
    }

    set_wstate(pSellected_Widget, FC_WS_NORMAL);

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

    flush_rect(pSellected_Widget->size, FALSE);

    /* turn off quick info timer/counter */ 
    widget_info_counter = 0;

  }

End:

  if (pInfo_Area) {
    flush_rect(*pInfo_Area, FALSE);
    FC_FREE(pInfo_Area);
    FREESURFACE(pInfo_Label);    
  }

  pSellected_Widget = NULL;
}

/**************************************************************************
  Sellect widget.  Redraw this widget;
**************************************************************************/
void widget_sellected_action(struct widget *pWidget)
{
  if (!pWidget || pWidget == pSellected_Widget) {
    return;
  }

  if (pSellected_Widget) {
    unsellect_widget_action();
  }

  switch (get_wtype(pWidget)) {
  case WT_TI_BUTTON:
    set_wstate(pWidget, FC_WS_SELLECTED);
    real_redraw_tibutton(pWidget);
    break;
  case WT_I_BUTTON:
    set_wstate(pWidget, FC_WS_SELLECTED);
    real_redraw_ibutton(pWidget);
    break;
  case WT_ICON:
  case WT_CHECKBOX:
    set_wstate(pWidget, FC_WS_SELLECTED);
    real_redraw_icon(pWidget);
    break;
  case WT_ICON2:
    set_wstate(pWidget, FC_WS_SELLECTED);
    real_redraw_icon2(pWidget);
    break;
  case WT_TCHECKBOX:
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_textcheckbox(pWidget);
    break;
  case WT_I_LABEL:
  case WT_T2_LABEL:  
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_label(pWidget);
    break;
  case WT_VSCROLLBAR:
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_vert(pWidget);
    break;
  case WT_HSCROLLBAR:
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_horiz(pWidget);
    break;
  default:
    break;
  }

  if (get_wstate(pWidget) == FC_WS_SELLECTED) {
    flush_rect(pWidget->size, FALSE);
    pSellected_Widget = pWidget;
    if (get_wflags(pWidget) & WF_WIDGET_HAS_INFO_LABEL) {
      widget_info_counter = 1;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void redraw_widget_info_label(SDL_Rect *rect)
{
  SDL_Surface *pText, *pBcgd;
  SDL_Rect srcrect, dstrect;
  SDL_Color color;

  struct widget *pWidget = pSellected_Widget;

  if (!pWidget) {
    return;
  }

  if (!pInfo_Label) {
  
    pInfo_Area = fc_calloc(1, sizeof(SDL_Rect));
  
    /*pWidget->string16->render = 3;*/
    
    color = pWidget->string16->fgcol;
    pWidget->string16->style |= TTF_STYLE_BOLD;
    pWidget->string16->fgcol = *get_game_colorRGB(COLOR_THEME_QUICK_INFO_TEXT);
    
    /* create string and bcgd theme */
    pText = create_text_surf_from_str16(pWidget->string16);

    pWidget->string16->fgcol = color;
    
    pBcgd = create_filled_surface(pText->w + adj_size(10), pText->h + adj_size(6),
              SDL_SWSURFACE, get_game_colorRGB(COLOR_THEME_QUICK_INFO_BG), TRUE);
    
    /* calculate start position */
    if (pWidget->size.y - pBcgd->h - adj_size(6) < 0) {
      pInfo_Area->y = pWidget->size.y + pWidget->size.h + adj_size(3);
    } else {
      pInfo_Area->y = pWidget->size.y - pBcgd->h - adj_size(5);
    }
  
    if (pWidget->size.x + pBcgd->w + adj_size(5) > Main.screen->w) {
      pInfo_Area->x = pWidget->size.x - pBcgd->w - adj_size(5);
    } else {
      pInfo_Area->x = pWidget->size.x + adj_size(3);
    }
  
    pInfo_Area->w = pBcgd->w + adj_size(2);
    pInfo_Area->h = pBcgd->h + adj_size(3);

    pInfo_Label = SDL_DisplayFormatAlpha(pBcgd);

    FREESURFACE(pBcgd);
    
    /* draw text */
    dstrect.x = 6;
    dstrect.y = 3;
    
    alphablit(pText, NULL, pInfo_Label, &dstrect);
    
    FREESURFACE(pText);    
    
    /* draw frame */
    putframe(pInfo_Label, 0, 0,
         pInfo_Label->w - 1, pInfo_Label->h - 1,
         map_rgba(pInfo_Label->format, *get_game_colorRGB(COLOR_THEME_QUICK_INFO_FRAME)));
    
  }

  if (rect) {
    dstrect.x = MAX(rect->x, pInfo_Area->x);
    dstrect.y = MAX(rect->y, pInfo_Area->y);
    
    srcrect.x = dstrect.x - pInfo_Area->x;
    srcrect.y = dstrect.y - pInfo_Area->y;
    srcrect.w = MIN((pInfo_Area->x + pInfo_Area->w), (rect->x + rect->w)) - dstrect.x;
    srcrect.h = MIN((pInfo_Area->y + pInfo_Area->h), (rect->y + rect->h)) - dstrect.y;

    alphablit(pInfo_Label, &srcrect, Main.screen, &dstrect);
  } else {
    alphablit(pInfo_Label, NULL, Main.screen, pInfo_Area);
  }

  if (correct_rect_region(pInfo_Area)) {
    SDL_UpdateRect(Main.screen, pInfo_Area->x, pInfo_Area->y,
				    pInfo_Area->w, pInfo_Area->h);
  }
  
  return;
}


/**************************************************************************
  Find ID in Widget's List ('pGUI_List') and return pointer to this
  Widgets.
**************************************************************************/
struct widget *get_widget_pointer_form_ID(const struct widget *pGUI_List,
				       Uint16 ID, enum scan_direction direction)
{
  while (pGUI_List) {
    if (pGUI_List->ID == ID) {
      return (struct widget *) pGUI_List;
    }
    if (direction == SCAN_FORWARD)
    pGUI_List = pGUI_List->next;
    else
      pGUI_List = pGUI_List->prev;  
  }
  return NULL;
}

/**************************************************************************
  Find ID in MAIN Widget's List ( pBeginWidgetList ) and return pointer to
  this Widgets.
**************************************************************************/
struct widget *get_widget_pointer_form_main_list(Uint16 ID)
{
  return get_widget_pointer_form_ID(pBeginMainWidgetList, ID, SCAN_FORWARD);
}

/**************************************************************************
  INIT Main Widget's List ( pBeginWidgetList )
**************************************************************************/
void init_gui_list(Uint16 ID, struct widget *pGUI)
{
  /* pEndWidgetList = pBeginWidgetList = pGUI; */
  pBeginMainWidgetList = pGUI;
  pBeginMainWidgetList->ID = ID;
}

/**************************************************************************
  Add Widget to Main Widget's List ( pBeginWidgetList )
**************************************************************************/
void add_to_gui_list(Uint16 ID, struct widget *pGUI)
{
  pGUI->next = pBeginMainWidgetList;
  pGUI->ID = ID;
  pBeginMainWidgetList->prev = pGUI;
  pBeginMainWidgetList = pGUI;
}

/**************************************************************************
  Add Widget to Widget's List at pAdd_Dock position on 'prev' slot.
**************************************************************************/
void DownAdd(struct widget *pNew_Widget, struct widget *pAdd_Dock)
{
  pNew_Widget->next = pAdd_Dock;
  pNew_Widget->prev = pAdd_Dock->prev;
  if (pAdd_Dock->prev) {
    pAdd_Dock->prev->next = pNew_Widget;
  }
  pAdd_Dock->prev = pNew_Widget;
  if (pAdd_Dock == pBeginMainWidgetList) {
    pBeginMainWidgetList = pNew_Widget;
  }
}

/**************************************************************************
  Delete Widget from Main Widget's List ( pBeginWidgetList )

  NOTE: This function does not destroy Widget, only remove his pointer from
  list. To destroy this Widget totaly ( free mem... ) call macro:
  del_widget_from_gui_list( pWidget ).  This macro call this function.
**************************************************************************/
void del_widget_pointer_from_gui_list(struct widget *pGUI)
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

  NOTE: This is used by My (move) GUI Window mechanism.  Return TRUE if is
  first.
**************************************************************************/
bool is_this_widget_first_on_list(struct widget *pGUI)
{
  return (pBeginMainWidgetList == pGUI);
}

/**************************************************************************
  Move pointer to Widget to list begin.

  NOTE: This is used by My GUI Window mechanism.
**************************************************************************/
void move_widget_to_front_of_gui_list(struct widget *pGUI)
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
void del_gui_list(struct widget *pGUI_List)
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
Uint16 redraw_group(const struct widget *pBeginGroupWidgetList,
		    const struct widget *pEndGroupWidgetList,
		      int add_to_update)
{
  Uint16 count = 0;
  struct widget *pTmpWidget = (struct widget *) pEndGroupWidgetList;

  while (pTmpWidget) {

    if ((get_wflags(pTmpWidget) & WF_HIDDEN) != WF_HIDDEN) {

      if (!(pTmpWidget->gfx) &&
	  (get_wflags(pTmpWidget) & WF_RESTORE_BACKGROUND)) {
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
void set_new_group_start_pos(const struct widget *pBeginGroupWidgetList,
			     const struct widget *pEndGroupWidgetList,
			     Sint16 Xrel, Sint16 Yrel)
{
  struct widget *pTmpWidget = (struct widget *) pEndGroupWidgetList;

  while (pTmpWidget) {

    pTmpWidget->size.x += Xrel;
    pTmpWidget->size.y += Yrel;

    if (get_wtype(pTmpWidget) == WT_VSCROLLBAR
      && pTmpWidget->private_data.adv_dlg
      && pTmpWidget->private_data.adv_dlg->pScroll) {
      pTmpWidget->private_data.adv_dlg->pScroll->max += Yrel;
      pTmpWidget->private_data.adv_dlg->pScroll->min += Yrel;
    }
    
    if (get_wtype(pTmpWidget) == WT_HSCROLLBAR
      && pTmpWidget->private_data.adv_dlg
      && pTmpWidget->private_data.adv_dlg->pScroll) {
      pTmpWidget->private_data.adv_dlg->pScroll->max += Xrel;
      pTmpWidget->private_data.adv_dlg->pScroll->min += Xrel;
    }
    
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
void move_group_to_front_of_gui_list(struct widget *pBeginGroupWidgetList,
				     struct widget *pEndGroupWidgetList)
{
  struct widget *pTmpWidget = pEndGroupWidgetList , *pPrev = NULL;
  struct gui_layer *gui_layer = get_gui_layer(pEndGroupWidgetList->dst);
  
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
  if (gui_layer) {
    int i = 0;
    while ((i < Main.guis_count - 1) && Main.guis[i]) {
      if (Main.guis[i] && Main.guis[i + 1] && (Main.guis[i] == gui_layer)) {
        Main.guis[i] = Main.guis[i + 1];
	Main.guis[i + 1] = gui_layer;
      }
      i++;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void del_group_of_widgets_from_gui_list(struct widget *pBeginGroupWidgetList,
					struct widget *pEndGroupWidgetList)
{
  struct widget *pBufWidget = NULL;
  struct widget *pTmpWidget = pEndGroupWidgetList;

  if (!pEndGroupWidgetList)
	return;
  
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
void set_group_state(struct widget *pBeginGroupWidgetList,
		     struct widget *pEndGroupWidgetList, enum widget_state state)
{
  struct widget *pTmpWidget = pEndGroupWidgetList;
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
void hide_group(struct widget *pBeginGroupWidgetList,
		struct widget *pEndGroupWidgetList)
{
  struct widget *pTmpWidget = pEndGroupWidgetList;

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
void show_group(struct widget *pBeginGroupWidgetList,
		struct widget *pEndGroupWidgetList)
{
  struct widget *pTmpWidget = pEndGroupWidgetList;

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
int redraw_widget(struct widget *pWidget)
{
  if (!pWidget && (get_wflags(pWidget) & WF_HIDDEN)) {
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
    return -2;
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
void popdown_window_group_dialog(struct widget *pBeginGroupWidgetList,
				 struct widget *pEndGroupWidgetList)
{
  if ((pBeginGroupWidgetList) && (pEndGroupWidgetList)) {
    if(pEndGroupWidgetList->dst == Main.gui) {
      /* undraw window */
      SDL_Rect dstrect;
      dstrect.x = pEndGroupWidgetList->size.x;
      dstrect.y = pEndGroupWidgetList->size.y;
      dstrect.w = pEndGroupWidgetList->size.w;
      dstrect.h = pEndGroupWidgetList->size.h;
      clear_surface(pEndGroupWidgetList->dst, &dstrect);
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
bool sellect_window_group_dialog(struct widget *pBeginWidgetList,
							 struct widget *pWindow)
{
  if (!is_this_widget_first_on_list(pBeginWidgetList)) {
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
bool move_window_group_dialog(struct widget *pBeginGroupWidgetList,
			     struct widget *pEndGroupWidgetList)
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
void move_window_group(struct widget *pBeginWidgetList, struct widget *pWindow)
{
  if (sellect_window_group_dialog(pBeginWidgetList, pWindow)) {
    flush_rect(pWindow->size, FALSE);
  }
  
  move_window_group_dialog(pBeginWidgetList, pWindow);
}


/* =================================================== */
/* ============ Vertical Scroll Group List =========== */
/* =================================================== */

/**************************************************************************
  scroll pointers on list.
  dir == directions: up == -1, down == 1.
**************************************************************************/
static struct widget *vertical_scroll_widget_list(struct widget *pActiveWidgetLIST,
				      struct widget *pBeginWidgetLIST,
				      struct widget *pEndWidgetLIST,
				      int active, int step, int dir)
{
  struct widget *pBegin = pActiveWidgetLIST;
  struct widget *pBuf = pActiveWidgetLIST;
  struct widget *pTmp = NULL;  
  int count = active; /* row */
  int count_step = step; /* col */
    
  if (dir < 0) {
    bool real = TRUE;
    /* up */
    if (pBuf != pEndWidgetLIST) {
      /*
       move pointers to positions and unhidde scrolled widgets
       B = pBuf - new top
       T = pTmp - current top == pActiveWidgetLIST
       [B] [ ] [ ]
       -----------
       [T] [ ] [ ]
       [ ] [ ] [ ]
       -----------
       [ ] [ ] [ ]
    */
      pTmp = pBuf; /* now pBuf == pActiveWidgetLIST == current Top */
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
       [T3] [T4] [T5]
       -----------
       [  ] [  ] [  ]
      
       start from B0 and go downd list
       B0 = T0, B1 = T1, B2 = T2
       T0 = T3, T1 = T4, T2 = T5
       etc...
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
	  /*
	     unsymetric list support.
	     This is big problem becouse we can't take position from no exist
	     list memeber. We must put here some hypotetical positions
	  
	     [B0] [B1] [B2]
             --------------
             [T0] [T1]
	  
	  */
	  if (active > 1) {
	    /* this work good if active > 1 but is buggy when active == 1 */
	    pBuf->size.y += pBuf->size.h;
	  } else {
	    /* this work good if active == 1 but may be broken if "next"
	       element have another "y" position */
	    pBuf->size.y = pBuf->next->size.y;
	  }
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
    count = active * step; /* row * col */
    
    /*
       find end
       B = pBuf
       A - start (pBuf == pActiveWidgetLIST)
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
       A - start (pActiveWidgetLIST)
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
       Unsymetric list support.
       correct pTmp and undraw empty fields
       B = pBuf
       T = pTmp
       A - start (pActiveWidgetLIST)
       [ ] [ ] [ ]
       -----------
       [A] [ ] [ ]
       [ ] [T] [U]  <- undraw U
       -----------
       [ ] [B]
    */
      count = count_step;
      while(count) {
	/* hack - clear area under no exist list members */
	dst = pTmp->size;
        fix_rect(pTmp->dst, &dst);
        clear_surface(pTmp->dst, &dst);
        alphablit(pTmp->gfx, NULL, pTmp->dst, &dst);
	sdl_dirty_rect(pTmp->size);
	FREESURFACE(pTmp->gfx);
	if (active == 1) {
	  set_wflag(pTmp, WF_HIDDEN);
	}
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

/**************************************************************************
  ...
**************************************************************************/
static int get_step(struct ScrollBar *pScroll)
{
  float step = pScroll->max - pScroll->min;
  step *= (float) (1.0 - (float) (pScroll->active * pScroll->step) /
						  (float)pScroll->count);
  step /= (float)(pScroll->count - pScroll->active * pScroll->step);
  step *= (float)pScroll->step;
  step++;
  return (int)step;
}

/**************************************************************************
  ...
**************************************************************************/
static int get_position(struct ADVANCED_DLG *pDlg)
{
  struct widget *pBuf = pDlg->pActiveWidgetList;
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
 
  if (pDlg->pScroll->pScrollBar) {
  return pDlg->pScroll->max - pDlg->pScroll->pScrollBar->size.h -
					count * (float)step / pDlg->pScroll->step;
  } else {
    return pDlg->pScroll->max - count * (float)step / pDlg->pScroll->step;
  }
}

/**************************************************************************
  ...
**************************************************************************/
static void inside_scroll_down_loop(void *pData)
{
  struct UP_DOWN *pDown = (struct UP_DOWN *)pData;
  SDL_Rect dest;  
  
  if (pDown->pEnd != pDown->pBeginWidgetLIST) {
      if (pDown->pVscroll->pScrollBar
	&& pDown->pVscroll->pScrollBar->size.y <=
	  pDown->pVscroll->max - pDown->pVscroll->pScrollBar->size.h) {
            
	/* draw bcgd */
        dest = pDown->pVscroll->pScrollBar->size;
        fix_rect(pDown->pVscroll->pScrollBar->dst, &dest);
        clear_surface(pDown->pVscroll->pScrollBar->dst, &dest);
	blit_entire_src(pDown->pVscroll->pScrollBar->gfx,
	    		pDown->pVscroll->pScrollBar->dst,
			dest.x, dest.y);
	sdl_dirty_rect(pDown->pVscroll->pScrollBar->size);

	if (pDown->pVscroll->pScrollBar->size.y + pDown->step >
	    pDown->pVscroll->max - pDown->pVscroll->pScrollBar->size.h) {
	  pDown->pVscroll->pScrollBar->size.y =
	      pDown->pVscroll->max - pDown->pVscroll->pScrollBar->size.h;
	} else {
	  pDown->pVscroll->pScrollBar->size.y += pDown->step;
	}
      }

      pDown->pBegin = vertical_scroll_widget_list(pDown->pBegin,
			  pDown->pBeginWidgetLIST, pDown->pEndWidgetLIST,
			  pDown->pVscroll->active, pDown->pVscroll->step, 1);

      pDown->pEnd = pDown->pEnd->prev;

      redraw_group(pDown->pBeginWidgetLIST, pDown->pEndWidgetLIST, TRUE);

      if (pDown->pVscroll->pScrollBar) {
	/* redraw scrollbar */
	refresh_widget_background(pDown->pVscroll->pScrollBar);
	redraw_vert(pDown->pVscroll->pScrollBar);

	sdl_dirty_rect(pDown->pVscroll->pScrollBar->size);
      }

      flush_dirty();
    }
}

/**************************************************************************
  ...
**************************************************************************/
static void inside_scroll_up_loop(void *pData)
{
  struct UP_DOWN *pUp = (struct UP_DOWN *)pData;
  SDL_Rect dest;  
  
  if (pUp && pUp->pBegin != pUp->pEndWidgetLIST) {

    if (pUp->pVscroll->pScrollBar
      && (pUp->pVscroll->pScrollBar->size.y >= pUp->pVscroll->min)) {

      /* draw bcgd */
      dest = pUp->pVscroll->pScrollBar->size;
      fix_rect(pUp->pVscroll->pScrollBar->dst, &dest);
      clear_surface(pUp->pVscroll->pScrollBar->dst, &dest);
      blit_entire_src(pUp->pVscroll->pScrollBar->gfx,
			pUp->pVscroll->pScrollBar->dst,
			dest.x, dest.y);
      sdl_dirty_rect(pUp->pVscroll->pScrollBar->size);

      if (((pUp->pVscroll->pScrollBar->size.y - pUp->step) < pUp->pVscroll->min)) {
	pUp->pVscroll->pScrollBar->size.y = pUp->pVscroll->min;
      } else {
	pUp->pVscroll->pScrollBar->size.y -= pUp->step;
      }
    }

    pUp->pBegin = vertical_scroll_widget_list(pUp->pBegin,
			pUp->pBeginWidgetLIST, pUp->pEndWidgetLIST,
			pUp->pVscroll->active, pUp->pVscroll->step, -1);

    redraw_group(pUp->pBeginWidgetLIST, pUp->pEndWidgetLIST, TRUE);

    if (pUp->pVscroll->pScrollBar) {
      /* redraw scroolbar */
      refresh_widget_background(pUp->pVscroll->pScrollBar);
      redraw_vert(pUp->pVscroll->pScrollBar);
      sdl_dirty_rect(pUp->pVscroll->pScrollBar->size);
    }

    flush_dirty();
  }
}

/**************************************************************************
  FIXME
**************************************************************************/
static Uint16 scroll_mouse_motion_handler(SDL_MouseMotionEvent *pMotionEvent, void *pData)
{
  struct UP_DOWN *pMotion = (struct UP_DOWN *)pData;
  SDL_Rect dest;  
  
  if (pMotion && pMotionEvent->yrel &&
    /*(old_y >= pMotion->pVscroll->min) && (old_y <= pMotion->pVscroll->max) &&*/
    (pMotionEvent->y >= pMotion->pVscroll->min) &&
    (pMotionEvent->y <= pMotion->pVscroll->max)) {
    int count;
    div_t tmp;
      
    /* draw bcgd */
    dest = pMotion->pVscroll->pScrollBar->size;
    fix_rect(pMotion->pVscroll->pScrollBar->dst, &dest);
    clear_surface(pMotion->pVscroll->pScrollBar->dst, &dest);
    blit_entire_src(pMotion->pVscroll->pScrollBar->gfx,
       			pMotion->pVscroll->pScrollBar->dst,
			dest.x, dest.y);
    sdl_dirty_rect(pMotion->pVscroll->pScrollBar->size);

    if ((pMotion->pVscroll->pScrollBar->size.y + pMotionEvent->yrel) >
	 (pMotion->pVscroll->max - pMotion->pVscroll->pScrollBar->size.h)) {
      pMotion->pVscroll->pScrollBar->size.y =
	      pMotion->pVscroll->max - pMotion->pVscroll->pScrollBar->size.h;
    } else {
      if ((pMotion->pVscroll->pScrollBar->size.y + pMotionEvent->yrel) <
	  pMotion->pVscroll->min) {
	pMotion->pVscroll->pScrollBar->size.y = pMotion->pVscroll->min;
      } else {
	pMotion->pVscroll->pScrollBar->size.y += pMotionEvent->yrel;
      }
    }
    
    count = pMotion->pVscroll->pScrollBar->size.y - pMotion->old_y;
    tmp = div(count, pMotion->step);
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

	pMotion->pBegin = vertical_scroll_widget_list(pMotion->pBegin,
			pMotion->pBeginWidgetLIST, pMotion->pEndWidgetLIST,
				pMotion->pVscroll->active,
				pMotion->pVscroll->step, count);

	if (count > 0) {
	  count--;
	} else {
	  count++;
	}

      }	/* while (count) */
      
      pMotion->old_y = pMotion->pVscroll->pScrollBar->size.y;
      redraw_group(pMotion->pBeginWidgetLIST, pMotion->pEndWidgetLIST, TRUE);
    }

    /* redraw scroolbar */
    refresh_widget_background(pMotion->pVscroll->pScrollBar);
    redraw_vert(pMotion->pVscroll->pScrollBar);
    sdl_dirty_rect(pMotion->pVscroll->pScrollBar->size);

    flush_dirty();
  }				/* if (count) */

  return ID_ERROR;
}

/**************************************************************************
  ...
**************************************************************************/
static Uint16 scroll_mouse_button_up(SDL_MouseButtonEvent *pButtonEvent, void *pData)
{
  return (Uint16)ID_SCROLLBAR;
}

/**************************************************************************
  ...
**************************************************************************/
static struct widget *down_scroll_widget_list(struct ScrollBar *pVscroll,
				    struct widget *pBeginActiveWidgetLIST,
				    struct widget *pBeginWidgetLIST,
				    struct widget *pEndWidgetLIST)
{
  struct UP_DOWN pDown;
  struct widget *pBegin = pBeginActiveWidgetLIST;
  int step = pVscroll->active * pVscroll->step - 1;

  while (step--) {
    pBegin = pBegin->prev;
  }

  pDown.step = get_step(pVscroll);
  pDown.pBegin = pBeginActiveWidgetLIST;
  pDown.pEnd = pBegin;
  pDown.pBeginWidgetLIST = pBeginWidgetLIST;
  pDown.pEndWidgetLIST = pEndWidgetLIST;
  pDown.pVscroll = pVscroll;
  
  gui_event_loop((void *)&pDown, inside_scroll_down_loop,
	NULL, NULL, NULL, scroll_mouse_button_up, NULL);
  
  return pDown.pBegin;
}

/**************************************************************************
  ...
**************************************************************************/
static struct widget *up_scroll_widget_list(struct ScrollBar *pVscroll,
				  struct widget *pBeginActiveWidgetLIST,
				  struct widget *pBeginWidgetLIST,
				  struct widget *pEndWidgetLIST)
{
  struct UP_DOWN pUp;
      
  pUp.step = get_step(pVscroll);
  pUp.pBegin = pBeginActiveWidgetLIST;
  pUp.pBeginWidgetLIST = pBeginWidgetLIST;
  pUp.pEndWidgetLIST = pEndWidgetLIST;
  pUp.pVscroll = pVscroll;
  
  gui_event_loop((void *)&pUp, inside_scroll_up_loop,
	NULL, NULL, NULL, scroll_mouse_button_up, NULL);
  
  return pUp.pBegin;
}

/**************************************************************************
  FIXME
**************************************************************************/
static struct widget *vertic_scroll_widget_list(struct ScrollBar *pVscroll,
				      struct widget *pBeginActiveWidgetLIST,
				      struct widget *pBeginWidgetLIST,
				      struct widget *pEndWidgetLIST)
{
  struct UP_DOWN pMotion;
      
  pMotion.step = get_step(pVscroll);
  pMotion.pBegin = pBeginActiveWidgetLIST;
  pMotion.pBeginWidgetLIST = pBeginWidgetLIST;
  pMotion.pEndWidgetLIST = pEndWidgetLIST;
  pMotion.pVscroll = pVscroll;
  pMotion.old_y = pVscroll->pScrollBar->size.y;
  
  MOVE_STEP_X = 0;
  MOVE_STEP_Y = 3;
  /* Filter mouse motion events */
  SDL_SetEventFilter(FilterMouseMotionEvents);
  gui_event_loop((void *)&pMotion, NULL, NULL, NULL, NULL,
		  scroll_mouse_button_up, scroll_mouse_motion_handler);
  /* Turn off Filter mouse motion events */
  SDL_SetEventFilter(NULL);
  MOVE_STEP_X = DEFAULT_MOVE_STEP;
  MOVE_STEP_Y = DEFAULT_MOVE_STEP;
  
  return pMotion.pBegin;
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
				      struct widget *pNew_Widget,
				      struct widget *pAdd_Dock, bool dir,
					Sint16 start_x, Sint16 start_y)
{
  struct widget *pBuf = NULL;
  struct widget *pEnd = NULL, *pOld_End = NULL;
  int count = 0;
  bool last = FALSE, seen = TRUE;
  SDL_Rect dest;
  
  assert(pNew_Widget != NULL);
  assert(pDlg != NULL);
  assert(pDlg->pScroll != NULL);
  
  if(!pAdd_Dock) {
    pAdd_Dock = pDlg->pBeginWidgetList;
  }
  
  pDlg->pScroll->count++;
  
  if(pDlg->pScroll->count > pDlg->pScroll->active * pDlg->pScroll->step) {
    if(pDlg->pActiveWidgetList) {
      int i = 0;
      /* find last active widget */
      pOld_End = pAdd_Dock;
      while(pOld_End != pDlg->pActiveWidgetList) {
        pOld_End = pOld_End->next;
	i++;
	if (pOld_End == pDlg->pEndActiveWidgetList) {
	  seen = FALSE;
	  break;
	}
      }
      if (seen) {
        count = pDlg->pScroll->active * pDlg->pScroll->step - 1;
        if (i > count) {
	  seen = FALSE;
        } else {
          while(count) {
	    pOld_End = pOld_End->prev;
	    count--;
          }
          if(pOld_End == pAdd_Dock) {
	    last = TRUE;
          }
	}
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
  if (seen) {
    if(!pDlg->pBeginActiveWidgetList) {
      /* first element ( active list empty ) */
      if(dir) {
	die("Forbided List Operation");
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
      } else { /* !last */
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
	    struct widget *pTmp = pBuf;
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
      }/* !last */
    } /* pDlg->pBeginActiveWidgetList */
  } else {/* !seen */
    set_wflag(pNew_Widget, WF_HIDDEN);
  }
  
  if(pDlg->pActiveWidgetList && pDlg->pScroll->pScrollBar) {
    dest = pDlg->pScroll->pScrollBar->size;
    fix_rect(pDlg->pScroll->pScrollBar->dst, &dest);
    clear_surface(pDlg->pScroll->pScrollBar->dst, &dest);
    blit_entire_src(pDlg->pScroll->pScrollBar->gfx,
    		    pDlg->pScroll->pScrollBar->dst,
		    dest.x, dest.y);
    sdl_dirty_rect(pDlg->pScroll->pScrollBar->size);
    pDlg->pScroll->pScrollBar->size.h = scrollbar_size(pDlg->pScroll);
    if(last) {
      pDlg->pScroll->pScrollBar->size.y = get_position(pDlg);
    }
    refresh_widget_background(pDlg->pScroll->pScrollBar);
    if (!seen) {
      redraw_vert(pDlg->pScroll->pScrollBar);
    }
  }

  return last;  
}

/**************************************************************************
  Del widget from srolled list and set draw position of all changed widgets
  Don't free pDlg and pDlg->pScroll (if exist)
  It is full secure for multi widget list case.
**************************************************************************/
bool del_widget_from_vertical_scroll_widget_list(struct ADVANCED_DLG *pDlg, 
  						struct widget *pWidget)
{
  int count = 0;
  struct widget *pBuf = pWidget;
  assert(pWidget != NULL);
  assert(pDlg != NULL);
  SDL_Rect dst;

  /* if begin == end -> size = 1 */
  if (pDlg->pBeginActiveWidgetList ==
      pDlg->pEndActiveWidgetList) {
    if(pDlg->pScroll) {
      pDlg->pScroll->count = 0;
      if(!pDlg->pScroll->pUp_Left_Button && 
	!pDlg->pScroll->pScrollBar &&
        !pDlg->pScroll->pDown_Right_Button) {
        pDlg->pBeginWidgetList = NULL;
        pDlg->pEndWidgetList = NULL;
      }
      if(pDlg->pBeginActiveWidgetList == pDlg->pBeginWidgetList) {
	pDlg->pBeginWidgetList = pDlg->pBeginWidgetList->next;
      }
      if(pDlg->pEndActiveWidgetList == pDlg->pEndWidgetList) {
	pDlg->pEndWidgetList = pDlg->pEndWidgetList->prev;
      }
      pDlg->pBeginActiveWidgetList = NULL;
      pDlg->pActiveWidgetList = NULL;
      pDlg->pEndActiveWidgetList = NULL;
    } else {
      pDlg->pBeginWidgetList = NULL;
      pDlg->pEndWidgetList = NULL;
    }
    dst = pWidget->size;
    fix_rect(pWidget->dst, &dst);
    clear_surface(pWidget->dst, &dst);
    alphablit(pWidget->gfx, NULL, pWidget->dst, &dst);
    sdl_dirty_rect(pWidget->size);
    del_widget_from_gui_list(pWidget);
    return FALSE;
  }
    
  if (pDlg->pScroll && pDlg->pActiveWidgetList) {
    /* scrollbar exist and active, start mod. from last seen label */
    
    struct widget *pLast;
      
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
        dst = pBuf->size;
        fix_rect(pBuf->dst, &dst);
        clear_surface(pBuf->dst, &dst);
        blit_entire_src(pBuf->gfx, pBuf->dst, dst.x, dst.y);
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
      if (pDlg->pScroll->pScrollBar) {
      pDlg->pScroll->pScrollBar->size.h = scrollbar_size(pDlg->pScroll);
    }
    }
    
  } else { /* no scrollbar */
    pBuf = pDlg->pBeginActiveWidgetList;
    
    /* undraw last widget */
    dst = pBuf->size;
    fix_rect(pBuf->dst, &dst);
    clear_surface(pBuf->dst, &dst);
    blit_entire_src(pBuf->gfx, pBuf->dst, dst.x, dst.y);
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
  
  if(pDlg->pScroll && pDlg->pScroll->pScrollBar && pDlg->pActiveWidgetList) {
    pDlg->pScroll->pScrollBar->size.y = get_position(pDlg);
  }
  
  return TRUE;
}

/**************************************************************************
  			Vertical ScrollBar
**************************************************************************/

/**************************************************************************
  ...
**************************************************************************/
static int std_up_advanced_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct ADVANCED_DLG *pDlg = pWidget->private_data.adv_dlg;
    struct widget *pBegin = up_scroll_widget_list(
                          pDlg->pScroll,
                          pDlg->pActiveWidgetList,
                          pDlg->pBeginActiveWidgetList,
                          pDlg->pEndActiveWidgetList);
  
    if (pBegin) {
      pDlg->pActiveWidgetList = pBegin;
    }
    
    unsellect_widget_action();
    pSellected_Widget = pWidget;
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_tibutton(pWidget);
    flush_rect(pWidget->size, FALSE);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int std_down_advanced_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct ADVANCED_DLG *pDlg = pWidget->private_data.adv_dlg;
    struct widget *pBegin = down_scroll_widget_list(
                          pDlg->pScroll,
                          pDlg->pActiveWidgetList,
                          pDlg->pBeginActiveWidgetList,
                          pDlg->pEndActiveWidgetList);
  
    if (pBegin) {
      pDlg->pActiveWidgetList = pBegin;
    }
  
    unsellect_widget_action();
    pSellected_Widget = pWidget;
    set_wstate(pWidget, FC_WS_SELLECTED);
    redraw_tibutton(pWidget);
    flush_rect(pWidget->size, FALSE);
  }
  return -1;
}

/**************************************************************************
  FIXME : fix main funct : vertic_scroll_widget_list(...)
**************************************************************************/
static int std_vscroll_advanced_dlg_callback(struct widget *pScrollBar)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct ADVANCED_DLG *pDlg = pScrollBar->private_data.adv_dlg;
    struct widget *pBegin = vertic_scroll_widget_list(
                          pDlg->pScroll,
                          pDlg->pActiveWidgetList,
                          pDlg->pBeginActiveWidgetList,
                          pDlg->pEndActiveWidgetList);
  
    if (pBegin) {
      pDlg->pActiveWidgetList = pBegin;
    }
    unsellect_widget_action();
    set_wstate(pScrollBar, FC_WS_SELLECTED);
    pSellected_Widget = pScrollBar;
    redraw_vert(pScrollBar);
    flush_rect(pScrollBar->size, FALSE);
  }
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
  struct widget *pBuf = NULL, *pWindow = NULL;
    
  assert(pDlg != NULL);
  
  pWindow = pDlg->pEndWidgetList;
  
  if(!pDlg->pScroll) {
    pDlg->pScroll = fc_calloc(1, sizeof(struct ScrollBar));
        
    pBuf = pDlg->pEndActiveWidgetList;
    while(pBuf && pBuf != pDlg->pBeginActiveWidgetList->prev) {
      pBuf = pBuf->prev;
      count++;
    }
  
    pDlg->pScroll->count = count;
  }
  
  pDlg->pScroll->active = active;
  pDlg->pScroll->step = step;
  
  if(create_buttons) {
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->private_data.adv_dlg = pDlg;
    pBuf->action = std_up_advanced_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    pDlg->pScroll->pUp_Left_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
    count = pBuf->size.w;
    
    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, pWindow->dst, NULL, 0);
    
    pBuf->ID = ID_BUTTON;
    pBuf->private_data.adv_dlg = pDlg;
    pBuf->action = std_down_advanced_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    pDlg->pScroll->pDown_Right_Button = pBuf;
    DownAdd(pBuf, pDlg->pBeginWidgetList);
    pDlg->pBeginWidgetList = pBuf;
    
  }
  
  if(create_scrollbar) {
    /* create vsrollbar */
    pBuf = create_vertical(pTheme->Vertic, pWindow->dst,
				10, WF_RESTORE_BACKGROUND);
    
    pBuf->ID = ID_SCROLLBAR;
    pBuf->private_data.adv_dlg = pDlg;
    pBuf->action = std_vscroll_advanced_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
  
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
int setup_vertical_widgets_position(int step,
	Sint16 start_x, Sint16 start_y, Uint16 w, Uint16 h,
				struct widget *pBegin, struct widget *pEnd)
{
  struct widget *pBuf = pEnd;
  register int count = 0;
  register int real_start_x = start_x;
  int ret = 0;
  
  while(pBuf)
  {
    pBuf->size.x = real_start_x;
    pBuf->size.y = start_y;
     
    if(w) {
      pBuf->size.w = w;
    }
    
    if(h) {
      pBuf->size.h = h;
    }
    
    if(!((count + 1) % step)) {
      real_start_x = start_x;
      start_y += pBuf->size.h;
      if (((get_wflags(pBuf) & WF_HIDDEN) != WF_HIDDEN))
      {
        ret += pBuf->size.h;
      }
    } else {
      real_start_x += pBuf->size.w;
    }
    
    if(pBuf == pBegin) {
      
      break;
    }
    count++;
    pBuf = pBuf->prev;
  }
  
  return ret;
}


/**************************************************************************
  ...
**************************************************************************/
void setup_vertical_scrollbar_default_callbacks(struct ScrollBar *pScroll)
{
  assert(pScroll != NULL);
  if(pScroll->pUp_Left_Button) {
    pScroll->pUp_Left_Button->action = std_up_advanced_dlg_callback;
  }
  if(pScroll->pDown_Right_Button) {
    pScroll->pDown_Right_Button->action = std_down_advanced_dlg_callback;
  }
  if(pScroll->pScrollBar) {
    pScroll->pScrollBar->action = std_vscroll_advanced_dlg_callback;
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
  struct widget *pBuf = NULL, *pWindow = NULL;
    
  assert(pDlg != NULL);
  
  pWindow = pDlg->pEndWidgetList;
  
  if(!pDlg->pScroll) {
    pDlg->pScroll = fc_calloc(1, sizeof(struct ScrollBar));
    
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
    pBuf->data.ptr = (void *)pDlg;
    set_wstate(pBuf, FC_WS_NORMAL);
    
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
    pBuf->data.ptr = (void *)pDlg;
    set_wstate(pBuf, FC_WS_NORMAL);
    
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
				width, WF_RESTORE_BACKGROUND);
    
    pBuf->ID = ID_SCROLLBAR;
    pBuf->data.ptr = (void *)pDlg;
    set_wstate(pBuf, FC_WS_NORMAL);
     
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
/* ======================= WINDOWs ==================== */
/* =================================================== */

/**************************************************************************
  Window Menager Mechanism.
  Idea is simple each window/dialog has own buffer layer which is draw
  on screen during flush operations.
  This consume lots of memory but is extremly effecive.

  Each widget has own "destination" parm == where ( on what buffer )
  will be draw.
**************************************************************************/

/**************************************************************************
  Buffer allocation function.
  This function is call by "create_window(...)" function and allocate 
  buffer layer for this function.

  Pointer for this buffer is put in buffer array on last position that 
  flush functions will draw this layer last.
**************************************************************************/
SDL_Surface *get_buffer_layer(int width, int height)
{
  SDL_Surface *pBuffer;

  pBuffer = create_surf_alpha(/*Main.screen->w*/width, /*Main.screen->h*/height, SDL_SWSURFACE);
  
  /* add to buffers array */
  if (Main.guis) {
    int i;
    /* find NULL element */
    for(i = 0; i < Main.guis_count; i++) {
      if(!Main.guis[i]) {
        Main.guis[i] = gui_layer_new(0, 0, pBuffer);
	return pBuffer;
      }
    }
    Main.guis_count++;
    Main.guis = fc_realloc(Main.guis, Main.guis_count * sizeof(struct gui_layer *));
    Main.guis[Main.guis_count - 1] = gui_layer_new(0, 0, pBuffer);
  } else {
    Main.guis = fc_calloc(1, sizeof(struct gui_layer *));
    Main.guis[0] = gui_layer_new(0, 0, pBuffer);
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
  
  for(i = 0; i < Main.guis_count - 1; i++) {
    if(Main.guis[i] && (Main.guis[i]->surface == pBuffer)) {
      gui_layer_destroy(&Main.guis[i]);
      Main.guis[i] = Main.guis[i + 1];
      Main.guis[i + 1] = NULL;
    } else {
      if(!Main.guis[i]) {
	Main.guis[i] = Main.guis[i + 1];
        Main.guis[i + 1] = NULL;
      }
    }
  }

  if (Main.guis[Main.guis_count - 1]) {
    gui_layer_destroy(&Main.guis[Main.guis_count - 1]);
  }
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
  SDL_Surface *pTmpLeft = 
    ResizeSurface(pTheme->FR_Left, pTheme->FR_Left->w, h, 1);
  SDL_Surface *pTmpRight = 
    ResizeSurface(pTheme->FR_Right, pTheme->FR_Right->w, h, 1);
  SDL_Surface *pTmpTop =
    ResizeSurface(pTheme->FR_Top, w, pTheme->FR_Top->h, 1);
  SDL_Surface *pTmpBottom =
    ResizeSurface(pTheme->FR_Bottom, w, pTheme->FR_Bottom->h, 1);
  
  SDL_Rect tmp,dst = {start_x, start_y, 0, 0};

  tmp = dst;
  alphablit(pTmpLeft, NULL, pDest, &tmp);
  
  dst.x += w - pTmpRight->w;
  tmp = dst;
  alphablit(pTmpRight, NULL, pDest, &tmp);

  dst.x = start_x;
  tmp = dst;
  alphablit(pTmpTop, NULL, pDest, &tmp);
  
  dst.y += h - pTmpBottom->h;
  tmp = dst;
  alphablit(pTmpBottom, NULL, pDest, &tmp);

  FREESURFACE(pTmpLeft);
  FREESURFACE(pTmpRight);
  FREESURFACE(pTmpTop);
  FREESURFACE(pTmpBottom);
}

/**************************************************************************
  ...
**************************************************************************/
void refresh_widget_background(struct widget *pWidget)
{
  SDL_Rect dest;
  
  if (pWidget) {
    dest = pWidget->size;
    fix_rect(pWidget->dst, &dest);    

    if (pWidget->gfx && pWidget->gfx->w == pWidget->size.w &&
      				pWidget->gfx->h == pWidget->size.h) {
      clear_surface(pWidget->gfx, NULL);
      alphablit(pWidget->dst, &dest, pWidget->gfx, NULL);
    } else {
      FREESURFACE(pWidget->gfx);
      pWidget->gfx = crop_rect_from_surface(pWidget->dst, &dest);
    }
  }
}
