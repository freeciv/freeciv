/***********************************************************************
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

/***********************************************************************
                          widget.c  -  description
                             -------------------
    begin                : June 30 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* utility */
#include "log.h"

/* gui-sdl2 */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"
#include "unistring.h"

#include "widget.h"
#include "widget_p.h"

struct widget *selected_widget;
SDL_Rect *pInfo_Area = NULL;

extern Uint32 widget_info_counter;

/* ================================ Private ============================ */

static struct widget *begin_main_widget_list;

static SDL_Surface *info_label = NULL;

/**********************************************************************//**
  Correct backgroud size ( set min size ). Used in create widget
  functions.
**************************************************************************/
void correct_size_bcgnd_surf(SDL_Surface *ptheme,
                             int *width, int *height)
{
  *width = MAX(*width, 2 * (ptheme->w / adj_size(16)));
  *height = MAX(*height, 2 * (ptheme->h / adj_size(16)));
}

/**********************************************************************//**
  Create background image for buttons, iconbuttons and edit fields
  then return  pointer to this image.

  Graphic is taken from ptheme surface and blit to new created image.

  Length and height depend of iText_with, iText_high parameters.

  Type of image depend of "state" parameter.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled
**************************************************************************/
SDL_Surface *create_bcgnd_surf(SDL_Surface *ptheme, Uint8 state,
                               Uint16 width, Uint16 height)
{
  bool zoom;
  int iTile_width_len_end, iTile_width_len_mid, iTile_count_len_mid;
  int iTile_width_height_end, iTile_width_height_mid, iTile_count_height_mid;
  int i, j;
  SDL_Rect src, des;
  SDL_Surface *background = NULL;
  int iStart_y = (ptheme->h / 4) * state;

  iTile_width_len_end = ptheme->w / 16;
  iTile_width_len_mid = ptheme->w - (iTile_width_len_end * 2);

  iTile_count_len_mid =
      (width - (iTile_width_len_end * 2)) / iTile_width_len_mid;

  /* corrections I */
  if (((iTile_count_len_mid *
        iTile_width_len_mid) + (iTile_width_len_end * 2)) < width) {
    iTile_count_len_mid++;
  }

  iTile_width_height_end = ptheme->h / 16;
  iTile_width_height_mid = (ptheme->h / 4) - (iTile_width_height_end * 2);
  iTile_count_height_mid =
      (height - (iTile_width_height_end * 2)) / iTile_width_height_mid;

  /* corrections II */
  if (((iTile_count_height_mid *
	iTile_width_height_mid) + (iTile_width_height_end * 2)) < height) {
    iTile_count_height_mid++;
  }

  i = MAX(iTile_width_len_end * 2, width);
  j = MAX(iTile_width_height_end * 2, height);
  zoom = ((i != width) ||  (j != height));

  /* now allocate memory */
  background = create_surf(i, j, SDL_SWSURFACE);

  /* copy left end */

  /* left top */
  src.x = 0;
  src.y = iStart_y;
  src.w = iTile_width_len_end;
  src.h = iTile_width_height_end;

  des.x = 0;
  des.y = 0;
  alphablit(ptheme, &src, background, &des, 255);

  /* left middle */
  src.y = iStart_y + iTile_width_height_end;
  src.h = iTile_width_height_mid;
  for (i = 0; i < iTile_count_height_mid; i++) {
    des.y = iTile_width_height_end + i * iTile_width_height_mid;
    alphablit(ptheme, &src, background, &des, 255);
  }

  /* left bottom */
  src.y = iStart_y + ((ptheme->h / 4) - iTile_width_height_end);
  src.h = iTile_width_height_end;
  des.y = background->h - iTile_width_height_end;
  clear_surface(background, &des);
  alphablit(ptheme, &src, background, &des, 255);

  /* copy middle parts */

  src.x = iTile_width_len_end;
  src.y = iStart_y;
  src.w = iTile_width_len_mid;

  for (i = 0; i < iTile_count_len_mid; i++) {

    /* middle top */
    des.x = iTile_width_len_end + i * iTile_width_len_mid;
    des.y = 0;
    src.y = iStart_y;
    alphablit(ptheme, &src, background, &des, 255);

    /* middle middle */
    src.y = iStart_y + iTile_width_height_end;
    src.h = iTile_width_height_mid;
    for (j = 0; j < iTile_count_height_mid; j++) {
      des.y = iTile_width_height_end + j * iTile_width_height_mid;
      alphablit(ptheme, &src, background, &des, 255);
    }

    /* middle bottom */
    src.y = iStart_y + ((ptheme->h / 4) - iTile_width_height_end);
    src.h = iTile_width_height_end;
    des.y = background->h - iTile_width_height_end;
    clear_surface(background, &des);
    alphablit(ptheme, &src, background, &des, 255);
  }

  /* copy right end */

  /* right top */
  src.x = ptheme->w - iTile_width_len_end;
  src.y = iStart_y;
  src.w = iTile_width_len_end;

  des.x = background->w - iTile_width_len_end;
  des.y = 0;

  alphablit(ptheme, &src, background, &des, 255);

  /* right middle */
  src.y = iStart_y + iTile_width_height_end;
  src.h = iTile_width_height_mid;
  for (i = 0; i < iTile_count_height_mid; i++) {
    des.y = iTile_width_height_end + i * iTile_width_height_mid;
    alphablit(ptheme, &src, background, &des, 255);
  }

  /* right bottom */
  src.y = iStart_y + ((ptheme->h / 4) - iTile_width_height_end);
  src.h = iTile_width_height_end;
  des.y = background->h - iTile_width_height_end;
  clear_surface(background, &des);
  alphablit(ptheme, &src, background, &des, 255);

  if (zoom) {
    SDL_Surface *zoomed = ResizeSurface(background, width, height, 1);

    FREESURFACE(background);
    background = zoomed;
  }

  return background;
}

/* =================================================== */
/* ===================== WIDGET LIST ==================== */
/* =================================================== */

/**********************************************************************//**
  Find the next visible widget in the widget list starting with
  start_widget that is drawn at position (x, y). If start_widget is NULL,
  the search starts with the first entry of the main widget list. 
**************************************************************************/
struct widget *find_next_widget_at_pos(struct widget *start_widget,
                                       int x, int y)
{
  SDL_Rect area = {0, 0, 0, 0};
  struct widget *pwidget;

  pwidget = start_widget ? start_widget : begin_main_widget_list;

  while (pwidget) {
    area.x = pwidget->dst->dest_rect.x + pwidget->size.x;
    area.y = pwidget->dst->dest_rect.y + pwidget->size.y;
    area.w = pwidget->size.w;
    area.h = pwidget->size.h;
    if (is_in_rect_area(x, y, area)
        && !((get_wflags(pwidget) & WF_HIDDEN) == WF_HIDDEN)) {
      return (struct widget *) pwidget;
    }
    pwidget = pwidget->next;
  }

  return NULL;
}

/**********************************************************************//**
  Find the next enabled and visible widget in the widget list starting
  with start_widget that handles the given key. If start_widget is NULL,
  the search starts with the first entry of the main widget list.
  NOTE: This function ignores CapsLock and NumLock Keys.
**************************************************************************/
struct widget *find_next_widget_for_key(struct widget *start_widget,
                                        SDL_Keysym key)
{
  struct widget *pwidget;

  pwidget = start_widget ? start_widget : begin_main_widget_list;

  key.mod &= ~(KMOD_NUM | KMOD_CAPS);
  while (pwidget) {
    if ((pwidget->key == key.sym
         || (pwidget->key == SDLK_RETURN && key.sym == SDLK_KP_ENTER)
         || (pwidget->key == SDLK_KP_ENTER && key.sym == SDLK_RETURN))
        && ((pwidget->mod & key.mod) || (pwidget->mod == key.mod))) {
      if (!((get_wstate(pwidget) == FC_WS_DISABLED)
            || ((get_wflags(pwidget) & WF_HIDDEN) == WF_HIDDEN))) {
	return (struct widget *) pwidget;
      }
    }
    pwidget = pwidget->next;
  }

  return NULL;
}

/**********************************************************************//**
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
    if ( pwidget->action )
      if ( pwidget->action(pwidget)  ) ID = 0;
    if widget callback function return = 0 then return NOZERO
    I default return (-1) from Widget callback functions.
**************************************************************************/
Uint16 widget_pressed_action(struct widget *pwidget)
{
  Uint16 ID = 0;

  if (!pwidget) {
    return 0;
  }

  widget_info_counter = 0;
  if (pInfo_Area) {
    dirty_sdl_rect(pInfo_Area);
    FC_FREE(pInfo_Area);
    FREESURFACE(info_label);
  }

  switch (get_wtype(pwidget)) {
    case WT_TI_BUTTON:
    case WT_I_BUTTON:
    case WT_ICON:
    case WT_ICON2:
      if (PRESSED_EVENT(main_data.event)) {
        set_wstate(pwidget, FC_WS_PRESSED);
        widget_redraw(pwidget);
        widget_mark_dirty(pwidget);
        flush_dirty();
        set_wstate(pwidget, FC_WS_SELECTED);
        SDL_Delay(300);
      }
      ID = pwidget->ID;
      if (pwidget->action) {
        if (pwidget->action(pwidget)) {
          ID = 0;
        }
      }
      break;

    case WT_EDIT:
    {
      if (PRESSED_EVENT(main_data.event)) {
        bool ret, loop = (get_wflags(pwidget) & WF_EDIT_LOOP);
        enum edit_return_codes change;

        do {
          ret = FALSE;
          change = edit_field(pwidget);
          if (change != ED_FORCE_EXIT && (!loop || change != ED_RETURN)) {
            widget_redraw(pwidget);
            widget_mark_dirty(pwidget);
            flush_dirty();
          }
          if (change != ED_FORCE_EXIT && change != ED_ESC && pwidget->action) {
            if (pwidget->action(pwidget)) {
              ID = 0;
            }
          }
          if (loop && change == ED_RETURN) {
            ret = TRUE;
          }
        } while (ret);
        ID = 0;
      }
      break;
    }
    case WT_VSCROLLBAR:
    case WT_HSCROLLBAR:
      if (PRESSED_EVENT(main_data.event)) {
        set_wstate(pwidget, FC_WS_PRESSED);
        widget_redraw(pwidget);
        widget_mark_dirty(pwidget);
        flush_dirty();
      }
      ID = pwidget->ID;
      if (pwidget->action) {
        if (pwidget->action(pwidget)) {
          ID = 0;
        }
      }
      break;
    case WT_CHECKBOX:
    case WT_TCHECKBOX:
      if (PRESSED_EVENT(main_data.event)) {
        set_wstate(pwidget, FC_WS_PRESSED);
        widget_redraw(pwidget);
        widget_mark_dirty(pwidget);
        flush_dirty();
        set_wstate(pwidget, FC_WS_SELECTED);
        toggle_checkbox(pwidget);
        SDL_Delay(300);
      }
      ID = pwidget->ID;
      if (pwidget->action) {
        if (pwidget->action(pwidget)) {
          ID = 0;
        }
      }
      break;
    case WT_COMBO:
      if (PRESSED_EVENT(main_data.event)) {
        set_wstate(pwidget, FC_WS_PRESSED);
        combo_popup(pwidget);
      } else {
        combo_popdown(pwidget);
      }
      break;
    default:
      ID = pwidget->ID;
      if (pwidget->action) {
        if (pwidget->action(pwidget) != 0) {
          ID = 0;
        }
      }
      break;
  }

  return ID;
}

/**********************************************************************//**
  Unselect (selected) widget and redraw this widget;
**************************************************************************/
void unselect_widget_action(void)
{
  if (selected_widget && (get_wstate(selected_widget) != FC_WS_DISABLED)) {
    set_wstate(selected_widget, FC_WS_NORMAL);

    if (!(get_wflags(selected_widget) & WF_HIDDEN)) {
      selected_widget->unselect(selected_widget);

      /* turn off quick info timer/counter */ 
      widget_info_counter = 0;
    }
  }

  if (pInfo_Area) {
    flush_rect(pInfo_Area, FALSE);
    FC_FREE(pInfo_Area);
    FREESURFACE(info_label);    
  }

  selected_widget = NULL;
}

/**********************************************************************//**
  Select widget.  Redraw this widget;
**************************************************************************/
void widget_selected_action(struct widget *pwidget)
{
  if (!pwidget || pwidget == selected_widget) {
    return;
  }

  if (selected_widget) {
    unselect_widget_action();
  }

  set_wstate(pwidget, FC_WS_SELECTED);

  pwidget->select(pwidget);

  selected_widget = pwidget;

  if (get_wflags(pwidget) & WF_WIDGET_HAS_INFO_LABEL) {
    widget_info_counter = 1;
  }
}

/**********************************************************************//**
  Draw or redraw info label to screen.
**************************************************************************/
void redraw_widget_info_label(SDL_Rect *rect)
{
  SDL_Surface *text;
  SDL_Rect srcrect, dstrect;
  SDL_Color color;
  struct widget *pwidget = selected_widget;

  if (!pwidget || !pwidget->info_label) {
    return;
  }

  if (!info_label) {
    pInfo_Area = fc_calloc(1, sizeof(SDL_Rect));

    color = pwidget->info_label->fgcol;
    pwidget->info_label->style |= TTF_STYLE_BOLD;
    pwidget->info_label->fgcol = *get_theme_color(COLOR_THEME_QUICK_INFO_TEXT);

    /* create string and bcgd theme */
    text = create_text_surf_from_utf8(pwidget->info_label);

    pwidget->info_label->fgcol = color;

    info_label = create_filled_surface(text->w + adj_size(10), text->h + adj_size(6),
                                       SDL_SWSURFACE, get_theme_color(COLOR_THEME_QUICK_INFO_BG));

    /* calculate start position */
    if ((pwidget->dst->dest_rect.y + pwidget->size.y) - info_label->h - adj_size(6) < 0) {
      pInfo_Area->y = (pwidget->dst->dest_rect.y + pwidget->size.y) + pwidget->size.h + adj_size(3);
    } else {
      pInfo_Area->y = (pwidget->dst->dest_rect.y + pwidget->size.y) - info_label->h - adj_size(5);
    }

    if ((pwidget->dst->dest_rect.x + pwidget->size.x) + info_label->w + adj_size(5) > main_window_width()) {
      pInfo_Area->x = (pwidget->dst->dest_rect.x + pwidget->size.x) - info_label->w - adj_size(5);
    } else {
      pInfo_Area->x = (pwidget->dst->dest_rect.x + pwidget->size.x) + adj_size(3);
    }

    pInfo_Area->w = info_label->w + adj_size(2);
    pInfo_Area->h = info_label->h + adj_size(3);

    /* draw text */
    dstrect.x = adj_size(6);
    dstrect.y = adj_size(3);

    alphablit(text, NULL, info_label, &dstrect, 255);

    FREESURFACE(text);

    /* draw frame */
    create_frame(info_label,
                 0, 0, info_label->w - 1, info_label->h - 1,
                 get_theme_color(COLOR_THEME_QUICK_INFO_FRAME));

  }

  if (rect) {
    dstrect.x = MAX(rect->x, pInfo_Area->x);
    dstrect.y = MAX(rect->y, pInfo_Area->y);

    srcrect.x = dstrect.x - pInfo_Area->x;
    srcrect.y = dstrect.y - pInfo_Area->y;
    srcrect.w = MIN((pInfo_Area->x + pInfo_Area->w), (rect->x + rect->w)) - dstrect.x;
    srcrect.h = MIN((pInfo_Area->y + pInfo_Area->h), (rect->y + rect->h)) - dstrect.y;

    screen_blit(info_label, &srcrect, &dstrect, 255);
  } else {
    screen_blit(info_label, NULL, pInfo_Area, 255);
  }

  if (correct_rect_region(pInfo_Area)) {
    update_main_screen();
#if 0
    SDL_UpdateRect(main_data.screen, pInfo_Area->x, pInfo_Area->y,
                   pInfo_Area->w, pInfo_Area->h);
#endif /* 0 */
  }
}

/**********************************************************************//**
  Find ID in Widget's List ('pGUI_List') and return pointer to this
  Widget.
**************************************************************************/
struct widget *get_widget_pointer_form_ID(const struct widget *pGUI_List,
                                          Uint16 ID, enum scan_direction direction)
{
  while (pGUI_List) {
    if (pGUI_List->ID == ID) {
      return (struct widget *) pGUI_List;
    }
    if (direction == SCAN_FORWARD) {
      pGUI_List = pGUI_List->next;
    } else {
      pGUI_List = pGUI_List->prev;
    }
  }

  return NULL;
}

/**********************************************************************//**
  Find ID in MAIN Widget's List ( begin_widget_list ) and return pointer to
  this Widgets.
**************************************************************************/
struct widget *get_widget_pointer_form_main_list(Uint16 ID)
{
  return get_widget_pointer_form_ID(begin_main_widget_list, ID, SCAN_FORWARD);
}

/**********************************************************************//**
  Add Widget to Main Widget's List ( begin_widget_list )
**************************************************************************/
void add_to_gui_list(Uint16 ID, struct widget *pGUI)
{
  if (begin_main_widget_list != NULL) {
    pGUI->next = begin_main_widget_list;
    pGUI->ID = ID;
    begin_main_widget_list->prev = pGUI;
    begin_main_widget_list = pGUI;
  } else {
    begin_main_widget_list = pGUI;
    begin_main_widget_list->ID = ID;
  }
}

/**********************************************************************//**
  Add Widget to Widget's List at add_dock position on 'prev' slot.
**************************************************************************/
void widget_add_as_prev(struct widget *new_widget, struct widget *add_dock)
{
  new_widget->next = add_dock;
  new_widget->prev = add_dock->prev;
  if (add_dock->prev) {
    add_dock->prev->next = new_widget;
  }
  add_dock->prev = new_widget;
  if (add_dock == begin_main_widget_list) {
    begin_main_widget_list = new_widget;
  }
}

/**********************************************************************//**
  Delete Widget from Main Widget's List ( begin_widget_list )

  NOTE: This function does not destroy Widget, only remove his pointer from
  list. To destroy this Widget totaly ( free mem... ) call macro:
  del_widget_from_gui_list( pwidget ).  This macro call this function.
**************************************************************************/
void del_widget_pointer_from_gui_list(struct widget *pGUI)
{
  if (!pGUI) {
    return;
  }

  if (pGUI == begin_main_widget_list) {
    begin_main_widget_list = begin_main_widget_list->next;
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

  if (selected_widget == pGUI) {
    selected_widget = NULL;
  }
}

/**********************************************************************//**
  Determinate if 'pGui' is first on WidgetList

  NOTE: This is used by My (move) GUI Window mechanism.  Return TRUE if is
  first.
**************************************************************************/
bool is_this_widget_first_on_list(struct widget *pGUI)
{
  return (begin_main_widget_list == pGUI);
}

/**********************************************************************//**
  Move pointer to Widget to list begin.

  NOTE: This is used by My GUI Window mechanism.
**************************************************************************/
void move_widget_to_front_of_gui_list(struct widget *pGUI)
{
  if (!pGUI || pGUI == begin_main_widget_list) {
    return;
  }

  /* pGUI->prev always exists because
     we don't do this to begin_main_widget_list */
  if (pGUI->next) {
    pGUI->prev->next = pGUI->next;
    pGUI->next->prev = pGUI->prev;
  } else {
    pGUI->prev->next = NULL;
  }

  pGUI->next = begin_main_widget_list;
  begin_main_widget_list->prev = pGUI;
  begin_main_widget_list = pGUI;
  pGUI->prev = NULL;
}

/**********************************************************************//**
  Delete Main Widget's List.
**************************************************************************/
void del_main_list(void)
{
  del_gui_list(begin_main_widget_list);
}

/**********************************************************************//**
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

/**********************************************************************//**
  Universal redraw Group of Widget function.  Function is optimized to
  WindowGroup: start draw from 'end' and stop on 'begin', in theory
  'end' is window widget;
**************************************************************************/
Uint16 redraw_group(const struct widget *begin_group_widget_list,
                    const struct widget *end_group_widget_list,
                    int add_to_update)
{
  Uint16 count = 0;
  struct widget *tmp_widget = (struct widget *) end_group_widget_list;

  while (tmp_widget) {

    if (!(get_wflags(tmp_widget) & WF_HIDDEN)) {

      widget_redraw(tmp_widget);

      if (add_to_update) {
        widget_mark_dirty(tmp_widget);
      }

      count++;
    }

    if (tmp_widget == begin_group_widget_list) {
      break;
    }

    tmp_widget = tmp_widget->prev;

  }

  return count;
}

/**********************************************************************//**
  Undraw all widgets in the group.
**************************************************************************/
void undraw_group(struct widget *begin_group_widget_list,
                  struct widget *end_group_widget_list)
{
  struct widget *tmp_widget = end_group_widget_list;

  while (tmp_widget) {
    widget_undraw(tmp_widget);
    widget_mark_dirty(tmp_widget);

    if (tmp_widget == begin_group_widget_list) {
      break;
    }

    tmp_widget = tmp_widget->prev;
  }
}

/**********************************************************************//**
  Move all widgets in the group by the given amounts.
**************************************************************************/
void set_new_group_start_pos(const struct widget *begin_group_widget_list,
                             const struct widget *end_group_widget_list,
                             Sint16 Xrel, Sint16 Yrel)
{
  struct widget *tmp_widget = (struct widget *) end_group_widget_list;

  while (tmp_widget) {

    widget_set_position(tmp_widget, tmp_widget->size.x + Xrel,
                        tmp_widget->size.y + Yrel);

    if (get_wtype(tmp_widget) == WT_VSCROLLBAR
        && tmp_widget->private_data.adv_dlg
        && tmp_widget->private_data.adv_dlg->scroll) {
      tmp_widget->private_data.adv_dlg->scroll->max += Yrel;
      tmp_widget->private_data.adv_dlg->scroll->min += Yrel;
    }

    if (get_wtype(tmp_widget) == WT_HSCROLLBAR
        && tmp_widget->private_data.adv_dlg
        && tmp_widget->private_data.adv_dlg->scroll) {
      tmp_widget->private_data.adv_dlg->scroll->max += Xrel;
      tmp_widget->private_data.adv_dlg->scroll->min += Xrel;
    }

    if (tmp_widget == begin_group_widget_list) {
      break;
    }

    tmp_widget = tmp_widget->prev;
  }
}

/**********************************************************************//**
  Move group of Widget's pointers to begin of main list.
  Move group destination buffer to end of buffer array.
  NOTE: This is used by My GUI Window(group) mechanism.
**************************************************************************/
void move_group_to_front_of_gui_list(struct widget *begin_group_widget_list,
                                     struct widget *end_group_widget_list)
{
  struct widget *tmp_widget = end_group_widget_list , *pPrev = NULL;
  struct gui_layer *gui_layer = get_gui_layer(end_group_widget_list->dst->surface);

  /* Widget Pointer Management */
  while (tmp_widget) {

    pPrev = tmp_widget->prev;

    /* tmp_widget->prev always exists because we
       don't do this to begin_main_widget_list */
    if (tmp_widget->next) {
      tmp_widget->prev->next = tmp_widget->next;
      tmp_widget->next->prev = tmp_widget->prev;
    } else {
      tmp_widget->prev->next = NULL;
    }

    tmp_widget->next = begin_main_widget_list;
    begin_main_widget_list->prev = tmp_widget;
    begin_main_widget_list = tmp_widget;
    begin_main_widget_list->prev = NULL;

    if (tmp_widget == begin_group_widget_list) {
      break;
    }

    tmp_widget = pPrev;
  }

  /* Window Buffer Management */
  if (gui_layer) {
    int i = 0;

    while ((i < main_data.guis_count - 1) && main_data.guis[i]) {
      if (main_data.guis[i] && main_data.guis[i + 1]
          && (main_data.guis[i] == gui_layer)) {
        main_data.guis[i] = main_data.guis[i + 1];
        main_data.guis[i + 1] = gui_layer;
      }
      i++;
    }
  }
}

/**********************************************************************//**
  Remove all widgets of the group from the list of displayed widgets.
  Does not free widget memory.
**************************************************************************/
void del_group_of_widgets_from_gui_list(struct widget *begin_group_widget_list,
                                        struct widget *end_group_widget_list)
{
  struct widget *bufWidget = NULL;
  struct widget *tmp_widget = end_group_widget_list;

  if (!end_group_widget_list) {
    return;
  }

  if (begin_group_widget_list == end_group_widget_list) {
    del_widget_from_gui_list(tmp_widget);
    return;
  }

  tmp_widget = tmp_widget->prev;

  while (tmp_widget) {

    bufWidget = tmp_widget->next;
    del_widget_from_gui_list(bufWidget);

    if (tmp_widget == begin_group_widget_list) {
      del_widget_from_gui_list(tmp_widget);
      break;
    }

    tmp_widget = tmp_widget->prev;
  }

}

/**********************************************************************//**
  Set state for all the widgets in the group.
**************************************************************************/
void set_group_state(struct widget *begin_group_widget_list,
                     struct widget *end_group_widget_list, enum widget_state state)
{
  struct widget *tmp_widget = end_group_widget_list;

  while (tmp_widget) {
    set_wstate(tmp_widget, state);
    if (tmp_widget == begin_group_widget_list) {
      break;
    }
    tmp_widget = tmp_widget->prev;
  }
}

/**********************************************************************//**
  Hide all widgets in the group.
**************************************************************************/
void hide_group(struct widget *begin_group_widget_list,
                struct widget *end_group_widget_list)
{
  struct widget *tmp_widget = end_group_widget_list;

  while (tmp_widget) {
    set_wflag(tmp_widget, WF_HIDDEN);

    if (tmp_widget == begin_group_widget_list) {
      break;
    }

    tmp_widget = tmp_widget->prev;
  }
}

/**********************************************************************//**
  Show all widgets in the group.
**************************************************************************/
void show_group(struct widget *begin_group_widget_list,
                struct widget *end_group_widget_list)
{
  struct widget *tmp_widget = end_group_widget_list;

  while (tmp_widget) {
    clear_wflag(tmp_widget, WF_HIDDEN);

    if (tmp_widget == begin_group_widget_list) {
      break;
    }

    tmp_widget = tmp_widget->prev;
  }
}

/**********************************************************************//**
  Set area for all widgets in the group.
**************************************************************************/
void group_set_area(struct widget *begin_group_widget_list,
                    struct widget *end_group_widget_list,
                    SDL_Rect area)
{
  struct widget *pwidget = end_group_widget_list;

  while (pwidget) {
    widget_set_area(pwidget, area);

    if (pwidget == begin_group_widget_list) {
      break;
    }

    pwidget = pwidget->prev;
  }
}

/* ===================================================================== *
 * =========================== Window Group ============================ *
 * ===================================================================== */

/*
 *	Window Group  -	group with 'begin' and 'end' where
 *	windowed type widget is last on list ( 'end' ).
 */

/**********************************************************************//**
  Undraw and destroy Window Group  The Trick is simple. We undraw only
  last member of group: the window.
**************************************************************************/
void popdown_window_group_dialog(struct widget *begin_group_widget_list,
                                 struct widget *end_group_widget_list)
{
  if ((begin_group_widget_list) && (end_group_widget_list)) {
    widget_mark_dirty(end_group_widget_list);
    remove_gui_layer(end_group_widget_list->dst);

    del_group(begin_group_widget_list, end_group_widget_list);
  }
}

/**********************************************************************//**
  Select Window Group. (move widget group up the widgets list)
  Function return TRUE when group was selected.
**************************************************************************/
bool select_window_group_dialog(struct widget *begin_widget_list,
                                struct widget *pwindow)
{
  if (!is_this_widget_first_on_list(begin_widget_list)) {
    move_group_to_front_of_gui_list(begin_widget_list, pwindow);

    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Move Window Group.  The Trick is simple.  We move only last member of
  group: the window , and then set new position to all members of group.

  Function return 1 when group was moved.
**************************************************************************/
bool move_window_group_dialog(struct widget *begin_group_widget_list,
                              struct widget *end_group_widget_list)
{
  bool ret = FALSE;
  Sint16 oldX = end_group_widget_list->size.x, oldY = end_group_widget_list->size.y;

  if (move_window(end_group_widget_list)) {
    set_new_group_start_pos(begin_group_widget_list,
                            end_group_widget_list->prev,
                            end_group_widget_list->size.x - oldX,
                            end_group_widget_list->size.y - oldY);
    ret = TRUE;
  }

  return ret;
}

/**********************************************************************//**
  Standart Window Group Widget Callback (window)
  When Pressed check mouse move;
  if move then move window and redraw else
  if not on fron then move window up to list and redraw.
**************************************************************************/
void move_window_group(struct widget *begin_widget_list, struct widget *pwindow)
{
  if (select_window_group_dialog(begin_widget_list, pwindow)) {
    widget_flush(pwindow);
  }

  move_window_group_dialog(begin_widget_list, pwindow);
}

/**********************************************************************//**
  Setup widgets vertically.
**************************************************************************/
int setup_vertical_widgets_position(int step,
                                    Sint16 start_x, Sint16 start_y,
                                    Uint16 w, Uint16 h,
                                    struct widget *begin, struct widget *end)
{
  struct widget *buf = end;
  register int count = 0;
  register int real_start_x = start_x;
  int ret = 0;

  while (buf) {
    buf->size.x = real_start_x;
    buf->size.y = start_y;

    if (w) {
      buf->size.w = w;
    }

    if (h) {
      buf->size.h = h;
    }

    if (((count + 1) % step) == 0) {
      real_start_x = start_x;
      start_y += buf->size.h;
      if (!(get_wflags(buf) & WF_HIDDEN)) {
        ret += buf->size.h;
      }
    } else {
      real_start_x += buf->size.w;
    }

    if (buf == begin) {
      break;
    }
    count++;
    buf = buf->prev;
  }

  return ret;
}

/* =================================================== */
/* ======================= WINDOWs ==================== */
/* =================================================== */

/**************************************************************************
  Window Manager Mechanism.
  Idea is simple each window/dialog has own buffer layer which is draw
  on screen during flush operations.
  This consume lots of memory but is extremly effecive.

  Each widget has own "destination" parm == where ( on what buffer )
  will be draw.
**************************************************************************/

/* =================================================== */
/* ======================== MISC ===================== */
/* =================================================== */

/**********************************************************************//**
  Draw Themed Frame.
**************************************************************************/
void draw_frame(SDL_Surface *pdest, Sint16 start_x, Sint16 start_y,
                Uint16 w, Uint16 h)
{
  SDL_Surface *pTmpLeft =
    ResizeSurface(current_theme->FR_Left, current_theme->FR_Left->w, h, 1);
  SDL_Surface *pTmpRight =
    ResizeSurface(current_theme->FR_Right, current_theme->FR_Right->w, h, 1);
  SDL_Surface *pTmpTop =
    ResizeSurface(current_theme->FR_Top, w, current_theme->FR_Top->h, 1);
  SDL_Surface *pTmpBottom =
    ResizeSurface(current_theme->FR_Bottom, w, current_theme->FR_Bottom->h, 1);
  SDL_Rect tmp,dst = {start_x, start_y, 0, 0};

  tmp = dst;
  alphablit(pTmpLeft, NULL, pdest, &tmp, 255);

  dst.x += w - pTmpRight->w;
  tmp = dst;
  alphablit(pTmpRight, NULL, pdest, &tmp, 255);

  dst.x = start_x;
  tmp = dst;
  alphablit(pTmpTop, NULL, pdest, &tmp, 255);

  dst.y += h - pTmpBottom->h;
  tmp = dst;
  alphablit(pTmpBottom, NULL, pdest, &tmp, 255);

  FREESURFACE(pTmpLeft);
  FREESURFACE(pTmpRight);
  FREESURFACE(pTmpTop);
  FREESURFACE(pTmpBottom);
}

/**********************************************************************//**
  Redraw background of the widget.
**************************************************************************/
void refresh_widget_background(struct widget *pwidget)
{
  if (pwidget) {
    if (pwidget->gfx && pwidget->gfx->w == pwidget->size.w
        && pwidget->gfx->h == pwidget->size.h) {
      clear_surface(pwidget->gfx, NULL);
      alphablit(pwidget->dst->surface, &pwidget->size, pwidget->gfx, NULL, 255);
    } else {
      FREESURFACE(pwidget->gfx);
      pwidget->gfx = crop_rect_from_surface(pwidget->dst->surface, &pwidget->size);
    }
  }
}
