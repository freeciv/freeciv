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
#include "government.h"
#include "movement.h"

/* client */
#include "client_main.h"
#include "helpdata.h"

/* gui-sdl2 */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "repodlgs.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"

#include "helpdlg.h"

static struct advanced_dialog *help_dlg = NULL;

struct techs_buttons {
  struct widget *targets[6], *sub_targets[6];
  struct widget *requirement_button[2], *sub_req[4];
  struct widget *dock;
  bool show_tree;
  bool show_full_tree;
};

struct units_buttons {
  struct widget *obsolete_by_button;
  struct widget *requirement_button;
  struct widget *dock;
};

enum help_page_type current_help_dlg = HELP_LAST;

static const int bufsz = 8192;

static int change_tech_callback(struct widget *pwidget);

/**********************************************************************//**
  Open Help Browser without any specific topic in mind
**************************************************************************/
void popup_help_browser(void)
{
  popup_tech_info(A_NONE);
}

/**********************************************************************//**
  Popup the help dialog to get help on the given string topic.  Note that
  the topic may appear in multiple sections of the help (it may be both
  an improvement and a unit, for example).

  The string will be untranslated.
**************************************************************************/
void popup_help_dialog_string(const char *item)
{
  popup_help_dialog_typed(Q_(item), HELP_ANY);
}

/**********************************************************************//**
  Popup the help dialog to display help on the given string topic from
  the given section.

  The string will be translated.
**************************************************************************/
void popup_help_dialog_typed(const char *item, enum help_page_type eHPT)
{
  log_debug("popup_help_dialog_typed : PORT ME");
}

/**********************************************************************//**
  Close the help dialog.
**************************************************************************/
void popdown_help_dialog(void)
{
  if (help_dlg) {
    popdown_window_group_dialog(help_dlg->begin_widget_list,
                                help_dlg->end_widget_list);
    FC_FREE(help_dlg->scroll);
    FC_FREE(help_dlg);
    current_help_dlg = HELP_LAST;
  }
}

/**********************************************************************//**
  User interacted with help dialog window
**************************************************************************/
static int help_dlg_window_callback(struct widget *pwindow)
{
  return -1;
}

/**********************************************************************//**
  User requested closing of the help dialog
**************************************************************************/
static int exit_help_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_help_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User requested new government help
**************************************************************************/
static int change_gov_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_gov_info(MAX_ID - pwidget->id);
  }

  return -1;
}

/**********************************************************************//**
  Show government info
**************************************************************************/
void popup_gov_info(int gov)
{
}

/**********************************************************************//**
  User requested new improvement help
**************************************************************************/
static int change_impr_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_impr_info(MAX_ID - pwidget->id);
  }

  return -1;
}

/**********************************************************************//**
  Refresh improvement help dialog
**************************************************************************/
static void redraw_impr_info_dlg(void)
{
  SDL_Color bg_color = {255, 255, 255, 64};
  struct widget *pwindow = help_dlg->end_widget_list;
  struct units_buttons *store = (struct units_buttons *)pwindow->data.ptr;
  SDL_Rect dst;

  redraw_group(pwindow->prev, pwindow, FALSE);

  dst.x = store->dock->prev->size.x - adj_size(10);
  dst.y = store->dock->prev->size.y - adj_size(10);
  dst.w = pwindow->size.w - (dst.x - pwindow->size.x) - adj_size(10);
  dst.h = pwindow->size.h - (dst.y - pwindow->size.y) - adj_size(10);

  fill_rect_alpha(pwindow->dst->surface, &dst, &bg_color);

  create_frame(pwindow->dst->surface,
               dst.x, dst.y, dst.w, dst.h,
               get_theme_color(COLOR_THEME_HELPDLG_FRAME));

  /*------------------------------------- */
  redraw_group(help_dlg->begin_widget_list, pwindow->prev->prev, FALSE);
  widget_flush(pwindow);
}

/**********************************************************************//**
  Show improvement info
**************************************************************************/
void popup_impr_info(Impr_type_id impr)
{
  SDL_Color bg_color = {255, 255, 255, 128};
  struct widget *pwindow;
  struct units_buttons *store;
  struct widget *close_button = NULL;
  struct widget *list_toggle_button = NULL;
  struct widget *improvement_button = NULL;
  struct widget *impr_name_label = NULL;
  struct widget *cost_label = NULL;
  struct widget *upkeep_label = NULL;
  struct widget *requirement_label = NULL;
  struct widget *requirement_label2 = NULL;
  struct widget *obsolete_by_label = NULL;
  struct widget *obsolete_by_label2 = NULL;
  struct widget *help_text_label = NULL;
  struct widget *dock;
  utf8_str *title;
  utf8_str *pstr;
  SDL_Surface *surf;
  int h, start_x, start_y, impr_type_count;
  bool created, text = FALSE;
  int scrollbar_width = 0;
  struct impr_type *pimpr_type;
  char buffer[64000];
  SDL_Rect area;
  struct advance *obsTech = NULL;

  if (current_help_dlg != HELP_IMPROVEMENT) {
    popdown_help_dialog();
  }

  if (!help_dlg) {
    SDL_Surface *background_tmpl, *background, *impr_name, *icon;
    SDL_Rect dst;

    current_help_dlg = HELP_IMPROVEMENT;
    created = TRUE;

    /* Create dialog */
    help_dlg = fc_calloc(1, sizeof(struct advanced_dialog));
    store = fc_calloc(1, sizeof(struct units_buttons));

    /* Create window */
    title = create_utf8_from_char_fonto(_("Help : Improvements"),
                                        FONTO_ATTENTION);
    title->style |= TTF_STYLE_BOLD;

    pwindow = create_window_skeleton(NULL, title, WF_FREE_DATA);
    pwindow->action = help_dlg_window_callback;
    set_wstate(pwindow , FC_WS_NORMAL);
    pwindow->data.ptr = (void *)store;
    add_to_gui_list(ID_WINDOW, pwindow);

    help_dlg->end_widget_list = pwindow;

    area = pwindow->area;
    /* ------------------ */

    /* Close button */
    close_button = create_themeicon(current_theme->small_cancel_icon,
                                    pwindow->dst,
                                    WF_WIDGET_HAS_INFO_LABEL
                                    | WF_RESTORE_BACKGROUND);
    close_button->info_label
      = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                    FONTO_ATTENTION);
    close_button->action = exit_help_dlg_callback;
    set_wstate(close_button, FC_WS_NORMAL);
    close_button->key = SDLK_ESCAPE;

    add_to_gui_list(ID_BUTTON, close_button);

    /* ------------------ */
    dock = close_button;

    pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
    pstr->style |= (TTF_STYLE_BOLD | SF_CENTER);

    /* Background template for entries in scroll list */
    background_tmpl = create_surf(adj_size(135), adj_size(40), SDL_SWSURFACE);
    SDL_FillRect(background_tmpl, NULL, map_rgba(background_tmpl->format, bg_color));

    create_frame(background_tmpl,
                 0, 0, background_tmpl->w - 1, background_tmpl->h - 1,
                 get_theme_color(COLOR_THEME_HELPDLG_FRAME));

    impr_type_count = 0;
    improvement_iterate(pimprove) {

      /* Copy background surface */
      background = copy_surface(background_tmpl);

      /* Blit improvement name */
      copy_chars_to_utf8_str(pstr, improvement_name_translation(pimprove));
      impr_name = create_text_surf_smaller_than_w(pstr, adj_size(100 - 4));
      dst.x = adj_size(40) + (background->w - impr_name->w - adj_size(40)) / 2;
      dst.y = (background->h - impr_name->h) / 2;
      alphablit(impr_name, NULL, background, &dst, 255);
      FREESURFACE(impr_name);

      /* Blit improvement icon */
      icon = resize_surface_box(get_building_surface(pimprove),
                                adj_size(36), adj_size(36), 1, TRUE, TRUE);
      dst.x = adj_size(5);
      dst.y = (background->h - icon->h) / 2;
      alphablit(icon, NULL, background, &dst, 255);
      FREESURFACE(icon);

      improvement_button = create_icon2(background, pwindow->dst,
                                        WF_FREE_THEME | WF_RESTORE_BACKGROUND);

      set_wstate(improvement_button, FC_WS_NORMAL);
      improvement_button->action = change_impr_callback;
      add_to_gui_list(MAX_ID - improvement_number(pimprove), improvement_button);

      if (impr_type_count++ >= 10) {
        set_wflag(improvement_button, WF_HIDDEN);
      }

    } improvement_iterate_end;

    FREESURFACE(background_tmpl);

    help_dlg->end_active_widget_list = dock->prev;
    help_dlg->begin_widget_list = improvement_button ? improvement_button : close_button;
    help_dlg->begin_active_widget_list = help_dlg->begin_widget_list;

    if (impr_type_count > 10) {
      help_dlg->active_widget_list = help_dlg->end_active_widget_list;
      scrollbar_width = create_vertical_scrollbar(help_dlg, 1, 10, TRUE, TRUE);
    }

    /* Toggle techs list button */
    list_toggle_button = create_themeicon_button_from_chars_fonto(
                                                            current_theme->up_icon,
                                                            pwindow->dst,
                                                            _("Improvements"),
                                                            FONTO_DEFAULT, 0);
#if 0
   list_toggle_button->action = toggle_full_tree_mode_in_help_dlg_callback;
   if (store->show_tree) {
     set_wstate(list_toggle_button, FC_WS_NORMAL);
   }
#endif

    widget_resize(list_toggle_button, adj_size(160), adj_size(15));
    list_toggle_button->string_utf8->fgcol = *get_theme_color(COLOR_THEME_HELPDLG_TEXT);

    add_to_gui_list(ID_BUTTON, list_toggle_button);

    dock = list_toggle_button;
    store->dock = dock;
  } else {
    created = FALSE;
    scrollbar_width = (help_dlg->scroll ? help_dlg->scroll->up_left_button->size.w : 0);
    pwindow = help_dlg->end_widget_list;
    store = (struct units_buttons *)pwindow->data.ptr;
    dock = store->dock;

    area = pwindow->area;

    /* Delete any previous list entries */
    if (dock != help_dlg->begin_widget_list) {
      del_group_of_widgets_from_gui_list(help_dlg->begin_widget_list,
                                         dock->prev);
      help_dlg->begin_widget_list = dock;
    }
  }

  pimpr_type = improvement_by_number(impr);

  surf = get_building_surface(pimpr_type);
  impr_name_label
    = create_iconlabel_from_chars_fonto(resize_surface_box(surf, adj_size(64),
                                                           adj_size(48), 1, TRUE, TRUE),
                                        pwindow->dst,
                                        city_improvement_name_translation(NULL, pimpr_type),
                                        FONTO_MAX, WF_FREE_THEME);

  impr_name_label->id = ID_LABEL;
  widget_add_as_prev(impr_name_label, dock);
  dock = impr_name_label;

  if (!is_convert_improvement(pimpr_type)) {
    sprintf(buffer, "%s %d", _("Base Cost:"),
            impr_base_build_shield_cost(pimpr_type));
    cost_label = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                   buffer,
                                                   FONTO_ATTENTION, 0);
    cost_label->id = ID_LABEL;
    widget_add_as_prev(cost_label, dock);
    dock = cost_label;

    if (!is_wonder(pimpr_type)) {
      sprintf(buffer, "%s %d", _("Upkeep:"), pimpr_type->upkeep);
      upkeep_label = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                       buffer,
                                                       FONTO_ATTENTION, 0);
      upkeep_label->id = ID_LABEL;
      widget_add_as_prev(upkeep_label, dock);
      dock = upkeep_label;
    }
  }

  /* Requirement */
  requirement_label = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                        _("Requirement:"),
                                                        FONTO_ATTENTION, 0);
  requirement_label->id = ID_LABEL;
  widget_add_as_prev(requirement_label, dock);
  dock = requirement_label;

  /* FIXME: this should show ranges, negated reqs, and all the reqs.
   * Currently it's limited to 1 req. */
  requirement_vector_iterate(&pimpr_type->reqs, preq) {
    if (!preq->present) {
      continue;
    }
    requirement_label2
      = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                             universal_name_translation(&preq->source, buffer, sizeof(buffer)),
                                          FONTO_ATTENTION,
                                          WF_RESTORE_BACKGROUND);
    if (preq->source.kind != VUT_ADVANCE) {
      break; /* FIXME */
    }
    requirement_label2->id = MAX_ID - advance_number(preq->source.value.advance);
    requirement_label2->string_utf8->fgcol
      = *get_tech_color(advance_number(preq->source.value.advance));
    requirement_label2->action = change_tech_callback;
    set_wstate(requirement_label2, FC_WS_NORMAL);
    break;
  } requirement_vector_iterate_end;

  if (requirement_label2 == NULL) {
    requirement_label2 = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                           Q_("?req:None"),
                                                           FONTO_ATTENTION, 0);
    requirement_label2->id = ID_LABEL;
  }

  widget_add_as_prev(requirement_label2, dock);
  dock = requirement_label2;
  store->requirement_button = requirement_label2;

  /* Obsolete by */
  obsolete_by_label = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                        _("Obsolete by:"),
                                                        FONTO_ATTENTION, 0);
  obsolete_by_label->id = ID_LABEL;
  widget_add_as_prev(obsolete_by_label, dock);
  dock = obsolete_by_label;

  requirement_vector_iterate(&pimpr_type->obsolete_by, pobs) {
    if (pobs->source.kind == VUT_ADVANCE) {
      obsTech = pobs->source.value.advance;
      break;
    }
  } requirement_vector_iterate_end;

  if (obsTech == NULL) {
    obsolete_by_label2 = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                           _("Never"),
                                                           FONTO_ATTENTION, 0);
    obsolete_by_label2->id = ID_LABEL;
  } else {
    obsolete_by_label2
      = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                          advance_name_translation(obsTech),
                                          FONTO_ATTENTION,
                                          WF_RESTORE_BACKGROUND);
    obsolete_by_label2->id = MAX_ID - advance_number(obsTech);
    obsolete_by_label2->string_utf8->fgcol
      = *get_tech_color(advance_number(obsTech));
    obsolete_by_label2->action = change_tech_callback;
    set_wstate(obsolete_by_label2, FC_WS_NORMAL);
  }
  widget_add_as_prev(obsolete_by_label2, dock);
  dock = obsolete_by_label2;
  store->obsolete_by_button = obsolete_by_label2;

  /* Helptext */
  start_x = (area.x + 1 + scrollbar_width + help_dlg->end_active_widget_list->size.w + adj_size(20));

  buffer[0] = '\0';
  helptext_building(buffer, sizeof(buffer), client.conn.playing, NULL,
                    pimpr_type);
  if (buffer[0] != '\0') {
    utf8_str *bstr = create_utf8_from_char_fonto(buffer, FONTO_ATTENTION);

    convert_utf8_str_to_const_surface_width(bstr, adj_size(640) - start_x - adj_size(20));
    help_text_label = create_iconlabel(NULL, pwindow->dst, bstr, 0);
    help_text_label->id = ID_LABEL;
    widget_add_as_prev(help_text_label, dock);
    text = TRUE;
  }

  help_dlg->begin_widget_list = help_text_label ? help_text_label : obsolete_by_label2;

  /* --------------------------------------------------------- */
  if (created) {
    surf = theme_get_background(active_theme, BACKGROUND_HELPDLG);
    if (resize_window(pwindow, surf, NULL, adj_size(640), adj_size(480))) {
      FREESURFACE(surf);
    }

    area = pwindow->area;

    widget_set_position(pwindow,
                        (main_window_width() - pwindow->size.w) / 2,
                        (main_window_height() - pwindow->size.h) / 2);

    /* Exit button */
    close_button = pwindow->prev;
    widget_set_position(close_button,
                        area.x + area.w - close_button->size.w - 1,
                        pwindow->size.y + adj_size(2));

    /* List toggle button */
    list_toggle_button = store->dock;
    widget_set_position(list_toggle_button, area.x, area.y);

    /* List entries */
    h = setup_vertical_widgets_position(1, area.x + scrollbar_width,
                                        area.y + list_toggle_button->size.h, 0, 0,
                                        help_dlg->begin_active_widget_list,
                                        help_dlg->end_active_widget_list);

    /* Scrollbar */
    if (help_dlg->scroll) {
      setup_vertical_scrollbar_area(help_dlg->scroll,
                                    area.x, area.y + list_toggle_button->size.h,
                                    h, FALSE);
    }
  }

  impr_name_label = store->dock->prev;
  widget_set_position(impr_name_label, start_x, area.y + adj_size(16));

  start_y = impr_name_label->size.y + impr_name_label->size.h + adj_size(10);

  if (!is_convert_improvement(pimpr_type)) {
    cost_label = impr_name_label->prev;
    widget_set_position(cost_label, start_x, start_y);
    if (!is_wonder(pimpr_type)) {
      upkeep_label = cost_label->prev;
      widget_set_position(upkeep_label,
                          cost_label->size.x + cost_label->size.w + adj_size(20),
                          start_y);
    }
    start_y += cost_label->size.h;
  }

  requirement_label = store->requirement_button->next;
  widget_set_position(requirement_label, start_x, start_y);

  requirement_label2 = store->requirement_button;
  widget_set_position(requirement_label2,
                      requirement_label->size.x + requirement_label->size.w + adj_size(5),
                      start_y);

  if (store->obsolete_by_button) {
    obsolete_by_label = store->obsolete_by_button->next;
    widget_set_position(obsolete_by_label,
                        requirement_label2->size.x + requirement_label2->size.w + adj_size(10),
                        start_y);

    obsolete_by_label2 = store->obsolete_by_button;
    widget_set_position(obsolete_by_label2,
                        obsolete_by_label->size.x + obsolete_by_label->size.w + adj_size(5),
                        start_y);

    start_y += obsolete_by_label2->size.h;
  }

  start_y += adj_size(30);

  if (text) {
    widget_set_position(help_text_label, start_x, start_y);
  }

  redraw_impr_info_dlg();
}

/**********************************************************************//**
  User requested new unit help
**************************************************************************/
static int change_unit_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_unit_info(MAX_ID - pwidget->id);
  }

  return -1;
}

/**********************************************************************//**
  Refresh unit help dialog
**************************************************************************/
static void redraw_unit_info_dlg(void)
{
  SDL_Color bg_color = {255, 255, 255, 64};
  struct widget *pwindow = help_dlg->end_widget_list;
  struct units_buttons *store = (struct units_buttons *)pwindow->data.ptr;
  SDL_Rect dst;

  redraw_group(pwindow->prev, pwindow, FALSE);

  dst.x = store->dock->prev->size.x - adj_size(10);
  dst.y = store->dock->prev->size.y - adj_size(10);
  dst.w = pwindow->size.w - (dst.x - pwindow->size.x) - adj_size(10);
  dst.h = pwindow->size.h - (dst.y - pwindow->size.y) - adj_size(10);

  fill_rect_alpha(pwindow->dst->surface, &dst, &bg_color);

  create_frame(pwindow->dst->surface,
               dst.x, dst.y, dst.w, dst.h,
               get_theme_color(COLOR_THEME_HELPDLG_FRAME));

  /*------------------------------------- */
  redraw_group(help_dlg->begin_widget_list, pwindow->prev->prev, FALSE);
  widget_flush(pwindow);
}

/**********************************************************************//**
  Show unit type info
**************************************************************************/
void popup_unit_info(Unit_type_id type_id)
{
  SDL_Color bg_color = {255, 255, 255, 128};
  struct widget *pwindow;
  struct units_buttons *store;
  struct widget *close_button = NULL;
  struct widget *list_toggle_button = NULL;
  struct widget *unit_button = NULL;
  struct widget *unit_name_label = NULL;
  struct widget *unit_info_label = NULL;
  struct widget *requirement_label = NULL;
  struct widget *requirement_label2 = NULL;
  struct widget *obsolete_by_label = NULL;
  struct widget *obsolete_by_label2 = NULL;
  struct widget *help_text_label = NULL;
  struct widget *dock;
  utf8_str *title;
  utf8_str *pstr;
  SDL_Surface *surf;
  int h, start_x, start_y, utype_count;
  bool created, text = FALSE;
  int scrollbar_width = 0;
  struct unit_type *punittype;
  char buffer[bufsz];
  SDL_Rect area;
  struct advance *req;

  if (current_help_dlg != HELP_UNIT) {
    popdown_help_dialog();
  }

  /* Create new dialog if it doesn't exist yet */
  if (!help_dlg) {
    SDL_Surface *background_tmpl, *background, *unit_name, *icon;
    SDL_Rect dst;

    current_help_dlg = HELP_UNIT;
    created = TRUE;

    /* Create dialog */
    help_dlg = fc_calloc(1, sizeof(struct advanced_dialog));
    store = fc_calloc(1, sizeof(struct units_buttons));

    /* Create window */
    title = create_utf8_from_char_fonto(_("Help : Units"), FONTO_ATTENTION);
    title->style |= TTF_STYLE_BOLD;

    pwindow = create_window_skeleton(NULL, title, WF_FREE_DATA);
    pwindow->action = help_dlg_window_callback;
    set_wstate(pwindow , FC_WS_NORMAL);
    pwindow->data.ptr = (void *)store;
    add_to_gui_list(ID_WINDOW, pwindow);

    help_dlg->end_widget_list = pwindow;

    area = pwindow->area;

    /* ------------------ */

    /* Close button */
    close_button = create_themeicon(current_theme->small_cancel_icon,
                                    pwindow->dst,
                                    WF_WIDGET_HAS_INFO_LABEL
                                    | WF_RESTORE_BACKGROUND);
    close_button->info_label
      = create_utf8_from_char_fonto(_("Close Dialog (Esc)"), FONTO_ATTENTION);
    close_button->action = exit_help_dlg_callback;
    set_wstate(close_button, FC_WS_NORMAL);
    close_button->key = SDLK_ESCAPE;

    add_to_gui_list(ID_BUTTON, close_button);

    /* ------------------ */
    dock = close_button;

    /* --- Create scrollable unit list on the left side ---*/

    pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
    pstr->style |= (TTF_STYLE_BOLD | SF_CENTER);

    /* Background template for entries in scroll list */
    background_tmpl = create_surf(adj_size(135), adj_size(40), SDL_SWSURFACE);
    SDL_FillRect(background_tmpl, NULL, map_rgba(background_tmpl->format, bg_color));

    create_frame(background_tmpl,
                 0, 0, background_tmpl->w - 1, background_tmpl->h - 1,
                 get_theme_color(COLOR_THEME_HELPDLG_FRAME));

    utype_count = 0;
    unit_type_iterate(ut) {

      /* Copy background surface */
      background = copy_surface(background_tmpl);

      /* Blit unit name */
      copy_chars_to_utf8_str(pstr, utype_name_translation(ut));
      unit_name = create_text_surf_smaller_than_w(pstr, adj_size(100 - 4));
      dst.x = adj_size(35) + (background->w - unit_name->w - adj_size(35)) / 2;
      dst.y = (background->h - unit_name->h) / 2;
      alphablit(unit_name, NULL, background, &dst, 255);
      FREESURFACE(unit_name);

      /* Blit unit icon */
      icon = resize_surface_box(get_unittype_surface(ut, direction8_invalid()),
                                adj_size(36), adj_size(36), 1, TRUE, TRUE);
      dst.x = (adj_size(35) - icon->w) / 2;
      dst.y = (background->h - icon->h) / 2;
      alphablit(icon, NULL, background, &dst, 255);
      FREESURFACE(icon);

      unit_button = create_icon2(background, pwindow->dst,
                                 WF_FREE_THEME | WF_RESTORE_BACKGROUND);

      set_wstate(unit_button, FC_WS_NORMAL);
      unit_button->action = change_unit_callback;
      add_to_gui_list(MAX_ID - utype_number(ut), unit_button);

      if (utype_count++ >= 10) {
        set_wflag(unit_button, WF_HIDDEN);
      }

    } unit_type_iterate_end;

    FREESURFACE(background_tmpl);

    help_dlg->end_active_widget_list = dock->prev;
    help_dlg->begin_widget_list = unit_button ? unit_button : close_button;
    help_dlg->begin_active_widget_list = help_dlg->begin_widget_list;

    if (utype_count > 10) {
      help_dlg->active_widget_list = help_dlg->end_active_widget_list;
      scrollbar_width = create_vertical_scrollbar(help_dlg, 1, 10, TRUE, TRUE);
    }

    /* Toggle techs list button */
    list_toggle_button = create_themeicon_button_from_chars_fonto(
                                                            current_theme->up_icon,
                                                            pwindow->dst,
                                                            _("Units"),
                                                            FONTO_DEFAULT, 0);
#if 0
    list_toggle_button->action = toggle_full_tree_mode_in_help_dlg_callback;
    if (store->show_tree) {
      set_wstate(list_toggle_button, FC_WS_NORMAL);
    }
#endif

    widget_resize(list_toggle_button, adj_size(160), adj_size(15));
    list_toggle_button->string_utf8->fgcol = *get_theme_color(COLOR_THEME_HELPDLG_TEXT);

    add_to_gui_list(ID_BUTTON, list_toggle_button);

    dock = list_toggle_button;
    store->dock = dock;
  } else {
    created = FALSE;
    scrollbar_width = (help_dlg->scroll ? help_dlg->scroll->up_left_button->size.w : 0);
    pwindow = help_dlg->end_widget_list;
    store = (struct units_buttons *)pwindow->data.ptr;
    dock = store->dock;

    area = pwindow->area;

    /* Delete any previous list entries */
    if (dock != help_dlg->begin_widget_list) {
      del_group_of_widgets_from_gui_list(help_dlg->begin_widget_list,
                                         dock->prev);
      help_dlg->begin_widget_list = dock;
    }
  }

  punittype = utype_by_number(type_id);
  unit_name_label
    = create_iconlabel_from_chars_fonto(adj_surf(get_unittype_surface(punittype,
                                                                      direction8_invalid())),
                                        pwindow->dst, utype_name_translation(punittype),
                                        FONTO_MAX, WF_FREE_THEME);

  unit_name_label->id = ID_LABEL;
  widget_add_as_prev(unit_name_label, dock);
  dock = unit_name_label;

  {
    char buf[2048];

    fc_snprintf(buf, sizeof(buf), "%s %d %s",
                _("Cost:"), utype_build_shield_cost_base(punittype),
                PL_("shield", "shields", utype_build_shield_cost_base(punittype)));

    if (punittype->pop_cost) {
      cat_snprintf(buf, sizeof(buf), " %d %s",
                   punittype->pop_cost, PL_("citizen", "citizens",
                                            punittype->pop_cost));
    }

    cat_snprintf(buf, sizeof(buf), "      %s",  _("Upkeep:"));

    if (punittype->upkeep[O_SHIELD]) {
      cat_snprintf(buf, sizeof(buf), " %d %s",
                   punittype->upkeep[O_SHIELD], PL_("shield", "shields",
                                                    punittype->upkeep[O_SHIELD]));
    }
    if (punittype->upkeep[O_FOOD]) {
      cat_snprintf(buf, sizeof(buf), " %d %s",
                   punittype->upkeep[O_FOOD], PL_("food", "foods",
                                                  punittype->upkeep[O_FOOD]));
    }
    if (punittype->upkeep[O_GOLD]) {
      cat_snprintf(buf, sizeof(buf), " %d %s",
                   punittype->upkeep[O_GOLD], PL_("gold", "golds",
                                                  punittype->upkeep[O_GOLD]));
    }
    if (punittype->happy_cost) {
      cat_snprintf(buf, sizeof(buf), " %d %s",
                   punittype->happy_cost, PL_("citizen", "citizens",
                                              punittype->happy_cost));
    }

    cat_snprintf(buf, sizeof(buf), "\n%s %d %s %d %s %s\n%s %d %s %d %s %d",
              _("Attack:"), punittype->attack_strength,
              _("Defense:"), punittype->defense_strength,
              _("Move:"), move_points_text(punittype->move_rate, TRUE),
              _("Vision:"), punittype->vision_radius_sq,
              _("Firepower:"), punittype->firepower,
              _("Hitpoints:"), punittype->hp);

    unit_info_label = create_iconlabel_from_chars_fonto(NULL, pwindow->dst, buf,
                                                        FONTO_ATTENTION, 0);
    unit_info_label->id = ID_LABEL;
    widget_add_as_prev(unit_info_label, dock);
    dock = unit_info_label;
  }

  /* Requirement */
  requirement_label = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                        _("Requirement:"),
                                                        FONTO_ATTENTION, 0);
  requirement_label->id = ID_LABEL;
  widget_add_as_prev(requirement_label, dock);
  dock = requirement_label;

  req = utype_primary_tech_req(punittype);

  if (advance_number(req) == A_NONE) {
    requirement_label2 = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                           Q_("?tech:None"),
                                                           FONTO_ATTENTION, 0);
    requirement_label2->id = ID_LABEL;
  } else {
    Tech_type_id req_id = advance_number(req);

    requirement_label2
      = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                          advance_name_translation(req),
                                          FONTO_ATTENTION,
                                          WF_RESTORE_BACKGROUND);
    requirement_label2->id = MAX_ID - req_id;
    requirement_label2->string_utf8->fgcol = *get_tech_color(req_id);
    requirement_label2->action = change_tech_callback;
    set_wstate(requirement_label2, FC_WS_NORMAL);
  }
  widget_add_as_prev(requirement_label2, dock);
  dock = requirement_label2;
  store->requirement_button = requirement_label2;

  /* Obsolete by */
  obsolete_by_label = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                        _("Obsolete by:"),
                                                        FONTO_ATTENTION, 0);
  obsolete_by_label->id = ID_LABEL;
  widget_add_as_prev(obsolete_by_label, dock);
  dock = obsolete_by_label;

  if (punittype->obsoleted_by == U_NOT_OBSOLETED) {
    obsolete_by_label2 = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                                           Q_("?utype:None"),
                                                           FONTO_ATTENTION, 0);
    obsolete_by_label2->id = ID_LABEL;
  } else {
    const struct unit_type *utype = punittype->obsoleted_by;
    struct advance *obs_req = utype_primary_tech_req(utype);

    obsolete_by_label2
      = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                          utype_name_translation(utype),
                                          FONTO_ATTENTION,
                                          WF_RESTORE_BACKGROUND);
    obsolete_by_label2->string_utf8->fgcol
      = *get_tech_color(advance_number(obs_req));
    obsolete_by_label2->id = MAX_ID - utype_number(utype);
    obsolete_by_label2->action = change_unit_callback;
    set_wstate(obsolete_by_label2, FC_WS_NORMAL);
  }
  widget_add_as_prev(obsolete_by_label2, dock);
  dock = obsolete_by_label2;
  store->obsolete_by_button = obsolete_by_label2;

  /* Helptext */
  start_x = (area.x + 1 + scrollbar_width + help_dlg->active_widget_list->size.w + adj_size(20));

  buffer[0] = '\0';
  helptext_unit(buffer, sizeof(buffer), client.conn.playing, "", utype_by_number(type_id),
                TRUE);
  if (buffer[0] != '\0') {
    utf8_str *ustr = create_utf8_from_char_fonto(buffer, FONTO_ATTENTION);

    convert_utf8_str_to_const_surface_width(ustr, adj_size(640) - start_x - adj_size(20));
    help_text_label = create_iconlabel(NULL, pwindow->dst, ustr, 0);
    help_text_label->id = ID_LABEL;
    widget_add_as_prev(help_text_label, dock);
    text = TRUE;
  }

  help_dlg->begin_widget_list = help_text_label ? help_text_label : obsolete_by_label2;

  /* --------------------------------------------------------- */
  if (created) {

    surf = theme_get_background(active_theme, BACKGROUND_HELPDLG);
    if (resize_window(pwindow, surf, NULL, adj_size(640), adj_size(480))) {
      FREESURFACE(surf);
    }

    area = pwindow->area;

    widget_set_position(pwindow,
                        (main_window_width() - pwindow->size.w) / 2,
                        (main_window_height() - pwindow->size.h) / 2);

    /* Exit button */
    close_button = pwindow->prev;
    widget_set_position(close_button,
                        area.x + area.w - close_button->size.w - 1,
                        pwindow->size.y + adj_size(2));

    /* List toggle button */
    list_toggle_button = store->dock;
    widget_set_position(list_toggle_button, area.x, area.y);

    /* List entries */
    h = setup_vertical_widgets_position(1, area.x + scrollbar_width,
                                           area.y + list_toggle_button->size.h, 0, 0,
                                           help_dlg->begin_active_widget_list,
                                           help_dlg->end_active_widget_list);

    /* Scrollbar */
    if (help_dlg->scroll) {
      setup_vertical_scrollbar_area(help_dlg->scroll,
                                    area.x, area.y + list_toggle_button->size.h,
                                    h, FALSE);
    }
  }

  unit_name_label = store->dock->prev;
  widget_set_position(unit_name_label, start_x, area.y + adj_size(16));

  start_y = unit_name_label->size.y + unit_name_label->size.h + adj_size(10);

  unit_info_label = unit_name_label->prev;
  widget_set_position(unit_info_label, start_x, start_y);

  start_y += unit_info_label->size.h;

  requirement_label = store->requirement_button->next;
  widget_set_position(requirement_label, start_x, start_y);

  requirement_label2 = store->requirement_button;
  widget_set_position(requirement_label2,
                      requirement_label->size.x + requirement_label->size.w + adj_size(5),
                      start_y);

  obsolete_by_label = store->obsolete_by_button->next;
  widget_set_position(obsolete_by_label,
                      requirement_label2->size.x + requirement_label2->size.w + adj_size(10),
                      start_y);

  obsolete_by_label2 = store->obsolete_by_button;
  widget_set_position(obsolete_by_label2,
                      obsolete_by_label->size.x + obsolete_by_label->size.w + adj_size(5),
                      start_y);

  start_y += obsolete_by_label2->size.h + adj_size(20);

  if (text) {
    help_text_label = store->obsolete_by_button->prev;
    widget_set_position(help_text_label, start_x, start_y);
  }

  redraw_unit_info_dlg();
}

/* =============================================== */
/* ==================== Tech Tree ================ */
/* =============================================== */

/**********************************************************************//**
  User requested new tech help
**************************************************************************/
static int change_tech_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_tech_info(MAX_ID - pwidget->id);
  }

  return -1;
}

/**********************************************************************//**
  User requested new tech tree
**************************************************************************/
static int show_tech_tree_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct techs_buttons *store = (struct techs_buttons *)help_dlg->end_widget_list->data.ptr;

    store->show_tree = !store->show_tree;
    if (!store->show_tree) {
      store->show_full_tree = FALSE;
      store->dock->theme2 = current_theme->up_icon;
    }
    popup_tech_info(MAX_ID - store->dock->prev->id);
  }

  return -1;
}

/**********************************************************************//**
  Refresh tech help dialog
**************************************************************************/
static void redraw_tech_info_dlg(void)
{
  SDL_Color bg_color = {255, 255, 255, 64};
  struct widget *pwindow = help_dlg->end_widget_list;
  struct techs_buttons *store = (struct techs_buttons *)pwindow->data.ptr;
  SDL_Surface *text0, *text1 = NULL;
  utf8_str *pstr;
  SDL_Rect dst;

  redraw_group(pwindow->prev, pwindow, FALSE);

  dst.x = store->dock->prev->prev->size.x - adj_size(10);
  dst.y = store->dock->prev->prev->size.y - adj_size(10);
  dst.w = pwindow->size.w - (dst.x - pwindow->size.x) - adj_size(10);
  dst.h = pwindow->size.h - (dst.y - pwindow->size.y) - adj_size(10);

  fill_rect_alpha(pwindow->dst->surface, &dst, &bg_color);

  create_frame(pwindow->dst->surface,
               dst.x, dst.y, dst.w, dst.h,
               get_theme_color(COLOR_THEME_HELPDLG_FRAME));

  /* -------------------------- */
  pstr = create_utf8_from_char_fonto(_("Allows"), FONTO_HEADING);
  pstr->style |= TTF_STYLE_BOLD;

  text0 = create_text_surf_from_utf8(pstr);
  dst.x = store->dock->prev->prev->size.x;
  if (store->targets[0]) {
    dst.y = store->targets[0]->size.y - text0->h;
  } else {
    dst.y = store->dock->prev->prev->size.y
              + store->dock->prev->prev->size.h + adj_size(10);
  }

  alphablit(text0, NULL, pwindow->dst->surface, &dst, 255);
  FREESURFACE(text0);

  if (store->sub_targets[0]) {
    int i;

    change_fonto_utf8(pstr, FONTO_ATTENTION);

    copy_chars_to_utf8_str(pstr, _("( with "));
    text0 = create_text_surf_from_utf8(pstr);

    copy_chars_to_utf8_str(pstr, _(" )"));
    text1 = create_text_surf_from_utf8(pstr);
    i = 0;
    while (i < 6 && store->sub_targets[i]) {
      dst.x = store->sub_targets[i]->size.x - text0->w;
      dst.y = store->sub_targets[i]->size.y;

      alphablit(text0, NULL, pwindow->dst->surface, &dst, 255);
      dst.x = store->sub_targets[i]->size.x + store->sub_targets[i]->size.w;
      dst.y = store->sub_targets[i]->size.y;

      alphablit(text1, NULL, pwindow->dst->surface, &dst, 255);
      i++;
    }

    FREESURFACE(text0);
    FREESURFACE(text1);
  }
  FREEUTF8STR(pstr);

  redraw_group(help_dlg->begin_widget_list, pwindow->prev->prev, FALSE);
  widget_flush(pwindow);
}

/**********************************************************************//**
  Create tech info widgets
**************************************************************************/
static struct widget *create_tech_info(Tech_type_id tech, int width,
                                       struct widget *pwindow,
                                       struct techs_buttons *store)
{
  struct widget *pwidget;
  struct widget *last, *budynki;
  struct widget *dock = store->dock;
  int i, targets_count, sub_targets_count, max_width = 0;
  int start_x, start_y, imp_count, unit_count, flags_count, gov_count;
  char buffer[bufsz];
  SDL_Surface *surf;

  start_x = (pwindow->area.x + adj_size(1) + width + help_dlg->active_widget_list->size.w + adj_size(20));

  /* Tech tree icon */
  pwidget = create_icon2(current_theme->tech_tree_icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND);

  set_wstate(pwidget, FC_WS_NORMAL);
  pwidget->action = show_tech_tree_callback;
  pwidget->id = MAX_ID - tech;
  widget_add_as_prev(pwidget, dock);
  dock = pwidget;

  /* Tech name (heading) */
  pwidget
    = create_iconlabel_from_chars_fonto(get_tech_icon(tech),
                                        pwindow->dst,
                                        advance_name_translation(advance_by_number(tech)),
                                        FONTO_MAX, WF_FREE_THEME);

  pwidget->id = ID_LABEL;
  widget_add_as_prev(pwidget, dock);
  dock = pwidget;

  /* Target techs */
  targets_count = 0;
  advance_index_iterate(A_FIRST, aidx) {
    if ((targets_count < 6)
        && (advance_required(aidx, AR_ONE) == tech
            || advance_required(aidx, AR_TWO) == tech)) {
      pwidget
        = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                 advance_name_translation(advance_by_number(aidx)),
                                            FONTO_ATTENTION,
                                            WF_RESTORE_BACKGROUND);
      pwidget->string_utf8->fgcol = *get_tech_color(aidx);
      max_width = MAX(max_width, pwidget->size.w);
      set_wstate(pwidget, FC_WS_NORMAL);
      pwidget->action = change_tech_callback;
      pwidget->id = MAX_ID - aidx;
      widget_add_as_prev(pwidget, dock);
      dock = pwidget;
      store->targets[targets_count++] = pwidget;
    }
  } advance_index_iterate_end;
  if (targets_count < 6) {
    store->targets[targets_count] = NULL;
  }

  sub_targets_count = 0;
  if (targets_count > 0) {
    int sub_tech;

    for (i = 0; i < targets_count; i++) {
      sub_tech = MAX_ID - store->targets[i]->id;
      if (advance_required(sub_tech, AR_ONE) == tech
          && advance_required(sub_tech, AR_TWO) != A_NONE) {
        sub_tech = advance_required(sub_tech, AR_TWO);
      } else if (advance_required(sub_tech, AR_TWO) == tech
                 && advance_required(sub_tech, AR_ONE) != A_NONE) {
        sub_tech = advance_required(sub_tech, AR_ONE);
      } else {
        continue;
      }
      pwidget
        = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
              advance_name_translation(advance_by_number(sub_tech)),
                                            FONTO_ATTENTION,
                                            WF_RESTORE_BACKGROUND);
      pwidget->string_utf8->fgcol = *get_tech_color(sub_tech);
      set_wstate(pwidget, FC_WS_NORMAL);
      pwidget->action = change_tech_callback;
      pwidget->id = MAX_ID - sub_tech;
      widget_add_as_prev(pwidget, dock);
      dock = pwidget;
      store->sub_targets[sub_targets_count++] = pwidget;
    }
  }
  if (sub_targets_count < 6) {
    store->sub_targets[sub_targets_count] = NULL;
  }

  /* Fill array with iprvm. icons */
  budynki = pwidget;

  /* Target governments */
  gov_count = 0;
  governments_iterate(gov) {
    requirement_vector_iterate(&(gov->reqs), preq) {
      if (VUT_ADVANCE == preq->source.kind
          && advance_number(preq->source.value.advance) == tech) {

        pwidget
          = create_iconlabel_from_chars_fonto(adj_surf(get_government_surface(gov)),
                pwindow->dst,
                government_name_translation(gov),
                FONTO_HEADING,
                WF_RESTORE_BACKGROUND | WF_SELECT_WITHOUT_BAR | WF_FREE_THEME);
        set_wstate(pwidget, FC_WS_NORMAL);
        pwidget->action = change_gov_callback;
        pwidget->id = MAX_ID - government_index(gov);
        widget_add_as_prev(pwidget, dock);
        dock = pwidget;
        gov_count++;
      }
    } requirement_vector_iterate_end;
  } governments_iterate_end;

  /* Target improvements */
  imp_count = 0;
  improvement_iterate(pimprove) {
    if (valid_improvement(pimprove)) {
      /* FIXME: this should show ranges and all the reqs.
       * Currently it's limited to 1 req. */
      requirement_vector_iterate(&(pimprove->reqs), preq) {
        if (VUT_ADVANCE == preq->source.kind
            && advance_number(preq->source.value.advance) == tech) {
          surf = get_building_surface(pimprove);
          pwidget = create_iconlabel_from_chars_fonto(
                  resize_surface_box(surf, adj_size(48), adj_size(48), 1, TRUE,
                                     TRUE),
                  pwindow->dst,
                  improvement_name_translation(pimprove),
                  FONTO_HEADING,
                  WF_RESTORE_BACKGROUND | WF_SELECT_WITHOUT_BAR);
          set_wstate(pwidget, FC_WS_NORMAL);
          if (is_wonder(pimprove)) {
            pwidget->string_utf8->fgcol = *get_theme_color(COLOR_THEME_CITYDLG_LUX);
          }
          pwidget->action = change_impr_callback;
          pwidget->id = MAX_ID - improvement_number(pimprove);
          widget_add_as_prev(pwidget, dock);
          dock = pwidget;
          imp_count++;
        }

        break;
      } requirement_vector_iterate_end;
    }
  } improvement_iterate_end;

  unit_count = 0;
  unit_type_iterate(un) {
    if (is_tech_req_for_utype(un, advance_by_number(tech))) {
      pwidget = create_iconlabel_from_chars_fonto(
                                   resize_surface_box(get_unittype_surface(un, direction8_invalid()),
                                   adj_size(48), adj_size(48), 1, TRUE, TRUE),
                  pwindow->dst, utype_name_translation(un), FONTO_HEADING,
                  (WF_FREE_THEME | WF_RESTORE_BACKGROUND | WF_SELECT_WITHOUT_BAR));
      set_wstate(pwidget, FC_WS_NORMAL);
      pwidget->action = change_unit_callback;
      pwidget->id = MAX_ID - utype_number(un);
      widget_add_as_prev(pwidget, dock);
      dock = pwidget;
      unit_count++;
    }
  } unit_type_iterate_end;

  buffer[0] = '\0';
  if (tech != A_NONE) {
    helptext_advance(buffer, sizeof(buffer), client.conn.playing, "", tech);
  }
  if (buffer[0] != '\0') {
    utf8_str *pstr = create_utf8_from_char_fonto(buffer, FONTO_ATTENTION);

    convert_utf8_str_to_const_surface_width(pstr, adj_size(640) - start_x - adj_size(20));
    pwidget = create_iconlabel(NULL, pwindow->dst, pstr, 0);
    pwidget->id = ID_LABEL;
    widget_add_as_prev(pwidget, dock);
    flags_count = 1;
  } else {
    flags_count = 0;
  }

  last = pwidget;
  /* --------------------------------------------- */

  /* Tree button */
  pwidget = store->dock->prev;
  pwidget->size.x = pwindow->area.x + pwindow->area.w - pwidget->size.w - adj_size(17);
  pwidget->size.y = pwindow->area.y + adj_size(16);

  /* Tech label */
  pwidget = pwidget->prev;
  pwidget->size.x = start_x;
  pwidget->size.y = pwindow->area.y + adj_size(16);
  start_y = pwidget->size.y + pwidget->size.h + adj_size(30);

  if (targets_count) {
    int j = 0;

    i = 0;

    while (i < 6 && store->targets[i]) {
      store->targets[i]->size.x = pwindow->size.x + start_x;
      store->targets[i]->size.y = start_y;

      if (store->sub_targets[j]) {
        int t0 = MAX_ID - store->targets[i]->id;
        int t1 = MAX_ID - store->sub_targets[j]->id;

        if (advance_required(t0, AR_ONE) == t1
            || advance_required(t0, AR_TWO) == t1) {
          store->sub_targets[j]->size.x = pwindow->size.x + start_x + max_width + 60;
          store->sub_targets[j]->size.y = store->targets[i]->size.y;
          j++;
        }
      }

      start_y += store->targets[i]->size.h;
      i++;
    }

    start_y += adj_size(10);
  }
  pwidget = NULL;

  if (gov_count) {
    pwidget = budynki->prev;
    while (gov_count-- && pwidget) {
      pwidget->size.x = pwindow->size.x + start_x;
      pwidget->size.y = start_y;
      start_y += pwidget->size.h + adj_size(2);
      pwidget = pwidget->prev;
    }
  }

  if (imp_count) {
    if (!pwidget) {
      pwidget = budynki->prev;
    }
    while (imp_count-- && pwidget) {
      pwidget->size.x = pwindow->size.x + start_x;
      pwidget->size.y = start_y;
      start_y += pwidget->size.h + adj_size(2);
      pwidget = pwidget->prev;
    }
  }

  if (unit_count) {
    if (!pwidget) {
      pwidget = budynki->prev;
    }
    while (unit_count-- && pwidget) {
      pwidget->size.x = pwindow->size.x + start_x;
      pwidget->size.y = start_y;
      start_y += pwidget->size.h + adj_size(2);
      pwidget = pwidget->prev;
    }
  }

  if (flags_count) {
    if (!pwidget) {
      pwidget = budynki->prev;
    }
    while (flags_count-- && pwidget) {
      pwidget->size.x = pwindow->size.x + start_x;
      pwidget->size.y = start_y;
      start_y += pwidget->size.h + adj_size(2);
      pwidget = pwidget->prev;
    }
  }

  return last;
}

/**********************************************************************//**
  Refresh tech tree dialog
**************************************************************************/
static void redraw_tech_tree_dlg(void)
{
  SDL_Color *line_color = get_theme_color(COLOR_THEME_HELPDLG_LINE);
  SDL_Color bg_color = {255, 255, 255, 64};
  struct widget *pwindow = help_dlg->end_widget_list;
  struct widget *sub0, *sub1;
  struct techs_buttons *store = (struct techs_buttons *)pwindow->data.ptr;
  struct widget *ptech = store->dock->prev;
  int i,j, tech, count;
  int step;
  int mod;
  SDL_Rect dst;

  /* Redraw Window with exit button */
  redraw_group(pwindow->prev, pwindow, FALSE);

  dst.x = pwindow->area.x + pwindow->area.w - adj_size(459) - adj_size(7);
  dst.y = pwindow->area.y + adj_size(6);
  dst.w = pwindow->area.w - (dst.x - pwindow->area.x) - adj_size(10);
  dst.h = pwindow->area.h - (dst.y - pwindow->area.y) - adj_size(10);

  fill_rect_alpha(pwindow->dst->surface, &dst, &bg_color);

  create_frame(pwindow->dst->surface,
               dst.x, dst.y, dst.w, dst.h,
               get_theme_color(COLOR_THEME_HELPDLG_FRAME));

  /* Draw req arrows */
  i = 0;
  while (i < 4 && store->sub_req[i]) {
    i++;
  }
  count = i;

  i = 0;
  while (i < 2 && store->requirement_button[i]) {
    tech = MAX_ID - store->requirement_button[i]->id;

    /* Find sub req's */
    if (i) {
      sub0 = NULL;
      for (j = count - 1; j >= 0; j--) {
        if (MAX_ID - store->sub_req[j]->id == advance_required(tech, AR_ONE)) {
          sub0 = store->sub_req[j];
          break;
        }
      }

      sub1 = NULL;
      for (j = count - 1; j >= 0; j--) {
        if (MAX_ID - store->sub_req[j]->id == advance_required(tech, AR_TWO)) {
          sub1 = store->sub_req[j];
          break;
        }
      }
    } else {
      sub0 = NULL;
      for (j = 0; j < 4 && store->sub_req[j]; j++) {
        if (MAX_ID - store->sub_req[j]->id == advance_required(tech, AR_ONE)) {
          sub0 = store->sub_req[j];
          break;
        }
      }

      sub1 = NULL;
      for (j = 0; j < 4 && store->sub_req[j]; j++) {
        if (MAX_ID - store->sub_req[j]->id == advance_required(tech, AR_TWO)) {
          sub1 = store->sub_req[j];
          break;
        }
      }
    }

    /* Draw main Arrow */
    create_line(store->requirement_button[i]->dst->surface,
           store->requirement_button[i]->size.x + store->requirement_button[i]->size.w,
           store->requirement_button[i]->size.y + store->requirement_button[i]->size.h / 2,
           ptech->size.x,
           store->requirement_button[i]->size.y + store->requirement_button[i]->size.h / 2,
           line_color);

    /* Draw sub req arrows */
    if (sub0 || sub1) {
      create_line(store->requirement_button[i]->dst->surface,
             store->requirement_button[i]->size.x - adj_size(10),
             store->requirement_button[i]->size.y + store->requirement_button[i]->size.h / 2,
             store->requirement_button[i]->size.x ,
             store->requirement_button[i]->size.y + store->requirement_button[i]->size.h / 2,
             line_color);
    }

    if (sub0) {
      create_line(store->requirement_button[i]->dst->surface,
             store->requirement_button[i]->size.x - adj_size(10),
             sub0->size.y + sub0->size.h / 2,
             store->requirement_button[i]->size.x - adj_size(10),
             store->requirement_button[i]->size.y + store->requirement_button[i]->size.h / 2,
             line_color);
      create_line(store->requirement_button[i]->dst->surface,
             sub0->size.x + sub0->size.w,
             sub0->size.y + sub0->size.h / 2,
             store->requirement_button[i]->size.x - adj_size(10),
             sub0->size.y + sub0->size.h / 2,
             line_color);
    }

    if (sub1) {
      create_line(store->requirement_button[i]->dst->surface,
             store->requirement_button[i]->size.x - adj_size(10),
             sub1->size.y + sub1->size.h / 2,
             store->requirement_button[i]->size.x - adj_size(10),
             store->requirement_button[i]->size.y + store->requirement_button[i]->size.h / 2,
             line_color);
      create_line(store->requirement_button[i]->dst->surface,
             sub1->size.x + sub1->size.w,
             sub1->size.y + sub1->size.h / 2,
             store->requirement_button[i]->size.x - adj_size(10),
             sub1->size.y + sub1->size.h / 2,
             line_color);
    }
    i++;
  }

  i = 0;
  while (i < 6 && store->targets[i]) {
    i++;
  }
  count = i;

  if (count > 4) {
    mod = 3;
  } else {
    mod = 2;
  }

  for (i = 0; i < count; i++) {
    tech = MAX_ID - store->targets[i]->id;
    step = ptech->size.h / (count + 1);

    switch ((i % mod)) {
    case 2:
      line_color = get_theme_color(COLOR_THEME_HELPDLG_LINE2);
      break;
    case 1:
      line_color = get_theme_color(COLOR_THEME_HELPDLG_LINE3);
      break;
    default:
      line_color = get_theme_color(COLOR_THEME_HELPDLG_LINE);
      break;
    }

    /* Find sub reqs */
    if (advance_required(tech, AR_ONE) == MAX_ID - ptech->id) {
      sub0 = ptech;
    } else {
      sub0 = NULL;
      for (j = 0; j < 6 && store->sub_targets[j]; j++) {
        if (MAX_ID - store->sub_targets[j]->id == advance_required(tech, AR_ONE)) {
          sub0 = store->sub_targets[j];
          break;
        }
      }
    }

    if (advance_required(tech, AR_TWO) == MAX_ID - ptech->id) {
      sub1 = ptech;
    } else {
      sub1 = NULL;
      for (j = 0; j < 6 && store->sub_targets[j]; j++) {
        if (MAX_ID - store->sub_targets[j]->id == advance_required(tech, AR_TWO)) {
          sub1 = store->sub_targets[j];
          break;
        }
      }
    }

    /* Draw sub targets arrows */
    if (sub0 || sub1) {
      create_line(store->targets[i]->dst->surface,
                  store->targets[i]->size.x - ((i % mod) + 1) * 6,
                  store->targets[i]->size.y + store->targets[i]->size.h / 2,
                  store->targets[i]->size.x ,
                  store->targets[i]->size.y + store->targets[i]->size.h / 2,
                  line_color);
    }

    if (sub0) {
      int y;

      if (sub0 == ptech) {
        y = sub0->size.y + step * (i + 1);
      } else {
        y = sub0->size.y + sub0->size.h / 2;
      }

      create_line(store->targets[i]->dst->surface,
                  store->targets[i]->size.x - ((i % mod) + 1) * 6,
                  y,
                  store->targets[i]->size.x - ((i % mod) + 1) * 6,
                  store->targets[i]->size.y + store->targets[i]->size.h / 2,
                  line_color);
      create_line(store->targets[i]->dst->surface,
                  sub0->size.x + sub0->size.w,
                  y,
                  store->targets[i]->size.x - ((i % mod) + 1) * 6,
                  y,
                  line_color);
    }

    if (sub1) {
      int y;

      if (sub1 == ptech) {
        y = sub1->size.y + step * (i + 1);
      } else {
        y = sub1->size.y + sub1->size.h / 2;
      }

      create_line(store->targets[i]->dst->surface,
                  store->targets[i]->size.x - ((i % mod) + 1) * 6,
                  y,
                  store->targets[i]->size.x - ((i % mod) + 1) * 6,
                  store->targets[i]->size.y + store->targets[i]->size.h / 2,
                  line_color);
      create_line(store->targets[i]->dst->surface,
                  sub1->size.x + sub1->size.w,
                  y,
                  store->targets[i]->size.x - ((i % mod) + 1) * 6,
                  y,
                  line_color);
    }
  }

  /* Redraw rest */
  redraw_group(help_dlg->begin_widget_list, pwindow->prev->prev, FALSE);

  widget_flush(pwindow);
}

/**********************************************************************//**
  User requested toggling between full tech tree and single tech
**************************************************************************/
static int toggle_full_tree_mode_in_help_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct techs_buttons *store = (struct techs_buttons *)help_dlg->end_widget_list->data.ptr;

    if (store->show_full_tree) {
      pwidget->theme2 = current_theme->up_icon;
    } else {
      pwidget->theme2 = current_theme->down_icon;
    }
    store->show_full_tree = !store->show_full_tree;
    popup_tech_info(MAX_ID - store->dock->prev->id);
  }

  return -1;
}

/**********************************************************************//**
  Create tech tree widgets
**************************************************************************/
static struct widget *create_tech_tree(Tech_type_id tech, int width,
                                       struct widget *pwindow,
                                       struct techs_buttons *store)
{
  int i, w, h, req_count, targets_count, sub_req_count, sub_targets_count;
  struct widget *pwidget;
  struct widget *ptech;
  utf8_str *pstr;
  SDL_Surface *surf;
  struct widget *dock = store->dock;

  pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  copy_chars_to_utf8_str(pstr, advance_name_translation(advance_by_number(tech)));
  surf = create_select_tech_icon(pstr, tech, TIM_FULL_MODE);
  pwidget = create_icon2(surf, pwindow->dst,
                         WF_FREE_THEME | WF_RESTORE_BACKGROUND);

  set_wstate(pwidget, FC_WS_NORMAL);
  pwidget->action = show_tech_tree_callback;
  pwidget->id = MAX_ID - tech;
  widget_add_as_prev(pwidget, dock);
  ptech = pwidget;
  dock = pwidget;

  req_count = 0;
  for (i = AR_ONE; i <= AR_TWO; i++) {
    Tech_type_id ar = advance_required(tech, i);
    struct advance *vap = valid_advance_by_number(ar);

    if (NULL != vap && A_NONE != ar) {
      copy_chars_to_utf8_str(pstr, advance_name_translation(vap));
      surf = create_select_tech_icon(pstr, ar, TIM_SMALL_MODE);
      pwidget = create_icon2(surf, pwindow->dst,
                             WF_FREE_THEME | WF_RESTORE_BACKGROUND);
      set_wstate(pwidget, FC_WS_NORMAL);
      pwidget->action = change_tech_callback;
      pwidget->id = MAX_ID - ar;
      widget_add_as_prev(pwidget, dock);
      dock = pwidget;
      store->requirement_button[req_count++] = pwidget;
    }
  }

  sub_req_count = 0;

  if (store->show_full_tree && req_count > 0) {
    int j, sub_tech;

    for (j = 0; j < req_count; j++) {
      sub_tech = MAX_ID - store->requirement_button[j]->id;
      for (i = AR_ONE; i <= AR_TWO; i++) {
        Tech_type_id ar = advance_required(sub_tech, i);
        struct advance *vap = valid_advance_by_number(ar);

        if (NULL != vap && A_NONE != ar) {
          copy_chars_to_utf8_str(pstr, advance_name_translation(vap));
          surf = create_select_tech_icon(pstr, ar, TIM_SMALL_MODE);
          pwidget = create_icon2(surf, pwindow->dst,
                                 WF_FREE_THEME | WF_RESTORE_BACKGROUND);
          set_wstate(pwidget, FC_WS_NORMAL);
          pwidget->action = change_tech_callback;
          pwidget->id = MAX_ID - ar;
          widget_add_as_prev(pwidget, dock);
          dock = pwidget;
          store->sub_req[sub_req_count++] = pwidget;
        }
      }
    }
  }

  if (sub_req_count < 4) {
    store->sub_req[sub_req_count] = NULL;
  }

  targets_count = 0;
  advance_index_iterate(A_FIRST, aidx) {
    if ((targets_count < 6)
        && (advance_required(aidx, AR_ONE) == tech
            || advance_required(aidx, AR_TWO) == tech)) {
      copy_chars_to_utf8_str(pstr, advance_name_translation(advance_by_number(aidx)));
      surf = create_select_tech_icon(pstr, aidx, TIM_SMALL_MODE);
      pwidget = create_icon2(surf, pwindow->dst,
                             WF_FREE_THEME | WF_RESTORE_BACKGROUND);

      set_wstate(pwidget, FC_WS_NORMAL);
      pwidget->action = change_tech_callback;
      pwidget->id = MAX_ID - aidx;
      widget_add_as_prev(pwidget, dock);
      dock = pwidget;
      store->targets[targets_count++] = pwidget;
    }
  } advance_index_iterate_end;
  if (targets_count < 6) {
    store->targets[targets_count] = NULL;
  }

  sub_targets_count = 0;
  if (targets_count > 0) {
    int sub_tech;

    for (i = 0; i < targets_count; i++) {
      sub_tech = MAX_ID - store->targets[i]->id;
      if (advance_required(sub_tech, AR_ONE) == tech
          && advance_required(sub_tech, AR_TWO) != A_NONE) {
        sub_tech = advance_required(sub_tech, AR_TWO);
      } else if (advance_required(sub_tech, AR_TWO) == tech
                 && advance_required(sub_tech, AR_ONE) != A_NONE) {
        sub_tech = advance_required(sub_tech, AR_ONE);
      } else {
        continue;
      }

      copy_chars_to_utf8_str(pstr, advance_name_translation(advance_by_number(sub_tech)));
      surf = create_select_tech_icon(pstr, sub_tech, TIM_SMALL_MODE);
      pwidget = create_icon2(surf, pwindow->dst,
                             WF_FREE_THEME | WF_RESTORE_BACKGROUND);
      set_wstate(pwidget, FC_WS_NORMAL);
      pwidget->action = change_tech_callback;
      pwidget->id = MAX_ID - sub_tech;
      widget_add_as_prev(pwidget, dock);
      dock = pwidget;
      store->sub_targets[sub_targets_count++] = pwidget;
    }
  }
  if (sub_targets_count < 6) {
    store->sub_targets[sub_targets_count] = NULL;
  }

  FREEUTF8STR(pstr);

  /* ------------------------------------------ */
  if (sub_req_count > 0) {
    w = (adj_size(20) + store->sub_req[0]->size.w) * 2;
    w += (pwindow->size.w - (20 + store->sub_req[0]->size.w + w + ptech->size.w)) / 2;
  } else {
    if (req_count > 0) {
      w = (pwindow->area.x + 1
           + width + store->requirement_button[0]->size.w * 2 + adj_size(20));
      w += (pwindow->size.w - ((adj_size(20) + store->requirement_button[0]->size.w)
                               + w + ptech->size.w)) / 2;
    } else {
      w = (pwindow->size.w - ptech->size.w) / 2;
    }
  }

  ptech->size.x = pwindow->size.x + w;
  ptech->size.y = pwindow->area.y + (pwindow->area.h - ptech->size.h) / 2;

  if (req_count > 0) {
    h = (req_count == 1 ? store->requirement_button[0]->size.h :
        req_count * (store->requirement_button[0]->size.h + adj_size(80)) - adj_size(80));
    h = ptech->size.y + (ptech->size.h - h) / 2;
    for (i = 0; i < req_count; i++) {
      store->requirement_button[i]->size.x
        = ptech->size.x - adj_size(20) - store->requirement_button[i]->size.w;
      store->requirement_button[i]->size.y = h;
      h += (store->requirement_button[i]->size.h + adj_size(80));
    }
  }

  if (sub_req_count > 0) {
    h = (sub_req_count == 1 ? store->sub_req[0]->size.h :
     sub_req_count * (store->sub_req[0]->size.h + adj_size(20)) - adj_size(20));
    h = ptech->size.y + (ptech->size.h - h) / 2;
    for (i = 0; i < sub_req_count; i++) {
      store->sub_req[i]->size.x
        = ptech->size.x - (adj_size(20) + store->sub_req[i]->size.w) * 2;
      store->sub_req[i]->size.y = h;
      h += (store->sub_req[i]->size.h + adj_size(20));
    }
  }

  if (targets_count > 0) {
    h = (targets_count == 1 ? store->targets[0]->size.h :
     targets_count * (store->targets[0]->size.h + adj_size(20)) - adj_size(20));
    h = ptech->size.y + (ptech->size.h - h) / 2;
    for (i = 0; i < targets_count; i++) {
      store->targets[i]->size.x = ptech->size.x + ptech->size.w + adj_size(20);
      store->targets[i]->size.y = h;
      h += (store->targets[i]->size.h + adj_size(20));
    }
  }

  if (sub_targets_count > 0) {
    if (sub_targets_count < 3) {
      store->sub_targets[0]->size.x
        = ptech->size.x + ptech->size.w - store->sub_targets[0]->size.w;
      store->sub_targets[0]->size.y
        = ptech->size.y - store->sub_targets[0]->size.h - adj_size(10);
      if (store->sub_targets[1]) {
        store->sub_targets[1]->size.x
          = ptech->size.x + ptech->size.w - store->sub_targets[1]->size.w;
        store->sub_targets[1]->size.y = ptech->size.y + ptech->size.h + adj_size(10);
      }
    } else {
      if (sub_targets_count < 5) {
        for (i = 0; i < MIN(sub_targets_count, 4); i++) {
          store->sub_targets[i]->size.x
            = ptech->size.x + ptech->size.w - store->sub_targets[i]->size.w;
          if (i < 2) {
            store->sub_targets[i]->size.y
              = ptech->size.y - (store->sub_targets[i]->size.h + adj_size(5)) * (2 - i);
          } else {
            store->sub_targets[i]->size.y
              = ptech->size.y + ptech->size.h + adj_size(5)
                + (store->sub_targets[i]->size.h + adj_size(5)) * (i - 2);
          }
        }
      } else {
        h = (store->sub_targets[0]->size.h + adj_size(6));
        for (i = 0; i < MIN(sub_targets_count, 6); i++) {
          switch (i) {
          case 0:
            store->sub_targets[i]->size.x
              = ptech->size.x + ptech->size.w - store->sub_targets[i]->size.w;
            store->sub_targets[i]->size.y = ptech->size.y - h * 2;
            break;
          case 1:
            store->sub_targets[i]->size.x
              = ptech->size.x + ptech->size.w - store->sub_targets[i]->size.w * 2
                - adj_size(10);
            store->sub_targets[i]->size.y = ptech->size.y - h - h / 2;
            break;
          case 2:
            store->sub_targets[i]->size.x
              = ptech->size.x + ptech->size.w - store->sub_targets[i]->size.w;
            store->sub_targets[i]->size.y = ptech->size.y - h;
            break;
          case 3:
            store->sub_targets[i]->size.x
              = ptech->size.x + ptech->size.w - store->sub_targets[i]->size.w;
            store->sub_targets[i]->size.y = ptech->size.y + ptech->size.h + adj_size(6);
            break;
          case 4:
            store->sub_targets[i]->size.x
              = ptech->size.x + ptech->size.w - store->sub_targets[i]->size.w;
            store->sub_targets[i]->size.y = ptech->size.y + ptech->size.h + adj_size(6) + h;
            break;
          default:
            store->sub_targets[i]->size.x
              = ptech->size.x + ptech->size.w - store->sub_targets[i]->size.w * 2
                - adj_size(10);
            store->sub_targets[i]->size.y
              = ptech->size.y + ptech->size.h + adj_size(6) + h / 2 ;
            break;
          }
        }
      }
    }
  }

  return pwidget;
}

/**********************************************************************//**
  Show tech info
**************************************************************************/
void popup_tech_info(Tech_type_id tech)
{
  struct widget *pwindow;
  struct techs_buttons *store;
  struct widget *close_button = NULL;
  struct widget *advance_label = NULL;
  struct widget *list_toggle_button = NULL;
  struct widget *dock;
  utf8_str *title;
  utf8_str *pstr;
  SDL_Surface *surf;
  int h, tech_count;
  bool created;
  int scrollbar_width = 0;
  SDL_Rect area;

  if (current_help_dlg != HELP_TECH) {
    popdown_help_dialog();
  }

  /* Create new dialog if it doesn't exist yet */
  if (!help_dlg) {
    current_help_dlg = HELP_TECH;
    created = TRUE;

    /* Create dialog */
    help_dlg = fc_calloc(1, sizeof(struct advanced_dialog));
    store = fc_calloc(1, sizeof(struct techs_buttons));

    store->show_tree = FALSE;
    store->show_full_tree = FALSE;

    /* Create window */
    title = create_utf8_from_char_fonto(_("Help : Advances Tree"),
                                        FONTO_ATTENTION);
    title->style |= TTF_STYLE_BOLD;

    pwindow = create_window_skeleton(NULL, title, WF_FREE_DATA);
    pwindow->data.ptr = (void *)store;
    pwindow->action = help_dlg_window_callback;
    set_wstate(pwindow, FC_WS_NORMAL);

    add_to_gui_list(ID_WINDOW, pwindow);

    help_dlg->end_widget_list = pwindow;

    /* ------------------ */

    /* Close button */
    close_button = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                                    WF_WIDGET_HAS_INFO_LABEL
                                    | WF_RESTORE_BACKGROUND);
    close_button->info_label
      = create_utf8_from_char_fonto(_("Close Dialog (Esc)"), FONTO_ATTENTION);
    close_button->action = exit_help_dlg_callback;
    set_wstate(close_button, FC_WS_NORMAL);
    close_button->key = SDLK_ESCAPE;

    add_to_gui_list(ID_BUTTON, close_button);

    /* ------------------ */
    dock = close_button;

    /* --- Create scrollable advance list on the left side ---*/
    pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
    pstr->style |= (TTF_STYLE_BOLD | SF_CENTER);

    tech_count = 0;
    advance_index_iterate(A_FIRST, i) {
      struct advance *vap = valid_advance_by_number(i);

      if (vap) {
        copy_chars_to_utf8_str(pstr, advance_name_translation(vap));
        surf = create_select_tech_icon(pstr, i, TIM_SMALL_MODE);
        advance_label = create_icon2(surf, pwindow->dst,
                                     WF_FREE_THEME | WF_RESTORE_BACKGROUND);

        set_wstate(advance_label, FC_WS_NORMAL);
        advance_label->action = change_tech_callback;
        add_to_gui_list(MAX_ID - i, advance_label);

        if (tech_count++ >= 10) {
          set_wflag(advance_label, WF_HIDDEN);
        }
      }
    } advance_index_iterate_end;

    FREEUTF8STR(pstr);

    help_dlg->end_active_widget_list = dock->prev;
    help_dlg->begin_widget_list = advance_label ? advance_label : close_button;
    help_dlg->begin_active_widget_list = help_dlg->begin_widget_list;

    if (tech_count > 10) {
      help_dlg->active_widget_list = help_dlg->end_active_widget_list;
      scrollbar_width = create_vertical_scrollbar(help_dlg, 1, 10, TRUE, TRUE);
    }

    /* Toggle techs list button */
    list_toggle_button = create_themeicon_button_from_chars_fonto(
                                                            current_theme->up_icon,
                                                            pwindow->dst,
                                                            _("Advances"),
                                                            FONTO_DEFAULT, 0);
    list_toggle_button->action = toggle_full_tree_mode_in_help_dlg_callback;
    if (store->show_tree) {
      set_wstate(list_toggle_button, FC_WS_NORMAL);
    }
    widget_resize(list_toggle_button, adj_size(160), adj_size(15));
    list_toggle_button->string_utf8->fgcol = *get_theme_color(COLOR_THEME_HELPDLG_TEXT);

    add_to_gui_list(ID_BUTTON, list_toggle_button);

    dock = list_toggle_button;
    store->dock = dock;
  } else {
    created = FALSE;
    scrollbar_width = (help_dlg->scroll ? help_dlg->scroll->up_left_button->size.w: 0);
    pwindow = help_dlg->end_widget_list;
    store = (struct techs_buttons *)pwindow->data.ptr;
    dock = store->dock;

    /* Delete any previous list entries */
    if (dock != help_dlg->begin_widget_list) {
      del_group_of_widgets_from_gui_list(help_dlg->begin_widget_list, dock->prev);
      help_dlg->begin_widget_list = dock;
    }

    /* Show/hide techs list */
    list_toggle_button = dock;

    if (store->show_tree) {
      set_wstate(list_toggle_button, FC_WS_NORMAL);
    } else {
      set_wstate(list_toggle_button, FC_WS_DISABLED);
    }

    if (store->show_full_tree) {
      /* All entries are visible without scrolling */
      hide_group(help_dlg->begin_active_widget_list,
                 help_dlg->end_active_widget_list);
      hide_scrollbar(help_dlg->scroll);
    } else {
      int count = help_dlg->scroll->active;

      advance_label = help_dlg->active_widget_list;
      while (advance_label->prev != NULL && --count > 0) {
        advance_label = advance_label->prev;
      }
      show_group(advance_label, help_dlg->active_widget_list);
      show_scrollbar(help_dlg->scroll);
    }
  }

  /* --------------------------------------------------------- */
  if (created) {

    surf = theme_get_background(active_theme, BACKGROUND_HELPDLG);
    if (resize_window(pwindow, surf, NULL, adj_size(640), adj_size(480))) {
      FREESURFACE(surf);
    }

    area = pwindow->area;

    widget_set_position(pwindow,
                        (main_window_width() - pwindow->size.w) / 2,
                        (main_window_height() - pwindow->size.h) / 2);

    /* Exit button */
    close_button = pwindow->prev;
    widget_set_position(close_button,
                        area.x + area.w - close_button->size.w - 1,
                        pwindow->size.y + adj_size(2));

    /* List toggle button */
    list_toggle_button = store->dock;
    widget_set_position(list_toggle_button, area.x, area.y);

    /* List entries */
    h = setup_vertical_widgets_position(1, area.x + scrollbar_width,
                                        area.y + list_toggle_button->size.h, 0, 0,
                                        help_dlg->begin_active_widget_list,
                                        help_dlg->end_active_widget_list);
    /* Scrollbar */
    if (help_dlg->scroll) {
      setup_vertical_scrollbar_area(help_dlg->scroll,
                                    area.x, area.y + list_toggle_button->size.h,
                                    h, FALSE);
    }
  }

  if (store->show_tree) {
    help_dlg->begin_widget_list = create_tech_tree(tech, scrollbar_width, pwindow, store);
    redraw_tech_tree_dlg();
  } else {
    help_dlg->begin_widget_list = create_tech_info(tech, scrollbar_width, pwindow, store);
    redraw_tech_info_dlg();
  }
}
