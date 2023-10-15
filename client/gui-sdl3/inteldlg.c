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

/* SDL3 */
#include <SDL3/SDL.h>

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "government.h"
#include "nation.h"
#include "research.h"

/* client */
#include "client_main.h"
#include "spaceshipdlg_g.h"

/* gui-sdl3 */
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "repodlgs.h"
#include "sprite.h"
#include "widget.h"

#include "inteldlg.h"

struct intel_dialog {
  struct player *pplayer;
  struct advanced_dialog *pdialog;
  int pos_x, pos_y;
};

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct intel_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct intel_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list *dialog_list;
static struct intel_dialog *create_intel_dialog(struct player *p);

/**********************************************************************//**
  Allocate intelligence dialog
**************************************************************************/
void intel_dialog_init(void)
{
  dialog_list = dialog_list_new();
}

/**********************************************************************//**
  Free intelligence dialog
**************************************************************************/
void intel_dialog_done(void)
{
  dialog_list_destroy(dialog_list);
}

/**********************************************************************//**
  Get intelligence dialog towards given player
**************************************************************************/
static struct intel_dialog *get_intel_dialog(struct player *pplayer)
{
  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pplayer == pplayer) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  User interacted with the intelligence dialog window
**************************************************************************/
static int intel_window_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct intel_dialog *selected_dialog = get_intel_dialog(pwindow->data.player);

    move_window_group(selected_dialog->pdialog->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with tech widget
**************************************************************************/
static int tech_callback(struct widget *pwidget)
{
  /* get tech help - PORT ME */
  return -1;
}

/**********************************************************************//**
  User interacted with spaceship widget
**************************************************************************/
static int spaceship_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct player *pplayer = pwidget->data.player;

    popdown_intel_dialog(pplayer);
    popup_spaceship_dialog(pplayer);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with intelligence dialog close button
**************************************************************************/
static int exit_intel_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_intel_dialog(pwidget->data.player);
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Close an intelligence dialog towards given player.
**************************************************************************/
void close_intel_dialog(struct player *p)
{
  popdown_intel_dialog(p);
}

/**********************************************************************//**
  Create an intelligence dialog towards given player.
**************************************************************************/
static struct intel_dialog *create_intel_dialog(struct player *pplayer)
{
  struct intel_dialog *pdialog = fc_calloc(1, sizeof(struct intel_dialog));

  pdialog->pplayer = pplayer;

  pdialog->pdialog = fc_calloc(1, sizeof(struct advanced_dialog));

  pdialog->pos_x = 0;
  pdialog->pos_y = 0;

  dialog_list_prepend(dialog_list, pdialog);

  return pdialog;
}

/**********************************************************************//**
  Popup an intelligence dialog for the given player.
**************************************************************************/
void popup_intel_dialog(struct player *p)
{
  struct intel_dialog *pdialog = get_intel_dialog(p);

  if (pdialog == NULL) {
    create_intel_dialog(p);
  } else {
    /* Bring existing dialog to front */
    select_window_group_dialog(pdialog->pdialog->begin_widget_list,
                               pdialog->pdialog->end_widget_list);
  }

  update_intel_dialog(p);
}

/**********************************************************************//**
  Popdown an intelligence dialog for the given player.
**************************************************************************/
void popdown_intel_dialog(struct player *p)
{
  struct intel_dialog *pdialog = get_intel_dialog(p);

  if (pdialog) {
    popdown_window_group_dialog(pdialog->pdialog->begin_widget_list,
                                pdialog->pdialog->end_widget_list);

    dialog_list_remove(dialog_list, pdialog);

    FC_FREE(pdialog->pdialog->scroll);
    FC_FREE(pdialog->pdialog);
    FC_FREE(pdialog);
  }
}

/**********************************************************************//**
  Popdown all intelligence dialogs
**************************************************************************/
void popdown_intel_dialogs(void)
{
  dialog_list_iterate(dialog_list, pdialog) {
    popdown_intel_dialog(pdialog->pplayer);
  } dialog_list_iterate_end;
}

/**********************************************************************//**
  Update the intelligence dialog for the given player. This is called by
  the core client code when that player's information changes.
**************************************************************************/
void update_intel_dialog(struct player *p)
{
  const struct research *mresearch, *presearch;
  struct intel_dialog *pdialog = get_intel_dialog(p);
  struct widget *pwindow = NULL, *buf = NULL, *last;
  SDL_Surface *logo = NULL, *tmp_surf = NULL;
  SDL_Surface *text1, *info, *text2 = NULL, *text3 = NULL;
  utf8_str *pstr;
  SDL_Rect dst;
  char cbuf[2560], plr_buf[4 * MAX_LEN_NAME];
  int ntech = 0, count = 0, col;
  int nwonder = 0;
  struct city *pcapital;
  SDL_Rect area;
  struct research *research;

  if (pdialog) {
    /* save window position and delete old content */
    if (pdialog->pdialog->end_widget_list) {
      pdialog->pos_x = pdialog->pdialog->end_widget_list->size.x;
      pdialog->pos_y = pdialog->pdialog->end_widget_list->size.y;

      popdown_window_group_dialog(pdialog->pdialog->begin_widget_list,
                                  pdialog->pdialog->end_widget_list);
    }

    pstr = create_utf8_from_char_fonto(_("Foreign Intelligence Report"),
                                       FONTO_ATTENTION);
    pstr->style |= TTF_STYLE_BOLD;

    pwindow = create_window_skeleton(NULL, pstr, 0);

    pwindow->action = intel_window_dlg_callback;
    set_wstate(pwindow , FC_WS_NORMAL);
    pwindow->data.player = p;

    add_to_gui_list(ID_WINDOW, pwindow);
    pdialog->pdialog->end_widget_list = pwindow;

    area = pwindow->area;

    /* ---------- */
    /* Exit button */
    buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                           WF_WIDGET_HAS_INFO_LABEL
                           | WF_RESTORE_BACKGROUND);
    buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                  FONTO_ATTENTION);
    area.w = MAX(area.w, buf->size.w + adj_size(10));
    buf->action = exit_intel_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->data.player = p;
    buf->key = SDLK_ESCAPE;

    add_to_gui_list(ID_BUTTON, buf);
    /* ---------- */

    logo = get_nation_flag_surface(nation_of_player(p));

    {
      double zoom = DEFAULT_ZOOM * 60.0 / logo->h;

      text1 = zoomSurface(logo, zoom, zoom, 1);
    }

    logo = text1;

    buf = create_icon2(logo, pwindow->dst,
                        WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL
                        | WF_FREE_THEME);
    buf->action = spaceship_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->data.player = p;
    fc_snprintf(cbuf, sizeof(cbuf),
                _("Intelligence Information about the %s Spaceship"),
                nation_adjective_for_player(p));
    buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);

    add_to_gui_list(ID_ICON, buf);

    /* ---------- */
    fc_snprintf(cbuf, sizeof(cbuf),
                _("Intelligence Information for the %s Empire"),
                nation_adjective_for_player(p));

    pstr = create_utf8_from_char_fonto(cbuf, FONTO_HEADING);
    pstr->style |= TTF_STYLE_BOLD;
    pstr->bgcol = (SDL_Color) {0, 0, 0, 0};

    text1 = create_text_surf_from_utf8(pstr);
    area.w = MAX(area.w, text1->w + adj_size(20));
    area.h += text1->h + adj_size(20);

    /* ---------- */

    pcapital = player_primary_capital(p);
    research = research_get(p);
    change_fonto_utf8(pstr, FONTO_DEFAULT);
    pstr->style &= ~TTF_STYLE_BOLD;

    /* FIXME: these should use common gui code, and avoid duplication! */
    if (client_is_global_observer() || team_has_embassy(client_player()->team, p)) {
      switch (research->researching) {
      case A_UNKNOWN:
      case A_UNSET:
        fc_snprintf(cbuf, sizeof(cbuf),
                    _("Ruler: %s  Government: %s\n"
                      "Capital: %s  Gold: %d\n"
                      "Tax: %d%% Science: %d%% Luxury: %d%%\n"
                      "Researching: %s\n"
                      "Culture: %d"),
                    ruler_title_for_player(p, plr_buf, sizeof(plr_buf)),
                    government_name_for_player(p),
                    /* TRANS: "unknown" location */
                    NULL != pcapital ? city_name_get(pcapital) : _("(Unknown)"),
                    p->economic.gold, p->economic.tax,
                    p->economic.science, p->economic.luxury,
                    research->researching == A_UNSET ? _("(none)") : _("(Unknown)"),
                    p->client.culture);
        break;
      default:
        fc_snprintf(cbuf, sizeof(cbuf),
                    _("Ruler: %s  Government: %s\n"
                      "Capital: %s  Gold: %d\n"
                      "Tax: %d%% Science: %d%% Luxury: %d%%\n"
                      "Researching: %s(%d/%d)\n"
                      "Culture: %d"),
                    ruler_title_for_player(p, plr_buf, sizeof(plr_buf)),
                    government_name_for_player(p),
                    /* TRANS: "unknown" location */
                    NULL != pcapital ? city_name_get(pcapital) : _("(Unknown)"),
                    p->economic.gold, p->economic.tax, p->economic.science,
                    p->economic.luxury,
                    research_advance_name_translation(research,
                                                      research->researching),
                    research->bulbs_researched,
                    research->client.researching_cost, p->client.culture);
        break;
      };
    } else {
      fc_snprintf(cbuf, sizeof(cbuf),
                  _("Ruler: %s  Government: %s\n"
                    "Capital: %s  Gold: %d\n"
                    "Tax rates unknown\n"
                    "Researching: (Unknown)\n"
                    "Culture: (Unknown)"),
                    ruler_title_for_player(p, plr_buf, sizeof(plr_buf)),
                    government_name_for_player(p),
                    /* TRANS: "unknown" location */
                    NULL != pcapital ? city_name_get(pcapital) : _("(Unknown)"),
                    p->economic.gold);
    }

    copy_chars_to_utf8_str(pstr, cbuf);
    info = create_text_surf_from_utf8(pstr);
    area.w = MAX(area.w, logo->w + adj_size(10) + info->w + adj_size(20));

    /* ---------- */
    tmp_surf = get_tech_icon(A_FIRST);
    col = area.w / (tmp_surf->w + adj_size(4));
    FREESURFACE(tmp_surf);
    ntech = 0;
    last = buf;
    mresearch = research_get(client_player());
    presearch = research_get(p);
    advance_index_iterate(A_FIRST, i) {
      if (TECH_KNOWN == research_invention_state(presearch, i)
          && research_invention_reachable(mresearch, i)
          && TECH_KNOWN != research_invention_state(mresearch, i)) {

        buf = create_icon2(get_tech_icon(i), pwindow->dst,
                            WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL
                            | WF_FREE_THEME);
        buf->action = tech_callback;
        set_wstate(buf, FC_WS_NORMAL);

        buf->info_label
          = create_utf8_from_char_fonto(advance_name_translation
                                        (advance_by_number(i)),
                                        FONTO_ATTENTION);

        add_to_gui_list(ID_ICON, buf);

        if (ntech > ((2 * col) - 1)) {
          set_wflag(buf, WF_HIDDEN);
        }

        ntech++;
      }
    } advance_index_iterate_end;

    pdialog->pdialog->begin_widget_list = buf;

    if (ntech > 0) {
      pdialog->pdialog->end_active_widget_list = last->prev;
      pdialog->pdialog->begin_active_widget_list = pdialog->pdialog->begin_widget_list;
      if (ntech > 2 * col) {
        pdialog->pdialog->active_widget_list = pdialog->pdialog->end_active_widget_list;
        count = create_vertical_scrollbar(pdialog->pdialog, col, 2, TRUE, TRUE);
        area.h += (2 * buf->size.h + adj_size(10));
      } else {
        count = 0;
        if (ntech > col) {
          area.h += buf->size.h;
        }
        area.h += (adj_size(10) + buf->size.h);
      }

      area.w = MAX(area.w, col * buf->size.w + count);

      fc_snprintf(cbuf, sizeof(cbuf), _("Their techs that we don't have :"));
      copy_chars_to_utf8_str(pstr, cbuf);
      pstr->style |= TTF_STYLE_BOLD;
      text2 = create_text_surf_from_utf8(pstr);
    }

    nwonder = 0;
    cbuf[0] = '\0';
    improvement_iterate(impr) {
      if (is_wonder(impr)) {
        const char *cityname;
        const char *notes = NULL;

        if (wonder_is_built(p, impr)) {
          struct city *wcity = city_from_wonder(p, impr);

          if (wcity != NULL) {
            cityname = city_name_get(wcity);
          } else {
            cityname = _("(unknown city)");
          }
          if (improvement_obsolete(p, impr, NULL)) {
            notes = _(" (obsolete)");
          }
        } else if (wonder_is_lost(p, impr)) {
          cityname = _("(lost)");
        } else {
          continue;
        }

        cat_snprintf(cbuf, sizeof(cbuf), "%s: %s%s\n",
                     improvement_name_translation(impr), cityname,
                     notes != NULL ? notes : "");

        nwonder++;
      }
    } improvement_iterate_end;

    if (nwonder > 0) {
      copy_chars_to_utf8_str(pstr, cbuf);
      text3 = create_text_surf_from_utf8(pstr);
      area.h += MAX(logo->h + adj_size(20), info->h + text3->h + adj_size(20));
    } else {
      area.h += MAX(logo->h + adj_size(20), info->h + adj_size(20));
    }

    FREEUTF8STR(pstr);

    resize_window(pwindow, NULL, NULL,
                  (pwindow->size.w - pwindow->area.w) + area.w,
                  (pwindow->size.h - pwindow->area.h) + area.h);

    area = pwindow->area;

    /* ------------------------ */
    widget_set_position(pwindow,
      (pdialog->pos_x) ? (pdialog->pos_x) : ((main_window_width() - pwindow->size.w) / 2),
      (pdialog->pos_y) ? (pdialog->pos_y) : ((main_window_height() - pwindow->size.h) / 2));

    /* exit button */
    buf = pwindow->prev;
    buf->size.x = area.x + area.w - buf->size.w - 1;
    buf->size.y = pwindow->size.y + adj_size(2);

    dst.x = area.x + (area.w - text1->w) / 2;
    dst.y = area.y + adj_size(8);

    alphablit(text1, NULL, pwindow->theme, &dst, 255);
    dst.y += text1->h + adj_size(10);
    FREESURFACE(text1);

    /* spaceship button */
    buf = buf->prev;
    dst.x = area.x + (area.w - (buf->size.w + adj_size(10) + info->w)) / 2;
    buf->size.x = dst.x;
    buf->size.y = dst.y;

    dst.x += buf->size.w + adj_size(10);
    alphablit(info, NULL, pwindow->theme, &dst, 255);
    dst.y += info->h + adj_size(10);
    FREESURFACE(info);

    /* --------------------- */

    if (ntech > 0) {
      dst.x = area.x + adj_size(5);
      alphablit(text2, NULL, pwindow->theme, &dst, 255);
      dst.y += text2->h + adj_size(2);
      FREESURFACE(text2);
    }

    if (nwonder > 0) {
      dst.x = area.x + adj_size(5);
      alphablit(text3, NULL, pwindow->theme, &dst, 255);
      dst.y += text3->h + adj_size(2);
      FREESURFACE(text3);
    }

    if (ntech > 0 || nwonder > 0) {
      setup_vertical_widgets_position(col, area.x, dst.y, 0, 0,
                                      pdialog->pdialog->begin_active_widget_list,
                                      pdialog->pdialog->end_active_widget_list);

      if (pdialog->pdialog->scroll) {
        setup_vertical_scrollbar_area(pdialog->pdialog->scroll,
                                      area.x + area.w, dst.y,
                                      area.h - (dst.y + 1), TRUE);
      }
    }

    redraw_group(pdialog->pdialog->begin_widget_list, pdialog->pdialog->end_widget_list, 0);
    widget_mark_dirty(pwindow);

    flush_dirty();
  }
}
