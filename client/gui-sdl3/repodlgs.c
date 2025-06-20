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
                          repodlgs.c  -  description
                             -------------------
    begin                : Nov 15 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "game.h"
#include "government.h"
#include "research.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "text.h"

/* gui-sdl3 */
#include "cityrep.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "helpdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"

#include "repodlgs.h"


/* ===================================================================== */
/* ======================== Active Units Report ======================== */
/* ===================================================================== */
static struct advanced_dialog *units_dlg = NULL;
static struct small_dialog *units_upg_dlg = NULL;

struct units_entry {
  int active_count;
  int upkeep[O_LAST];
  int building_count;
  int soonest_completions;
};

/**********************************************************************//**
  Fill unit types specific report data + totals.
**************************************************************************/
static void get_units_report_data(struct units_entry *entries,
                                  struct units_entry *total)
{
  int time_to_build;

  memset(entries, '\0', U_LAST * sizeof(struct units_entry));
  memset(total, '\0', sizeof(struct units_entry));
  for (time_to_build = 0; time_to_build < U_LAST; time_to_build++) {
    entries[time_to_build].soonest_completions = FC_INFINITY;
  }

  unit_list_iterate(client.conn.playing->units, punit) {
    Unit_type_id uti = utype_index(unit_type_get(punit));

    (entries[uti].active_count)++;
    (total->active_count)++;
    if (punit->homecity) {
      output_type_iterate(o) {
        entries[uti].upkeep[o] += punit->upkeep[o];
        total->upkeep[o] += punit->upkeep[o];
      } output_type_iterate_end;
    }
  } unit_list_iterate_end;

  city_list_iterate(client.conn.playing->cities, pcity) {
    if (VUT_UTYPE == pcity->production.kind) {
      const struct unit_type *punittype = pcity->production.value.utype;
      Unit_type_id uti = utype_index(punittype);
      int num_units;

      /* Account for build slots in city */
      (void) city_production_build_units(pcity, TRUE, &num_units);
      /* Unit is in progress even if it won't be done this turn */
      num_units = MAX(num_units, 1);
      (entries[uti].building_count) += num_units;
      (total->building_count) += num_units;
      entries[uti].soonest_completions =
        MIN(entries[uti].soonest_completions,
            city_production_turns_to_build(pcity, TRUE));
    }
  } city_list_iterate_end;
}

/**********************************************************************//**
  User interacted with Units Report button.
**************************************************************************/
static int units_dialog_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(units_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/* --------------------------------------------------------------- */

/**********************************************************************//**
  User interacted with accept button of the unit upgrade dialog.
**************************************************************************/
static int ok_upgrade_unit_window_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int ut1 = MAX_ID - pwidget->id;

    /* popdown upgrade dlg */
    popdown_window_group_dialog(units_upg_dlg->begin_widget_list,
                                units_upg_dlg->end_widget_list);
    FC_FREE(units_upg_dlg);

    dsend_packet_unit_type_upgrade(&client.conn, ut1);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Upgrade Obsolete button of the unit upgrade dialog.
**************************************************************************/
static int upgrade_unit_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(units_upg_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Cancel button of the unit upgrade dialog.
**************************************************************************/
static int cancel_upgrade_unit_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (units_upg_dlg) {
      popdown_window_group_dialog(units_upg_dlg->begin_widget_list,
                                  units_upg_dlg->end_widget_list);
      FC_FREE(units_upg_dlg);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  Open dialog for upgrading units.
**************************************************************************/
static int popup_upgrade_unit_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit_type *ut1;
    const struct unit_type *ut2;
    int value;
    char tBuf[128], cbuf[128];
    struct widget *buf = NULL, *pwindow;
    utf8_str *pstr;
    SDL_Surface *text;
    SDL_Rect dst;
    SDL_Rect area;

    ut1 = utype_by_number(MAX_ID - pwidget->id);

    if (units_upg_dlg) {
      return 1;
    }

    set_wstate(pwidget, FC_WS_NORMAL);
    selected_widget = NULL;
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);

    units_upg_dlg = fc_calloc(1, sizeof(struct small_dialog));

    ut2 = can_upgrade_unittype(client.conn.playing, ut1);
    value = unit_upgrade_price(client.conn.playing, ut1, ut2);

    fc_snprintf(tBuf, ARRAY_SIZE(tBuf), PL_("Treasury contains %d gold.",
                                            "Treasury contains %d gold.",
                                            client_player()->economic.gold),
                client_player()->economic.gold);

    fc_snprintf(cbuf, sizeof(cbuf),
          /* TRANS: Last %s is pre-pluralised "Treasury contains %d gold." */
          PL_("Upgrade as many %s to %s as possible for %d gold each?\n%s",
              "Upgrade as many %s to %s as possible for %d gold each?\n%s",
              value),
          utype_name_translation(ut1),
          utype_name_translation(ut2),
          value, tBuf);

    pstr = create_utf8_from_char_fonto(_("Upgrade Obsolete Units"),
                                       FONTO_ATTENTION);
    pstr->style |= TTF_STYLE_BOLD;

    pwindow = create_window_skeleton(NULL, pstr, 0);

    pwindow->action = upgrade_unit_window_callback;
    set_wstate(pwindow, FC_WS_NORMAL);

    add_to_gui_list(ID_WINDOW, pwindow);

    units_upg_dlg->end_widget_list = pwindow;

    area = pwindow->area;

    /* ============================================================= */

    /* Create text label */
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pstr->fgcol = *get_theme_color(COLOR_THEME_UNITUPGRADE_TEXT);

    text = create_text_surf_from_utf8(pstr);
    FREEUTF8STR(pstr);

    area.h += (text->h + adj_size(10));
    area.w = MAX(area.w, text->w + adj_size(20));

    /* Cancel button */
    buf = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                                   pwindow->dst, _("No"),
                                                   FONTO_ATTENTION, 0);

    buf->action = cancel_upgrade_unit_callback;
    set_wstate(buf, FC_WS_NORMAL);

    area.h += (buf->size.h + adj_size(20));

    add_to_gui_list(ID_BUTTON, buf);

    if (value <= client.conn.playing->economic.gold) {
      buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                     pwindow->dst,
                                                     _("Yes"),
                                                     FONTO_ATTENTION, 0);

      buf->action = ok_upgrade_unit_window_callback;
      set_wstate(buf, FC_WS_NORMAL);

      add_to_gui_list(pwidget->id, buf);
      buf->size.w = MAX(buf->size.w, buf->next->size.w);
      buf->next->size.w = buf->size.w;
      area.w = MAX(area.w, adj_size(30) + buf->size.w * 2);
    } else {
      area.w = MAX(area.w, buf->size.w + adj_size(20));
    }
    /* ============================================ */

    units_upg_dlg->begin_widget_list = buf;

    resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                  (pwindow->size.w - pwindow->area.w) + area.w,
                  (pwindow->size.h - pwindow->area.h) + area.h);

    widget_set_position(pwindow,
                        units_dlg->end_widget_list->size.x +
                          (units_dlg->end_widget_list->size.w - pwindow->size.w) / 2,
                        units_dlg->end_widget_list->size.y +
                          (units_dlg->end_widget_list->size.h - pwindow->size.h) / 2);

    /* setup rest of widgets */
    /* label */
    dst.x = area.x + (area.w - text->w) / 2;
    dst.y = area.y + adj_size(10);
    alphablit(text, NULL, pwindow->theme, &dst, 255);
    FREESURFACE(text);

    /* cancel button */
    buf = pwindow->prev;
    buf->size.y = area.y + area.h - buf->size.h - adj_size(10);

    if (value <= client.conn.playing->economic.gold) {
      /* sell button */
      buf = buf->prev;
      buf->size.x = area.x + (area.w - (2 * buf->size.w + adj_size(10))) / 2;
      buf->size.y = buf->next->size.y;

      /* cancel button */
      buf->next->size.x = buf->size.x + buf->size.w + adj_size(10);
    } else {
      /* x position of cancel button */
      buf->size.x = area.x + area.w - buf->size.w - adj_size(10);
    }

    /* ================================================== */
    /* redraw */
    redraw_group(units_upg_dlg->begin_widget_list, pwindow, 0);

    widget_mark_dirty(pwindow);
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with units dialog Close Dialog button.
**************************************************************************/
static int exit_units_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (units_dlg) {
      if (units_upg_dlg) {
         del_group_of_widgets_from_gui_list(units_upg_dlg->begin_widget_list,
                                            units_upg_dlg->end_widget_list);
         FC_FREE(units_upg_dlg);
      }
      popdown_window_group_dialog(units_dlg->begin_widget_list,
                                  units_dlg->end_widget_list);
      FC_FREE(units_dlg->scroll);
      FC_FREE(units_dlg);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  Rebuild the units report.
**************************************************************************/
static void real_activeunits_report_dialog_update(struct units_entry *units,
                                                  struct units_entry *total)
{
  SDL_Color bg_color = {255, 255, 255, 136};
  struct widget *buf = NULL;
  struct widget *pwindow, *last;
  utf8_str *pstr;
  SDL_Surface *text1, *text2, *text3 , *text4, *text5, *logo;
  int w = 0 , count, ww, hh = 0, name_w = 0;
  char cbuf[64];
  SDL_Rect dst;
  bool upgrade = FALSE;
  SDL_Rect area;

  if (units_dlg) {
    popdown_window_group_dialog(units_dlg->begin_widget_list,
                                units_dlg->end_widget_list);
  } else {
    units_dlg = fc_calloc(1, sizeof(struct advanced_dialog));
  }

  fc_snprintf(cbuf, sizeof(cbuf), _("active"));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= SF_CENTER;
  text1 = create_text_surf_from_utf8(pstr);

  fc_snprintf(cbuf, sizeof(cbuf), _("under\nconstruction"));
  copy_chars_to_utf8_str(pstr, cbuf);
  text2 = create_text_surf_from_utf8(pstr);

  fc_snprintf(cbuf, sizeof(cbuf), _("soonest\ncompletion"));
  copy_chars_to_utf8_str(pstr, cbuf);
  text5 = create_text_surf_from_utf8(pstr);

  fc_snprintf(cbuf, sizeof(cbuf), _("Total"));
  copy_chars_to_utf8_str(pstr, cbuf);
  text3 = create_text_surf_from_utf8(pstr);

  fc_snprintf(cbuf, sizeof(cbuf), _("Units"));
  copy_chars_to_utf8_str(pstr, cbuf);
  text4 = create_text_surf_from_utf8(pstr);
  name_w = text4->w;
  FREEUTF8STR(pstr);

  /* --------------- */
  pstr = create_utf8_from_char_fonto(_("Units Report"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);
  set_wstate(pwindow, FC_WS_NORMAL);
  pwindow->action = units_dialog_callback;

  add_to_gui_list(ID_UNITS_DIALOG_WINDOW, pwindow);

  units_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ------------------------- */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  buf->action = exit_units_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_BUTTON, buf);
  /* ------------------------- */
  /* Totals */
  fc_snprintf(cbuf, sizeof(cbuf), "%d", total->active_count);

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  buf = create_iconlabel(NULL, pwindow->dst, pstr,
                         WF_RESTORE_BACKGROUND);

  area.h += buf->size.h;
  buf->size.w = text1->w + adj_size(6);
  add_to_gui_list(ID_LABEL, buf);
  /* ---------------------------------------------- */
  fc_snprintf(cbuf, sizeof(cbuf), "%d", total->upkeep[O_SHIELD]);

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

  buf->size.w = text1->w;
  add_to_gui_list(ID_LABEL, buf);
  /* ---------------------------------------------- */
  fc_snprintf(cbuf, sizeof(cbuf), "%d", total->upkeep[O_FOOD]);

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

  buf->size.w = text1->w;
  add_to_gui_list(ID_LABEL, buf);
  /* ---------------------------------------------- */
  fc_snprintf(cbuf, sizeof(cbuf), "%d", total->upkeep[O_GOLD]);

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

  buf->size.w = text1->w;
  add_to_gui_list(ID_LABEL, buf);
  /* ---------------------------------------------- */
  fc_snprintf(cbuf, sizeof(cbuf), "%d", total->building_count);

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  buf = create_iconlabel(NULL, pwindow->dst, pstr,
                         WF_RESTORE_BACKGROUND);

  buf->size.w = text2->w + adj_size(6);
  add_to_gui_list(ID_LABEL, buf);

  /* ------------------------- */
  last = buf;
  count = 0;
  unit_type_iterate(i) {
    if ((units[utype_index(i)].active_count > 0)
        || (units[utype_index(i)].building_count > 0)) {
      upgrade = (can_upgrade_unittype(client.conn.playing, i) != NULL);

      /* Unit type icon */
      buf = create_iconlabel(adj_surf(get_unittype_surface(i, direction8_invalid())),
                             pwindow->dst, NULL,
                             WF_RESTORE_BACKGROUND | WF_FREE_THEME);
      if (count > adj_size(72)) {
        set_wflag(buf, WF_HIDDEN);
      }
      hh = buf->size.h;
      add_to_gui_list(MAX_ID - utype_number(i), buf);

      /* Unit type name */
      pstr = create_utf8_from_char_fonto(utype_name_translation(i),
                                         FONTO_ATTENTION);
      pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
      buf = create_iconlabel(NULL, pwindow->dst, pstr,
                             (WF_RESTORE_BACKGROUND | WF_SELECT_WITHOUT_BAR));
      if (upgrade) {
        buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_UNITUPGRADE_TEXT);
        buf->action = popup_upgrade_unit_callback;
        set_wstate(buf, FC_WS_NORMAL);
      } else {
        buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_UNITSREP_TEXT);
      }
      buf->string_utf8->style &= ~SF_CENTER;
      if (count > adj_size(72)) {
        set_wflag(buf , WF_HIDDEN);
      }
      hh = MAX(hh, buf->size.h);
      name_w = MAX(buf->size.w, name_w);
      add_to_gui_list(MAX_ID - utype_number(i), buf);

      /* Active */
      fc_snprintf(cbuf, sizeof(cbuf), "%d", units[utype_index(i)].active_count);
      pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
      pstr->style |= SF_CENTER;
      buf = create_iconlabel(NULL, pwindow->dst, pstr,
                             WF_RESTORE_BACKGROUND);
      if (count > adj_size(72)) {
        set_wflag(buf, WF_HIDDEN);
      }
      hh = MAX(hh, buf->size.h);
      buf->size.w = text1->w + adj_size(6);
      add_to_gui_list(MAX_ID - utype_number(i), buf);

      /* Shield upkeep */
      fc_snprintf(cbuf, sizeof(cbuf), "%d", units[utype_index(i)].upkeep[O_SHIELD]);
      pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
      pstr->style |= SF_CENTER;
      buf = create_iconlabel(NULL, pwindow->dst, pstr,
                             WF_RESTORE_BACKGROUND);
      if (count > adj_size(72)) {
        set_wflag(buf, WF_HIDDEN);
      }
      hh = MAX(hh, buf->size.h);
      buf->size.w = text1->w;
      add_to_gui_list(MAX_ID - utype_number(i), buf);

      /* Food upkeep */
      fc_snprintf(cbuf, sizeof(cbuf), "%d", units[utype_index(i)].upkeep[O_FOOD]);
      pstr = create_utf8_from_char(cbuf, FONTO_DEFAULT);
      pstr->style |= SF_CENTER;
      buf = create_iconlabel(NULL, pwindow->dst, pstr,
                             WF_RESTORE_BACKGROUND);
      if (count > adj_size(72)) {
        set_wflag(buf, WF_HIDDEN);
      }

      hh = MAX(hh, buf->size.h);
      buf->size.w = text1->w;
      add_to_gui_list(MAX_ID - utype_number(i), buf);

      /* Gold upkeep */
      fc_snprintf(cbuf, sizeof(cbuf), "%d", units[utype_index(i)].upkeep[O_GOLD]);
      pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
      pstr->style |= SF_CENTER;
      buf = create_iconlabel(NULL, pwindow->dst, pstr,
                             WF_RESTORE_BACKGROUND);
      if (count > adj_size(72)) {
        set_wflag(buf, WF_HIDDEN);
      }

      hh = MAX(hh, buf->size.h);
      buf->size.w = text1->w;
      add_to_gui_list(MAX_ID - utype_number(i), buf);

      /* Building */
      if (units[utype_index(i)].building_count > 0) {
        fc_snprintf(cbuf, sizeof(cbuf), "%d", units[utype_index(i)].building_count);
      } else {
        fc_snprintf(cbuf, sizeof(cbuf), "--");
      }
      pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
      pstr->style |= SF_CENTER;
      buf = create_iconlabel(NULL, pwindow->dst, pstr,
                             WF_RESTORE_BACKGROUND);
      if (count > adj_size(72)) {
        set_wflag(buf, WF_HIDDEN);
      }
      hh = MAX(hh, buf->size.h);
      buf->size.w = text2->w + adj_size(6);
      add_to_gui_list(MAX_ID - utype_number(i), buf);

      /* Soonest completion */
      if (units[utype_index(i)].building_count > 0) {
        fc_snprintf(cbuf, sizeof(cbuf), "%d %s",
                    units[utype_index(i)].soonest_completions,
                    PL_("turn", "turns", units[utype_index(i)].soonest_completions));
      } else {
        fc_snprintf(cbuf, sizeof(cbuf), "--");
      }

      pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
      pstr->style |= SF_CENTER;
      buf = create_iconlabel(NULL, pwindow->dst, pstr,
                             WF_RESTORE_BACKGROUND);

      if (count > adj_size(72)) {
        set_wflag(buf, WF_HIDDEN);
      }
      hh = MAX(hh, buf->size.h);
      buf->size.w = text5->w + adj_size(6);
      add_to_gui_list(MAX_ID - utype_number(i), buf);

      count += adj_size(8);
      area.h += (hh / 2);
    }
  } unit_type_iterate_end;

  units_dlg->begin_widget_list = buf;
  area.w = MAX(area.w, (tileset_full_tile_width(tileset) * 2 + name_w + adj_size(15))
               + (adj_size(4) * text1->w + adj_size(46)) + (text2->w + adj_size(16))
               + (text5->w + adj_size(6)) + adj_size(2));
  if (count > 0) {
    units_dlg->end_active_widget_list = last->prev;
    units_dlg->begin_active_widget_list = units_dlg->begin_widget_list;
    if (count > adj_size(80)) {
      units_dlg->active_widget_list = units_dlg->end_active_widget_list;
      if (units_dlg->scroll) {
        units_dlg->scroll->count = count;
      }
      ww = create_vertical_scrollbar(units_dlg, 8, 10, TRUE, TRUE);
      area.w += ww;
      area.h = (hh + 9 * (hh/2) + adj_size(10));
    } else {
      area.h += hh/2;
    }
  } else {
    area.h = adj_size(50);
  }

  area.h += text1->h + adj_size(10);
  area.w += adj_size(2);

  logo = theme_get_background(active_theme, BACKGROUND_UNITSREP);
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

  /* exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  /* totals background and label */
  dst.x = area.x + adj_size(2);
  dst.y = area.y + area.h - (text3->h + adj_size(2)) - adj_size(2);
  dst.w = name_w + tileset_full_tile_width(tileset) * 2 + adj_size(5);
  dst.h = text3->h + adj_size(2);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_UNITSREP_FRAME));

  dst.y += 1;
  dst.x += ((name_w + tileset_full_tile_width(tileset) * 2 + adj_size(5)) - text3->w) / 2;
  alphablit(text3, NULL, pwindow->theme, &dst, 255);
  FREESURFACE(text3);

  /* total active widget */
  buf = buf->prev;
  buf->size.x = area.x + name_w
    + tileset_full_tile_width(tileset) * 2 + adj_size(17);
  buf->size.y = dst.y;

  /* total shields cost widget */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
  buf->size.y = dst.y;

  /* total food cost widget */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
  buf->size.y = dst.y;

  /* total gold cost widget */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
  buf->size.y = dst.y;

  /* total building count widget */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
  buf->size.y = dst.y;

  /* units background and labels */
  dst.x = area.x + adj_size(2);
  dst.y = area.y + 1;
  dst.w = name_w + tileset_full_tile_width(tileset) * 2 + adj_size(5);
  dst.h = text4->h + adj_size(2);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_UNITSREP_FRAME));

  dst.y += 1;
  dst.x += ((name_w + tileset_full_tile_width(tileset) * 2 + adj_size(5))- text4->w) / 2;
  alphablit(text4, NULL, pwindow->theme, &dst, 255);
  FREESURFACE(text4);

  /* active count background and label */
  dst.x = area.x + adj_size(2) + name_w + tileset_full_tile_width(tileset) * 2 + adj_size(15);
  dst.y = area.y + 1;
  dst.w = text1->w + adj_size(6);
  dst.h = area.h - adj_size(3);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_UNITSREP_FRAME));

  dst.x += adj_size(3);
  alphablit(text1, NULL, pwindow->theme, &dst, 255);
  ww = text1->w;
  hh = text1->h;
  FREESURFACE(text1);

  /* shields cost background and label */
  dst.x += (ww + adj_size(13));
  w = dst.x;
  dst.w = ww;
  dst.h = area.h - adj_size(3);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_UNITSREP_FRAME));

  dst.y = area.y + adj_size(3);
  dst.x += ((ww - icons->big_shield->w) / 2);
  alphablit(icons->big_shield, NULL, pwindow->theme, &dst, 255);

  /* food cost background and label */
  dst.x = w + ww + adj_size(10);
  w = dst.x;
  dst.y = area.y + 1;
  dst.w = ww;
  dst.h = area.h - adj_size(3);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_UNITSREP_FRAME));

  dst.y = area.y + adj_size(3);
  dst.x += ((ww - icons->big_food->w) / 2);
  alphablit(icons->big_food, NULL, pwindow->theme, &dst, 255);

  /* gold cost background and label */
  dst.x = w + ww + adj_size(10);
  w = dst.x;
  dst.y = area.y + 1;
  dst.w = ww;
  dst.h = area.h - adj_size(3);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_UNITSREP_FRAME));

  dst.y = area.y + adj_size(3);
  dst.x += ((ww - icons->big_coin->w) / 2);
  alphablit(icons->big_coin, NULL, pwindow->theme, &dst, 255);

  /* building count background and label */
  dst.x = w + ww + adj_size(10);
  dst.y = area.y + 1;
  dst.w = text2->w + adj_size(6);
  ww = text2->w + adj_size(6);
  w = dst.x;
  dst.h = area.h - adj_size(3);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_UNITSREP_FRAME));

  dst.x += adj_size(3);
  alphablit(text2, NULL, pwindow->theme, &dst, 255);
  FREESURFACE(text2);

  /* building count background and label */
  dst.x = w + ww + adj_size(10);
  dst.y = area.y + 1;
  dst.w = text5->w + adj_size(6);
  dst.h = area.h - adj_size(3);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w, dst.h - 1,
               get_theme_color(COLOR_THEME_UNITSREP_FRAME));

  dst.x += adj_size(3);
  alphablit(text5, NULL, pwindow->theme, &dst, 255);
  FREESURFACE(text5);

  if (count) {
    int start_x = area.x + adj_size(2);
    int start_y = area.y + hh + adj_size(3);
    int mod = 0;

    buf = buf->prev;
    while (TRUE) {
      /* Unit type icon */
      buf->size.x = start_x + (mod ? tileset_full_tile_width(tileset) : 0);
      buf->size.y = start_y;
      hh = buf->size.h;
      mod ^= 1;

      /* Unit type name */
      buf = buf->prev;
      buf->size.w = name_w;
      buf->size.x = start_x + tileset_full_tile_width(tileset) * 2 + adj_size(5);
      buf->size.y = start_y + (hh - buf->size.h) / 2;

      /* Number active */
      buf = buf->prev;
      buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
      buf->size.y = start_y + (hh - buf->size.h) / 2;

      /* Shield upkeep */
      buf = buf->prev;
      buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
      buf->size.y = start_y + (hh - buf->size.h) / 2;

      /* Food upkeep */
      buf = buf->prev;
      buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
      buf->size.y = start_y + (hh - buf->size.h) / 2;

      /* Gold upkeep */
      buf = buf->prev;
      buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
      buf->size.y = start_y + (hh - buf->size.h) / 2;

      /* Number under construction */
      buf = buf->prev;
      buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
      buf->size.y = start_y + (hh - buf->size.h) / 2;

      /* Soonest completion */
      buf = buf->prev;
      buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(10);
      buf->size.y = start_y + (hh - buf->size.h) / 2;

      start_y += (hh >> 1);
      if (buf == units_dlg->begin_active_widget_list) {
        break;
      }
      buf = buf->prev;
    }

    if (units_dlg->scroll) {
      setup_vertical_scrollbar_area(units_dlg->scroll,
                                    area.x + area.w, area.y,
                                    area.h, TRUE);
    }

  }
  /* ----------------------------------- */
  redraw_group(units_dlg->begin_widget_list, pwindow, 0);
  widget_mark_dirty(pwindow);

  flush_dirty();
}

/**********************************************************************//**
  Update the units report.
**************************************************************************/
void real_units_report_dialog_update(void *unused)
{
  if (units_dlg) {
    struct units_entry units[U_LAST];
    struct units_entry units_total;
    struct widget *pwidget, *pbuf;
    bool is_in_list = FALSE;
    char cbuf[32];
    bool upgrade;
    bool search_finished;

    get_units_report_data(units, &units_total);

    /* Find if there are new units entry (if not then rebuild all) */
    pwidget = units_dlg->end_active_widget_list; /* Icon of first list entry */
    unit_type_iterate(i) {
      if ((units[utype_index(i)].active_count > 0)
          || (units[utype_index(i)].building_count > 0)) {
        is_in_list = FALSE;

        pbuf = pwidget; /* Unit type icon */
        while (pbuf != NULL) {
          if ((MAX_ID - pbuf->id) == utype_number(i)) {
            is_in_list = TRUE;
            pwidget = pbuf;
            break;
          }
          if (pbuf->prev->prev->prev->prev->prev->prev->prev ==
              units_dlg->begin_active_widget_list) {
            break;
          }

          /* First widget of next list entry */
          pbuf = pbuf->prev->prev->prev->prev->prev->prev->prev->prev;
        }

        if (!is_in_list) {
          real_activeunits_report_dialog_update(units, &units_total);
          return;
        }
      }
    } unit_type_iterate_end;

    /* Update list */
    pwidget = units_dlg->end_active_widget_list;
    if (pwidget != NULL) {
      unit_type_iterate(i) {
        pbuf = pwidget; /* First widget (icon) of the first list entry */

        if ((units[utype_index(i)].active_count > 0)
            || (units[utype_index(i)].building_count > 0)) {
          /* The player has at least one unit of this type */

          search_finished = FALSE;
          while (!search_finished) {
            if ((MAX_ID - pbuf->id) == utype_number(i)) {
              /* List entry for this unit type found */

              upgrade = (can_upgrade_unittype(client.conn.playing, i) != NULL);

              pbuf = pbuf->prev; /* Unit type name */
              if (upgrade) {
                pbuf->string_utf8->fgcol
                  = *get_theme_color(COLOR_THEME_UNITUPGRADE_TEXT);
                pbuf->action = popup_upgrade_unit_callback;
                set_wstate(pbuf, FC_WS_NORMAL);
              }

              pbuf = pbuf->prev; /* Active */
              fc_snprintf(cbuf, sizeof(cbuf), "%d",
                          units[utype_index(i)].active_count);
              copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

              pbuf = pbuf->prev; /* Shield upkeep */
              fc_snprintf(cbuf, sizeof(cbuf), "%d",
                          units[utype_index(i)].upkeep[O_SHIELD]);
              copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

              pbuf = pbuf->prev; /* Food upkeep */
              fc_snprintf(cbuf, sizeof(cbuf), "%d",
                          units[utype_index(i)].upkeep[O_FOOD]);
              copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

              pbuf = pbuf->prev; /* Gold upkeep */
              fc_snprintf(cbuf, sizeof(cbuf), "%d",
                          units[utype_index(i)].upkeep[O_GOLD]);
              copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

              pbuf = pbuf->prev; /* Building */
              if (units[utype_index(i)].building_count > 0) {
                fc_snprintf(cbuf, sizeof(cbuf), "%d",
                            units[utype_index(i)].building_count);
              } else {
                fc_snprintf(cbuf, sizeof(cbuf), "--");
              }
              copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

              pbuf = pbuf->prev; /* Soonest completion */
              if (units[utype_index(i)].building_count > 0) {
                fc_snprintf(cbuf, sizeof(cbuf), "%d %s",
                            units[utype_index(i)].soonest_completions,
                            PL_("turn", "turns",
                                units[utype_index(i)].soonest_completions));
              } else {
                fc_snprintf(cbuf, sizeof(cbuf), "--");
              }
              copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

              pwidget = pbuf->prev; /* Icon of next unit type */

              search_finished = TRUE;

            } else { /* List entry for this unit type not found yet */

              /* Search it */
              pbuf = pwidget->next;
              do {
                del_widget_from_vertical_scroll_widget_list(units_dlg,
                                                            pbuf->prev);
              } while (((MAX_ID - pbuf->prev->id) != utype_number(i))
                       && (pbuf->prev != units_dlg->begin_active_widget_list));

              if (pbuf->prev == units_dlg->begin_active_widget_list) {
                /* List entry not found - can this really happen? */
                del_widget_from_vertical_scroll_widget_list(units_dlg,
                                                            pbuf->prev);
                pwidget = pbuf->prev; /* units_dlg->begin_active_widget_list */
                search_finished = TRUE;
              } else {
                /* Found it */
                pbuf = pbuf->prev; /* First widget (icon) of list entry */
              }
            }
          }
        } else { /* Player has no unit of this type */
          if (pbuf->next != units_dlg->begin_active_widget_list) {
            if (utype_number(i) < (MAX_ID - pbuf->id)) {
              continue;
            } else {
              pbuf = pbuf->next;
              do {
                del_widget_from_vertical_scroll_widget_list(units_dlg,
                                                            pbuf->prev);
              } while (((MAX_ID - pbuf->prev->id) == utype_number(i))
                       && (pbuf->prev != units_dlg->begin_active_widget_list));
              if (pbuf->prev == units_dlg->begin_active_widget_list) {
                del_widget_from_vertical_scroll_widget_list(units_dlg,
                                                            pbuf->prev);
              }
              pwidget = pbuf->prev;
            }
          }
        }
      } unit_type_iterate_end;
    }

    /* -------------------------------------- */

    /* Total active */
    pbuf = units_dlg->end_widget_list->prev->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", units_total.active_count);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

    /* Total shields cost */
    pbuf = pbuf->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", units_total.upkeep[O_SHIELD]);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

    /* Total food cost widget */
    pbuf = pbuf->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", units_total.upkeep[O_FOOD]);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

    /* Total gold cost widget */
    pbuf = pbuf->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", units_total.upkeep[O_GOLD]);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

    /* Total building count */
    pbuf = pbuf->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", units_total.building_count);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);

    /* -------------------------------------- */
    redraw_group(units_dlg->begin_widget_list, units_dlg->end_widget_list, 0);
    widget_mark_dirty(units_dlg->end_widget_list);

    flush_dirty();
  }
}

/**********************************************************************//**
  Popup (or raise) the units report (F2). It may or may not be modal.
**************************************************************************/
void units_report_dialog_popup(bool make_modal)
{
  struct units_entry units[U_LAST];
  struct units_entry units_total;

  if (units_dlg) {
    return;
  }

  get_units_report_data(units, &units_total);
  real_activeunits_report_dialog_update(units, &units_total);
}

/**************************************************************************
  Popdown the units report.
**************************************************************************/
void units_report_dialog_popdown(void)
{
  if (units_dlg) {
    if (units_upg_dlg) {
      del_group_of_widgets_from_gui_list(units_upg_dlg->begin_widget_list,
                                         units_upg_dlg->end_widget_list);
      FC_FREE(units_upg_dlg);
    }
    popdown_window_group_dialog(units_dlg->begin_widget_list,
                                units_dlg->end_widget_list);
    FC_FREE(units_dlg->scroll);
    FC_FREE(units_dlg);
  }
}

/* ===================================================================== */
/* ======================== Economy Report ============================= */
/* ===================================================================== */
static struct advanced_dialog *economy_dlg = NULL;
static struct small_dialog *economy_sell_dlg = NULL;

struct rates_move {
  int min, max, tax, x, gov_max;
  int *src_rate, *dst_rate;
  struct widget *horiz_src, *horiz_dst;
  struct widget *label_src, *label_dst;
};

/**********************************************************************//**
  User interacted with Economy Report window.
**************************************************************************/
static int economy_dialog_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(economy_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Economy dialog Close Dialog button.
**************************************************************************/
static int exit_economy_dialog_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (economy_dlg) {
      if (economy_sell_dlg) {
        del_group_of_widgets_from_gui_list(economy_sell_dlg->begin_widget_list,
                                           economy_sell_dlg->end_widget_list);
        FC_FREE(economy_sell_dlg);
      }
      popdown_window_group_dialog(economy_dlg->begin_widget_list,
                                  economy_dlg->end_widget_list);
      FC_FREE(economy_dlg->scroll);
      FC_FREE(economy_dlg);
      set_wstate(get_tax_rates_widget(), FC_WS_NORMAL);
      widget_redraw(get_tax_rates_widget());
      widget_mark_dirty(get_tax_rates_widget());
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  Toggle Rates dialog locking checkbox.
**************************************************************************/
static int toggle_block_callback(struct widget *pcheckbox)
{
  if (PRESSED_EVENT(main_data.event)) {
    switch (pcheckbox->id) {
    case ID_CHANGE_TAXRATE_DLG_LUX_BLOCK_CHECKBOX:
      sdl3_client_flags ^= CF_CHANGE_TAXRATE_LUX_BLOCK;
      return -1;

    case ID_CHANGE_TAXRATE_DLG_SCI_BLOCK_CHECKBOX:
      sdl3_client_flags ^= CF_CHANGE_TAXRATE_SCI_BLOCK;
      return -1;

    default:
      return -1;
    }
  }

  return -1;
}

/**********************************************************************//**
  User released mouse button while adjusting rates.
**************************************************************************/
static widget_id report_scroll_mouse_button_up(SDL_MouseButtonEvent *button_event,
                                               void *data)
{
  return (widget_id)ID_SCROLLBAR;
}

/**********************************************************************//**
  User moved a mouse while adjusting rates.
**************************************************************************/
static widget_id report_scroll_mouse_motion_handler(SDL_MouseMotionEvent *motion_event,
                                                    void *data)
{
  struct rates_move *motion = (struct rates_move *)data;
  struct widget *tax_label = economy_dlg->end_widget_list->prev->prev;
  struct widget *pbuf = NULL;
  char cbuf[8];
  int dir, inc, x, *buf_rate = NULL;

  motion_event->x -= motion->horiz_src->dst->dest_rect.x;

  if ((abs(motion_event->x - motion->x) > 7)
      && (motion_event->x >= motion->min)
      && (motion_event->x <= motion->max)) {

    /* Set up directions */
    if (motion_event->xrel > 0) {
      dir = 15;
      inc = 10;
    } else {
      dir = -15;
      inc = -10;
    }

    /* Make checks */
    x = motion->horiz_src->size.x;
    if (((x + dir) <= motion->max) && ((x + dir) >= motion->min)) {
      /* src in range */
      if (motion->horiz_dst) {
        x = motion->horiz_dst->size.x;
        if (((x + (-1 * dir)) > motion->max) || ((x + (-1 * dir)) < motion->min)) {
          /* dst out of range */
          if (motion->tax + (-1 * inc) <= motion->gov_max
              && motion->tax + (-1 * inc) >= 0) {
            /* tax in range */
            pbuf = motion->horiz_dst;
            motion->horiz_dst = NULL;
            buf_rate = motion->dst_rate;
            motion->dst_rate = &motion->tax;
            motion->label_dst = tax_label;
          } else {
            motion->x = motion->horiz_src->size.x;
            return ID_ERROR;
          }
        }
      } else {
        if (motion->tax + (-1 * inc) > motion->gov_max
            || motion->tax + (-1 * inc) < 0) {
          motion->x = motion->horiz_src->size.x;
          return ID_ERROR;
        }
      }

      /* Undraw scrollbars */
      widget_undraw(motion->horiz_src);
      widget_mark_dirty(motion->horiz_src);

      if (motion->horiz_dst) {
        widget_undraw(motion->horiz_dst);
        widget_mark_dirty(motion->horiz_dst);
      }

      motion->horiz_src->size.x += dir;
      if (motion->horiz_dst) {
        motion->horiz_dst->size.x -= dir;
      }

      *motion->src_rate += inc;
      *motion->dst_rate -= inc;

      fc_snprintf(cbuf, sizeof(cbuf), "%d%%", *motion->src_rate);
      copy_chars_to_utf8_str(motion->label_src->string_utf8, cbuf);
      fc_snprintf(cbuf, sizeof(cbuf), "%d%%", *motion->dst_rate);
      copy_chars_to_utf8_str(motion->label_dst->string_utf8, cbuf);

      /* Redraw label */
      widget_redraw(motion->label_src);
      widget_mark_dirty(motion->label_src);

      widget_redraw(motion->label_dst);
      widget_mark_dirty(motion->label_dst);

      /* Redraw scrollbar */
      if (get_wflags(motion->horiz_src) & WF_RESTORE_BACKGROUND) {
        refresh_widget_background(motion->horiz_src);
      }
      widget_redraw(motion->horiz_src);
      widget_mark_dirty(motion->horiz_src);

      if (motion->horiz_dst) {
        if (get_wflags(motion->horiz_dst) & WF_RESTORE_BACKGROUND) {
          refresh_widget_background(motion->horiz_dst);
        }
        widget_redraw(motion->horiz_dst);
        widget_mark_dirty(motion->horiz_dst);
      }

      flush_dirty();

      if (pbuf != NULL) {
        motion->horiz_dst = pbuf;
        motion->label_dst = motion->horiz_dst->prev;
        motion->dst_rate = buf_rate;
        pbuf = NULL;
      }

      motion->x = motion->horiz_src->size.x;
    }
  } /* if */

  return ID_ERROR;
}

/**********************************************************************//**
  Handle Rates sliders.
**************************************************************************/
static int horiz_taxrate_callback(struct widget *horiz_src)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct rates_move motion;

    motion.horiz_src = horiz_src;
    motion.label_src = horiz_src->prev;

    switch (horiz_src->id) {
      case ID_CHANGE_TAXRATE_DLG_LUX_SCROLLBAR:
        if (sdl3_client_flags & CF_CHANGE_TAXRATE_LUX_BLOCK) {
          goto END;
        }
        motion.src_rate = (int *)horiz_src->data.ptr;
        motion.horiz_dst = horiz_src->prev->prev->prev; /* sci */
        motion.dst_rate = (int *)motion.horiz_dst->data.ptr;
        motion.tax = 100 - *motion.src_rate - *motion.dst_rate;
        if ((sdl3_client_flags & CF_CHANGE_TAXRATE_SCI_BLOCK)) {
          if (motion.tax <= get_player_bonus(client.conn.playing, EFT_MAX_RATES)) {
            motion.horiz_dst = NULL; /* tax */
            motion.dst_rate = &motion.tax;
          } else {
            goto END; /* all blocked */
          }
        }

      break;

      case ID_CHANGE_TAXRATE_DLG_SCI_SCROLLBAR:
        if ((sdl3_client_flags & CF_CHANGE_TAXRATE_SCI_BLOCK)) {
          goto END;
        }
        motion.src_rate = (int *)horiz_src->data.ptr;
        motion.horiz_dst = horiz_src->next->next->next; /* lux */
        motion.dst_rate = (int *)motion.horiz_dst->data.ptr;
        motion.tax = 100 - *motion.src_rate - *motion.dst_rate;
        if (sdl3_client_flags & CF_CHANGE_TAXRATE_LUX_BLOCK) {
          if (motion.tax <= get_player_bonus(client.conn.playing, EFT_MAX_RATES)) {
            /* Tax */
            motion.horiz_dst = NULL;
            motion.dst_rate = &motion.tax;
          } else {
            goto END; /* All blocked */
          }
        }

      break;

      default:
        return -1;
    }

    if (motion.horiz_dst) {
      motion.label_dst = motion.horiz_dst->prev;
    } else {
      /* tax label */
      motion.label_dst = economy_dlg->end_widget_list->prev->prev;
    }

    motion.min = horiz_src->next->size.x + horiz_src->next->size.w + adj_size(2);
    motion.gov_max = get_player_bonus(client.conn.playing, EFT_MAX_RATES);
    motion.max = motion.min + motion.gov_max * 1.5;
    motion.x = horiz_src->size.x;

    MOVE_STEP_Y = 0;
    /* Filter mouse motion events */
    SDL_SetEventFilter(FilterMouseMotionEvents, NULL);
    gui_event_loop((void *)(&motion), NULL, NULL, NULL, NULL, NULL, NULL,
                   NULL, NULL,
                   report_scroll_mouse_button_up,
                   report_scroll_mouse_motion_handler);
    /* Turn off Filter mouse motion events */
    SDL_SetEventFilter(NULL, NULL);
    MOVE_STEP_Y = DEFAULT_MOVE_STEP;

END:
    unselect_widget_action();
    selected_widget = horiz_src;
    set_wstate(horiz_src, FC_WS_SELECTED);
    widget_redraw(horiz_src);
    widget_flush(horiz_src);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Update button of the Rates.
**************************************************************************/
static int apply_taxrates_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct widget *buf;
    int science, luxury, tax;

    if (C_S_RUNNING != client_state()) {
      return -1;
    }

    /* Science Scrollbar */
    buf = button->next->next;
    science = *(int *)buf->data.ptr;

    /* Luxuries Scrollbar */
    buf = buf->next->next->next;
    luxury = *(int *)buf->data.ptr;

    /* Tax */
    tax = 100 - luxury - science;

    if (tax != client.conn.playing->economic.tax
        || science != client.conn.playing->economic.science
        || luxury != client.conn.playing->economic.luxury) {
      dsend_packet_player_rates(&client.conn, tax, luxury, science);
    }

    widget_redraw(button);
    widget_flush(button);
  }

  return -1;
}

/**********************************************************************//**
  Set economy dialog widgets enabled.
**************************************************************************/
static void enable_economy_dlg(void)
{
  /* lux lock */
  struct widget *buf = economy_dlg->end_widget_list->prev->prev->prev->prev->prev->prev;

  set_wstate(buf, FC_WS_NORMAL);

  /* lux scrollbar */
  buf = buf->prev;
  set_wstate(buf, FC_WS_NORMAL);

  /* sci lock */
  buf = buf->prev->prev;
  set_wstate(buf, FC_WS_NORMAL);

  /* sci scrollbar */
  buf = buf->prev;
  set_wstate(buf, FC_WS_NORMAL);

  /* update button */
  buf = buf->prev->prev;
  set_wstate(buf, FC_WS_NORMAL);

  /* cancel button */
  buf = buf->prev;
  set_wstate(buf, FC_WS_NORMAL);

  set_group_state(economy_dlg->begin_active_widget_list,
                  economy_dlg->end_active_widget_list, FC_WS_NORMAL);
  if (economy_dlg->scroll && economy_dlg->active_widget_list) {
    set_wstate(economy_dlg->scroll->up_left_button, FC_WS_NORMAL);
    set_wstate(economy_dlg->scroll->down_right_button, FC_WS_NORMAL);
    set_wstate(economy_dlg->scroll->pscroll_bar, FC_WS_NORMAL);
  }
}

/**********************************************************************//**
  Set economy dialog widgets disabled.
**************************************************************************/
static void disable_economy_dlg(void)
{
  /* lux lock */
  struct widget *buf = economy_dlg->end_widget_list->prev->prev->prev->prev->prev->prev;

  set_wstate(buf, FC_WS_DISABLED);

  /* lux scrollbar */
  buf = buf->prev;
  set_wstate(buf, FC_WS_DISABLED);

  /* sci lock */
  buf = buf->prev->prev;
  set_wstate(buf, FC_WS_DISABLED);

  /* sci scrollbar */
  buf = buf->prev;
  set_wstate(buf, FC_WS_DISABLED);

  /* update button */
  buf = buf->prev->prev;
  set_wstate(buf, FC_WS_DISABLED);

  /* cancel button */
  buf = buf->prev;
  set_wstate(buf, FC_WS_DISABLED);

  set_group_state(economy_dlg->begin_active_widget_list,
                  economy_dlg->end_active_widget_list, FC_WS_DISABLED);
  if (economy_dlg->scroll && economy_dlg->active_widget_list) {
    set_wstate(economy_dlg->scroll->up_left_button, FC_WS_DISABLED);
    set_wstate(economy_dlg->scroll->down_right_button, FC_WS_DISABLED);
    set_wstate(economy_dlg->scroll->pscroll_bar, FC_WS_DISABLED);
  }
}

/* --------------------------------------------------------------- */

/**********************************************************************//**
  User interacted with Yes button of the improvement selling dialog.
**************************************************************************/
static int ok_sell_impr_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int imp, total_count, count = 0;
    struct widget *impr = (struct widget *)pwidget->data.ptr;

    imp = impr->data.cont->id0;
    total_count = impr->data.cont->id1;

    /* popdown sell dlg */
    popdown_window_group_dialog(economy_sell_dlg->begin_widget_list,
                                economy_sell_dlg->end_widget_list);
    FC_FREE(economy_sell_dlg);
    enable_economy_dlg();

    /* send sell */
    city_list_iterate(client.conn.playing->cities, pcity) {
      if (!pcity->did_sell
          && city_has_building(pcity, improvement_by_number(imp))) {
        count++;

        city_sell_improvement(pcity, imp);
      }
    } city_list_iterate_end;

    if (count == total_count) {
      del_widget_from_vertical_scroll_widget_list(economy_dlg, impr);
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with the improvement selling window.
**************************************************************************/
static int sell_impr_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(economy_sell_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Cancel button of the improvement selling dialog.
**************************************************************************/
static int cancel_sell_impr_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (economy_sell_dlg) {
      popdown_window_group_dialog(economy_sell_dlg->begin_widget_list,
                                  economy_sell_dlg->end_widget_list);
      FC_FREE(economy_sell_dlg);
      enable_economy_dlg();
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  Open improvement selling dialog.
**************************************************************************/
static int popup_sell_impr_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int imp, total_count ,count = 0, gold = 0;
    int value;
    char cbuf[128];
    struct widget *buf = NULL, *pwindow;
    utf8_str *pstr;
    SDL_Surface *text;
    SDL_Rect dst;
    SDL_Rect area;

    if (economy_sell_dlg) {
      return 1;
    }

    set_wstate(pwidget, FC_WS_NORMAL);
    selected_widget = NULL;
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);

    economy_sell_dlg = fc_calloc(1, sizeof(struct small_dialog));

    imp = pwidget->data.cont->id0;
    total_count = pwidget->data.cont->id1;
    value = impr_sell_gold(improvement_by_number(imp));

    city_list_iterate(client.conn.playing->cities, pcity) {
      if (!pcity->did_sell
          && city_has_building(pcity, improvement_by_number(imp))) {
        count++;
        gold += value;
      }
    } city_list_iterate_end;

    if (count > 0) {
      fc_snprintf(cbuf, sizeof(cbuf),
                  _("We have %d of %s\n(total value is : %d)\n"
                    "We can sell %d of them for %d gold."),
                  total_count,
                  improvement_name_translation(improvement_by_number(imp)),
                  total_count * value, count, gold);
    } else {
      fc_snprintf(cbuf, sizeof(cbuf),
                  _("We can't sell any %s in this turn."),
                  improvement_name_translation(improvement_by_number(imp)));
    }

    pstr = create_utf8_from_char_fonto(_("Sell It?"), FONTO_ATTENTION);
    pstr->style |= TTF_STYLE_BOLD;

    pwindow = create_window_skeleton(NULL, pstr, 0);

    pwindow->action = sell_impr_window_callback;
    set_wstate(pwindow, FC_WS_NORMAL);

    economy_sell_dlg->end_widget_list = pwindow;

    add_to_gui_list(ID_WINDOW, pwindow);

    area = pwindow->area;

    /* ============================================================= */

    /* Create text label */
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pstr->fgcol = *get_theme_color(COLOR_THEME_SELLIMPR_TEXT);

    text = create_text_surf_from_utf8(pstr);
    FREEUTF8STR(pstr);

    area.w = MAX(area.w, text->w + adj_size(20));
    area.h += (text->h + adj_size(10));

    /* Cancel button */
    buf = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                                   pwindow->dst, _("No"),
                                                   FONTO_ATTENTION, 0);

    buf->action = cancel_sell_impr_callback;
    set_wstate(buf, FC_WS_NORMAL);

    area.h += (buf->size.h + adj_size(20));

    add_to_gui_list(ID_BUTTON, buf);

    if (count > 0) {
      buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                     pwindow->dst,
                                                     _("Sell"),
                                                     FONTO_ATTENTION, 0);

      buf->action = ok_sell_impr_callback;
      set_wstate(buf, FC_WS_NORMAL);
      buf->data.ptr = (void *)pwidget;

      add_to_gui_list(ID_BUTTON, buf);
      buf->size.w = MAX(buf->size.w, buf->next->size.w);
      buf->next->size.w = buf->size.w;
      area.w = MAX(area.w, adj_size(30) + buf->size.w * 2);
    } else {
      area.w = MAX(area.w, buf->size.w + adj_size(20));
    }
    /* ============================================ */

    economy_sell_dlg->begin_widget_list = buf;

    resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                  (pwindow->size.w - pwindow->area.w) + area.w,
                  (pwindow->size.h - pwindow->area.h) + area.h);

    area = pwindow->area;

    widget_set_position(pwindow,
                        economy_dlg->end_widget_list->size.x +
                          (economy_dlg->end_widget_list->size.w - pwindow->size.w) / 2,
                        economy_dlg->end_widget_list->size.y +
                          (economy_dlg->end_widget_list->size.h - pwindow->size.h) / 2);

    /* setup rest of widgets */
    /* label */
    dst.x = area.x + (area.w - text->w) / 2;
    dst.y = area.y + adj_size(10);
    alphablit(text, NULL, pwindow->theme, &dst, 255);
    FREESURFACE(text);

    /* cancel button */
    buf = pwindow->prev;
    buf->size.y = area.y + area.h - buf->size.h - adj_size(10);

    if (count > 0) {
      /* sell button */
      buf = buf->prev;
      buf->size.x = area.x + (area.w - (2 * buf->size.w + adj_size(10))) / 2;
      buf->size.y = buf->next->size.y;

      /* cancel button */
      buf->next->size.x = buf->size.x + buf->size.w + adj_size(10);
    } else {
      /* x position of cancel button */
      buf->size.x = area.x + area.w - adj_size(10) - buf->size.w;
    }

    /* ================================================== */
    /* redraw */
    redraw_group(economy_sell_dlg->begin_widget_list, pwindow, 0);
    disable_economy_dlg();

    widget_mark_dirty(pwindow);
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Update the economy report.
**************************************************************************/
void real_economy_report_dialog_update(void *unused)
{
  if (economy_dlg) {
    struct widget *pbuf = economy_dlg->end_widget_list;
    int tax, total, entries_used = 0;
    char cbuf[128];
    struct improvement_entry entries[B_LAST];

    get_economy_report_data(entries, &entries_used, &total, &tax);

    /* tresure */
    pbuf = pbuf->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", client.conn.playing->economic.gold);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);
    remake_label_size(pbuf);

    /* Icome */
    pbuf = pbuf->prev->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", tax);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);
    remake_label_size(pbuf);

    /* Cost */
    pbuf = pbuf->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", total);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);
    remake_label_size(pbuf);

    /* Netto */
    pbuf = pbuf->prev;
    fc_snprintf(cbuf, sizeof(cbuf), "%d", tax - total);
    copy_chars_to_utf8_str(pbuf->string_utf8, cbuf);
    remake_label_size(pbuf);
    if (tax - total < 0) {
      pbuf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_ECONOMYDLG_NEG_TEXT);
    } else {
      pbuf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_ECONOMYDLG_TEXT);
    }

    /* ---------------- */
    redraw_group(economy_dlg->begin_widget_list, economy_dlg->end_widget_list, 0);
    widget_flush(economy_dlg->end_widget_list);
  }
}

/**********************************************************************//**
  Popdown the economy report.
**************************************************************************/
void economy_report_dialog_popdown(void)
{
  if (economy_dlg) {
    if (economy_sell_dlg) {
       del_group_of_widgets_from_gui_list(economy_sell_dlg->begin_widget_list,
                                          economy_sell_dlg->end_widget_list);
       FC_FREE(economy_sell_dlg);
    }
    popdown_window_group_dialog(economy_dlg->begin_widget_list,
                                economy_dlg->end_widget_list);
    FC_FREE(economy_dlg->scroll);
    FC_FREE(economy_dlg);
    set_wstate(get_tax_rates_widget(), FC_WS_NORMAL);
    widget_redraw(get_tax_rates_widget());
    widget_mark_dirty(get_tax_rates_widget());
  }
}

#define TARGETS_ROW     2
#define TARGETS_COL     4

/**********************************************************************//**
  Popup (or raise) the economy report (F5).  It may or may not be modal.
**************************************************************************/
void economy_report_dialog_popup(bool make_modal)
{
  SDL_Color bg_color = {255,255,255,128};
  SDL_Color bg_color2 = {255,255,255,136};
  SDL_Color bg_color3 = {255,255,255,64};
  struct widget *buf;
  struct widget *pwindow , *last;
  utf8_str *pstr, *pstr2;
  SDL_Surface *surf, *text_name, *text, *zoomed;
  SDL_Surface *background;
  int i, count , h = 0;
  int w = 0; /* left column values */
  int w2 = 0; /* right column: lock + scrollbar + ... */
  int w3 = 0; /* left column text without values */
  int tax, total, entries_used = 0;
  char cbuf[128];
  struct improvement_entry entries[B_LAST];
  SDL_Rect dst;
  SDL_Rect area;
  struct government *gov = government_of_player(client.conn.playing);
  SDL_Surface *treasury_text;
  SDL_Surface *tax_rate_text;
  SDL_Surface *total_income_text;
  SDL_Surface *total_cost_text;
  SDL_Surface *net_income_text;
  SDL_Surface *max_rate_text;

  if (economy_dlg) {
    return;
  }

  /* disable "Economy" button */
  buf = get_tax_rates_widget();
  set_wstate(buf, FC_WS_DISABLED);
  widget_redraw(buf);
  widget_mark_dirty(buf);

  economy_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  get_economy_report_data(entries, &entries_used, &total, &tax);

  /* --------------- */
  pstr = create_utf8_from_char_fonto(_("Economy Report"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);
  economy_dlg->end_widget_list = pwindow;
  set_wstate(pwindow, FC_WS_NORMAL);
  pwindow->action = economy_dialog_callback;

  add_to_gui_list(ID_ECONOMY_DIALOG_WINDOW, pwindow);

  area = pwindow->area;

  /* ------------------------- */

  /* "Treasury" text surface */
  fc_snprintf(cbuf, sizeof(cbuf), _("Treasury: "));
  pstr2 = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  pstr2->style |= TTF_STYLE_BOLD;
  treasury_text = create_text_surf_from_utf8(pstr2);
  w3 = MAX(w3, treasury_text->w);

  /* "Treasury" value label */
  fc_snprintf(cbuf, sizeof(cbuf), "%d", client.conn.playing->economic.gold);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  buf = create_iconlabel(icons->big_coin, pwindow->dst, pstr,
                         (WF_RESTORE_BACKGROUND|WF_ICON_CENTER_RIGHT));

  add_to_gui_list(ID_LABEL, buf);

  w = MAX(w, buf->size.w);
  h += buf->size.h;

  /* "Tax Rate" text surface */
  fc_snprintf(cbuf, sizeof(cbuf), _("Tax Rate: "));
  copy_chars_to_utf8_str(pstr2, cbuf);
  tax_rate_text = create_text_surf_from_utf8(pstr2);
  w3 = MAX(w3, tax_rate_text->w);

  /* "Tax Rate" value label */
  /* It is important to leave 1 space at ending of this string. */
  fc_snprintf(cbuf, sizeof(cbuf), "%d%% ", client.conn.playing->economic.tax);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

  add_to_gui_list(ID_LABEL, buf);

  w = MAX(w, buf->size.w + buf->next->size.w);
  h += buf->size.h;

  /* "Total Income" text surface */
  fc_snprintf(cbuf, sizeof(cbuf), _("Total Income: "));
  copy_chars_to_utf8_str(pstr2, cbuf);
  total_income_text = create_text_surf_from_utf8(pstr2);
  w3 = MAX(w3, total_income_text->w);

  /* "Total Icome" value label */
  fc_snprintf(cbuf, sizeof(cbuf), "%d", tax);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

  add_to_gui_list(ID_LABEL, buf);

  w = MAX(w, buf->size.w);
  h += buf->size.h;

  /* "Total Cost" text surface */
  fc_snprintf(cbuf, sizeof(cbuf), _("Total Cost: "));
  copy_chars_to_utf8_str(pstr2, cbuf);
  total_cost_text = create_text_surf_from_utf8(pstr2);

  /* "Total Cost" value label */
  fc_snprintf(cbuf, sizeof(cbuf), "%d", total);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

  add_to_gui_list(ID_LABEL, buf);

  w = MAX(w, buf->size.w);
  h += buf->size.h;

  /* "Net Income" text surface */
  fc_snprintf(cbuf, sizeof(cbuf), _("Net Income: "));
  copy_chars_to_utf8_str(pstr2, cbuf);
  net_income_text = create_text_surf_from_utf8(pstr2);
  w3 = MAX(w3, net_income_text->w);

  /* "Net Icome" value label */
  fc_snprintf(cbuf, sizeof(cbuf), "%d", tax - total);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  if (tax - total < 0) {
    pstr->fgcol = *get_theme_color(COLOR_THEME_ECONOMYDLG_NEG_TEXT);
  }

  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

  add_to_gui_list(ID_LABEL, buf);

  w = MAX(w, buf->size.w);
  h += buf->size.h;

  /* gov and taxrate */
  fc_snprintf(cbuf, sizeof(cbuf), _("%s max rate : %d%%"),
              government_name_translation(gov),
              get_player_bonus(client.conn.playing, EFT_MAX_RATES));
  copy_chars_to_utf8_str(pstr2, cbuf);
  max_rate_text = create_text_surf_from_utf8(pstr2);

  FREEUTF8STR(pstr2);

  /* ------------------------- */
  /* Lux rate */

  /* Lux rate lock */
  fc_snprintf(cbuf, sizeof(cbuf), _("Lock"));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= TTF_STYLE_BOLD;

  buf = create_checkbox(pwindow->dst,
                        sdl3_client_flags & CF_CHANGE_TAXRATE_LUX_BLOCK,
                        WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL);
  set_new_checkbox_theme(buf, current_theme->lock_icon,
                         current_theme->unlock_icon);
  buf->info_label = pstr;
  buf->action = toggle_block_callback;
  set_wstate(buf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_LUX_BLOCK_CHECKBOX, buf);

  w2 = adj_size(10) + buf->size.w;

  /* Lux rate slider */
  buf = create_horizontal(current_theme->horiz, pwindow->dst, adj_size(30),
                          (WF_FREE_DATA | WF_RESTORE_BACKGROUND));

  buf->action = horiz_taxrate_callback;
  buf->data.ptr = fc_calloc(1, sizeof(int));
  *(int *)buf->data.ptr = client.conn.playing->economic.luxury;
  set_wstate(buf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_LUX_SCROLLBAR, buf);

  w2 += adj_size(184);

  /* Lux rate iconlabel */

  /* It is important to leave 1 space at the end of this string. */
  fc_snprintf(cbuf, sizeof(cbuf), "%d%% ", client.conn.playing->economic.luxury);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT_PLUS);
  pstr->style |= TTF_STYLE_BOLD;

  buf = create_iconlabel(icons->big_luxury, pwindow->dst, pstr,
                         WF_RESTORE_BACKGROUND);
  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_LUX_LABEL, buf);

  w2 += (adj_size(5) + buf->size.w + adj_size(10));

  /* ------------------------- */
  /* Science rate */

  /* Science rate lock */
  fc_snprintf(cbuf, sizeof(cbuf), _("Lock"));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= TTF_STYLE_BOLD;

  buf = create_checkbox(pwindow->dst,
                        sdl3_client_flags & CF_CHANGE_TAXRATE_SCI_BLOCK,
                        WF_RESTORE_BACKGROUND | WF_WIDGET_HAS_INFO_LABEL);

  set_new_checkbox_theme(buf, current_theme->lock_icon,
                         current_theme->unlock_icon);

  buf->info_label = pstr;
  buf->action = toggle_block_callback;
  set_wstate(buf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_SCI_BLOCK_CHECKBOX, buf);

  /* Science rate slider */
  buf = create_horizontal(current_theme->horiz, pwindow->dst, adj_size(30),
                          (WF_FREE_DATA | WF_RESTORE_BACKGROUND));

  buf->action = horiz_taxrate_callback;
  buf->data.ptr = fc_calloc(1, sizeof(int));
  *(int *)buf->data.ptr = client.conn.playing->economic.science;

  set_wstate(buf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_SCI_SCROLLBAR, buf);

  /* Science rate iconlabel */
  /* It is important to leave 1 space at the end of this string */
  fc_snprintf(cbuf, sizeof(cbuf), "%d%% ", client.conn.playing->economic.science);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT_PLUS);
  pstr->style |= TTF_STYLE_BOLD;

  buf = create_iconlabel(icons->big_colb, pwindow->dst, pstr,
                         WF_RESTORE_BACKGROUND);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_SCI_LABEL, buf);

  /* ---- */

  fc_snprintf(cbuf, sizeof(cbuf), _("Update"));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  buf = create_themeicon_button(current_theme->small_ok_icon, pwindow->dst,
                                pstr, 0);
  buf->action = apply_taxrates_callback;
  set_wstate(buf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_OK_BUTTON, buf);

  /* ---- */

  fc_snprintf(cbuf, sizeof(cbuf), _("Close Dialog (Esc)"));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = pstr;
  buf->action = exit_economy_dialog_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_CANCEL_BUTTON, buf);

  h += adj_size(5);

  /* ------------------------- */
  last = buf;
  if (entries_used > 0) {

    /* Create Imprv Background Icon */
    background = create_surf(adj_size(116), adj_size(116));

    SDL_FillSurfaceRect(background, NULL, map_rgba(background->format, bg_color));

    create_frame(background,
                 0, 0, background->w - 1, background->h - 1,
                 get_theme_color(COLOR_THEME_ECONOMYDLG_FRAME));

    pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
    pstr->style |= (SF_CENTER|TTF_STYLE_BOLD);
    pstr->bgcol = (SDL_Color) {0, 0, 0, 0};

    for (i = 0; i < entries_used; i++) {
      struct improvement_entry *p = &entries[i];
      struct impr_type *pimprove = p->type;

      surf = crop_rect_from_surface(background, NULL);

      fc_snprintf(cbuf, sizeof(cbuf), "%s",
                  improvement_name_translation(pimprove));

      copy_chars_to_utf8_str(pstr, cbuf);
      pstr->style |= TTF_STYLE_BOLD;
      text_name = create_text_surf_smaller_than_w(pstr, surf->w - adj_size(4));

      fc_snprintf(cbuf, sizeof(cbuf), "%s %d\n%s %d",
                  _("Built"), p->count, _("U Total"),p->total_cost);
      copy_chars_to_utf8_str(pstr, cbuf);
      pstr->style &= ~TTF_STYLE_BOLD;

      text = create_text_surf_from_utf8(pstr);

      /*-----------------*/

      zoomed = get_building_surface(pimprove);
      zoomed = zoomSurface(zoomed, DEFAULT_ZOOM * ((float)54 / zoomed->w),
                           DEFAULT_ZOOM * ((float)54 / zoomed->w), 1);

      dst.x = (surf->w - zoomed->w) / 2;
      dst.y = (surf->h / 2 - zoomed->h) / 2;
      alphablit(zoomed, NULL, surf, &dst, 255);
      dst.y += zoomed->h;
      FREESURFACE(zoomed);

      dst.x = (surf->w - text_name->w)/2;
      dst.y += ((surf->h - dst.y) -
                (text_name->h + (icons->big_coin->h + 2) + text->h)) / 2;
      alphablit(text_name, NULL, surf, &dst, 255);

      dst.y += text_name->h;
      if (p->cost) {
        dst.x = (surf->w - p->cost * (icons->big_coin->w + 1))/2;
        for (count = 0; count < p->cost; count++) {
          alphablit(icons->big_coin, NULL, surf, &dst, 255);
          dst.x += icons->big_coin->w + 1;
        }
      } else {

        if (!is_wonder(pimprove)) {
          copy_chars_to_utf8_str(pstr, _("Nation"));
        } else {
          copy_chars_to_utf8_str(pstr, _("Wonder"));
        }
        /* pstr->style &= ~TTF_STYLE_BOLD; */

        zoomed = create_text_surf_from_utf8(pstr);

        dst.x = (surf->w - zoomed->w) / 2;
        alphablit(zoomed, NULL, surf, &dst, 255);
        FREESURFACE(zoomed);
      }

      dst.y += (icons->big_coin->h + adj_size(2));
      dst.x = (surf->w - text->w) / 2;
      alphablit(text, NULL, surf, &dst, 255);

      FREESURFACE(text);
      FREESURFACE(text_name);

      buf = create_icon2(surf, pwindow->dst,
                         (WF_RESTORE_BACKGROUND|WF_FREE_THEME|WF_FREE_DATA));

      set_wstate(buf, FC_WS_NORMAL);

      buf->data.cont = fc_calloc(1, sizeof(struct container));
      buf->data.cont->id0 = improvement_number(p->type);
      buf->data.cont->id1 = p->count;
      buf->action = popup_sell_impr_callback;

      add_to_gui_list(MAX_ID - i, buf);

      if (i > (TARGETS_ROW * TARGETS_COL - 1)) {
        set_wflag(buf, WF_HIDDEN);
      }
    }

    FREEUTF8STR(pstr);
    FREESURFACE(background);

    economy_dlg->end_active_widget_list = last->prev;
    economy_dlg->begin_widget_list = buf;
    economy_dlg->begin_active_widget_list = economy_dlg->begin_widget_list;

    if (entries_used > (TARGETS_ROW * TARGETS_COL)) {
      economy_dlg->active_widget_list = economy_dlg->end_active_widget_list;
      count = create_vertical_scrollbar(economy_dlg,
                                        TARGETS_COL, TARGETS_ROW, TRUE, TRUE);
      h += (TARGETS_ROW * buf->size.h + adj_size(10));
    } else {
      count = 0;
      if (entries_used > TARGETS_COL) {
        h += buf->size.h;
      }
      h += (adj_size(10) + buf->size.h);
    }
    count = TARGETS_COL * buf->size.w + count;
  } else {
    economy_dlg->begin_widget_list = buf;
    h += adj_size(10);
    count = 0;
  }

  area.w = MAX(area.w, MAX(adj_size(10) + w3 + w + w2, count));
  area.h = h;

  background = theme_get_background(active_theme, BACKGROUND_ECONOMYDLG);
  if (resize_window(pwindow, background, NULL,
                    (pwindow->size.w - pwindow->area.w) + area.w,
                    (pwindow->size.h - pwindow->area.h) + area.h)) {
    FREESURFACE(background);
  }

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* "Treasury" value label */
  buf = pwindow->prev;
  buf->size.x = area.x + adj_size(10) + treasury_text->w;
  buf->size.y = area.y + adj_size(5);

  w = treasury_text->w + buf->size.w;
  h = buf->size.h;

  /* "Tax Rate" value label */
  buf = buf->prev;
  buf->size.x = area.x + adj_size(10) + tax_rate_text->w;
  buf->size.y = buf->next->size.y + buf->next->size.h;

  w = MAX(w, tax_rate_text->w + buf->size.w);
  h += buf->size.h;

  /* "Total Income" value label */
  buf = buf->prev;
  buf->size.x = area.x + adj_size(10) + total_income_text->w;
  buf->size.y = buf->next->size.y + buf->next->size.h;

  w = MAX(w, total_income_text->w + buf->size.w);
  h += buf->size.h;

  /* "Total Cost" value label */
  buf = buf->prev;
  buf->size.x = area.x + adj_size(10) + total_cost_text->w;
  buf->size.y = buf->next->size.y + buf->next->size.h;

  w = MAX(w, total_cost_text->w + buf->size.w);
  h += buf->size.h;

  /* "Net Income" value label */
  buf = buf->prev;
  buf->size.x = area.x + adj_size(10) + net_income_text->w;
  buf->size.y = buf->next->size.y + buf->next->size.h;

  w = MAX(w, net_income_text->w + buf->size.w);
  h += buf->size.h;

  /* Backgrounds */
  dst.x = area.x;
  dst.y = area.y;
  dst.w = area.w;
  dst.h = h + adj_size(15);
  h = dst.h;

  fill_rect_alpha(pwindow->theme, &dst, &bg_color2);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w - 1, dst.h - 1,
               get_theme_color(COLOR_THEME_ECONOMYDLG_FRAME));

  /* draw statical strings */
  dst.x = area.x + adj_size(10);
  dst.y = area.y + adj_size(5);

  /* "Treasury */
  alphablit(treasury_text, NULL, pwindow->theme, &dst, 255);
  dst.y += treasury_text->h;
  FREESURFACE(treasury_text);

  /* Tax Rate */
  alphablit(tax_rate_text, NULL, pwindow->theme, &dst, 255);
  dst.y += tax_rate_text->h;
  FREESURFACE(tax_rate_text);

  /* Total Income */
  alphablit(total_income_text, NULL, pwindow->theme, &dst, 255);
  dst.y += total_income_text->h;
  FREESURFACE(total_income_text);

  /* Total Cost */
  alphablit(total_cost_text, NULL, pwindow->theme, &dst, 255);
  dst.y += total_cost_text->h;
  FREESURFACE(total_cost_text);

  /* Net Income */
  alphablit(net_income_text, NULL, pwindow->theme, &dst, 255);
  dst.y += net_income_text->h;
  FREESURFACE(net_income_text);

  /* gov and taxrate */
  dst.x = area.x + adj_size(10) + w + ((area.w - (w + adj_size(10)) - max_rate_text->w) / 2);
  dst.y = area.y + adj_size(5);

  alphablit(max_rate_text, NULL, pwindow->theme, &dst, 255);
  dst.y += (max_rate_text->h + 1);
  FREESURFACE(max_rate_text);

  /* Luxuries Horizontal Scrollbar Background */
  dst.x = area.x + adj_size(10) + w + (area.w - (w + adj_size(10)) - adj_size(184)) / 2;
  dst.w = adj_size(184);
  dst.h = current_theme->horiz->h - adj_size(2);

  fill_rect_alpha(pwindow->theme, &dst, &bg_color3);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w - 1, dst.h - 1,
               get_theme_color(COLOR_THEME_ECONOMYDLG_FRAME));

  /* lock icon */
  buf = buf->prev;
  buf->size.x = dst.x - buf->size.w;
  buf->size.y = dst.y - adj_size(2);

  /* lux scrollbar */
  buf = buf->prev;
  buf->size.x = dst.x + adj_size(2) + (client.conn.playing->economic.luxury * 3) / 2;
  buf->size.y = dst.y -1;

  /* lux rate */
  buf = buf->prev;
  buf->size.x = dst.x + dst.w + adj_size(5);
  buf->size.y = dst.y + 1;

  /* Science Horizontal Scrollbar Background */
  dst.y += current_theme->horiz->h + 1;
  fill_rect_alpha(pwindow->theme, &dst, &bg_color3);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w - 1, dst.h - 1,
               get_theme_color(COLOR_THEME_ECONOMYDLG_FRAME));

  /* science lock icon */
  buf = buf->prev;
  buf->size.x = dst.x - buf->size.w;
  buf->size.y = dst.y - adj_size(2);

  /* science scrollbar */
  buf = buf->prev;
  buf->size.x = dst.x + adj_size(2) + (client.conn.playing->economic.science * 3) / 2;
  buf->size.y = dst.y -1;

  /* science rate */
  buf = buf->prev;
  buf->size.x = dst.x + dst.w + adj_size(5);
  buf->size.y = dst.y + 1;

  /* update */
  buf = buf->prev;
  buf->size.x = dst.x + (dst.w - buf->size.w) / 2;
  buf->size.y = dst.y + dst.h + adj_size(3);

  /* cancel */
  buf = buf->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);
  /* ------------------------------- */

  if (entries_used > 0) {
    setup_vertical_widgets_position(TARGETS_COL,
                                    area.x,
                                    area.y + h,
                                    0, 0, economy_dlg->begin_active_widget_list,
                                    economy_dlg->end_active_widget_list);
    if (economy_dlg->scroll) {
      setup_vertical_scrollbar_area(economy_dlg->scroll,
                                    area.x + area.w - 1,
                                    area.y + h,
                                    area.h - h - 1, TRUE);
    }
  }

  /* ------------------------ */
  redraw_group(economy_dlg->begin_widget_list, pwindow, 0);
  widget_mark_dirty(pwindow);
  flush_dirty();
}

/* ===================================================================== */
/* ======================== Science Report ============================= */
/* ===================================================================== */
static struct small_dialog *science_dlg = NULL;

static struct advanced_dialog *change_tech_dlg = NULL;

/**********************************************************************//**
  Create icon surface for a tech.
**************************************************************************/
SDL_Surface *create_select_tech_icon(utf8_str *pstr, Tech_type_id tech_id,
                                     enum tech_info_mode mode)
{
  SDL_Surface *surf, *text, *tmp, *tmp2;
  SDL_Surface *surf_array[10], **buf_array;
  SDL_Rect dst;
  SDL_Color color;
  int w, h;

  color = *get_tech_color(tech_id);
  switch (mode) {
  case TIM_SMALL_MODE:
    h = adj_size(40);
    w = adj_size(135);
    break;
  case TIM_MED_MODE:
    color = *get_theme_color(COLOR_THEME_SCIENCEDLG_MED_TECHICON_BG);
    fc__fallthrough; /* No break, continue to setting default h & w */
  default:
    h = adj_size(200);
    w = adj_size(100);
    break;
  }

  text = create_text_surf_smaller_than_w(pstr, adj_size(100 - 4));

  /* Create label surface */
  surf = create_surf(w, h);

  if (tech_id == research_get(client_player())->researching) {
    color.a = 180;
  } else {
    color.a = 128;
  }

  SDL_FillSurfaceRect(surf, NULL, map_rgba(surf->format, color));

  create_frame(surf,
               0,0, surf->w - 1, surf->h - 1,
               get_theme_color(COLOR_THEME_SCIENCEDLG_FRAME));

  tmp = get_tech_icon(tech_id);

  if (mode == TIM_SMALL_MODE) {
    /* Draw name tech text */
    dst.x = adj_size(35) + (surf->w - text->w - adj_size(35)) / 2;
    dst.y = (surf->h - text->h) / 2;
    alphablit(text, NULL, surf, &dst, 255);
    FREESURFACE(text);

    /* Draw tech icon */
    text = resize_surface(tmp, adj_size(25), adj_size(25), 1);
    dst.x = (adj_size(35) - text->w) / 2;
    dst.y = (surf->h - text->h) / 2;
    alphablit(text, NULL, surf, &dst, 255);
    FREESURFACE(text);

  } else {

    /* Draw name tech text */
    dst.x = (surf->w - text->w) / 2;
    dst.y = adj_size(20);
    alphablit(text, NULL, surf, &dst, 255);
    dst.y += text->h + adj_size(10);
    FREESURFACE(text);

    /* Draw tech icon */
    dst.x = (surf->w - tmp->w) / 2;
    alphablit(tmp, NULL, surf, &dst, 255);
    dst.y += tmp->w + adj_size(10);

    /* Fill array with iprvm. icons */
    w = 0;
    improvement_iterate(pimprove) {
      requirement_vector_iterate(&pimprove->reqs, preq) {
        if (VUT_ADVANCE == preq->source.kind
            && advance_number(preq->source.value.advance) == tech_id) {
          tmp2 = get_building_surface(pimprove);
          surf_array[w++] = zoomSurface(tmp2, DEFAULT_ZOOM * ((float)36 / tmp2->w),
                                        DEFAULT_ZOOM * ((float)36 / tmp2->w), 1);
        }
      } requirement_vector_iterate_end;
    } improvement_iterate_end;

    if (w) {
      if (w >= 2) {
        dst.x = (surf->w - 2 * surf_array[0]->w) / 2;
      } else {
        dst.x = (surf->w - surf_array[0]->w) / 2;
      }

      /* Draw iprvm. icons */
      buf_array = surf_array;
      h = 0;
      while (w) {
        alphablit(*buf_array, NULL, surf, &dst, 255);
        dst.x += (*buf_array)->w;
        w--;
        h++;
        if (!(h % 2)) {
          if (w >= 2) {
            dst.x = (surf->w - 2 * (*buf_array)->w) / 2;
          } else {
            dst.x = (surf->w - (*buf_array)->w) / 2;
          }
          dst.y += (*buf_array)->h;
          h = 0;
        } /* h == 2 */
        buf_array++;
      } /* while */
      dst.y += surf_array[0]->h + adj_size(5);
    } /* if (w) */
  /* -------------------------------------------------------- */
    w = 0;
    unit_type_iterate(un) {
      if (is_tech_req_for_utype(un, advance_by_number(tech_id))) {
        surf_array[w++] = adj_surf(get_unittype_surface(un, direction8_invalid()));
      }
    } unit_type_iterate_end;

    if (w) {
      if (w < 2) {
        /* w == 1 */
        if (surf_array[0]->w > 64) {
          float zoom = DEFAULT_ZOOM * (64.0 / surf_array[0]->w);
          SDL_Surface *zoomed = zoomSurface(surf_array[0], zoom, zoom, 1);

          dst.x = (surf->w - zoomed->w) / 2;
          alphablit(zoomed, NULL, surf, &dst, 255);
          FREESURFACE(zoomed);
        } else {
          dst.x = (surf->w - surf_array[0]->w) / 2;
          alphablit(surf_array[0], NULL, surf, &dst, 255);
        }
        FREESURFACE(surf_array[0]);
      } else {
        float zoom;

        if (w > 2) {
          zoom = DEFAULT_ZOOM * (38.0 / surf_array[0]->w);
        } else {
          zoom = DEFAULT_ZOOM * (45.0 / surf_array[0]->w);
        }
        dst.x = (surf->w - (surf_array[0]->w * 2) * zoom - 2) / 2;
        buf_array = surf_array;
        h = 0;
        while (w) {
          SDL_Surface *zoomed = zoomSurface((*buf_array), zoom, zoom, 1);

          FREESURFACE(*buf_array);
          alphablit(zoomed, NULL, surf, &dst, 255);
          dst.x += zoomed->w + 2;
          w--;
          h++;
          if (!(h % 2)) {
            if (w >= 2) {
              dst.x = (surf->w - 2 * zoomed->w - 2 ) / 2;
            } else {
              dst.x = (surf->w - zoomed->w) / 2;
            }
            dst.y += zoomed->h + 2;
            h = 0;
          } /* h == 2 */
          buf_array++;
          FREESURFACE(zoomed);
        } /* while */
      } /* w > 1 */
    } /* if (w) */
  }

  FREESURFACE(tmp);

  return surf;
}

/**********************************************************************//**
  Enable science dialog group ( without window )
**************************************************************************/
static void enable_science_dialog(void)
{
  set_group_state(science_dlg->begin_widget_list,
                  science_dlg->end_widget_list->prev, FC_WS_NORMAL);
}

/**********************************************************************//**
  Disable science dialog group ( without window )
**************************************************************************/
static void disable_science_dialog(void)
{
  set_group_state(science_dlg->begin_widget_list,
                  science_dlg->end_widget_list->prev, FC_WS_DISABLED);
}

/**********************************************************************//**
  Update the science report.
**************************************************************************/
void real_science_report_dialog_update(void *unused)
{
  SDL_Color bg_color = {255, 255, 255, 136};

  if (science_dlg) {
    const struct research *presearch = research_get(client_player());
    char cbuf[128];
    utf8_str *str;
    SDL_Surface *msurf;
    SDL_Surface *colb_surface = icons->big_colb;
    int step, i, cost;
    SDL_Rect dest;
    struct widget *change_research_button;
    struct widget *change_research_goal_button;
    SDL_Rect area;
    struct widget *pwindow = science_dlg->end_widget_list;

    area = pwindow->area;
    change_research_button = pwindow->prev;
    change_research_goal_button = pwindow->prev->prev;

    if (A_UNSET != presearch->researching) {
      cost = presearch->client.researching_cost;
    } else {
      cost = 0;
    }

    /* Update current research icons */
    FREESURFACE(change_research_button->theme);
    change_research_button->theme = get_tech_icon(presearch->researching);
    FREESURFACE(change_research_goal_button->theme);
    change_research_goal_button->theme = get_tech_icon(presearch->tech_goal);

    /* Redraw Window */
    widget_redraw(pwindow);

    /* ------------------------------------- */

    /* Research progress text */
    str = create_utf8_from_char_fonto(science_dialog_text(), FONTO_ATTENTION);
    str->style |= SF_CENTER;
    str->fgcol = *get_theme_color(COLOR_THEME_SCIENCEDLG_TEXT);

    msurf = create_text_surf_from_utf8(str);

    dest.x = area.x + (area.w - msurf->w) / 2;
    dest.y = area.y + adj_size(2);
    alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);

    dest.y += msurf->h + adj_size(4);

    FREESURFACE(msurf);

    dest.x = area.x + adj_size(16);

    /* separator */
    create_line(pwindow->dst->surface,
                dest.x, dest.y, (area.x + area.w - adj_size(16)), dest.y,
                get_theme_color(COLOR_THEME_SCIENCEDLG_FRAME));

    dest.y += adj_size(6);

    widget_set_position(change_research_button, dest.x, dest.y + adj_size(18));

    /* current research text */
    fc_snprintf(cbuf, sizeof(cbuf), "%s: %s",
                research_advance_name_translation(presearch,
                                                  presearch->researching),
                get_science_target_text(NULL));

    copy_chars_to_utf8_str(str, cbuf);

    msurf = create_text_surf_from_utf8(str);

    dest.x = change_research_button->size.x + change_research_button->size.w + adj_size(10);

    alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);

    dest.y += msurf->h + adj_size(4);

    FREESURFACE(msurf);

    /* progress bar */
    if (cost > 0) {
      int cost_div_safe = cost - 1;

      cost_div_safe = (cost_div_safe != 0 ? cost_div_safe : 1);
      dest.w = cost * colb_surface->w;
      step = colb_surface->w;
      if (dest.w > (area.w - dest.x - adj_size(16))) {
        dest.w = (area.w - dest.x - adj_size(16));
        step = ((area.w - dest.x - adj_size(16)) - colb_surface->w) / cost_div_safe;

        if (step == 0) {
          step = 1;
        }
      }

      dest.h = colb_surface->h + adj_size(4);
      fill_rect_alpha(pwindow->dst->surface, &dest, &bg_color);

      create_frame(pwindow->dst->surface,
                   dest.x - 1, dest.y - 1, dest.w, dest.h,
                   get_theme_color(COLOR_THEME_SCIENCEDLG_FRAME));

      if (cost > adj_size(286)) {
        cost = adj_size(286) * ((float) presearch->bulbs_researched / cost);
      } else {
        cost = (float) cost * ((float) presearch->bulbs_researched / cost);
      }

      dest.y += adj_size(2);
      for (i = 0; i < cost; i++) {
        alphablit(colb_surface, NULL, pwindow->dst->surface, &dest, 255);
        dest.x += step;
      }
    }

    /* improvement icons */

    dest.y += dest.h + adj_size(4);
    dest.x = change_research_button->size.x + change_research_button->size.w + adj_size(10);

    /* buildings */
    improvement_iterate(pimprove) {
      requirement_vector_iterate(&pimprove->reqs, preq) {
        if (VUT_ADVANCE == preq->source.kind
            && (advance_number(preq->source.value.advance)
                == presearch->researching)) {
          msurf = adj_surf(get_building_surface(pimprove));
          alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);
          dest.x += msurf->w + 1;
        }
      } requirement_vector_iterate_end;
    } improvement_iterate_end;

    dest.x += adj_size(5);

    /* Units */
    unit_type_iterate(un) {
      if (is_tech_req_for_utype(un, advance_by_number(presearch->researching))) {
        SDL_Surface *usurf = get_unittype_surface(un, direction8_invalid());
        int w = usurf->w;

        if (w > 64) {
          float zoom = DEFAULT_ZOOM * (64.0 / w);

          msurf = zoomSurface(usurf, zoom, zoom, 1);
          alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);
          dest.x += msurf->w + adj_size(2);
          FREESURFACE(msurf);
        } else {
          msurf = adj_surf(usurf);
          alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);
          dest.x += msurf->w + adj_size(2);
        }
      }
    } unit_type_iterate_end;

    /* -------------------------------- */
    /* draw separator line */
    dest.x = area.x + adj_size(16);
    dest.y += adj_size(48) + adj_size(6);

    create_line(pwindow->dst->surface,
                dest.x, dest.y, (area.x + area.w - adj_size(16)), dest.y,
                get_theme_color(COLOR_THEME_SCIENCEDLG_FRAME));

    dest.x = change_research_button->size.x;
    dest.y += adj_size(6);

    widget_set_position(change_research_goal_button, dest.x, dest.y + adj_size(16));

    /* -------------------------------- */

    /* Goals */
    if (A_UNSET != presearch->tech_goal) {
      /* current goal text */
      copy_chars_to_utf8_str(str, research_advance_name_translation
                             (presearch, presearch->tech_goal));
      msurf = create_text_surf_from_utf8(str);

      dest.x = change_research_goal_button->size.x + change_research_goal_button->size.w + adj_size(10);
      alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);

      dest.y += msurf->h;

      FREESURFACE(msurf);

      copy_chars_to_utf8_str(str, get_science_goal_text
                             (presearch->tech_goal));
      msurf = create_text_surf_from_utf8(str);

      dest.x = change_research_goal_button->size.x + change_research_goal_button->size.w + adj_size(10);
      alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);

      dest.y += msurf->h + adj_size(6);

      FREESURFACE(msurf);

      /* buildings */
      improvement_iterate(pimprove) {
        requirement_vector_iterate(&pimprove->reqs, preq) {
          if (VUT_ADVANCE == preq->source.kind
              && (advance_number(preq->source.value.advance)
                  == presearch->tech_goal)) {
            msurf = adj_surf(get_building_surface(pimprove));
            alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);
            dest.x += msurf->w + 1;
          }
        } requirement_vector_iterate_end;
      } improvement_iterate_end;

      dest.x += adj_size(6);

      /* units */
      unit_type_iterate(un) {
        if (is_tech_req_for_utype(un, advance_by_number(presearch->tech_goal))) {
          SDL_Surface *usurf = get_unittype_surface(un, direction8_invalid());
          int w = usurf->w;

          if (w > 64) {
            float zoom = DEFAULT_ZOOM * (64.0 / w);

            msurf = zoomSurface(usurf, zoom, zoom, 1);
            alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);
            dest.x += msurf->w + adj_size(2);
            FREESURFACE(msurf);
          } else {
            msurf = adj_surf(usurf);
            alphablit(msurf, NULL, pwindow->dst->surface, &dest, 255);
            dest.x += msurf->w + adj_size(2);
          }
        }
      } unit_type_iterate_end;
    }

    /* -------------------------------- */
    widget_mark_dirty(pwindow);
    redraw_group(science_dlg->begin_widget_list, pwindow->prev, 1);
    flush_dirty();

    FREEUTF8STR(str);
  }
}

/**********************************************************************//**
  Close science report dialog.
**************************************************************************/
static void science_report_dialog_popdown(void)
{
  if (science_dlg) {
    popdown_window_group_dialog(science_dlg->begin_widget_list,
                                science_dlg->end_widget_list);
    FC_FREE(science_dlg);
    set_wstate(get_research_widget(), FC_WS_NORMAL);
    widget_redraw(get_research_widget());
    widget_mark_dirty(get_research_widget());
    flush_dirty();
  }
}

/**********************************************************************//**
  Close research target changing dialog.
**************************************************************************/
static int exit_change_tech_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (change_tech_dlg) {
      popdown_window_group_dialog(change_tech_dlg->begin_widget_list,
                                  change_tech_dlg->end_widget_list);
      FC_FREE(change_tech_dlg->scroll);
      FC_FREE(change_tech_dlg);
      enable_science_dialog();
      if (pwidget) {
        flush_dirty();
      }
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with button of specific Tech.
**************************************************************************/
static int change_research_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    dsend_packet_player_research(&client.conn, (MAX_ID - pwidget->id));
    exit_change_tech_dlg_callback(NULL);
  } else if (main_data.event.button.button == SDL_BUTTON_MIDDLE) {
    popup_tech_info((MAX_ID - pwidget->id));
  }

  return -1;
}

/**********************************************************************//**
  This function is used by change research and change goals dlgs.
**************************************************************************/
static int change_research_goal_dialog_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (select_window_group_dialog(change_tech_dlg->begin_widget_list, pwindow)) {
      widget_flush(pwindow);
    }
  }

  return -1;
}

/**********************************************************************//**
  Popup dialog to change current research.
**************************************************************************/
static void popup_change_research_dialog(void)
{
  const struct research *presearch = research_get(client_player());
  struct widget *buf = NULL;
  struct widget *pwindow;
  utf8_str *pstr;
  SDL_Surface *surf;
  int max_col, max_row, col, i, count = 0, h;
  SDL_Rect area;
  Tech_type_id ac;

  if (is_future_tech(presearch->researching)) {
    return;
  }

  ac = advance_count();
  advance_index_iterate_max(A_FIRST, aidx, ac) {
    if (!research_invention_gettable(presearch, aidx, FALSE)) {
      continue;
    }
    count++;
  } advance_index_iterate_max_end;

  if (count < 2) {
    return;
  }

  change_tech_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  pstr = create_utf8_from_char_fonto(_("What should we focus on now?"),
                                     FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);
  change_tech_dlg->end_widget_list = pwindow;
  set_wstate(pwindow, FC_WS_NORMAL);
  pwindow->action = change_research_goal_dialog_callback;

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_REASARCH_WINDOW, pwindow);

  area = pwindow->area;

  /* ------------------------- */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  area.w += buf->size.w + adj_size(10);
  buf->action = exit_change_tech_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, buf);

  /* ------------------------- */
  /* max col - 104 is select tech widget width */
  max_col = (main_window_width() - (pwindow->size.w - pwindow->area.w) - adj_size(2)) / adj_size(104);
  /* max row - 204 is select tech widget height */
  max_row = (main_window_height() - (pwindow->size.h - pwindow->area.h) - adj_size(2)) / adj_size(204);

  /* make space on screen for scrollbar */
  if (max_col * max_row < count) {
    max_col--;
  }

  if (count < max_col + 1) {
    col = count;
  } else {
    if (count < max_col + 3) {
      col = max_col - 2;
    } else {
      if (count < max_col + 5) {
        col = max_col - 1;
      } else {
        col = max_col;
      }
    }
  }

  pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  count = 0;
  h = col * max_row;
  advance_index_iterate_max(A_FIRST, aidx, ac) {
    if (!research_invention_gettable(presearch, aidx, FALSE)) {
      continue;
    }

    count++;

    copy_chars_to_utf8_str(pstr, advance_name_translation(advance_by_number(aidx)));
    surf = create_select_tech_icon(pstr, aidx, TIM_MED_MODE);
    buf = create_icon2(surf, pwindow->dst,
                       WF_FREE_THEME | WF_RESTORE_BACKGROUND);

    set_wstate(buf, FC_WS_NORMAL);
    buf->action = change_research_callback;

    add_to_gui_list(MAX_ID - aidx, buf);

    if (count > h) {
      set_wflag(buf, WF_HIDDEN);
    }
  } advance_index_iterate_max_end;

  FREEUTF8STR(pstr);

  change_tech_dlg->begin_widget_list = buf;
  change_tech_dlg->begin_active_widget_list = change_tech_dlg->begin_widget_list;
  change_tech_dlg->end_active_widget_list = change_tech_dlg->end_widget_list->prev->prev;

  /* -------------------------------------------------------------- */

  i = 0;
  if (count > col) {
    count = (count + (col - 1)) / col;
    if (count > max_row) {
      change_tech_dlg->active_widget_list = change_tech_dlg->end_active_widget_list;
      count = max_row;
      i = create_vertical_scrollbar(change_tech_dlg, col, count, TRUE, TRUE);
    }
  } else {
    count = 1;
  }

  disable_science_dialog();

  area.w = MAX(area.w, (col * buf->size.w + adj_size(2) + i));
  area.h = MAX(area.h, count * buf->size.h + adj_size(2));

  /* alloca window theme and win background buffer */
  surf = theme_get_background(active_theme, BACKGROUND_CHANGERESEARCHDLG);
  resize_window(pwindow, surf, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);
  FREESURFACE(surf);

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  setup_vertical_widgets_position(col, area.x + 1,
                                  area.y, 0, 0,
                                  change_tech_dlg->begin_active_widget_list,
                                  change_tech_dlg->end_active_widget_list);

  if (change_tech_dlg->scroll) {
    setup_vertical_scrollbar_area(change_tech_dlg->scroll,
                                  area.x + area.w, area.y,
                                  area.h, TRUE);
  }

  redraw_group(change_tech_dlg->begin_widget_list, pwindow, FALSE);

  widget_flush(pwindow);
}

/**********************************************************************//**
  User chose spesic tech as research goal.
**************************************************************************/
static int change_research_goal_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    dsend_packet_player_tech_goal(&client.conn, (MAX_ID - pwidget->id));

    exit_change_tech_dlg_callback(NULL);

    /* Following is to make the menu go back to the current goal;
     * there may be a better way to do this?  --dwp */
    real_science_report_dialog_update(NULL);
  } else if (main_data.event.button.button == SDL_BUTTON_MIDDLE) {
    popup_tech_info((MAX_ID - pwidget->id));
  }

  return -1;
}

/**********************************************************************//**
  Popup dialog to change research goal.
**************************************************************************/
static void popup_change_research_goal_dialog(void)
{
  const struct research *presearch = research_get(client_player());
  struct widget *buf = NULL;
  struct widget *pwindow;
  utf8_str *pstr;
  SDL_Surface *surf;
  char cbuf[128];
  int max_col, max_row, col, i, count = 0, h , num;
  SDL_Rect area;
  Tech_type_id ac = advance_count();

  /* Collect all techs which are reachable in under 11 steps
   * hist will hold afterwards the techid of the current choice
   */
  advance_index_iterate_max(A_FIRST, aidx, ac) {
    if (research_invention_reachable(presearch, aidx)
        && TECH_KNOWN != research_invention_state(presearch, aidx)
        && (11 > research_goal_unknown_techs(presearch, aidx)
            || aidx == presearch->tech_goal)) {
      count++;
    }
  } advance_index_iterate_max_end;

  if (count < 1) {
    return;
  }

  change_tech_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  pstr = create_utf8_from_char_fonto(_("Select target :"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);
  change_tech_dlg->end_widget_list = pwindow;
  set_wstate(pwindow, FC_WS_NORMAL);
  pwindow->action = change_research_goal_dialog_callback;

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_WINDOW, pwindow);

  area = pwindow->area;

  /* ------------------------- */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  area.w += buf->size.w + adj_size(10);
  buf->action = exit_change_tech_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_CANCEL_BUTTON, buf);

  /* ------------------------- */
  /* max col - 104 is goal tech widget width */
  max_col = (main_window_width() - (pwindow->size.w - pwindow->area.w) - adj_size(2)) / adj_size(104);

  /* max row - 204 is goal tech widget height */
  max_row = (main_window_height() - (pwindow->size.h - pwindow->area.h) - adj_size(2)) / adj_size(204);

  /* make space on screen for scrollbar */
  if (max_col * max_row < count) {
    max_col--;
  }

  if (count < max_col + 1) {
    col = count;
  } else {
    if (count < max_col + 3) {
      col = max_col - 2;
    } else {
      if (count < max_col + 5) {
        col = max_col - 1;
      } else {
        col = max_col;
      }
    }
  }

  pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  /* Collect all techs which are reachable in under 11 steps
   * hist will hold afterwards the techid of the current choice
   */
  count = 0;
  h = col * max_row;
  advance_index_iterate_max(A_FIRST, aidx, ac) {
    if (research_invention_reachable(presearch, aidx)
        && TECH_KNOWN != research_invention_state(presearch, aidx)
        && (11 > (num = research_goal_unknown_techs(presearch, aidx))
            || aidx == presearch->tech_goal)) {

      count++;
      fc_snprintf(cbuf, sizeof(cbuf), "%s\n%d %s",
                  advance_name_translation(advance_by_number(aidx)),
                  num,
                  PL_("step", "steps", num));
      copy_chars_to_utf8_str(pstr, cbuf);
      surf = create_select_tech_icon(pstr, aidx, TIM_FULL_MODE);
      buf = create_icon2(surf, pwindow->dst,
                         WF_FREE_THEME | WF_RESTORE_BACKGROUND);

      set_wstate(buf, FC_WS_NORMAL);
      buf->action = change_research_goal_callback;

      add_to_gui_list(MAX_ID - aidx, buf);

      if (count > h) {
        set_wflag(buf, WF_HIDDEN);
      }
    }
  } advance_index_iterate_max_end;

  FREEUTF8STR(pstr);

  change_tech_dlg->begin_widget_list = buf;
  change_tech_dlg->begin_active_widget_list = change_tech_dlg->begin_widget_list;
  change_tech_dlg->end_active_widget_list = change_tech_dlg->end_widget_list->prev->prev;

  /* -------------------------------------------------------------- */

  i = 0;
  if (count > col) {
    count = (count + (col-1)) / col;
    if (count > max_row) {
      change_tech_dlg->active_widget_list = change_tech_dlg->end_active_widget_list;
      count = max_row;
      i = create_vertical_scrollbar(change_tech_dlg, col, count, TRUE, TRUE);
    }
  } else {
    count = 1;
  }

  disable_science_dialog();

  area.w = MAX(area.w, (col * buf->size.w + adj_size(2) + i));
  area.h = MAX(area.h, count * buf->size.h + adj_size(2));

  /* Alloca window theme and win background buffer */
  surf = theme_get_background(active_theme, BACKGROUND_CHANGERESEARCHDLG);
  resize_window(pwindow, surf, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);
  FREESURFACE(surf);

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* Exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  setup_vertical_widgets_position(col, area.x + 1,
                                  area.y, 0, 0,
                                  change_tech_dlg->begin_active_widget_list,
                                  change_tech_dlg->end_active_widget_list);

  if (change_tech_dlg->scroll) {
    setup_vertical_scrollbar_area(change_tech_dlg->scroll,
                                  area.x + area.w, area.y,
                                  area.h, TRUE);
  }

  redraw_group(change_tech_dlg->begin_widget_list, pwindow, FALSE);

  widget_flush(pwindow);
}

/**********************************************************************//**
  User interacted with science dialog window.
**************************************************************************/
static int science_dialog_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (!change_tech_dlg) {
      if (select_window_group_dialog(science_dlg->begin_widget_list, pwindow)) {
        widget_flush(pwindow);
      }
      if (move_window_group_dialog(science_dlg->begin_widget_list, pwindow)) {
        real_science_report_dialog_update(NULL);
      }
    }
  }

  return -1;
}

/**********************************************************************//**
  Open research target changing dialog.
**************************************************************************/
static int popup_change_research_dialog_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    set_wstate(pwidget, FC_WS_NORMAL);
    selected_widget = NULL;
    widget_redraw(pwidget);
    widget_flush(pwidget);

    popup_change_research_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Open research goal changing dialog.
**************************************************************************/
static int popup_change_research_goal_dialog_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    set_wstate(pwidget, FC_WS_NORMAL);
    selected_widget = NULL;
    widget_redraw(pwidget);
    widget_flush(pwidget);

    popup_change_research_goal_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Close science dialog.
**************************************************************************/
static int popdown_science_dialog_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    science_report_dialog_popdown();
  }

  return -1;
}

/**********************************************************************//**
  Popup (or raise) the science report(F6). It may or may not be modal.
**************************************************************************/
void science_report_dialog_popup(bool raise)
{
  const struct research *presearch;
  struct widget *pwidget, *pwindow;
  struct widget *change_research_button;
  struct widget *change_research_goal_button;
  struct widget *exit_button;
  utf8_str *pstr;
  SDL_Surface *background, *tech_icon;
  int count;
  SDL_Rect area;

  if (science_dlg) {
    return;
  }

  presearch = research_get(client_player());

  /* Disable research button */
  pwidget = get_research_widget();
  set_wstate(pwidget, FC_WS_DISABLED);
  widget_redraw(pwidget);
  widget_mark_dirty(pwidget);

  science_dlg = fc_calloc(1, sizeof(struct small_dialog));

  /* TRANS: Research report title */
  pstr = create_utf8_from_char_fonto(_("Research"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

#ifdef GUI_SDL3_SMALL_SCREEN
  pwindow = create_window(NULL, pstr, 200, 132, 0);
#else  /* GUI_SDL3_SMALL_SCREEN */
  pwindow = create_window(NULL, pstr, adj_size(400), adj_size(246), 0);
#endif /* GUI_SDL3_SMALL_SCREEN */
  set_wstate(pwindow, FC_WS_NORMAL);
  pwindow->action = science_dialog_callback;

  science_dlg->end_widget_list = pwindow;

  background = theme_get_background(active_theme, BACKGROUND_SCIENCEDLG);
  FREESURFACE(pwindow->theme);
  pwindow->theme = resize_surface(background, pwindow->size.w,
                                  pwindow->size.h, 1);
  FREESURFACE(background);

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  add_to_gui_list(ID_SCIENCE_DLG_WINDOW, pwindow);

  /* count number of researchable techs */
  count = 0;
  advance_index_iterate(A_FIRST, i) {
    if (research_invention_reachable(presearch, i)
        && TECH_KNOWN != research_invention_state(presearch, i)) {
      count++;
    }
  } advance_index_iterate_end;

  /* current research icon */
  tech_icon = get_tech_icon(presearch->researching);
  change_research_button = create_icon2(tech_icon, pwindow->dst,
                                        WF_RESTORE_BACKGROUND | WF_FREE_THEME);

  change_research_button->action = popup_change_research_dialog_callback;
  if (count > 0) {
    set_wstate(change_research_button, FC_WS_NORMAL);
  }

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_REASARCH_BUTTON, change_research_button);

  /* current research goal icon */
  tech_icon = get_tech_icon(presearch->tech_goal);
  change_research_goal_button = create_icon2(tech_icon, pwindow->dst,
                                             WF_RESTORE_BACKGROUND | WF_FREE_THEME);

  change_research_goal_button->action = popup_change_research_goal_dialog_callback;
  if (count > 0) {
    set_wstate(change_research_goal_button, FC_WS_NORMAL);
  }

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_BUTTON, change_research_goal_button);

  /* ------ */
  /* Exit button */
  exit_button = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                                 WF_WIDGET_HAS_INFO_LABEL
                                 | WF_RESTORE_BACKGROUND);
  exit_button->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                        FONTO_ATTENTION);
  exit_button->action = popdown_science_dialog_callback;
  set_wstate(exit_button, FC_WS_NORMAL);
  exit_button->key = SDLK_ESCAPE;

  add_to_gui_list(ID_SCIENCE_CANCEL_DLG_BUTTON, exit_button);

  widget_set_position(exit_button,
                      area.x + area.w - exit_button->size.w - adj_size(1),
                      pwindow->size.y + adj_size(2));

  /* ======================== */
  science_dlg->begin_widget_list = exit_button;

  real_science_report_dialog_update(NULL);
}

/**********************************************************************//**
  Popdown all the science reports (report, change tech, change goals).
**************************************************************************/
void science_report_dialogs_popdown_all(void)
{
  if (change_tech_dlg) {
    popdown_window_group_dialog(change_tech_dlg->begin_widget_list,
                                change_tech_dlg->end_widget_list);
    FC_FREE(change_tech_dlg->scroll);
    FC_FREE(change_tech_dlg);
  }
  if (science_dlg) {
    popdown_window_group_dialog(science_dlg->begin_widget_list,
                                science_dlg->end_widget_list);
    FC_FREE(science_dlg);
    set_wstate(get_research_widget(), FC_WS_NORMAL);
    widget_redraw(get_research_widget());
    widget_mark_dirty(get_research_widget());
  }
}

/**********************************************************************//**
  Resize and redraw the requirement tree.
**************************************************************************/
void science_report_dialog_redraw(void)
{
  /* No requirement tree yet. */
}

/* ===================================================================== */
/* ======================== Endgame Report ============================= */
/* ===================================================================== */

static char eg_buffer[150 * MAX_NUM_PLAYERS];
static int eg_player_count = 0;
static int eg_players_received = 0;

/**********************************************************************//**
  Show a dialog with player statistics at endgame.
  TODO: Display all statistics in packet_endgame_report.
**************************************************************************/
void endgame_report_dialog_start(const struct packet_endgame_report *packet)
{
  eg_buffer[0] = '\0';
  eg_player_count = packet->player_num;
  eg_players_received = 0;
}

/**********************************************************************//**
  Received endgame report information about single player
**************************************************************************/
void endgame_report_dialog_player(const struct packet_endgame_player *packet)
{
  const struct player *pplayer = player_by_number(packet->player_id);

  eg_players_received++;

  cat_snprintf(eg_buffer, sizeof(eg_buffer),
               PL_("%2d: The %s ruler %s scored %d point\n",
                   "%2d: The %s ruler %s scored %d points\n",
                   packet->score),
               eg_players_received, nation_adjective_for_player(pplayer),
               player_name(pplayer), packet->score);

  if (eg_players_received == eg_player_count) {
    popup_notify_dialog(_("Final Report:"),
                        _("The Greatest Civilizations in the world."),
                        eg_buffer);
  }
}
