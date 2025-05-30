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
                          citydlg.c  -  description
                             -------------------
    begin                : Wed Sep 04 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* SDL3 */
#include <SDL3/SDL.h>

/* utility */
#include "astring.h"
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"

/* common */
#include "game.h"
#include "movement.h"
#include "specialist.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "control.h"
#include "text.h"

/* gui-sdl3 */
#include "cityrep.h"
#include "cma_fe.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "menu.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"
#include "wldlg.h"

#include "citydlg.h"

/* ============================================================= */

static struct city_dialog {
  struct city *pcity;

  enum {
    INFO_PAGE = 0,
    HAPPINESS_PAGE = 1,
    ARMY_PAGE,
    SUPPORTED_UNITS_PAGE,
    MISC_PAGE
  } page;

  /* main window group list */
  struct widget *begin_city_widget_list;
  struct widget *end_city_widget_list;

  /* Imprvm. vscrollbar */
  struct advanced_dialog *imprv;

  /* Penel group list */
  struct advanced_dialog *panel;

  /* Menu imprv. dlg. */
  struct widget *begin_city_menu_widget_list;
  struct widget *end_city_menu_widget_list;

  /* shortcuts */
  struct widget *add_point;
  struct widget *buy_button;
  struct widget *resource_map;
  struct widget *city_name_edit;

  int citizen_step;

  SDL_Rect spec_area;

  bool lock;
} *pcity_dlg = NULL;

enum specialist_type {
  SP_ELVIS, SP_SCIENTIST, SP_TAXMAN, SP_LAST
};

static float city_map_zoom = 1;

static struct small_dialog *hurry_prog_dlg = NULL;

static void popdown_hurry_production_dialog(void);
static void disable_city_dlg_widgets(void);
static void redraw_city_dialog(struct city *pcity);
static void rebuild_imprm_list(struct city *pcity);
static void rebuild_citydlg_title_str(struct widget *pwindow, struct city *pcity);

/* ======================================================================= */

/**********************************************************************//**
  Return first building that has given effect.

  FIXME: Some callers would work better if they got building actually
         provides the effect at the moment, and not just first potential
         one.
**************************************************************************/
struct impr_type *get_building_for_effect(enum effect_type effect_type)
{
  improvement_iterate(pimprove) {
    if (building_has_effect(pimprove, effect_type)) {
      return pimprove;
    }
  } improvement_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Destroy City Menu Dlg but not undraw.
**************************************************************************/
static void popdown_city_menu_dlg(bool enable_city_dialog_widgets)
{
  if (pcity_dlg->end_city_menu_widget_list) {
    popdown_window_group_dialog(pcity_dlg->begin_city_menu_widget_list,
                                pcity_dlg->end_city_menu_widget_list);
    pcity_dlg->end_city_menu_widget_list = NULL;
  }
  if (enable_city_dialog_widgets) {
    /* enable city dlg */
    enable_city_dlg_widgets();
  }
}

/**********************************************************************//**
  Destroy City Dlg
**************************************************************************/
static void del_city_dialog(void)
{
  if (pcity_dlg) {
    if (pcity_dlg->imprv->end_widget_list) {
      del_group_of_widgets_from_gui_list(pcity_dlg->imprv->begin_widget_list,
                                         pcity_dlg->imprv->end_widget_list);
    }
    FC_FREE(pcity_dlg->imprv->scroll);
    FC_FREE(pcity_dlg->imprv);

    if (pcity_dlg->panel) {
      del_group_of_widgets_from_gui_list(pcity_dlg->panel->begin_widget_list,
                                         pcity_dlg->panel->end_widget_list);
      FC_FREE(pcity_dlg->panel->scroll);
      FC_FREE(pcity_dlg->panel);
    }

    if (hurry_prog_dlg) {
      del_group_of_widgets_from_gui_list(hurry_prog_dlg->begin_widget_list,
                                         hurry_prog_dlg->end_widget_list);

      FC_FREE(hurry_prog_dlg);
    }

    free_city_units_lists();
    popdown_city_menu_dlg(FALSE);

    popdown_window_group_dialog(pcity_dlg->begin_city_widget_list,
                                pcity_dlg->end_city_widget_list);
    FC_FREE(pcity_dlg);
  }
}

/**********************************************************************//**
  Main City Dlg. window callback.
  This implements specialist change ( Elvis, Taxman, Scientist )
**************************************************************************/
static int city_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (!cma_is_city_under_agent(pcity_dlg->pcity, NULL)
        && city_owner(pcity_dlg->pcity) == client.conn.playing) {

      if (is_in_rect_area(main_data.event.motion.x, main_data.event.motion.y,
                          &(pcity_dlg->spec_area))) {
        city_rotate_specialist(pcity_dlg->pcity,
                               (main_data.event.motion.x - pcity_dlg->spec_area.x)
                               / pcity_dlg->citizen_step);

        return -1;
      }
    }

    if (!pcity_dlg->lock) {
      if (pcity_dlg->panel) {
        select_window_group_dialog(pcity_dlg->begin_city_widget_list, pwindow);
        select_window_group_dialog(pcity_dlg->panel->begin_widget_list,
                                   pcity_dlg->panel->end_widget_list);
        widget_flush(pwindow);
      } else {
        if (select_window_group_dialog(pcity_dlg->begin_city_widget_list, pwindow)) {
          widget_flush(pwindow);
        }
      }
    }
  }

  return -1;
}

/* ===================================================================== */
/* ========================== Units Orders Menu ======================== */
/* ===================================================================== */

/**********************************************************************//**
  Popdown unit city orders menu.
**************************************************************************/
static int cancel_units_orders_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_window_group_dialog(pcity_dlg->begin_city_menu_widget_list,
                                pcity_dlg->end_city_menu_widget_list);
    pcity_dlg->end_city_menu_widget_list = NULL;

    /* Enable city dlg */
    enable_city_dlg_widgets();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Activate unit and del unit order dlg. widget group.
**************************************************************************/
static int activate_units_orders_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = button->data.unit;

    popdown_city_menu_dlg(TRUE);
    if (punit) {
      unit_focus_set(punit);
    }
  }

  return -1;
}

/**********************************************************************//**
  Activate unit and popdow city dlg. + center on unit.
**************************************************************************/
static int activate_and_exit_units_orders_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = button->data.unit;

    if (punit) {
      popdown_window_group_dialog(pcity_dlg->begin_city_menu_widget_list,
                                  pcity_dlg->end_city_menu_widget_list);
      pcity_dlg->end_city_menu_widget_list = NULL;

      popdown_city_dialog(pcity_dlg->pcity);

      center_tile_mapcanvas(unit_tile(punit));
      unit_focus_set(punit);
    }
  }

  return -1;
}

/**********************************************************************//**
  Sentry unit and del unit order dlg. widget group.
**************************************************************************/
static int sentry_units_orders_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = button->data.unit;

    popdown_city_menu_dlg(TRUE);
    if (punit) {
      request_unit_sentry(punit);
    }
  }

  return -1;
}

/**********************************************************************//**
  Fortify unit and del unit order dlg. widget group.
**************************************************************************/
static int fortify_units_orders_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = button->data.unit;

    popdown_city_menu_dlg(TRUE);
    if (punit) {
      request_unit_fortify(punit);
    }
  }

  return -1;
}

/**********************************************************************//**
  Disband unit and del unit order dlg. widget group.
**************************************************************************/
static int disband_units_orders_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = button->data.unit;

    popdown_city_menu_dlg(TRUE);
    popup_unit_disband_dlg(punit, TRUE);
  }

  return -1;
}

/**********************************************************************//**
  Homecity unit and del unit order dlg. widget group.
**************************************************************************/
static int homecity_units_orders_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = button->data.unit;

    popdown_city_menu_dlg(TRUE);
    if (punit) {
      request_unit_change_homecity(punit);
    }
  }

  return -1;
}

/**********************************************************************//**
  Upgrade unit and del unit order dlg. widget group.
**************************************************************************/
static int upgrade_units_orders_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = button->data.unit;

    popdown_city_menu_dlg(TRUE);
    popup_unit_upgrade_dlg(punit, TRUE);
  }

  return -1;
}

/**********************************************************************//**
  Main unit order dlg. callback.
**************************************************************************/
static int units_orders_dlg_callback(struct widget *button)
{
  return -1;
}

/**********************************************************************//**
  Popup units orders menu.
**************************************************************************/
static int units_orders_city_dlg_callback(struct widget *button)
{
  bool right_button = (main_data.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                       && main_data.event.button.button == SDL_BUTTON_RIGHT);

  if (PRESSED_EVENT(main_data.event) || right_button) {
    utf8_str *pstr;
    char cbuf[80];
    struct widget *buf, *pwindow;
    struct unit *punit;
    const struct unit_type *putype;
    Uint16 i = 0, hh = 0;
    SDL_Rect area;

    punit = player_unit_by_number(client_player(), MAX_ID - button->id);

    if (punit == NULL || !can_client_issue_orders()) {
      return -1;
    }

    if (right_button) {
      popdown_city_dialog(pcity_dlg->pcity);
      center_tile_mapcanvas(unit_tile(punit));
      unit_focus_set(punit);

      return -1;
    }

    /* Disable city dlg */
    unselect_widget_action();
    disable_city_dlg_widgets();

    putype = unit_type_get(punit);

    /* Window */
    fc_snprintf(cbuf, sizeof(cbuf), "%s:", _("Unit commands"));
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
    pstr->style |= TTF_STYLE_BOLD;
    pwindow = create_window_skeleton(NULL, pstr, 0);

    pwindow->action = units_orders_dlg_callback;
    set_wstate(pwindow, FC_WS_NORMAL);
    add_to_gui_list(ID_REVOLUTION_DLG_WINDOW, pwindow);
    pcity_dlg->end_city_menu_widget_list = pwindow;

    area = pwindow->area;

    /* Unit description */
    fc_snprintf(cbuf, sizeof(cbuf), "%s", unit_description(punit));
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    buf = create_iconlabel(adj_surf(get_unittype_surface(putype, punit->facing)),
                            pwindow->dst, pstr, WF_FREE_THEME);
    area.w = MAX(area.w, buf->size.w);
    add_to_gui_list(ID_LABEL, buf);

    /* Activate unit */
    buf = create_icon_button_from_chars_fonto(NULL, pwindow->dst,
                                              _("Activate unit"),
                                              FONTO_ATTENTION, 0);
    i++;
    area.w = MAX(area.w, buf->size.w);
    hh = MAX(hh, buf->size.h);
    buf->action = activate_units_orders_city_dlg_callback;
    buf->data = button->data;
    set_wstate(buf, FC_WS_NORMAL);
    add_to_gui_list(button->id, buf);

    /* Activate unit, close dlg. */
    buf = create_icon_button_from_chars_fonto(NULL, pwindow->dst,
                                              _("Activate unit, close dialog"),
                                              FONTO_ATTENTION, 0);
    i++;
    area.w = MAX(area.w, buf->size.w);
    hh = MAX(hh, buf->size.h);
    buf->action = activate_and_exit_units_orders_city_dlg_callback;
    buf->data = button->data;
    set_wstate(buf, FC_WS_NORMAL);
    add_to_gui_list(button->id, buf);
    /* ----- */

    if (pcity_dlg->page == ARMY_PAGE) {
      /* Sentry unit */
      buf = create_icon_button_from_chars_fonto(NULL, pwindow->dst,
                                                _("Sentry unit"),
                                                FONTO_ATTENTION, 0);
      i++;
      area.w = MAX(area.w, buf->size.w);
      hh = MAX(hh, buf->size.h);
      buf->data = button->data;
      buf->action = sentry_units_orders_city_dlg_callback;
      if (punit->activity != ACTIVITY_SENTRY
          && can_unit_do_activity_client(punit, ACTIVITY_SENTRY)) {
        set_wstate(buf, FC_WS_NORMAL);
      }
      add_to_gui_list(button->id, buf);
      /* ----- */

      /* Fortify unit */
      buf = create_icon_button_from_chars_fonto(NULL, pwindow->dst,
                                                _("Fortify unit"),
                                                FONTO_ATTENTION, 0);
      i++;
      area.w = MAX(area.w, buf->size.w);
      hh = MAX(hh, buf->size.h);
      buf->data = button->data;
      buf->action = fortify_units_orders_city_dlg_callback;
      if (punit->activity != ACTIVITY_FORTIFYING
          && can_unit_do_activity_client(punit, ACTIVITY_FORTIFYING)) {
        set_wstate(buf, FC_WS_NORMAL);
      }
      add_to_gui_list(button->id, buf);
    }
    /* ----- */

    /* Disband unit */
    buf = create_icon_button_from_chars_fonto(NULL, pwindow->dst,
                                              _("Disband unit"),
                                              FONTO_ATTENTION, 0);
    i++;
    area.w = MAX(area.w, buf->size.w);
    hh = MAX(hh, buf->size.h);
    buf->data = button->data;
    buf->action = disband_units_orders_city_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    add_to_gui_list(button->id, buf);
    /* ----- */

    if (pcity_dlg->page == ARMY_PAGE) {
      if (punit->homecity != pcity_dlg->pcity->id) {
        /* Make new Homecity */
        buf = create_icon_button_from_chars_fonto(NULL, pwindow->dst,
            action_id_name_translation(ACTION_HOME_CITY),
                                            FONTO_ATTENTION, 0);
        i++;
        area.w = MAX(area.w, buf->size.w);
        hh = MAX(hh, buf->size.h);
        buf->data = button->data;
        buf->action = homecity_units_orders_city_dlg_callback;
        set_wstate(buf, FC_WS_NORMAL);
        add_to_gui_list(button->id, buf);
      }
      /* ----- */

      if (can_upgrade_unittype(client.conn.playing, putype)) {
        /* Upgrade unit */
        buf = create_icon_button_from_chars_fonto(NULL, pwindow->dst,
                                                  _("Upgrade unit"),
                                                  FONTO_ATTENTION, 0);
        i++;
        area.w = MAX(area.w, buf->size.w);
        hh = MAX(hh, buf->size.h);
        buf->data = button->data;
        buf->action = upgrade_units_orders_city_dlg_callback;
        set_wstate(buf, FC_WS_NORMAL);
        add_to_gui_list(button->id, buf);
      }
    }

    /* ----- */
    /* Cancel */
    buf = create_icon_button_from_chars_fonto(NULL, pwindow->dst,
                                              _("Cancel"),
                                              FONTO_ATTENTION, 0);
    i++;
    area.w = MAX(area.w, buf->size.w);
    hh = MAX(hh, buf->size.h);
    buf->key = SDLK_ESCAPE;
    buf->action = cancel_units_orders_city_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    add_to_gui_list(button->id, buf);
    pcity_dlg->begin_city_menu_widget_list = buf;

    /* ================================================== */
    unselect_widget_action();
    /* ================================================== */

    area.w += adj_size(10);
    hh += adj_size(4);

    /* Create window background */
    resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                  (pwindow->size.w - pwindow->area.w) + area.w,
                  (pwindow->size.h - pwindow->area.h) + pwindow->prev->size.h +
                  (i * hh) + adj_size(5));

    area = pwindow->area;

    widget_set_position(pwindow,
                        button->size.x + adj_size(2),
                        pwindow->area.y + button->size.y + 1);

    /* Label */
    buf = pwindow->prev;
    buf->size.w = area.w;
    buf->size.x = area.x;
    buf->size.y = area.y + 1;
    buf = buf->prev;

    /* First button */
    buf->size.w = area.w;
    buf->size.h = hh;
    buf->size.x = area.x;
    buf->size.y = buf->next->size.y + buf->next->size.h + adj_size(5);
    buf = buf->prev;

    while (buf != NULL) {
      buf->size.w = area.w;
      buf->size.h = hh;
      buf->size.x = buf->next->size.x;
      buf->size.y = buf->next->size.y + buf->next->size.h;
      if (buf == pcity_dlg->begin_city_menu_widget_list) {
        break;
      }
      buf = buf->prev;
    }

    /* ================================================== */
    /* Redraw */
    redraw_group(pcity_dlg->begin_city_menu_widget_list, pwindow, 0);
    widget_flush(pwindow);
  }

  return -1;
}

/* ======================================================================= */
/* ======================= City Dlg. Panels ============================== */
/* ======================================================================= */

/**********************************************************************//**
  Create unit icon with support icons.
**************************************************************************/
static SDL_Surface *create_unit_surface(struct unit *punit, bool support,
                                        int w, int h)
{
  SDL_Rect src_rect;
  SDL_Surface *psurf;
  struct canvas *destcanvas;
  const struct civ_map *nmap = &(wld.map);

  destcanvas = canvas_create(tileset_full_tile_width(tileset),
                             tileset_unit_with_small_upkeep_height(tileset));

  put_unit(punit, destcanvas, 1.0, 0, 0);

  /* Get unit sprite width, and crop top. Bottom might get restored in 'support'
   * case below. */
  get_smaller_surface_rect(destcanvas->surf, &src_rect);

  if (support) {
    int i, step;
    int free_unhappy;
    int happy_cost;
    SDL_Rect dest;
    int offset = tileset_unit_layout_small_offset_y(tileset);

    /* Support also layouts placing support icons higher than unit. */
    src_rect.y = MIN(src_rect.y, offset);
    /* Restore bottom space when needed for support icons. */
    src_rect.h = destcanvas->surf->h - src_rect.y;

    free_unhappy = get_city_bonus(pcity_dlg->pcity, EFT_MAKE_CONTENT_MIL);
    happy_cost = city_unit_unhappiness(nmap, punit, &free_unhappy);

    i = punit->upkeep[O_SHIELD] + punit->upkeep[O_FOOD] +
        punit->upkeep[O_GOLD] + happy_cost;

    if (i * icons->food->w > src_rect.w / 2) {
      step = (src_rect.w / 2 - icons->food->w) / (i - 1);
    } else {
      step = icons->food->w;
    }

    dest.y = tileset_unit_layout_small_offset_y(tileset);
    dest.x = src_rect.x + src_rect.w / 8;

    for (i = 0; i < punit->upkeep[O_SHIELD]; i++) {
      alphablit(icons->shield, NULL, destcanvas->surf, &dest, 255);
      dest.x += step;
    }

    for (i = 0; i < punit->upkeep[O_FOOD]; i++) {
      alphablit(icons->food, NULL, destcanvas->surf, &dest, 255);
      dest.x += step;
    }

    for (i = 0; i < punit->upkeep[O_GOLD]; i++) {
      alphablit(icons->coint, NULL, destcanvas->surf, &dest, 255);
      dest.x += step;
    }

    for (i = 0; i < happy_cost; i++) {
      alphablit(icons->face, NULL, destcanvas->surf, &dest, 255);
      dest.x += step;
    }

  }

  psurf = create_surf(src_rect.w, src_rect.h);
  alphablit(destcanvas->surf, &src_rect, psurf, NULL, 255);

  canvas_free(destcanvas);

  if (w != src_rect.w || h != src_rect.h) {
    SDL_Surface *pzoomed;

    pzoomed = resize_surface_box(psurf, w, h, 1, TRUE, TRUE);
    FREESURFACE(psurf);
    psurf = pzoomed;
  }

  return psurf;
}

/**********************************************************************//**
  Create present/supported units widget list
  207 pixels is panel width in city dlg.
  220 - max y position pixel position belong to panel area.
**************************************************************************/
static void create_present_supported_units_widget_list(struct unit_list *units)
{
  int i;
  struct widget *buf = NULL;
  struct widget *end = NULL;
  struct widget *pwindow = pcity_dlg->end_city_widget_list;
  struct city *home_city;
  const struct unit_type *putype;
  SDL_Surface *surf;
  utf8_str *pstr;
  char cbuf[256];
  int num_x, num_y, w, h;

  i = 0;

  num_x = (adj_size(160) / (tileset_full_tile_width(tileset) + adj_size(4)));
  if (num_x < 4) {
    num_x = 4;
    w = adj_size(160 - 4*4) / 4;
  } else {
    w = tileset_full_tile_width(tileset) + (adj_size(160) % (tileset_full_tile_width(tileset) + 4)) / num_x;
  }

  num_y = (adj_size(151) / (tileset_full_tile_height(tileset)+4));
  if (num_y < 4) {
    num_y = 4;
    h = adj_size(151 - 4*4) / 4;
  } else {
    h = tileset_full_tile_height(tileset) + (adj_size(151) % (tileset_full_tile_height(tileset)+4)) / num_y;
  }

  unit_list_iterate(units, punit) {
    const char *vetname;
    struct astring addition = ASTRING_INIT;

    putype = unit_type_get(punit);
    vetname = utype_veteran_name_translation(putype, punit->veteran);
    home_city = game_city_by_number(punit->homecity);
    unit_activity_astr(punit, &addition);
    fc_snprintf(cbuf, sizeof(cbuf), "%s (%d,%d,%s)%s%s\n%s\n(%d/%d)\n%s",
                utype_name_translation(putype),
                putype->attack_strength,
                putype->defense_strength,
                move_points_text(putype->move_rate, FALSE),
                (vetname != NULL ? "\n" : ""),
                (vetname != NULL ? vetname : ""),
                astr_str(&addition),
                punit->hp, putype->hp,
                home_city ? home_city->name : Q_("?homecity:None"));
    astr_free(&addition);

    if (pcity_dlg->page == SUPPORTED_UNITS_PAGE) {
      int city_near_dist;
      struct city *near_city = get_nearest_city(punit, &city_near_dist);

      sz_strlcat(cbuf, "\n");
      sz_strlcat(cbuf, get_nearest_city_text(near_city, city_near_dist));
      surf = adj_surf(create_unit_surface(punit, TRUE, w, h));
    } else {
      surf = adj_surf(create_unit_surface(punit, FALSE, w, h));
    }

    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;

    buf = create_icon2(surf, pwindow->dst, WF_FREE_THEME
                        | WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL);
    buf->info_label = pstr;
    buf->data.unit = punit;
    add_to_gui_list(MAX_ID - punit->id, buf);

    if (!end) {
      end = buf;
    }

    if (++i > num_x * num_y) {
      set_wflag(buf, WF_HIDDEN);
    }

    if (city_owner(pcity_dlg->pcity) == client.conn.playing) {
      set_wstate(buf, FC_WS_NORMAL);
    }

    buf->action = units_orders_city_dlg_callback;
  } unit_list_iterate_end;

  pcity_dlg->panel = fc_calloc(1, sizeof(struct advanced_dialog));
  pcity_dlg->panel->end_widget_list = end;
  pcity_dlg->panel->begin_widget_list = buf;
  pcity_dlg->panel->end_active_widget_list = pcity_dlg->panel->end_widget_list;
  pcity_dlg->panel->begin_active_widget_list = pcity_dlg->panel->begin_widget_list;
  pcity_dlg->panel->active_widget_list = pcity_dlg->panel->end_active_widget_list;

  setup_vertical_widgets_position(num_x,
                                  pwindow->area.x + adj_size(5),
                                  pwindow->area.y + adj_size(44),
                                  0, 0, pcity_dlg->panel->begin_active_widget_list,
                                  pcity_dlg->panel->end_active_widget_list);

  if (i > num_x * num_y) {
    create_vertical_scrollbar(pcity_dlg->panel,
                              num_x, num_y, TRUE, TRUE);

    setup_vertical_scrollbar_area(pcity_dlg->panel->scroll,
                                  pwindow->area.x + adj_size(185),
                                  pwindow->area.y + adj_size(45),
                                  adj_size(150), TRUE);
  }
}

/**********************************************************************//**
  Free city present/supported units panel list.
**************************************************************************/
void free_city_units_lists(void)
{
  if (pcity_dlg && pcity_dlg->panel) {
    del_group_of_widgets_from_gui_list(pcity_dlg->panel->begin_widget_list,
                                       pcity_dlg->panel->end_widget_list);
    FC_FREE(pcity_dlg->panel->scroll);
    FC_FREE(pcity_dlg->panel);
  }
}

/**********************************************************************//**
  Change to present units panel.
**************************************************************************/
static int army_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pcity_dlg->page != ARMY_PAGE) {
      free_city_units_lists();
      pcity_dlg->page = ARMY_PAGE;
      redraw_city_dialog(pcity_dlg->pcity);
      flush_dirty();
    } else {
      widget_redraw(button);
      widget_flush(button);
    }
  }

  return -1;
}

/**********************************************************************//**
  Change to supported units panel.
**************************************************************************/
static int supported_unit_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pcity_dlg->page != SUPPORTED_UNITS_PAGE) {
      free_city_units_lists();
      pcity_dlg->page = SUPPORTED_UNITS_PAGE;
      redraw_city_dialog(pcity_dlg->pcity);
      flush_dirty();
    } else {
      widget_redraw(button);
      widget_flush(button);
    }
  }

  return -1;
}

/* ---------------------- */

/**********************************************************************//**
  Change to info panel.
**************************************************************************/
static int info_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pcity_dlg->page != INFO_PAGE) {
      free_city_units_lists();
      pcity_dlg->page = INFO_PAGE;
      redraw_city_dialog(pcity_dlg->pcity);
      flush_dirty();
    } else {
      widget_redraw(button);
      widget_flush(button);
    }
  }

  return -1;
}

/* ---------------------- */
/**********************************************************************//**
  Change to happines panel.
**************************************************************************/
static int happy_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pcity_dlg->page != HAPPINESS_PAGE) {
      free_city_units_lists();
      pcity_dlg->page = HAPPINESS_PAGE;
      redraw_city_dialog(pcity_dlg->pcity);
      flush_dirty();
    } else {
      widget_redraw(button);
      widget_flush(button);
    }
  }

  return -1;
}

/**********************************************************************//**
  City option callback
**************************************************************************/
static int misc_panel_city_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    bv_city_options new_options = pcity_dlg->pcity->city_options;

    switch (MAX_ID - pwidget->id) {
    case 0x10:
      if (BV_ISSET(new_options, CITYO_DISBAND)) {
        BV_CLR(new_options, CITYO_DISBAND);
      } else {
        BV_SET(new_options, CITYO_DISBAND);
      }
      break;
    case 0x20:
      if (BV_ISSET(new_options, CITYO_SCIENCE_SPECIALISTS)) {
        BV_CLR(new_options, CITYO_SCIENCE_SPECIALISTS);
      } else {
        BV_SET(new_options, CITYO_SCIENCE_SPECIALISTS);
      }
      if (BV_ISSET(new_options, CITYO_GOLD_SPECIALISTS)) {
        BV_CLR(new_options, CITYO_GOLD_SPECIALISTS);
      } else {
        BV_SET(new_options, CITYO_GOLD_SPECIALISTS);
      }

      pwidget->theme2 = get_tax_surface(O_GOLD);
      pwidget->id = MAX_ID - 0x40;
      widget_redraw(pwidget);
      widget_flush(pwidget);
      break;
    case 0x40:
      BV_CLR(new_options, CITYO_SCIENCE_SPECIALISTS);
      BV_CLR(new_options, CITYO_GOLD_SPECIALISTS);
      pwidget->theme2 = get_tax_surface(O_LUXURY);
      pwidget->id = MAX_ID - 0x60;
      widget_redraw(pwidget);
      widget_flush(pwidget);
      break;
    case 0x60:
      if (BV_ISSET(new_options, CITYO_SCIENCE_SPECIALISTS)) {
        BV_CLR(new_options, CITYO_SCIENCE_SPECIALISTS);
      } else {
        BV_SET(new_options, CITYO_SCIENCE_SPECIALISTS);
      }

      pwidget->theme2 = get_tax_surface(O_SCIENCE);
      pwidget->id = MAX_ID - 0x20;
      widget_redraw(pwidget);
      widget_flush(pwidget);
      break;
    }

    dsend_packet_city_options_req(&client.conn, pcity_dlg->pcity->id,
                                  new_options, pcity_dlg->pcity->wlcb);
  }

  return -1;
}

/**********************************************************************//**
  Create city options widgets.
**************************************************************************/
static void create_city_options_widget_list(struct city *pcity)
{
  struct widget *buf, *pwindow = pcity_dlg->end_city_widget_list;
  SDL_Surface *surf;
  utf8_str *pstr;
  char cbuf[80];

  fc_snprintf(cbuf, sizeof(cbuf),
              _("Allow unit production\nto disband city"));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= TTF_STYLE_BOLD;
  pstr->fgcol = *get_theme_color(COLOR_THEME_CHECKBOX_LABEL_TEXT);

  buf =
    create_textcheckbox(pwindow->dst,
                        BV_ISSET(pcity->city_options, CITYO_DISBAND),
                        pstr, WF_RESTORE_BACKGROUND);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = misc_panel_city_dlg_callback;
  add_to_gui_list(MAX_ID - 0x10, buf);
  buf->size.x = pwindow->area.x + adj_size(7);
  buf->size.y = pwindow->area.y + adj_size(45);

  /* ----- */

  pcity_dlg->panel = fc_calloc(1, sizeof(struct advanced_dialog));
  pcity_dlg->panel->end_widget_list = buf;

  /* ----- */

  fc_snprintf(cbuf, sizeof(cbuf), "%s:", _("New citizens produce"));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT_PLUS);
  pstr->style |= SF_CENTER;

  if (BV_ISSET(pcity->city_options, CITYO_SCIENCE_SPECIALISTS)) {
    surf = get_tax_surface(O_SCIENCE);
    buf = create_icon_button(surf, pwindow->dst, pstr,
                             WF_ICON_CENTER_RIGHT | WF_FREE_THEME2);
    add_to_gui_list(MAX_ID - 0x20, buf);
  } else {
    if (BV_ISSET(pcity->city_options, CITYO_GOLD_SPECIALISTS)) {
      surf = get_tax_surface(O_GOLD);
      buf = create_icon_button(surf, pwindow->dst,
                               pstr, WF_ICON_CENTER_RIGHT | WF_FREE_THEME2);
      add_to_gui_list(MAX_ID - 0x40, buf);
    } else {
      surf = get_tax_surface(O_LUXURY);
      buf = create_icon_button(surf, pwindow->dst,
                               pstr, WF_ICON_CENTER_RIGHT | WF_FREE_THEME2);
      add_to_gui_list(MAX_ID - 0x60, buf);
    }
  }

  buf->size.w = adj_size(177);
  buf->action = misc_panel_city_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);

  buf->size.x = buf->next->size.x;
  buf->size.y = buf->next->size.y + buf->next->size.h + adj_size(5);
  pcity_dlg->panel->begin_widget_list = buf;
}

/**********************************************************************//**
  Change to city options panel.
**************************************************************************/
static int options_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pcity_dlg->page != MISC_PAGE) {
      free_city_units_lists();
      pcity_dlg->page = MISC_PAGE;
      redraw_city_dialog(pcity_dlg->pcity);
      flush_dirty();
    } else {
      widget_redraw(button);
      widget_flush(button);
    }
  }

  return -1;
}

/* ======================================================================= */

/**********************************************************************//**
  User interacted with Citizen Governor button.
**************************************************************************/
static int cma_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    disable_city_dlg_widgets();
    popup_city_cma_dialog(pcity_dlg->pcity);
  }

  return -1;
}

/**********************************************************************//**
  Exit city dialog.
**************************************************************************/
static int exit_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_city_dialog(pcity_dlg->pcity);
  }

  return -1;
}

/* ======================================================================= */
/* ======================== Buy Production Dlg. ========================== */
/* ======================================================================= */

/**********************************************************************//**
  Popdown buy productions dlg.
**************************************************************************/
static int cancel_buy_prod_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_hurry_production_dialog();

    if (pcity_dlg) {
      /* enable city dlg */
      enable_city_dlg_widgets();
    }
  }

  return -1;
}

/**********************************************************************//**
  Buy productions.
**************************************************************************/
static int ok_buy_prod_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct city *pcity = button->data.city;    /* Save it. */

    popdown_hurry_production_dialog();
    city_buy_production(pcity);

    if (pcity_dlg) {
      /* enable city dlg */
      enable_city_dlg_widgets();

      /* disable buy button */
      set_wstate(pcity_dlg->buy_button, FC_WS_DISABLED);
      widget_redraw(pcity_dlg->buy_button);
      widget_mark_dirty(pcity_dlg->buy_button);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  Popup buy productions dlg.
**************************************************************************/
static int buy_prod_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(button);
    widget_flush(button);
    disable_city_dlg_widgets();
    popup_hurry_production_dialog(pcity_dlg->pcity, button->dst->surface);
  }

  return -1;
}

/**********************************************************************//**
  Popup buy productions dlg.
**************************************************************************/
static void popdown_hurry_production_dialog(void)
{
  if (hurry_prog_dlg) {
    popdown_window_group_dialog(hurry_prog_dlg->begin_widget_list,
                                hurry_prog_dlg->end_widget_list);
    FC_FREE(hurry_prog_dlg);
    flush_dirty();
  }
}

/**********************************************************************//**
  Main hurry productions dlg. callback
**************************************************************************/
static int hurry_production_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(hurry_prog_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  Popup buy production dlg.
**************************************************************************/
void popup_hurry_production_dialog(struct city *pcity, SDL_Surface *pdest)
{
  char tbuf[512], cbuf[512];
  struct widget *buf = NULL, *pwindow;
  utf8_str *pstr;
  SDL_Surface *text;
  SDL_Rect dst;
  int window_x = 0, window_y = 0;
  SDL_Rect area;
  const char *name = city_production_name_translation(pcity);
  int value = pcity->client.buy_cost;

  if (hurry_prog_dlg) {
    return;
  }

  fc_snprintf(tbuf, ARRAY_SIZE(tbuf), PL_("Treasury contains %d gold.",
                                          "Treasury contains %d gold.",
                                          client_player()->economic.gold),
              client_player()->economic.gold);

  hurry_prog_dlg = fc_calloc(1, sizeof(struct small_dialog));

  if (city_can_buy(pcity)) {
    if (value <= client_player()->economic.gold) {
      fc_snprintf(cbuf, sizeof(cbuf),
                  /* TRANS: Last %s is pre-pluralised "Treasury contains %d gold." */
                  PL_("Buy %s for %d gold?\n%s",
                      "Buy %s for %d gold?\n%s", value),
                  name, value, tbuf);
    } else {
      fc_snprintf(cbuf, sizeof(cbuf),
                  /* TRANS: Last %s is pre-pluralised "Treasury contains %d gold." */
                  PL_("%s costs %d gold.\n%s",
                      "%s costs %d gold.\n%s", value),
                  name, value, tbuf);
    }
  } else {
    if (pcity->did_buy) {
      fc_snprintf(cbuf, sizeof(cbuf),
                  _("Sorry, you have already bought here in this turn."));
    } else {
      fc_snprintf(cbuf, sizeof(cbuf),
                  _("Sorry, you can't buy here in this turn."));
    }
  }

  pstr = create_utf8_from_char_fonto(_("Buy it?"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;
  pwindow = create_window_skeleton(NULL, pstr, 0);
  pwindow->action = hurry_production_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);
  add_to_gui_list(ID_WINDOW, pwindow);

  hurry_prog_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  area.h += 1;

  /* ============================================================= */

  /* Label */
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_BUY);

  text = create_text_surf_from_utf8(pstr);
  FREEUTF8STR(pstr);
  area.w = MAX(area.w , text->w);
  area.h += text->h + adj_size(5);

  buf = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                                 pwindow->dst, _("No"),
                                                 FONTO_ATTENTION, 0);

  buf->action = cancel_buy_prod_city_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  area.h += buf->size.h;

  add_to_gui_list(ID_BUTTON, buf);

  if (city_can_buy(pcity) && (value <= client.conn.playing->economic.gold)) {
    buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                   pwindow->dst,
                                                   _("Yes"),
                                                   FONTO_ATTENTION, 0);

    buf->action = ok_buy_prod_city_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->data.city = pcity;
    buf->key = SDLK_RETURN;
    add_to_gui_list(ID_BUTTON, buf);
    buf->size.w = MAX(buf->next->size.w, buf->size.w);
    buf->next->size.w = buf->size.w;
    area.w = MAX(area.w , 2 * buf->size.w + adj_size(20));
  }

  hurry_prog_dlg->begin_widget_list = buf;

  /* Setup window size and start position */
  area.w += adj_size(10);
  area.h += adj_size(5);

  resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  if (city_dialog_is_open(pcity)) {
    window_x = pcity_dlg->buy_button->size.x;
    window_y = pcity_dlg->buy_button->size.y - pwindow->size.h;
  } else if (is_city_report_open()) {
    fc_assert(selected_widget != NULL);

    if (selected_widget->size.x + tileset_tile_width(tileset)
        + pwindow->size.w > main_window_width()) {
      window_x = selected_widget->size.x - pwindow->size.w;
    } else {
      window_x = selected_widget->size.x + tileset_tile_width(tileset);
    }

    window_y = selected_widget->size.y
      + (selected_widget->size.h - pwindow->size.h) / 2;

    if (window_y + pwindow->size.h > main_window_height()) {
      window_y = main_window_height() - pwindow->size.h - 1;
    } else if (window_y < 0) {
      window_y = 0;
    }
  } else {
    put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h, pcity->tile);
  }

  widget_set_position(pwindow, window_x, window_y);

  /* Setup rest of widgets */
  /* Label */
  dst.x = area.x + (area.w - text->w) / 2;
  dst.y = area.y + 1;
  alphablit(text, NULL, pwindow->theme, &dst, 255);
  dst.y += text->h + adj_size(5);
  FREESURFACE(text);

  /* No */
  buf = pwindow->prev;
  buf->size.y = dst.y;

  if (city_can_buy(pcity) && value <= client.conn.playing->economic.gold) {
    /* Yes */
    buf = buf->prev;
    buf->size.x = area.x + (area.w - (2 * buf->size.w + adj_size(20))) / 2;
    buf->size.y = dst.y;

    /* No */
    buf->next->size.x = buf->size.x + buf->size.w + adj_size(20);
  } else {
    /* No */
    buf->size.x = area.x + area.w - buf->size.w - adj_size(10);
  }

  /* ================================================== */
  /* Redraw */
  redraw_group(hurry_prog_dlg->begin_widget_list, pwindow, 0);
  widget_mark_dirty(pwindow);
  flush_dirty();
}

/* =======================================================================*/
/* ========================== CHANGE PRODUCTION ==========================*/
/* =======================================================================*/

/**********************************************************************//**
  Popup the change production dialog.
**************************************************************************/
static int change_prod_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(button);
    widget_flush(button);

    disable_city_dlg_widgets();
    popup_worklist_editor(pcity_dlg->pcity, NULL);
  }

  return -1;
}

/* =======================================================================*/
/* ========================== SELL IMPROVEMENTS ==========================*/
/* =======================================================================*/

/**********************************************************************//**
  Popdown Sell Imprv. Dlg. and exit without sell.
**************************************************************************/
static int sell_imprvm_dlg_cancel_callback(struct widget *cancel_button)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_window_group_dialog(pcity_dlg->begin_city_menu_widget_list,
                                pcity_dlg->end_city_menu_widget_list);
    pcity_dlg->end_city_menu_widget_list = NULL;
    enable_city_dlg_widgets();
    redraw_city_dialog(pcity_dlg->pcity);
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Popdown Sell Imprv. Dlg. and exit with sell.
**************************************************************************/
static int sell_imprvm_dlg_ok_callback(struct widget *ok_button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct widget *tmp = (struct widget *)ok_button->data.ptr;

    city_sell_improvement(pcity_dlg->pcity, MAX_ID - 3000 - tmp->id);

    /* popdown, we don't redraw and flush because this is made by redraw city dlg.
       when response from server comes */
    popdown_window_group_dialog(pcity_dlg->begin_city_menu_widget_list,
                                pcity_dlg->end_city_menu_widget_list);

    pcity_dlg->end_city_menu_widget_list = NULL;

    /* del imprv from widget list */
    del_widget_from_vertical_scroll_widget_list(pcity_dlg->imprv, tmp);

    enable_city_dlg_widgets();

    if (pcity_dlg->imprv->end_widget_list) {
      set_group_state(pcity_dlg->imprv->begin_active_widget_list,
                      pcity_dlg->imprv->end_active_widget_list, FC_WS_DISABLED);
    }

    redraw_city_dialog(pcity_dlg->pcity);
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Popup Sell Imprvm. Dlg.
**************************************************************************/
static int sell_imprvm_dlg_callback(struct widget *impr)
{
  if (PRESSED_EVENT(main_data.event)) {
    utf8_str *pstr = NULL;
    struct widget *label = NULL;
    struct widget *pwindow = NULL;
    struct widget *cancel_button = NULL;
    struct widget *ok_button = NULL;
    char cbuf[80];
    int id;
    SDL_Rect area;
    int price;

    unselect_widget_action();
    disable_city_dlg_widgets();

    pstr = create_utf8_from_char_fonto(_("Sell it?"), FONTO_ATTENTION);
    pstr->style |= TTF_STYLE_BOLD;
    pwindow = create_window_skeleton(NULL, pstr, 0);
    /*pwindow->action = move_sell_imprvm_dlg_callback; */
    /*set_wstate(pwindow, FC_WS_NORMAL); */
    add_to_gui_list(ID_WINDOW, pwindow);
    pcity_dlg->end_city_menu_widget_list = pwindow;

    area = pwindow->area;

    /* Create text label */
    id = MAX_ID - 3000 - impr->id;

    price = impr_sell_gold(improvement_by_number(id));
    fc_snprintf(cbuf, sizeof(cbuf), PL_("Sell %s for %d gold?",
                                        "Sell %s for %d gold?", price),
                city_improvement_name_translation(pcity_dlg->pcity,
                                                  improvement_by_number(id)),
                price);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_SELL);
    label = create_iconlabel(NULL, pwindow->dst, pstr, 0);
    add_to_gui_list(ID_LABEL, label);

    /* Create cancel button */
    cancel_button =
      create_themeicon_button_from_chars_fonto(current_theme->small_cancel_icon,
                                               pwindow->dst, _("Cancel"),
                                               FONTO_DEFAULT, 0);
    cancel_button->action = sell_imprvm_dlg_cancel_callback;
    cancel_button->key = SDLK_ESCAPE;
    set_wstate(cancel_button, FC_WS_NORMAL);
    add_to_gui_list(ID_BUTTON, cancel_button);

    /* create ok button */
    ok_button = create_themeicon_button_from_chars_fonto(current_theme->small_ok_icon,
                                                         pwindow->dst, _("Sell"),
                                                         FONTO_DEFAULT, 0);
    ok_button->data.ptr = (void *)impr;
    ok_button->size.w = cancel_button->size.w;
    ok_button->action = sell_imprvm_dlg_ok_callback;
    ok_button->key = SDLK_RETURN;
    set_wstate(ok_button, FC_WS_NORMAL);
    add_to_gui_list(ID_BUTTON, ok_button);

    pcity_dlg->begin_city_menu_widget_list = ok_button;

    /* correct sizes */
    if ((ok_button->size.w + cancel_button->size.w + adj_size(30)) >
        label->size.w + adj_size(20)) {
      area.w = MAX(area.w, ok_button->size.w + cancel_button->size.w + adj_size(30));
    } else {
      area.w = MAX(area.w, label->size.w + adj_size(20));
    }

    area.h = MAX(area.h, ok_button->size.h + label->size.h + adj_size(25));

    /* create window background */
    resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                  (pwindow->size.w - pwindow->area.w) + area.w,
                  (pwindow->size.h - pwindow->area.h) + area.h);

    area = pwindow->area;

    /* set start positions */
    widget_set_position(pwindow,
                        (main_window_width() - pwindow->size.w) / 2,
                        (main_window_height() - pwindow->size.h) / 2 + adj_size(10));

    ok_button->size.x = area.x + adj_size(10);
    ok_button->size.y = area.y + area.h - ok_button->size.h - adj_size(10);

    cancel_button->size.y = ok_button->size.y;
    cancel_button->size.x = area.x + area.w - cancel_button->size.w - adj_size(10);

    label->size.x = area.x;
    label->size.y = area.y + adj_size(4);
    label->size.w = area.w;

    /* redraw */
    redraw_group(pcity_dlg->begin_city_menu_widget_list,
                 pcity_dlg->end_city_menu_widget_list, 0);

    widget_mark_dirty(pwindow);
    flush_dirty();
  }

  return -1;
}
/* ====================================================================== */

/**********************************************************************//**
  Enable city dialog widgets that can be enabled.
**************************************************************************/
void enable_city_dlg_widgets(void)
{
  if (pcity_dlg) {
    set_group_state(pcity_dlg->begin_city_widget_list,
                    pcity_dlg->end_city_widget_list->prev, FC_WS_NORMAL);

    if (pcity_dlg->imprv->end_active_widget_list) {
      if (pcity_dlg->imprv->scroll) {
        set_wstate(pcity_dlg->imprv->scroll->pscroll_bar, FC_WS_NORMAL);       /* vscroll */
        set_wstate(pcity_dlg->imprv->scroll->up_left_button, FC_WS_NORMAL);    /* up */
        set_wstate(pcity_dlg->imprv->scroll->down_right_button, FC_WS_NORMAL); /* down */
      }

      /* There is common function test_player_sell_building_now(),
       * but we are not using it here, since we want to use set_group_state()
       * when possible */
      if (pcity_dlg->pcity->did_sell
          || pcity_dlg->pcity->owner != client.conn.playing) {
        set_group_state(pcity_dlg->imprv->begin_active_widget_list,
                        pcity_dlg->imprv->end_active_widget_list, FC_WS_DISABLED);
      } else {
        struct widget *tmp_widget = pcity_dlg->imprv->end_active_widget_list;

        while (TRUE) {
          struct impr_type *pimpr = improvement_by_number(MAX_ID - 3000 -
                                                          tmp_widget->id);

          if (!can_city_sell_building(pcity_dlg->pcity, pimpr)) {
            set_wstate(tmp_widget, FC_WS_DISABLED);
          } else {
            set_wstate(tmp_widget, FC_WS_NORMAL);
          }

          if (tmp_widget == pcity_dlg->imprv->begin_active_widget_list) {
            break;
          }

          tmp_widget = tmp_widget->prev;

        } /* while */
      }
    }

    if (!city_can_buy(pcity_dlg->pcity) && pcity_dlg->buy_button) {
      set_wstate(pcity_dlg->buy_button, FC_WS_DISABLED);
    }

    if (pcity_dlg->panel) {
      set_group_state(pcity_dlg->panel->begin_widget_list,
                      pcity_dlg->panel->end_widget_list, FC_WS_NORMAL);
    }

    if (cma_is_city_under_agent(pcity_dlg->pcity, NULL)) {
      set_wstate(pcity_dlg->resource_map, FC_WS_DISABLED);
    }

    pcity_dlg->lock = FALSE;
  }
}

/**********************************************************************//**
  Disable all city dialog widgets
**************************************************************************/
static void disable_city_dlg_widgets(void)
{
  if (pcity_dlg->panel) {
    set_group_state(pcity_dlg->panel->begin_widget_list,
                    pcity_dlg->panel->end_widget_list, FC_WS_DISABLED);
  }

  if (pcity_dlg->imprv->end_widget_list) {
    set_group_state(pcity_dlg->imprv->begin_widget_list,
                    pcity_dlg->imprv->end_widget_list, FC_WS_DISABLED);
  }

  set_group_state(pcity_dlg->begin_city_widget_list,
                  pcity_dlg->end_city_widget_list->prev, FC_WS_DISABLED);
  pcity_dlg->lock = TRUE;
}

/* ======================================================================== */

/**********************************************************************//**
  Return scaled city map.
**************************************************************************/
SDL_Surface *get_scaled_city_map(struct city *pcity)
{
  SDL_Surface *buf = create_city_map(pcity);

  city_map_zoom = ((buf->w * 159 > buf->h * 249) ?
                   (float)adj_size(249) / buf->w
                   : (float)adj_size(159) / buf->h);

  return zoomSurface(buf, city_map_zoom, city_map_zoom, 1);
}

/**********************************************************************//**
  City resource map: event callback
**************************************************************************/
static int resource_map_city_dlg_callback(struct widget *map)
{
  if (PRESSED_EVENT(main_data.event)) {
    int col, row;

    if (canvas_to_city_pos(&col, &row,
                           city_map_radius_sq_get(pcity_dlg->pcity),
      1 / city_map_zoom * (main_data.event.motion.x - map->dst->dest_rect.x
                           - map->size.x),
      1 / city_map_zoom * (main_data.event.motion.y - map->dst->dest_rect.y
                           - map->size.y))) {

      city_toggle_worker(pcity_dlg->pcity, col, row);
    }
  }

  return -1;
}

/* ====================================================================== */

/**********************************************************************//**
  Helper for switch_city_callback.
**************************************************************************/
static int city_comp_by_turn_founded(const void *a, const void *b)
{
  struct city *pcity1 = *((struct city **) a);
  struct city *pcity2 = *((struct city **) b);

  return pcity1->turn_founded - pcity2->turn_founded;
}

/**********************************************************************//**
  Callback for next/prev city button
**************************************************************************/
static int next_prev_city_dlg_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct city **array;
    int i, dir, non_open_size;
    int size = city_list_size(client.conn.playing->cities);

    fc_assert_ret_val(size >= 1, -1);
    fc_assert_ret_val(city_owner(pcity_dlg->pcity)
                      == client.conn.playing, -1);

    if (size == 1) {
      return -1;
    }

    /* dir = 1 will advance to the city, dir = -1 will get previous */
    if (button->id == ID_CITY_DLG_NEXT_BUTTON) {
      dir = 1;
    } else {
      if (button->id == ID_CITY_DLG_PREV_BUTTON) {
        dir = -1;
      } else {
        /* Always fails. */
        fc_assert_ret_val(button->id == ID_CITY_DLG_NEXT_BUTTON
                          || button->id != ID_CITY_DLG_PREV_BUTTON, -1);
        dir = 1;
      }
    }

    array = fc_calloc(1, size * sizeof(struct city *));

    non_open_size = 0;
    for (i = 0; i < size; i++) {
      array[non_open_size++] = city_list_get(client.conn.playing->cities, i);
    }

    fc_assert_ret_val(non_open_size > 0, -1);

    if (non_open_size <= 1) {
      FC_FREE(array);
      return -1;
    }

    qsort(array, non_open_size, sizeof(struct city *),
          city_comp_by_turn_founded);

    for (i = 0; i < non_open_size; i++) {
      if (pcity_dlg->pcity == array[i]) {
        break;
      }
    }

    fc_assert_ret_val(i < non_open_size, -1);
    pcity_dlg->pcity = array[(i + dir + non_open_size) % non_open_size];
    FC_FREE(array);

    /* free panel widgets */
    free_city_units_lists();
    /* refresh resource map */
    FREESURFACE(pcity_dlg->resource_map->theme);
    pcity_dlg->resource_map->theme = get_scaled_city_map(pcity_dlg->pcity);
    rebuild_imprm_list(pcity_dlg->pcity);

    /* redraw */
    redraw_city_dialog(pcity_dlg->pcity);
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  New city name given for renaming it.
**************************************************************************/
static int new_name_city_dlg_callback(struct widget *pedit)
{
  if (pedit->string_utf8->text != NULL) {
    if (strcmp(pedit->string_utf8->text, city_name_get(pcity_dlg->pcity))) {
      sdl3_client_flags |= CF_CHANGED_CITY_NAME;
      city_rename(pcity_dlg->pcity, pedit->string_utf8->text);
    }
  } else {
    /* Empty input -> restore previous content */
    copy_chars_to_utf8_str(pedit->string_utf8, city_name_get(pcity_dlg->pcity));
    widget_redraw(pedit);
    widget_mark_dirty(pedit);
    flush_dirty();
  }

  return -1;
}

/* ======================================================================= */
/* ======================== Redrawing City Dlg. ========================== */
/* ======================================================================= */

/**********************************************************************//**
  Refresh (update) the city names for the dialog
**************************************************************************/
static void refresh_city_names(struct city *pcity)
{
  if (pcity_dlg->city_name_edit) {
    char name[MAX_LEN_NAME];

    fc_snprintf(name, MAX_LEN_NAME, "%s", pcity_dlg->city_name_edit->string_utf8->text);
    if ((strcmp(city_name_get(pcity), name) != 0)
        || (sdl3_client_flags & CF_CHANGED_CITY_NAME)) {
      copy_chars_to_utf8_str(pcity_dlg->city_name_edit->string_utf8,
                             city_name_get(pcity));
      rebuild_citydlg_title_str(pcity_dlg->end_city_widget_list, pcity);
      sdl3_client_flags &= ~CF_CHANGED_CITY_NAME;
    }
  }
}

/**********************************************************************//**
  Redraw city option panel
  207 = max panel width
**************************************************************************/
static void redraw_misc_city_dialog(struct widget *city_window,
                                    struct city *pcity)
{
  char cbuf[60];
  utf8_str *pstr;
  SDL_Surface *surf;
  SDL_Rect dest;

  fc_snprintf(cbuf, sizeof(cbuf), _("City options"));

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_PANEL);
  pstr->style |= TTF_STYLE_BOLD;

  surf = create_text_surf_from_utf8(pstr);

  dest.x = city_window->area.x + adj_size(2) + (adj_size(192) - surf->w) / 2;
  dest.y = city_window->area.y + adj_size(4) + current_theme->info_icon->h;

  alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

  FREESURFACE(surf);
  FREEUTF8STR(pstr);

  if (!pcity_dlg->panel) {
    create_city_options_widget_list(pcity);
  }
  redraw_group(pcity_dlg->panel->begin_widget_list,
               pcity_dlg->panel->end_widget_list, 0);
}

/**********************************************************************//**
  Redraw supported unit panel
  207 = max panel width
**************************************************************************/
static void redraw_supported_units_city_dialog(struct widget *city_window,
                                               struct city *pcity)
{
  char cbuf[60];
  utf8_str *pstr;
  SDL_Surface *surf;
  SDL_Rect dest;
  struct unit_list *units;
  int size;

  if (city_owner(pcity_dlg->pcity) != client.conn.playing) {
    units = (pcity_dlg->pcity->client.info_units_supported);
  } else {
    units = (pcity_dlg->pcity->units_supported);
  }

  size = unit_list_size(units);

  fc_snprintf(cbuf, sizeof(cbuf), _("Supported units: %d"), size);

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_PANEL);
  pstr->style |= TTF_STYLE_BOLD;

  surf = create_text_surf_from_utf8(pstr);

  dest.x = city_window->area.x + adj_size(2) + (adj_size(192) - surf->w) / 2;
  dest.y = city_window->area.y + + adj_size(4) + current_theme->info_icon->h;

  alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

  FREESURFACE(surf);
  FREEUTF8STR(pstr);

  if (pcity_dlg->panel) {
    if (size > 0) {
      redraw_group(pcity_dlg->panel->begin_widget_list,
                   pcity_dlg->panel->end_widget_list, 0);
    } else {
      del_group_of_widgets_from_gui_list(pcity_dlg->panel->begin_widget_list,
                                         pcity_dlg->panel->end_widget_list);
      FC_FREE(pcity_dlg->panel->scroll);
      FC_FREE(pcity_dlg->panel);
    }
  } else {
    if (size > 0) {
      create_present_supported_units_widget_list(units);
      redraw_group(pcity_dlg->panel->begin_widget_list,
                   pcity_dlg->panel->end_widget_list, 0);
    }
  }
}

/**********************************************************************//**
  Redraw garrison panel
  207 = max panel width
**************************************************************************/
static void redraw_army_city_dialog(struct widget *city_window,
                                    struct city *pcity)
{
  char cbuf[60];
  utf8_str *pstr;
  SDL_Surface *surf;
  SDL_Rect dest;
  struct unit_list *units;
  int size;

  if (city_owner(pcity_dlg->pcity) != client.conn.playing) {
    units = pcity_dlg->pcity->client.info_units_present;
  } else {
    units = pcity_dlg->pcity->tile->units;
  }

  size = unit_list_size(units);

  fc_snprintf(cbuf, sizeof(cbuf), _("Present units: %d"), size);

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_PANEL);
  pstr->style |= TTF_STYLE_BOLD;

  surf = create_text_surf_from_utf8(pstr);

  dest.x = city_window->area.x + adj_size(2) + (adj_size(192) - surf->w) / 2;
  dest.y = city_window->area.y + adj_size(4) + current_theme->info_icon->h;

  alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

  FREESURFACE(surf);
  FREEUTF8STR(pstr);

  if (pcity_dlg->panel) {
    if (size) {
      redraw_group(pcity_dlg->panel->begin_widget_list,
                   pcity_dlg->panel->end_widget_list, 0);
    } else {
      del_group_of_widgets_from_gui_list(pcity_dlg->panel->begin_widget_list,
                                         pcity_dlg->panel->end_widget_list);
      FC_FREE(pcity_dlg->panel->scroll);
      FC_FREE(pcity_dlg->panel);
    }
  } else {
    if (size) {
      create_present_supported_units_widget_list(units);
      redraw_group(pcity_dlg->panel->begin_widget_list,
                   pcity_dlg->panel->end_widget_list, 0);
    }
  }
}

/**********************************************************************//**
  Redraw Info panel
  207 = max panel width
**************************************************************************/
static void redraw_info_city_dialog(struct widget *city_window,
                                    struct city *pcity)
{
  char cbuf[30];
  struct city *trade_city = NULL;
  int step, i, xx;
  utf8_str *pstr = NULL;
  SDL_Surface *surf = NULL;
  SDL_Rect dest;

  fc_snprintf(cbuf, sizeof(cbuf), _("City info"));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_PANEL);
  pstr->style |= TTF_STYLE_BOLD;

  surf = create_text_surf_from_utf8(pstr);

  dest.x = city_window->area.x + adj_size(2) + (adj_size(192) - surf->w) / 2;
  dest.y = city_window->area.y + adj_size(4) + current_theme->info_icon->h;

  alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

  dest.x = city_window->size.x + adj_size(10);
  dest.y += surf->h + 1;

  FREESURFACE(surf);

  change_fonto_utf8(pstr, FONTO_DEFAULT_PLUS);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_INFOPANEL);

  if (pcity->pollution) {
    fc_snprintf(cbuf, sizeof(cbuf), _("Pollution: %d"),
                pcity->pollution);

    copy_chars_to_utf8_str(pstr, cbuf);

    surf = create_text_surf_from_utf8(pstr);

    alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

    dest.y += surf->h + adj_size(3);

    FREESURFACE(surf);

    if (((icons->pollution->w + 1) * pcity->pollution) > adj_size(187)) {
      step = (adj_size(187) - icons->pollution->w) / (pcity->pollution - 1);
    } else {
      step = icons->pollution->w + 1;
    }

    for (i = 0; i < pcity->pollution; i++) {
      alphablit(icons->pollution, NULL, city_window->dst->surface, &dest, 255);
      dest.x += step;
    }

    dest.x = city_window->size.x + adj_size(10);
    dest.y += icons->pollution->h + adj_size(3);

  } else {
    fc_snprintf(cbuf, sizeof(cbuf), _("Pollution: none"));

    copy_chars_to_utf8_str(pstr, cbuf);

    surf = create_text_surf_from_utf8(pstr);

    alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

    dest.y += surf->h + adj_size(3);

    FREESURFACE(surf);
  }

  if (game.info.illness_on) {
    int risk_pml = city_illness_calc(pcity, NULL, NULL, NULL, NULL);

    fc_snprintf(cbuf, sizeof(cbuf), _("Plague risk: %.1f%%"),
                (double)risk_pml / 10.0);
  } else {
    fc_snprintf(cbuf, sizeof(cbuf), _("Plague risk: none"));
  }

  copy_chars_to_utf8_str(pstr, cbuf);
  surf = create_text_surf_from_utf8(pstr);
  alphablit(surf, NULL, city_window->dst->surface, &dest, 255);
  dest.y += surf->h + adj_size(3);
  FREESURFACE(surf);

  fc_snprintf(cbuf, sizeof(cbuf), _("Trade routes: "));

  copy_chars_to_utf8_str(pstr, cbuf);

  surf = create_text_surf_from_utf8(pstr);

  alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

  xx = dest.x + surf->w;
  dest.y += surf->h + adj_size(3);

  FREESURFACE(surf);

  step = 0;
  dest.x = city_window->size.x + adj_size(10);

  trade_routes_iterate(pcity, proute) {
    step += proute->value;

    if ((trade_city = game_city_by_number(proute->partner))) {
      fc_snprintf(cbuf, sizeof(cbuf), "%s: +%d", city_name_get(trade_city),
                  proute->value);
    } else {
      fc_snprintf(cbuf, sizeof(cbuf), "%s: +%d", _("Unknown"),
                  proute->value);
    }

    copy_chars_to_utf8_str(pstr, cbuf);

    surf = create_text_surf_from_utf8(pstr);

    alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

    /* Blit trade icon */
    dest.x += surf->w + adj_size(3);
    dest.y += adj_size(4);
    alphablit(icons->trade, NULL, city_window->dst->surface, &dest, 255);
    dest.x = city_window->size.x + adj_size(10);
    dest.y -= adj_size(4);

    dest.y += surf->h;

    FREESURFACE(surf);
  } trade_routes_iterate_end;

  if (step > 0) {
    fc_snprintf(cbuf, sizeof(cbuf), _("Trade: +%d"), step);

    copy_chars_to_utf8_str(pstr, cbuf);
    surf = create_text_surf_from_utf8(pstr);
    alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

    dest.x += surf->w + adj_size(3);
    dest.y += adj_size(4);
    alphablit(icons->trade, NULL, city_window->dst->surface, &dest, 255);

    FREESURFACE(surf);
  } else {
    fc_snprintf(cbuf, sizeof(cbuf), Q_("?trade:None"));

    copy_chars_to_utf8_str(pstr, cbuf);

    surf = create_text_surf_from_utf8(pstr);

    dest.x = xx;
    dest.y -= surf->h + adj_size(3);
    alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

    FREESURFACE(surf);
  }

  FREEUTF8STR(pstr);
}

/**********************************************************************//**
  Redraw (refresh/update) the happiness info for the dialog
  207 - max panel width
  180 - max citizens icons area width
**************************************************************************/
static void redraw_happiness_city_dialog(const struct widget *city_window,
                                         struct city *pcity)
{
  char cbuf[30];
  int step, i, j, count;
  SDL_Surface *tmp;
  utf8_str *pstr = NULL;
  SDL_Surface *surf = NULL;
  SDL_Rect dest = {0, 0, 0, 0};
  struct effect_list *sources = effect_list_new();

  fc_snprintf(cbuf, sizeof(cbuf), _("Happiness"));

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_PANEL);
  pstr->style |= TTF_STYLE_BOLD;

  surf = create_text_surf_from_utf8(pstr);

  dest.x = city_window->area.x + adj_size(2) + (adj_size(192) - surf->w) / 2;
  dest.y = city_window->area.y + adj_size(4) + current_theme->info_icon->h;
  alphablit(surf, NULL, city_window->dst->surface, &dest, 255);

  dest.x = city_window->size.x + adj_size(10);
  dest.y += surf->h + 1;

  FREESURFACE(surf);
  FREEUTF8STR(pstr);

  count = (pcity->feel[CITIZEN_HAPPY][FEELING_FINAL] + pcity->feel[CITIZEN_CONTENT][FEELING_FINAL]
           + pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL] + pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]
           + city_specialists(pcity));

  if (count * icons->male_happy->w > adj_size(166)) {
    step = (adj_size(166) - icons->male_happy->w) / (count - 1);
  } else {
    step = icons->male_happy->w;
  }

  for (j = 0; j < FEELING_LAST; j++) {
    if (j == 0 || pcity->feel[CITIZEN_HAPPY][j - 1] != pcity->feel[CITIZEN_HAPPY][j]
        || pcity->feel[CITIZEN_CONTENT][j - 1] != pcity->feel[CITIZEN_CONTENT][j]
        || pcity->feel[CITIZEN_UNHAPPY][j - 1] != pcity->feel[CITIZEN_UNHAPPY][j]
        || pcity->feel[CITIZEN_ANGRY][j - 1] != pcity->feel[CITIZEN_ANGRY][j]) {
      int spe, spe_max;

      if (j != 0) {
        create_line(city_window->dst->surface,
                    dest.x, dest.y, dest.x + adj_size(176), dest.y,
                    get_theme_color(COLOR_THEME_CITYDLG_FRAME));
        dest.y += adj_size(5);
      }

      if (pcity->feel[CITIZEN_HAPPY][j]) {
        surf = icons->male_happy;
        for (i = 0; i < pcity->feel[CITIZEN_HAPPY][j]; i++) {
          alphablit(surf, NULL, city_window->dst->surface, &dest, 255);
          dest.x += step;
          if (surf == icons->male_happy) {
            surf = icons->female_happy;
          } else {
            surf = icons->male_happy;
          }
        }
      }

      if (pcity->feel[CITIZEN_CONTENT][j]) {
        surf = icons->male_content;
        for (i = 0; i < pcity->feel[CITIZEN_CONTENT][j]; i++) {
          alphablit(surf, NULL, city_window->dst->surface, &dest, 255);
          dest.x += step;
          if (surf == icons->male_content) {
            surf = icons->female_content;
          } else {
            surf = icons->male_content;
          }
        }
      }

      if (pcity->feel[CITIZEN_UNHAPPY][j]) {
        surf = icons->male_unhappy;
        for (i = 0; i < pcity->feel[CITIZEN_UNHAPPY][j]; i++) {
          alphablit(surf, NULL, city_window->dst->surface, &dest, 255);
          dest.x += step;
          if (surf == icons->male_unhappy) {
            surf = icons->female_unhappy;
          } else {
            surf = icons->male_unhappy;
          }
        }
      }

      if (pcity->feel[CITIZEN_ANGRY][j]) {
        surf = icons->male_angry;
        for (i = 0; i < pcity->feel[CITIZEN_ANGRY][j]; i++) {
          alphablit(surf, NULL, city_window->dst->surface, &dest, 255);
          dest.x += step;
          if (surf == icons->male_angry) {
            surf = icons->female_angry;
          } else {
            surf = icons->male_angry;
          }
        }
      }

      spe_max = specialist_count();
      for (spe = 0 ; spe < spe_max; spe++) {
        if (pcity->specialists[spe]) {
          for (i = 0; i < pcity->specialists[spe]; i++) {
            alphablit(icons->specialists[spe], NULL, city_window->dst->surface,
                      &dest, 255);
            dest.x += step;
          }
        }
      }

      if (j == 1) { /* Luxury effect */
        dest.x =
          city_window->size.x + adj_size(187) - icons->big_luxury->w - adj_size(2);
        count = dest.y;
        dest.y += (icons->male_happy->h -
                   icons->big_luxury->h) / 2;
        alphablit(icons->big_luxury, NULL, city_window->dst->surface, &dest, 255);
        dest.y = count;
      }

      if (j == 2) { /* Improvements effects */
        int w = -1;
        count = 0;

        get_city_bonus_effects(sources, pcity, NULL, EFT_MAKE_CONTENT);

        effect_list_iterate(sources, psource) {
          requirement_vector_iterate(&(psource->reqs), preq) {
            if (preq->source.kind == VUT_IMPROVEMENT) {
              tmp = get_building_surface(preq->source.value.building);
              tmp = zoomSurface(tmp, DEFAULT_ZOOM * ((float)18 / tmp->w),
                                DEFAULT_ZOOM * ((float)18 / tmp->w), 1);

              count += (tmp->h + 1);

              if (w < 0) {
                w = tmp->w;
              }

              FREESURFACE(tmp);
            }
          } requirement_vector_iterate_end;
        } effect_list_iterate_end;

        if (w >= 0) {
          dest.x = city_window->size.x + adj_size(187) - w - adj_size(2);
          i = dest.y;
          dest.y += (icons->male_happy->h - count) / 2;

          effect_list_iterate(sources, psource) {
            requirement_vector_iterate(&(psource->reqs), preq) {
              if (preq->source.kind == VUT_IMPROVEMENT) {
                tmp = get_building_surface(preq->source.value.building);
                tmp = zoomSurface(tmp, DEFAULT_ZOOM * ((float)18 / tmp->w),
                                  DEFAULT_ZOOM * ((float)18 / tmp->w), 1);

                alphablit(tmp, NULL, city_window->dst->surface, &dest, 255);
                dest.y += (tmp->h + 1);

                FREESURFACE(tmp);
              }
            } requirement_vector_iterate_end;
          } effect_list_iterate_end;

          dest.y = i;
        }

        effect_list_clear(sources);
      }

      if (j == 3) { /* police effect */
        dest.x = city_window->size.x + adj_size(187) - icons->police->w - adj_size(5);
        i = dest.y;
        dest.y +=
          (icons->male_happy->h - icons->police->h) / 2;
        alphablit(icons->police, NULL, city_window->dst->surface, &dest, 255);
        dest.y = i;
      }

      if (j == 4) { /* Wonders effect */
        int w = -1;
        count = 0;

        get_city_bonus_effects(sources, pcity, NULL, EFT_MAKE_HAPPY);
        effect_list_iterate(sources, psource) {
          requirement_vector_iterate(&(psource->reqs), preq) {
            if (preq->source.kind == VUT_IMPROVEMENT) {
              tmp = get_building_surface(preq->source.value.building);
              tmp = zoomSurface(tmp, DEFAULT_ZOOM * ((float)18 / tmp->w),
                                DEFAULT_ZOOM * ((float)18 / tmp->w), 1);

              count += (tmp->h + 1);

              if (w < 0) {
                w = tmp->w;
              }

              FREESURFACE(tmp);
            }
          } requirement_vector_iterate_end;
        } effect_list_iterate_end;

        effect_list_clear(sources);

        get_city_bonus_effects(sources, pcity, NULL, EFT_FORCE_CONTENT);

        effect_list_iterate(sources, psource) {
          requirement_vector_iterate(&(psource->reqs), preq) {
            if (preq->source.kind == VUT_IMPROVEMENT) {
              tmp = get_building_surface(preq->source.value.building);
              tmp = zoomSurface(tmp, DEFAULT_ZOOM * ((float)18 / tmp->w),
                                DEFAULT_ZOOM * ((float)18 / tmp->w), 1);
              count += (tmp->h + 1);

              if (w < 0) {
                w = tmp->w;
              }

              FREESURFACE(tmp);
            }
          } requirement_vector_iterate_end;
        } effect_list_iterate_end;

        effect_list_clear(sources);

        get_city_bonus_effects(sources, pcity, NULL, EFT_NO_UNHAPPY);

        effect_list_iterate(sources, psource) {
          requirement_vector_iterate(&(psource->reqs), preq) {
            if (preq->source.kind == VUT_IMPROVEMENT) {
              tmp = get_building_surface(preq->source.value.building);
              tmp = zoomSurface(tmp, DEFAULT_ZOOM * ((float)18 / tmp->w),
                                DEFAULT_ZOOM * ((float)18 / tmp->w), 1);

              count += (tmp->h + 1);

              if (w < 0) {
                w = tmp->w;
              }

              FREESURFACE(tmp);
            }
          } requirement_vector_iterate_end;
        } effect_list_iterate_end;

        effect_list_clear(sources);

        if (w >= 0) {
          dest.x = city_window->size.x + adj_size(187) - surf->w - adj_size(2);
          i = dest.y;
          dest.y += (icons->male_happy->h - count) / 2;

          get_city_bonus_effects(sources, pcity, NULL, EFT_MAKE_HAPPY);

          effect_list_iterate(sources, psource) {
            requirement_vector_iterate(&(psource->reqs), preq) {
              if (preq->source.kind == VUT_IMPROVEMENT) {
                tmp = get_building_surface(preq->source.value.building);
                tmp = zoomSurface(tmp, DEFAULT_ZOOM * ((float)18 / tmp->w),
                                  DEFAULT_ZOOM * ((float)18 / tmp->w), 1);

                alphablit(tmp, NULL, city_window->dst->surface, &dest, 255);
                dest.y += (tmp->h + 1);

                FREESURFACE(tmp);
              }
            } requirement_vector_iterate_end;
          } effect_list_iterate_end;

          effect_list_clear(sources);

          get_city_bonus_effects(sources, pcity, NULL, EFT_FORCE_CONTENT);

          effect_list_iterate(sources, psource) {
            requirement_vector_iterate(&(psource->reqs), preq) {
              if (preq->source.kind == VUT_IMPROVEMENT) {
                tmp = get_building_surface(preq->source.value.building);
                tmp = zoomSurface(tmp, DEFAULT_ZOOM * ((float)18 / tmp->w),
                                  DEFAULT_ZOOM * ((float)18 / tmp->w), 1);

                alphablit(tmp, NULL, city_window->dst->surface, &dest, 255);
                dest.y += (tmp->h + 1);

                FREESURFACE(tmp);
              }
            } requirement_vector_iterate_end;
          } effect_list_iterate_end;

          effect_list_clear(sources);

          get_city_bonus_effects(sources, pcity, NULL, EFT_NO_UNHAPPY);

          effect_list_iterate(sources, psource) {
            requirement_vector_iterate(&(psource->reqs), preq) {
              if (preq->source.kind == VUT_IMPROVEMENT) {
                tmp = get_building_surface(preq->source.value.building);
                tmp = zoomSurface(tmp, DEFAULT_ZOOM * ((float)18 / tmp->w),
                                  DEFAULT_ZOOM * ((float)18 / tmp->w), 1);

                alphablit(tmp, NULL, city_window->dst->surface, &dest, 255);
                dest.y += (tmp->h + 1);

                FREESURFACE(tmp);
              }
            } requirement_vector_iterate_end;
          } effect_list_iterate_end;

          effect_list_clear(sources);

          dest.y = i;
        }
      }

      dest.x = city_window->size.x + adj_size(10);
      dest.y += icons->male_happy->h + adj_size(5);

    }
  }

  effect_list_destroy(sources);
}

/**********************************************************************//**
  Redraw the dialog.
**************************************************************************/
static void redraw_city_dialog(struct city *pcity)
{
  char cbuf[40];
  int i, step, count, limit;
  int cost = 0;
  SDL_Rect dest;
  struct widget *pwindow = pcity_dlg->end_city_widget_list;
  SDL_Surface *buf = NULL, *buf2 = NULL;
  utf8_str *pstr = NULL;
  int spe, spe_max;

  refresh_city_names(pcity);

  if ((city_unhappy(pcity) || city_celebrating(pcity) || city_happy(pcity)
       || cma_is_city_under_agent(pcity, NULL))
      ^ ((sdl3_client_flags & CF_CITY_STATUS_SPECIAL) == CF_CITY_STATUS_SPECIAL)) {
    /* City status was changed : NORMAL <-> DISORDER, HAPPY, CELEBR. */

    sdl3_client_flags ^= CF_CITY_STATUS_SPECIAL;

#if 0
    /* Upd. resource map */
    FREESURFACE(pcity_dlg->resource_map->theme);
    pcity_dlg->resource_map->theme = get_scaled_city_map(pcity);
#endif

    /* Upd. window title */
    rebuild_citydlg_title_str(pcity_dlg->end_city_widget_list, pcity);
  }

  /* Update resource map */
  FREESURFACE(pcity_dlg->resource_map->theme);
  pcity_dlg->resource_map->theme = get_scaled_city_map(pcity);

  /* Redraw city dlg */
  redraw_group(pcity_dlg->begin_city_widget_list,
               pcity_dlg->end_city_widget_list, 0);

  /* ================================================================= */
  fc_snprintf(cbuf, sizeof(cbuf), _("City map"));

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_GOLD);
  pstr->style |= TTF_STYLE_BOLD;

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(196) + (adj_size(132) - buf->w) / 2;
  dest.y = pwindow->size.y + adj_size(49) + (adj_size(13) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  fc_snprintf(cbuf, sizeof(cbuf), _("Citizens"));

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_LUX);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(344) + (adj_size(146) - buf->w) / 2;
  dest.y = pwindow->size.y + adj_size(47) + (adj_size(13) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  fc_snprintf(cbuf, sizeof(cbuf), _("City improvements"));

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_GOLD);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(504) + (adj_size(132) - buf->w) / 2;
  dest.y = pwindow->size.y + adj_size(49) + (adj_size(13) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* ================================================================= */
  /* Food label */
  fc_snprintf(cbuf, sizeof(cbuf), _("Food: %d per turn"),
              pcity->prod[O_FOOD]);

  copy_chars_to_utf8_str(pstr, cbuf);

  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_FOODPERTURN);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(200);
  dest.y = pwindow->size.y + adj_size(228) + (adj_size(16) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw food income */
  dest.y = pwindow->size.y + adj_size(246) + (adj_size(16) - icons->big_food->h) / 2;
  dest.x = pwindow->size.x + adj_size(203);

  if (pcity->surplus[O_FOOD] >= 0) {
    count = pcity->prod[O_FOOD] - pcity->surplus[O_FOOD];
  } else {
    count = pcity->prod[O_FOOD];
  }

  if (((icons->big_food->w + 1) * count) > adj_size(200)) {
    step = (adj_size(200) - icons->big_food->w) / (count - 1);
  } else {
    step = icons->big_food->w + 1;
  }

  for (i = 0; i < count; i++) {
    alphablit(icons->big_food, NULL, pwindow->dst->surface, &dest, 255);
    dest.x += step;
  }

  fc_snprintf(cbuf, sizeof(cbuf), Q_("?food:Surplus: %d"),
              pcity->surplus[O_FOOD]);

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_FOOD_SURPLUS);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(440) - buf->w;
  dest.y = pwindow->size.y + adj_size(228) + (adj_size(16) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw surplus of food */
  if (pcity->surplus[O_FOOD]) {
    if (pcity->surplus[O_FOOD] > 0) {
      count = pcity->surplus[O_FOOD];
      buf = icons->big_food;
    } else {
      count = -1 * pcity->surplus[O_FOOD];
      buf = icons->big_food_corr;
    }

    dest.x = pwindow->size.x + adj_size(423);
    dest.y = pwindow->size.y + adj_size(246) + (adj_size(16) - buf->h) / 2;

    if (count > 2) {
      if (count < 18) {
        step = (adj_size(30) - buf->w) / (count - 1);
      } else {
        step = 1;
        count = 17;
      }
    } else {
      step = buf->w + 1;
    }

    for (i = 0; i < count; i++) {
      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x -= step;
    }
  }

  /* ================================================================= */
  /* Productions label */
  fc_snprintf(cbuf, sizeof(cbuf), _("Production: %d (%d) per turn"),
              pcity->surplus[O_SHIELD],
              pcity->prod[O_SHIELD] + pcity->waste[O_SHIELD]);

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_PROD);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(200);
  dest.y = pwindow->size.y + adj_size(263) + (adj_size(15) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw productions shields */
  if (pcity->surplus[O_SHIELD]) {

    if (pcity->surplus[O_SHIELD] > 0) {
      count = pcity->surplus[O_SHIELD] + pcity->waste[O_SHIELD];
      buf = icons->big_shield;
    } else {
      count = -1 * pcity->surplus[O_SHIELD];
      buf = icons->big_shield_corr;
    }

    dest.y = pwindow->size.y + adj_size(281) + (adj_size(16) - buf->h) / 2;
    dest.x = pwindow->size.x + adj_size(203);

    if ((buf->w * count) > adj_size(200)) {
      step = (adj_size(200) - buf->w) / (count - 1);
    } else {
      step = buf->w;
    }

    for (i = 0; i < count; i++) {
      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += step;
      if (i > pcity->surplus[O_SHIELD]) {
        buf = icons->big_shield_corr;
      }
    }
  }

  /* Support shields label */
  fc_snprintf(cbuf, sizeof(cbuf), Q_("?production:Support: %d"),
              pcity->prod[O_SHIELD] + pcity->waste[O_SHIELD] -
              pcity->surplus[O_SHIELD]);

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_SUPPORT);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(440) - buf->w;
  dest.y = pwindow->size.y + adj_size(263) + (adj_size(15) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw support shields */
  if (pcity->prod[O_SHIELD] - pcity->surplus[O_SHIELD]) {
    dest.x = pwindow->size.x + adj_size(423);
    dest.y =
      pwindow->size.y + adj_size(281) + (adj_size(16) - icons->big_shield->h) / 2;

    if ((icons->big_shield->w + 1) * (pcity->prod[O_SHIELD] -
                                      pcity->surplus[O_SHIELD]) > adj_size(30)) {
      step =
        (adj_size(30) - icons->big_food->w) / (pcity->prod[O_SHIELD] -
                                               pcity->surplus[O_SHIELD] - 1);
    } else {
      step = icons->big_shield->w + 1;
    }

    for (i = 0; i < (pcity->prod[O_SHIELD] - pcity->surplus[O_SHIELD]); i++) {
      alphablit(icons->big_shield, NULL, pwindow->dst->surface, &dest, 255);
      dest.x -= step;
    }
  }

  /* ================================================================= */
  /* Trade label */
  fc_snprintf(cbuf, sizeof(cbuf), _("Trade: %d per turn"),
              pcity->surplus[O_TRADE]);

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_TRADE);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(200);
  dest.y = pwindow->size.y + adj_size(298) + (adj_size(15) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw total (trade - corruption) */
  if (pcity->surplus[O_TRADE]) {
    dest.y =
      pwindow->size.y + adj_size(316) + (adj_size(16) - icons->big_trade->h) / 2;
    dest.x = pwindow->size.x + adj_size(203);

    if (((icons->big_trade->w + 1) * pcity->surplus[O_TRADE]) > adj_size(200)) {
      step = (adj_size(200) - icons->big_trade->w) / (pcity->surplus[O_TRADE] - 1);
    } else {
      step = icons->big_trade->w + 1;
    }

    for (i = 0; i < pcity->surplus[O_TRADE]; i++) {
      alphablit(icons->big_trade, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += step;
    }
  }

  /* Corruption label */
  fc_snprintf(cbuf, sizeof(cbuf), _("Corruption: %d"),
              pcity->waste[O_TRADE]);

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_CORRUPTION);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(440) - buf->w;
  dest.y = pwindow->size.y + adj_size(298) + (adj_size(15) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw corruption */
  if (pcity->waste[O_TRADE] > 0) {
    dest.x = pwindow->size.x + adj_size(423);
    dest.y =
      pwindow->size.y + adj_size(316) + (adj_size(16) - icons->big_trade->h) / 2;

    if (((icons->big_trade_corr->w + 1) * pcity->waste[O_TRADE]) > adj_size(30)) {
      step =
          (adj_size(30) - icons->big_trade_corr->w) / (pcity->waste[O_TRADE] - 1);
    } else {
      step = icons->big_trade_corr->w + 1;
    }

    for (i = 0; i < pcity->waste[O_TRADE]; i++) {
      alphablit(icons->big_trade_corr, NULL, pwindow->dst->surface,
                &dest, 255);
      dest.x -= step;
    }
  }

  /* ================================================================= */
  /* Gold label */
  fc_snprintf(cbuf, sizeof(cbuf), _("Gold: %d (%d) per turn"),
              pcity->surplus[O_GOLD], pcity->prod[O_GOLD]);

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_GOLD);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(200);
  dest.y = pwindow->size.y + adj_size(342) + (adj_size(15) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw coins */
  count = pcity->surplus[O_GOLD];
  if (count) {

    if (count > 0) {
      buf = icons->big_coin;
    } else {
      count *= -1;
      buf = icons->big_coin_corr;
    }

    dest.y = pwindow->size.y + adj_size(359) + (adj_size(16) - buf->h) / 2;
    dest.x = pwindow->size.x + adj_size(203);

    if ((buf->w * count) > adj_size(110)) {
      step = (adj_size(110) - buf->w) / (count - 1);
      if (!step) {
        step = 1;
        count = 97;
      }
    } else {
      step = buf->w;
    }

    for (i = 0; i < count; i++) {
      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += step;
    }

  }

  /* Upkeep label */
  fc_snprintf(cbuf, sizeof(cbuf), _("Upkeep: %d"),
              pcity->prod[O_GOLD] - pcity->surplus[O_GOLD]);

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_UPKEEP);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(440) - buf->w;
  dest.y = pwindow->size.y + adj_size(342) + (adj_size(15) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw upkeep */
  count = pcity->surplus[O_GOLD];
  if (pcity->prod[O_GOLD] - count) {

    dest.x = pwindow->size.x + adj_size(423);
    dest.y = pwindow->size.y + adj_size(359)
      + (adj_size(16) - icons->big_coin_upkeep->h) / 2;

    if (((icons->big_coin_upkeep->w + 1) *
         (pcity->prod[O_GOLD] - count)) > adj_size(110)) {
      step = (adj_size(110) - icons->big_coin_upkeep->w) /
          (pcity->prod[O_GOLD] - count - 1);
    } else {
      step = icons->big_coin_upkeep->w + 1;
    }

    for (i = 0; i < (pcity->prod[O_GOLD] - count); i++) {
      alphablit(icons->big_coin_upkeep, NULL, pwindow->dst->surface,
                &dest, 255);
      dest.x -= step;
    }
  }

  /* ================================================================= */
  /* Science label */
  fc_snprintf(cbuf, sizeof(cbuf), _("Science: %d per turn"),
              pcity->prod[O_SCIENCE]);

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_SCIENCE);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(200);
  dest.y = pwindow->size.y + adj_size(376) + (adj_size(15) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw colb */
  count = pcity->prod[O_SCIENCE];
  if (count) {

    dest.y =
        pwindow->size.y + adj_size(394) + (adj_size(16) - icons->big_colb->h) / 2;
    dest.x = pwindow->size.x + adj_size(203);

    if ((icons->big_colb->w * count) > adj_size(235)) {
      step = (adj_size(235) - icons->big_colb->w) / (count - 1);
      if (!step) {
        step = 1;
        count = 222;
      }
    } else {
      step = icons->big_colb->w;
    }

    for (i = 0; i < count; i++) {
      alphablit(icons->big_colb, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += step;
    }
  }

  /* ================================================================= */
  /* Luxury label */
  fc_snprintf(cbuf, sizeof(cbuf), _("Luxury: %d per turn"),
              pcity->prod[O_LUXURY]);

  copy_chars_to_utf8_str(pstr, cbuf);
  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_LUX);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(200);
  dest.y = pwindow->size.y + adj_size(412) + (adj_size(15) - buf->h) / 2;

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  /* Draw luxury */
  if (pcity->prod[O_LUXURY]) {

    dest.y =
        pwindow->size.y + adj_size(429) + (adj_size(16) - icons->big_luxury->h) / 2;
    dest.x = pwindow->size.x + adj_size(203);

    if ((icons->big_luxury->w * pcity->prod[O_LUXURY]) > adj_size(235)) {
      step =
          (adj_size(235) - icons->big_luxury->w) / (pcity->prod[O_LUXURY] - 1);
    } else {
      step = icons->big_luxury->w;
    }

    for (i = 0; i < pcity->prod[O_LUXURY]; i++) {
      alphablit(icons->big_luxury, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += step;
    }
  }

  /* ================================================================= */
  /* Turns to grow label */
  count = city_turns_to_grow(pcity);
  if (count == 0) {
    fc_snprintf(cbuf, sizeof(cbuf), _("City growth: blocked"));
  } else if (count == FC_INFINITY) {
    fc_snprintf(cbuf, sizeof(cbuf), _("City growth: never"));
  } else if (count < 0) {
    /* Turns until famine */
    fc_snprintf(cbuf, sizeof(cbuf),
                _("City shrinks: %d %s"), abs(count),
                PL_("turn", "turns", abs(count)));
  } else {
    fc_snprintf(cbuf, sizeof(cbuf),
                _("City growth: %d %s"), count,
                PL_("turn", "turns", count));
  }

  copy_chars_to_utf8_str(pstr, cbuf);

  pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_GROWTH);

  buf = create_text_surf_from_utf8(pstr);

  dest.x = pwindow->size.x + adj_size(445) + (adj_size(192) - buf->w) / 2;
  dest.y = pwindow->size.y + adj_size(227);

  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);

  count = (city_granary_size(city_size_get(pcity))) / 10;

  if (count > 12) {
    step = (adj_size(168) - icons->big_food->h) / adj_size((11 + count - 12));
    i = (count - 1) * step + 14;
  } else {
    step = icons->big_food->h;
    i = count * step;
  }

  /* Food stock */
  if (get_city_bonus(pcity, EFT_GROWTH_FOOD) > 0) {
    /* With granary */
    /* Stocks label */
    copy_chars_to_utf8_str(pstr, _("Stock"));
    buf = create_text_surf_from_utf8(pstr);

    dest.x = pwindow->size.x + adj_size(461) + (adj_size(76) - buf->w) / 2;
    dest.y = pwindow->size.y + adj_size(258) - buf->h - 1;

    alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

    FREESURFACE(buf);

    /* Granary label */
    copy_chars_to_utf8_str(pstr, _("Granary"));
    buf = create_text_surf_from_utf8(pstr);

    dest.x = pwindow->size.x + adj_size(549) + (adj_size(76) - buf->w) / 2;
    dest.y = pwindow->size.y + adj_size(258) - buf->h - 1;

    alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

    FREESURFACE(buf);

    /* Draw bcgd granary */
    dest.x = pwindow->size.x + adj_size(462);
    dest.y = pwindow->size.y + adj_size(260);
    dest.w = 70 + 4;
    dest.h = i + 4;

    fill_rect_alpha(pwindow->dst->surface, &dest,
                    get_theme_color(COLOR_THEME_CITYDLG_GRANARY));

    create_frame(pwindow->dst->surface,
                 dest.x - 1, dest.y - 1, dest.w, dest.h,
                 get_theme_color(COLOR_THEME_CITYDLG_FRAME));

    /* Draw bcgd stocks*/
    dest.x = pwindow->size.x + adj_size(550);
    dest.y = pwindow->size.y + adj_size(260);

    fill_rect_alpha(pwindow->dst->surface, &dest,
                    get_theme_color(COLOR_THEME_CITYDLG_STOCKS));

    create_frame(pwindow->dst->surface,
                 dest.x - 1, dest.y - 1, dest.w, dest.h,
                 get_theme_color(COLOR_THEME_CITYDLG_FRAME));

    /* Draw stocks icons */
    cost = city_granary_size(city_size_get(pcity));
    if (pcity->food_stock + pcity->surplus[O_FOOD] > cost) {
      count = cost;
    } else {
      if (pcity->surplus[O_FOOD] < 0) {
        count = pcity->food_stock;
      } else {
        count = pcity->food_stock + pcity->surplus[O_FOOD];
      }
    }
    cost /= 2;

    if (pcity->surplus[O_FOOD] < 0) {
      limit = pcity->food_stock + pcity->surplus[O_FOOD];
      if (limit < 0) {
        limit = 0;
      }
    } else {
      limit = 0xffff;
    }

    dest.x += 2;
    dest.y += 2;
    i = 0;
    buf = icons->big_food;
    while (count && cost) {
      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += buf->w;
      count--;
      cost--;
      i++;
      if (dest.x > pwindow->size.x + adj_size(620)) {
        dest.x = pwindow->size.x + adj_size(552);
        dest.y += step;
      }
      if (i > limit - 1) {
        buf = icons->big_food_corr;
      } else {
        if (i > pcity->food_stock - 1) {
          buf = icons->big_food_surplus;
        }
      }
    }
    /* Draw granary icons */
    dest.x = pwindow->size.x + adj_size(462) + adj_size(2);
    dest.y = pwindow->size.y + adj_size(260) + adj_size(2);

    while (count) {
      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += buf->w;
      count--;
      i++;

      if (dest.x > pwindow->size.x + adj_size(532)) {
        dest.x = pwindow->size.x + adj_size(464);
        dest.y += step;
      }

      if (i > limit - 1) {
        buf = icons->big_food_corr;
      } else if (i > pcity->food_stock - 1) {
        buf = icons->big_food_surplus;
      }
    }

  } else {
    /* Without granary */
    /* Stocks label */
    copy_chars_to_utf8_str(pstr, _("Stock"));
    buf = create_text_surf_from_utf8(pstr);

    dest.x = pwindow->size.x + adj_size(461) + (adj_size(144) - buf->w) / 2;
    dest.y = pwindow->size.y + adj_size(258) - buf->h - 1;

    alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
    FREESURFACE(buf);

    /* Food stock */

    /* Draw bcgd */
    dest.x = pwindow->size.x + adj_size(462);
    dest.y = pwindow->size.y + adj_size(260);
    dest.w = adj_size(144);
    dest.h = i + adj_size(4);

    fill_rect_alpha(pwindow->dst->surface, &dest,
                    get_theme_color(COLOR_THEME_CITYDLG_FOODSTOCK));

    create_frame(pwindow->dst->surface,
                 dest.x - 1, dest.y - 1, dest.w, dest.h,
                 get_theme_color(COLOR_THEME_CITYDLG_FRAME));

    /* Draw icons */
    cost = city_granary_size(city_size_get(pcity));
    if (pcity->food_stock + pcity->surplus[O_FOOD] > cost) {
      count = cost;
    } else {
      if (pcity->surplus[O_FOOD] < 0) {
        count = pcity->food_stock;
      } else {
        count = pcity->food_stock + pcity->surplus[O_FOOD];
      }
    }

    if (pcity->surplus[O_FOOD] < 0) {
      limit = pcity->food_stock + pcity->surplus[O_FOOD];
      if (limit < 0) {
        limit = 0;
      }
    } else {
      limit = 0xffff;
    }

    dest.x += adj_size(2);
    dest.y += adj_size(2);
    i = 0;
    buf = icons->big_food;
    while (count) {
      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += buf->w;
      count--;
      i++;

      if (dest.x > pwindow->size.x + adj_size(602)) {
        dest.x = pwindow->size.x + adj_size(464);
        dest.y += step;
      }
      if (i > limit - 1) {
        buf = icons->big_food_corr;
      } else {
        if (i > pcity->food_stock - 1) {
          buf = icons->big_food_surplus;
        }
      }
    }
  }
  /* ================================================================= */

  /* draw productions shields progress */
  if (VUT_UTYPE == pcity->production.kind) {
    const struct unit_type *punittype = pcity->production.value.utype;

    cost = utype_build_shield_cost(pcity, NULL, punittype);
    count = cost / 10;

    copy_chars_to_utf8_str(pstr, utype_name_translation(punittype));
    buf = create_text_surf_from_utf8(pstr);

    buf2 = get_unittype_surface(punittype, direction8_invalid());
    buf2 = zoomSurface(buf2, DEFAULT_ZOOM * ((float)32 / buf2->h),
                        DEFAULT_ZOOM * ((float)32 / buf2->h), 1);

    /* blit unit icon */
    dest.x = pwindow->size.x + adj_size(6) + (adj_size(185) - (buf->w + buf2->w + adj_size(5))) / 2;
    dest.y = pwindow->size.y + adj_size(233);

    alphablit(buf2, NULL, pwindow->dst->surface, &dest, 255);

    dest.y += (buf2->h - buf->h) / 2;
    dest.x += buf2->w + adj_size(5);

  } else {
    const struct impr_type *pimprove = pcity->production.value.building;

    if (is_convert_improvement(pimprove)) {

      if (pcity_dlg->buy_button
          && get_wstate(pcity_dlg->buy_button) != FC_WS_DISABLED) {
        set_wstate(pcity_dlg->buy_button, FC_WS_DISABLED);
        widget_redraw(pcity_dlg->buy_button);
      }

      /* You can't see capitalization progress */
      count = 0;

    } else {

      if (city_can_buy(pcity) && pcity_dlg->buy_button
          && (get_wstate(pcity_dlg->buy_button) == FC_WS_DISABLED)) {
        set_wstate(pcity_dlg->buy_button, FC_WS_NORMAL);
        widget_redraw(pcity_dlg->buy_button);
      }

      cost = impr_build_shield_cost(pcity, pimprove);
      count = cost / 10;
    }

    copy_chars_to_utf8_str(pstr, improvement_name_translation(pimprove));
    buf = create_text_surf_from_utf8(pstr);

    buf2 = get_building_surface(pcity->production.value.building);
    buf2 = zoomSurface(buf2, DEFAULT_ZOOM * ((float)32 / buf2->h),
                       DEFAULT_ZOOM * ((float)32 / buf2->h), 1);

    /* blit impr icon */
    dest.x = pwindow->size.x + adj_size(6) + (adj_size(185) - (buf->w + buf2->w + adj_size(5))) / 2;
    dest.y = pwindow->size.y + adj_size(233);

    alphablit(buf2, NULL, pwindow->dst->surface, &dest, 255);

    dest.y += (buf2->h - buf->h) / 2;
    dest.x += buf2->w + adj_size(5);
  }

  /* blit unit/impr name */
  alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

  FREESURFACE(buf);
  FREESURFACE(buf2);

  if (count) {
    if (count > 11) {
      step = (adj_size(154) - icons->big_shield->h) / adj_size((10 + count - 11));

      if (!step) {
        step = 1;
      }

      i = (step * (count - 1)) + icons->big_shield->h;
    } else {
      step = icons->big_shield->h;
      i = count * step;
    }

    /* draw shield stock background */
    dest.x = pwindow->size.x + adj_size(28);
    dest.y = pwindow->size.y + adj_size(270);
    dest.w = adj_size(144);
    dest.h = i + adj_size(4);

    fill_rect_alpha(pwindow->dst->surface, &dest,
                    get_theme_color(COLOR_THEME_CITYDLG_SHIELDSTOCK));

    create_frame(pwindow->dst->surface,
                 dest.x - 1, dest.y - 1, dest.w, dest.h,
                 get_theme_color(COLOR_THEME_CITYDLG_FRAME));

    /* draw production progress text */
    dest.y = pwindow->size.y + adj_size(270) + dest.h + 1;

    if (pcity->shield_stock < cost) {
      count = city_production_turns_to_build(pcity, TRUE);
      if (count == 999) {
        fc_snprintf(cbuf, sizeof(cbuf), "(%d/%d) %s!",
                    pcity->shield_stock, cost,  _("blocked"));
      } else {
        fc_snprintf(cbuf, sizeof(cbuf), "(%d/%d) %d %s",
                    pcity->shield_stock, cost, count, PL_("turn", "turns", count));
      }
    } else {
      fc_snprintf(cbuf, sizeof(cbuf), "(%d/%d) %s!",
                  pcity->shield_stock, cost, _("finished"));
    }

    copy_chars_to_utf8_str(pstr, cbuf);
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_LUX);

    buf = create_text_surf_from_utf8(pstr);

    dest.x = pwindow->size.x + adj_size(6) + (adj_size(185) - buf->w) / 2;

    alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);

    FREEUTF8STR(pstr);
    FREESURFACE(buf);

    /* draw shield stock */
    if (pcity->shield_stock + pcity->surplus[O_SHIELD] <= cost) {
      count = pcity->shield_stock + pcity->surplus[O_SHIELD];
    } else {
      count = cost;
    }
    dest.x = pwindow->size.x + adj_size(29) + adj_size(2);
    dest.y = pwindow->size.y + adj_size(270) + adj_size(2);
    i = 0;

    buf = icons->big_shield;
    while (count > 0) {
      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += buf->w;
      count--;
      if (dest.x > pwindow->size.x + adj_size(170)) {
        dest.x = pwindow->size.x + adj_size(31);
        dest.y += step;
      }
      i++;
      if (i > pcity->shield_stock - 1) {
        buf = icons->big_shield_surplus;
      }
    }
  }

  /* count != 0 */
  /* ==================================================== */
  /* Draw Citizens and Superspecialists*/
  count = (pcity->feel[CITIZEN_HAPPY][FEELING_FINAL] + pcity->feel[CITIZEN_CONTENT][FEELING_FINAL]
           + pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL] + pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]
           + city_specialists(pcity) + city_superspecialists(pcity));

  buf = get_citizen_surface(CITIZEN_HAPPY, 0);

  if (count > 13) {
    step = (adj_size(440) - buf->w) / (adj_size(12 + count - 13));
  } else {
    step = buf->w;
  }

  pcity_dlg->citizen_step = step;

  dest.x = pwindow->size.x + adj_size(198);
  dest.y = pwindow->size.y + pwindow->area.y + adj_size(1) + (adj_size(22) - buf->h) / 2;
  pcity_dlg->spec_area.x = pwindow->dst->dest_rect.x + dest.x;
  pcity_dlg->spec_area.y = pwindow->dst->dest_rect.y + dest.y;
  pcity_dlg->spec_area.w = count * step;
  pcity_dlg->spec_area.h = buf->h;

  if (pcity->feel[CITIZEN_HAPPY][FEELING_FINAL]) {
    for (i = 0; i < pcity->feel[CITIZEN_HAPPY][FEELING_FINAL]; i++) {
      buf = adj_surf(get_citizen_surface(CITIZEN_HAPPY, i));

      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += step;
      FREESURFACE(buf);
    }
  }

  if (pcity->feel[CITIZEN_CONTENT][FEELING_FINAL]) {
    for (i = 0; i < pcity->feel[CITIZEN_CONTENT][FEELING_FINAL]; i++) {
      buf = adj_surf(get_citizen_surface(CITIZEN_CONTENT, i));

      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += step;
      FREESURFACE(buf);
    }
  }

  if (pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL]) {
    for (i = 0; i < pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL]; i++) {
      buf = adj_surf(get_citizen_surface(CITIZEN_UNHAPPY, i));

      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += step;
      FREESURFACE(buf);
    }
  }

  if (pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]) {
    for (i = 0; i < pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]; i++) {
      buf = adj_surf(get_citizen_surface(CITIZEN_ANGRY, i));

      alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
      dest.x += step;
      FREESURFACE(buf);
    }
  }

  spe_max = specialist_count();
  for (spe = 0; spe < spe_max; spe++) {
    if (pcity->specialists[spe] > 0) {
      buf = adj_surf(get_citizen_surface(CITIZEN_SPECIALIST + spe, i));

      for (i = 0; i < pcity->specialists[spe]; i++) {
        alphablit(buf, NULL, pwindow->dst->surface, &dest, 255);
        dest.x += step;
      }
      FREESURFACE(buf);
    }
  }

  /* ==================================================== */


  switch (pcity_dlg->page) {
  case INFO_PAGE:
    redraw_info_city_dialog(pwindow, pcity);
    break;

  case HAPPINESS_PAGE:
    redraw_happiness_city_dialog(pwindow, pcity);
    break;

  case ARMY_PAGE:
    redraw_army_city_dialog(pwindow, pcity);
    break;

  case SUPPORTED_UNITS_PAGE:
    redraw_supported_units_city_dialog(pwindow, pcity);
    break;

  case MISC_PAGE:
    redraw_misc_city_dialog(pwindow, pcity);
    break;

  default:
    break;

  }

  /* redraw "sell improvement" dialog */
  redraw_group(pcity_dlg->begin_city_menu_widget_list,
               pcity_dlg->end_city_menu_widget_list, 0);

  widget_mark_dirty(pwindow);
}

/* ============================================================== */

/**********************************************************************//**
  Recreate improvement list for city dialog.
**************************************************************************/
static void rebuild_imprm_list(struct city *pcity)
{
  int count = 0;
  struct widget *pwindow = pcity_dlg->end_city_widget_list;
  struct widget *add_dock, *buf, *last;
  SDL_Surface *logo = NULL;
  utf8_str *pstr = NULL;
  struct player *owner = city_owner(pcity);
  int prev_y = 0;

  if (!pcity_dlg->imprv) {
    pcity_dlg->imprv = fc_calloc(1, sizeof(struct advanced_dialog));
  }

  /* free old list */
  if (pcity_dlg->imprv->end_widget_list) {
    del_group_of_widgets_from_gui_list(pcity_dlg->imprv->begin_widget_list,
                                       pcity_dlg->imprv->end_widget_list);
    pcity_dlg->imprv->end_widget_list = NULL;
    pcity_dlg->imprv->begin_widget_list = NULL;
    pcity_dlg->imprv->end_active_widget_list = NULL;
    pcity_dlg->imprv->begin_active_widget_list = NULL;
    pcity_dlg->imprv->active_widget_list = NULL;
    FC_FREE(pcity_dlg->imprv->scroll);
  }

  add_dock = pcity_dlg->add_point;
  buf = last = add_dock;

  /* Alloc new */
  city_built_iterate(pcity, pimprove) {
    pstr = create_utf8_from_char_fonto(
                                 city_improvement_name_translation(pcity, pimprove),
                                 FONTO_DEFAULT);
    pstr->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_IMPR);

    pstr->style |= TTF_STYLE_BOLD;

    logo = get_building_surface(pimprove);
    logo = resize_surface_box(logo, adj_size(22), adj_size(22), 1, TRUE, TRUE);

    buf = create_iconlabel(logo, pwindow->dst, pstr,
                           (WF_FREE_THEME | WF_RESTORE_BACKGROUND));

    buf->size.x = pwindow->size.x + adj_size(450);
    buf->size.y = pwindow->size.y + adj_size(66) + prev_y;

    prev_y += buf->size.h;

    buf->size.w = adj_size(165);
    buf->action = sell_imprvm_dlg_callback;

    if (!pcity_dlg->pcity->did_sell
        && !is_wonder(pimprove) && (owner == client.conn.playing)) {
      set_wstate(buf, FC_WS_NORMAL);
    }

    buf->id = MAX_ID - improvement_number(pimprove) - 3000;
    widget_add_as_prev(buf, add_dock);
    add_dock = buf;

    count++;

    if (count > 7) {
      set_wflag(buf, WF_HIDDEN);
    }

  } city_built_iterate_end;

  if (count) {
    pcity_dlg->imprv->end_widget_list = last->prev;
    pcity_dlg->imprv->end_active_widget_list = pcity_dlg->imprv->end_widget_list;
    pcity_dlg->imprv->begin_widget_list = buf;
    pcity_dlg->imprv->begin_active_widget_list = pcity_dlg->imprv->begin_widget_list;

    if (count > 7) {
      pcity_dlg->imprv->active_widget_list = pcity_dlg->imprv->end_active_widget_list;

      create_vertical_scrollbar(pcity_dlg->imprv, 1, 7, TRUE, TRUE);

      setup_vertical_scrollbar_area(pcity_dlg->imprv->scroll,
                                    pwindow->size.x + adj_size(635),
                                    pwindow->size.y + adj_size(66),
                                    adj_size(155), TRUE);
    }
  }
}

/**********************************************************************//**
  Recreate citydialog title.
**************************************************************************/
static void rebuild_citydlg_title_str(struct widget *pwindow,
                                      struct city *pcity)
{
  char cbuf[512];

  fc_snprintf(cbuf, sizeof(cbuf),
              _("City of %s (Population %s citizens)"),
              city_name_get(pcity),
              population_to_text(city_population(pcity)));

  if (city_unhappy(pcity)) {
    /* TRANS: preserve leading space */
    fc_strlcat(cbuf, _(" - DISORDER"), sizeof(cbuf));
  } else {
    if (city_celebrating(pcity)) {
      /* TRANS: preserve leading space */
      fc_strlcat(cbuf, _(" - celebrating"), sizeof(cbuf));
    } else {
      if (city_happy(pcity)) {
        /* TRANS: preserve leading space */
        fc_strlcat(cbuf, _(" - happy"), sizeof(cbuf));
      }
    }
  }

  if (cma_is_city_under_agent(pcity, NULL)) {
    /* TRANS: preserve leading space */
    fc_strlcat(cbuf, _(" - under Citizen Governor control."), sizeof(cbuf));
  }

  copy_chars_to_utf8_str(pwindow->string_utf8, cbuf);
}


/* ========================= Public ================================== */

/**********************************************************************//**
  Pop up (or bring to the front) a dialog for the given city.  It may or
  may not be modal.
**************************************************************************/
void real_city_dialog_popup(struct city *pcity)
{
  struct widget *pwindow = NULL, *buf = NULL;
  SDL_Surface *logo = NULL;
  utf8_str *pstr = NULL;
  int cs;
  struct player *owner = city_owner(pcity);
  SDL_Rect area;

  if (pcity_dlg) {
    return;
  }

  menus_update();

  pcity_dlg = fc_calloc(1, sizeof(struct city_dialog));
  pcity_dlg->pcity = pcity;
  pcity_dlg->page = ARMY_PAGE;

  pstr = create_utf8_str_fonto(NULL, 0, FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;
  pwindow = create_window(NULL, pstr, adj_size(640), adj_size(480), 0);

  rebuild_citydlg_title_str(pwindow, pcity);

  pwindow->action = city_dlg_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_WINDOW, pwindow);

  pcity_dlg->end_city_widget_list = pwindow;

  /* create window background */
  logo = theme_get_background(active_theme, BACKGROUND_CITYDLG);
  if (resize_window(pwindow, logo, NULL, adj_size(640), adj_size(480))) {
    FREESURFACE(logo);
  }

  logo = get_city_gfx();
  alphablit(logo, NULL, pwindow->theme, NULL, 255);

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* ============================================================= */

  /* Close dialog button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  buf->action = exit_city_dlg_callback;
  buf->size.x = area.x + area.w - buf->size.w;
  buf->size.y = pwindow->size.y + adj_size(2);
  buf->key = SDLK_ESCAPE;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_EXIT_BUTTON, buf);

  /* -------- */

  buf = create_themeicon(current_theme->army_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Present units"),
                                                FONTO_ATTENTION);
  buf->action = army_city_dlg_callback;
  buf->size.x = area.x + adj_size(2) + ((adj_size(183) - 5 * buf->size.w) / 6);
  buf->size.y = area.y + adj_size(2);
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_ARMY_BUTTON, buf);
  /* -------- */

  buf = create_themeicon(current_theme->support_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Supported units"),
                                                FONTO_ATTENTION);
  buf->action = supported_unit_city_dlg_callback;
  buf->size.x =
      area.x + adj_size(2) + 2 * ((adj_size(183) - 5 * buf->size.w) / 6) + buf->size.w;
  buf->size.y = area.y + adj_size(2);

  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_SUPPORT_BUTTON, buf);
  /* -------- */

  buf = create_themeicon(current_theme->happy_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Happiness"),
                                                FONTO_ATTENTION);
  buf->action = happy_city_dlg_callback;
  buf->size.x
    = area.x + adj_size(2) + 3 * ((adj_size(183) - 5 * buf->size.w) / 6) + 2 * buf->size.w;
  buf->size.y = area.y + adj_size(2);
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_HAPPY_BUTTON, buf);
  /* -------- */

  buf = create_themeicon(current_theme->info_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("City info"),
                                                FONTO_ATTENTION);
  buf->action = info_city_dlg_callback;
  buf->size.x
    = area.x + adj_size(4) + 4 * ((adj_size(183) - 5 * buf->size.w) / 6) + 3 * buf->size.w;
  buf->size.y = area.y + adj_size(2);
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_INFO_BUTTON, buf);

  pcity_dlg->add_point = buf;
  pcity_dlg->begin_city_widget_list = buf;
  /* ===================================================== */
  rebuild_imprm_list(pcity);
  /* ===================================================== */

  logo = get_scaled_city_map(pcity);

  buf = create_themelabel(logo, pwindow->dst, NULL,
                          logo->w, logo->h, WF_SELECT_WITHOUT_BAR);

  pcity_dlg->resource_map = buf;

  buf->action = resource_map_city_dlg_callback;
  if (!cma_is_city_under_agent(pcity, NULL) && (owner == client.conn.playing)) {
    set_wstate(buf, FC_WS_NORMAL);
  }
  buf->size.x = area.x + adj_size(193) + (adj_size(249) - buf->size.w) / 2;
  buf->size.y = area.y + adj_size(41) + (adj_size(158) - buf->size.h) / 2;
  add_to_gui_list(ID_CITY_DLG_RESOURCE_MAP, buf);
  /* -------- */

  buf = create_themeicon(current_theme->options_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("City options"),
                                                FONTO_ATTENTION);
  buf->action = options_city_dlg_callback;
  buf->size.x
    = area.x + adj_size(4) + 5 * ((adj_size(183) - 5 * buf->size.w) / 6) + 4 * buf->size.w;
  buf->size.y = area.y + adj_size(2);
  if (owner == client.conn.playing) {
    set_wstate(buf, FC_WS_NORMAL);
  }
  add_to_gui_list(ID_CITY_DLG_OPTIONS_BUTTON, buf);
  /* -------- */

  buf = create_themeicon(current_theme->prod_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Change production"),
                                                FONTO_ATTENTION);
  buf->action = change_prod_dlg_callback;
  buf->size.x = area.x + adj_size(7);
  buf->size.y = area.y + area.h - buf->size.h - adj_size(5);
  if (owner == client.conn.playing) {
    set_wstate(buf, FC_WS_NORMAL);
  }
  buf->key = SDLK_C;
  add_to_gui_list(ID_CITY_DLG_CHANGE_PROD_BUTTON, buf);
  /* -------- */

  buf = create_themeicon(current_theme->buy_prod_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Hurry production"),
                                                FONTO_ATTENTION);
  buf->action = buy_prod_city_dlg_callback;
  buf->size.x = area.x + adj_size(7) + (buf->size.w + adj_size(2));
  buf->size.y = area.y + area.h - buf->size.h - adj_size(5);
  pcity_dlg->buy_button = buf;
  buf->key = SDLK_H;
  if (city_can_buy(pcity)) {
    set_wstate(buf, FC_WS_NORMAL);
  }
  add_to_gui_list(ID_CITY_DLG_PROD_BUY_BUTTON, buf);
  /* -------- */

  buf = create_themeicon(current_theme->cma_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Citizen Governor"),
                                                FONTO_ATTENTION);
  buf->action = cma_city_dlg_callback;
  buf->key = SDLK_A;
  buf->size.x = area.x + adj_size(7) + (buf->size.w + adj_size(2)) * 2;
  buf->size.y = area.y + area.h - buf->size.h - adj_size(5);
  if (owner == client.conn.playing) {
    set_wstate(buf, FC_WS_NORMAL);
  }
  add_to_gui_list(ID_CITY_DLG_CMA_BUTTON, buf);


  /* -------- */
  buf = create_themeicon(current_theme->l_arrow_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Previous city"),
                                                FONTO_ATTENTION);
  buf->action = next_prev_city_dlg_callback;
  buf->size.x = area.x + adj_size(220) - buf->size.w - adj_size(8);
  buf->size.y = area.y + area.h - buf->size.h;
  if (owner == client.conn.playing) {
    set_wstate(buf, FC_WS_NORMAL);
  }
  buf->key = SDLK_LEFT;
  buf->mod = SDL_KMOD_LSHIFT;
  add_to_gui_list(ID_CITY_DLG_PREV_BUTTON, buf);
  /* -------- */

  buf = create_themeicon(current_theme->r_arrow_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Next city"),
                                                FONTO_ATTENTION);
  buf->action = next_prev_city_dlg_callback;
  buf->size.x = area.x + adj_size(420) + adj_size(2);
  buf->size.y = area.y + area.h - buf->size.h;
  if (owner == client.conn.playing) {
    set_wstate(buf, FC_WS_NORMAL);
  }
  buf->key = SDLK_RIGHT;
  buf->mod = SDL_KMOD_LSHIFT;
  add_to_gui_list(ID_CITY_DLG_NEXT_BUTTON, buf);
  /* -------- */

  buf = create_edit_from_chars_fonto(NULL, pwindow->dst, city_name_get(pcity),
                                     FONTO_DEFAULT, adj_size(200),
                                     WF_RESTORE_BACKGROUND);
  buf->action = new_name_city_dlg_callback;
  buf->size.x = area.x + (area.w - buf->size.w) / 2;
  buf->size.y = area.y + area.h - buf->size.h - adj_size(2);
  if (owner == client.conn.playing) {
    set_wstate(buf, FC_WS_NORMAL);
  }

  pcity_dlg->city_name_edit = buf;
  add_to_gui_list(ID_CITY_DLG_NAME_EDIT, buf);

  pcity_dlg->begin_city_widget_list = buf;

  /* check if Citizen Icons style was loaded */
  cs = style_of_city(pcity);

  if (cs != icons->style) {
    reload_citizens_icons(cs);
  }

  /* ===================================================== */
  if ((city_unhappy(pcity) || city_celebrating(pcity)
       || city_happy(pcity))) {
    sdl3_client_flags |= CF_CITY_STATUS_SPECIAL;
  }
  /* ===================================================== */

  redraw_city_dialog(pcity);
  flush_dirty();
}

/**********************************************************************//**
  Close the dialog for the given city.
**************************************************************************/
void popdown_city_dialog(struct city *pcity)
{
  if (city_dialog_is_open(pcity)) {
    del_city_dialog();

    flush_dirty();

    sdl3_client_flags &= ~CF_CITY_STATUS_SPECIAL;
    menus_update();
  }
}

/**********************************************************************//**
  Close all cities dialogs.
**************************************************************************/
void popdown_all_city_dialogs(void)
{
  if (pcity_dlg) {
    popdown_city_dialog(pcity_dlg->pcity);
  }
}

/**********************************************************************//**
  Refresh (update) all data for the given city's dialog.
**************************************************************************/
void real_city_dialog_refresh(struct city *pcity)
{
  if (city_dialog_is_open(pcity)) {
    redraw_city_dialog(pcity_dlg->pcity);
    flush_dirty();
  }
}

/**********************************************************************//**
  Update city dialogs when the given unit's status changes.  This
  typically means updating both the unit's home city (if any) and the
  city in which it is present (if any).
**************************************************************************/
void refresh_unit_city_dialogs(struct unit *punit)
{
  struct city *city_sup = game_city_by_number(punit->homecity);
  struct city *city_pre = tile_city(unit_tile(punit));

  if (pcity_dlg && ((pcity_dlg->pcity == city_sup)
                   || (pcity_dlg->pcity == city_pre))) {
    free_city_units_lists();
    redraw_city_dialog(pcity_dlg->pcity);
    flush_dirty();
  }
}

/**********************************************************************//**
  Return whether the dialog for the given city is open.
**************************************************************************/
bool city_dialog_is_open(struct city *pcity)
{
  return (pcity_dlg && (pcity_dlg->pcity == pcity));
}
