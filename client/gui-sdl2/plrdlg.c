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
#include "astring.h"
#include "fcintl.h"

/* common */
#include "nation.h"

/* client */
#include "client_main.h"
#include "climisc.h"

/* gui-sdl2 */
#include "chatline.h"
#include "colors.h"
#include "diplodlg.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "inteldlg.h"
#include "mapview.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"

#include "plrdlg.h"

#ifndef M_PI
#define M_PI		3.14159265358979323846  /* pi */
#endif
#ifndef M_PI_2
#define M_PI_2		1.57079632679489661923  /* pi/2 */
#endif

static struct small_dialog *pplayers_dlg = NULL;

static int player_nation_callback(struct widget *pwidget);

/**********************************************************************//**
  User interacted with player dialog close button.
**************************************************************************/
static int exit_players_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_players_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with player widget.
**************************************************************************/
static int player_callback(struct widget *pwidget)
{
  /* We want exactly same functionality that the small dialog has. */
  return player_nation_callback(pwidget);
}

/**********************************************************************//**
  User interacted with player dialog window.
**************************************************************************/
static int players_window_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (move_window_group_dialog(pplayers_dlg->begin_widget_list, pwindow)) {
      select_window_group_dialog(pplayers_dlg->begin_widget_list, pwindow);
      players_dialog_update();
    } else {
      if (select_window_group_dialog(pplayers_dlg->begin_widget_list, pwindow)) {
        widget_flush(pwindow);
      }
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with 'draw war status' toggle.
**************************************************************************/
static int toggle_draw_war_status_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pplayer = pplayers_dlg->end_widget_list->prev->prev->prev->prev->prev->prev;

    sdl2_client_flags ^= CF_DRAW_PLAYERS_WAR_STATUS;
    do {
      pplayer = pplayer->prev;
      FREESURFACE(pplayer->gfx);
    } while (pplayer != pplayers_dlg->begin_widget_list);

    players_dialog_update();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with 'draw cease-fire status' toggle.
**************************************************************************/
static int toggle_draw_ceasefire_status_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pplayer = pplayers_dlg->end_widget_list->prev->prev->prev->prev->prev->prev;

    sdl2_client_flags ^= CF_DRAW_PLAYERS_CEASEFIRE_STATUS;
    do {
      pplayer = pplayer->prev;
      FREESURFACE(pplayer->gfx);
    } while (pplayer != pplayers_dlg->begin_widget_list);

    players_dialog_update();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with 'draw peace status' toggle.
**************************************************************************/
static int toggle_draw_peace_status_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pplayer = pplayers_dlg->end_widget_list->prev->prev->prev->prev->prev->prev;

    sdl2_client_flags ^= CF_DRAW_PLAYERS_PEACE_STATUS;
    do {
      pplayer = pplayer->prev;
      FREESURFACE(pplayer->gfx);
    } while (pplayer != pplayers_dlg->begin_widget_list);

    players_dialog_update();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with 'draw alliance status' toggle.
**************************************************************************/
static int toggle_draw_alliance_status_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pplayer = pplayers_dlg->end_widget_list->prev->prev->prev->prev->prev->prev;

    sdl2_client_flags ^= CF_DRAW_PLAYERS_ALLIANCE_STATUS;
    do {
      pplayer = pplayer->prev;
      FREESURFACE(pplayer->gfx);
    } while (pplayer != pplayers_dlg->begin_widget_list);

    players_dialog_update();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with 'draw neutral status' toggle.
**************************************************************************/
static int toggle_draw_neutral_status_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pplayer = pplayers_dlg->end_widget_list->prev->prev->prev->prev->prev->prev;

    sdl2_client_flags ^= CF_DRAW_PLAYERS_NEUTRAL_STATUS;
    do {
      pplayer = pplayer->prev;
      FREESURFACE(pplayer->gfx);
    } while (pplayer != pplayers_dlg->begin_widget_list);

    players_dialog_update();
  }

  return -1;
}

/**********************************************************************//**
  Does the attached player have embassy-level information about the player.
**************************************************************************/
static bool have_diplomat_info_about(struct player *pplayer)
{
  return (pplayer == client.conn.playing
          || (pplayer != client.conn.playing
              && team_has_embassy(client.conn.playing->team, pplayer)));
}

/**********************************************************************//**
  Update all information in the player list dialog.
**************************************************************************/
void real_players_dialog_update(void *unused)
{
  if (pplayers_dlg) {
    struct widget *pplayer0, *pplayer1;
    struct player *pplayer;
    int i;
    struct astring astr = ASTRING_INIT;

    /* redraw window */
    widget_redraw(pplayers_dlg->end_widget_list);

    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    pplayer0 = pplayers_dlg->end_widget_list->prev->prev->prev->prev->prev->prev;
    do {
      pplayer0 = pplayer0->prev;
      pplayer1 = pplayer0;
      pplayer = pplayer0->data.player;

      for (i = 0; i < num_player_dlg_columns; i++) {
        if (player_dlg_columns[i].show) {
          switch (player_dlg_columns[i].type) {
          case COL_TEXT:
          case COL_RIGHT_TEXT:
            astr_add_line(&astr, "%s: %s", player_dlg_columns[i].title,
                          player_dlg_columns[i].func(pplayer));
            break;
          case COL_BOOLEAN:
            astr_add_line(&astr, "%s: %s", player_dlg_columns[i].title,
                          player_dlg_columns[i].bool_func(pplayer) ?
                          _("Yes") : _("No"));
            break;
          default:
            break;
          }
        }
      }

      copy_chars_to_utf8_str(pplayer0->info_label, astr_str(&astr));

      astr_free(&astr);

      /* now add some eye candy ... */
      if (pplayer1 != pplayers_dlg->begin_widget_list) {
        SDL_Rect dst0, dst1;

        dst0.x = pplayer0->size.x + pplayer0->size.w / 2;
        dst0.y = pplayer0->size.y + pplayer0->size.h / 2;

        do {
          pplayer1 = pplayer1->prev;
          if (have_diplomat_info_about(pplayer)
              || have_diplomat_info_about(pplayer1->data.player)) {

            dst1.x = pplayer1->size.x + pplayer1->size.w / 2;
            dst1.y = pplayer1->size.y + pplayer1->size.h / 2;

            switch (player_diplstate_get(pplayer,
                                         pplayer1->data.player)->type) {
            case DS_ARMISTICE:
              if (sdl2_client_flags & CF_DRAW_PLAYERS_NEUTRAL_STATUS) {
                create_line(pplayer1->dst->surface,
                            dst0.x, dst0.y, dst1.x, dst1.y,
                            get_theme_color(COLOR_THEME_PLRDLG_ARMISTICE));
              }
              break;
            case DS_WAR:
              if (sdl2_client_flags & CF_DRAW_PLAYERS_WAR_STATUS) {
                create_line(pplayer1->dst->surface,
                            dst0.x, dst0.y, dst1.x, dst1.y,
                            get_theme_color(COLOR_THEME_PLRDLG_WAR));
              }
              break;
            case DS_CEASEFIRE:
              if (sdl2_client_flags & CF_DRAW_PLAYERS_CEASEFIRE_STATUS) {
                create_line(pplayer1->dst->surface,
                            dst0.x, dst0.y, dst1.x, dst1.y,
                            get_theme_color(COLOR_THEME_PLRDLG_CEASEFIRE));
              }
              break;
            case DS_PEACE:
              if (sdl2_client_flags & CF_DRAW_PLAYERS_PEACE_STATUS) {
                create_line(pplayer1->dst->surface,
                            dst0.x, dst0.y, dst1.x, dst1.y,
                            get_theme_color(COLOR_THEME_PLRDLG_PEACE));
              }
              break;
            case DS_ALLIANCE:
              if (sdl2_client_flags & CF_DRAW_PLAYERS_ALLIANCE_STATUS) {
                create_line(pplayer1->dst->surface,
                            dst0.x, dst0.y, dst1.x, dst1.y,
                            get_theme_color(COLOR_THEME_PLRDLG_ALLIANCE));
              }
              break;
            default:
              /* no contact */
              break;
            }
          }
        } while (pplayer1 != pplayers_dlg->begin_widget_list);
      }
    } while (pplayer0 != pplayers_dlg->begin_widget_list);

    /* -------------------- */
    /* redraw */
    redraw_group(pplayers_dlg->begin_widget_list,
                 pplayers_dlg->end_widget_list->prev, 0);
    widget_mark_dirty(pplayers_dlg->end_widget_list);

    flush_dirty();
  }
}

/**********************************************************************//**
  Popup (or raise) the player list dialog.
**************************************************************************/
void popup_players_dialog(bool raise)
{
  struct widget *pwindow = NULL, *buf = NULL;
  SDL_Surface *logo = NULL, *zoomed = NULL;
  utf8_str *pstr;
  SDL_Rect dst;
  int i, n, h;
  double a, b, r;
  SDL_Rect area;

  if (pplayers_dlg) {
    return;
  }

  n = 0;
  players_iterate(pplayer) {
    if (is_barbarian(pplayer)) {
      continue;
    }
    n++;
  } players_iterate_end;

  if (n < 2) {
    return;
  }

  pplayers_dlg = fc_calloc(1, sizeof(struct small_dialog));

  pstr = create_utf8_from_char_fonto(Q_("?header:Players"),
                                     FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = players_window_dlg_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_WINDOW, pwindow);
  pplayers_dlg->end_widget_list = pwindow;
  /* ---------- */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  buf->action = exit_players_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_BUTTON, buf);
  /* ---------- */

  for (i = 0; i < DS_LAST; i++) {
    switch (i) {
      case DS_ARMISTICE:
        buf = create_checkbox(pwindow->dst,
                              (sdl2_client_flags & CF_DRAW_PLAYERS_NEUTRAL_STATUS),
                              WF_RESTORE_BACKGROUND);
        buf->action = toggle_draw_neutral_status_callback;
        buf->key = SDLK_n;
      break;
      case DS_WAR:
        buf = create_checkbox(pwindow->dst,
                              (sdl2_client_flags & CF_DRAW_PLAYERS_WAR_STATUS),
                              WF_RESTORE_BACKGROUND);
        buf->action = toggle_draw_war_status_callback;
        buf->key = SDLK_w;
      break;
      case DS_CEASEFIRE:
        buf = create_checkbox(pwindow->dst,
                              (sdl2_client_flags & CF_DRAW_PLAYERS_CEASEFIRE_STATUS),
                              WF_RESTORE_BACKGROUND);
        buf->action = toggle_draw_ceasefire_status_callback;
        buf->key = SDLK_c;
      break;
      case DS_PEACE:
        buf = create_checkbox(pwindow->dst,
                              (sdl2_client_flags & CF_DRAW_PLAYERS_PEACE_STATUS),
                              WF_RESTORE_BACKGROUND);
        buf->action = toggle_draw_peace_status_callback;
        buf->key = SDLK_p;
      break;
      case DS_ALLIANCE:
        buf = create_checkbox(pwindow->dst,
                              (sdl2_client_flags & CF_DRAW_PLAYERS_ALLIANCE_STATUS),
                              WF_RESTORE_BACKGROUND);
        buf->action = toggle_draw_alliance_status_callback;
        buf->key = SDLK_a;
      break;
      default:
        /* no contact */
        continue;
      break;
    }
    set_wstate(buf, FC_WS_NORMAL);
    add_to_gui_list(ID_CHECKBOX, buf);
  }
  /* ---------- */

  players_iterate(pplayer) {
    if (is_barbarian(pplayer)) {
      continue;
    }

    pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

    logo = get_nation_flag_surface(nation_of_player(pplayer));
    {
      /* Aim for a flag height of 60 pixels, but draw smaller flags if there
       * are more players */
      double zoom = DEFAULT_ZOOM * (60.0 - n) / logo->h;

      zoomed = zoomSurface(logo, zoom, zoom, 1);
    }

    buf = create_icon2(zoomed, pwindow->dst,
                        WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL
                        | WF_FREE_THEME);
    buf->info_label = pstr;

    if (!pplayer->is_alive) {
      pstr = create_utf8_from_char_fonto(_("R.I.P."), FONTO_DEFAULT);
      pstr->style |= TTF_STYLE_BOLD;
      pstr->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_TEXT);
      logo = create_text_surf_from_utf8(pstr);
      FREEUTF8STR(pstr);

      dst.x = (zoomed->w - logo->w) / 2;
      dst.y = (zoomed->h - logo->h) / 2;
      alphablit(logo, NULL, zoomed, &dst, 255);
      FREESURFACE(logo);
    }

    if (pplayer->is_alive) {
      set_wstate(buf, FC_WS_NORMAL);
    }

    buf->data.player = pplayer;

    buf->action = player_callback;

    add_to_gui_list(ID_LABEL, buf);

  } players_iterate_end;

  pplayers_dlg->begin_widget_list = buf;

  resize_window(pwindow, NULL, NULL, adj_size(500), adj_size(400));

  area = pwindow->area;

  r = MIN(area.w, area.h);
  r -= ((MAX(buf->size.w, buf->size.h) * 2));
  r /= 2;
  a = (2.0 * M_PI) / n;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* exit button */
  buf = pwindow->prev;

  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  n = area.y;
  pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
  pstr->style |= TTF_STYLE_BOLD;
  pstr->bgcol = (SDL_Color) {0, 0, 0, 0};

  for (i = 0; i < DS_LAST; i++) {
    switch (i) {
    case DS_ARMISTICE:
      pstr->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_ARMISTICE);
      break;
    case DS_WAR:
      pstr->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_WAR);
      break;
    case DS_CEASEFIRE:
      pstr->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_CEASEFIRE);
      break;
    case DS_PEACE:
      pstr->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_PEACE);
      break;
    case DS_ALLIANCE:
      pstr->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_ALLIANCE);
      break;
    default:
      /* no contact */
      continue;
      break;
    }

    copy_chars_to_utf8_str(pstr, diplstate_type_translated_name(i));
    logo = create_text_surf_from_utf8(pstr);

    buf = buf->prev;
    h = MAX(buf->size.h, logo->h);
    buf->size.x = area.x + adj_size(5);
    buf->size.y = n + (h - buf->size.h) / 2;

    dst.x = adj_size(5) + buf->size.w + adj_size(6);
    dst.y = n + (h - logo->h) / 2;
    alphablit(logo, NULL, pwindow->theme, &dst, 255);
    n += h;
    FREESURFACE(logo);
  }
  FREEUTF8STR(pstr);

  /* first player shield */
  buf = buf->prev;
  buf->size.x = area.x + area.w / 2 - buf->size.w / 2;
  buf->size.y = area.y + area.h / 2 - r - buf->size.h / 2;

  n = 1;
  if (buf != pplayers_dlg->begin_widget_list) {
    do {
      buf = buf->prev;
      b = M_PI_2 + n * a;
      buf->size.x = area.x + area.w / 2 - r * cos(b) - buf->size.w / 2;
      buf->size.y = area.y + area.h / 2 - r * sin(b) - buf->size.h / 2;
      n++;
    } while (buf != pplayers_dlg->begin_widget_list);
  }

  players_dialog_update();
}

/**********************************************************************//**
  Popdown the player list dialog.
**************************************************************************/
void popdown_players_dialog(void)
{
  if (pplayers_dlg) {
    popdown_window_group_dialog(pplayers_dlg->begin_widget_list,
                                pplayers_dlg->end_widget_list);
    FC_FREE(pplayers_dlg);
  }
}


/* ============================== SHORT =============================== */
static struct advanced_dialog *short_players_dlg = NULL;

/**********************************************************************//**
  User interacted with nations window.
**************************************************************************/
static int players_nations_window_dlg_callback(struct widget *pwindow)
{
  return -1;
}

/**********************************************************************//**
  User interacted with nations window close button.
**************************************************************************/
static int exit_players_nations_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_players_nations_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with widget of a single nation/player.
**************************************************************************/
static int player_nation_callback(struct widget *pwidget)
{
  struct player *pplayer = pwidget->data.player;
  bool try_dlg = FALSE;
  bool popdown = FALSE;

  if (main_data.event.type == SDL_MOUSEBUTTONDOWN) {
    switch (main_data.event.button.button) {
#if 0
    case SDL_BUTTON_LEFT:

      break;
    case SDL_BUTTON_MIDDLE:

      break;
#endif /* 0 */
    case SDL_BUTTON_RIGHT:
      popup_intel_dialog(pplayer);

      popdown = TRUE;
      break;
    default:
      try_dlg = TRUE;
      break;
    }
  } else if (PRESSED_EVENT(main_data.event)) {
    try_dlg = TRUE;
  }

  if (try_dlg && pplayer != client_player()) {
    const struct player_diplstate *ds;

    ds = player_diplstate_get(client_player(), pplayer);

    if (ds->type != DS_NO_CONTACT
        && (ds->type != DS_WAR
            || can_meet_with_player(pplayer)
            || can_intel_with_player(pplayer))) {
      popup_diplomacy_dialog(pplayer);

      popdown = TRUE;
    } else {
      set_output_window_text(_("You cannot interact with that player "
                               "with your current contact state - "
                               "Try right mouse button for intelligence."));
    }
  }

  if (popdown) {
    /* We came from one of these dialogs. Just try to pop down both -
     * the one open will react. */
    popdown_players_nations_dialog();
    popdown_players_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Popup (or raise) the small player list dialog version.
**************************************************************************/
void popup_players_nations_dialog(void)
{
  struct widget *pwindow = NULL, *buf = NULL;
  SDL_Surface *logo = NULL;
  utf8_str *pstr;
  char cbuf[128], *state;
  int n = 0, w = 0, units_h = 0;
  SDL_Rect area;

  if (short_players_dlg) {
    return;
  }

  short_players_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  /* TRANS: Nations report title */
  pstr = create_utf8_from_char_fonto(_("Nations"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = players_nations_window_dlg_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_WINDOW, pwindow);
  short_players_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ---------- */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  area.w = MAX(area.w, buf->size.w + adj_size(10));
  buf->action = exit_players_nations_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_BUTTON, buf);
  /* ---------- */

  players_iterate(pplayer) {
    if (pplayer != client.conn.playing) {
      const struct player_diplstate *ds;

      if (!pplayer->is_alive || is_barbarian(pplayer)) {
        continue;
      }

      ds = player_diplstate_get(client.conn.playing, pplayer);

      if (is_ai(pplayer)) {
        state = _("AI");
      } else {
        if (pplayer->is_connected) {
          if (pplayer->phase_done) {
            state = _("done");
          } else {
            state = _("moving");
          }
        } else {
          state = _("disconnected");
        }
      }

      if (ds->type == DS_CEASEFIRE) {
        fc_snprintf(cbuf, sizeof(cbuf), "%s(%s) - %d %s",
                    nation_adjective_for_player(pplayer),
                    state,
                    ds->turns_left, PL_("turn", "turns", ds->turns_left));
      } else {
        fc_snprintf(cbuf, sizeof(cbuf), "%s(%s)",
                    nation_adjective_for_player(pplayer),
                    state);
      }

      pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
      pstr->style |= TTF_STYLE_BOLD;

      logo = get_nation_flag_surface(nation_of_player(pplayer));

      buf = create_iconlabel(logo, pwindow->dst, pstr,
                             (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));

      /* At least RMB works always */
      set_wstate(buf, FC_WS_NORMAL);

      /* Now add some eye candy ... */
      switch (ds->type) {
      case DS_ARMISTICE:
        buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_ARMISTICE);
        break;
      case DS_WAR:
        if (can_meet_with_player(pplayer) || can_intel_with_player(pplayer)) {
          buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_WAR);
        } else {
          buf->string_utf8->fgcol = *(get_theme_color(COLOR_THEME_PLRDLG_WAR_RESTRICTED));
        }
        break;
      case DS_CEASEFIRE:
        buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_CEASEFIRE);
        break;
      case DS_PEACE:
        buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_PEACE);
        break;
      case DS_ALLIANCE:
        buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_PLRDLG_ALLIANCE);
        break;
      case DS_NO_CONTACT:
        buf->string_utf8->fgcol = *(get_theme_color(COLOR_THEME_PLRDLG_NO_CONTACT));
        break;
      case DS_TEAM:
      case DS_LAST:
        break;
      }

      buf->string_utf8->bgcol = (SDL_Color) {0, 0, 0, 0};

      buf->data.player = pplayer;

      buf->action = player_nation_callback;

      add_to_gui_list(ID_LABEL, buf);

      area.w = MAX(w, buf->size.w);
      area.h += buf->size.h;

      if (n > 19) {
        set_wflag(buf, WF_HIDDEN);
      }

      n++;
    }
  } players_iterate_end;

  short_players_dlg->begin_widget_list = buf;
  short_players_dlg->begin_active_widget_list = short_players_dlg->begin_widget_list;
  short_players_dlg->end_active_widget_list = pwindow->prev->prev;
  short_players_dlg->active_widget_list = short_players_dlg->end_active_widget_list;


  /* ---------- */
  if (n > 20) {
    units_h = create_vertical_scrollbar(short_players_dlg, 1, 20, TRUE, TRUE);
    short_players_dlg->scroll->count = n;

    n = units_h;
    area.w += n;

    units_h = 20 * buf->size.h;

  } else {
    units_h = area.h;
  }

  /* ---------- */

  area.h = units_h;

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h + pwindow->area.h) + area.h);

  area = pwindow->area;

  widget_set_position(pwindow,
    ((main_data.event.motion.x + pwindow->size.w + adj_size(10) < main_window_width()) ?
      (main_data.event.motion.x + adj_size(10)) :
      (main_window_width() - pwindow->size.w - adj_size(10))),
    ((main_data.event.motion.y - adj_size(2) + pwindow->size.h < main_window_height()) ?
      (main_data.event.motion.y - adj_size(2)) :
      (main_window_height() - pwindow->size.h - adj_size(10))));

  w = area.w;

  if (short_players_dlg->scroll) {
    w -= n;
  }

  /* exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  /* cities */
  buf = buf->prev;
  setup_vertical_widgets_position(1, area.x, area.y, w, 0,
                                  short_players_dlg->begin_active_widget_list,
                                  buf);

  if (short_players_dlg->scroll) {
    setup_vertical_scrollbar_area(short_players_dlg->scroll,
                                  area.x + area.w, area.y,
                                  area.h, TRUE);
  }

  /* -------------------- */
  /* redraw */
  redraw_group(short_players_dlg->begin_widget_list, pwindow, 0);
  widget_mark_dirty(pwindow);

  flush_dirty();
}

/**********************************************************************//**
  Popdown the short player list dialog version.
**************************************************************************/
void popdown_players_nations_dialog(void)
{
  if (short_players_dlg) {
    popdown_window_group_dialog(short_players_dlg->begin_widget_list,
                                short_players_dlg->end_widget_list);
    FC_FREE(short_players_dlg->scroll);
    FC_FREE(short_players_dlg);
  }
}
