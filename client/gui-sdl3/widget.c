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

/* SDL3 */
#include <SDL3/SDL.h>

/* utility */
#include "log.h"

/* gui-sdl3 */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"

#include "widget.h"
#include "widget_p.h"

struct widget *selected_widget;
SDL_Rect *info_area = NULL;

extern Uint32 widget_info_counter;

/* ================================ Private ============================ */

static struct widget *begin_main_widget_list;

static SDL_Surface *info_label = NULL;

/**********************************************************************//**
  Correct background size ( set min size ). Used in create widget
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
  then return pointer to this image.

  Graphic is taken from ptheme surface and blit to new created image.

  Type of image depend of "state" parameter.
**************************************************************************/
SDL_Surface *create_bcgnd_surf(SDL_Surface *ptheme, enum widget_state state,
                               Uint16 width, Uint16 height)
{
  bool zoom;
  int tile_width_len_end, tile_width_len_mid, tile_count_len_mid;
  int tile_width_height_end, tile_width_height_mid, tile_count_height_mid;
  int i, j;
  SDL_Rect src, des;
  SDL_Surface *background = NULL;
  int start_y = (ptheme->h / 4) * state;

  tile_width_len_end = ptheme->w / 16;
  tile_width_len_mid = ptheme->w - (tile_width_len_end * 2);

  tile_count_len_mid =
      (width - (tile_width_len_end * 2)) / tile_width_len_mid;

  /* corrections I */
  if (((tile_count_len_mid *
        tile_width_len_mid) + (tile_width_len_end * 2)) < width) {
    tile_count_len_mid++;
  }

  tile_width_height_end = ptheme->h / 16;
  tile_width_height_mid = (ptheme->h / 4) - (tile_width_height_end * 2);
  tile_count_height_mid =
      (height - (tile_width_height_end * 2)) / tile_width_height_mid;

  /* corrections II */
  if (((tile_count_height_mid *
        tile_width_height_mid) + (tile_width_height_end * 2)) < height) {
    tile_count_height_mid++;
  }

  i = MAX(tile_width_len_end * 2, width);
  j = MAX(tile_width_height_end * 2, height);
  zoom = ((i != width) ||  (j != height));

  /* Now allocate memory */
  background = create_surf(i, j);

  /* Copy left end */

  /* Left top */
  src.x = 0;
  src.y = start_y;
  src.w = tile_width_len_end;
  src.h = tile_width_height_end;

  des.x = 0;
  des.y = 0;
  alphablit(ptheme, &src, background, &des, 255);

  /* left middle */
  src.y = start_y + tile_width_height_end;
  src.h = tile_width_height_mid;
  for (i = 0; i < tile_count_height_mid; i++) {
    des.y = tile_width_height_end + i * tile_width_height_mid;
    alphablit(ptheme, &src, background, &des, 255);
  }

  /* left bottom */
  src.y = start_y + ((ptheme->h / 4) - tile_width_height_end);
  src.h = tile_width_height_end;
  des.y = background->h - tile_width_height_end;
  clear_surface(background, &des);
  alphablit(ptheme, &src, background, &des, 255);

  /* copy middle parts */

  src.x = tile_width_len_end;
  src.y = start_y;
  src.w = tile_width_len_mid;

  for (i = 0; i < tile_count_len_mid; i++) {

    /* middle top */
    des.x = tile_width_len_end + i * tile_width_len_mid;
    des.y = 0;
    src.y = start_y;
    alphablit(ptheme, &src, background, &des, 255);

    /* middle middle */
    src.y = start_y + tile_width_height_end;
    src.h = tile_width_height_mid;
    for (j = 0; j < tile_count_height_mid; j++) {
      des.y = tile_width_height_end + j * tile_width_height_mid;
      alphablit(ptheme, &src, background, &des, 255);
    }

    /* middle bottom */
    src.y = start_y + ((ptheme->h / 4) - tile_width_height_end);
    src.h = tile_width_height_end;
    des.y = background->h - tile_width_height_end;
    clear_surface(background, &des);
    alphablit(ptheme, &src, background, &des, 255);
  }

  /* copy right end */

  /* right top */
  src.x = ptheme->w - tile_width_len_end;
  src.y = start_y;
  src.w = tile_width_len_end;

  des.x = background->w - tile_width_len_end;
  des.y = 0;

  alphablit(ptheme, &src, background, &des, 255);

  /* right middle */
  src.y = start_y + tile_width_height_end;
  src.h = tile_width_height_mid;
  for (i = 0; i < tile_count_height_mid; i++) {
    des.y = tile_width_height_end + i * tile_width_height_mid;
    alphablit(ptheme, &src, background, &des, 255);
  }

  /* right bottom */
  src.y = start_y + ((ptheme->h / 4) - tile_width_height_end);
  src.h = tile_width_height_end;
  des.y = background->h - tile_width_height_end;
  clear_surface(background, &des);
  alphablit(ptheme, &src, background, &des, 255);

  if (zoom) {
    SDL_Surface *zoomed = resize_surface(background, width, height, 1);

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
  struct widget *pwidget;

  pwidget = start_widget ? start_widget : begin_main_widget_list;

  while (pwidget) {
    SDL_Rect area = {
      .x = pwidget->dst->dest_rect.x + pwidget->size.x,
      .y = pwidget->dst->dest_rect.y + pwidget->size.y,
      .w = pwidget->size.w,
      .h = pwidget->size.h};

    if (is_in_rect_area(x, y, &area)
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
                                        SDL_KeyboardEvent *key)
{
  struct widget *pwidget;

  pwidget = start_widget ? start_widget : begin_main_widget_list;

  key->mod &= ~(SDL_KMOD_NUM | SDL_KMOD_CAPS);
  while (pwidget) {
    if ((pwidget->key == key->key
         || (pwidget->key == SDLK_RETURN && key->key == SDLK_KP_ENTER)
         || (pwidget->key == SDLK_KP_ENTER && key->key == SDLK_RETURN))
        && ((pwidget->mod & key->mod) || (pwidget->mod == key->mod))) {
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
    wait 300 ms ( to see result :)
    If exist (button callback function) then
      call (button callback function)

    Function normal return Widget ID.
    NOTE: NOZERO return of this function determinate exit of
        MAIN_SDL_GAME_LOOP
    if ( pwidget->action )
      if ( pwidget->action(pwidget)  ) ID = 0;
    if widget callback function return = 0 then return NOZERO
    I default return (-1) from Widget callback functions.
**************************************************************************/
Uint16 widget_pressed_action(struct widget *pwidget)
{
  Uint16 id;

  if (!pwidget) {
    return 0;
  }

  widget_info_counter = 0;
  if (info_area) {
    dirty_sdl_rect(info_area);
    FC_FREE(info_area);
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
      if (pwidget->action != NULL && pwidget->action(pwidget)) {
        id = 0;
      } else {
        id = pwidget->id;
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
          if (change != ED_FORCE_EXIT && change != ED_ESC
              && pwidget->action != NULL) {
            pwidget->action(pwidget);
          }
          if (loop && change == ED_RETURN) {
            ret = TRUE;
          }
        } while (ret);
      }

      id = 0;
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
      if (pwidget->action != NULL && pwidget->action(pwidget)) {
        id = 0;
      } else {
        id = pwidget->id;
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
      if (pwidget->action != NULL && pwidget->action(pwidget)) {
        id = 0;
      } else {
        id = pwidget->id;
      }

      break;
    case WT_COMBO:
      if (PRESSED_EVENT(main_data.event)) {
        set_wstate(pwidget, FC_WS_PRESSED);
        combo_popup(pwidget);
      } else {
        combo_popdown(pwidget);
      }

      id = 0;
      break;
    default:
      if (pwidget->action != NULL && pwidget->action(pwidget)) {
        id = 0;
      } else {
        id = pwidget->id;
      }

      break;
  }

  return id;
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

      /* Turn off quick info timer/counter */
      widget_info_counter = 0;
    }
  }

  if (info_area) {
    flush_rect(info_area, FALSE);
    FC_FREE(info_area);
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
    info_area = fc_calloc(1, sizeof(SDL_Rect));

    color = pwidget->info_label->fgcol;
    pwidget->info_label->style |= TTF_STYLE_BOLD;
    pwidget->info_label->fgcol = *get_theme_color(COLOR_THEME_QUICK_INFO_TEXT);

    /* Create string and bcgd theme */
    text = create_text_surf_from_utf8(pwidget->info_label);

    pwidget->info_label->fgcol = color;

    info_label = create_filled_surface(text->w + adj_size(10), text->h + adj_size(6),
                                       get_theme_color(COLOR_THEME_QUICK_INFO_BG));

    /* Calculate start position */
    if ((pwidget->dst->dest_rect.y + pwidget->size.y) - info_label->h - adj_size(6) < 0) {
      info_area->y = (pwidget->dst->dest_rect.y + pwidget->size.y) + pwidget->size.h + adj_size(3);
    } else {
      info_area->y = (pwidget->dst->dest_rect.y + pwidget->size.y) - info_label->h - adj_size(5);
    }

    if ((pwidget->dst->dest_rect.x + pwidget->size.x) + info_label->w + adj_size(5) > main_window_width()) {
      info_area->x = (pwidget->dst->dest_rect.x + pwidget->size.x) - info_label->w - adj_size(5);
    } else {
      info_area->x = (pwidget->dst->dest_rect.x + pwidget->size.x) + adj_size(3);
    }

    info_area->w = info_label->w + adj_size(2);
    info_area->h = info_label->h + adj_size(3);

    /* Draw text */
    dstrect.x = adj_size(6);
    dstrect.y = adj_size(3);

    alphablit(text, NULL, info_label, &dstrect, 255);

    FREESURFACE(text);

    /* Draw frame */
    create_frame(info_label,
                 0, 0, info_label->w - 1, info_label->h - 1,
                 get_theme_color(COLOR_THEME_QUICK_INFO_FRAME));

  }

  if (rect) {
    dstrect.x = MAX(rect->x, info_area->x);
    dstrect.y = MAX(rect->y, info_area->y);

    srcrect.x = dstrect.x - info_area->x;
    srcrect.y = dstrect.y - info_area->y;
    srcrect.w = MIN((info_area->x + info_area->w), (rect->x + rect->w)) - dstrect.x;
    srcrect.h = MIN((info_area->y + info_area->h), (rect->y + rect->h)) - dstrect.y;

    screen_blit(info_label, &srcrect, &dstrect, 255);
  } else {
    screen_blit(info_label, NULL, info_area, 255);
  }

  if (correct_rect_region(info_area)) {
    update_main_screen();
#if 0
    SDL_UpdateRect(main_data.screen, info_area->x, info_area->y,
                   info_area->w, info_area->h);
#endif /* 0 */
  }
}

/**********************************************************************//**
  Find ID in widgets list ('gui_list') and return pointer to this
  Widget.
**************************************************************************/
struct widget *get_widget_pointer_from_id(const struct widget *gui_list,
                                          Uint16 id, enum scan_direction direction)
{
  if (direction == SCAN_FORWARD) {
    while (gui_list) {
      if (gui_list->id == id) {
        return (struct widget *) gui_list;
      }
      gui_list = gui_list->next;
    }
  } else {
    while (gui_list) {
      if (gui_list->id == id) {
        return (struct widget *) gui_list;
      }
      gui_list = gui_list->prev;
    }
  }

  return NULL;
}

/**********************************************************************//**
  Find ID in MAIN widgets list ( begin_widget_list ) and return pointer to
  this Widgets.
**************************************************************************/
struct widget *get_widget_pointer_from_main_list(Uint16 id)
{
  return get_widget_pointer_from_id(begin_main_widget_list, id, SCAN_FORWARD);
}

/**********************************************************************//**
  Add Widget to Main widgets list ( begin_widget_list )
**************************************************************************/
void add_to_gui_list(widget_id id, struct widget *gui)
{
  if (begin_main_widget_list != NULL) {
    gui->next = begin_main_widget_list;
    gui->id = id;
    begin_main_widget_list->prev = gui;
    begin_main_widget_list = gui;
  } else {
    begin_main_widget_list = gui;
    begin_main_widget_list->id = id;
  }
}

/**********************************************************************//**
  Add Widget to widgets list at add_dock position on 'prev' slot.
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
  Delete Widget from Main widgets list ( begin_widget_list )

  NOTE: This function does not destroy Widget, only remove its pointer from
  list. To destroy this Widget totally ( free mem... ) call macro:
  del_widget_from_gui_list( pwidget ). This macro call this function.
**************************************************************************/
void del_widget_pointer_from_gui_list(struct widget *gui)
{
  if (!gui) {
    return;
  }

  if (gui == begin_main_widget_list) {
    begin_main_widget_list = begin_main_widget_list->next;
  }

  if (gui->prev && gui->next) {
    gui->prev->next = gui->next;
    gui->next->prev = gui->prev;
  } else {
    if (gui->prev) {
      gui->prev->next = NULL;
    }

    if (gui->next) {
      gui->next->prev = NULL;
    }

  }

  if (selected_widget == gui) {
    selected_widget = NULL;
  }
}

/**********************************************************************//**
  Determinate if 'gui' is first on WidgetList

  NOTE: This is used by My (move) GUI Window mechanism.  Return TRUE if is
  first.
**************************************************************************/
bool is_this_widget_first_on_list(struct widget *gui)
{
  return (begin_main_widget_list == gui);
}

/**********************************************************************//**
  Move pointer to Widget to list begin.

  NOTE: This is used by My GUI Window mechanism.
**************************************************************************/
void move_widget_to_front_of_gui_list(struct widget *gui)
{
  if (!gui || gui == begin_main_widget_list) {
    return;
  }

  /* gui->prev always exists because
     we don't do this to begin_main_widget_list */
  if (gui->next) {
    gui->prev->next = gui->next;
    gui->next->prev = gui->prev;
  } else {
    gui->prev->next = NULL;
  }

  gui->next = begin_main_widget_list;
  begin_main_widget_list->prev = gui;
  begin_main_widget_list = gui;
  gui->prev = NULL;
}

/**********************************************************************//**
  Delete Main widgets list.
**************************************************************************/
void del_main_list(void)
{
  del_gui_list(begin_main_widget_list);
}

/**********************************************************************//**
  Delete Wideget's List ('gui_list').
**************************************************************************/
void del_gui_list(struct widget *gui_list)
{
  while (gui_list) {
    if (gui_list->next) {
      gui_list = gui_list->next;
      FREEWIDGET(gui_list->prev);
    } else {
      FREEWIDGET(gui_list);
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
  struct widget *tmp_widget = end_group_widget_list, *prev = NULL;
  struct gui_layer *gui_layer = get_gui_layer(end_group_widget_list->dst->surface);

  /* Widget Pointer Management */
  while (tmp_widget) {

    prev = tmp_widget->prev;

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

    tmp_widget = prev;
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
    struct widget *buf_widget = NULL;

    buf_widget = tmp_widget->next;
    del_widget_from_gui_list(buf_widget);

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
 *      Window Group  - group with 'begin' and 'end' where
 *      windowed type widget is last on list ( 'end' ).
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
  Sint16 old_x = end_group_widget_list->size.x, old_y = end_group_widget_list->size.y;

  if (move_window(end_group_widget_list)) {
    set_new_group_start_pos(begin_group_widget_list,
                            end_group_widget_list->prev,
                            end_group_widget_list->size.x - old_x,
                            end_group_widget_list->size.y - old_y);
    ret = TRUE;
  }

  return ret;
}

/**********************************************************************//**
  Standart Window Group Widget Callback (window)
  When Pressed check mouse move;
  if move then move window and redraw else
  if not on front then move window up to list and redraw.
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
  SDL_Surface *tmp_left =
    resize_surface(current_theme->fr_left, current_theme->fr_left->w, h, 1);
  SDL_Surface *tmp_right =
    resize_surface(current_theme->fr_right, current_theme->fr_right->w, h, 1);
  SDL_Surface *tmp_top =
    resize_surface(current_theme->fr_top, w, current_theme->fr_top->h, 1);
  SDL_Surface *tmp_bottom =
    resize_surface(current_theme->fr_bottom, w, current_theme->fr_bottom->h, 1);
  SDL_Rect tmp,dst = {start_x, start_y, 0, 0};

  tmp = dst;
  alphablit(tmp_left, NULL, pdest, &tmp, 255);

  dst.x += w - tmp_right->w;
  tmp = dst;
  alphablit(tmp_right, NULL, pdest, &tmp, 255);

  dst.x = start_x;
  tmp = dst;
  alphablit(tmp_top, NULL, pdest, &tmp, 255);

  dst.y += h - tmp_bottom->h;
  tmp = dst;
  alphablit(tmp_bottom, NULL, pdest, &tmp, 255);

  FREESURFACE(tmp_left);
  FREESURFACE(tmp_right);
  FREESURFACE(tmp_top);
  FREESURFACE(tmp_bottom);
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
