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
                          chatline.c  -  description
                             -------------------
    begin                : Sun Jun 30 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "fcintl.h"

#include "gui_mem.h"

#include "packets.h"
#include "support.h"

#include "climisc.h"
#include "clinet.h"

#include "colors.h"
#include "graphics.h"
#include "unistring.h"
#include "gui_string.h"
#include "gui_id.h"
#include "gui_stuff.h"
#include "gui_zoom.h"
#include "gui_main.h"

#include "chatline.h"

#define PTSIZE_LOG_FONT 10

float maxpix;
float fStart_Vert;
float tmp = 0;

struct GUI_StrList {
  struct GUI_StrList *next;
  struct GUI_StrList *prev;
  SDL_String16 *string16;
  SDL_Surface *string_surf;
};

static struct _LOG {
  struct GUI_StrList *startlogdraw;
  struct GUI_StrList *mainlogdraw;
  struct GUI_StrList *endloglist;

  /* Widgets */
  struct GUI *window;
  struct GUI *up_button;
  struct GUI *down_button;
  struct GUI *vscroll;

  Uint16 step;			/* used by vscroll: step */
  Uint16 min;			/* used by vscroll: min position */
  Uint16 max;			/* used by vscroll: max position */
  Uint16 count;			/* size of string list */

} *pLog;

static void real_redraw_log_window(const struct GUI_StrList
				   *pCurrentString);
static void add_to_log_list(SDL_String16 * pString16);

static int up_chatline_callback(struct GUI *pWidget);
static int down_chatline_callback(struct GUI *pWidget);
static int chatline_vertical_callback(struct GUI *pWidget);
static int inputline_return_callback(struct GUI *pWidget);

static void scaling(Uint16 new_count);

static void set_new_logs_widged_start_pos(void);

/* =================================================== */
/* =================== Private ======================= */
/* =================================================== */

/**************************************************************************
  ...
**************************************************************************/
static int move_chatline_window_callback(struct GUI *pWidget)
{

  if (is_this_widget_first_on_list(pWidget)) {
    move_widget_to_front_of_gui_list(pWidget);
    move_ID_to_front_of_gui_list(ID_CHATLINE_UP_BUTTON);
    move_ID_to_front_of_gui_list(ID_CHATLINE_DOWN_BUTTON);
    move_ID_to_front_of_gui_list(ID_CHATLINE_VSCROLLBAR);
  }

  move_window(pWidget);

  set_new_logs_widged_start_pos();

  pLog->startlogdraw = pLog->mainlogdraw;

  /* draw background */
  blit_entire_src(pWidget->gfx, Main.screen, pWidget->size.x,
		  pWidget->size.y);

  real_redraw_log_window(pLog->startlogdraw);

  refresh_rect(pWidget->size);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int up_chatline_callback(struct GUI *pWidget)
{
  do {
    if (pLog->vscroll->size.y > pLog->min) {

      if ((pLog->vscroll->size.y - pLog->step) < pLog->min) {
	pLog->vscroll->size.y = pLog->min;
      } else {
	pLog->vscroll->size.y -= pLog->step;
      }

      /* edraw_vert_with_bg(pLog->vscroll); */

      if (pLog->startlogdraw->prev) {
	pLog->startlogdraw = pLog->startlogdraw->prev;
      }

      blit_entire_src(pLog->window->gfx, Main.screen,
		      pLog->window->size.x, pLog->window->size.y);
      real_redraw_log_window(pLog->startlogdraw);
      refresh_rect(pLog->window->size);
      /* DL_Delay(20); */
    }

    SDL_PollEvent(&Main.event);
  } while (Main.event.type != SDL_MOUSEBUTTONUP);

  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  redraw_tibutton(pWidget);
  refresh_rect(pWidget->size);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int down_chatline_callback(struct GUI *pWidget)
{
  do {
    if (pLog->vscroll->size.y < (pLog->max - pLog->vscroll->size.h)) {

      if (pLog->vscroll->size.y + pLog->step >
	  pLog->max - pLog->vscroll->size.h) {
	pLog->vscroll->size.y = pLog->max - pLog->vscroll->size.h;
      } else {
	pLog->vscroll->size.y += pLog->step;
      }


      if (pLog->startlogdraw != pLog->mainlogdraw) {
	pLog->startlogdraw = pLog->startlogdraw->next;
      }

      blit_entire_src(pLog->window->gfx, Main.screen,
		      pLog->window->size.x, pLog->window->size.y);

      real_redraw_log_window(pLog->startlogdraw);

      refresh_rect(pLog->window->size);

    }

    SDL_PollEvent(&Main.event);
  } while (Main.event.type != SDL_MOUSEBUTTONUP);

  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  redraw_tibutton(pWidget);
  refresh_rect(pWidget->size);

  return -1;
}

/**************************************************************************
  FIXME
**************************************************************************/
static int chatline_vertical_callback(struct GUI *pVScroll)
{
  bool ret = TRUE;

  while (ret) {
    SDL_WaitEvent(&Main.event);
    switch (Main.event.type) {
    case SDL_MOUSEBUTTONUP:
      ret = 0;
      break;
    case SDL_MOUSEMOTION:
      if (Main.event.motion.yrel
	  && pVScroll->size.y >= pLog->min
	  && pVScroll->size.y <= pLog->max
	  && Main.event.motion.y >= pLog->min
	  && Main.event.motion.y <= pLog->max) {

	if ((pVScroll->size.y +
	     Main.event.motion.yrel) > pLog->max - pVScroll->size.h) {
	  pVScroll->size.y = pLog->max - pVScroll->size.h;
	} else {
	  if ((pVScroll->size.y + Main.event.motion.yrel) < pLog->min) {
	    pVScroll->size.y = pLog->min;
	  } else {
	    pVScroll->size.y += Main.event.motion.yrel;
	  }
	}
#if 0
	redraw_vert_with_bg(pVScroll);
	refresh_rects();
#endif
      }
      break;
    }				/* switch */
  }				/* while */

  unsellect_widget_action();
  set_wstate(pVScroll, WS_SELLECTED);
  pSellected_Widget = pVScroll;
  redraw_vert(pVScroll);
  refresh_rect(pVScroll->size);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int inputline_return_callback(struct GUI *pWidget)
{
  struct packet_generic_message apacket;
  char *theinput = NULL;

  if (!pWidget->string16->text) {
    return -1;
  }

  theinput = convert_to_chars(pWidget->string16->text);

  if (*theinput) {
    mystrlcpy(apacket.message, theinput,
	      MAX_LEN_MSG - MAX_LEN_USERNAME + 1);
    send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);

    unicode_append_output_window(pWidget->string16->text);
    FREE(theinput);
  }

  pWidget->string16->text = NULL;

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static void add_to_log_list(SDL_String16 * pString16)
{
  pLog->endloglist->next = MALLOC(sizeof(struct GUI_StrList));
  pLog->endloglist->next->string16 = pString16;
  pLog->endloglist->next->string_surf =
      create_text_surf_from_str16(pString16);
  pLog->endloglist->next->prev = pLog->endloglist;
  pLog->endloglist = pLog->endloglist->next;

  if (pLog->endloglist->string_surf->h > 14) {
    pLog->count += pLog->endloglist->string_surf->h / 14;
  } else {
    pLog->count++;
  }

  if (pLog->count > fStart_Vert) {
    FREESURFACE(pLog->mainlogdraw->string_surf);
    pLog->startlogdraw = pLog->mainlogdraw = pLog->mainlogdraw->next;
    scaling(pLog->count);
  }
}

/**************************************************************************
  FIXME
**************************************************************************/
static void scaling(Uint16 new_count)
{
  float fStep, fStep10;
  struct GUI *pVert =
      get_widget_pointer_form_main_list(ID_CHATLINE_VSCROLLBAR);

  fStep = maxpix / (new_count - 1);
  fStep10 = maxpix / log10(new_count);
  if (pVert->size.h > 10) {
    if (tmp) {
      tmp -= fStep * 0.5;
      /*pVert->size.y +=(tmp - fStep); */
    } else {
      tmp = fStep;
      /*pVert->size.y +=(fStep); */
    }


    pVert->size.h -= (Uint16) tmp;
    pVert->size.y = pLog->window->size.y + pLog->window->size.h -
	(pLog->down_button->size.h + pVert->size.h) - 3;
    pLog->step = (pVert->size.y - pLog->min) / (new_count - fStart_Vert);
    tmp = fStep;
  }
#if 0
  redraw_vert_with_bg(pVert);
  refresh_rects();
#endif
}

/**************************************************************************
  ...
**************************************************************************/
static void real_redraw_log_window(const struct GUI_StrList
				   *pCurrentString)
{

  Uint16 sy;
  SDL_Surface *pText = NULL;

#if 0
  blit_entire_src(pLog->window->gfx, Main.screen,
		  pLog->window->size.x, pLog->window->size.y);
#endif

  redraw_window(pLog->window);

  /* redraw window widgets */
  redraw_tibutton(pLog->up_button);

  redraw_tibutton(pLog->down_button);

  redraw_vert(pLog->vscroll);

  /* end redraw window widgets */

  /* draw frame */
  pText = ResizeSurface(pTheme->FR_Vert, pTheme->FR_Vert->w,
			pLog->window->size.h - 4, 2);
  blit_entire_src(pText, Main.screen, pLog->window->size.x +
		  pLog->window->size.w - pLog->up_button->size.w - 6,
		  pLog->window->size.y + 2);
  FREESURFACE(pText);

  /* draw log text */
  sy = WINDOW_TILE_HIGH;
  while (pCurrentString) {
    if (sy > pLog->window->size.h) {
      break;
    }
    if (pCurrentString->string_surf) {
      blit_entire_src(pCurrentString->string_surf,
		      Main.screen,
		      pLog->window->size.x + 5, pLog->window->size.y + sy);
      sy += (pCurrentString->string_surf->h + 1);
    } else {
      pText = create_text_surf_from_str16(pCurrentString->string16);
      blit_entire_src(pText, Main.screen,
		      pLog->window->size.x + 5, pLog->window->size.y + sy);
      sy += (pText->h + 1);
      FREESURFACE(pText);
    }

    pCurrentString = pCurrentString->next;
  }
}

/**************************************************************************
  ...
**************************************************************************/
static int togle_log_window(struct GUI *pTogle_Button)
{
  if (get_wflags(pLog->window) & WF_HIDDEN) {

    clear_wflag(pLog->window, WF_HIDDEN);
    clear_wflag(pLog->up_button, WF_HIDDEN);
    clear_wflag(pLog->down_button, WF_HIDDEN);
    clear_wflag(pLog->vscroll, WF_HIDDEN);

    refresh_widget_background(pLog->window);

    /* redraw window */
    real_redraw_log_window(pLog->mainlogdraw);

  } else {
    set_wflag(pLog->window, WF_HIDDEN);
    set_wflag(pLog->up_button, WF_HIDDEN);
    set_wflag(pLog->down_button, WF_HIDDEN);
    set_wflag(pLog->vscroll, WF_HIDDEN);

    /* draw background */
    blit_entire_src(pLog->window->gfx, Main.screen,
		    pLog->window->size.x, pLog->window->size.y);

  }

  refresh_rect(pLog->window->size);

  pSellected_Widget = pTogle_Button;
  set_wstate(pTogle_Button, WS_SELLECTED);
  redraw_icon(pTogle_Button);
  refresh_rect(pTogle_Button->size);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static void set_new_logs_widged_start_pos(void)
{
  /* set up button */
  pLog->up_button->size.x = pLog->window->size.x + pLog->window->size.w
      - pLog->up_button->size.w - 3;
  pLog->up_button->size.y = pLog->window->size.y + 3;

  pLog->min = pLog->window->size.y + pLog->up_button->size.h + 3;

  /* set down button */
  pLog->down_button->size.x = pLog->window->size.x + pLog->window->size.w
      - pLog->down_button->size.w - 3;
  pLog->down_button->size.y = pLog->max = pLog->window->size.y +
      pLog->window->size.h - pLog->down_button->size.h - 3;

  /* set vsrollbar */
  pLog->vscroll->size.x = pLog->window->size.x + pLog->window->size.w
      - pLog->vscroll->size.w - 1;

  pLog->vscroll->size.y = pLog->max - pLog->vscroll->size.h;
}

/* ======================================================= */
/* ====================== Public ========================= */
/* ======================================================= */

/**************************************************************************
  Redraw Log Window witch parametr 'bcgd'.
    bcgd = 1:  draw window background first.
    bcgd = 2:  free old background and alloc new from area under window.
    bcgd = *:  only redraw log window.
**************************************************************************/
void Redraw_Log_Window(int bcgd)
{

  if (!(get_wflags(pLog->window) & WF_HIDDEN)) {

    switch (bcgd) {
    case 1:
      /* draw background */
      blit_entire_src(pLog->window->gfx, Main.screen,
		      pLog->window->size.x, pLog->window->size.y);
      break;
    case 2:
      refresh_widget_background(pLog->window);
      break;
    default:
      break;
    }

    /* redraw window */
    real_redraw_log_window(pLog->mainlogdraw);
  }
}

/**************************************************************************
  ...
**************************************************************************/
void Resize_Log_Window(Uint16 w, Uint16 h)
{
  resize_window(pLog->window, NULL, NULL, w, h);
}

/**************************************************************************
  ...
**************************************************************************/
void Init_Log_Window(Sint16 start_x, Sint16 start_y, Uint16 w, Uint16 h)
{

  pLog = MALLOC(sizeof(struct _LOG));
  pLog->endloglist = MALLOC(sizeof(struct GUI_StrList));
  pLog->endloglist->string16 =
      create_str16_from_char(_("SDL_Client: Welcome..."), PTSIZE_LOG_FONT);
  pLog->endloglist->string_surf =
      create_text_surf_from_str16(pLog->endloglist->string16);
  pLog->mainlogdraw = pLog->startlogdraw = pLog->endloglist;

  pLog->count = 1;

  /* create window */
  pLog->window = create_window(create_str16_from_char(_("Log"), 12),
			       w, h, WF_DRAW_THEME_TRANSPARENT);

  pLog->window->string16->style = TTF_STYLE_BOLD;
  pLog->window->size.x = start_x;
  pLog->window->size.y = start_y;
  resize_window(pLog->window, NULL, NULL, w, h);

  pLog->window->action = move_chatline_window_callback;
  set_wstate(pLog->window, WS_NORMAL);

  /* create up button */
  pLog->up_button = create_themeicon_button(pTheme->UP_Icon, NULL, 0);
  pLog->up_button->size.x = start_x + w - pLog->up_button->size.w - 3;
  pLog->up_button->size.y = start_y + 3;
  pLog->up_button->action = up_chatline_callback;
  set_wstate(pLog->up_button, WS_NORMAL);
  pLog->min = start_y + pLog->up_button->size.h + 3;

  /* create down button */
  pLog->down_button = create_themeicon_button(pTheme->DOWN_Icon, NULL, 0);
  pLog->down_button->size.x = start_x + w - pLog->down_button->size.w - 3;
  pLog->down_button->size.y = pLog->max = start_y + h
      - pLog->down_button->size.h - 3;
  pLog->down_button->action = down_chatline_callback;
  set_wstate(pLog->down_button, WS_NORMAL);

  /* create vsrollbar */
  pLog->vscroll =
      create_vertical(pTheme->Vertic, pLog->max - pLog->min, 0);
  pLog->vscroll->size.x = start_x + w - pLog->vscroll->size.w - 1;
  pLog->vscroll->size.y = pLog->min;

  pLog->vscroll->action = chatline_vertical_callback;
  /*pLog->vscroll->state = WS_NORMAL; */

  maxpix = pLog->max - pLog->min;
  fStart_Vert = (float) (pLog->window->size.h - WINDOW_TILE_HIGH) /
      (pLog->endloglist->string_surf->h + 1);

  add_to_gui_list(ID_CHATLINE_WINDOW, pLog->window);
  add_to_gui_list(ID_CHATLINE_UP_BUTTON, pLog->up_button);
  add_to_gui_list(ID_CHATLINE_DOWN_BUTTON, pLog->down_button);
  add_to_gui_list(ID_CHATLINE_VSCROLLBAR, pLog->vscroll);

  add_to_gui_list(ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON,
		  create_themeicon(pTheme->LOG_Icon, 0));

  set_action(ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON, togle_log_window);
  set_key(ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON, SDLK_BACKQUOTE);
}

/**************************************************************************
  ...
**************************************************************************/
void Init_Input_Edit(Sint16 start_x, Sint16 start_y, Uint16 w, Uint16 h)
{

  struct GUI *pInput_Edit = create_edit_from_unichars(NULL, NULL, 18,
						      w,
						      WF_DRAW_THEME_TRANSPARENT);
  pInput_Edit->size.x = start_x;
  pInput_Edit->size.y = start_y;

  if (h > pInput_Edit->size.h) {
    pInput_Edit->size.h = h;
  }

  pInput_Edit->action = inputline_return_callback;

  add_to_gui_list(ID_CHATLINE_INPUT_EDIT, pInput_Edit);
}

 /* ======================================================= */

/**************************************************************************
  add to list with refresh screen
**************************************************************************/
void unicode_append_output_window(Uint16 * pUnistring)
{
  add_to_log_list(create_string16(pUnistring, PTSIZE_LOG_FONT));

  /* draw background */
  blit_entire_src(pLog->window->gfx, Main.screen,
		  pLog->window->size.x, pLog->window->size.y);
  /* redraw window */
  real_redraw_log_window(pLog->mainlogdraw);
  refresh_rect(pLog->window->size);
}

/**************************************************************************
  add to list without refresh screen
**************************************************************************/
void set_output_window_unitext(Uint16 * pUnistring)
{
  return add_to_log_list(create_string16(pUnistring, PTSIZE_LOG_FONT));
}

/**************************************************************************
  Appends the string to the chat output window.  The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void real_append_output_window(const char *astring)
{
  Uint16 *buf = convert_to_utf16(astring);

  if (get_wflags(pLog->window) & WF_HIDDEN) {
    set_output_window_unitext(buf);
  } else {
    unicode_append_output_window(buf);
  }
}

/**************************************************************************
  Get the text of the output window, and call write_chatline_content() to
  log it.
**************************************************************************/
void log_output_window(void)
{
  /* TODO */
}

/**************************************************************************
  Clear all text from the output window.
**************************************************************************/
void clear_output_window(void)
{
  /* TODO */
}
