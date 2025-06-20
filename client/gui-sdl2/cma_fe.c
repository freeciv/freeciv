/***********************************************************************
 Freeciv - Copyright (C) 2001 - R. Falke, M. Kaufman
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
#include <fc_config.h>
#endif

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* utility */
#include "fcintl.h"

/* common */
#include "game.h"

/* client */
#include "client_main.h" /* can_client_issue_orders() */

/* gui-sdl2 */
#include "citydlg.h"
#include "cityrep.h"
#include "cma_fec.h"
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"

#include "cma_fe.h"

struct hmove {
  struct widget *pscroll_bar;
  int min, max, base;
};

static struct cma_dialog {
  struct city *pcity;
  struct small_dialog *dlg;
  struct advanced_dialog *adv;
  struct cm_parameter edited_cm_parm;
} *cma = NULL;

enum specialist_type {
  SP_ELVIS, SP_SCIENTIST, SP_TAXMAN, SP_LAST
};

static void set_cma_hscrollbars(void);

/* =================================================================== */

/**********************************************************************//**
  User interacted with cma dialog.
**************************************************************************/
static int cma_dlg_callback(struct widget *pwindow)
{
  return -1;
}

/**********************************************************************//**
  User interacted with cma dialog close button.
**************************************************************************/
static int exit_cma_dialog_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_city_cma_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User released mouse button while in scrollbar.
**************************************************************************/
static widget_id scroll_mouse_button_up(SDL_MouseButtonEvent *button_event,
                                        void *data)
{
  return (widget_id)ID_SCROLLBAR;
}

/**********************************************************************//**
  User moved mouse while holding scrollbar.
**************************************************************************/
static widget_id scroll_mouse_motion_handler(SDL_MouseMotionEvent *motion_event,
                                             void *data)
{
  struct hmove *motion = (struct hmove *)data;
  char cbuf[4];

  motion_event->x -= motion->pscroll_bar->dst->dest_rect.x;

  if (motion && motion_event->xrel
      && (motion_event->x >= motion->min) && (motion_event->x <= motion->max)) {
    /* draw bcgd */
    widget_undraw(motion->pscroll_bar);
    widget_mark_dirty(motion->pscroll_bar);

    if ((motion->pscroll_bar->size.x + motion_event->xrel) >
        (motion->max - motion->pscroll_bar->size.w)) {
      motion->pscroll_bar->size.x = motion->max - motion->pscroll_bar->size.w;
    } else {
      if ((motion->pscroll_bar->size.x + motion_event->xrel) < motion->min) {
        motion->pscroll_bar->size.x = motion->min;
      } else {
        motion->pscroll_bar->size.x += motion_event->xrel;
      }
    }

    *(int *)motion->pscroll_bar->data.ptr =
      motion->base + (motion->pscroll_bar->size.x - motion->min);

    fc_snprintf(cbuf, sizeof(cbuf), "%d",
                *(int *)motion->pscroll_bar->data.ptr);
    copy_chars_to_utf8_str(motion->pscroll_bar->next->string_utf8, cbuf);

    /* Redraw label */
    widget_redraw(motion->pscroll_bar->next);
    widget_mark_dirty(motion->pscroll_bar->next);

    /* Redraw scrollbar */
    if (get_wflags(motion->pscroll_bar) & WF_RESTORE_BACKGROUND) {
      refresh_widget_background(motion->pscroll_bar);
    }
    widget_redraw(motion->pscroll_bar);
    widget_mark_dirty(motion->pscroll_bar);

    flush_dirty();
  }

  return ID_ERROR;
}

/**********************************************************************//**
  User interacted with minimal horizontal cma scrollbar
**************************************************************************/
static int min_horiz_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct hmove motion;

    motion.pscroll_bar = pwidget;
    motion.min = pwidget->next->size.x + pwidget->next->size.w + 5;
    motion.max = motion.min + 70;
    motion.base = -20;

    MOVE_STEP_X = 2;
    MOVE_STEP_Y = 0;
    /* Filter mouse motion events */
    SDL_SetEventFilter(FilterMouseMotionEvents, NULL);
    gui_event_loop((void *)(&motion), NULL, NULL, NULL, NULL, NULL, NULL,
                   NULL, NULL,
                   scroll_mouse_button_up, scroll_mouse_motion_handler);
    /* Turn off Filter mouse motion events */
    SDL_SetEventFilter(NULL, NULL);
    MOVE_STEP_X = DEFAULT_MOVE_STEP;
    MOVE_STEP_Y = DEFAULT_MOVE_STEP;

    selected_widget = pwidget;
    set_wstate(pwidget, FC_WS_SELECTED);
    /* save the change */
    cmafec_set_fe_parameter(cma->pcity, &cma->edited_cm_parm);
    /* refreshes the cma */
    if (cma_is_city_under_agent(cma->pcity, NULL)) {
      cma_release_city(cma->pcity);
      cma_put_city_under_agent(cma->pcity, &cma->edited_cm_parm);
    }
    update_city_cma_dialog();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with factor horizontal cma scrollbar
**************************************************************************/
static int factor_horiz_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct hmove motion;

    motion.pscroll_bar = pwidget;
    motion.min = pwidget->next->size.x + pwidget->next->size.w + 5;
    motion.max = motion.min + 54;
    motion.base = 1;

    MOVE_STEP_X = 2;
    MOVE_STEP_Y = 0;
    /* Filter mouse motion events */
    SDL_SetEventFilter(FilterMouseMotionEvents, NULL);
    gui_event_loop((void *)(&motion), NULL, NULL, NULL, NULL, NULL, NULL,
                   NULL, NULL,
                   scroll_mouse_button_up, scroll_mouse_motion_handler);
    /* Turn off Filter mouse motion events */
    SDL_SetEventFilter(NULL, NULL);
    MOVE_STEP_X = DEFAULT_MOVE_STEP;
    MOVE_STEP_Y = DEFAULT_MOVE_STEP;

    selected_widget = pwidget;
    set_wstate(pwidget, FC_WS_SELECTED);
    /* save the change */
    cmafec_set_fe_parameter(cma->pcity, &cma->edited_cm_parm);
    /* refreshes the cma */
    if (cma_is_city_under_agent(cma->pcity, NULL)) {
      cma_release_city(cma->pcity);
      cma_put_city_under_agent(cma->pcity, &cma->edited_cm_parm);
    }
    update_city_cma_dialog();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with cma celebrating -toggle.
**************************************************************************/
static int toggle_cma_celebrating_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    cma->edited_cm_parm.require_happy ^= TRUE;
    /* save the change */
    cmafec_set_fe_parameter(cma->pcity, &cma->edited_cm_parm);
    update_city_cma_dialog();
  }

  return -1;
}

/* ============================================================= */

/**********************************************************************//**
  User interacted with widget that result in cma window getting saved.
**************************************************************************/
static int save_cma_window_callback(struct widget *pwindow)
{
  return -1;
}

/**********************************************************************//**
  User interacted with "yes" button from save cma dialog.
**************************************************************************/
static int ok_save_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pwidget && cma && cma->adv) {
      struct widget *pedit = (struct widget *)pwidget->data.ptr;

      if (pedit->string_utf8->text != NULL) {
        cmafec_preset_add(pedit->string_utf8->text, &cma->edited_cm_parm);
      } else {
        cmafec_preset_add(_("new preset"), &cma->edited_cm_parm);
      }

      del_group_of_widgets_from_gui_list(cma->adv->begin_widget_list,
                                         cma->adv->end_widget_list);
      FC_FREE(cma->adv);

      update_city_cma_dialog();
    }
  }

  return -1;
}

/**********************************************************************//**
  Cancel : SAVE, LOAD, DELETE Dialogs
**************************************************************************/
static int cancel_sld_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (cma && cma->adv) {
      popdown_window_group_dialog(cma->adv->begin_widget_list,
                                  cma->adv->end_widget_list);
      FC_FREE(cma->adv->scroll);
      FC_FREE(cma->adv);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with cma setting saving button.
**************************************************************************/
static int save_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct widget *buf, *pwindow;
    utf8_str *pstr;
    SDL_Surface *text;
    SDL_Rect dst;
    SDL_Rect area;

    if (cma->adv) {
      return 1;
    }

    cma->adv = fc_calloc(1, sizeof(struct advanced_dialog));

    pstr = create_utf8_from_char_fonto(_("Name new preset"),
                                       FONTO_ATTENTION);
    pstr->style |= TTF_STYLE_BOLD;

    pwindow = create_window_skeleton(NULL, pstr, 0);

    pwindow->action = save_cma_window_callback;
    set_wstate(pwindow, FC_WS_NORMAL);
    cma->adv->end_widget_list = pwindow;

    add_to_gui_list(ID_WINDOW, pwindow);

    area = pwindow->area;
    area.h = MAX(area.h, 1);

    /* ============================================================= */
    /* Label */
    pstr = create_utf8_from_char_fonto(_("What should we name the preset?"),
                                       FONTO_DEFAULT);
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pstr->fgcol = *get_theme_color(COLOR_THEME_CMA_TEXT);

    text = create_text_surf_from_utf8(pstr);
    FREEUTF8STR(pstr);
    area.w = MAX(area.w, text->w);
    area.h += text->h + adj_size(5);
    /* ============================================================= */

    buf = create_edit(NULL, pwindow->dst,
                      create_utf8_from_char_fonto(_("new preset"),
                                                  FONTO_ATTENTION),
                      adj_size(100),
                      (WF_RESTORE_BACKGROUND|WF_FREE_STRING));
    set_wstate(buf, FC_WS_NORMAL);
    area.h += buf->size.h;
    area.w = MAX(area.w, buf->size.w);

    add_to_gui_list(ID_EDIT, buf);
    /* ============================================================= */

    buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                   pwindow->dst,
                                                   _("Yes"),
                                                   FONTO_ATTENTION, 0);

    buf->action = ok_save_cma_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->key = SDLK_RETURN;
    add_to_gui_list(ID_BUTTON, buf);
    buf->data.ptr = (void *)buf->next;

    buf = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                                   pwindow->dst, _("No"),
                                                   FONTO_ATTENTION, 0);
    buf->action = cancel_sld_cma_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->key = SDLK_ESCAPE;

    add_to_gui_list(ID_BUTTON, buf);

    area.h += buf->size.h;
    buf->size.w = MAX(buf->next->size.w, buf->size.w);
    buf->next->size.w = buf->size.w;
    area.w = MAX(area.w, 2 * buf->size.w + adj_size(20));

    cma->adv->begin_widget_list = buf;

    /* setup window size and start position */
    area.w += adj_size(20);
    area.h += adj_size(15);

    resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                  (pwindow->size.w - pwindow->area.w) + area.w,
                  (pwindow->size.h - pwindow->area.h) + area.h);

    area = pwindow->area;

    widget_set_position(pwindow,
                        pwidget->size.x - pwindow->size.w / 2,
                        pwidget->size.y - pwindow->size.h);

    /* setup rest of widgets */
    /* label */
    dst.x = area.x + (area.w - text->w) / 2;
    dst.y = area.y + 1;
    alphablit(text, NULL, pwindow->theme, &dst, 255);
    dst.y += text->h + adj_size(5);
    FREESURFACE(text);

    /* edit */
    buf = pwindow->prev;
    buf->size.w = area.w - adj_size(10);
    buf->size.x = area.x + adj_size(5);
    buf->size.y = dst.y;
    dst.y += buf->size.h + adj_size(5);

    /* yes */
    buf = buf->prev;
    buf->size.x = area.x + (area.w - (2 * buf->size.w + adj_size(20))) / 2;
    buf->size.y = dst.y;

    /* no */
    buf = buf->prev;
    buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(20);
    buf->size.y = buf->next->size.y;

    /* ================================================== */
    /* redraw */
    redraw_group(cma->adv->begin_widget_list, pwindow, 0);
    widget_mark_dirty(pwindow);
    flush_dirty();
  }

  return -1;
}

/* ================================================== */

/**********************************************************************//**
  User interacted with some preset cma button.
**************************************************************************/
static int ld_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    bool load = pwidget->data.ptr != NULL;
    int index = MAX_ID - pwidget->id;

    popdown_window_group_dialog(cma->adv->begin_widget_list,
                                cma->adv->end_widget_list);
    FC_FREE(cma->adv->scroll);
    FC_FREE(cma->adv);

    if (load) {
      cm_copy_parameter(&cma->edited_cm_parm,
                        cmafec_preset_get_parameter(index));
      set_cma_hscrollbars();
      /* save the change */
      cmafec_set_fe_parameter(cma->pcity, &cma->edited_cm_parm);
      /* stop the cma */
      if (cma_is_city_under_agent(cma->pcity, NULL)) {
        cma_release_city(cma->pcity);
      }
    } else {
      cmafec_preset_remove(index);
    }

    update_city_cma_dialog();
  }

  return -1;
}

/**********************************************************************//**
  User clicked either load or delete preset widget.
**************************************************************************/
static void popup_load_del_presets_dialog(bool load, struct widget *button)
{
  int hh, count, i;
  struct widget *buf, *pwindow;
  utf8_str *pstr;
  SDL_Rect area;

  if (cma->adv) {
    return;
  }

  count = cmafec_preset_num();

  if (count == 1) {
    if (load) {
      cm_copy_parameter(&cma->edited_cm_parm, cmafec_preset_get_parameter(0));
      set_cma_hscrollbars();
      /* save the change */
      cmafec_set_fe_parameter(cma->pcity, &cma->edited_cm_parm);
      /* stop the cma */
      if (cma_is_city_under_agent(cma->pcity, NULL)) {
        cma_release_city(cma->pcity);
      }
    } else {
      cmafec_preset_remove(0);
    }
    update_city_cma_dialog();
    return;
  }

  cma->adv = fc_calloc(1, sizeof(struct advanced_dialog));

  pstr = create_utf8_from_char_fonto(_("Presets"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = save_cma_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);
  cma->adv->end_widget_list = pwindow;

  add_to_gui_list(ID_WINDOW, pwindow);

  area = pwindow->area;

  /* ---------- */
  /* Create exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  buf->action = cancel_sld_cma_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  area.w += (buf->size.w + adj_size(10));

  add_to_gui_list(ID_BUTTON, buf);
  /* ---------- */

  for (i = 0; i < count; i++) {
    pstr = create_utf8_from_char_fonto(cmafec_preset_get_descr(i), FONTO_DEFAULT);
    pstr->style |= TTF_STYLE_BOLD;
    buf = create_iconlabel(NULL, pwindow->dst, pstr,
            (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    buf->string_utf8->bgcol = (SDL_Color) {0, 0, 0, 0};
    buf->action = ld_cma_callback;

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    set_wstate(buf , FC_WS_NORMAL);

    if (load) {
      buf->data.ptr = (void *)1;
    } else {
      buf->data.ptr = NULL;
    }

    add_to_gui_list(MAX_ID - i, buf);

    if (i > 10) {
      set_wflag(buf, WF_HIDDEN);
    }
  }
  cma->adv->begin_widget_list = buf;
  cma->adv->begin_active_widget_list = cma->adv->begin_widget_list;
  cma->adv->end_active_widget_list = pwindow->prev->prev;
  cma->adv->active_widget_list = cma->adv->end_active_widget_list;

  area.w += adj_size(2);
  area.h += 1;

  if (count > 11) {
    create_vertical_scrollbar(cma->adv, 1, 11, FALSE, TRUE);

    /* ------- window ------- */
    area.h = 11 * pwindow->prev->prev->size.h + adj_size(2)
      + 2 * cma->adv->scroll->up_left_button->size.h;
    cma->adv->scroll->up_left_button->size.w = area.w;
    cma->adv->scroll->down_right_button->size.w = area.w;
  }

  /* ----------------------------------- */

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  widget_set_position(pwindow,
                      button->size.x - (pwindow->size.w / 2),
                      button->size.y - pwindow->size.h);

  /* exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  buf = buf->prev;
  hh = (cma->adv->scroll ? cma->adv->scroll->up_left_button->size.h + 1 : 0);
  setup_vertical_widgets_position(1, area.x + 1,
                                  area.y + 1 + hh, area.w - 1, 0,
                                  cma->adv->begin_active_widget_list, buf);

  if (cma->adv->scroll) {
    cma->adv->scroll->up_left_button->size.x = area.x;
    cma->adv->scroll->up_left_button->size.y = area.y;
    cma->adv->scroll->down_right_button->size.x = area.x;
    cma->adv->scroll->down_right_button->size.y =
      area.y + area.h - cma->adv->scroll->down_right_button->size.h;
  }

  /* ==================================================== */
  /* redraw */
  redraw_group(cma->adv->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}

/**********************************************************************//**
  User interacted with load cma settings -widget
**************************************************************************/
static int load_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_load_del_presets_dialog(TRUE, pwidget);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with delete cma settings -widget
**************************************************************************/
static int del_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_load_del_presets_dialog(FALSE, pwidget);
  }

  return -1;
}

/* ================================================== */

/**********************************************************************//**
  Changes the workers of the city to the cma parameters and puts the
  city under agent control
**************************************************************************/
static int run_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    cma_put_city_under_agent(cma->pcity, &cma->edited_cm_parm);
    update_city_cma_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Changes the workers of the city to the cma parameters
**************************************************************************/
static int run_cma_once_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct cm_result *result;

    update_city_cma_dialog();
    /* fill in result label */
    result = cm_result_new(cma->pcity);
    cm_query_result(cma->pcity, &cma->edited_cm_parm, result, FALSE);
    cma_apply_result(cma->pcity, result);
    cm_result_destroy(result);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with release city from cma -widget
**************************************************************************/
static int stop_cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    cma_release_city(cma->pcity);
    update_city_cma_dialog();
  }

  return -1;
}

/* ===================================================================== */

/**********************************************************************//**
  Setup horizontal cma scrollbars
**************************************************************************/
static void set_cma_hscrollbars(void)
{
  struct widget *pbuf;
  char cbuf[4];

  if (!cma) {
    return;
  }

  /* exit button */
  pbuf = cma->dlg->end_widget_list->prev;
  output_type_iterate(i) {
    /* min label */
    pbuf = pbuf->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", *(int *)pbuf->prev->data.ptr);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

    /* min scrollbar */
    pbuf = pbuf->prev;
    pbuf->size.x = pbuf->next->size.x
        + pbuf->next->size.w + adj_size(5) + adj_size(20) + *(int *)pbuf->data.ptr;

    /* factor label */
    pbuf = pbuf->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", *(int *)pbuf->prev->data.ptr);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

    /* factor scrollbar*/
    pbuf = pbuf->prev;
    pbuf->size.x = pbuf->next->size.x
      + pbuf->next->size.w + adj_size(5) + *(int *)pbuf->data.ptr - 1;
  } output_type_iterate_end;

  /* happy factor label */
  pbuf = pbuf->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", *(int *)pbuf->prev->data.ptr);
  copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

  /* happy factor scrollbar */
  pbuf = pbuf->prev;
  pbuf->size.x = pbuf->next->size.x
    + pbuf->next->size.w + adj_size(5) + *(int *)pbuf->data.ptr - 1;
}

/**********************************************************************//**
  Update cma dialog
**************************************************************************/
void update_city_cma_dialog(void)
{
  SDL_Color bg_color = {255, 255, 255, 136};
  int count, step, i;
  struct widget *buf = cma->dlg->end_widget_list; /* pwindow */
  SDL_Surface *text;
  utf8_str *pstr;
  SDL_Rect dst;
  bool cma_presets_exist = cmafec_preset_num() > 0;
  bool client_under_control = can_client_issue_orders();
  bool controlled = cma_is_city_under_agent(cma->pcity, NULL);
  struct cm_result *result = cm_result_new(cma->pcity);

  /* redraw window background and exit button */
  redraw_group(buf->prev, buf, 0);

  /* fill in result label */
  cm_result_from_main_map(result, cma->pcity);

  if (result->found_a_valid) {
    /* redraw Citizens */
    count = city_size_get(cma->pcity);

    text = get_tax_surface(O_LUXURY);
    step = (buf->size.w - adj_size(20)) / text->w;
    if (count > step) {
      step = (buf->size.w - adj_size(20) - text->w) / (count - 1);
    } else {
      step = text->w;
    }

    dst.y = buf->area.y + adj_size(4);
    dst.x = buf->area.x + adj_size(7);

    for (i = 0;
         i < count - (result->specialists[SP_ELVIS]
                      + result->specialists[SP_SCIENTIST]
                      + result->specialists[SP_TAXMAN]); i++) {
      text = adj_surf(get_citizen_surface(CITIZEN_CONTENT, i));
      alphablit(text, NULL, buf->dst->surface, &dst, 255);
      dst.x += step;
    }

    text = get_tax_surface(O_LUXURY);
    for (i = 0; i < result->specialists[SP_ELVIS]; i++) {
      alphablit(text, NULL, buf->dst->surface, &dst, 255);
      dst.x += step;
    }

    text = get_tax_surface(O_GOLD);
    for (i = 0; i < result->specialists[SP_TAXMAN]; i++) {
      alphablit(text, NULL, buf->dst->surface, &dst, 255);
      dst.x += step;
    }

    text = get_tax_surface(O_SCIENCE);
    for (i = 0; i < result->specialists[SP_SCIENTIST]; i++) {
      alphablit(text, NULL, buf->dst->surface, &dst, 255);
      dst.x += step;
    }
  }

  /* Create result text surface */
  pstr = create_utf8_from_char_fonto(cmafec_get_result_descr(cma->pcity, result,
                                                             &cma->edited_cm_parm),
                                     FONTO_ATTENTION);

  text = create_text_surf_from_utf8(pstr);
  FREEUTF8STR(pstr);

  /* Fill result text background */
  dst.x = buf->area.x + adj_size(7);
  dst.y = buf->area.y + adj_size(186);
  dst.w = text->w + adj_size(10);
  dst.h = text->h + adj_size(10);
  fill_rect_alpha(buf->dst->surface, &dst, &bg_color);

  create_frame(buf->dst->surface,
               dst.x, dst.y, dst.x + dst.w - 1, dst.y + dst.h - 1,
               get_theme_color(COLOR_THEME_CMA_FRAME));

  dst.x += adj_size(5);
  dst.y += adj_size(5);
  alphablit(text, NULL, buf->dst->surface, &dst, 255);
  FREESURFACE(text);

  /* Happy factor scrollbar */
  buf = cma->dlg->begin_widget_list->next->next->next->next->next->next->next;
  if (client_under_control && get_checkbox_state(buf->prev)) {
    set_wstate(buf, FC_WS_NORMAL);
  } else {
    set_wstate(buf, FC_WS_DISABLED);
  }

  /* Save as ... */
  buf = buf->prev->prev;
  if (client_under_control) {
    set_wstate(buf, FC_WS_NORMAL);
  } else {
    set_wstate(buf, FC_WS_DISABLED);
  }

  /* Load */
  buf = buf->prev;
  if (cma_presets_exist && client_under_control) {
    set_wstate(buf, FC_WS_NORMAL);
  } else {
    set_wstate(buf, FC_WS_DISABLED);
  }

  /* Del */
  buf = buf->prev;
  if (cma_presets_exist && client_under_control) {
    set_wstate(buf, FC_WS_NORMAL);
  } else {
    set_wstate(buf, FC_WS_DISABLED);
  }

  /* Run */
  buf = buf->prev;
  if (client_under_control && result->found_a_valid && !controlled) {
    set_wstate(buf, FC_WS_NORMAL);
  } else {
    set_wstate(buf, FC_WS_DISABLED);
  }

  /* Run once */
  buf = buf->prev;
  if (client_under_control && result->found_a_valid && !controlled) {
    set_wstate(buf, FC_WS_NORMAL);
  } else {
    set_wstate(buf, FC_WS_DISABLED);
  }

  /* Stop */
  buf = buf->prev;
  if (client_under_control && controlled) {
    set_wstate(buf, FC_WS_NORMAL);
  } else {
    set_wstate(buf, FC_WS_DISABLED);
  }

  /* redraw rest widgets */
  redraw_group(cma->dlg->begin_widget_list,
               cma->dlg->end_widget_list->prev->prev, 0);

  widget_flush(cma->dlg->end_widget_list);

  cm_result_destroy(result);
}

/**********************************************************************//**
  Open cma dialog for city.
**************************************************************************/
void popup_city_cma_dialog(struct city *pcity)
{
  SDL_Color bg_color = {255, 255, 255, 136};

  struct widget *pwindow, *buf;
  SDL_Surface *logo, *text[O_LAST + 1], *minimal, *factor;
  SDL_Surface *pcity_map;
  utf8_str *pstr;
  char cbuf[128];
  int w, text_w, x, cs;
  SDL_Rect dst, area;

  if (cma) {
    return;
  }

  cma = fc_calloc(1, sizeof(struct cma_dialog));
  cma->pcity = pcity;
  cma->dlg = fc_calloc(1, sizeof(struct small_dialog));
  cma->adv = NULL;
  pcity_map = get_scaled_city_map(pcity);

  cmafec_get_fe_parameter(pcity, &cma->edited_cm_parm);

  /* --------------------------- */

  fc_snprintf(cbuf, sizeof(cbuf),
              _("City of %s (Population %s citizens) : %s"),
              city_name_get(pcity),
              population_to_text(city_population(pcity)),
              _("Citizen Governor"));

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = cma_dlg_callback;
  set_wstate(pwindow, FC_WS_NORMAL);
  add_to_gui_list(ID_WINDOW, pwindow);
  cma->dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ---------- */
  /* Create exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  buf->action = exit_cma_dialog_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  area.w += (buf->size.w + adj_size(10));

  add_to_gui_list(ID_BUTTON, buf);

  pstr = create_utf8_str_fonto(NULL, 0, FONTO_ATTENTION);
  text_w = 0;

  copy_chars_to_utf8_str(pstr, _("Minimal Surplus"));
  minimal = create_text_surf_from_utf8(pstr);

  copy_chars_to_utf8_str(pstr, _("Factor"));
  factor = create_text_surf_from_utf8(pstr);

  /* ---------- */
  output_type_iterate(i) {
    copy_chars_to_utf8_str(pstr, get_output_name(i));
    text[i] = create_text_surf_from_utf8(pstr);
    text_w = MAX(text_w, text[i]->w);

    /* Minimal label */
    buf = create_iconlabel(NULL, pwindow->dst,
                           create_utf8_from_char_fonto("999", FONTO_DEFAULT),
                           (WF_FREE_STRING | WF_RESTORE_BACKGROUND));

    add_to_gui_list(ID_LABEL, buf);

    /* Minimal scrollbar */
    buf = create_horizontal(current_theme->horiz, pwindow->dst, adj_size(30),
                            (WF_RESTORE_BACKGROUND));

    buf->action = min_horiz_cma_callback;
    buf->data.ptr = &cma->edited_cm_parm.minimal_surplus[i];

    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(ID_SCROLLBAR, buf);

    /* Factor label */
    buf = create_iconlabel(NULL, pwindow->dst,
                           create_utf8_from_char_fonto("999", FONTO_DEFAULT),
                           (WF_FREE_STRING | WF_RESTORE_BACKGROUND));

    add_to_gui_list(ID_LABEL, buf);

    /* Factor scrollbar */
    buf = create_horizontal(current_theme->horiz, pwindow->dst, adj_size(30),
                            (WF_RESTORE_BACKGROUND));

    buf->action = factor_horiz_cma_callback;
    buf->data.ptr = &cma->edited_cm_parm.factor[i];

    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(ID_SCROLLBAR, buf);
  } output_type_iterate_end;

  copy_chars_to_utf8_str(pstr, _("Celebrate"));
  text[O_LAST] = create_text_surf_from_utf8(pstr);
  FREEUTF8STR(pstr);

  /* Happy factor label */
  buf = create_iconlabel(NULL, pwindow->dst,
                         create_utf8_from_char_fonto("999", FONTO_DEFAULT),
                         (WF_FREE_STRING | WF_RESTORE_BACKGROUND));

  add_to_gui_list(ID_LABEL, buf);

  /* Happy factor scrollbar */
  buf = create_horizontal(current_theme->horiz, pwindow->dst, adj_size(30),
                          (WF_RESTORE_BACKGROUND));

  buf->action = factor_horiz_cma_callback;
  buf->data.ptr = &cma->edited_cm_parm.happy_factor;

  set_wstate(buf, FC_WS_NORMAL);

  add_to_gui_list(ID_SCROLLBAR, buf);

  /* celebrating */
  buf = create_checkbox(pwindow->dst,
                        cma->edited_cm_parm.require_happy,
                        WF_RESTORE_BACKGROUND);

  set_wstate(buf, FC_WS_NORMAL);
  buf->action = toggle_cma_celebrating_callback;
  add_to_gui_list(ID_CHECKBOX, buf);

  /* Save as ... */
  buf = create_themeicon(current_theme->save_icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND |WF_WIDGET_HAS_INFO_LABEL);
  buf->action = save_cma_callback;
  buf->info_label = create_utf8_from_char_fonto(_("Save settings as..."),
                                                FONTO_DEFAULT);

  add_to_gui_list(ID_ICON, buf);

  /* Load settings */
  buf = create_themeicon(current_theme->load_icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL);
  buf->action = load_cma_callback;
  buf->info_label = create_utf8_from_char_fonto(_("Load settings"),
                                                FONTO_DEFAULT);

  add_to_gui_list(ID_ICON, buf);

  /* Del settings */
  buf = create_themeicon(current_theme->delete_icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL);
  buf->action = del_cma_callback;
  buf->info_label = create_utf8_from_char_fonto(_("Delete settings"),
                                                FONTO_DEFAULT);

  add_to_gui_list(ID_ICON, buf);

  /* Run cma */
  buf = create_themeicon(current_theme->qprod_icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL);
  buf->action = run_cma_callback;
  buf->info_label = create_utf8_from_char_fonto(_("Control city"), FONTO_DEFAULT);

  add_to_gui_list(ID_ICON, buf);

  /* Run cma once */
  buf = create_themeicon(current_theme->find_city_icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL);
  buf->action = run_cma_once_callback;
  buf->info_label = create_utf8_from_char_fonto(_("Apply once"), FONTO_DEFAULT);

  add_to_gui_list(ID_ICON, buf);

  /* Del settings */
  buf = create_themeicon(current_theme->support_icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL);
  buf->action = stop_cma_callback;
  buf->info_label = create_utf8_from_char_fonto(_("Release city"), FONTO_DEFAULT);

  add_to_gui_list(ID_ICON, buf);

  /* -------------------------------- */
  cma->dlg->begin_widget_list = buf;

#ifdef GUI_SDL2_SMALL_SCREEN
  area.w = MAX(pcity_map->w + adj_size(220) + text_w + adj_size(10) +
               (pwindow->prev->prev->size.w + adj_size(5 + 70 + 5) +
                pwindow->prev->prev->size.w + adj_size(5 + 55 + 15)), area.w);
  area.h = adj_size(390) - (pwindow->size.w - pwindow->area.w);
#else  /* GUI_SDL2_SMALL_SCREEN */
  area.w = MAX(pcity_map->w + adj_size(150) + text_w + adj_size(10) +
               (pwindow->prev->prev->size.w + adj_size(5 + 70 + 5) +
                pwindow->prev->prev->size.w + adj_size(5 + 55 + 15)), area.w);
  area.h = adj_size(360) - (pwindow->size.w - pwindow->area.w);
#endif /* GUI_SDL2_SMALL_SCREEN */

  logo = theme_get_background(active_theme, BACKGROUND_CITYGOVDLG);
  if (resize_window(pwindow, logo, NULL,
                    (pwindow->size.w - pwindow->area.w) + area.w,
                    (pwindow->size.w - pwindow->area.w) + area.h)) {
    FREESURFACE(logo);
  }

#if 0
  logo = SDL_DisplayFormat(pwindow->theme);
  FREESURFACE(pwindow->theme);
  pwindow->theme = logo;
#endif /* 0 */

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  /* ---------- */
  dst.x = pcity_map->w + adj_size(80) +
    (pwindow->size.w - (pcity_map->w + adj_size(40)) -
     (text_w + adj_size(10) + pwindow->prev->prev->size.w + adj_size(5 + 70 + 5) +
      pwindow->prev->prev->size.w + adj_size(5 + 55))) / 2;

#ifdef GUI_SDL2_SMALL_SCREEN
  dst.x += 22;
#endif

  dst.y =  adj_size(75);

  x = area.x = dst.x - adj_size(10);
  area.y = dst.y - adj_size(20);
  w = area.w = adj_size(10) + text_w + adj_size(10) + pwindow->prev->prev->size.w + adj_size(5 + 70 + 5)
    + pwindow->prev->prev->size.w + adj_size(5 + 55 + 10);
  area.h = (O_LAST + 1) * (text[0]->h + adj_size(6)) + adj_size(20);
  fill_rect_alpha(pwindow->theme, &area, &bg_color);

  create_frame(pwindow->theme,
               area.x, area.y, area.w - 1, area.h - 1,
               get_theme_color(COLOR_THEME_CMA_FRAME));

  area.x = dst.x + text_w + adj_size(10);
  alphablit(minimal, NULL, pwindow->theme, &area, 255);
  area.x += minimal->w + adj_size(10);
  FREESURFACE(minimal);

  alphablit(factor, NULL, pwindow->theme, &area, 255);
  FREESURFACE(factor);

  area.x = pwindow->area.x + adj_size(22);
  area.y = pwindow->area.y + adj_size(31);
  alphablit(pcity_map, NULL, pwindow->theme, &area, 255);
  FREESURFACE(pcity_map);

  output_type_iterate(i) {
    /* min label */
    buf = buf->prev;
    buf->size.x = pwindow->size.x + dst.x + text_w + adj_size(10);
    buf->size.y = pwindow->size.y + dst.y + (text[i]->h - buf->size.h) / 2;

    /* min sb */
    buf = buf->prev;
    buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(5);
    buf->size.y = pwindow->size.y + dst.y + (text[i]->h - buf->size.h) / 2;

    area.x = buf->size.x - pwindow->size.x - adj_size(2);
    area.y = buf->size.y - pwindow->size.y;
    area.w = adj_size(74);
    area.h = buf->size.h;
    fill_rect_alpha(pwindow->theme, &area, &bg_color);

    create_frame(pwindow->theme,
                 area.x, area.y, area.w - 1, area.h - 1,
                 get_theme_color(COLOR_THEME_CMA_FRAME));

    /* factor label */
    buf = buf->prev;
    buf->size.x = buf->next->size.x + adj_size(75);
    buf->size.y = pwindow->size.y + dst.y + (text[i]->h - buf->size.h) / 2;

    /* factor sb */
    buf = buf->prev;
    buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(5);
    buf->size.y = pwindow->size.y + dst.y + (text[i]->h - buf->size.h) / 2;

    area.x = buf->size.x - pwindow->size.x - adj_size(2);
    area.y = buf->size.y - pwindow->size.y;
    area.w = adj_size(58);
    area.h = buf->size.h;
    fill_rect_alpha(pwindow->theme, &area, &bg_color);

    create_frame(pwindow->theme,
                 area.x, area.y, area.w - 1, area.h - 1,
                 get_theme_color(COLOR_THEME_CMA_FRAME));

    alphablit(text[i], NULL, pwindow->theme, &dst, 255);
    dst.y += text[i]->h + adj_size(6);
    FREESURFACE(text[i]);
  } output_type_iterate_end;

  /* happy factor label */
  buf = buf->prev;
  buf->size.x = buf->next->next->size.x;
  buf->size.y = pwindow->size.y + dst.y + (text[O_LAST]->h - buf->size.h) / 2;

  /* happy factor sb */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(5);
  buf->size.y = pwindow->size.y + dst.y + (text[O_LAST]->h - buf->size.h) / 2;

  area.x = buf->size.x - pwindow->size.x - adj_size(2);
  area.y = buf->size.y - pwindow->size.y;
  area.w = adj_size(58);
  area.h = buf->size.h;
  fill_rect_alpha(pwindow->theme, &area, &bg_color);

  create_frame(pwindow->theme,
               area.x, area.y, area.w - 1, area.h - 1,
               get_theme_color(COLOR_THEME_CMA_FRAME));

  /* Celebrate cbox */
  buf = buf->prev;
  buf->size.x = pwindow->size.x + dst.x + adj_size(10);
  buf->size.y = pwindow->size.y + dst.y;

  /* Celebrate static text */
  dst.x += (adj_size(10) + buf->size.w + adj_size(5));
  dst.y += (buf->size.h - text[O_LAST]->h) / 2;
  alphablit(text[O_LAST], NULL, pwindow->theme, &dst, 255);
  FREESURFACE(text[O_LAST]);
  /* ------------------------ */

  /* Save as */
  buf = buf->prev;
  buf->size.x = pwindow->size.x + x + (w - (buf->size.w + adj_size(6)) * 6) / 2;
  buf->size.y = pwindow->size.y + pwindow->size.h - buf->size.h * 2 - adj_size(10);

  area.x = x;
  area.y = buf->size.y - pwindow->size.y - adj_size(5);
  area.w = w;
  area.h = buf->size.h + adj_size(10);
  fill_rect_alpha(pwindow->theme, &area, &bg_color);

  create_frame(pwindow->theme,
               area.x, area.y, area.w - 1, area.h - 1,
               get_theme_color(COLOR_THEME_CMA_FRAME));

  /* Load */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(4);
  buf->size.y = buf->next->size.y;

  /* Del */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(4);
  buf->size.y = buf->next->size.y;

  /* Run */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(4);
  buf->size.y = buf->next->size.y;

  /* Run one time */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(4);
  buf->size.y = buf->next->size.y;

  /* Del */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(4);
  buf->size.y = buf->next->size.y;

  /* ------------------------ */
  /* Check if Citizen Icons style was loaded */
  cs = style_of_city(cma->pcity);

  if (cs != icons->style) {
    reload_citizens_icons(cs);
  }

  set_cma_hscrollbars();
  update_city_cma_dialog();
}

/**********************************************************************//**
  Close cma dialog
**************************************************************************/
void popdown_city_cma_dialog(void)
{
  if (cma) {
    popdown_window_group_dialog(cma->dlg->begin_widget_list,
                                cma->dlg->end_widget_list);
    FC_FREE(cma->dlg);
    if (cma->adv) {
      del_group_of_widgets_from_gui_list(cma->adv->begin_widget_list,
                                         cma->adv->end_widget_list);
      FC_FREE(cma->adv->scroll);
      FC_FREE(cma->adv);
    }
    if (city_dialog_is_open(cma->pcity)) {
      /* enable city dlg */
      enable_city_dlg_widgets();
      refresh_city_dialog(cma->pcity);
    }

    city_report_dialog_update();
    FC_FREE(cma);
  }
}
