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
#include "log.h"

/* common */
#include "game.h"

/* client */
#include "client_main.h"

/* gui-sdl2 */
#include "citydlg.h"
#include "cma_fe.h"
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapctrl.h"
#include "mapview.h"
#include "repodlgs.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"
#include "wldlg.h"

#include "cityrep.h"

static struct advanced_dialog *city_rep = NULL;

static void real_info_city_report_dialog_update(void);

/* ==================================================================== */

/**********************************************************************//**
  Close city report dialog.
**************************************************************************/
void city_report_dialog_popdown(void)
{
  if (city_rep) {
    popdown_window_group_dialog(city_rep->begin_widget_list,
                                city_rep->end_widget_list);
    FC_FREE(city_rep->scroll);
    FC_FREE(city_rep);

    enable_and_redraw_find_city_button();
    flush_dirty();
  }
}

/**********************************************************************//**
  User interacted with cityreport window.
**************************************************************************/
static int city_report_windows_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(city_rep->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with city report close button.
**************************************************************************/
static int exit_city_report_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    city_report_dialog_popdown();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with city button on city report.
**************************************************************************/
static int popup_citydlg_from_city_report_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_city_dialog(pwidget->data.city);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with worklist button on city report.
**************************************************************************/
static int popup_worklist_from_city_report_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_worklist_editor(pwidget->data.city, NULL);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with city production button on city report.
**************************************************************************/
static int popup_buy_production_from_city_report_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_hurry_production_dialog(pwidget->data.city, NULL);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with cma button on city report.
**************************************************************************/
static int popup_cma_from_city_report_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct city *pcity = game_city_by_number(MAX_ID - pwidget->id);

    /* state is changed before enter this function */
    if (!get_checkbox_state(pwidget)) {
      cma_release_city(pcity);
      city_report_dialog_update_city(pcity);
    } else {
      popup_city_cma_dialog(pcity);
    }
  }

  return -1;
}

#if 0
/**********************************************************************//**
  User interacted with information report button.
**************************************************************************/
static int info_city_report_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    set_wstate(pwidget, FC_WS_NORMAL);
    selected_widget = NULL;
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    real_info_city_report_dialog_update();
  }

  return -1;
}
#endif /* 0 */

#define COL	17

/**********************************************************************//**
  Rebuild the city info report.
**************************************************************************/
static void real_info_city_report_dialog_update(void)
{
  SDL_Color bg_color = {255, 255, 255, 136};

  struct widget *pbuf = NULL;
  struct widget *pwindow, *last;
  utf8_str *pstr;
  SDL_Surface *text1, *text2, *text3, *units_icon, *cma_icon, *text4;
  SDL_Surface *logo;
  int togrow, w = 0 , count, ww = 0, hh = 0, name_w = 0, prod_w = 0, h;
  char cbuf[128];
  const char *name;
  SDL_Rect dst;
  SDL_Rect area;

  if (city_rep) {
    popdown_window_group_dialog(city_rep->begin_widget_list,
                                city_rep->end_widget_list);
  } else {
    city_rep = fc_calloc(1, sizeof(struct advanced_dialog));
  }

  fc_snprintf(cbuf, sizeof(cbuf), _("size"));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= SF_CENTER;
  text1 = create_text_surf_from_utf8(pstr);

  fc_snprintf(cbuf, sizeof(cbuf), _("time\nto grow"));
  copy_chars_to_utf8_str(pstr, cbuf);
  text2 = create_text_surf_from_utf8(pstr);

  fc_snprintf(cbuf, sizeof(cbuf), _("City Name"));
  copy_chars_to_utf8_str(pstr, cbuf);
  text3 = create_text_surf_from_utf8(pstr);
  name_w = text3->w + adj_size(6);

  fc_snprintf(cbuf, sizeof(cbuf), _("Production"));
  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYREP_TEXT);
  text4 = create_text_surf_from_utf8(pstr);
  prod_w = text4->w;
  FREEUTF8STR(pstr);

  units_icon = create_icon_from_theme(current_theme->units_icon, 0);
  cma_icon = create_icon_from_theme(current_theme->cma_icon, 0);

  /* --------------- */
  pstr = create_utf8_from_char_fonto(_("Cities Report"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);
  city_rep->end_widget_list = pwindow;
  set_wstate(pwindow, FC_WS_NORMAL);
  pwindow->action = city_report_windows_callback;

  add_to_gui_list(ID_WINDOW, pwindow);

  area = pwindow->area;

  /* ------------------------- */
  /* Exit button */
  pbuf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                          WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  pbuf->info_label = create_utf8_from_char_fonto(_("Close Dialog"),
                                                 FONTO_ATTENTION);
  pbuf->action = exit_city_report_callback;
  set_wstate(pbuf, FC_WS_NORMAL);
  pbuf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_BUTTON, pbuf);

/* FIXME: Not implemented yet */
#if 0
  /* ------------------------- */
  pbuf = create_themeicon(current_theme->info_icon, pwindow->dst,
                          WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  pbuf->info_label = create_utf8_from_char_fonto(_("Information Report"),
                                                 FONTO_ATTENTION);
/*
  pbuf->action = info_city_report_callback;
  set_wstate(pbuf, FC_WS_NORMAL);
*/
  add_to_gui_list(ID_BUTTON, pbuf);
  /* -------- */
  pbuf = create_themeicon(current_theme->happy_icon, pwindow->dst,
                          WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  pbuf->info_label = create_utf8_from_char_fonto(_("Happiness Report"),
                                                 FONTO_ATTENTION);
/*
  pbuf->action = happy_city_report_callback;
  set_wstate(pbuf, FC_WS_NORMAL);
*/
  add_to_gui_list(ID_BUTTON, pbuf);
  /* -------- */
  pbuf = create_themeicon(current_theme->army_icon, pwindow->dst,
                          WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);

  pbuf->info_label = create_utf8_from_char_fonto(_("Garrison Report"),
                                                 FONTO_ATTENTION);
/*
  pbuf->action = army_city_dlg_callback;
  set_wstate(pbuf, FC_WS_NORMAL);
*/
  add_to_gui_list(ID_BUTTON, pbuf);
  /* -------- */
  pbuf = create_themeicon(current_theme->support_icon, pwindow->dst,
                          WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  pbuf->info_label = create_utf8_from_char_fonto(_("Maintenance Report"),
                                                 FONTO_ATTENTION);
/*
  pbuf->action = supported_unit_city_dlg_callback;
  set_wstate(pbuf, FC_WS_NORMAL);
*/
  add_to_gui_list(ID_BUTTON, pbuf);
  /* ------------------------ */
#endif /* 0 */

  last = pbuf;
  count = 0;
  city_list_iterate(client.conn.playing->cities, pcity) {
    pstr = create_utf8_from_char_fonto(city_name_get(pcity), FONTO_ATTENTION);
    pstr->style |= TTF_STYLE_BOLD;
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            (WF_RESTORE_BACKGROUND | WF_SELECT_WITHOUT_BAR));

    if (city_unhappy(pcity)) {
      pbuf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_TRADE);
    } else {
      if (city_celebrating(pcity)) {
        pbuf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_CELEB);
      } else {
        if (city_happy(pcity)) {
          pbuf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_HAPPY);
        }
      }
    }

    pbuf->action = popup_citydlg_from_city_report_callback;
    set_wstate(pbuf, FC_WS_NORMAL);
    pbuf->data.city = pcity;
    if (count > 13 * COL) {
      set_wflag(pbuf , WF_HIDDEN);
    }
    hh = pbuf->size.h;
    name_w = MAX(pbuf->size.w, name_w);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d", city_size_get(pcity));
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = text1->w + adj_size(8);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    pbuf = create_checkbox(pwindow->dst,
                           cma_is_city_under_agent(pcity, NULL),
                           WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    fc_assert(MAX_ID > pcity->id);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);
    set_wstate(pbuf, FC_WS_NORMAL);
    pbuf->action = popup_cma_from_city_report_callback;

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d",
                pcity->prod[O_FOOD] - pcity->surplus[O_FOOD]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_game_color(COLOR_OVERVIEW_LAND);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_food->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->surplus[O_FOOD]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_FOOD_SURPLUS);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_food_corr->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    togrow = city_turns_to_grow(pcity);
    switch (togrow) {
    case 0:
      fc_snprintf(cbuf, sizeof(cbuf), "#");
      break;
    case FC_INFINITY:
      fc_snprintf(cbuf, sizeof(cbuf), "--");
      break;
    default:
      fc_snprintf(cbuf, sizeof(cbuf), "%d", togrow);
      break;
    }

    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    if (togrow < 0) {
      pstr->fgcol.r = 255;
    }
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = text2->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->surplus[O_TRADE]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_TRADE);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_trade->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->waste[O_TRADE]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_trade_corr->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->surplus[O_GOLD]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_GOLD);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_coin->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->prod[O_SCIENCE]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_SCIENCE);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_colb->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->prod[O_LUXURY]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_LUX);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_luxury->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d",
                pcity->prod[O_SHIELD] + pcity->waste[O_SHIELD]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_PROD);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_shield->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->waste[O_SHIELD]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_shield_corr->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d",
                pcity->prod[O_SHIELD] + pcity->waste[O_SHIELD] - pcity->surplus[O_SHIELD]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_SUPPORT);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = units_icon->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->surplus[O_SHIELD]);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_TRADE);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            WF_RESTORE_BACKGROUND);
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    pbuf->size.w = icons->big_shield_surplus->w + adj_size(6);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);

    /* ----------- */
    if (VUT_UTYPE == pcity->production.kind) {
      const struct unit_type *punittype = pcity->production.value.utype;

      logo = resize_surface_box(get_unittype_surface(punittype,
                                                     direction8_invalid()),
                               adj_size(36), adj_size(24), 1,
                               TRUE, TRUE);
      togrow = utype_build_shield_cost(pcity, NULL, punittype);
      name = utype_name_translation(punittype);
    } else {
      const struct impr_type *pimprove = pcity->production.value.building;

      logo = resize_surface_box(get_building_surface(pcity->production.value.building),
                               adj_size(36), adj_size(24), 1,
                               TRUE, TRUE);
      togrow = impr_build_shield_cost(pcity, pimprove);
      name = improvement_name_translation(pimprove);
    }

    if (!worklist_is_empty(&(pcity->worklist))) {
      dst.x = logo->w - icons->worklist->w;
      dst.y = 0;
      alphablit(icons->worklist, NULL, logo, &dst, 255);
      fc_snprintf(cbuf, sizeof(cbuf), "%s\n(%d/%d)\n%s",
                  name, pcity->shield_stock, togrow, _("worklist"));
    } else {
      fc_snprintf(cbuf, sizeof(cbuf), "%s\n(%d/%d)%s",
                  name, pcity->shield_stock, togrow,
                  pcity->shield_stock > togrow ? _("\nfinished"): "" );
    }

    /* Info string */
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;

    togrow = city_production_turns_to_build(pcity, TRUE);
    if (togrow == 999) {
      fc_snprintf(cbuf, sizeof(cbuf), "%s", _("never"));
    } else {
      fc_snprintf(cbuf, sizeof(cbuf), "%d %s",
                  togrow, PL_("turn", "turns", togrow));
    }

    pbuf = create_icon2(logo, pwindow->dst,
                        WF_WIDGET_HAS_INFO_LABEL |WF_RESTORE_BACKGROUND
                        | WF_FREE_THEME);
    pbuf->info_label = pstr;
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);
    set_wstate(pbuf, FC_WS_NORMAL);
    pbuf->action = popup_worklist_from_city_report_callback;
    pbuf->data.city = pcity;

    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYREP_TEXT);
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
                            (WF_SELECT_WITHOUT_BAR | WF_RESTORE_BACKGROUND));
    if (count > 13 * COL) {
      set_wflag(pbuf, WF_HIDDEN);
    }
    hh = MAX(hh, pbuf->size.h);
    prod_w = MAX(prod_w, pbuf->size.w);
    add_to_gui_list(MAX_ID - pcity->id, pbuf);
    pbuf->data.city = pcity;
    pbuf->action = popup_buy_production_from_city_report_callback;
    if (city_can_buy(pcity)) {
      set_wstate(pbuf, FC_WS_NORMAL);
    }

    count += COL;
  } city_list_iterate_end;

  h = hh;
  city_rep->begin_widget_list = pbuf;

  /* Setup window width */
  area.w = name_w + adj_size(6) + text1->w + adj_size(8) + cma_icon->w
    + (icons->big_food->w + adj_size(6)) * 10 + text2->w + adj_size(6)
    + units_icon->w + adj_size(6) + prod_w + adj_size(170);

  if (count) {
    city_rep->end_active_widget_list = last->prev;
    city_rep->begin_active_widget_list = city_rep->begin_widget_list;
    if (count > 14 * COL) {
      city_rep->active_widget_list = city_rep->end_active_widget_list;
      if (city_rep->scroll) {
        city_rep->scroll->count = count;
      }
      ww = create_vertical_scrollbar(city_rep, COL, 14, TRUE, TRUE);
      area.w += ww;
      area.h = 14 * (hh + adj_size(2));
    } else {
      area.h = (count / COL) * (hh + adj_size(2));
    }
  }

  area.h += text2->h + adj_size(6);
  area.w += adj_size(2);

  logo = theme_get_background(active_theme, BACKGROUND_CITYREP);
  resize_window(pwindow, logo, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);
  FREESURFACE(logo);

#if 0
  logo = SDL_DisplayFormat(pwindow->theme);
  FREESURFACE(pwindow->theme);
  pwindow->theme = logo;
  logo = NULL;
#endif /* 0 */

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* Exit button */
  pbuf = pwindow->prev;
  pbuf->size.x = area.x + area.w - pbuf->size.w - 1;
  pbuf->size.y = pwindow->size.y + adj_size(2);

/* FIXME: not implemented yet */
#if 0
  /* Info button */
  pbuf = pbuf->prev;
  pbuf->size.x = area.x + area.w - pbuf->size.w - adj_size(5);
  pbuf->size.y = area.y + area.h - pbuf->size.h - adj_size(5);

  /* Happy button */
  pbuf = pbuf->prev;
  pbuf->size.x = pbuf->next->size.x - adj_size(5) - pbuf->size.w;
  pbuf->size.y = pbuf->next->size.y;

  /* Army button */
  pbuf = pbuf->prev;
  pbuf->size.x = pbuf->next->size.x - adj_size(5) - pbuf->size.w;
  pbuf->size.y = pbuf->next->size.y;

  /* Supported button */
  pbuf = pbuf->prev;
  pbuf->size.x = pbuf->next->size.x - adj_size(5) - pbuf->size.w;
  pbuf->size.y = pbuf->next->size.y;
#endif /* 0 */

  /* Cities background and labels */
  dst.x = area.x + adj_size(2);
  dst.y = area.y + 1;
  dst.w = (name_w + adj_size(6)) + (text1->w + adj_size(8)) + adj_size(5);
  dst.h = area.h - adj_size(2);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color);

  create_frame(pwindow->theme,
               dst.x , dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_CITYREP_FRAME));

  dst.y += (text2->h - text3->h) / 2;
  dst.x += ((name_w + adj_size(6)) - text3->w) / 2;
  alphablit(text3, NULL, pwindow->theme, &dst, 255);
  FREESURFACE(text3);

  /* City size background and label */
  dst.x = area.x + adj_size(5) + name_w + adj_size(5 + 4);
  alphablit(text1, NULL, pwindow->theme, &dst, 255);
  ww = text1->w;
  FREESURFACE(text1);

  /* CMA icon */
  dst.x += (ww + adj_size(9));
  dst.y = area.y + 1 + (text2->h - cma_icon->h) / 2;
  alphablit(cma_icon, NULL, pwindow->theme, &dst, 255);
  ww = cma_icon->w;
  FREESURFACE(cma_icon);

  /* -------------- */
  /* Populations food upkeep background and label */
  dst.x += (ww + 1);
  dst.y = area.y + 1;
  w = dst.x + adj_size(2);
  dst.w = (icons->big_food->w + adj_size(6)) + adj_size(10)
    + (icons->big_food_surplus->w + adj_size(6)) + adj_size(10)
    + text2->w + adj_size(6 + 2);
  dst.h = area.h - adj_size(2);
  fill_rect_alpha(pwindow->theme, &dst,
                  get_theme_color(COLOR_THEME_CITYREP_FOODSTOCK));

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_CITYREP_FRAME));

  dst.y = area.y + 1 + (text2->h - icons->big_food->h) / 2;
  dst.x += adj_size(5);
  alphablit(icons->big_food, NULL, pwindow->theme, &dst, 255);

  /* Food surplus icon */
  w += (icons->big_food->w + adj_size(6)) + adj_size(10);
  dst.x = w + adj_size(3);
  alphablit(icons->big_food_surplus, NULL, pwindow->theme, &dst, 255);

  /* To grow label */
  w += (icons->big_food_surplus->w + adj_size(6)) + adj_size(10);
  dst.x = w + adj_size(3);
  dst.y = area.y + 1;
  alphablit(text2, NULL, pwindow->theme, &dst, 255);
  hh = text2->h;
  ww = text2->w;
  FREESURFACE(text2);
  /* -------------- */

  /* Trade, corruptions, gold, science, luxury income background and label */
  dst.x = w + (ww + adj_size(8));
  dst.y = area.y + 1;
  w = dst.x + adj_size(2);
  dst.w = (icons->big_trade->w + adj_size(6)) + adj_size(10) +
          (icons->big_trade_corr->w + adj_size(6)) + adj_size(10) +
          (icons->big_coin->w + adj_size(6)) + adj_size(10) +
          (icons->big_colb->w + adj_size(6)) + adj_size(10) +
          (icons->big_luxury->w + adj_size(6)) + adj_size(4);
  dst.h = area.h - adj_size(2);

  fill_rect_alpha(pwindow->theme, &dst,
                  get_theme_color(COLOR_THEME_CITYREP_TRADE));

  create_frame(pwindow->theme,
               dst.x , dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_CITYREP_FRAME));

  dst.y = area.y + 1 + (hh - icons->big_trade->h) / 2;
  dst.x += adj_size(5);
  alphablit(icons->big_trade, NULL, pwindow->theme, &dst, 255);

  w += (icons->big_trade->w + adj_size(6)) + adj_size(10);
  dst.x = w + adj_size(3);
  alphablit(icons->big_trade_corr, NULL, pwindow->theme, &dst, 255);

  w += (icons->big_food_corr->w + adj_size(6)) + adj_size(10);
  dst.x = w + adj_size(3);
  alphablit(icons->big_coin, NULL, pwindow->theme, &dst, 255);

  w += (icons->big_coin->w + adj_size(6)) + adj_size(10);
  dst.x = w + adj_size(3);
  alphablit(icons->big_colb, NULL, pwindow->theme, &dst, 255);

  w += (icons->big_colb->w + adj_size(6)) + adj_size(10);
  dst.x = w + adj_size(3);
  alphablit(icons->big_luxury, NULL, pwindow->theme, &dst, 255);
  /* --------------------- */

  /* Total productions, waste, support, shields surplus background and label */
  w += (icons->big_luxury->w + adj_size(6)) + adj_size(4);
  dst.x = w;
  w += adj_size(2);
  dst.y = area.y + 1;
  dst.w = (icons->big_shield->w + adj_size(6)) + adj_size(10) +
          (icons->big_shield_corr->w + adj_size(6)) + adj_size(10) +
          (units_icon->w + adj_size(6)) + adj_size(10) +
          (icons->big_shield_surplus->w + adj_size(6)) + adj_size(4);
  dst.h = area.h - adj_size(2);

  fill_rect_alpha(pwindow->theme, &dst,
                  get_theme_color(COLOR_THEME_CITYREP_PROD));

  create_frame(pwindow->theme,
               dst.x , dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_CITYREP_FRAME));

  dst.y = area.y + 1 + (hh - icons->big_shield->h) / 2;
  dst.x += adj_size(5);
  alphablit(icons->big_shield, NULL, pwindow->theme, &dst, 255);

  w += (icons->big_shield->w + adj_size(6)) + adj_size(10);
  dst.x = w + adj_size(3);
  alphablit(icons->big_shield_corr, NULL, pwindow->theme, &dst, 255);

  w += (icons->big_shield_corr->w + adj_size(6)) + adj_size(10);
  dst.x = w + adj_size(3);
  dst.y = area.y + 1 + (hh - units_icon->h) / 2;
  alphablit(units_icon, NULL, pwindow->theme, &dst, 255);

  w += (units_icon->w + adj_size(6)) + adj_size(10);
  FREESURFACE(units_icon);
  dst.x = w + adj_size(3);
  dst.y = area.y + 1 + (hh - icons->big_shield_surplus->h) / 2;
  alphablit(icons->big_shield_surplus, NULL, pwindow->theme, &dst, 255);
  /* ------------------------------- */

  w += (icons->big_shield_surplus->w + adj_size(6)) + adj_size(10);
  dst.x = w;
  dst.y = area.y + 1;
  dst.w = adj_size(36) + adj_size(5) + prod_w;
  dst.h = hh + adj_size(2);

  fill_rect_alpha(pwindow->theme, &dst,
                  get_theme_color(COLOR_THEME_CITYREP_PROD));

  create_frame(pwindow->theme,
               dst.x , dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_CITYREP_FRAME));

  dst.y = area.y + 1 + (hh - text4->h) / 2;
  dst.x += (dst.w - text4->w) / 2;
  alphablit(text4, NULL, pwindow->theme, &dst, 255);
  FREESURFACE(text4);

  if (count) {
    int start_x = area.x + adj_size(5);
    int start_y = area.y + hh + adj_size(3);

    h += adj_size(2);
    pbuf = pbuf->prev;

    while (TRUE) {

      /* City name */
      pbuf->size.x = start_x;
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;
      pbuf->size.w = name_w;

      /* City size */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(5);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* CMA */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(6);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Food cons. */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(6);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Food surplus */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Time to grow */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Trade */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(5);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Trade corruptions */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Net gold income */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Science income */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Luxuries income */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Total production */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(6);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Waste */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Units support */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Production surplus */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Currently build */
      /* Icon */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(10);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;

      /* Label */
      pbuf = pbuf->prev;
      pbuf->size.x = pbuf->next->size.x + pbuf->next->size.w + adj_size(5);
      pbuf->size.y = start_y + (h - pbuf->size.h) / 2;
      pbuf->size.w = prod_w;

      start_y += h;
      if (pbuf == city_rep->begin_active_widget_list) {
        break;
      }
      pbuf = pbuf->prev;
    }

    if (city_rep->scroll) {
      setup_vertical_scrollbar_area(city_rep->scroll,
                                    area.x + area.w, area.y,
                                    area.h, TRUE);
    }

  }

  /* ----------------------------------- */
  redraw_group(city_rep->begin_widget_list, pwindow, 0);
  widget_mark_dirty(pwindow);
  flush_dirty();
}

/**********************************************************************//**
  Update city information in city report.
**************************************************************************/
static struct widget *real_city_report_dialog_update_city(struct widget *pwidget,
                                                          struct city *pcity)
{
  char cbuf[64];
  const char *name;
  int togrow;
  SDL_Surface *logo;
  SDL_Rect dst;

  /* city name status */
  if (city_unhappy(pcity)) {
    pwidget->string_utf8->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_TRADE);
  } else {
    if (city_celebrating(pcity)) {
      pwidget->string_utf8->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_CELEB);
    } else {
      if (city_happy(pcity)) {
        pwidget->string_utf8->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_HAPPY);
      }
    }
  }

  /* city size */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", city_size_get(pcity));
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* cma check box */
  pwidget = pwidget->prev;
  if (cma_is_city_under_agent(pcity, NULL) != get_checkbox_state(pwidget)) {
    toggle_checkbox(pwidget);
  }

  /* food consumptions */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d",
              pcity->prod[O_FOOD] - pcity->surplus[O_FOOD]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* food surplus */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->surplus[O_FOOD]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* time to grow */
  pwidget = pwidget->prev;
  togrow = city_turns_to_grow(pcity);
  switch (togrow) {
  case 0:
    fc_snprintf(cbuf, sizeof(cbuf), "#");
    break;
  case FC_INFINITY:
    fc_snprintf(cbuf, sizeof(cbuf), "--");
    break;
  default:
    fc_snprintf(cbuf, sizeof(cbuf), "%d", togrow);
    break;
  }
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  if (togrow < 0) {
    pwidget->string_utf8->fgcol.r = 255;
  } else {
    pwidget->string_utf8->fgcol.r = 0;
  }

  /* trade production */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->surplus[O_TRADE]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* corruptions */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->waste[O_TRADE]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* gold surplus */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->surplus[O_GOLD]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* science income */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->prod[O_SCIENCE]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* lugury income */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->prod[O_LUXURY]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* total production */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d",
              pcity->prod[O_SHIELD] + pcity->waste[O_SHIELD]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* waste */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->waste[O_SHIELD]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* units support */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->prod[O_SHIELD] +
              pcity->waste[O_SHIELD] - pcity->surplus[O_SHIELD]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* production income */
  pwidget = pwidget->prev;
  fc_snprintf(cbuf, sizeof(cbuf), "%d", pcity->surplus[O_SHIELD]);
  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  /* change production */
  if (VUT_UTYPE == pcity->production.kind) {
    const struct unit_type *punittype = pcity->production.value.utype;

    logo = resize_surface(get_unittype_surface(punittype, direction8_invalid()),
                          adj_size(36), adj_size(24), 1);
    togrow = utype_build_shield_cost(pcity, NULL, punittype);
    name = utype_name_translation(punittype);
  } else {
    const struct impr_type *pimprove = pcity->production.value.building;

    logo = resize_surface(get_building_surface(pcity->production.value.building),
                          adj_size(36), adj_size(24), 1);
    togrow = impr_build_shield_cost(pcity, pimprove);
    name = improvement_name_translation(pimprove);
  }

  if (!worklist_is_empty(&(pcity->worklist))) {
    dst.x = logo->w - icons->worklist->w;
    dst.y = 0;
    alphablit(icons->worklist, NULL, logo, &dst, 255);
    fc_snprintf(cbuf, sizeof(cbuf), "%s\n(%d/%d)\n%s",
                name, pcity->shield_stock, togrow, _("worklist"));
  } else {
    fc_snprintf(cbuf, sizeof(cbuf), "%s\n(%d/%d)",
                name, pcity->shield_stock, togrow);
  }

  pwidget = pwidget->prev;
  copy_chars_to_utf8_str(pwidget->info_label, cbuf);
  FREESURFACE(pwidget->theme);
  pwidget->theme = logo;

  /* hurry productions */
  pwidget = pwidget->prev;
  togrow = city_production_turns_to_build(pcity, TRUE);
  if (togrow == 999) {
    fc_snprintf(cbuf, sizeof(cbuf), "%s", _("never"));
  } else {
    fc_snprintf(cbuf, sizeof(cbuf), "%d %s",
                togrow, PL_("turn", "turns", togrow));
  }

  copy_chars_to_utf8_str(pwidget->string_utf8, cbuf);

  return pwidget->prev;
}

/* ======================================================================== */

/**********************************************************************//**
  Check if city report is open.
**************************************************************************/
bool is_city_report_open(void)
{
  return (city_rep != NULL);
}

/**********************************************************************//**
  Pop up or brings forward the city report dialog.  It may or may not
  be modal.
**************************************************************************/
void city_report_dialog_popup(bool make_modal)
{
  if (!city_rep) {
    real_info_city_report_dialog_update();
  }
}

/**********************************************************************//**
  Update (refresh) the entire city report dialog.
**************************************************************************/
void real_city_report_dialog_update(void *unused)
{
  if (city_rep) {
    struct widget *pwidget;
    int count;

    /* find if the lists are identical (if not then rebuild all) */
    pwidget = city_rep->end_active_widget_list; /* name of first city */
    city_list_iterate(client.conn.playing->cities, pcity) {
      if (pcity->id == MAX_ID - pwidget->id) {
        count = COL;

        while (count) {
          count--;
          pwidget = pwidget->prev;
        }
      } else {
        real_info_city_report_dialog_update();

        return;
      }
    } city_list_iterate_end;

    /* check it there are some city widgets left on list */
    if (pwidget && pwidget->next != city_rep->begin_active_widget_list) {
      real_info_city_report_dialog_update();
      return;
    }

    /* update widget city list (widget list is the same that city list) */
    pwidget = city_rep->end_active_widget_list;
    city_list_iterate(client.conn.playing->cities, pcity) {
      pwidget = real_city_report_dialog_update_city(pwidget, pcity);
    } city_list_iterate_end;

    /* -------------------------------------- */
    redraw_group(city_rep->begin_widget_list, city_rep->end_widget_list, 0);
    widget_mark_dirty(city_rep->end_widget_list);

    flush_dirty();
  }
}

/**********************************************************************//**
  Update the city report dialog for a single city.
**************************************************************************/
void real_city_report_update_city(struct city *pcity)
{
  if (city_rep && pcity) {
    struct widget *buf = city_rep->end_active_widget_list;

    while (pcity->id != MAX_ID - buf->id
           && buf != city_rep->begin_active_widget_list) {
      buf = buf->prev;
    }

    if (buf == city_rep->begin_active_widget_list) {
      real_info_city_report_dialog_update();
      return;
    }
    real_city_report_dialog_update_city(buf, pcity);

    /* -------------------------------------- */
    redraw_group(city_rep->begin_widget_list, city_rep->end_widget_list, 0);
    widget_mark_dirty(city_rep->end_widget_list);

    flush_dirty();
  }
}

/**********************************************************************//**
  After a selection rectangle is defined, make the cities that
  are hilited on the canvas exclusively hilited in the
  City List window.
**************************************************************************/
void hilite_cities_from_canvas(void)
{
  log_debug("hilite_cities_from_canvas : PORT ME");
}

/**********************************************************************//**
  Toggle a city's hilited status.
**************************************************************************/
void toggle_city_hilite(struct city *pcity, bool on_off)
{
  log_debug("toggle_city_hilite : PORT ME");
}
