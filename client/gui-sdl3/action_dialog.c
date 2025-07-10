/***********************************************************************
 Freeciv - Copyright (C) 1996-2006 - Freeciv Development Team
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

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "log.h"

/* common */
#include "actions.h"
#include "game.h"
#include "movement.h"
#include "research.h"
#include "specialist.h"
#include "traderoutes.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "control.h"
#include "text.h"

/* client/gui-sdl3 */
#include "citydlg.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "repodlgs.h"
#include "themespec.h"
#include "widget.h"

#include "dialogs_g.h"

typedef int (*act_func)(struct widget *);

struct diplomat_dialog {
  int actor_unit_id;
  int target_ids[ATK_COUNT];
  int sub_target_id[ASTK_COUNT];
  action_id act_id;
  struct advanced_dialog *pdialog;
};

struct small_diplomat_dialog {
  int actor_unit_id;
  int target_id;
  action_id act_id;
  struct small_dialog *pdialog;
};

extern bool is_unit_move_blocked;

void popdown_diplomat_dialog(void);
void popdown_incite_dialog(void);
void popdown_bribe_dialog(void);


static struct diplomat_dialog *diplomat_dlg = NULL;
static bool is_more_user_input_needed = FALSE;
static bool did_not_decide = FALSE;

/**********************************************************************//**
  The action selection is done.
**************************************************************************/
static void act_sel_done_primary(int actor_unit_id)
{
  if (!is_more_user_input_needed) {
    /* The client isn't waiting for information for any unanswered follow
     * up questions. */

    struct unit *actor_unit;

    if ((actor_unit = game_unit_by_number(actor_unit_id))) {
      /* The action selection dialog wasn't closed because the actor unit
       * was lost. */

      /* The probabilities didn't just disappear, right? */
      fc_assert_action(actor_unit->client.act_prob_cache,
                       client_unit_init_act_prob_cache(actor_unit));

      FC_FREE(actor_unit->client.act_prob_cache);
    }

    /* The action selection process is over, at least for now. */
    action_selection_no_longer_in_progress(actor_unit_id);

    if (did_not_decide) {
      /* The action selection dialog was closed but the player didn't
       * decide what the unit should do. */

      /* Reset so the next action selection dialog does the right thing. */
      did_not_decide = FALSE;
    } else {
      /* An action, or no action at all, was selected. */
      action_decision_clear_want(actor_unit_id);
      action_selection_next_in_focus(actor_unit_id);
    }
  }
}

/**********************************************************************//**
  A follow up question about the selected action is done.
**************************************************************************/
static void act_sel_done_secondary(int actor_unit_id)
{
  /* Stop blocking. */
  is_more_user_input_needed = FALSE;
  act_sel_done_primary(actor_unit_id);
}

/**********************************************************************//**
  Let the non shared client code know that the action selection process
  no longer is in progress for the specified unit.

  This allows the client to clean up any client specific assumptions.
**************************************************************************/
void action_selection_no_longer_in_progress_gui_specific(int actor_id)
{
  /* Stop assuming the answer to a follow up question will arrive. */
  is_more_user_input_needed = FALSE;
}

/**********************************************************************//**
  Get the non targeted version of an action so it, if enabled, can appear
  in the target selection dialog.
**************************************************************************/
static action_id get_non_targeted_action_id(action_id tgt_action_id)
{
  /* Don't add an action mapping here unless the non targeted version is
   * selectable in the targeted version's target selection dialog. */
  switch ((enum gen_action)tgt_action_id) {
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
    return ACTION_SPY_SABOTAGE_CITY;
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
    return ACTION_SPY_SABOTAGE_CITY_ESC;
  case ACTION_SPY_TARGETED_STEAL_TECH:
    return ACTION_SPY_STEAL_TECH;
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
    return ACTION_SPY_STEAL_TECH_ESC;
  default:
    /* No non targeted version found. */
    return ACTION_NONE;
  }
}

/**********************************************************************//**
  Get the production targeted version of an action so it, if enabled, can
  appear in the target selection dialog.
**************************************************************************/
static action_id get_production_targeted_action_id(action_id tgt_action_id)
{
  /* Don't add an action mapping here unless the non targeted version is
   * selectable in the targeted version's target selection dialog. */
  switch ((enum gen_action)tgt_action_id) {
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
    return ACTION_SPY_SABOTAGE_CITY_PRODUCTION;
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
    return ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC;
  case ACTION_STRIKE_BUILDING:
    return ACTION_STRIKE_PRODUCTION;
  default:
    /* No non targeted version found. */
    return ACTION_NONE;
  }
}

/**********************************************************************//**
  User interacted with diplomat dialog.
**************************************************************************/
static int diplomat_dlg_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(diplomat_dlg->pdialog->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  Requests up-to-date list of improvements, the return of
  which will trigger the popup_sabotage_dialog() function.
**************************************************************************/
static int spy_strike_bld_request(struct widget *pwidget)
{
  if (NULL != game_unit_by_number(diplomat_dlg->actor_unit_id)
      && NULL != game_city_by_number(
              diplomat_dlg->target_ids[ATK_CITY])) {
    request_action_details(ACTION_STRIKE_BUILDING,
                           diplomat_dlg->actor_unit_id,
                           diplomat_dlg->target_ids[ATK_CITY]);
    is_more_user_input_needed = TRUE;
    popdown_diplomat_dialog();
  } else {
    popdown_diplomat_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Requests up-to-date list of improvements, the return of
  which will trigger the popup_sabotage_dialog() function.
**************************************************************************/
static int spy_sabotage_request(struct widget *pwidget)
{
  if (NULL != game_unit_by_number(diplomat_dlg->actor_unit_id)
      && NULL != game_city_by_number(
              diplomat_dlg->target_ids[ATK_CITY])) {
    request_action_details(ACTION_SPY_TARGETED_SABOTAGE_CITY,
                           diplomat_dlg->actor_unit_id,
                           diplomat_dlg->target_ids[ATK_CITY]);
    is_more_user_input_needed = TRUE;
    popdown_diplomat_dialog();
  } else {
    popdown_diplomat_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Requests up-to-date list of improvements, the return of
  which will trigger the popup_sabotage_dialog() function.
  (Escape version)
**************************************************************************/
static int spy_sabotage_esc_request(struct widget *pwidget)
{
  if (NULL != game_unit_by_number(diplomat_dlg->actor_unit_id)
      && NULL != game_city_by_number(
              diplomat_dlg->target_ids[ATK_CITY])) {
    request_action_details(ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC,
                           diplomat_dlg->actor_unit_id,
                           diplomat_dlg->target_ids[ATK_CITY]);
    is_more_user_input_needed = TRUE;
    popdown_diplomat_dialog();
  } else {
    popdown_diplomat_dialog();
  }

  return -1;
}

/* --------------------------------------------------------- */

/**********************************************************************//**
  User interacted with spy's steal dialog window.
**************************************************************************/
static int spy_steal_dlg_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(diplomat_dlg->pdialog->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  Exit spy's steal or sabotage dialog.
**************************************************************************/
static int exit_spy_tgt_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int actor_id = diplomat_dlg->actor_unit_id;

    fc_assert(is_more_user_input_needed);
    popdown_diplomat_dialog();
    act_sel_done_secondary(actor_id);
  }

  return -1;
}

/**********************************************************************//**
  User selected which tech spy steals.
**************************************************************************/
static int spy_steal_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int steal_advance = MAX_ID - pwidget->id;
    int actor_id = diplomat_dlg->actor_unit_id;

    if (NULL != game_unit_by_number(diplomat_dlg->actor_unit_id)
        && NULL != game_city_by_number(
                diplomat_dlg->target_ids[ATK_CITY])) {
      if (steal_advance == A_UNSET) {
        /* This is the untargeted version. */
        request_do_action(get_non_targeted_action_id(
                          diplomat_dlg->act_id),
                          diplomat_dlg->actor_unit_id,
                          diplomat_dlg->target_ids[ATK_CITY],
                          steal_advance, "");
      } else {
        /* This is the targeted version. */
        request_do_action(diplomat_dlg->act_id,
                          diplomat_dlg->actor_unit_id,
                          diplomat_dlg->target_ids[ATK_CITY],
                          steal_advance, "");
      }
    }

    fc_assert(is_more_user_input_needed);
    popdown_diplomat_dialog();
    act_sel_done_secondary(actor_id);
  }

  return -1;
}

/**********************************************************************//**
  Popup spy tech stealing dialog.
**************************************************************************/
static int spy_steal_popup_shared(struct widget *pwidget)
{
  const struct research *presearch, *vresearch;
  struct city *vcity = pwidget->data.city;
  int id = diplomat_dlg->actor_unit_id;
  struct player *victim = NULL;
  struct container *cont;
  struct widget *buf = NULL;
  struct widget *pwindow;
  utf8_str *pstr;
  SDL_Surface *surf;
  int max_col, max_row, col, count = 0;
  int tech, idx;
  SDL_Rect area;
  struct unit *actor_unit = game_unit_by_number(id);
  action_id act_id = diplomat_dlg->act_id;
  Tech_type_id ac;

  is_more_user_input_needed = TRUE;
  popdown_diplomat_dialog();

  if (vcity) {
    victim = city_owner(vcity);
  }

  fc_assert_ret_val_msg(!diplomat_dlg, 1, "Diplomat dialog already open");

  if (!victim) {
    act_sel_done_secondary(id);
    return 1;
  }

  count = 0;
  presearch = research_get(client_player());
  vresearch = research_get(victim);
  ac = advance_count();
  advance_index_iterate_max(A_FIRST, i, ac) {
    if (research_invention_gettable(presearch, i,
                                    game.info.tech_steal_allow_holes)
        && TECH_KNOWN == research_invention_state(vresearch, i)
        && TECH_KNOWN != research_invention_state(presearch, i)) {
      count++;
    }
  } advance_index_iterate_max_end;

  if (!count) {
    /* if there is no known tech to steal then
     * send steal order at Spy's Discretion */
    int target_id = vcity->id;

    request_do_action(get_non_targeted_action_id(act_id),
                      id, target_id, A_UNSET, "");

    act_sel_done_secondary(id);

    return -1;
  }

  cont = fc_calloc(1, sizeof(struct container));
  cont->id0 = vcity->id;
  cont->id1 = id; /* spy id */

  diplomat_dlg = fc_calloc(1, sizeof(struct diplomat_dialog));
  diplomat_dlg->act_id = act_id;
  diplomat_dlg->actor_unit_id = id;
  diplomat_dlg->target_ids[ATK_CITY] = vcity->id;
  diplomat_dlg->pdialog = fc_calloc(1, sizeof(struct advanced_dialog));

  pstr = create_utf8_from_char_fonto(_("Select Advance to Steal"),
                                     FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = spy_steal_dlg_window_callback;
  set_wstate(pwindow , FC_WS_NORMAL);

  add_to_gui_list(ID_DIPLOMAT_DLG_WINDOW, pwindow);
  diplomat_dlg->pdialog->end_widget_list = pwindow;

  area = pwindow->area;
  area.w = MAX(area.w, adj_size(8));

  /* ------------------ */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  area.w += buf->size.w + adj_size(10);
  buf->action = exit_spy_tgt_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, buf);
  /* ------------------------- */

  if (action_prob_possible(actor_unit->client.act_prob_cache[
                           get_non_targeted_action_id(act_id)])) {
     /* count + at Spy's Discretion */
    count++;
  }

  /* max col - 104 is steal tech widget width */
  max_col = (main_window_width() - (pwindow->size.w - pwindow->area.w) - adj_size(2)) / adj_size(104);
  /* max row - 204 is steal tech widget height */
  max_row = (main_window_height() - (pwindow->size.h - pwindow->area.h)) / adj_size(204);

  /* make space on screen for scrollbar */
  if (max_col * max_row < count) {
    max_col--;
  }

  if (count < max_col + 1) {
    col = count;
  } else {
    if (count < max_col + adj_size(3)) {
      col = max_col - adj_size(2);
    } else {
      if (count < max_col + adj_size(5)) {
        col = max_col - 1;
      } else {
        col = max_col;
      }
    }
  }

  pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  count = 0;
  advance_index_iterate_max(A_FIRST, i, ac) {
    if (research_invention_gettable(presearch, i,
                                    game.info.tech_steal_allow_holes)
        && TECH_KNOWN == research_invention_state(vresearch, i)
        && TECH_KNOWN != research_invention_state(presearch, i)) {
      count++;

      copy_chars_to_utf8_str(pstr, advance_name_translation(advance_by_number(i)));
      surf = create_select_tech_icon(pstr, i, TIM_FULL_MODE);
      buf = create_icon2(surf, pwindow->dst,
                         WF_FREE_THEME | WF_RESTORE_BACKGROUND);

      set_wstate(buf, FC_WS_NORMAL);
      buf->action = spy_steal_callback;
      buf->data.cont = cont;

      add_to_gui_list(MAX_ID - i, buf);

      if (count > (col * max_row)) {
        set_wflag(buf, WF_HIDDEN);
      }
    }
  } advance_index_iterate_max_end;

  /* Get Spy tech to use for "At Spy's Discretion" -- this will have the
   * side effect of displaying the unit's icon */
  tech = advance_number(utype_primary_tech_req(unit_type_get(actor_unit)));

  if (action_prob_possible(actor_unit->client.act_prob_cache[
                           get_non_targeted_action_id(act_id)])) {
    struct astring str = ASTRING_INIT;

    /* TRANS: %s is a unit name, e.g., Spy */
    astr_set(&str, _("At %s's Discretion"),
             unit_name_translation(actor_unit));
    copy_chars_to_utf8_str(pstr, astr_str(&str));
    astr_free(&str);

    surf = create_select_tech_icon(pstr, tech, TIM_FULL_MODE);

    buf = create_icon2(surf, pwindow->dst,
                       (WF_FREE_THEME | WF_RESTORE_BACKGROUND
                        | WF_FREE_DATA));
    set_wstate(buf, FC_WS_NORMAL);
    buf->action = spy_steal_callback;
    buf->data.cont = cont;

    add_to_gui_list(MAX_ID - A_UNSET, buf);
    count++;
  }

  /* --------------------------------------------------------- */
  FREEUTF8STR(pstr);
  diplomat_dlg->pdialog->begin_widget_list = buf;
  diplomat_dlg->pdialog->begin_active_widget_list = diplomat_dlg->pdialog->begin_widget_list;
  diplomat_dlg->pdialog->end_active_widget_list = diplomat_dlg->pdialog->end_widget_list->prev->prev;

  /* -------------------------------------------------------------- */

  idx = 0;
  if (count > col) {
    count = (count + (col - 1)) / col;
    if (count > max_row) {
      diplomat_dlg->pdialog->active_widget_list = diplomat_dlg->pdialog->end_active_widget_list;
      count = max_row;
      idx = create_vertical_scrollbar(diplomat_dlg->pdialog, col, count, TRUE, TRUE);
    }
  } else {
    count = 1;
  }

  area.w = MAX(area.w, (col * buf->size.w + adj_size(2) + idx));
  area.h = count * buf->size.h + adj_size(2);

  /* alloca window theme and win background buffer */
  surf = theme_get_background(active_theme, BACKGROUND_SPYSTEALDLG);
  if (resize_window(pwindow, surf, NULL,
                    (pwindow->size.w - pwindow->area.w) + area.w,
                    (pwindow->size.h - pwindow->area.h) + area.h)) {
    FREESURFACE(surf);
  }

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
                  diplomat_dlg->pdialog->begin_active_widget_list,
                  diplomat_dlg->pdialog->end_active_widget_list);

  if (diplomat_dlg->pdialog->scroll) {
    setup_vertical_scrollbar_area(diplomat_dlg->pdialog->scroll,
                                  area.x + area.w, area.y,
                                  area.h, TRUE);
  }

  redraw_group(diplomat_dlg->pdialog->begin_widget_list, pwindow, FALSE);
  widget_mark_dirty(pwindow);

  return -1;
}

/**********************************************************************//**
  Popup spy tech stealing dialog for "Targeted Steal Tech".
**************************************************************************/
static int spy_steal_popup(struct widget *pwidget)
{
  diplomat_dlg->act_id = ACTION_SPY_TARGETED_STEAL_TECH;
  return spy_steal_popup_shared(pwidget);
}

/**********************************************************************//**
  Popup spy tech stealing dialog for "Targeted Steal Tech Escape Expected".
**************************************************************************/
static int spy_steal_esc_popup(struct widget *pwidget)
{
  diplomat_dlg->act_id = ACTION_SPY_TARGETED_STEAL_TECH_ESC;
  return spy_steal_popup_shared(pwidget);
}

/**********************************************************************//**
  Ask the server how much the revolt is going to cost us
**************************************************************************/
static int diplomat_incite_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (NULL != game_unit_by_number(diplomat_dlg->actor_unit_id)
        && NULL != game_city_by_number(
                diplomat_dlg->target_ids[ATK_CITY])) {
      request_action_details(ACTION_SPY_INCITE_CITY,
                             diplomat_dlg->actor_unit_id,
                             diplomat_dlg->target_ids[ATK_CITY]);
      is_more_user_input_needed = TRUE;
      popdown_diplomat_dialog();
    } else {
      popdown_diplomat_dialog();
    }
  }

  return -1;
}

/**********************************************************************//**
  Ask the server how much the revolt is going to cost us
**************************************************************************/
static int spy_incite_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (NULL != game_unit_by_number(diplomat_dlg->actor_unit_id)
        && NULL != game_city_by_number(
                diplomat_dlg->target_ids[ATK_CITY])) {
      request_action_details(ACTION_SPY_INCITE_CITY_ESC,
                             diplomat_dlg->actor_unit_id,
                             diplomat_dlg->target_ids[ATK_CITY]);
      is_more_user_input_needed = TRUE;
      popdown_diplomat_dialog();
    } else {
      popdown_diplomat_dialog();
    }
  }

  return -1;
}

/**********************************************************************//**
  Delay selection of what action to take.
**************************************************************************/
static int act_sel_wait_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    key_unit_wait();

    /* The dialog was popped down when key_unit_wait() resulted in
     * action_selection_close() being called. */
  }

  return -1;
}

/**********************************************************************//**
  Ask the server how much the bribe costs
**************************************************************************/
static int diplomat_bribe_unit_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (NULL != game_unit_by_number(diplomat_dlg->actor_unit_id)
        && NULL !=
         game_unit_by_number(diplomat_dlg->target_ids[ATK_UNIT])) {
      request_action_details(ACTION_SPY_BRIBE_UNIT,
                             diplomat_dlg->actor_unit_id,
                             diplomat_dlg->target_ids[ATK_UNIT]);
      is_more_user_input_needed = TRUE;
      popdown_diplomat_dialog();
    } else {
      popdown_diplomat_dialog();
    }
  }

  return -1;
}

/**********************************************************************//**
  Ask the server how much the bribe costs
**************************************************************************/
static int diplomat_bribe_stack_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (game_unit_by_number(diplomat_dlg->actor_unit_id) != nullptr) {
      request_action_details(ACTION_SPY_BRIBE_STACK,
                             diplomat_dlg->actor_unit_id,
                             diplomat_dlg->target_ids[ATK_STACK]);
      is_more_user_input_needed = TRUE;
      popdown_diplomat_dialog();
    } else {
      popdown_diplomat_dialog();
    }
  }

  return -1;
}

/**********************************************************************//**
  User clicked "Found City"
**************************************************************************/
static int found_city_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int actor_id = diplomat_dlg->actor_unit_id;

    popdown_diplomat_dialog();
    dsend_packet_city_name_suggestion_req(&client.conn,
                                          actor_id);
  }

  return -1;
}

/**********************************************************************//**
  User clicked "Upgrade Unit"
**************************************************************************/
static int upgrade_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit;

    if (game_city_by_number(diplomat_dlg->target_ids[ATK_CITY]) != NULL
        && (punit = game_unit_by_number(diplomat_dlg->actor_unit_id))) {
      popup_unit_upgrade_dlg(punit, FALSE);
    }

    popdown_diplomat_dialog();
  }

  return -1;
}

/**********************************************************************//**
  User selected an action from the choice dialog and the action has no
  special needs.
**************************************************************************/
static int simple_action_callback(struct widget *pwidget)
{
  int actor_id, target_id, sub_target;
  struct action *paction;

  bool failed = FALSE;

  if (!PRESSED_EVENT(main_data.event)) {
    return -1;
  }

  /* Data */
  paction = action_by_number(MAX_ID - pwidget->id);

  /* Actor */
  fc_assert(action_get_actor_kind(paction) == AAK_UNIT);
  actor_id = diplomat_dlg->actor_unit_id;
  if (NULL == game_unit_by_number(actor_id)) {
    /* Probably dead. */
    failed = TRUE;
  }

  /* Target */
  target_id = IDENTITY_NUMBER_ZERO;
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    target_id = diplomat_dlg->target_ids[ATK_CITY];
    if (NULL == game_city_by_number(target_id)) {
      /* Probably destroyed. */
      failed = TRUE;
    }
    break;
  case ATK_UNIT:
    target_id = diplomat_dlg->target_ids[ATK_UNIT];
    if (NULL == game_unit_by_number(target_id)) {
      /* Probably dead. */
      failed = TRUE;
    }
    break;
  case ATK_STACK:
  case ATK_TILE:
  case ATK_EXTRAS:
    target_id = diplomat_dlg->target_ids[ATK_TILE];
    if (NULL == index_to_tile(&(wld.map), target_id)) {
      /* TODO: Should this be possible at all? If not: add assertion. */
      failed = TRUE;
    }
    break;
  case ATK_SELF:
    target_id = IDENTITY_NUMBER_ZERO;
    break;
  case ATK_COUNT:
    fc_assert(action_get_target_kind(paction) != ATK_COUNT);
    failed = TRUE;
  }

  /* Sub target. */
  sub_target = NO_TARGET;
  if (paction->target_complexity != ACT_TGT_COMPL_SIMPLE) {
    switch (action_get_sub_target_kind(paction)) {
    case ASTK_BUILDING:
      sub_target = diplomat_dlg->sub_target_id[ASTK_BUILDING];
      if (NULL == improvement_by_number(sub_target)) {
        /* Did the ruleset change? */
        failed = TRUE;
      }
      break;
    case ASTK_TECH:
      sub_target = diplomat_dlg->sub_target_id[ASTK_TECH];
      if (NULL == valid_advance_by_number(sub_target)) {
        /* Did the ruleset change? */
        failed = TRUE;
      }
      break;
    case ASTK_EXTRA:
    case ASTK_EXTRA_NOT_THERE:
      /* TODO: validate if the extra is there? */
      sub_target = diplomat_dlg->sub_target_id[ASTK_EXTRA];
      if (NULL == extra_by_number(sub_target)) {
        /* Did the ruleset change? */
        failed = TRUE;
      }
      break;
    case ASTK_SPECIALIST:
      sub_target = diplomat_dlg->sub_target_id[ASTK_SPECIALIST];
      if (nullptr == specialist_by_number(sub_target)) {
        /* Did the ruleset change? */
        failed = TRUE;
      }
      break;
    case ASTK_NONE:
    case ASTK_COUNT:
      /* Shouldn't happen. */
      fc_assert(action_get_sub_target_kind(paction) != ASTK_NONE);
      failed = TRUE;
      break;
    }
  }

  /* Send request. */
  if (!failed) {
    request_do_action(paction->id, actor_id, target_id, sub_target, "");
  }

  /* Clean up. */
  popdown_diplomat_dialog();
  return -1;
}

/**********************************************************************//**
  Close diplomat dialog.
**************************************************************************/
static int diplomat_close_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_diplomat_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Popdown a dialog giving a diplomatic unit some options when moving into
  the target tile.
**************************************************************************/
void popdown_diplomat_dialog(void)
{
  if (diplomat_dlg) {
    act_sel_done_primary(diplomat_dlg->actor_unit_id);

    is_unit_move_blocked = FALSE;
    popdown_window_group_dialog(diplomat_dlg->pdialog->begin_widget_list,
                                diplomat_dlg->pdialog->end_widget_list);
    FC_FREE(diplomat_dlg->pdialog->scroll);
    FC_FREE(diplomat_dlg->pdialog);
    FC_FREE(diplomat_dlg);
    queue_flush();
  }
}

/* Mapping from an action to the function to call when its button is
 * pushed. */
static const act_func af_map[ACTION_COUNT] = {
  /* Unit acting against a city target. */
  [ACTION_SPY_TARGETED_SABOTAGE_CITY] = spy_sabotage_request,
  [ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC] = spy_sabotage_esc_request,
  [ACTION_SPY_TARGETED_STEAL_TECH] = spy_steal_popup,
  [ACTION_SPY_TARGETED_STEAL_TECH_ESC] = spy_steal_esc_popup,
  [ACTION_SPY_INCITE_CITY] = diplomat_incite_callback,
  [ACTION_SPY_INCITE_CITY_ESC] = spy_incite_callback,
  [ACTION_UPGRADE_UNIT] = upgrade_callback,
  [ACTION_STRIKE_BUILDING] = spy_strike_bld_request,

  /* Unit acting against a unit target. */
  [ACTION_SPY_BRIBE_UNIT] = diplomat_bribe_unit_callback,

  /* Unit acting against all units at a tile. */
  [ACTION_SPY_BRIBE_STACK] = diplomat_bribe_stack_callback,

  /* Unit acting against a tile. */
  [ACTION_FOUND_CITY] = found_city_callback,

  /* Unit acting with no target except itself. */
  /* No special callback functions needed for any self targeted actions. */
};

/**********************************************************************//**
  Add an entry for an action in the action choice dialog.
**************************************************************************/
static void action_entry(const action_id act,
                         const struct act_prob *act_probs,
                         struct unit *act_unit,
                         struct tile *tgt_tile,
                         struct city *tgt_city,
                         struct unit *tgt_unit,
                         struct widget *pwindow,
                         SDL_Rect *area)
{
  struct widget *buf;
  utf8_str *pstr;
  const char *ui_name;
  act_func cb;

  const char *custom = get_act_sel_action_custom_text(action_by_number(act),
                                                      act_probs[act],
                                                      act_unit,
                                                      tgt_city);

  if (af_map[act] == NULL) {
    /* No special call back function needed for this action. */
    cb = simple_action_callback;
  } else {
    /* Special action specific callback function specified. */
    cb = af_map[act];
  }

  /* Don't show disabled actions */
  if (!action_prob_possible(act_probs[act])) {
    return;
  }

  ui_name = action_prepare_ui_name(act, "",
                                   act_probs[act], custom);

  create_active_iconlabel(buf, pwindow->dst, pstr,
                          ui_name, cb);

  switch (action_id_get_target_kind(act)) {
  case ATK_CITY:
    buf->data.city = tgt_city;
    break;
  case ATK_UNIT:
    buf->data.unit = tgt_unit;
    break;
  case ATK_TILE:
  case ATK_EXTRAS:
  case ATK_STACK:
    buf->data.tile = tgt_tile;
    break;
  case ATK_SELF:
    buf->data.unit = act_unit;
    break;
  case ATK_COUNT:
    fc_assert_msg(FALSE, "Unsupported target kind");
  }

  set_wstate(buf, FC_WS_NORMAL);

  add_to_gui_list(MAX_ID - act, buf);

  area->w = MAX(area->w, buf->size.w);
  area->h += buf->size.h;
}

/**********************************************************************//**
  Popup a dialog that allows the player to select what action a unit
  should take.
**************************************************************************/
void popup_action_selection(struct unit *actor_unit,
                            struct city *target_city,
                            struct unit *target_unit,
                            struct tile *target_tile,
                            struct extra_type *target_extra,
                            const struct act_prob *act_probs)
{
  struct widget *pwindow = NULL, *buf = NULL;
  utf8_str *pstr;
  SDL_Rect area;

  fc_assert_ret_msg(!diplomat_dlg, "Diplomat dialog already open");

  /* Could be caused by the server failing to reply to a request for more
   * information or a bug in the client code. */
  fc_assert_msg(!is_more_user_input_needed,
                "Diplomat queue problem. Is another diplomat window open?");

  /* No extra input is required as no action has been chosen yet. */
  is_more_user_input_needed = FALSE;

  is_unit_move_blocked = TRUE;

  diplomat_dlg = fc_calloc(1, sizeof(struct diplomat_dialog));
  diplomat_dlg->actor_unit_id = actor_unit->id;
  diplomat_dlg->pdialog = fc_calloc(1, sizeof(struct advanced_dialog));

  /* window */
  if (target_city) {
    struct astring title = ASTRING_INIT;

    /* TRANS: %s is a unit name, e.g., Spy */
    astr_set(&title, _("Choose Your %s's Strategy"),
             unit_name_translation(actor_unit));
    pstr = create_utf8_from_char_fonto(astr_str(&title), FONTO_ATTENTION);
    astr_free(&title);
  } else {
    pstr = create_utf8_from_char_fonto(_("Subvert Enemy Unit"),
                                       FONTO_ATTENTION);
  }

  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = diplomat_dlg_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_CARAVAN_DLG_WINDOW, pwindow);
  diplomat_dlg->pdialog->end_widget_list = pwindow;

  area = pwindow->area;
  area.w = MAX(area.w, adj_size(8));
  area.h = MAX(area.h, adj_size(2));

  if (target_city) {
    diplomat_dlg->target_ids[ATK_CITY] = target_city->id;
  } else {
    diplomat_dlg->target_ids[ATK_CITY] = IDENTITY_NUMBER_ZERO;
  }

  if (target_unit) {
    diplomat_dlg->target_ids[ATK_UNIT] = target_unit->id;
  } else {
    diplomat_dlg->target_ids[ATK_UNIT] = IDENTITY_NUMBER_ZERO;
  }

  diplomat_dlg->target_ids[ATK_STACK] = tile_index(target_tile);
  diplomat_dlg->target_ids[ATK_TILE] = tile_index(target_tile);
  diplomat_dlg->target_ids[ATK_EXTRAS] = tile_index(target_tile);

  /* No target building or target tech supplied. (Feb 2020) */
  diplomat_dlg->sub_target_id[ASTK_BUILDING] = B_LAST;
  diplomat_dlg->sub_target_id[ASTK_TECH] = A_UNSET;
  diplomat_dlg->sub_target_id[ASTK_SPECIALIST] = -1;

  if (target_extra) {
    diplomat_dlg->sub_target_id[ASTK_EXTRA] = extra_number(target_extra);
    diplomat_dlg->sub_target_id[ASTK_EXTRA_NOT_THERE]
        = extra_number(target_extra);
  } else {
    diplomat_dlg->sub_target_id[ASTK_EXTRA] = EXTRA_NONE;
    diplomat_dlg->sub_target_id[ASTK_EXTRA_NOT_THERE] = EXTRA_NONE;
  }

  diplomat_dlg->target_ids[ATK_SELF] = actor_unit->id;

  /* ---------- */
  /* Unit acting against a city */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_CITY) {
      action_entry(act, act_probs,
                   actor_unit, NULL, target_city, NULL,
                   pwindow, &area);
    }
  } action_iterate_end;

  /* Unit acting against another unit */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_UNIT) {
      action_entry(act, act_probs,
                   actor_unit, NULL, NULL, target_unit,
                   pwindow, &area);
    }
  } action_iterate_end;

  /* Unit acting against all units at a tile */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_STACK) {
      action_entry(act, act_probs,
                   actor_unit, target_tile, NULL, NULL,
                   pwindow, &area);
    }
  } action_iterate_end;

  /* Unit acting against a tile. */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_TILE) {
      action_entry(act, act_probs,
                   actor_unit, target_tile, NULL, NULL,
                   pwindow, &area);
    }
  } action_iterate_end;

  /* Unit acting against a tile's extras. */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_EXTRAS) {
      action_entry(act, act_probs,
                   actor_unit, target_tile, NULL, NULL,
                   pwindow, &area);
    }
  } action_iterate_end;

  /* Unit acting against itself. */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_SELF) {
      action_entry(act, act_probs,
                   actor_unit, NULL, NULL, target_unit,
                   pwindow, &area);
    }
  } action_iterate_end;

  /* ---------- */
  create_active_iconlabel(buf, pwindow->dst, pstr,
                          _("Wait"), act_sel_wait_callback);

  buf->data.tile = target_tile;

  set_wstate(buf, FC_WS_NORMAL);

  add_to_gui_list(MAX_ID - actor_unit->id, buf);

  area.w = MAX(area.w, buf->size.w);
  area.h += buf->size.h;

  /* ---------- */
  create_active_iconlabel(buf, pwindow->dst, pstr,
                          _("Cancel"), diplomat_close_callback);

  set_wstate(buf , FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_LABEL , buf);

  area.w = MAX(area.w, buf->size.w);
  area.h += buf->size.h;
  /* ---------- */
  diplomat_dlg->pdialog->begin_widget_list = buf;

  /* setup window size and start position */

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  auto_center_on_focus_unit();
  put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h,
                           unit_tile(actor_unit));

  /* setup widget size and start position */

  buf = pwindow->prev;
  setup_vertical_widgets_position(1,
        area.x,
        area.y + 1, area.w, 0,
        diplomat_dlg->pdialog->begin_widget_list, buf);

  /* --------------------- */
  /* redraw */
  redraw_group(diplomat_dlg->pdialog->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);

  /* Give follow up questions access to action probabilities. */
  client_unit_init_act_prob_cache(actor_unit);
  action_iterate(act) {
    actor_unit->client.act_prob_cache[act] = act_probs[act];
  } action_iterate_end;
}

/**********************************************************************//**
  Returns the id of the actor unit currently handled in action selection
  dialog when the action selection dialog is open.
  Returns IDENTITY_NUMBER_ZERO if no action selection dialog is open.
**************************************************************************/
int action_selection_actor_unit(void)
{
  if (!diplomat_dlg) {
    return IDENTITY_NUMBER_ZERO;
  }

  return diplomat_dlg->actor_unit_id;
}

/**********************************************************************//**
  Returns id of the target city of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  city target. Returns IDENTITY_NUMBER_ZERO if no action selection dialog
  is open or no city target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_city(void)
{
  if (!diplomat_dlg) {
    return IDENTITY_NUMBER_ZERO;
  }

  return diplomat_dlg->target_ids[ATK_CITY];
}

/**********************************************************************//**
  Returns id of the target unit of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  unit target. Returns IDENTITY_NUMBER_ZERO if no action selection dialog
  is open or no unit target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_unit(void)
{
  if (!diplomat_dlg) {
    return IDENTITY_NUMBER_ZERO;
  }

  return diplomat_dlg->target_ids[ATK_UNIT];
}

/**********************************************************************//**
  Returns id of the target tile of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  tile target. Returns TILE_INDEX_NONE if no action selection dialog is
  open.
**************************************************************************/
int action_selection_target_tile(void)
{
  if (!diplomat_dlg) {
    return TILE_INDEX_NONE;
  }

  return diplomat_dlg->target_ids[ATK_TILE];
}

/**********************************************************************//**
  Returns id of the target extra of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has an
  extra target. Returns EXTRA_NONE if no action selection dialog is open
  or no extra target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_extra(void)
{
  if (!diplomat_dlg) {
    return EXTRA_NONE;
  }

  return diplomat_dlg->sub_target_id[ASTK_EXTRA];
}

/**********************************************************************//**
  Updates the action selection dialog with new information.
**************************************************************************/
void action_selection_refresh(struct unit *actor_unit,
                              struct city *target_city,
                              struct unit *target_unit,
                              struct tile *target_tile,
                              struct extra_type *target_extra,
                              const struct act_prob *act_probs)
{
  action_selection_close();
  popup_action_selection(actor_unit,
                         target_city, target_unit,
                         target_tile, target_extra,
                         act_probs);
}

/**********************************************************************//**
  Closes the action selection dialog
**************************************************************************/
void action_selection_close(void)
{
  did_not_decide = TRUE;
  popdown_diplomat_dialog();
}

/* ====================================================================== */
/* ============================ SABOTAGE DIALOG ========================= */
/* ====================================================================== */

/**********************************************************************//**
  User selected what to sabotage.
**************************************************************************/
static int sabotage_impr_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int sabotage_improvement = MAX_ID - pwidget->id;
    int diplomat_target_id = pwidget->data.cont->id0;
    int diplomat_id = pwidget->data.cont->id1;
    action_id act_id = pwidget->data.cont->value;

    fc_assert(is_more_user_input_needed);
    popdown_diplomat_dialog();

    if (sabotage_improvement == 1000) {
      sabotage_improvement = -1;
    }

    if (NULL != game_unit_by_number(diplomat_id)
        && NULL != game_city_by_number(diplomat_target_id)) {
      if (sabotage_improvement == B_LAST) {
        /* This is the untargeted version. */
        request_do_action(get_non_targeted_action_id(act_id),
                          diplomat_id, diplomat_target_id,
                          sabotage_improvement, "");
      } else if (sabotage_improvement == -1) {
        /* This is the city production version. */
        request_do_action(get_production_targeted_action_id(act_id),
                          diplomat_id, diplomat_target_id,
                          sabotage_improvement, "");
      } else {
        /* This is the targeted version. */
        request_do_action(act_id,
                          diplomat_id, diplomat_target_id,
                          sabotage_improvement, "");
      }
    }

    act_sel_done_secondary(diplomat_id);
  }

  return -1;
}

/**********************************************************************//**
  Pops-up the Spy sabotage dialog, upon return of list of
  available improvements requested by the above function.
**************************************************************************/
void popup_sabotage_dialog(struct unit *actor, struct city *pcity,
                           const struct action *paction)
{
  struct widget *pwindow = NULL, *buf = NULL , *last = NULL;
  struct container *cont;
  utf8_str *pstr;
  SDL_Rect area, area2;
  int n, w = 0, h, imp_h = 0, y;

  fc_assert_ret_msg(!diplomat_dlg, "Diplomat dialog already open");

  /* Should be set before sending request to the server. */
  fc_assert(is_more_user_input_needed);

  if (!actor) {
    act_sel_done_secondary(IDENTITY_NUMBER_ZERO);
    return;
  }

  is_unit_move_blocked = TRUE;

  diplomat_dlg = fc_calloc(1, sizeof(struct diplomat_dialog));
  diplomat_dlg->actor_unit_id = actor->id;
  diplomat_dlg->target_ids[ATK_CITY] = pcity->id;
  diplomat_dlg->pdialog = fc_calloc(1, sizeof(struct advanced_dialog));

  cont = fc_calloc(1, sizeof(struct container));
  cont->id0 = pcity->id;
  cont->id1 = actor->id; /* Spy id */
  cont->value = paction->id;

  pstr = create_utf8_from_char_fonto(_("Select Improvement to Sabotage"),
                                     FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = diplomat_dlg_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_TERRAIN_ADV_DLG_WINDOW, pwindow);
  diplomat_dlg->pdialog->end_widget_list = pwindow;

  area = pwindow->area;
  area.h = MAX(area.h, adj_size(2));

  /* ---------- */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  area.w += buf->size.w + adj_size(10);
  buf->action = exit_spy_tgt_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, buf);
  /* ---------- */

  if (action_prob_possible(actor->client.act_prob_cache[
                           get_production_targeted_action_id(
                               paction->id)])) {
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("City Production"), sabotage_impr_callback);
    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);
    set_wflag(buf, WF_FREE_DATA);
    add_to_gui_list(MAX_ID - 1000, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

    /* separator */
    buf = create_iconlabel(NULL, pwindow->dst, NULL, WF_FREE_THEME);

    add_to_gui_list(ID_SEPARATOR, buf);
    area.h += buf->next->size.h;

    diplomat_dlg->pdialog->end_active_widget_list = buf;
  }

  /* ------------------ */
  n = 0;
  city_built_iterate(pcity, pimprove) {
    if (pimprove->sabotage > 0) {
      create_active_iconlabel(buf, pwindow->dst, pstr,
             (char *) city_improvement_name_translation(pcity, pimprove),
                                      sabotage_impr_callback);
      buf->data.cont = cont;
      set_wstate(buf, FC_WS_NORMAL);

      add_to_gui_list(MAX_ID - improvement_number(pimprove), buf);

      area.w = MAX(area.w , buf->size.w);
      imp_h += buf->size.h;

      if (n > 9) {
        set_wflag(buf, WF_HIDDEN);
      }

      n++;
      /* ----------- */
    }
  } city_built_iterate_end;

  diplomat_dlg->pdialog->begin_active_widget_list = buf;

  if (n > 0
      && action_prob_possible(actor->client.act_prob_cache[
                              get_non_targeted_action_id(paction->id)])) {
    /* separator */
    buf = create_iconlabel(NULL, pwindow->dst, NULL, WF_FREE_THEME);

    add_to_gui_list(ID_SEPARATOR, buf);
    area.h += buf->next->size.h;
  /* ------------------ */
  }

  if (action_prob_possible(actor->client.act_prob_cache[
                           get_non_targeted_action_id(paction->id)])) {
    struct astring str = ASTRING_INIT;

    /* TRANS: %s is a unit name, e.g., Spy */
    astr_set(&str, _("At %s's Discretion"), unit_name_translation(actor));
    create_active_iconlabel(buf, pwindow->dst, pstr, astr_str(&str),
                            sabotage_impr_callback);
    astr_free(&str);

    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(MAX_ID - B_LAST, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
  }

  /* ----------- */

  last = buf;
  diplomat_dlg->pdialog->begin_widget_list = last;
  diplomat_dlg->pdialog->active_widget_list = diplomat_dlg->pdialog->end_active_widget_list;

  /* ---------- */
  if (n > 10) {
    imp_h = 10 * buf->size.h;

    n = create_vertical_scrollbar(diplomat_dlg->pdialog,
                                  1, 10, TRUE, TRUE);
    area.w += n;
  } else {
    n = 0;
  }
  /* ---------- */

  area.h += imp_h;

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  auto_center_on_focus_unit();
  put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h,
                           unit_tile(actor));

  w = area.w;

  /* exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  /* Production sabotage */
  buf = buf->prev;

  buf->size.x = area.x;
  buf->size.y = y = area.y + 1;
  buf->size.w = w;
  h = buf->size.h;

  area2.x = adj_size(10);
  area2.h = adj_size(2);

  buf = buf->prev;
  while (buf) {
    if (buf == diplomat_dlg->pdialog->end_active_widget_list) {
      w -= n;
    }

    buf->size.w = w;
    buf->size.x = buf->next->size.x;
    buf->size.y = y = y + buf->next->size.h;

    if (buf->id == ID_SEPARATOR) {
      FREESURFACE(buf->theme);
      buf->size.h = h;
      buf->theme = create_surf(w, h);

      area2.y = buf->size.h / 2 - 1;
      area2.w = buf->size.w - adj_size(20);

      SDL_FillSurfaceRect(buf->theme , &area2, map_rgba(buf->theme->format,
                                                        *get_theme_color(COLOR_THEME_SABOTAGEDLG_SEPARATOR)));
    }

    if (buf == last) {
      break;
    }

    if (buf == diplomat_dlg->pdialog->begin_active_widget_list) {
      /* Reset to end of scrolling area */
      y = MIN(y, diplomat_dlg->pdialog->end_active_widget_list->size.y
              + 9 * buf->size.h);
      w += n;
    }
    buf = buf->prev;
  }

  if (diplomat_dlg->pdialog->scroll) {
    setup_vertical_scrollbar_area(diplomat_dlg->pdialog->scroll,
        area.x + area.w,
        diplomat_dlg->pdialog->end_active_widget_list->size.y,
        diplomat_dlg->pdialog->begin_active_widget_list->prev->size.y
          - diplomat_dlg->pdialog->end_active_widget_list->size.y,
        TRUE);
  }

  /* -------------------- */
  /* redraw */
  redraw_group(diplomat_dlg->pdialog->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}

/* ====================================================================== */
/* ============================== INCITE DIALOG ========================= */
/* ====================================================================== */
static struct small_diplomat_dialog *incite_dlg = NULL;

/**********************************************************************//**
  User interacted with Incite Revolt dialog window.
**************************************************************************/
static int incite_dlg_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(incite_dlg->pdialog->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User confirmed incite
**************************************************************************/
static int diplomat_incite_yes_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (NULL != game_unit_by_number(incite_dlg->actor_unit_id)
        && NULL != game_city_by_number(incite_dlg->target_id)) {
      request_do_action(incite_dlg->act_id, incite_dlg->actor_unit_id,
                        incite_dlg->target_id, 0, "");
    }

    popdown_incite_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Close incite dialog.
**************************************************************************/
static int exit_incite_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_incite_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Popdown a window asking a diplomatic unit if it wishes to incite the
  given enemy city.
**************************************************************************/
void popdown_incite_dialog(void)
{
  if (incite_dlg) {
    act_sel_done_secondary(incite_dlg->actor_unit_id);

    is_unit_move_blocked = FALSE;
    popdown_window_group_dialog(incite_dlg->pdialog->begin_widget_list,
                                incite_dlg->pdialog->end_widget_list);
    FC_FREE(incite_dlg->pdialog);
    FC_FREE(incite_dlg);
    flush_dirty();
  }
}

/**********************************************************************//**
  Popup a window asking a diplomatic unit if it wishes to incite the
  given enemy city.
**************************************************************************/
void popup_incite_dialog(struct unit *actor, struct city *pcity, int cost,
                         const struct action *paction)
{
  struct widget *pwindow = NULL, *buf = NULL;
  utf8_str *pstr;
  char tBuf[255], cbuf[255];
  bool exit = FALSE;
  SDL_Rect area;

  if (incite_dlg) {
    return;
  }

  /* Should be set before sending request to the server. */
  fc_assert(is_more_user_input_needed);

  if (!actor || !unit_can_do_action(actor, paction->id)) {
    act_sel_done_secondary(actor ? actor->id : IDENTITY_NUMBER_ZERO);
    return;
  }

  is_unit_move_blocked = TRUE;

  incite_dlg = fc_calloc(1, sizeof(struct small_diplomat_dialog));
  incite_dlg->actor_unit_id = actor->id;
  incite_dlg->target_id = pcity->id;
  incite_dlg->act_id = paction->id;
  incite_dlg->pdialog = fc_calloc(1, sizeof(struct small_dialog));

  fc_snprintf(tBuf, ARRAY_SIZE(tBuf), PL_("Treasury contains %d gold.",
                                          "Treasury contains %d gold.",
                                          client_player()->economic.gold),
              client_player()->economic.gold);

  /* Window */
  pstr = create_utf8_from_char_fonto(_("Incite a Revolt!"), FONTO_ATTENTION);

  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = incite_dlg_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_INCITE_DLG_WINDOW, pwindow);
  incite_dlg->pdialog->end_widget_list = pwindow;

  area = pwindow->area;
  area.w = MAX(area.w, adj_size(8));
  area.h = MAX(area.h, adj_size(2));

  if (INCITE_IMPOSSIBLE_COST == cost) {
    /* Exit button */
    buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                           WF_WIDGET_HAS_INFO_LABEL
                           | WF_RESTORE_BACKGROUND);
    buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                  FONTO_ATTENTION);
    area.w += buf->size.w + adj_size(10);
    buf->action = exit_incite_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->key = SDLK_ESCAPE;

    add_to_gui_list(ID_INCITE_DLG_EXIT_BUTTON, buf);
    exit = TRUE;
    /* --------------- */

    fc_snprintf(cbuf, sizeof(cbuf), _("You can't incite a revolt in %s."),
                city_name_get(pcity));

    create_active_iconlabel(buf, pwindow->dst, pstr, cbuf, NULL);

    add_to_gui_list(ID_LABEL , buf);

    area.w = MAX(area.w , buf->size.w);
    area.h += buf->size.h;
    /*------------*/
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("City can't be incited!"), NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

  } else if (cost <= client_player()->economic.gold) {
    fc_snprintf(cbuf, sizeof(cbuf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Incite a revolt for %d gold?\n%s",
                    "Incite a revolt for %d gold?\n%s", cost), cost, tBuf);

    create_active_iconlabel(buf, pwindow->dst, pstr, cbuf, NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

    /*------------*/
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("Yes") , diplomat_incite_yes_callback);

    buf->data.city = pcity;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(MAX_ID - actor->id, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    /* ------- */
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("No"), exit_incite_dlg_callback);

    set_wstate(buf, FC_WS_NORMAL);
    buf->key = SDLK_ESCAPE;

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

  } else {
    /* Exit button */
    buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                           WF_WIDGET_HAS_INFO_LABEL
                           | WF_RESTORE_BACKGROUND);
    buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                  FONTO_ATTENTION);
    area.w += buf->size.w + adj_size(10);
    buf->action = exit_incite_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->key = SDLK_ESCAPE;

    add_to_gui_list(ID_INCITE_DLG_EXIT_BUTTON, buf);
    exit = TRUE;
    /* --------------- */

    fc_snprintf(cbuf, sizeof(cbuf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Inciting a revolt costs %d gold.\n%s",
                    "Inciting a revolt costs %d gold.\n%s", cost), cost, tBuf);

    create_active_iconlabel(buf, pwindow->dst, pstr, cbuf, NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

    /*------------*/
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("Traitors Demand Too Much!"), NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
  }
  incite_dlg->pdialog->begin_widget_list = buf;

  /* setup window size and start position */

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  auto_center_on_focus_unit();
  put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h,
                           pcity->tile);

  /* setup widget size and start position */
  buf = pwindow;

  if (exit) {
    /* exit button */
    buf = buf->prev;
    buf->size.x = area.x + area.w - buf->size.w - 1;
    buf->size.y = pwindow->size.y + adj_size(2);
  }

  buf = buf->prev;
  setup_vertical_widgets_position(1,
        area.x,
        area.y + 1, area.w, 0,
        incite_dlg->pdialog->begin_widget_list, buf);

  /* --------------------- */
  /* redraw */
  redraw_group(incite_dlg->pdialog->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}

/* ====================================================================== */
/* ============================ BRIBE DIALOG ========================== */
/* ====================================================================== */
static struct small_diplomat_dialog *bribe_dlg = NULL;

/**********************************************************************//**
  User interacted with bribe dialog window.
**************************************************************************/
static int bribe_dlg_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(bribe_dlg->pdialog->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User confirmed unit bribe.
**************************************************************************/
static int diplomat_bribe_unit_yes_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (NULL != game_unit_by_number(bribe_dlg->actor_unit_id)
        && NULL != game_unit_by_number(bribe_dlg->target_id)) {
      request_do_action(bribe_dlg->act_id, bribe_dlg->actor_unit_id,
                        bribe_dlg->target_id, 0, "");
    }
    popdown_bribe_dialog();
  }

  return -1;
}

/**********************************************************************//**
  User confirmed stack bribe.
**************************************************************************/
static int diplomat_bribe_stack_yes_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (NULL != game_unit_by_number(bribe_dlg->actor_unit_id)) {
      request_do_action(bribe_dlg->act_id, bribe_dlg->actor_unit_id,
                        bribe_dlg->target_id, 0, "");
    }
    popdown_bribe_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Close bribe dialog.
**************************************************************************/
static int exit_bribe_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_bribe_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Popdown a dialog asking a diplomatic unit if it wishes to bribe the
  given enemy unit.
**************************************************************************/
void popdown_bribe_dialog(void)
{
  if (bribe_dlg) {
    act_sel_done_secondary(bribe_dlg->actor_unit_id);

    is_unit_move_blocked = FALSE;
    popdown_window_group_dialog(bribe_dlg->pdialog->begin_widget_list,
                                bribe_dlg->pdialog->end_widget_list);
    FC_FREE(bribe_dlg->pdialog);
    FC_FREE(bribe_dlg);
    flush_dirty();
  }
}

/**********************************************************************//**
  Popup a dialog asking a diplomatic unit if it wishes to bribe the
  given enemy unit.
**************************************************************************/
void popup_bribe_unit_dialog(struct unit *actor, struct unit *punit, int cost,
                             const struct action *paction)
{
  struct widget *pwindow = NULL, *buf = NULL;
  utf8_str *pstr;
  char tBuf[255], cbuf[255];
  bool exit = FALSE;
  SDL_Rect area;

  if (bribe_dlg) {
    return;
  }

  /* Should be set before sending request to the server. */
  fc_assert(is_more_user_input_needed);

  if (!actor || !unit_can_do_action(actor, paction->id)) {
    act_sel_done_secondary(actor ? actor->id : IDENTITY_NUMBER_ZERO);
    return;
  }

  is_unit_move_blocked = TRUE;

  bribe_dlg = fc_calloc(1, sizeof(struct small_diplomat_dialog));
  bribe_dlg->act_id = paction->id;
  bribe_dlg->actor_unit_id = actor->id;
  bribe_dlg->target_id = punit->id;
  bribe_dlg->pdialog = fc_calloc(1, sizeof(struct small_dialog));

  fc_snprintf(tBuf, ARRAY_SIZE(tBuf), PL_("Treasury contains %d gold.",
                                          "Treasury contains %d gold.",
                                          client_player()->economic.gold),
              client_player()->economic.gold);

  /* Window */
  pstr = create_utf8_from_char_fonto(_("Bribe Enemy Unit"), FONTO_ATTENTION);

  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = bribe_dlg_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_BRIBE_DLG_WINDOW, pwindow);
  bribe_dlg->pdialog->end_widget_list = pwindow;

  area = pwindow->area;
  area.w = MAX(area.w, adj_size(8));
  area.h = MAX(area.h, adj_size(2));

  if (cost <= client_player()->economic.gold) {
    fc_snprintf(cbuf, sizeof(cbuf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Bribe unit for %d gold?\n%s",
                    "Bribe unit for %d gold?\n%s", cost), cost, tBuf);

    create_active_iconlabel(buf, pwindow->dst, pstr, cbuf, NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

    /*------------*/
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("Yes"), diplomat_bribe_unit_yes_callback);
    buf->data.unit = punit;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(MAX_ID - actor->id, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    /* ------- */
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("No"), exit_bribe_dlg_callback);

    set_wstate(buf, FC_WS_NORMAL);
    buf->key = SDLK_ESCAPE;

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

  } else {
    /* Exit button */
    buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                           WF_WIDGET_HAS_INFO_LABEL
                           | WF_RESTORE_BACKGROUND);
    buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                  FONTO_ATTENTION);
    area.w += buf->size.w + adj_size(10);
    buf->action = exit_bribe_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->key = SDLK_ESCAPE;

    add_to_gui_list(ID_BRIBE_DLG_EXIT_BUTTON, buf);
    exit = TRUE;
    /* --------------- */

    fc_snprintf(cbuf, sizeof(cbuf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Bribing the unit costs %d gold.\n%s",
                    "Bribing the unit costs %d gold.\n%s", cost), cost, tBuf);

    create_active_iconlabel(buf, pwindow->dst, pstr, cbuf, NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

    /*------------*/
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("Traitors Demand Too Much!"), NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
  }
  bribe_dlg->pdialog->begin_widget_list = buf;

  /* Setup window size and start position */

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  auto_center_on_focus_unit();
  put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h,
                           unit_tile(actor));

  /* Setup widget size and start position */
  buf = pwindow;

  if (exit) {
    /* Exit button */
    buf = buf->prev;
    buf->size.x = area.x + area.w - buf->size.w - 1;
    buf->size.y = pwindow->size.y + adj_size(2);
  }

  buf = buf->prev;
  setup_vertical_widgets_position(1,
        area.x,
        area.y + 1, area.w, 0,
        bribe_dlg->pdialog->begin_widget_list, buf);

  /* --------------------- */
  /* Redraw */
  redraw_group(bribe_dlg->pdialog->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}

/**********************************************************************//**
  Popup a dialog asking a diplomatic unit if it wishes to bribe the
  given enemy unit.
**************************************************************************/
void popup_bribe_stack_dialog(struct unit *actor, struct tile *ptile, int cost,
                              const struct action *paction)
{
  struct widget *pwindow = NULL, *buf = NULL;
  utf8_str *pstr;
  char tBuf[255], cbuf[255];
  bool exit = FALSE;
  SDL_Rect area;

  if (bribe_dlg) {
    return;
  }

  /* Should be set before sending request to the server. */
  fc_assert(is_more_user_input_needed);

  if (!actor || !unit_can_do_action(actor, paction->id)) {
    act_sel_done_secondary(actor ? actor->id : IDENTITY_NUMBER_ZERO);
    return;
  }

  is_unit_move_blocked = TRUE;

  bribe_dlg = fc_calloc(1, sizeof(struct small_diplomat_dialog));
  bribe_dlg->act_id = paction->id;
  bribe_dlg->actor_unit_id = actor->id;
  bribe_dlg->target_id = ptile->index;
  bribe_dlg->pdialog = fc_calloc(1, sizeof(struct small_dialog));

  fc_snprintf(tBuf, ARRAY_SIZE(tBuf), PL_("Treasury contains %d gold.",
                                          "Treasury contains %d gold.",
                                          client_player()->economic.gold),
              client_player()->economic.gold);

  /* Window */
  pstr = create_utf8_from_char_fonto(_("Bribe Enemy Stack"), FONTO_ATTENTION);

  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = bribe_dlg_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_BRIBE_DLG_WINDOW, pwindow);
  bribe_dlg->pdialog->end_widget_list = pwindow;

  area = pwindow->area;
  area.w = MAX(area.w, adj_size(8));
  area.h = MAX(area.h, adj_size(2));

  if (cost <= client_player()->economic.gold) {
    fc_snprintf(cbuf, sizeof(cbuf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Bribe unit stack for %d gold?\n%s",
                    "Bribe unit stack for %d gold?\n%s", cost), cost, tBuf);

    create_active_iconlabel(buf, pwindow->dst, pstr, cbuf, NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

    /*------------*/
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("Yes"), diplomat_bribe_stack_yes_callback);
    buf->data.tile = ptile;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(MAX_ID - actor->id, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    /* ------- */
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("No"), exit_bribe_dlg_callback);

    set_wstate(buf, FC_WS_NORMAL);
    buf->key = SDLK_ESCAPE;

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

  } else {
    /* Exit button */
    buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                           WF_WIDGET_HAS_INFO_LABEL
                           | WF_RESTORE_BACKGROUND);
    buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                  FONTO_ATTENTION);
    area.w += buf->size.w + adj_size(10);
    buf->action = exit_bribe_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->key = SDLK_ESCAPE;

    add_to_gui_list(ID_BRIBE_DLG_EXIT_BUTTON, buf);
    exit = TRUE;
    /* --------------- */

    fc_snprintf(cbuf, sizeof(cbuf),
                /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
                PL_("Bribing the unit stack costs %d gold.\n%s",
                    "Bribing the unit stack costs %d gold.\n%s", cost), cost, tBuf);

    create_active_iconlabel(buf, pwindow->dst, pstr, cbuf, NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;

    /*------------*/
    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("Traitors Demand Too Much!"), NULL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
  }
  bribe_dlg->pdialog->begin_widget_list = buf;

  /* Setup window size and start position */

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  auto_center_on_focus_unit();
  put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h,
                           unit_tile(actor));

  /* Setup widget size and start position */
  buf = pwindow;

  if (exit) {
    /* Exit button */
    buf = buf->prev;
    buf->size.x = area.x + area.w - buf->size.w - 1;
    buf->size.y = pwindow->size.y + adj_size(2);
  }

  buf = buf->prev;
  setup_vertical_widgets_position(1,
        area.x,
        area.y + 1, area.w, 0,
        bribe_dlg->pdialog->begin_widget_list, buf);

  /* --------------------- */
  /* Redraw */
  redraw_group(bribe_dlg->pdialog->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}
