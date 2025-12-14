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
                          wldlg.c  -  description
                             -------------------
    begin                : Wed Sep 18 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdlib.h>

/* SDL3 */
#include <SDL3/SDL.h>

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "game.h"
#include "movement.h"
#include "unit.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "global_worklist.h"

/* gui-sdl3 */
#include "citydlg.h"
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "helpdlg.h"
#include "mapview.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"

#include "wldlg.h"

#define TARGETS_COL 4
#define TARGETS_ROW 4

struct wl_editor {
  struct widget *begin_widget_list;
  struct widget *end_widget_list; /* window */

  struct advanced_dialog *targets;
  struct advanced_dialog *work;
  struct advanced_dialog *global;

  struct city *pcity;
  int global_worklist_id;
  struct worklist worklist_copy;
  char worklist_name[MAX_LEN_NAME];

  /* shortcuts  */
  struct widget *dock;
  struct widget *worklist_counter;

  struct widget *production_name;
  struct widget *production_progress;

  int stock;
  struct universal currently_building;
} *editor = NULL;


static int worklist_editor_item_callback(struct widget *pwidget);
static SDL_Surface *get_progress_icon(int stock, int cost, int *progress);
static const char *get_production_name(struct city *pcity,
                                       struct universal prod, int *cost);
static void refresh_worklist_count_label(void);
static void refresh_production_label(int stock);

/* =========================================================== */

/**********************************************************************//**
  Worklist Editor Window Callback
**************************************************************************/
static int window_worklist_editor_callback(struct widget *pwidget)
{
  return -1;
}

/**********************************************************************//**
  Popdwon Worklist Editor
**************************************************************************/
static int popdown_worklist_editor_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_worklist_editor();
  }

  return -1;
}

/**********************************************************************//**
  Commit changes to city/global worklist.
  In City Mode Remove Double entry of Imprv/Woder Targets from list.
**************************************************************************/
static int ok_worklist_editor_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int i, j;
    struct city *pcity = editor->pcity;

    /* remove duplicate entry of impv./wonder target from worklist */
    for (i = 0; i < worklist_length(&editor->worklist_copy); i++) {
      if (VUT_IMPROVEMENT == editor->worklist_copy.entries[i].kind) {
        for (j = i + 1; j < worklist_length(&editor->worklist_copy); j++) {
          if (VUT_IMPROVEMENT == editor->worklist_copy.entries[j].kind
              && editor->worklist_copy.entries[i].value.building ==
              editor->worklist_copy.entries[j].value.building) {
            worklist_remove(&editor->worklist_copy, j);
          }
        }
      }
    }

    if (pcity) {
      /* remove duplicate entry of currently building impv./wonder from worklist */
      if (VUT_IMPROVEMENT == editor->currently_building.kind) {
        for (i = 0; i < worklist_length(&editor->worklist_copy); i++) {
          if (VUT_IMPROVEMENT == editor->worklist_copy.entries[i].kind
              && editor->worklist_copy.entries[i].value.building ==
              editor->currently_building.value.building) {
            worklist_remove(&editor->worklist_copy, i);
          }
        }
      }

      /* change production */
      if (!are_universals_equal(&pcity->production, &editor->currently_building)) {
        city_change_production(pcity, &editor->currently_building);
      }

      /* commit new city worklist */
      city_set_worklist(pcity, &editor->worklist_copy);
    } else {
      /* commit global worklist */
      struct global_worklist *gwl = global_worklist_by_id(editor->global_worklist_id);

      if (gwl) {
        global_worklist_set(gwl, &editor->worklist_copy);
        global_worklist_set_name(gwl, editor->worklist_name);
        update_worklist_report_dialog();
      }
    }

    /* popdown dialog */
    popdown_worklist_editor();
  }

  return -1;
}

/**********************************************************************//**
  Rename Global Worklist
**************************************************************************/
static int rename_worklist_editor_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pwidget->string_utf8->text != NULL) {
      fc_snprintf(editor->worklist_name, MAX_LEN_NAME, "%s",
                  pwidget->string_utf8->text);
    } else {
      /* empty input -> restore previous content */
      copy_chars_to_utf8_str(pwidget->string_utf8, editor->worklist_name);
      widget_redraw(pwidget);
      widget_mark_dirty(pwidget);
      flush_dirty();
    }
  }

  return -1;
}

/* ====================================================================== */

/**********************************************************************//**
  Add target to worklist.
**************************************************************************/
static void add_target_to_worklist(struct widget *target)
{
  struct widget *buf = NULL, *dock = NULL;
  utf8_str *pstr = NULL;
  int i;
  struct universal prod = cid_decode(MAX_ID - target->id);

  set_wstate(target, FC_WS_SELECTED);
  widget_redraw(target);
  widget_flush(target);

  /* Deny adding currently building Impr/Wonder Target */
  if (editor->pcity
      && VUT_IMPROVEMENT == prod.kind
      && are_universals_equal(&prod, &editor->currently_building)) {
    return;
  }

  if (worklist_length(&editor->worklist_copy) >= MAX_LEN_WORKLIST - 1) {
    return;
  }

  for (i = 0; i < worklist_length(&editor->worklist_copy); i++) {
    if (VUT_IMPROVEMENT == prod.kind
        && are_universals_equal(&prod, &editor->worklist_copy.entries[i])) {
      return;
    }
  }

  worklist_append(&editor->worklist_copy, &prod);

  /* Create widget entry */
  if (VUT_UTYPE == prod.kind) {
    pstr = create_utf8_from_char_fonto(utype_name_translation(prod.value.utype),
                                       FONTO_DEFAULT);
  } else {
    pstr = create_utf8_from_char_fonto(city_improvement_name_translation(editor->pcity,
                                                                   prod.value.building),
                                       FONTO_DEFAULT);
  }

  pstr->style |= SF_CENTER;
  buf = create_iconlabel(NULL, target->dst, pstr,
                          (WF_RESTORE_BACKGROUND|WF_FREE_DATA));

  set_wstate(buf, FC_WS_NORMAL);
  buf->action = worklist_editor_item_callback;

  buf->data.ptr = fc_calloc(1, sizeof(int));
  *((int *)buf->data.ptr) = worklist_length(&editor->worklist_copy) - 1;

  buf->id = MAX_ID - cid_encode(prod);

  if (editor->work->begin_active_widget_list) {
    dock = editor->work->begin_active_widget_list;
  } else {
    dock = editor->dock;
  }

/* FIXME */
#if 0
  if (worklist_length(&editor->worklist_copy) > editor->work->scroll->active + 1) {

    setup_vertical_widgets_position(1,
      editor->end_widget_list->area.x + adj_size(2),
      get_widget_pointer_from_main_list(ID_WINDOW)->area.y + adj_size(152) +
        editor->work->scroll->up_left_button->size.h + 1,
      adj_size(126), 0, editor->work->begin_widget_list,
      editor->work->end_widget_list);

    setup_vertical_scrollbar_area(editor->work->scroll,
        editor->end_widget_list->area.x + adj_size(2),
        get_widget_pointer_from_main_list(ID_WINDOW)->area.y + adj_size(152),
        adj_size(225), FALSE);

    show_scrollbar(editor->work->scroll);
  }
#endif /* 0 */

  add_widget_to_vertical_scroll_widget_list(editor->work, buf,
                                            dock, FALSE,
                                            editor->end_widget_list->area.x + adj_size(2),
                                            editor->end_widget_list->area.y + adj_size(152));

  buf->size.w = adj_size(126);

  refresh_worklist_count_label();
  redraw_group(editor->work->begin_widget_list,
               editor->work->end_widget_list, TRUE);
  flush_dirty();
}

/**********************************************************************//**
  Find if two targets are the same class (unit, imprv. , wonder).
  This is needed by calculation of change production shields penalty.
  [similar to are_universals_equal()]
**************************************************************************/
static bool are_prods_same_class(const struct universal *one,
                                 const struct universal *two)
{
  if (one->kind != two->kind) {
    return FALSE;
  }

  if (VUT_IMPROVEMENT == one->kind) {
    if (is_wonder(one->value.building)) {
      return is_wonder(two->value.building);
    } else {
      return !is_wonder(two->value.building);
    }
  }

  return FALSE;
}

/**********************************************************************//**
  Change production in editor shell, calculate production shields penalty and
  refresh production progress label
**************************************************************************/
static void change_production(struct universal *prod)
{
  if (!are_prods_same_class(&editor->currently_building, prod)) {
    if (editor->stock != editor->pcity->shield_stock) {
      if (are_prods_same_class(&editor->pcity->production, prod)) {
        editor->stock = editor->pcity->shield_stock;
      }
    } else {
      editor->stock =
        city_change_production_penalty(editor->pcity, prod);
    }
  }

  editor->currently_building = *prod;
  refresh_production_label(editor->stock);
}

/**********************************************************************//**
  Change production of city but only in Editor.
  You must commit this changes by exit editor via OK button (commit function).

  This function don't remove double imprv./wonder target entry from worklist
  and allow more entry of such target (In Production and worklist - this is
  fixed by commit function).
**************************************************************************/
static void add_target_to_production(struct widget *target)
{
  int dummy;
  struct universal prod;

  fc_assert_ret(target != NULL);

  /* redraw Target Icon */
  set_wstate(target, FC_WS_SELECTED);
  widget_redraw(target);
  widget_flush(target);

  prod = cid_decode(MAX_ID - target->id);

  /* check if we change to the same target */
  if (are_universals_equal(&prod, &editor->currently_building)) {
    /* commit changes and exit - double click detection */
    ok_worklist_editor_callback(NULL);
    return;
  }

  change_production(&prod);

  /* change Production Text Label in Worklist Widget list */
  copy_chars_to_utf8_str(editor->work->end_active_widget_list->string_utf8,
                         get_production_name(editor->pcity, prod, &dummy));

  editor->work->end_active_widget_list->id = MAX_ID - cid_encode(prod);

  widget_redraw(editor->work->end_active_widget_list);
  widget_mark_dirty(editor->work->end_active_widget_list);

  flush_dirty();
}

/**********************************************************************//**
  Get Help Info about target
**************************************************************************/
static void get_target_help_data(struct widget *target)
{
  struct universal prod;

  fc_assert_ret(target != NULL);

  /* redraw Target Icon */
  set_wstate(target, FC_WS_SELECTED);
  widget_redraw(target);
  /*widget_flush(target);*/

  prod = cid_decode(MAX_ID - target->id);

  if (VUT_UTYPE == prod.kind) {
    popup_unit_info(utype_number(prod.value.utype));
  } else {
    popup_impr_info(improvement_number(prod.value.building));
  }
}

/**********************************************************************//**
  Targets callback
  left mouse button -> In city mode change production to target.
                       In global mode add target to worklist.
  middle mouse button -> get target "help"
  right mouse button -> add target to worklist.
**************************************************************************/
static int worklist_editor_targets_callback(struct widget *pwidget)
{
  if (main_data.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    switch (main_data.event.button.button) {
    case SDL_BUTTON_LEFT:
      if (editor->pcity) {
        add_target_to_production(pwidget);
      } else {
        add_target_to_worklist(pwidget);
      }
      break;
    case SDL_BUTTON_MIDDLE:
      get_target_help_data(pwidget);
      break;
    case SDL_BUTTON_RIGHT:
      add_target_to_worklist(pwidget);
      break;
    default:
      /* do nothing */
      break;
    }
  } else if (PRESSED_EVENT(main_data.event)) {
    if (editor->pcity) {
      add_target_to_production(pwidget);
    } else {
      add_target_to_worklist(pwidget);
    }
  }

  return -1;
}

/* ====================================================================== */

/**********************************************************************//**
  Remove element from worklist or
  remove currently building imprv/unit and change production to first worklist
  element (or Capitalizations if worklist is empty)
**************************************************************************/
static void remove_item_from_worklist(struct widget *item)
{
  /* only one item (production) is left */
  if (worklist_is_empty(&editor->worklist_copy)) {
    return;
  }

  if (item->data.ptr) {
    /* correct "data" widget fields */
    struct widget *buf = item;

    if (buf != editor->work->begin_active_widget_list) {
      do {
        buf = buf->prev;
        *((int *)buf->data.ptr) = *((int *)buf->data.ptr) - 1;
      } while (buf != editor->work->begin_active_widget_list);
    }

    /* remove element from worklist */
    worklist_remove(&editor->worklist_copy, *((int *)item->data.ptr));

    /* remove widget from widget list */
    del_widget_from_vertical_scroll_widget_list(editor->work, item);
  } else {
    /* change productions to first worklist element */
    struct widget *buf = item->prev;

    change_production(&editor->worklist_copy.entries[0]);
    worklist_advance(&editor->worklist_copy);
    del_widget_from_vertical_scroll_widget_list(editor->work, item);
    FC_FREE(buf->data.ptr);
    if (buf != editor->work->begin_active_widget_list) {
      do {
        buf = buf->prev;
        *((int *)buf->data.ptr) = *((int *)buf->data.ptr) - 1;
      } while (buf != editor->work->begin_active_widget_list);
    }
  }

/* FIXME: fix scrollbar code */
#if 0
  /* worklist_length(&editor->worklist_copy): without production */
  if (worklist_length(&editor->worklist_copy) <= editor->work->scroll->active + 1) {

    setup_vertical_widgets_position(1,
      editor->end_widget_list->area.x + adj_size(2),
      get_widget_pointer_from_main_list(ID_WINDOW)->area.y + adj_size(152),
        adj_size(126), 0, editor->work->begin_widget_list,
      editor->work->end_widget_list);
#if 0
    setup_vertical_scrollbar_area(editor->work->scroll,
        editor->end_widget_list->area.x + adj_size(2),
        get_widget_pointer_from_main_list(ID_WINDOW)->area.y + adj_size(152),
        adj_size(225), FALSE);*/
#endif /* 0 */
    hide_scrollbar(editor->work->scroll);
  }
#endif /* 0 */

  refresh_worklist_count_label();
  redraw_group(editor->work->begin_widget_list,
               editor->work->end_widget_list, TRUE);
  flush_dirty();
}

/**********************************************************************//**
  Swap worklist entries DOWN.
  Function swap current element with next element of worklist.

  If item is last widget or there is only one widget on widgets list
  function remove this widget from widget list and target from worklist

  In City mode, when item is first worklist element, function make
  change production (currently building is moved to first element of worklist
  and first element of worklist is build).
**************************************************************************/
static void swap_item_down_from_worklist(struct widget *item)
{
  char *text;
  Uint16 id;
  bool changed = FALSE;
  struct universal tmp;

  if (item == editor->work->begin_active_widget_list) {
    remove_item_from_worklist(item);
    return;
  }

  text = item->string_utf8->text;
  id = item->id;

  /* second item or higher was clicked */
  if (item->data.ptr) {
    /* worklist operations -> swap down */
    int row = *((int *)item->data.ptr);

    tmp = editor->worklist_copy.entries[row];
    editor->worklist_copy.entries[row] = editor->worklist_copy.entries[row + 1];
    editor->worklist_copy.entries[row + 1] = tmp;

    changed = TRUE;
  } else {
    /* first item was clicked -> change production */
    change_production(&editor->worklist_copy.entries[0]);
    editor->worklist_copy.entries[0] = cid_decode(MAX_ID - id);
    changed = TRUE;
  }

  if (changed) {
    item->string_utf8->text = item->prev->string_utf8->text;
    item->id = item->prev->id;

    item->prev->string_utf8->text = text;
    item->prev->id = id;

    redraw_group(editor->work->begin_widget_list,
                 editor->work->end_widget_list, TRUE);
    flush_dirty();
  }
}

/**********************************************************************//**
  Swap worklist entries UP.
  Function swap current element with prev. element of worklist.

  If item is first widget on widgets list function remove this widget
  from widget list and target from worklist (global mode)
  or from production (city mode)

  In City mode, when item is first worklist element, function make
  change production (currently building is moved to first element of worklist
  and first element of worklist is build).
**************************************************************************/
static void swap_item_up_from_worklist(struct widget *item)
{
  char *text = item->string_utf8->text;
  Uint16 id = item->id;
  bool changed = FALSE;
  struct universal tmp;

  /* first item was clicked -> remove */
  if (item == editor->work->end_active_widget_list) {
    remove_item_from_worklist(item);

    return;
  }

  /* third item or higher was clicked */
  if (item->data.ptr && *((int *)item->data.ptr) > 0) {
    /* worklist operations -> swap up */
    int row = *((int *)item->data.ptr);

    tmp = editor->worklist_copy.entries[row];
    editor->worklist_copy.entries[row] = editor->worklist_copy.entries[row - 1];
    editor->worklist_copy.entries[row - 1] = tmp;

    changed = TRUE;
  } else {
    /* second item was clicked -> change production ... */
    tmp = editor->currently_building;
    change_production(&editor->worklist_copy.entries[0]);
    editor->worklist_copy.entries[0] = tmp;

    changed = TRUE;
  }

  if (changed) {
    item->string_utf8->text = item->next->string_utf8->text;
    item->id = item->next->id;

    item->next->string_utf8->text = text;
    item->next->id = id;

    redraw_group(editor->work->begin_widget_list,
                 editor->work->end_widget_list, TRUE);
    flush_dirty();
  }
}

/**********************************************************************//**
  worklist callback
  left mouse button -> swap entries up.
  middle mouse button -> remove element from list
  right mouse button -> swap entries down.
**************************************************************************/
static int worklist_editor_item_callback(struct widget *pwidget)
{
  if (main_data.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    switch (main_data.event.button.button) {
    case SDL_BUTTON_LEFT:
      swap_item_up_from_worklist(pwidget);
      break;
    case SDL_BUTTON_MIDDLE:
      remove_item_from_worklist(pwidget);
      break;
    case SDL_BUTTON_RIGHT:
      swap_item_down_from_worklist(pwidget);
      break;
    default:
      ;/* do nothing */
      break;
    }
  } else if (PRESSED_EVENT(main_data.event)) {
    swap_item_up_from_worklist(pwidget);
  }

  return -1;
}
/* ======================================================= */

/**********************************************************************//**
  Add global worklist to city worklist starting from last free entry.
  Add only avilable targets in current game state.
  If global worklist have more targets that city worklist have free
  entries then we adding only first part of global worklist.
**************************************************************************/
static void add_global_worklist(struct widget *pwidget)
{
  struct global_worklist *gwl = global_worklist_by_id(MAX_ID - pwidget->id);
  const struct worklist *pworklist;
  int count, firstfree;

  if (!gwl
      || !(pworklist = global_worklist_get(gwl))
      || worklist_is_empty(pworklist)) {
    return;
  }

  if (worklist_length(&editor->worklist_copy) >= MAX_LEN_WORKLIST - 1) {
    /* Worklist is full */
    return;
  }

  firstfree = worklist_length(&editor->worklist_copy);
  /* Copy global worklist to city worklist */
  for (count = 0 ; count < worklist_length(pworklist); count++) {
    struct widget *buf;

    /* Global worklist can have targets unavilable in current state of game
       then we must remove those targets from new city worklist */
    if (!can_city_build_later(&(wld.map), editor->pcity, &pworklist->entries[count])) {
      continue;
    }

    worklist_append(&editor->worklist_copy, &pworklist->entries[count]);

    /* Create widget */
    if (VUT_UTYPE == pworklist->entries[count].kind) {
      buf = create_iconlabel(NULL, pwidget->dst,
                              create_utf8_from_char_fonto(
                      utype_name_translation(pworklist->entries[count].value.utype),
                      FONTO_DEFAULT),
                              (WF_RESTORE_BACKGROUND|WF_FREE_DATA));
      buf->id = MAX_ID - cid_encode_unit(pworklist->entries[count].value.utype);
    } else {
      buf = create_iconlabel(NULL, pwidget->dst,
                              create_utf8_from_char_fonto(
                      city_improvement_name_translation(editor->pcity,
                                                        pworklist->entries[count].value.building),
                      FONTO_DEFAULT),
                              (WF_RESTORE_BACKGROUND|WF_FREE_DATA));
      buf->id = MAX_ID - cid_encode_building(pworklist->entries[count].value.building);
    }

    buf->string_utf8->style |= SF_CENTER;
    set_wstate(buf, FC_WS_NORMAL);
    buf->action = worklist_editor_item_callback;
    buf->size.w = adj_size(126);
    buf->data.ptr = fc_calloc(1, sizeof(int));
    *((int *)buf->data.ptr) = firstfree;

    add_widget_to_vertical_scroll_widget_list(editor->work,
                                              buf, editor->work->begin_active_widget_list,
                                              FALSE,
                                              editor->end_widget_list->area.x + adj_size(2),
                                              editor->end_widget_list->area.y + adj_size(152));

    firstfree++;
    if (firstfree == MAX_LEN_WORKLIST - 1) {
      break;
    }
  }

  refresh_worklist_count_label();
  redraw_group(editor->work->begin_widget_list,
               editor->work->end_widget_list, TRUE);

  flush_dirty();
}

/**********************************************************************//**
  Clear city worklist and copy here global worklist.
  Copy only available targets in current game state.
  If all targets are unavilable then leave city worklist untouched.
**************************************************************************/
static void set_global_worklist(struct widget *pwidget)
{
  struct global_worklist *gwl = global_worklist_by_id(MAX_ID - pwidget->id);
  struct widget *buf = editor->work->end_active_widget_list;
  const struct worklist *pworklist;
  struct worklist wl;
  int count, wl_count;
  struct universal target;

  if (!gwl
      || !(pworklist = global_worklist_get(gwl))
      || worklist_is_empty(pworklist)) {
    return;
  }

  /* clear tmp worklist */
  worklist_init(&wl);

  wl_count = 0;
  /* copy global worklist to city worklist */
  for (count = 0; count < worklist_length(pworklist); count++) {
    /* global worklist can have targets unavilable in current state of game
       then we must remove those targets from new city worklist */
    if (!can_city_build_later(&(wld.map), editor->pcity, &pworklist->entries[count])) {
      continue;
    }

    wl.entries[wl_count] = pworklist->entries[count];
    wl_count++;
  }
  /* --------------------------------- */

  if (!worklist_is_empty(&wl)) {
    /* free old widget list */
    if (buf != editor->work->begin_active_widget_list) {
      buf = buf->prev;
      if (buf != editor->work->begin_active_widget_list) {
        do {
          buf = buf->prev;
          del_widget_from_vertical_scroll_widget_list(editor->work, buf->next);
        } while (buf != editor->work->begin_active_widget_list);
      }
      del_widget_from_vertical_scroll_widget_list(editor->work, buf);
    }
    /* --------------------------------- */

    worklist_copy(&editor->worklist_copy, &wl);

    /* --------------------------------- */
    /* Create new widget list */
    for (count = 0; count < MAX_LEN_WORKLIST; count++) {
      /* End of list */
      if (!worklist_peek_ith(&editor->worklist_copy, &target, count)) {
        break;
      }

      if (VUT_UTYPE == target.kind) {
        buf = create_iconlabel(NULL, pwidget->dst,
          create_utf8_from_char_fonto(utype_name_translation(target.value.utype),
                                      FONTO_DEFAULT),
                               (WF_RESTORE_BACKGROUND|WF_FREE_DATA));
        buf->id = MAX_ID - B_LAST - utype_number(target.value.utype);
      } else {
        buf = create_iconlabel(NULL, pwidget->dst,
          create_utf8_from_char_fonto(city_improvement_name_translation(editor->pcity,
                                                                  target.value.building),
                                      FONTO_DEFAULT),
                               (WF_RESTORE_BACKGROUND|WF_FREE_DATA));
        buf->id = MAX_ID - improvement_number(target.value.building);
      }
      buf->string_utf8->style |= SF_CENTER;
      set_wstate(buf, FC_WS_NORMAL);
      buf->action = worklist_editor_item_callback;
      buf->size.w = adj_size(126);
      buf->data.ptr = fc_calloc(1, sizeof(int));
      *((int *)buf->data.ptr) = count;

      add_widget_to_vertical_scroll_widget_list(editor->work,
        buf, editor->work->begin_active_widget_list, FALSE,
        editor->end_widget_list->area.x + adj_size(2),
        editor->end_widget_list->area.y + adj_size(152));
    }

    refresh_worklist_count_label();
    redraw_group(editor->work->begin_widget_list,
                 editor->work->end_widget_list, TRUE);

    flush_dirty();
  }
}

/**********************************************************************//**
  Global worklist callback
  left mouse button -> add global worklist to current city list
  right mouse button -> clear city worklist and copy here global worklist.

  There are problems with imprv./wonder targets because those can't be doubled
  on worklist and adding/setting can give you situation that global worklist
  have imprv./wonder entry that exist on city worklist or in building state.
  I don't make such check here and allow this "functionality" because doubled
  imprv./wonder entry are removed from city worklist during "commit" phase.
**************************************************************************/
static int global_worklist_callback(struct widget *pwidget)
{
  if (main_data.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    switch (main_data.event.button.button) {
    case SDL_BUTTON_LEFT:
      add_global_worklist(pwidget);
      break;
    case SDL_BUTTON_MIDDLE:
      /* nothing */
      break;
    case SDL_BUTTON_RIGHT:
      set_global_worklist(pwidget);
      break;
    default:
      /* do nothing */
      break;
    }
  } else if (PRESSED_EVENT(main_data.event)) {
    add_global_worklist(pwidget);
  }

  return -1;
}

/* ======================================================= */

/**********************************************************************//**
  Return full unit/imprv. name and build cost in "cost" pointer.
**************************************************************************/
static const char *get_production_name(struct city *pcity,
                                       struct universal prod, int *cost)
{
  fc_assert_ret_val(cost != NULL, NULL);

  *cost = universal_build_shield_cost(pcity, &prod);

  if (VUT_UTYPE == prod.kind) {
    return utype_name_translation(prod.value.utype);
  } else {
    return city_improvement_name_translation(pcity, prod.value.building);
  }
}

/**********************************************************************//**
  Return progress icon (bar) of current production state
  stock - current shields stocks
  cost - unit/imprv. cost
  function return in "progress" pointer (0 - 100 %) progress in numbers
**************************************************************************/
static SDL_Surface *get_progress_icon(int stock, int cost, int *progress)
{
  SDL_Surface *icon = NULL;
  int width;

  fc_assert_ret_val(progress != NULL, NULL);

  if (stock < cost) {
    width = ((float)stock / cost) * adj_size(116.0);
    *progress = ((float)stock / cost) * 100.0;
    if (!width && stock) {
      *progress = 1;
      width = 1;
    }
  } else {
    width = adj_size(116);
    *progress = 100;
  }

  icon = create_bcgnd_surf(current_theme->edit, FC_WS_NORMAL, adj_size(120), adj_size(30));

  if (width) {
    SDL_Rect dst = {2,1,0,0};
    SDL_Surface *buf = create_bcgnd_surf(current_theme->button, FC_WS_DISABLED, width,
                                         adj_size(28));

    alphablit(buf, NULL, icon, &dst, 255);
    FREESURFACE(buf);
  }

  return icon;
}

/**********************************************************************//**
  Update and redraw production name label in worklist editor.
  stock - pcity->shields_stock or current stock after change production lost.
**************************************************************************/
static void refresh_production_label(int stock)
{
  int cost, turns;
  char cbuf[64];
  SDL_Rect area;
  bool convert_prod = (VUT_IMPROVEMENT == editor->currently_building.kind)
      && is_convert_improvement(editor->currently_building.value.building);
  const char *name = get_production_name(editor->pcity,
                                         editor->currently_building, &cost);

  if (convert_prod) {
    int output = MAX(0, editor->pcity->surplus[O_SHIELD]);

    if (improvement_has_flag(editor->currently_building.value.building,
                             IF_GOLD)) {
      fc_snprintf(cbuf, sizeof(cbuf),
                  PL_("%s\n%d gold per turn",
                      "%s\n%d gold per turn", output),
                  name, output);
    } else if (improvement_has_flag(editor->currently_building.value.building,
                                    IF_INFRA)) {
      fc_snprintf(cbuf, sizeof(cbuf),
                  PL_("%s\n%d infrapoint per turn",
                      "%s\n%d infrapoints per turn", output),
                  name, output);
    } else {
      fc_snprintf(cbuf, sizeof(cbuf), "%s\n-", name);
    }
  } else {
    if (stock < cost) {
      turns = city_turns_to_build(editor->pcity,
                                  &editor->currently_building, TRUE);
      if (turns == 999) {
        fc_snprintf(cbuf, sizeof(cbuf), _("%s\nblocked!"), name);
      } else {
        fc_snprintf(cbuf, sizeof(cbuf), _("%s\n%d %s"),
                    name, turns, PL_("turn", "turns", turns));
      }
    } else {
      fc_snprintf(cbuf, sizeof(cbuf), _("%s\nfinished!"), name);
    }
  }
  copy_chars_to_utf8_str(editor->production_name->string_utf8, cbuf);

  widget_undraw(editor->production_name);
  remake_label_size(editor->production_name);

  editor->production_name->size.x = editor->end_widget_list->area.x +
    (adj_size(130) - editor->production_name->size.w)/2;

  /* Can't just widget_mark_dirty(), as it may have reduced in size */
  area.x = editor->end_widget_list->area.x;  /* left edge of client area */
  area.y = editor->production_name->size.y;
  area.w = adj_size(130);
  area.h = editor->production_name->size.h;
  layer_rect_to_screen_rect(editor->end_widget_list->dst, &area);

  if (get_wflags(editor->production_name) & WF_RESTORE_BACKGROUND) {
    refresh_widget_background(editor->production_name);
  }

  widget_redraw(editor->production_name);
  dirty_sdl_rect(&area);

  FREESURFACE(editor->production_progress->theme);
  editor->production_progress->theme =
    get_progress_icon(stock, cost, &cost);

  if (!convert_prod) {
    fc_snprintf(cbuf, sizeof(cbuf), "%d%%" , cost);
  } else {
    fc_snprintf(cbuf, sizeof(cbuf), "-");
  }
  copy_chars_to_utf8_str(editor->production_progress->string_utf8, cbuf);
  widget_redraw(editor->production_progress);
  widget_mark_dirty(editor->production_progress);
}

/**********************************************************************//**
  Update and redraw worklist length counter in worklist editor
**************************************************************************/
static void refresh_worklist_count_label(void)
{
  char cbuf[64];
  SDL_Rect area;
  int len = worklist_length(&editor->worklist_copy);

  if (editor->pcity != NULL) {
    len += 1;  /* External entry from current production */
  }

  fc_snprintf(cbuf, sizeof(cbuf),
              /* TRANS: length of worklist */
              PL_("( %d entry )", "( %d entries )", len), len);
  copy_chars_to_utf8_str(editor->worklist_counter->string_utf8, cbuf);

  widget_undraw(editor->worklist_counter);
  remake_label_size(editor->worklist_counter);

  editor->worklist_counter->size.x = editor->end_widget_list->area.x +
    (adj_size(130) - editor->worklist_counter->size.w)/2;

  if (get_wflags(editor->worklist_counter) & WF_RESTORE_BACKGROUND) {
    refresh_widget_background(editor->worklist_counter);
  }

  widget_redraw(editor->worklist_counter);

  /* Can't just widget_mark_dirty(), as it may have reduced in size */
  area.x = editor->end_widget_list->area.x;  /* left edge of client area */
  area.y = editor->worklist_counter->size.y;
  area.w = adj_size(130);
  area.h = editor->worklist_counter->size.h;
  layer_rect_to_screen_rect(editor->end_widget_list->dst, &area);
  dirty_sdl_rect(&area);
}

/* ====================================================================== */

/**********************************************************************//**
  Global/City worklist editor.
  if pcity == NULL then function takes worklist as global worklist.
  worklist must be not NULL.
**************************************************************************/
void popup_worklist_editor(struct city *pcity, struct global_worklist *gwl)
{
  SDL_Color bg_color = {255,255,255,128};
  SDL_Color bg_color2 = {255,255,255,136};
  int count = 0, turns;
  int widget_w = 0, widget_h = 0;
  utf8_str *pstr = NULL;
  struct widget *buf = NULL, *pwindow, *last;
  SDL_Surface *text = NULL, *text_name = NULL, *zoomed = NULL;
  SDL_Surface *main_surf;
  SDL_Surface *icon;
  SDL_Rect dst;
  char cbuf[128];
  struct unit_type *punittype = NULL;
  char *state = NULL;
  bool advanced_tech;
  bool can_build, can_eventually_build;
  SDL_Rect area;
  int len;
  const struct civ_map *nmap = &(wld.map);

  if (editor) {
    return;
  }

  editor = fc_calloc(1, sizeof(struct wl_editor));

  if (pcity) {
    editor->pcity = pcity;
    editor->global_worklist_id = -1;
    editor->currently_building = pcity->production;
    editor->stock = pcity->shield_stock;
    worklist_copy(&editor->worklist_copy, &pcity->worklist);
    fc_snprintf(editor->worklist_name, sizeof(editor->worklist_name),
                "%s worklist", city_name_get(pcity));
  } else if (gwl != NULL) {
    editor->pcity = NULL;
    editor->global_worklist_id = global_worklist_id(gwl);
    worklist_copy(&editor->worklist_copy, global_worklist_get(gwl));
    sz_strlcpy(editor->worklist_name, global_worklist_name(gwl));
  } else {
    /* Not valid variant! */
    return;
  }

  len = worklist_length(&editor->worklist_copy);
  advanced_tech = (pcity == NULL);

  /* --------------- */
  /* Create Target Background Icon */
  main_surf = create_surf(adj_size(116), adj_size(116));
  SDL_FillSurfaceRect(main_surf, NULL, map_rgba(main_surf->format, bg_color));

  create_frame(main_surf,
               0, 0, main_surf->w - 1, main_surf->h - 1,
               get_theme_color(COLOR_THEME_WLDLG_FRAME));

  /* ---------------- */
  /* Create Main Window */
  pwindow = create_window_skeleton(NULL, NULL, 0);
  pwindow->action = window_worklist_editor_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_WINDOW, pwindow);
  editor->end_widget_list = pwindow;

  area = pwindow->area;

  /* ---------------- */
  if (pcity) {
    fc_snprintf(cbuf, sizeof(cbuf), _("Worklist of\n%s"), city_name_get(pcity));
    len += 1;  /* External entry from current production */
  } else {
    fc_snprintf(cbuf, sizeof(cbuf), "%s", global_worklist_name(gwl));
  }

  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

  add_to_gui_list(ID_LABEL, buf);
  /* --------------------------- */

  fc_snprintf(cbuf, sizeof(cbuf),
              /* TRANS: Length of worklist */
              PL_("( %d entry )", "( %d entries )", len), len);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->bgcol = (SDL_Color) {0, 0, 0, 0};
  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);
  editor->worklist_counter = buf;
  add_to_gui_list(ID_LABEL, buf);
  /* --------------------------- */

  /* Create production progress label or rename worklist edit */
  if (pcity) {
    /* count == cost */
    /* turns == progress */
    const char *name = city_production_name_translation(pcity);
    bool convert_prod = city_production_is_genus(pcity, IG_CONVERT);

    count = city_production_build_shield_cost(pcity);

    if (convert_prod) {
      int output = MAX(0, pcity->surplus[O_SHIELD]);

      if (city_production_has_flag(pcity, IF_GOLD)) {
        fc_snprintf(cbuf, sizeof(cbuf),
                    PL_("%s\n%d gold per turn",
                        "%s\n%d gold per turn", output),
                    name, output);
      } else if (city_production_has_flag(pcity, IF_INFRA)) {
        fc_snprintf(cbuf, sizeof(cbuf),
                    PL_("%s\n%d infrapoint per turn",
                        "%s\n%d infrapoints per turn", output),
                    name, output);
      } else {
        fc_snprintf(cbuf, sizeof(cbuf), "%s\n-", name);
      }
    } else {
      if (pcity->shield_stock < count) {
        turns = city_production_turns_to_build(pcity, TRUE);
        if (turns == 999) {
          fc_snprintf(cbuf, sizeof(cbuf), _("%s\nblocked!"), name);
        } else {
          fc_snprintf(cbuf, sizeof(cbuf), _("%s\n%d %s"),
                      name, turns, PL_("turn", "turns", turns));
        }
      } else {
        fc_snprintf(cbuf, sizeof(cbuf), _("%s\nfinished!"), name);
      }
    }
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    pstr->style |= SF_CENTER;
    buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

    editor->production_name = buf;
    add_to_gui_list(ID_LABEL, buf);

    icon = get_progress_icon(pcity->shield_stock, count, &turns);

    if (!convert_prod) {
      fc_snprintf(cbuf, sizeof(cbuf), "%d%%" , turns);
    } else {
      fc_snprintf(cbuf, sizeof(cbuf), "-");
    }
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
    pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

    buf = create_iconlabel(icon, pwindow->dst, pstr,
                           (WF_RESTORE_BACKGROUND|WF_ICON_CENTER|WF_FREE_THEME));

    icon = NULL;
    turns = 0;
    editor->production_progress = buf;
    add_to_gui_list(ID_LABEL, buf);
  } else {
    buf = create_edit_from_chars_fonto(NULL, pwindow->dst,
                                       global_worklist_name(gwl), FONTO_DEFAULT,
                                       adj_size(120), WF_RESTORE_BACKGROUND);
    buf->action = rename_worklist_editor_callback;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(ID_EDIT, buf);
  }

  /* --------------------------- */
  /* Commit Widget */
  buf = create_themeicon(current_theme->ok_icon, pwindow->dst, WF_RESTORE_BACKGROUND);

  buf->action = ok_worklist_editor_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_RETURN;

  add_to_gui_list(ID_BUTTON, buf);
  /* --------------------------- */
  /* Cancel Widget */
  buf = create_themeicon(current_theme->cancel_icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND);

  buf->action = popdown_worklist_editor_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_BUTTON, buf);
  /* --------------------------- */
  /* work list */

  /*
     pwidget->data filed will contains position of target in worklist all
     action on worklist (swap/romove/add) must correct this fields

     Production Widget Label in worklist Widget list
     will have this field NULL
  */

  editor->work = fc_calloc(1, sizeof(struct advanced_dialog));

  editor->work->scroll = fc_calloc(1, sizeof(struct scroll_bar));
  editor->work->scroll->count = 0;
  editor->work->scroll->active = MAX_LEN_WORKLIST;
  editor->work->scroll->step = 1;

/* FIXME: this should replace the 4 lines above, but
 *        editor->work->end_widget_list is not set yet */
#if 0
  create_vertical_scrollbar(editor->work, 1, MAX_LEN_WORKLIST, TRUE, TRUE);
#endif /* 0 */

  if (pcity) {
    /* Production Widget Label */
    pstr = create_utf8_from_char_fonto(city_production_name_translation(pcity),
                                       FONTO_DEFAULT);
    turns = city_production_build_shield_cost(pcity);
    pstr->style |= SF_CENTER;
    buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);

    set_wstate(buf, FC_WS_NORMAL);
    buf->action = worklist_editor_item_callback;

    add_to_gui_list(MAX_ID - cid_encode(pcity->production), buf);

    editor->work->end_widget_list = buf;
    editor->work->begin_widget_list = buf;
    editor->work->end_active_widget_list = editor->work->end_widget_list;
    editor->work->begin_active_widget_list = editor->work->begin_widget_list;
    editor->work->scroll->count++;
  }

  last = buf;
  editor->dock = buf;

  /* Create Widget Labels of worklist entries */

  count = 0;

  worklist_iterate(&editor->worklist_copy, prod) {
    if (VUT_UTYPE == prod.kind) {
      pstr = create_utf8_from_char_fonto(utype_name_translation(prod.value.utype),
                                         FONTO_DEFAULT);
    } else {
      pstr = create_utf8_from_char_fonto(city_improvement_name_translation(pcity,
                                                                     prod.value.building),
                                         FONTO_DEFAULT);
    }
    pstr->style |= SF_CENTER;
    buf = create_iconlabel(NULL, pwindow->dst, pstr,
                            (WF_RESTORE_BACKGROUND|WF_FREE_DATA));

    set_wstate(buf, FC_WS_NORMAL);
    buf->action = worklist_editor_item_callback;

    buf->data.ptr = fc_calloc(1, sizeof(int));
    *((int *)buf->data.ptr) = count;

    add_to_gui_list(MAX_ID - cid_encode(prod), buf);

    count++;

    if (count > editor->work->scroll->active - 1) {
      set_wflag(buf, WF_HIDDEN);
    }

  } worklist_iterate_end;

  if (count) {
    if (!pcity) {
      editor->work->end_widget_list = last->prev;
      editor->work->end_active_widget_list = editor->work->end_widget_list;
    }
    editor->work->begin_widget_list = buf;
    editor->work->begin_active_widget_list = editor->work->begin_widget_list;
  } else {
    if (!pcity) {
      editor->work->end_widget_list = last;
    }
    editor->work->begin_widget_list = last;
  }

/* FIXME */
#if 0
  editor->work->active_widget_list = editor->work->end_active_widget_list;
  create_vertical_scrollbar(editor->work, 1,
                            editor->work->scroll->active, FALSE, TRUE);
  editor->work->scroll->up_left_button->size.w = adj_size(122);
  editor->work->scroll->down_right_button->size.w = adj_size(122);

  /* count: without production */
  if (count <= editor->work->scroll->active + 1) {
    if (count > 0) {
      struct widget *tmp = last;

      do {
        tmp = tmp->prev;
        clear_wflag(tmp, WF_HIDDEN);
      } while (tmp != buf);
    }
    hide_scrollbar(editor->work->scroll);
  }
#endif /* 0 */

  editor->work->scroll->count += count;
  last = editor->work->begin_widget_list;

  /* --------------------------- */
  /* Global worklists */
  if (pcity) {
    count = 0;

    global_worklists_iterate(iter_gwl) {
      buf = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                              global_worklist_name(iter_gwl),
                                              FONTO_DEFAULT,
                                              WF_RESTORE_BACKGROUND);
      set_wstate(buf, FC_WS_NORMAL);
      add_to_gui_list(MAX_ID - global_worklist_id(iter_gwl), buf);
      buf->string_utf8->style |= SF_CENTER;
      buf->action = global_worklist_callback;
      buf->string_utf8->fgcol = bg_color;

      count++;

      if (count > 4) {
        set_wflag(buf, WF_HIDDEN);
      }
    } global_worklists_iterate_end;

    if (count) {
      editor->global = fc_calloc(1, sizeof(struct advanced_dialog));
      editor->global->end_widget_list = last->prev;
      editor->global->end_active_widget_list = editor->global->end_widget_list;
      editor->global->begin_widget_list = buf;
      editor->global->begin_active_widget_list = editor->global->begin_widget_list;

      if (count > 6) {
        editor->global->active_widget_list = editor->global->end_active_widget_list;

        create_vertical_scrollbar(editor->global, 1, 4, FALSE, TRUE);
        editor->global->scroll->up_left_button->size.w = adj_size(122);
        editor->global->scroll->down_right_button->size.w = adj_size(122);
      } else {
        struct widget *tmp = last;

        do {
          tmp = tmp->prev;
          clear_wflag(tmp, WF_HIDDEN);
        } while (tmp != buf);
      }

      last = editor->global->begin_widget_list;
    }
  }
  /* ----------------------------- */
  count = 0;
  /* Targets units and imprv. to build */
  pstr = create_utf8_str_fonto(NULL, 0, FONTO_DEFAULT);
  pstr->style |= (SF_CENTER|TTF_STYLE_BOLD);
  pstr->bgcol = (SDL_Color) {0, 0, 0, 0};

  improvement_iterate(pimprove) {
    can_build = can_player_build_improvement_now(client.conn.playing, pimprove);
    can_eventually_build =
        can_player_build_improvement_later(client.conn.playing, pimprove);

    /* If there's a city, can the city build the improvement? */
    if (pcity) {
      can_build = can_build && can_city_build_improvement_now(pcity, pimprove);
      can_eventually_build = can_eventually_build
        && can_city_build_improvement_later(pcity, pimprove);
    }

    if ((advanced_tech && can_eventually_build)
        || (!advanced_tech && can_build)) {

      icon = crop_rect_from_surface(main_surf, NULL);

      fc_snprintf(cbuf, sizeof(cbuf), "%s", improvement_name_translation(pimprove));
      copy_chars_to_utf8_str(pstr, cbuf);
      pstr->style |= TTF_STYLE_BOLD;

      if (pcity && is_improvement_redundant(pcity, pimprove)) {
        pstr->style |= TTF_STYLE_STRIKETHROUGH;
      }

      text_name = create_text_surf_smaller_than_w(pstr, icon->w - 4);

      if (is_wonder(pimprove)) {
        if (improvement_obsolete(client.conn.playing, pimprove, pcity)) {
          state = _("Obsolete");
        } else if (is_great_wonder(pimprove)) {
          if (great_wonder_is_built(pimprove)) {
            state = _("Built");
          } else if (great_wonder_is_destroyed(pimprove)) {
            state = _("Destroyed");
          } else {
            state = _("Great Wonder");
          }
        } else if (is_small_wonder(pimprove)) {
          if (small_wonder_is_built(client.conn.playing, pimprove)) {
            state = _("Built");
          } else {
            state = _("Small Wonder");
          }
        }
      } else {
        state = NULL;
      }

      if (pcity) {
        if (!is_convert_improvement(pimprove)) {
          struct universal univ = cid_production(cid_encode_building(pimprove));
          int cost = impr_build_shield_cost(pcity, pimprove);

          turns = city_turns_to_build(pcity, &univ, TRUE);

          if (turns == FC_INFINITY) {
            if (state) {
              fc_snprintf(cbuf, sizeof(cbuf), _("(%s)\n%d/%d %s\n%s"),
                          state, pcity->shield_stock, cost,
                          PL_("shield", "shields", cost),
                          _("never"));
            } else {
              fc_snprintf(cbuf, sizeof(cbuf), _("%d/%d %s\n%s"),
                          pcity->shield_stock, cost,
                          PL_("shield", "shields", cost), _("never"));
            }
          } else {
            if (state) {
              fc_snprintf(cbuf, sizeof(cbuf), _("(%s)\n%d/%d %s\n%d %s"),
                          state, pcity->shield_stock, cost,
                          PL_("shield", "shields", cost),
                          turns, PL_("turn", "turns", turns));
            } else {
              fc_snprintf(cbuf, sizeof(cbuf), _("%d/%d %s\n%d %s"),
                          pcity->shield_stock, cost,
                          PL_("shield", "shields", cost),
                          turns, PL_("turn", "turns", turns));
            }
          }
        } else {
          int output = MAX(0, pcity->surplus[O_SHIELD]);

          /* Coinage-like */
          if (improvement_has_flag(pimprove, IF_GOLD)) {
            fc_snprintf(cbuf, sizeof(cbuf), PL_("%d gold per turn",
                                                "%d gold per turn", output),
                        output);
          } else if (improvement_has_flag(pimprove, IF_INFRA)) {
            fc_snprintf(cbuf, sizeof(cbuf), PL_("%d infrapoint per turn",
                                                "%d infrapoints per turn", output),
                        output);
          } else {
            fc_strlcpy(cbuf, "-", sizeof(cbuf));
          }
        }
      } else {
        /* Non city mode */
        if (!is_convert_improvement(pimprove)) {
          int cost = impr_build_shield_cost(NULL, pimprove);

          if (state) {
            fc_snprintf(cbuf, sizeof(cbuf), _("(%s)\n%d %s"),
                        state, cost,
                        PL_("shield", "shields", cost));
          } else {
            fc_snprintf(cbuf, sizeof(cbuf), _("%d %s"),
                        cost,
                        PL_("shield", "shields", cost));
          }
        } else {
          if (improvement_has_flag(pimprove, IF_GOLD)) {
            fc_strlcpy(cbuf, _("shields into gold"), sizeof(cbuf));
          } else if (improvement_has_flag(pimprove, IF_INFRA)) {
            fc_strlcpy(cbuf, _("shields into infrapoints"), sizeof(cbuf));
          } else {
            fc_strlcpy(cbuf, "-", sizeof(cbuf));
          }
        }
      }

      copy_chars_to_utf8_str(pstr, cbuf);
      pstr->style &= ~TTF_STYLE_BOLD;
      pstr->style &= ~TTF_STYLE_STRIKETHROUGH;

      text = create_text_surf_from_utf8(pstr);

      /*-----------------*/

      zoomed = get_building_surface(pimprove);
      zoomed = zoomSurface(zoomed, DEFAULT_ZOOM * ((float)54 / zoomed->w),
                           DEFAULT_ZOOM * ((float)54 / zoomed->w), 1);
      dst.x = (icon->w - zoomed->w) / 2;
      dst.y = (icon->h/2 - zoomed->h) / 2;
      alphablit(zoomed, NULL, icon, &dst, 255);
      dst.y += zoomed->h;
      FREESURFACE(zoomed);

      dst.x = (icon->w - text_name->w) / 2;
      dst.y += ((icon->h - dst.y) - (text_name->h + text->h)) / 2;
      alphablit(text_name, NULL, icon, &dst, 255);

      dst.x = (icon->w - text->w) / 2;
      dst.y += text_name->h;
      alphablit(text, NULL, icon, &dst, 255);

      FREESURFACE(text);
      FREESURFACE(text_name);

      buf = create_icon2(icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND|WF_FREE_THEME);
      set_wstate(buf, FC_WS_NORMAL);

      widget_w = MAX(widget_w, buf->size.w);
      widget_h = MAX(widget_h, buf->size.h);

      buf->data.city = pcity;
      add_to_gui_list(MAX_ID - improvement_number(pimprove), buf);
      buf->action = worklist_editor_targets_callback;

      if (count > (TARGETS_ROW * TARGETS_COL - 1)) {
        set_wflag(buf, WF_HIDDEN);
      }
      count++;
    }
  } improvement_iterate_end;

  /* ------------------------------ */

  unit_type_iterate(un) {
    can_build = can_player_build_unit_now(client.conn.playing, un);
    can_eventually_build =
        can_player_build_unit_later(client.conn.playing, un);

    /* If there's a city, can the city build the unit? */
    if (pcity) {
      can_build = can_build && can_city_build_unit_now(nmap, pcity, un);
      can_eventually_build = can_eventually_build
        && can_city_build_unit_later(nmap, pcity, un);
    }

    if ((advanced_tech && can_eventually_build)
        || (!advanced_tech && can_build)) {

      punittype = un;

      icon = crop_rect_from_surface(main_surf, NULL);

      fc_snprintf(cbuf, sizeof(cbuf), "%s", utype_name_translation(un));

      copy_chars_to_utf8_str(pstr, cbuf);
      pstr->style |= TTF_STYLE_BOLD;
      text_name = create_text_surf_smaller_than_w(pstr, icon->w - 4);

      if (pcity) {
        struct universal univ = cid_production(cid_encode_unit(un));
        int cost = utype_build_shield_cost(pcity, NULL, un);

        turns = city_turns_to_build(pcity, &univ, TRUE);

        if (turns == FC_INFINITY) {
          fc_snprintf(cbuf, sizeof(cbuf),
                      _("(%d/%d/%s)\n%d/%d %s\nnever"),
                      punittype->attack_strength,
                      punittype->defense_strength,
                      move_points_text(punittype->move_rate, TRUE),
                      pcity->shield_stock, cost,
                      PL_("shield", "shields", cost));
        } else {
          fc_snprintf(cbuf, sizeof(cbuf),
                      _("(%d/%d/%s)\n%d/%d %s\n%d %s"),
                      punittype->attack_strength,
                      punittype->defense_strength,
                      move_points_text(punittype->move_rate, TRUE),
                      pcity->shield_stock, cost,
                      PL_("shield", "shields", cost),
                      turns, PL_("turn", "turns", turns));
        }
      } else {
        int cost = utype_build_shield_cost(NULL, client_player(), un);

        fc_snprintf(cbuf, sizeof(cbuf),
                    _("(%d/%d/%s)\n%d %s"),
                    punittype->attack_strength,
                    punittype->defense_strength,
                    move_points_text(punittype->move_rate, TRUE),
                    cost,
                    PL_("shield", "shields", cost));
      }

      copy_chars_to_utf8_str(pstr, cbuf);
      pstr->style &= ~TTF_STYLE_BOLD;

      text = create_text_surf_from_utf8(pstr);

      zoomed = adj_surf(get_unittype_surface(un, direction8_invalid()));
      dst.x = (icon->w - zoomed->w) / 2;
      dst.y = (icon->h / 2 - zoomed->h) / 2;
      alphablit(zoomed, NULL, icon, &dst, 255);
      FREESURFACE(zoomed);

      dst.x = (icon->w - text_name->w) / 2;
      dst.y = icon->h / 2 + (icon->h / 2 - (text_name->h + text->h)) / 2;
      alphablit(text_name, NULL, icon, &dst, 255);

      dst.x = (icon->w - text->w) / 2;
      dst.y += text_name->h;
      alphablit(text, NULL, icon, &dst, 255);

      FREESURFACE(text);
      FREESURFACE(text_name);

      buf = create_icon2(icon, pwindow->dst,
                         WF_RESTORE_BACKGROUND|WF_FREE_THEME);
      set_wstate(buf, FC_WS_NORMAL);

      widget_w = MAX(widget_w, buf->size.w);
      widget_h = MAX(widget_h, buf->size.h);

      buf->data.city = pcity;
      add_to_gui_list(MAX_ID - cid_encode_unit(un), buf);
      buf->action = worklist_editor_targets_callback;

      if (count > (TARGETS_ROW * TARGETS_COL - 1)) {
        set_wflag(buf, WF_HIDDEN);
      }
      count++;

    }
  } unit_type_iterate_end;

  editor->targets = fc_calloc(1, sizeof(struct advanced_dialog));

  editor->targets->end_widget_list = last->prev;
  editor->targets->begin_widget_list = buf;
  editor->targets->end_active_widget_list = editor->targets->end_widget_list;
  editor->targets->begin_active_widget_list = editor->targets->begin_widget_list;
  editor->targets->active_widget_list = editor->targets->end_active_widget_list;

  /* --------------- */
  if (count > (TARGETS_ROW * TARGETS_COL - 1)) {
    count = create_vertical_scrollbar(editor->targets,
                                      TARGETS_COL, TARGETS_ROW, TRUE, TRUE);
  } else {
    count = 0;
  }
  /* --------------- */

  editor->begin_widget_list = editor->targets->begin_widget_list;

  /* Window */
  area.w = MAX(area.w, widget_w * TARGETS_COL + count + adj_size(130));
  area.h = MAX(area.h, widget_h * TARGETS_ROW);

  icon = theme_get_background(active_theme, BACKGROUND_WLDLG);
  if (resize_window(pwindow, icon, NULL,
                    (pwindow->size.w - pwindow->area.w) + area.w,
                    (pwindow->size.h - pwindow->area.h) + area.h)) {
    FREESURFACE(icon);
  }

  area = pwindow->area;

  /* Backgrounds */
  dst.x = area.x;
  dst.y = area.y;
  dst.w = adj_size(130);
  dst.h = adj_size(145);

  SDL_FillSurfaceRect(pwindow->theme, &dst,
                      map_rgba(pwindow->theme->format,
                               *get_theme_color(COLOR_THEME_BACKGROUND)));

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w - 1, dst.h - 1,
               get_theme_color(COLOR_THEME_WLDLG_FRAME));
  create_frame(pwindow->theme,
               dst.x + 2, dst.y + 2, dst.w - 5, dst.h - 5,
               get_theme_color(COLOR_THEME_WLDLG_FRAME));

  dst.x = area.x;
  dst.y += dst.h + adj_size(2);
  dst.w = adj_size(130);
  dst.h = adj_size(228);
  fill_rect_alpha(pwindow->theme, &dst, &bg_color2);

  create_frame(pwindow->theme,
               dst.x, dst.y, dst.w - 1, dst.h - 1,
               get_theme_color(COLOR_THEME_WLDLG_FRAME));

  if (editor->global) {
    dst.x = area.x;
    dst.y += dst.h + adj_size(2);
    dst.w = adj_size(130);
    dst.h = pwindow->size.h - dst.y - adj_size(4);

    SDL_FillSurfaceRect(pwindow->theme, &dst,
                        map_rgba(pwindow->theme->format,
                                 *get_theme_color(COLOR_THEME_BACKGROUND)));

    create_frame(pwindow->theme,
                 dst.x, dst.y, dst.w - 1, dst.h - 1,
                 get_theme_color(COLOR_THEME_WLDLG_FRAME));
    create_frame(pwindow->theme,
                 dst.x + adj_size(2), dst.y + adj_size(2),
                 dst.w - adj_size(5), dst.h - adj_size(5),
                 get_theme_color(COLOR_THEME_WLDLG_FRAME));
  }

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* Name */
  buf = pwindow->prev;
  buf->size.x = area.x + (adj_size(130) - buf->size.w) / 2;
  buf->size.y = area.y + adj_size(4);

  /* Size of worklist (without production) */
  buf = buf->prev;
  buf->size.x = area.x + (adj_size(130) - buf->size.w) / 2;
  buf->size.y = buf->next->size.y + buf->next->size.h;

  if (pcity) {
    /* Current build and progress bar */
    buf = buf->prev;
    buf->size.x = area.x + (adj_size(130) - buf->size.w) / 2;
    buf->size.y = buf->next->size.y + buf->next->size.h + adj_size(5);

    buf = buf->prev;
    buf->size.x = area.x + (adj_size(130) - buf->size.w) / 2;
    buf->size.y = buf->next->size.y + buf->next->size.h;
  } else {
    /* Rename worklist */
    buf = buf->prev;
    buf->size.x = area.x + (adj_size(130) - buf->size.w) / 2;
    buf->size.y = area.y + 1 + (adj_size(145) - buf->size.h) / 2;
  }

  /* Ok button */
  buf = buf->prev;
  buf->size.x = area.x + (adj_size(65) - buf->size.w) / 2;
  buf->size.y = area.y + adj_size(135) - buf->size.h;

  /* Exit button */
  buf = buf->prev;
  buf->size.x = area.x + adj_size(65) + (adj_size(65) - buf->size.w) / 2;
  buf->size.y = area.y + adj_size(135) - buf->size.h;

  /* Worklist */
  /* editor->work->scroll->count: including production */
  if (len > 0) {
    /* FIXME */
    setup_vertical_widgets_position(1,
                                    area.x + adj_size(2), area.y + adj_size(152)
          /* + ((editor->work->scroll->count > editor->work->scroll->active + 2) ?
             editor->work->scroll->up_left_button->size.h + 1 : 0)*/,
                                    adj_size(126), 0, editor->work->begin_widget_list,
                                    editor->work->end_widget_list);

    setup_vertical_scrollbar_area(editor->work->scroll,
                                  area.x + adj_size(2),
                                  area.y + adj_size(152),
                                  adj_size(225), FALSE);
  }

  /* Global worklists */
  if (editor->global) {
    setup_vertical_widgets_position(1,
                                    area.x + adj_size(4),
                                    area.y + adj_size(384) +
                                    (editor->global->scroll ?
                                     editor->global->scroll->up_left_button->size.h + 1 : 0),
                                    adj_size(122), 0, editor->global->begin_widget_list,
                                    editor->global->end_widget_list);

    if (editor->global->scroll) {
      setup_vertical_scrollbar_area(editor->global->scroll,
                                    area.x + adj_size(4),
                                    area.y + adj_size(384),
                                    adj_size(93), FALSE);
    }

  }

  /* Targets */
  setup_vertical_widgets_position(TARGETS_COL,
                                  area.x + adj_size(130), area.y,
                                  0, 0, editor->targets->begin_widget_list,
                                  editor->targets->end_widget_list);

  if (editor->targets->scroll) {
    setup_vertical_scrollbar_area(editor->targets->scroll,
                                  area.x + area.w,
                                  area.y + 1,
                                  area.h - 1, TRUE);

  }

  /* ----------------------------------- */
  FREEUTF8STR(pstr);
  FREESURFACE(main_surf);

  redraw_group(editor->begin_widget_list, pwindow, 0);
  widget_flush(pwindow);
}

/**********************************************************************//**
  Close worklist from view.
**************************************************************************/
void popdown_worklist_editor(void)
{
  if (editor) {
    popdown_window_group_dialog(editor->begin_widget_list,
                                editor->end_widget_list);
    FC_FREE(editor->targets->scroll);
    FC_FREE(editor->targets);

    FC_FREE(editor->work->scroll);
    FC_FREE(editor->work);

    if (editor->global) {
      FC_FREE(editor->global->scroll);
      FC_FREE(editor->global);
    }

    if (city_dialog_is_open(editor->pcity)) {
      enable_city_dlg_widgets();
    }

    FC_FREE(editor);

    flush_dirty();
  }
}
