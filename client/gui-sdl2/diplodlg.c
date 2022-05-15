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
#include "diptreaty.h"
#include "game.h"
#include "research.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "packhand.h"

/* gui-sdl2 */
#include "chatline.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"
#include "widget.h"

#include "diplodlg.h"

#define MAX_NUM_CLAUSES 64

struct diplomacy_dialog {
  struct Treaty treaty;
  struct advanced_dialog *pdialog;
  struct advanced_dialog *pwants;
  struct advanced_dialog *poffers;
};

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct diplomacy_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct diplomacy_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list *dialog_list;

static void update_diplomacy_dialog(struct diplomacy_dialog *pdialog);
static void update_acceptance_icons(struct diplomacy_dialog *pdialog);
static void update_clauses_list(struct diplomacy_dialog *pdialog);
static void remove_clause_widget_from_list(int counterpart, int giver,
                                           enum clause_type type, int value);
static void popdown_diplomacy_dialog(int counterpart);
static void popdown_diplomacy_dialogs(void);
static void popdown_sdip_dialog(void);

/**********************************************************************//**
  Initialialize diplomacy dialog system.
**************************************************************************/
void diplomacy_dialog_init(void)
{
  dialog_list = dialog_list_new();
}

/**********************************************************************//**
  Free resources allocated for diplomacy dialog system.
**************************************************************************/
void diplomacy_dialog_done(void)
{
  dialog_list_destroy(dialog_list);
}

/**********************************************************************//**
  Get diplomacy dialog between client user and other player.
**************************************************************************/
static struct diplomacy_dialog *get_diplomacy_dialog(int other_player_id)
{
  struct player *plr0 = client.conn.playing;
  struct player *plr1 = player_by_number(other_player_id);

  dialog_list_iterate(dialog_list, pdialog) {
    if ((pdialog->treaty.plr0 == plr0 && pdialog->treaty.plr1 == plr1)
        || (pdialog->treaty.plr0 == plr1 && pdialog->treaty.plr1 == plr0)) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Update a player's acceptance status of a treaty (traditionally shown
  with the thumbs-up/thumbs-down sprite).
**************************************************************************/
void handle_diplomacy_accept_treaty(int counterpart, bool I_accepted,
                                    bool other_accepted)
{
  struct diplomacy_dialog *pdialog = get_diplomacy_dialog(counterpart);

  if (!pdialog) {
    return;
  }

  pdialog->treaty.accept0 = I_accepted;
  pdialog->treaty.accept1 = other_accepted;

  update_acceptance_icons(pdialog);
}

/**********************************************************************//**
  Update the diplomacy dialog when the meeting is canceled (the dialog
  should be closed).
**************************************************************************/
void handle_diplomacy_cancel_meeting(int counterpart, int initiated_from)
{
  popdown_diplomacy_dialog(counterpart);
  flush_dirty();
}

/* ----------------------------------------------------------------------- */

/**********************************************************************//**
  User interacted with remove clause -widget.
**************************************************************************/
static int remove_clause_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct diplomacy_dialog *pdialog;

    if (!(pdialog = get_diplomacy_dialog(pwidget->data.cont->id1))) {
      pdialog = get_diplomacy_dialog(pwidget->data.cont->id0);
    }

    dsend_packet_diplomacy_remove_clause_req(&client.conn,
                                             player_number(pdialog->treaty.plr1),
                                             pwidget->data.cont->id0,
                                             (enum clause_type) ((pwidget->data.
                                             cont->value >> 16) & 0xFFFF),
                                             pwidget->data.cont->value & 0xFFFF);
  }

  return -1;
}

/**********************************************************************//**
  Update the diplomacy dialog by adding a clause.
**************************************************************************/
void handle_diplomacy_create_clause(int counterpart, int giver,
                                    enum clause_type type, int value)
{
  struct diplomacy_dialog *pdialog = get_diplomacy_dialog(counterpart);

  if (!pdialog) {
    return;
  }

  clause_list_iterate(pdialog->treaty.clauses, pclause) {
    remove_clause_widget_from_list(player_number(pdialog->treaty.plr1),
                                   player_number(pclause->from),
                                   pclause->type,
                                   pclause->value);
  } clause_list_iterate_end;

  add_clause(&pdialog->treaty, player_by_number(giver), type, value);

  update_clauses_list(pdialog);
  update_acceptance_icons(pdialog);
}

/**********************************************************************//**
  Update the diplomacy dialog by removing a clause.
**************************************************************************/
void handle_diplomacy_remove_clause(int counterpart, int giver,
                                    enum clause_type type, int value)
{
  struct diplomacy_dialog *pdialog = get_diplomacy_dialog(counterpart);

  if (!pdialog) {
    return;
  }

  clause_list_iterate(pdialog->treaty.clauses, pclause) {
    remove_clause_widget_from_list(player_number(pdialog->treaty.plr1),
                                   player_number(pclause->from),
                                   pclause->type,
                                   pclause->value);
  } clause_list_iterate_end;

  remove_clause(&pdialog->treaty, player_by_number(giver), type, value);

  update_clauses_list(pdialog);
  update_acceptance_icons(pdialog);
}

/* ================================================================= */

/**********************************************************************//**
  User interacted with cancel meeting -widget.
**************************************************************************/
static int cancel_meeting_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    dsend_packet_diplomacy_cancel_meeting_req(&client.conn,
                                              pwidget->data.cont->id1);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with accept treaty -widget.
**************************************************************************/
static int accept_treaty_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    dsend_packet_diplomacy_accept_treaty_req(&client.conn,
                                             pwidget->data.cont->id1);
  }

  return -1;
}

/* ------------------------------------------------------------------------ */

/**********************************************************************//**
  User interacted with pact selection -widget.
**************************************************************************/
static int pact_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int clause_type;
    struct diplomacy_dialog *pdialog;

    if (!(pdialog = get_diplomacy_dialog(pwidget->data.cont->id1))) {
      pdialog = get_diplomacy_dialog(pwidget->data.cont->id0);
    }

    switch (MAX_ID - pwidget->id) {
    case 2:
      clause_type = CLAUSE_CEASEFIRE;
    break;
    case 1:
      clause_type = CLAUSE_PEACE;
      break;
    default:
      clause_type = CLAUSE_ALLIANCE;
      break;
    }

    dsend_packet_diplomacy_create_clause_req(&client.conn,
                                             player_number(pdialog->treaty.plr1),
                                             pwidget->data.cont->id0,
                                             clause_type, 0);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with shared vision -widget.
**************************************************************************/
static int vision_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct diplomacy_dialog *pdialog;

    if (!(pdialog = get_diplomacy_dialog(pwidget->data.cont->id1))) {
      pdialog = get_diplomacy_dialog(pwidget->data.cont->id0);
    }
  
    dsend_packet_diplomacy_create_clause_req(&client.conn,
                                             player_number(pdialog->treaty.plr1),
                                             pwidget->data.cont->id0,
                                             CLAUSE_VISION, 0);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with embassy -widget.
**************************************************************************/
static int embassy_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct diplomacy_dialog *pdialog;

    if (!(pdialog = get_diplomacy_dialog(pwidget->data.cont->id1))) {
      pdialog = get_diplomacy_dialog(pwidget->data.cont->id0);
    }

    dsend_packet_diplomacy_create_clause_req(&client.conn,
                                             player_number(pdialog->treaty.plr1),
                                             pwidget->data.cont->id0,
                                             CLAUSE_EMBASSY, 0);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with map -widget.
**************************************************************************/
static int maps_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int clause_type;
    struct diplomacy_dialog *pdialog;

    if (!(pdialog = get_diplomacy_dialog(pwidget->data.cont->id1))) {
      pdialog = get_diplomacy_dialog(pwidget->data.cont->id0);
    }

    switch (MAX_ID - pwidget->id) {
    case 1:
      clause_type = CLAUSE_MAP;
      break;
    default:
      clause_type = CLAUSE_SEAMAP;
      break;
    }

    dsend_packet_diplomacy_create_clause_req(&client.conn,
                                             player_number(pdialog->treaty.plr1),
                                             pwidget->data.cont->id0,
                                             clause_type, 0);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with tech -widget.
**************************************************************************/
static int techs_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct diplomacy_dialog *pdialog;

    if (!(pdialog = get_diplomacy_dialog(pwidget->data.cont->id1))) {
      pdialog = get_diplomacy_dialog(pwidget->data.cont->id0);
    }

    dsend_packet_diplomacy_create_clause_req(&client.conn,
                                             player_number(pdialog->treaty.plr1),
                                             pwidget->data.cont->id0,
                                             CLAUSE_ADVANCE,
                                             (MAX_ID - pwidget->id));
  }

  return -1;
}

/**********************************************************************//**
  User interacted with gold -widget.
**************************************************************************/
static int gold_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int amount;
    struct diplomacy_dialog *pdialog;

    if (!(pdialog = get_diplomacy_dialog(pwidget->data.cont->id1))) {
      pdialog = get_diplomacy_dialog(pwidget->data.cont->id0);
    }

    if (pwidget->string_utf8->text != NULL) {
      sscanf(pwidget->string_utf8->text, "%d", &amount);

      if (amount > pwidget->data.cont->value) {
        /* max player gold */
        amount = pwidget->data.cont->value;
      }

    } else {
      amount = 0;
    }

    if (amount > 0) {
      dsend_packet_diplomacy_create_clause_req(&client.conn,
                                               player_number(pdialog->treaty.plr1),
                                               pwidget->data.cont->id0,
                                               CLAUSE_GOLD, amount);
      
    } else {
      output_window_append(ftc_client,
                           _("Invalid amount of gold specified."));
    }

    if (amount || pwidget->string_utf8->text == NULL) {
      copy_chars_to_utf8_str(pwidget->string_utf8, "0");
      widget_redraw(pwidget);
      widget_flush(pwidget);
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with city -widget.
**************************************************************************/
static int cities_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct diplomacy_dialog *pdialog;

    if (!(pdialog = get_diplomacy_dialog(pwidget->data.cont->id1))) {
      pdialog = get_diplomacy_dialog(pwidget->data.cont->id0);
    }

    dsend_packet_diplomacy_create_clause_req(&client.conn,
                                             player_number(pdialog->treaty.plr1),
                                             pwidget->data.cont->id0,
                                             CLAUSE_CITY,
                                             (MAX_ID - pwidget->id));
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Diplomacy meeting -window.
**************************************************************************/
static int dipomatic_window_callback(struct widget *pwindow)
{
  return -1;
}

/**********************************************************************//**
  Create treaty widgets. Pact (that will always be both ways) related
  widgets are created only when pplayer0 is client user.
**************************************************************************/
static struct advanced_dialog *popup_diplomatic_objects(struct player *pplayer0,
                                                        struct player *pplayer1,
                                                        struct widget *main_window,
                                                        bool L_R)
{
  struct advanced_dialog *dlg = fc_calloc(1, sizeof(struct advanced_dialog));
  struct container *cont = fc_calloc(1, sizeof(struct container));
  int width, height, count = 0, scroll_w = 0;
  char cbuf[128];
  struct widget *buf = NULL, *pwindow;
  utf8_str *pstr;
  int window_x = 0, window_y = 0;
  SDL_Rect area;
  enum diplstate_type type
    = player_diplstate_get(pplayer0, pplayer1)->type;

  cont->id0 = player_number(pplayer0);
  cont->id1 = player_number(pplayer1);

  pstr = create_utf8_from_char(nation_adjective_for_player(pplayer0), adj_font(12));
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, WF_FREE_DATA);

  pwindow->action = dipomatic_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  dlg->end_widget_list = pwindow;
  pwindow->data.cont = cont;
  add_to_gui_list(ID_WINDOW, pwindow);

  area = pwindow->area;

  /* ============================================================= */
  width = 0;
  height = 0;

  /* Pacts. */
  if (pplayer0 == client.conn.playing && DS_ALLIANCE != type) {
    buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                       _("Pacts"), adj_font(12), WF_RESTORE_BACKGROUND);
    buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_HEADING_TEXT);
    width = buf->size.w;
    height = buf->size.h;
    add_to_gui_list(ID_LABEL, buf);
    count++;

    if (type != DS_CEASEFIRE && type != DS_TEAM) {
      fc_snprintf(cbuf, sizeof(cbuf), "  %s", Q_("?diplomatic_state:Cease-fire"));
      buf = create_iconlabel_from_chars(NULL, pwindow->dst,
        cbuf, adj_font(12), (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
      width = MAX(width, buf->size.w);
      height = MAX(height, buf->size.h);
      buf->action = pact_callback;
      buf->data.cont = cont;
      set_wstate(buf, FC_WS_NORMAL);
      add_to_gui_list(MAX_ID - 2, buf);
      count++;
    }

    if (type != DS_PEACE && type != DS_TEAM) {
      fc_snprintf(cbuf, sizeof(cbuf), "  %s", Q_("?diplomatic_state:Peace"));

      buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                         cbuf, adj_font(12),
                                         (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
      width = MAX(width, buf->size.w);
      height = MAX(height, buf->size.h);
      buf->action = pact_callback;
      buf->data.cont = cont;
      set_wstate(buf, FC_WS_NORMAL);
      add_to_gui_list(MAX_ID - 1, buf);
      count++;
    }

    if (pplayer_can_make_treaty(pplayer0, pplayer1, DS_ALLIANCE)) {
      fc_snprintf(cbuf, sizeof(cbuf), "  %s", Q_("?diplomatic_state:Alliance"));

      buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                         cbuf, adj_font(12),
                                         (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
      width = MAX(width, buf->size.w);
      height = MAX(height, buf->size.h);
      buf->action = pact_callback;
      buf->data.cont = cont;
      set_wstate(buf, FC_WS_NORMAL);
      add_to_gui_list(MAX_ID, buf);
      count++;
    }
  }

  /* ---------------------------- */
  if (!gives_shared_vision(pplayer0, pplayer1)) {
    buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                       _("Give shared vision"), adj_font(12),
                         (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
    width = MAX(width, buf->size.w);
    height = MAX(height, buf->size.h);
    buf->action = vision_callback;
    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);
    add_to_gui_list(ID_LABEL, buf);
    count++;

    /* ---------------------------- */
    /* you can't give maps if you give shared vision */
    buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                       _("Maps"), adj_font(12), WF_RESTORE_BACKGROUND);
    buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_HEADING_TEXT);
    width = MAX(width, buf->size.w);
    height = MAX(height, buf->size.h);
    add_to_gui_list(ID_LABEL, buf);
    count++;

    /* ----- */
    fc_snprintf(cbuf, sizeof(cbuf), "  %s", _("World map"));

    buf = create_iconlabel_from_chars(NULL, pwindow->dst,
        cbuf, adj_font(12), (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
    width = MAX(width, buf->size.w);
    height = MAX(height, buf->size.h);
    buf->action = maps_callback;
    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);
    add_to_gui_list(MAX_ID - 1, buf);
    count++;

    /* ----- */
    fc_snprintf(cbuf, sizeof(cbuf), "  %s", _("Sea map"));

    buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                      cbuf, adj_font(12),
                                      (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
    width = MAX(width, buf->size.w);
    height = MAX(height, buf->size.h);
    buf->action = maps_callback;
    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);
    add_to_gui_list(MAX_ID, buf);
    count++;
  }

  /* Don't take in account the embassy effects. */
  if (!player_has_real_embassy(pplayer1, pplayer0)) {
    buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                      _("Give embassy"), adj_font(12),
                                      (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
    width = MAX(width, buf->size.w);
    height = MAX(height, buf->size.h);
    buf->action = embassy_callback;
    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);
    add_to_gui_list(ID_LABEL, buf);
    count++;
  }

  /* ---------------------------- */
  if (game.info.trading_gold && pplayer0->economic.gold > 0) {
    cont->value = pplayer0->economic.gold;

    fc_snprintf(cbuf, sizeof(cbuf), _("Gold(max %d)"), pplayer0->economic.gold);
    buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                      cbuf, adj_font(12), WF_RESTORE_BACKGROUND);
    buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_HEADING_TEXT);
    width = MAX(width, buf->size.w);
    height = MAX(height, buf->size.h);
    add_to_gui_list(ID_LABEL, buf);
    count++;

    buf = create_edit(NULL, pwindow->dst,
                      create_utf8_from_char("0", adj_font(10)), 0,
                      (WF_RESTORE_BACKGROUND|WF_FREE_STRING));
    buf->data.cont = cont;
    buf->action = gold_callback;
    set_wstate(buf, FC_WS_NORMAL);
    width = MAX(width, buf->size.w);
    height = MAX(height, buf->size.h);
    add_to_gui_list(ID_LABEL, buf);
    count++;
  }
  /* ---------------------------- */

  /* Trading: advances */
  if (game.info.trading_tech) {
    const struct research *presearch0 = research_get(pplayer0);
    const struct research *presearch1 = research_get(pplayer1);
    int flag = A_NONE;

    advance_index_iterate(A_FIRST, i) {
      if (research_invention_state(presearch0, i) == TECH_KNOWN
          && research_invention_gettable(presearch1, i,
                                         game.info.tech_trade_allow_holes)
          && (research_invention_state(presearch1, i) == TECH_UNKNOWN
              || research_invention_state(presearch1, i)
                 == TECH_PREREQS_KNOWN)) {

        buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                          _("Advances"), adj_font(12),
                                          WF_RESTORE_BACKGROUND);
        buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_HEADING_TEXT);
        width = MAX(width, buf->size.w);
        height = MAX(height, buf->size.h);
        add_to_gui_list(ID_LABEL, buf);
        count++;

        fc_snprintf(cbuf, sizeof(cbuf), "  %s",
                    advance_name_translation(advance_by_number(i)));

        buf = create_iconlabel_from_chars(NULL, pwindow->dst, cbuf, adj_font(12),
                                          (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
        buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
        width = MAX(width, buf->size.w);
        height = MAX(height, buf->size.h);
        buf->action = techs_callback;
        set_wstate(buf, FC_WS_NORMAL);
        buf->data.cont = cont;
        add_to_gui_list(MAX_ID - i, buf);
        count++;
        flag = ++i;
        break;
      }
    } advance_index_iterate_end;

    if (flag > A_NONE) {
      advance_index_iterate(flag, i) {
        if (research_invention_state(presearch0, i) == TECH_KNOWN
            && research_invention_gettable(presearch1, i,
                                           game.info.tech_trade_allow_holes)
            && (research_invention_state(presearch1, i) == TECH_UNKNOWN
                || research_invention_state(presearch1, i)
                   == TECH_PREREQS_KNOWN)) {

          fc_snprintf(cbuf, sizeof(cbuf), "  %s",
                      advance_name_translation(advance_by_number(i)));

          buf = create_iconlabel_from_chars(NULL, pwindow->dst, cbuf, adj_font(12),
                              (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
          buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
          width = MAX(width, buf->size.w);
          height = MAX(height, buf->size.h);
          buf->action = techs_callback;
          set_wstate(buf, FC_WS_NORMAL);
          buf->data.cont = cont;
          add_to_gui_list(MAX_ID - i, buf);
          count++;
        }
      }
    } advance_index_iterate_end;
  }  /* Advances */

  /* Trading: cities */
  /****************************************************************
  Creates a sorted list of pplayer0's cities, excluding the capital and
  any cities not visible to pplayer1.  This means that you can only trade
  cities visible to requesting player.

                              - Kris Bubendorfer
  *****************************************************************/
  if (game.info.trading_city) {
    int i = 0, j = 0, n = city_list_size(pplayer0->cities);
    struct city **city_list_ptrs;

    if (n > 0) {
      city_list_ptrs = fc_calloc(1, sizeof(struct city *) * n);
      city_list_iterate(pplayer0->cities, pcity) {
        if (!is_capital(pcity)) {
          city_list_ptrs[i] = pcity;
          i++;
        }
      } city_list_iterate_end;
    } else {
      city_list_ptrs = NULL;
    }

    if (i > 0) {
      buf = create_iconlabel_from_chars(NULL, pwindow->dst,
                                         _("Cities"), adj_font(12),
                                         WF_RESTORE_BACKGROUND);
      buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_HEADING_TEXT);
      buf->string_utf8->style &= ~SF_CENTER;
      width = MAX(width, buf->size.w);
      height = MAX(height, buf->size.h);
      add_to_gui_list(ID_LABEL, buf);
      count++;

      qsort(city_list_ptrs, i, sizeof(struct city *), city_name_compare);

      for (j = 0; j < i; j++) {
        fc_snprintf(cbuf, sizeof(cbuf), "  %s", city_name_get(city_list_ptrs[j]));

        buf = create_iconlabel_from_chars(NULL, pwindow->dst, cbuf, adj_font(12),
                            (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
        buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
        width = MAX(width, buf->size.w);
        height = MAX(height, buf->size.h);
        buf->data.cont = cont;
        buf->action = cities_callback;
        set_wstate(buf, FC_WS_NORMAL);
        /* MAX_ID is unigned short type range and city id must be in this range */
        fc_assert(city_list_ptrs[j]->id <= MAX_ID);
        add_to_gui_list(MAX_ID - city_list_ptrs[j]->id, buf);
        count++;
      }
    }
    FC_FREE(city_list_ptrs);
  } /* Cities */

  dlg->begin_widget_list = buf;
  dlg->begin_active_widget_list = dlg->begin_widget_list;
  dlg->end_active_widget_list = dlg->end_widget_list->prev;
  dlg->scroll = NULL;

  area.h = (main_window_height() - adj_size(100) - (pwindow->size.h - pwindow->area.h));

  if (area.h < (count * height)) {
    dlg->active_widget_list = dlg->end_active_widget_list;
    count = area.h / height;
    scroll_w = create_vertical_scrollbar(dlg, 1, count, TRUE, TRUE);
    buf = pwindow;
    /* hide not seen widgets */
    do {
      buf = buf->prev;
      if (count) {
        count--;
      } else {
        set_wflag(buf, WF_HIDDEN);
      }
    } while (buf != dlg->begin_active_widget_list);
  }

  area.w = MAX(width + adj_size(4) + scroll_w, adj_size(150) - (pwindow->size.w - pwindow->area.w));

  resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  if (L_R) {
    window_x = main_window->dst->dest_rect.x + main_window->size.w + adj_size(20);
  } else {
    window_x = main_window->dst->dest_rect.x - adj_size(20) - pwindow->size.w;
  }
  window_y = (main_window_height() - pwindow->size.h) / 2;

  widget_set_position(pwindow, window_x, window_y);

  setup_vertical_widgets_position(1,
                                  area.x + adj_size(2), area.y + 1,
                                  width, height, dlg->begin_active_widget_list,
                                  dlg->end_active_widget_list);

  if (dlg->scroll) {
    setup_vertical_scrollbar_area(dlg->scroll,
                                  area.x + area.w,
                                  area.y,
                                  area.h, TRUE);
  }

  return dlg;
}

/**********************************************************************//**
  Open new diplomacy dialog between players.
**************************************************************************/
static struct diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
                                                        struct player *plr1)
{
  struct diplomacy_dialog *pdialog = fc_calloc(1, sizeof(struct diplomacy_dialog));

  init_treaty(&pdialog->treaty, plr0, plr1);

  pdialog->pdialog = fc_calloc(1, sizeof(struct advanced_dialog));

  dialog_list_prepend(dialog_list, pdialog);

  return pdialog;
}

/**********************************************************************//**
  Update diplomacy dialog information.
**************************************************************************/
static void update_diplomacy_dialog(struct diplomacy_dialog *pdialog)
{
  SDL_Color bg_color = {255, 255, 255, 136};
  struct player *pplayer0, *pplayer1;
  struct container *cont = fc_calloc(1, sizeof(struct container));
  char cbuf[128];
  struct widget *buf = NULL, *pwindow;
  utf8_str *pstr;
  SDL_Rect dst;
  SDL_Rect area;

  if (pdialog) {
    /* delete old content */
    if (pdialog->pdialog->end_widget_list) {
      popdown_window_group_dialog(pdialog->poffers->begin_widget_list,
                                  pdialog->poffers->end_widget_list);
      FC_FREE(pdialog->poffers->scroll);
      FC_FREE(pdialog->poffers);

      popdown_window_group_dialog(pdialog->pwants->begin_widget_list,
                                  pdialog->pwants->end_widget_list);
      FC_FREE(pdialog->pwants->scroll);
      FC_FREE(pdialog->pwants);

      popdown_window_group_dialog(pdialog->pdialog->begin_widget_list,
                                  pdialog->pdialog->end_widget_list);
    }

    pplayer0 = pdialog->treaty.plr0;
    pplayer1 = pdialog->treaty.plr1;

    cont->id0 = player_number(pplayer0);
    cont->id1 = player_number(pplayer1);

    fc_snprintf(cbuf, sizeof(cbuf), _("Diplomacy meeting"));

    pstr = create_utf8_from_char(cbuf, adj_font(12));
    pstr->style |= TTF_STYLE_BOLD;

    pwindow = create_window_skeleton(NULL, pstr, 0);

    pwindow->action = dipomatic_window_callback;
    set_wstate(pwindow, FC_WS_NORMAL);
    pwindow->data.cont = cont;
    pdialog->pdialog->end_widget_list = pwindow;

    add_to_gui_list(ID_WINDOW, pwindow);

    /* ============================================================= */

    pstr = create_utf8_from_char(nation_adjective_for_player(pplayer0), adj_font(12));
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pstr->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);

    buf = create_iconlabel(create_icon_from_theme(current_theme->cancel_pact_icon, 0),
                           pwindow->dst, pstr,
                           (WF_ICON_ABOVE_TEXT|WF_FREE_PRIVATE_DATA|WF_FREE_THEME|
                            WF_RESTORE_BACKGROUND));

    buf->private_data.cbox = fc_calloc(1, sizeof(struct checkbox));
    buf->private_data.cbox->state = FALSE;
    buf->private_data.cbox->true_theme = current_theme->ok_pact_icon;
    buf->private_data.cbox->false_theme = current_theme->cancel_pact_icon;

    add_to_gui_list(ID_ICON, buf);

    pstr = create_utf8_from_char(nation_adjective_for_player(pplayer1), adj_font(12));
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pstr->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);

    buf = create_iconlabel(create_icon_from_theme(current_theme->cancel_pact_icon, 0),
                           pwindow->dst, pstr,
                           (WF_ICON_ABOVE_TEXT|WF_FREE_PRIVATE_DATA|WF_FREE_THEME|
                            WF_RESTORE_BACKGROUND));
    buf->private_data.cbox = fc_calloc(1, sizeof(struct checkbox));
    buf->private_data.cbox->state = FALSE;
    buf->private_data.cbox->true_theme = current_theme->ok_pact_icon;
    buf->private_data.cbox->false_theme = current_theme->cancel_pact_icon;
    add_to_gui_list(ID_ICON, buf);
    /* ============================================================= */

    buf = create_themeicon(current_theme->cancel_pact_icon, pwindow->dst,
                           WF_WIDGET_HAS_INFO_LABEL
                           | WF_RESTORE_BACKGROUND);
    buf->info_label = create_utf8_from_char(_("Cancel meeting"),
                                             adj_font(12));
    buf->action = cancel_meeting_callback;
    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(ID_ICON, buf);

    buf = create_themeicon(current_theme->ok_pact_icon, pwindow->dst,
                           WF_FREE_DATA | WF_WIDGET_HAS_INFO_LABEL
                           |WF_RESTORE_BACKGROUND);
    buf->info_label = create_utf8_from_char(_("Accept treaty"),
                                            adj_font(12));
    buf->action = accept_treaty_callback;
    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(ID_ICON, buf);
    /* ============================================================= */

    pdialog->pdialog->begin_widget_list = buf;

    create_vertical_scrollbar(pdialog->pdialog, 1, 7, TRUE, TRUE);
    hide_scrollbar(pdialog->pdialog->scroll);

    /* ============================================================= */

    resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                  adj_size(250), adj_size(300));

    area = pwindow->area;

    widget_set_position(pwindow,
                        (main_window_width() - pwindow->size.w) / 2,
                        (main_window_height() - pwindow->size.h) / 2);

    buf = pwindow->prev;
    buf->size.x = area.x + adj_size(17);
    buf->size.y = area.y + adj_size(6);

    dst.y = area.y + adj_size(6) + buf->size.h + adj_size(10);
    dst.x = adj_size(10);
    dst.w = area.w - adj_size(14);

    buf = buf->prev;
    buf->size.x = area.x + area.w - buf->size.w - adj_size(20);
    buf->size.y = area.y + adj_size(6);

    buf = buf->prev;
    buf->size.x = area.x + (area.w - (2 * buf->size.w + adj_size(40))) / 2;
    buf->size.y = area.y + area.h - buf->size.w - adj_size(17);

    buf = buf->prev;
    buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(40);
    buf->size.y = area.y + area.h - buf->size.w - adj_size(17);

    dst.h = area.h - buf->size.w - adj_size(3) - dst.y;
    /* ============================================================= */

    fill_rect_alpha(pwindow->theme, &dst, &bg_color);

    /* ============================================================= */
    setup_vertical_scrollbar_area(pdialog->pdialog->scroll,
                                  area.x + dst.x + dst.w,
                                  dst.y,
                                  dst.h, TRUE);
    /* ============================================================= */
    pdialog->poffers = popup_diplomatic_objects(pplayer0, pplayer1, pwindow,
                                                FALSE);

    pdialog->pwants = popup_diplomatic_objects(pplayer1, pplayer0, pwindow,
                                               TRUE);
    /* ============================================================= */
    /* redraw */
    redraw_group(pdialog->pdialog->begin_widget_list, pwindow, 0);
    widget_mark_dirty(pwindow);

    redraw_group(pdialog->poffers->begin_widget_list,
                 pdialog->poffers->end_widget_list, 0);
    widget_mark_dirty(pdialog->poffers->end_widget_list);

    redraw_group(pdialog->pwants->begin_widget_list,
                 pdialog->pwants->end_widget_list, 0);
    widget_mark_dirty(pdialog->pwants->end_widget_list);

    flush_dirty();
  }
}

/**********************************************************************//**
  Update treaty acceptance icons (accepted / rejected)
**************************************************************************/
static void update_acceptance_icons(struct diplomacy_dialog *pdialog)
{
  struct widget *label;
  SDL_Surface *thm;
  SDL_Rect src = {0, 0, 0, 0};

  /* updates your own acceptance status */
  label = pdialog->pdialog->end_widget_list->prev;

  label->private_data.cbox->state = pdialog->treaty.accept0;
  if (label->private_data.cbox->state) {
    thm = label->private_data.cbox->true_theme;
  } else {
    thm = label->private_data.cbox->false_theme;
  }

  src.w = thm->w / 4;
  src.h = thm->h;

  alphablit(thm, &src, label->theme, NULL, 255);
  SDL_SetSurfaceAlphaMod(thm, 255);

  widget_redraw(label);
  widget_flush(label);

  /* updates other player's acceptance status */
  label = pdialog->pdialog->end_widget_list->prev->prev;

  label->private_data.cbox->state = pdialog->treaty.accept1;
  if (label->private_data.cbox->state) {
    thm = label->private_data.cbox->true_theme;
  } else {
    thm = label->private_data.cbox->false_theme;
  }

  src.w = thm->w / 4;
  src.h = thm->h;

  alphablit(thm, &src, label->theme, NULL, 255);

  widget_redraw(label);
  widget_flush(label);
}

/**********************************************************************//**
  Refresh diplomacy dialog when list of clauses has been changed.
**************************************************************************/
static void update_clauses_list(struct diplomacy_dialog *pdialog)
{
  utf8_str *pstr;
  struct widget *pbuf, *pwindow = pdialog->pdialog->end_widget_list;
  char cbuf[128];
  bool redraw_all, scroll = (pdialog->pdialog->active_widget_list != NULL);
  int len = pdialog->pdialog->scroll->up_left_button->size.w;

  clause_list_iterate(pdialog->treaty.clauses, pclause) {
    client_diplomacy_clause_string(cbuf, sizeof(cbuf), pclause);

    pstr = create_utf8_from_char(cbuf, adj_font(12));
    pbuf = create_iconlabel(NULL, pwindow->dst, pstr,
     (WF_FREE_DATA|WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_RESTORE_BACKGROUND));

    if (pclause->from != client.conn.playing) {
       pbuf->string_utf8->style |= SF_CENTER_RIGHT;
    }

    pbuf->data.cont = fc_calloc(1, sizeof(struct container));
    pbuf->data.cont->id0 = player_number(pclause->from);
    pbuf->data.cont->id1 = player_number(pdialog->treaty.plr1);
    pbuf->data.cont->value = ((int)pclause->type << 16) + pclause->value;

    pbuf->action = remove_clause_callback;
    set_wstate(pbuf, FC_WS_NORMAL);

    pbuf->size.w = pwindow->size.w - adj_size(24) - (scroll ? len : 0);

    redraw_all = add_widget_to_vertical_scroll_widget_list(pdialog->pdialog,
                  pbuf, pdialog->pdialog->begin_widget_list,
                  FALSE,
                  pwindow->size.x + adj_size(12),
                  pdialog->pdialog->scroll->up_left_button->size.y + adj_size(2));

    if (!scroll && (pdialog->pdialog->active_widget_list != NULL)) {
      /* -> the scrollbar has been activated */
      pbuf = pdialog->pdialog->end_active_widget_list->next;
      do {
        pbuf = pbuf->prev;
        pbuf->size.w -= len;
        /* we need to save a new background because the width has changed */
        FREESURFACE(pbuf->gfx);
      } while (pbuf != pdialog->pdialog->begin_active_widget_list);
      scroll = TRUE;
    }

    /* redraw */
    if (redraw_all) {
      redraw_group(pdialog->pdialog->begin_widget_list, pwindow, 0);
      widget_mark_dirty(pwindow);
    } else {
      widget_redraw(pbuf);
      widget_mark_dirty(pbuf);
    }
  } clause_list_iterate_end;

  flush_dirty();
}

/**********************************************************************//**
  Remove widget related to clause from list of widgets.
**************************************************************************/
static void remove_clause_widget_from_list(int counterpart, int giver,
                                           enum clause_type type, int value)
{
  struct widget *buf;
  SDL_Rect src = {0, 0, 0, 0};
  bool scroll = TRUE;
  struct diplomacy_dialog *pdialog = get_diplomacy_dialog(counterpart);

  /* find widget with clause */
  buf = pdialog->pdialog->end_active_widget_list->next;

  do {
    buf = buf->prev;
  } while (!((buf->data.cont->id0 == giver)
             && (((buf->data.cont->value >> 16) & 0xFFFF) == (int)type)
             && ((buf->data.cont->value & 0xFFFF) == value))
           && (buf != pdialog->pdialog->begin_active_widget_list));

  if (!(buf->data.cont->id0 == giver
        && ((buf->data.cont->value >> 16) & 0xFFFF) == (int)type
        && (buf->data.cont->value & 0xFFFF) == value)) {
     return;
  }

  scroll = (pdialog->pdialog->active_widget_list != NULL);
  del_widget_from_vertical_scroll_widget_list(pdialog->pdialog, buf);

  if (scroll && (pdialog->pdialog->active_widget_list == NULL)) {
    /* -> the scrollbar has been deactivated */
    int len = pdialog->pdialog->scroll->up_left_button->size.w;

    buf = pdialog->pdialog->end_active_widget_list->next;
    do {
      buf = buf->prev;
      widget_undraw(buf);
      buf->size.w += len;
      /* we need to save a new background because the width has changed */
      FREESURFACE(buf->gfx);
    } while (buf != pdialog->pdialog->begin_active_widget_list);

    scroll = FALSE;
  }

  /* update state icons */
  buf = pdialog->pdialog->end_widget_list->prev;

  if (buf->private_data.cbox->state) {
    buf->private_data.cbox->state = FALSE;
    src.w = buf->private_data.cbox->false_theme->w / 4;
    src.h = buf->private_data.cbox->false_theme->h;

    alphablit(buf->private_data.cbox->false_theme, &src, buf->theme, NULL, 255);
  }
}

/**********************************************************************//**
  Handle the start of a diplomacy meeting - usually by popping up a
  diplomacy dialog.
**************************************************************************/
void handle_diplomacy_init_meeting(int counterpart, int initiated_from)
{
  struct diplomacy_dialog *pdialog;

  if (!can_client_issue_orders()) {
    return;
  }

  if (!is_human(client.conn.playing)) {
    return; /* Don't show if we are not under human control. */
  }

  if (!(pdialog = get_diplomacy_dialog(counterpart))) {
    pdialog = create_diplomacy_dialog(client.conn.playing,
                                      player_by_number(counterpart));
  } else {
    /* bring existing dialog to front */
    select_window_group_dialog(pdialog->pdialog->begin_widget_list,
                               pdialog->pdialog->end_widget_list);
  }

  update_diplomacy_dialog(pdialog);
}

/**********************************************************************//**
  Close diplomacy dialog between client user and given counterpart.
**************************************************************************/
static void popdown_diplomacy_dialog(int counterpart)
{
  struct diplomacy_dialog *pdialog = get_diplomacy_dialog(counterpart);

  if (pdialog) {
    popdown_window_group_dialog(pdialog->poffers->begin_widget_list,
                                pdialog->poffers->end_widget_list);
    FC_FREE(pdialog->poffers->scroll);
    FC_FREE(pdialog->poffers);

    popdown_window_group_dialog(pdialog->pwants->begin_widget_list,
                                pdialog->pwants->end_widget_list);
    FC_FREE(pdialog->pwants->scroll);
    FC_FREE(pdialog->pwants);

    popdown_window_group_dialog(pdialog->pdialog->begin_widget_list,
                                pdialog->pdialog->end_widget_list);

    dialog_list_remove(dialog_list, pdialog);

    FC_FREE(pdialog->pdialog->scroll);
    FC_FREE(pdialog->pdialog);  
    FC_FREE(pdialog);
  }
}

/**********************************************************************//**
  Popdown all diplomacy dialogs
**************************************************************************/
static void popdown_diplomacy_dialogs(void)
{
  dialog_list_iterate(dialog_list, pdialog) {
    popdown_diplomacy_dialog(player_number(pdialog->treaty.plr1));
  } dialog_list_iterate_end;
}

/**********************************************************************//**
  Close all open diplomacy dialogs, for when client disconnects from game.
**************************************************************************/
void close_all_diplomacy_dialogs(void)
{
  popdown_sdip_dialog();
  popdown_diplomacy_dialogs();
}

/* ================================================================= */
/* ========================== Small Diplomat Dialog ================ */
/* ================================================================= */
static struct small_dialog *spy_dlg = NULL;

/**********************************************************************//**
  Close small diplomacy dialog.
**************************************************************************/
static void popdown_sdip_dialog(void)
{
  if (spy_dlg) {
    popdown_window_group_dialog(spy_dlg->begin_widget_list,
                                spy_dlg->end_widget_list);
    FC_FREE(spy_dlg);
  }
}

/**********************************************************************//**
  User interacted with small diplomacy window.
**************************************************************************/
static int sdip_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(spy_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with withdraw vision -widget.
**************************************************************************/
static int withdraw_vision_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_sdip_dialog();

    dsend_packet_diplomacy_cancel_pact(&client.conn,
                                       player_number(pwidget->data.player),
                                       CLAUSE_VISION);

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with cancel pact -widget.
**************************************************************************/
static int cancel_pact_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_sdip_dialog();

    dsend_packet_diplomacy_cancel_pact(&client.conn,
                                       player_number(pwidget->data.player),
                                       CLAUSE_CEASEFIRE);

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with call meeting -widget.
**************************************************************************/
static int call_meeting_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_sdip_dialog();

    if (can_meet_with_player(pwidget->data.player)) {
      dsend_packet_diplomacy_init_meeting_req(&client.conn,
                                              player_number
                                              (pwidget->data.player));
    }

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with cancel small diplomacy dialog -widget.
**************************************************************************/
static int cancel_sdip_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_sdip_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Open war declaring dialog after some incident caused by pplayer.
**************************************************************************/
static void popup_war_dialog(struct player *pplayer)
{
  char cbuf[128];
  struct widget *buf = NULL, *pwindow;
  utf8_str *pstr;
  SDL_Surface *text;
  SDL_Rect dst;
  SDL_Rect area;

  if (spy_dlg) {
    return;
  }

  spy_dlg = fc_calloc(1, sizeof(struct small_dialog));

  fc_snprintf(cbuf, sizeof(cbuf),
              /* TRANS: "Polish incident !" FIXME!!! */
              _("%s incident !"),
              nation_adjective_for_player(pplayer));
  pstr = create_utf8_from_char(cbuf, adj_font(12));
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = sdip_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_WINDOW, pwindow);

  spy_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ============================================================= */
  /* label */
  fc_snprintf(cbuf, sizeof(cbuf), _("Shall we declare WAR on them?"));

  pstr = create_utf8_from_char(cbuf, adj_font(14));
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pstr->fgcol = *get_theme_color(COLOR_THEME_WARDLG_TEXT);

  text = create_text_surf_from_utf8(pstr);
  FREEUTF8STR(pstr);
  area.w = MAX(area.w, text->w);
  area.h += text->h + adj_size(10);

  buf = create_themeicon_button_from_chars(current_theme->cancel_icon,
                                           pwindow->dst, _("No"),
                                           adj_font(12), 0);

  buf->action = cancel_sdip_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  area.h += buf->size.h;

  add_to_gui_list(ID_BUTTON, buf);

  buf = create_themeicon_button_from_chars(current_theme->ok_icon, pwindow->dst,
                                           _("Yes"), adj_font(12), 0);

  buf->action = cancel_pact_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->data.player = pplayer;
  buf->key = SDLK_RETURN;
  add_to_gui_list(ID_BUTTON, buf);
  buf->size.w = MAX(buf->next->size.w, buf->size.w);
  buf->next->size.w = buf->size.w;
  area.w = MAX(area.w , 2 * buf->size.w + adj_size(20));

  spy_dlg->begin_widget_list = buf;

  /* setup window size and start position */
  area.w += adj_size(10);

  buf = pwindow->prev;

  area.h += adj_size(5);

  resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* setup rest of widgets */
  /* label */
  dst.x = area.x + (area.w - text->w) / 2;
  dst.y = area.y + 1;
  alphablit(text, NULL, pwindow->theme, &dst, 255);
  dst.y += text->h + adj_size(5);
  FREESURFACE(text);

  /* no */
  buf = pwindow->prev;
  buf->size.y = dst.y;

  /* yes */
  buf = buf->prev;
  buf->size.x = area.x + (area.w - (2 * buf->size.w + adj_size(20))) / 2;
  buf->size.y = dst.y;

  /* no */
  buf->next->size.x = buf->size.x + buf->size.w + adj_size(20);

  /* ================================================== */
  /* redraw */
  redraw_group(spy_dlg->begin_widget_list, pwindow, 0);
  widget_mark_dirty(pwindow);
  flush_dirty();
}

/* ===================================================================== */

/**********************************************************************//**
  Open diplomacy dialog between client user and given player.
**************************************************************************/
void popup_diplomacy_dialog(struct player *pplayer)
{
  enum diplstate_type type =
    player_diplstate_get(client.conn.playing, pplayer)->type;

  if (!can_meet_with_player(pplayer)) {
    if (DS_WAR == type || pplayer == client.conn.playing) {
      flush_dirty();

      return;
    } else {
      popup_war_dialog(pplayer);

      return;
    }
  } else {
    int button_w = 0, button_h = 0;
    char cbuf[128];
    struct widget *buf = NULL, *pwindow;
    utf8_str *pstr;
    SDL_Surface *text;
    SDL_Rect dst;
    bool shared;
    SDL_Rect area;
    int buttons = 0;
    bool can_toward_war;

    if (spy_dlg) {
      return;
    }

    spy_dlg = fc_calloc(1, sizeof(struct small_dialog));

    fc_snprintf(cbuf, sizeof(cbuf),  _("Foreign Minister"));
    pstr = create_utf8_from_char(cbuf, adj_font(12));
    pstr->style |= TTF_STYLE_BOLD;

    pwindow = create_window_skeleton(NULL, pstr, 0);

    pwindow->action = sdip_window_callback;
    set_wstate(pwindow, FC_WS_NORMAL);
    spy_dlg->end_widget_list = pwindow;

    add_to_gui_list(ID_WINDOW, pwindow);

    area = pwindow->area;

    /* ============================================================= */
    /* label */
    if (client.conn.playing == NULL || client.conn.playing->is_male) {
      fc_snprintf(cbuf, sizeof(cbuf), _("Sir!, the %s ambassador has arrived\n"
                                        "What are your wishes?"),
                  nation_adjective_for_player(pplayer));
    } else {
      fc_snprintf(cbuf, sizeof(cbuf), _("Ma'am!, the %s ambassador has arrived\n"
                                        "What are your wishes?"),
                  nation_adjective_for_player(pplayer));
    }

    pstr = create_utf8_from_char(cbuf, adj_font(14));
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pstr->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_TEXT);

    text = create_text_surf_from_utf8(pstr);
    FREEUTF8STR(pstr);
    area.w = MAX(area.w , text->w);
    area.h += text->h + adj_size(15);

    can_toward_war = can_client_issue_orders()
      && pplayer_can_cancel_treaty(client_player(), pplayer) == DIPL_OK;

    if (can_toward_war) {
      if (type == DS_ARMISTICE) {
        fc_snprintf(cbuf, sizeof(cbuf), _("Declare WAR"));
      } else {
        fc_snprintf(cbuf, sizeof(cbuf), _("Cancel Treaty"));
      }

      /* cancel treaty */
      buf = create_themeicon_button_from_chars(current_theme->units2_icon,
                                               pwindow->dst, cbuf,
                                               adj_font(12), 0);

      buf->action = cancel_pact_dlg_callback;
      set_wstate(buf, FC_WS_NORMAL);
      buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
      buf->data.player = pplayer;
      buf->key = SDLK_c;
      add_to_gui_list(ID_BUTTON, buf);
      buf->size.w = MAX(buf->next->size.w, buf->size.w);
      buf->next->size.w = buf->size.w;
      button_w = MAX(button_w , buf->size.w);
      button_h = MAX(button_h , buf->size.h);
      buttons++;
    }

    shared = gives_shared_vision(client.conn.playing, pplayer);

    if (shared) {
      /* shared vision */
      buf = create_themeicon_button_from_chars(current_theme->units2_icon,
                                               pwindow->dst, _("Withdraw vision"),
                                               adj_font(12), 0);

      buf->action = withdraw_vision_dlg_callback;
      set_wstate(buf, FC_WS_NORMAL);
      buf->data.player = pplayer;
      buf->key = SDLK_w;
      buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
      add_to_gui_list(ID_BUTTON, buf);
      buf->size.w = MAX(buf->next->size.w, buf->size.w);
      buf->next->size.w = buf->size.w;
      button_w = MAX(button_w , buf->size.w);
      button_h = MAX(button_h , buf->size.h);
      buttons++;
    }

    /* meet */
    buf = create_themeicon_button_from_chars(current_theme->players_icon,
                                             pwindow->dst,
                                             _("Call Diplomatic Meeting"),
                                             adj_font(12), 0);

    buf->action = call_meeting_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->data.player = pplayer;
    buf->key = SDLK_m;
    buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
    add_to_gui_list(ID_BUTTON, buf);
    buf->size.w = MAX(buf->next->size.w, buf->size.w);
    buf->next->size.w = buf->size.w;
    button_w = MAX(button_w , buf->size.w);
    button_h = MAX(button_h , buf->size.h);
    buttons++;

    buf = create_themeicon_button_from_chars(current_theme->cancel_icon,
                                             pwindow->dst, _("Send them back"),
                                             adj_font(12), 0);

    buf->action = cancel_sdip_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_DIPLODLG_MEETING_TEXT);
    buf->key = SDLK_ESCAPE;
    button_w = MAX(button_w , buf->size.w);
    button_h = MAX(button_h , buf->size.h);
    buttons++;

    button_h += adj_size(4);
    area.w = MAX(area.w, button_w + adj_size(20));

    area.h += buttons * (button_h + adj_size(10));

    add_to_gui_list(ID_BUTTON, buf);

    spy_dlg->begin_widget_list = buf;

    /* setup window size and start position */
    area.w += adj_size(10);

    buf = pwindow->prev;

    area.h += adj_size(2);

    resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                  (pwindow->size.w - pwindow->area.w) + area.w,
                  (pwindow->size.h - pwindow->area.h) + area.h);

    area = pwindow->area;

    widget_set_position(pwindow,
                        (main_window_width() - pwindow->size.w) / 2,
                        (main_window_height() - pwindow->size.h) / 2);

    /* setup rest of widgets */
    /* label */
    dst.x = area.x + (area.w - text->w) / 2;
    dst.y = area.y + 1;
    alphablit(text, NULL, pwindow->theme, &dst, 255);
    dst.y += text->h + adj_size(15);
    FREESURFACE(text);

    buf = pwindow;

    /* war: meet, peace: cancel treaty */
    buf = buf->prev;
    buf->size.w = button_w;
    buf->size.h = button_h;
    buf->size.x = area.x + (area.w - (buf->size.w)) / 2;
    buf->size.y = dst.y;

    if (shared) {
      /* vision */
      buf = buf->prev;
      buf->size.w = button_w;
      buf->size.h = button_h;
      buf->size.y = buf->next->size.y + buf->next->size.h + adj_size(10);
      buf->size.x = buf->next->size.x;
    }

    if (type != DS_WAR) {
      /* meet */
      buf = buf->prev;
      buf->size.w = button_w;
      buf->size.h = button_h;
      buf->size.y = buf->next->size.y + buf->next->size.h + adj_size(10);
      buf->size.x = buf->next->size.x;
    }

    /* cancel */
    if (can_toward_war) {
      buf = buf->prev;
      buf->size.w = button_w;
      buf->size.h = button_h;
      buf->size.y = buf->next->size.y + buf->next->size.h + adj_size(10);
      buf->size.x = buf->next->size.x;
    }

    /* ================================================== */
    /* redraw */
    redraw_group(spy_dlg->begin_widget_list, pwindow, 0);
    widget_mark_dirty(pwindow);

    flush_dirty();
  }
}
