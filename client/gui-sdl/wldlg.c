/********************************************************************** 
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
/**********************************************************************
                          wldlg.c  -  description
                             -------------------
    begin                : Wed Sep 18 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <SDL/SDL.h>


#include "gui_mem.h"

#include "city.h"
#include "fcintl.h"
#include "game.h"


#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_zoom.h"
#include "gui_stuff.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "colors.h"

#include "helpdlg.h"
#include "inputdlg.h"

#include "packets.h"
#include "worklist.h"
#include "support.h"
#include "climisc.h"
#include "clinet.h"

#include "wldlg.h"
#include "citydlg.h"

#define TARGETS_COL		4
#define TARGETS_ROW		4

struct EDITOR {
  struct GUI *pBeginWidgetList;
  struct GUI *pEndWidgetList;/* window */
    
  struct ADVANCED_DLG *pTargets;
  struct ADVANCED_DLG *pWork;
  struct ADVANCED_DLG *pGlobal;
    
  struct city *pCity;
  struct worklist *pOrginal_WorkList;
  struct worklist *pCopy_WorkList;
  
  /* shortcuts  */
  struct GUI *pDock;
  struct GUI *pWorkList_Counter;
  
  struct GUI *pProduction_Name;
  struct GUI *pProduction_Progres;
    
  int stock;
  int currently_building;
  bool is_building_unit;
  
} *pEditor = NULL;


static int worklist_editor_item_callback(struct GUI *pWidget);
static SDL_Surface * get_progress_icon(int stock, int cost, int *progress);
static const char * get_production_name(struct city *pCity,
					  int id, bool is_unit, int *cost);
static void refresh_worklist_count_label(void);
static void refresh_production_label(int stock);

/* =========================================================== */

/* Worklist Editor Window Callback */
static int window_worklist_editor_callback(struct GUI *pWidget)
{
  return -1;
}

/* Popdwon Worklist Editor */
static int popdown_worklist_editor_callback(struct GUI *pWidget)
{
  if(pEditor) {
    popdown_window_group_dialog(pEditor->pBeginWidgetList,
					    pEditor->pEndWidgetList);
    FREE(pEditor->pTargets->pScroll);
    FREE(pEditor->pWork->pScroll);
    if(pEditor->pGlobal) {
      FREE(pEditor->pGlobal->pScroll);
      FREE(pEditor->pGlobal);
    }
    FREE(pEditor->pTargets);
    FREE(pEditor->pWork);
    FREE(pEditor->pCopy_WorkList);
        
    if(city_dialog_is_open(pEditor->pCity)) {
      enable_city_dlg_widgets();
    }
  
    FREE(pEditor);
    flush_dirty();
  }
  return -1;
}

/*
 * Commit changes to city/global worklist
 * In City Mode Remove Double entry of Imprv/Woder Targets from list.
 */
static int ok_worklist_editor_callback(struct GUI *pWidget)
{
  int i, j;
  struct city *pCity = pEditor->pCity;
  bool same_prod = TRUE;
  
  /* remove duplicate entry of impv./wonder target from worklist */
  for(i = 0; i < MAX_LEN_WORKLIST; i++) {
    if(pEditor->pCopy_WorkList->wlefs[i] == WEF_END) {
      break;
    }
    if(pEditor->pCopy_WorkList->wlefs[i] == WEF_IMPR) {
      for(j = i + 1; j < MAX_LEN_WORKLIST; j++) {
	if(pEditor->pCopy_WorkList->wlefs[j] == WEF_END) {
     	  break;
    	}
        if(pEditor->pCopy_WorkList->wlefs[j] == WEF_IMPR &&
	  pEditor->pCopy_WorkList->wlids[i] ==
				  pEditor->pCopy_WorkList->wlids[j]) {
	  worklist_remove(pEditor->pCopy_WorkList, j);
	}
      }
    }
  }
  
  if(pCity) {
    /* remove duplicate entry of currently building impv./wonder from worklist */
    if(!pEditor->is_building_unit) {
      for(i = 0; i < MAX_LEN_WORKLIST; i++) {
	if(pEditor->pCopy_WorkList->wlefs[i] == WEF_END) {
	  break;
        }
        if(pEditor->pCopy_WorkList->wlefs[i] == WEF_IMPR &&
          pEditor->pCopy_WorkList->wlids[i] == pEditor->currently_building) {
	    worklist_remove(pEditor->pCopy_WorkList, i);
        }
      }
    }
    
    /* change production */
    if(pEditor->is_building_unit != pCity->is_building_unit ||
       pEditor->currently_building != pCity->currently_building) {
      city_change_production(pCity, pEditor->is_building_unit,
			     pEditor->currently_building);
      same_prod = FALSE;
    }
    
    /* commit new city worklist */
    city_set_worklist(pCity, pEditor->pCopy_WorkList);
  } else {
    /* commit global worklist */
    copy_worklist(pEditor->pOrginal_WorkList, pEditor->pCopy_WorkList);
    update_worklist_report_dialog();
  }  
  
  /* popdown dlg */
  popdown_window_group_dialog(pEditor->pBeginWidgetList,
					    pEditor->pEndWidgetList);
  FREE(pEditor->pTargets->pScroll);
  FREE(pEditor->pWork->pScroll);
  if(pEditor->pGlobal) {
    FREE(pEditor->pGlobal->pScroll);
    FREE(pEditor->pGlobal);
  }
  FREE(pEditor->pTargets);
  FREE(pEditor->pWork);
  FREE(pEditor->pCopy_WorkList);
  FREE(pEditor);
  
  if(city_dialog_is_open(pCity)) {
    enable_city_dlg_widgets();
    if(same_prod) {
      flush_dirty();
    }
  } else {
    flush_dirty();
  }
  return -1;
}

/*
 * Rename Global Worklist
 */
static int rename_worklist_editor_callback(struct GUI *pWidget)
{
  if(pWidget->string16->text) {
    char *pText = convert_to_chars(pWidget->string16->text);
    my_snprintf(pEditor->pCopy_WorkList->name, MAX_LEN_NAME, "%s", pText);
    FREE(pText);
  }
  return -1;
}

/* ====================================================================== */

/*
 * Add target to worklist.
 */
static void add_target_to_worklist(struct GUI *pTarget)
{
  bool is_unit = FALSE;
  int i, target = MAX_ID - pTarget->ID;
  struct GUI *pBuf = NULL, *pDock = NULL;
  SDL_String16 *pStr = NULL;
  SDL_Surface *pDest = pTarget->dst;
  
  set_wstate(pTarget, FC_WS_SELLECTED);
  redraw_widget(pTarget);
  flush_rect(pTarget->size);
  
  if(target < 1000) {
    is_unit = TRUE;
  } else {
    target -= 1000;
  }
 
  /* Denny adding currently building Impr/Wonder Target */ 
  if(pEditor->pCity && !is_unit && !pEditor->is_building_unit &&
	    (target == pEditor->currently_building)) {
    return;
  }
  
  /* find first free place or imprvm. already in list */
  for(i = 0; i < MAX_LEN_WORKLIST; i++)
    if(pEditor->pCopy_WorkList->wlefs[i] == WEF_END || 
      (!is_unit && pEditor->pCopy_WorkList->wlefs[i] == WEF_IMPR &&
			    pEditor->pCopy_WorkList->wlids[i] == target)) {
      break;
    }
    
  /* there is no room or imprvm. is already on list */
  if(i >= MAX_LEN_WORKLIST - 1 || pEditor->pCopy_WorkList->wlefs[i] != WEF_END) {
    return;
  }
    
  pEditor->pCopy_WorkList->wlefs[i] = is_unit ? WEF_UNIT : WEF_IMPR;
  pEditor->pCopy_WorkList->wlids[i] = target;
  
  pEditor->pCopy_WorkList->wlefs[i + 1] = WEF_END;
  pEditor->pCopy_WorkList->wlids[i + 1] = 0;
  
  /* create widget entry */
  if(is_unit) {
    pStr = create_str16_from_char(get_unit_type(target)->name, 10);
  } else {
    pStr = create_str16_from_char(get_impr_name_ex(pEditor->pCity, target), 10);
  }
  pStr->style |= SF_CENTER;
  pBuf = create_iconlabel(NULL, pDest, pStr,
				(WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
    
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->action = worklist_editor_item_callback;
        
  pBuf->data.ptr = MALLOC(sizeof(int));
  *((int *)pBuf->data.ptr) = i;
    
  if(is_unit) {
    pBuf->ID = MAX_ID - target;
  } else {
    pBuf->ID = MAX_ID - 1000 - target;
  }
  
  if(pEditor->pWork->pBeginActiveWidgetList) {
    pDock = pEditor->pWork->pBeginActiveWidgetList;
  } else {
    pDock = pEditor->pDock;
  }
  
  add_widget_to_vertical_scroll_widget_list(pEditor->pWork, pBuf,
			pDock, FALSE,
			pEditor->pEndWidgetList->size.x + FRAME_WH + 2,
			pEditor->pEndWidgetList->size.y + FRAME_WH + 152);
  
  pBuf->size.w = 126;
  
  refresh_worklist_count_label();
  redraw_group(pEditor->pWork->pBeginWidgetList,
			  pEditor->pWork->pEndWidgetList, TRUE);
  flush_dirty();
}

/*
 * Find if two targets are the same class (unit, imprv. , wonder).
 * This is needed by callculation of change production shields penalty.
 */
static bool are_the_same_class(int id_1, bool is_id_1_unit, int id_2, bool is_id_2_unit)
{
  bool is_id_1_wonder = is_id_1_unit ? FALSE : is_wonder(id_1);
  bool is_id_2_wonder = is_id_2_unit ? FALSE : is_wonder(id_2);
  return ((is_id_1_unit && is_id_2_unit) || (is_id_1_wonder && is_id_2_wonder) ||
  	(!is_id_1_unit && !is_id_1_wonder && !is_id_2_unit && !is_id_2_wonder));
}

/*
 * Change production in editor shell, callculate production shields penalty and
 * refresh production progress label
 */
static void change_production(int target, bool is_unit)
{
    
  if(!are_the_same_class(pEditor->currently_building,
  		pEditor->is_building_unit, target, is_unit)) {
    if(pEditor->stock != pEditor->pCity->shield_stock) {
      if(are_the_same_class(pEditor->pCity->currently_building,
  		pEditor->pCity->is_building_unit, target, is_unit)) {
	pEditor->stock = pEditor->pCity->shield_stock;
      }
    } else {
      pEditor->stock =
	  city_change_production_penalty(pEditor->pCity,
					      target, is_unit, FALSE);
    }	  	  
  }
      
  pEditor->currently_building = target;
  pEditor->is_building_unit = is_unit;
  refresh_production_label(pEditor->stock);

  return;
}

/*
 * Change production of city but only in Editor.
 * You must commit this changes by exit editor via OK button (commit function).
 *
 * This function don't remove double imprv./wonder target entry from worklist
 * and allow more entry of such target (In Production and worklist - this is
 * fixed by commit function).
 */
static void add_target_to_production(struct GUI *pTarget)
{
  bool is_unit = FALSE;
  int target, dummy;
  assert(pTarget != NULL);
  
  /* redraw Target Icon */
  set_wstate(pTarget, FC_WS_SELLECTED);
  redraw_widget(pTarget);
  flush_rect(pTarget->size);
  
  /* decode target */
  target = MAX_ID - pTarget->ID;
  if(target < 1000) {
    is_unit = TRUE;
  } else {
    target -= 1000;
  }
  
  /* check if we change to the same target */
  if(((pEditor->currently_building == target) && 
  	(pEditor->is_building_unit == is_unit)) || pEditor->pCity->did_buy) {
    /* comit changes and exit - double click detection */
    ok_worklist_editor_callback(NULL);
    return;
  }
  
  change_production(target, is_unit);
  
  /* change Production Text Label in Worklist Widget list */
  copy_chars_to_string16(pEditor->pWork->pEndActiveWidgetList->string16,
  	get_production_name(pEditor->pCity, target, is_unit, &dummy));
  
  /* code Target ID */
  if(is_unit) {
    pEditor->pWork->pEndActiveWidgetList->ID = MAX_ID - target;
  } else {
    pEditor->pWork->pEndActiveWidgetList->ID = MAX_ID - 1000 - target;
  }
    
  redraw_widget(pEditor->pWork->pEndActiveWidgetList);
  sdl_dirty_rect(pEditor->pWork->pEndActiveWidgetList->size);
  
  flush_dirty();    
  
}

/* Get Help Info about target */
static void get_target_help_data(struct GUI *pTarget)
{
  bool is_unit = FALSE;
  int target;
  assert(pTarget != NULL);
  
  /* redraw Target Icon */
  set_wstate(pTarget, FC_WS_SELLECTED);
  redraw_widget(pTarget);
  /*flush_rect(pTarget->size);*/
  
  /* decode target */
  target = MAX_ID - pTarget->ID;
  if(target < 1000) {
    is_unit = TRUE;
  } else {
    target -= 1000;
  }

  if (is_unit)
  {
    popup_unit_info(target);
  } else {
    popup_impr_info(target);
  }
  
}


/*
 * Targets callback
 * left mouse button -> In city mode change production to target.
			In global mode add target to worklist.
 * middle mouse button -> get target "help"
 * right mouse button -> add target to worklist.
 */
static int worklist_editor_targets_callback(struct GUI *pWidget)
{
  switch(Main.event.button.button) {
    case SDL_BUTTON_LEFT:
      if(pEditor->pCity) {
        add_target_to_production(pWidget);
      } else {
	add_target_to_worklist(pWidget);
      }
      break;
    case SDL_BUTTON_MIDDLE:
      get_target_help_data(pWidget);
      break;
    case SDL_BUTTON_RIGHT:
      add_target_to_worklist(pWidget);
      break;
    default:
    	;/* do nothing */
    break;
  }
  return -1;
}

/* ====================================================================== */

/*
 * Remove element from worklist or
 * remove currently building imprv/unit and change production to first worklist
 * element (or Capitalizations if worklist is empty)
 */
static void remove_item_from_worklist(struct GUI *pItem)
{
  if(pItem->data.ptr) {
    /* correct "data" widget fiels */
    struct GUI *pBuf = pItem;
    if(pBuf != pEditor->pWork->pBeginActiveWidgetList) {
      do{
	pBuf = pBuf->prev;
	*((int *)pBuf->data.ptr) = *((int *)pBuf->data.ptr) - 1;
      } while(pBuf != pEditor->pWork->pBeginActiveWidgetList);
    }
    /* remove element from worklist */
    worklist_remove(pEditor->pCopy_WorkList, *((int *)pItem->data.ptr));
    /* remove widget from widget list */
    del_widget_from_vertical_scroll_widget_list(pEditor->pWork, pItem);
  } else {
    /* change production ... */
    if(!pEditor->pCity->did_buy) {
      if(worklist_is_empty(pEditor->pCopy_WorkList)) {
        /* there is no worklist */
        if(!(!pEditor->is_building_unit &&
	   (pEditor->currently_building == B_CAPITAL))) {
	  /* change to capitalization */
	  int dummy;   
	  change_production(B_CAPITAL, FALSE);
	  copy_chars_to_string16(pItem->string16,
	     get_production_name(pEditor->pCity, B_CAPITAL, FALSE, &dummy));
	  
          pItem->ID = MAX_ID - 1000 - B_CAPITAL;
        }
      } else {
        /* change productions to first worklist element */
        struct GUI *pBuf = pItem->prev;
        change_production(pEditor->pCopy_WorkList->wlids[0],
      			(pEditor->pCopy_WorkList->wlefs[0] == WEF_UNIT));
        worklist_advance(pEditor->pCopy_WorkList);
        del_widget_from_vertical_scroll_widget_list(pEditor->pWork, pItem);
        FREE(pBuf->data.ptr);
        if(pBuf != pEditor->pWork->pBeginActiveWidgetList) {
          do{
	    pBuf = pBuf->prev;
	    *((int *)pBuf->data.ptr) = *((int *)pBuf->data.ptr) - 1;
          } while(pBuf != pEditor->pWork->pBeginActiveWidgetList);
        }
      }
    }
  }
    
  refresh_worklist_count_label();
  redraw_group(pEditor->pWork->pBeginWidgetList,
			  pEditor->pWork->pEndWidgetList, TRUE);
  flush_dirty();
}

/*
 * Swap worklist elements DOWN.
 * Fuction swap current element with next element of worklist.
 *
 * If pItem is last widget or there is only one widget on widgets list
 * fuction remove this widget from widget list and target from worklist
 *
 * In City mode, when pItem is first worklist element, function make
 * change production (currently building is moved to first element of worklist
 * and first element of worklist is build).
 */
static void swap_item_down_from_worklist(struct GUI *pItem)
{
  int id;
  Uint16 *pText, ID;
  enum worklist_elem_flag flag;
  bool changed = FALSE;
  
  if(pItem == pEditor->pWork->pBeginActiveWidgetList) {
    remove_item_from_worklist(pItem);
    return;
  }
  
  pText = pItem->string16->text;
  ID = pItem->ID;
  
  if(pItem->data.ptr) {
    /* worklist operations -> swap down */
    int row = *((int *)pItem->data.ptr);
       
    flag = pEditor->pCopy_WorkList->wlefs[row];
    id = pEditor->pCopy_WorkList->wlids[row];
      
    pEditor->pCopy_WorkList->wlids[row] = pEditor->pCopy_WorkList->wlids[row + 1];
    pEditor->pCopy_WorkList->wlefs[row] = pEditor->pCopy_WorkList->wlefs[row + 1];
    pEditor->pCopy_WorkList->wlids[row + 1] = id;
    pEditor->pCopy_WorkList->wlefs[row + 1] = flag;
    changed = TRUE;  
  } else {
    /* change production ... */
    if(!pEditor->pCity->did_buy) {
      id = MAX_ID - ID;
    
      if(id < 1000) {
        flag = WEF_UNIT;
      } else {
        id -= 1000;
	flag = WEF_IMPR;
      }
      
      change_production(pEditor->pCopy_WorkList->wlids[0],
      			(pEditor->pCopy_WorkList->wlefs[0] == WEF_UNIT));
      pEditor->pCopy_WorkList->wlids[0] = id;
      pEditor->pCopy_WorkList->wlefs[0] = flag;
      changed = TRUE;
    }
  }
  
  if(changed) {
    pItem->string16->text = pItem->prev->string16->text;
    pItem->ID = pItem->prev->ID;
  
    pItem->prev->string16->text = pText;
    pItem->prev->ID = ID;

    redraw_group(pEditor->pWork->pBeginWidgetList,
			  pEditor->pWork->pEndWidgetList, TRUE);
    flush_dirty();
  }
  
}

/*
 * Swap worklist elements UP.
 * Fuction swap current element with prev. element of worklist.
 *
 * If pItem is first widget on widgets list fuction remove this widget 
 * from widget list and target from worklist (global mode)
 * or from production (city mode)
 *
 * In City mode, when pItem is first worklist element, function make
 * change production (currently building is moved to first element of worklist
 * and first element of worklist is build).
 */
static void swap_item_up_from_worklist(struct GUI *pItem)
{
  int id;
  Uint16 *pText = pItem->string16->text;
  Uint16 ID = pItem->ID;
  enum worklist_elem_flag flag;
  bool changed = FALSE;
  
  if(pItem == pEditor->pWork->pEndActiveWidgetList) {
    remove_item_from_worklist(pItem);
    return;
  }
    
  if(pItem->data.ptr && *((int *)pItem->data.ptr) > 0) {
    /* worklist operations -> swap up*/
    int row = *((int *)pItem->data.ptr);
    
    flag = pEditor->pCopy_WorkList->wlefs[row];
    id = pEditor->pCopy_WorkList->wlids[row];
      
    pEditor->pCopy_WorkList->wlids[row] = pEditor->pCopy_WorkList->wlids[row - 1];
    pEditor->pCopy_WorkList->wlefs[row] = pEditor->pCopy_WorkList->wlefs[row - 1];
    pEditor->pCopy_WorkList->wlids[row - 1] =  id;
    pEditor->pCopy_WorkList->wlefs[row - 1] = flag;
    changed = TRUE;    
  } else {
    if(!pEditor->pCity->did_buy) {
      /* change production ... */
      id = pEditor->currently_building;
    
      if(pEditor->is_building_unit) {
        flag = WEF_UNIT;
      } else {
        flag = WEF_IMPR;
      }
      
      change_production(pEditor->pCopy_WorkList->wlids[0],
      			(pEditor->pCopy_WorkList->wlefs[0] == WEF_UNIT));
      pEditor->pCopy_WorkList->wlids[0] = id;
      pEditor->pCopy_WorkList->wlefs[0] = flag;  
      changed = FALSE;
    }
  }
  
  if(changed) {
    pItem->string16->text = pItem->next->string16->text;
    pItem->ID = pItem->next->ID;
    
    pItem->next->string16->text = pText;
    pItem->next->ID = ID;
    
    redraw_group(pEditor->pWork->pBeginWidgetList,
			  pEditor->pWork->pEndWidgetList, TRUE);
    flush_dirty();
  }
}

/*
 * worklist callback
 * left mouse button -> swap elements up.
 * middle mouse button -> remove element from list
 * right mouse button -> swap elements down.
 */
static int worklist_editor_item_callback(struct GUI *pWidget)
{
  switch(Main.event.button.button) {
    case SDL_BUTTON_LEFT:
      swap_item_up_from_worklist(pWidget);
      break;
    case SDL_BUTTON_MIDDLE:
      remove_item_from_worklist(pWidget);
      break;
    case SDL_BUTTON_RIGHT:
      swap_item_down_from_worklist(pWidget);
      break;
    default:
    	;/* do nothing */
    break;
  }
  return -1;
}
/* ======================================================= */

/*
 * Add global worklist to city worklist starting from last free entry.
 * Add only avilable targets in current game state.
 * If global worklist have more targets that city worklist have free
 * elements then we adding only first part of global worklist.
 */
static void add_global_worklist(struct GUI *pWidget)
{
  if(!worklist_is_empty(&game.player_ptr->worklists[MAX_ID - pWidget->ID])) {
    SDL_Surface *pDest = pWidget->dst;
    int count, firstfree;
    struct GUI *pBuf = pEditor->pWork->pEndActiveWidgetList;
    struct worklist *pWorkList = &game.player_ptr->worklists[MAX_ID - pWidget->ID];
      
    /* find first free element in worklist */
    for(count = 0; count < MAX_LEN_WORKLIST; count++) {
      if(pEditor->pCopy_WorkList->wlefs[count] == WEF_END) {
	break;
      }
    }
    
    if(count >= MAX_LEN_WORKLIST - 1) {
      /* worklist is full */
      return;
    }
    
    firstfree = count;
    /* copy global worklist to city worklist */
    for(count = 0 ; count < MAX_LEN_WORKLIST; count++) {
      
      /* last element */
      if(pWorkList->wlefs[count] == WEF_END) {
	break;
      }
      
      /* global worklist can have targets unavilable in current state of game
         then we must remove those targets from new city worklist */
      if(((pWorkList->wlefs[count] == WEF_UNIT) &&
	  !can_eventually_build_unit(pEditor->pCity, pWorkList->wlids[count])) ||
      	((pWorkList->wlefs[count] == WEF_IMPR) &&
	  !can_eventually_build_improvement(pEditor->pCity, pWorkList->wlids[count]))) {
	continue;  
      }
      
      pEditor->pCopy_WorkList->wlids[firstfree] = pWorkList->wlids[count];
      pEditor->pCopy_WorkList->wlefs[firstfree] = pWorkList->wlefs[count];
      
      /* create widget */      
      if(pWorkList->wlefs[count] == WEF_UNIT) {
	pBuf = create_iconlabel(NULL, pDest,
		create_str16_from_char(
			get_unit_type(pWorkList->wlids[count])->name, 10),
				(WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
	pBuf->ID = MAX_ID - pWorkList->wlids[count];
      } else {
	pBuf = create_iconlabel(NULL, pDest,
		create_str16_from_char(
			get_impr_name_ex(pEditor->pCity,
				pWorkList->wlids[count]), 10),
				   (WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
	pBuf->ID = MAX_ID - 1000 - pWorkList->wlids[count];
      }
      
      pBuf->string16->style |= SF_CENTER;
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = worklist_editor_item_callback;
      pBuf->size.w = 126;  
      pBuf->data.ptr = MALLOC(sizeof(int));
      *((int *)pBuf->data.ptr) = firstfree;
        
      add_widget_to_vertical_scroll_widget_list(pEditor->pWork,
			pBuf, pEditor->pWork->pBeginActiveWidgetList, FALSE,
      			pEditor->pEndWidgetList->size.x + FRAME_WH + 2,
			pEditor->pEndWidgetList->size.y + FRAME_WH + 152);
      
      firstfree++;
      if(firstfree == MAX_LEN_WORKLIST - 1) {
	break;
      }
    }
    
    if(firstfree < MAX_LEN_WORKLIST) {
      pEditor->pCopy_WorkList->wlids[firstfree] = 0;
      pEditor->pCopy_WorkList->wlefs[firstfree] = WEF_END;
    }
    
    refresh_worklist_count_label();
    redraw_group(pEditor->pWork->pBeginWidgetList,
    			pEditor->pWork->pEndWidgetList, TRUE);
    
    flush_dirty();
  }
}

/*
 * Clear city worklist and copy here global worklist.
 * Copy only avilable targets in current game state.
 * If all targets are unavilable then leave city worklist untouched.
 */
static void set_global_worklist(struct GUI *pWidget)
{
  if(!worklist_is_empty(&game.player_ptr->worklists[MAX_ID - pWidget->ID])) {
    SDL_Surface *pDest = pWidget->dst;
    int count, target , wl_count;
    bool is_unit;
    struct GUI *pBuf = pEditor->pWork->pEndActiveWidgetList;
    struct worklist wl ,
	      *pWorkList = &game.player_ptr->worklists[MAX_ID - pWidget->ID];
    
    /* clear tmp worklist */
    init_worklist(&wl);
    
    wl_count = 0;
    /* copy global worklist to city worklist */
    for(count = 0; count < MAX_LEN_WORKLIST; count++) {
      
      /* last element */
      if(pWorkList->wlefs[count] == WEF_END) {
	break;
      }
      
      /* global worklist can have targets unavilable in current state of game
         then we must remove those targets from new city worklist */
      if(((pWorkList->wlefs[count] == WEF_UNIT) &&
	  !can_eventually_build_unit(pEditor->pCity, pWorkList->wlids[count])) ||
      	((pWorkList->wlefs[count] == WEF_IMPR) &&
	  !can_eventually_build_improvement(pEditor->pCity, pWorkList->wlids[count]))) {
	continue;  
      }
      
      wl.wlids[wl_count] = pWorkList->wlids[count];
      wl.wlefs[wl_count] = pWorkList->wlefs[count];
      wl_count++;
    }
    /* --------------------------------- */
    
    if(!worklist_is_empty(&wl)) {
      /* free old widget list */
      if(pBuf != pEditor->pWork->pBeginActiveWidgetList) {
        pBuf = pBuf->prev;
        if(pBuf != pEditor->pWork->pBeginActiveWidgetList) {
          do {
            pBuf = pBuf->prev;
            del_widget_from_vertical_scroll_widget_list(pEditor->pWork, pBuf->next);
          } while(pBuf != pEditor->pWork->pBeginActiveWidgetList);
        }
        del_widget_from_vertical_scroll_widget_list(pEditor->pWork, pBuf);
      }  
      /* --------------------------------- */
      
      copy_worklist(pEditor->pCopy_WorkList, &wl);
    
      /* --------------------------------- */
      /* create new widget list */
      for(count = 0; count < MAX_LEN_WORKLIST; count++) {
        /* end of list */
        if(!worklist_peek_ith(pEditor->pCopy_WorkList, &target, &is_unit, count)) {
          break;
        }
    
        if(is_unit) {
	  pBuf = create_iconlabel(NULL, pDest,
		create_str16_from_char(get_unit_type(target)->name, 10),
				(WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
	  pBuf->ID = MAX_ID - target;
        } else {
	  pBuf = create_iconlabel(NULL, pDest,
	  create_str16_from_char(get_impr_name_ex(pEditor->pCity, target), 10),
				(WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
	  pBuf->ID = MAX_ID -1000 - target;
        }
        pBuf->string16->style |= SF_CENTER;
        set_wstate(pBuf, FC_WS_NORMAL);
        pBuf->action = worklist_editor_item_callback;
        pBuf->size.w = 126;  
        pBuf->data.ptr = MALLOC(sizeof(int));
        *((int *)pBuf->data.ptr) = count;
        
        add_widget_to_vertical_scroll_widget_list(pEditor->pWork,
			pBuf, pEditor->pWork->pBeginActiveWidgetList, FALSE,
      			pEditor->pEndWidgetList->size.x + FRAME_WH + 2,
			pEditor->pEndWidgetList->size.y + FRAME_WH + 152);
      }
    
      refresh_worklist_count_label();
      redraw_group(pEditor->pWork->pBeginWidgetList,
    			pEditor->pWork->pEndWidgetList, TRUE);
    
      flush_dirty();
    }
  }
}

/*
 * global worklist callback
 * left mouse button -> add global worklist to current city list
 * right mouse button -> clear city worklist and copy here global worklist.
 *
 * There are problems with impv./wonder tagets becouse those can't be doubled
 * on worklist and adding/seting can give you situation that global worklist
 * have imprv./wonder entry that exist on city worklist or in building state.
 * I don't make such check here and allow this "functionality" becouse doubled
 * impov./wonder entry are removed from city worklist during "commit" phase.
 */
static int global_worklist_callback(struct GUI *pWidget)
{
  switch(Main.event.button.button) {
    case SDL_BUTTON_LEFT:
      add_global_worklist(pWidget);
      break;
    case SDL_BUTTON_MIDDLE:
      /* nothing */
      break;
    case SDL_BUTTON_RIGHT:
      set_global_worklist(pWidget);
      break;
    default:
    	;/* do nothing */
    break;
  }
  return -1;
}

/* ======================================================= */

/* return full unit/imprv. name and build cost in "cost" pointer */
static const char * get_production_name(struct city *pCity,
					  int id, bool is_unit, int *cost)
{
  assert(cost != NULL);
        
  if(is_unit) {
    struct unit_type *pType = get_unit_type(id);
    *cost = unig_build_value(id);
    return pType->name;
  } else {
    *cost = impr_build_shield_cost(id);
    return get_impr_name_ex(pCity, id);
  }
}

/*
 * return progress icon (bar) of current production state
 * stock - current shielsd stocks
 * cost - unit/imprv. cost
 * function return in "proggres" pointer (0 - 100 %) progress in numbers
*/
static SDL_Surface * get_progress_icon(int stock, int cost, int *progress)
{
  SDL_Surface *pIcon = NULL;
  int width;
  assert(progress != NULL);
  
  if(stock < cost) {
    width = ((float)stock / cost) * 116.0;
    *progress = ((float)stock / cost) * 100.0;
    if(!width && stock) {
      *progress = 1;
      width = 1;
    }
  } else {
    width = 116;
    *progress = 100;
  }
    
  pIcon = create_bcgnd_surf(pTheme->Edit, 1, 0, 120, 30);
    
  if(width) {
    SDL_Rect dst = {2,1,0,0};
    SDL_Surface *pBuf = create_bcgnd_surf(pTheme->Button, 1, 3, width, 28);
    SDL_BlitSurface(pBuf, NULL, pIcon, &dst);
    FREESURFACE(pBuf);
  }
    
  return pIcon;  
}

/*
 * update and redraw production name label in worklist editor
 * stock - pCity->shields_stock or current stock after chnge production lost.
 */
static void refresh_production_label(int stock)
{
  int cost, turns;
  char cBuf[64];
  SDL_Rect area;
  const char *name = get_production_name(pEditor->pCity,
    				pEditor->currently_building,
    				pEditor->is_building_unit, &cost);

  if (!pEditor->is_building_unit
     && pEditor->currently_building == B_CAPITAL)
  {
     my_snprintf(cBuf, sizeof(cBuf),
      	_("%s\n%d gold per turn"), name, MAX(0, pEditor->pCity->shield_surplus));
  } else {
    if(stock < cost) {
      turns = city_turns_to_build(pEditor->pCity,
    	pEditor->currently_building, pEditor->is_building_unit, TRUE);
      if(turns == 999)
      {
        my_snprintf(cBuf, sizeof(cBuf), _("%s\nblocked!"), name);
      } else {
        my_snprintf(cBuf, sizeof(cBuf), _("%s\n%d %s"),
		    name, turns, PL_("turn", "turns", turns));
      }
    } else {
      my_snprintf(cBuf, sizeof(cBuf), _("%s\nfinished!"), name);
    }
  }
  copy_chars_to_string16(pEditor->pProduction_Name->string16, cBuf);
  
  blit_entire_src(pEditor->pProduction_Name->gfx,
		  	pEditor->pProduction_Name->dst,
  			pEditor->pProduction_Name->size.x,
  			pEditor->pProduction_Name->size.y);
  
  remake_label_size(pEditor->pProduction_Name);
  
  pEditor->pProduction_Name->size.x = pEditor->pEndWidgetList->size.x +
		(130 - pEditor->pProduction_Name->size.w)/2 + FRAME_WH;
  
  area.x = pEditor->pEndWidgetList->size.x + FRAME_WH;
  area.y = pEditor->pProduction_Name->size.y;
  area.w = 130;
  area.h = pEditor->pProduction_Name->size.h;
  
  refresh_widget_background(pEditor->pProduction_Name);
  
  redraw_label(pEditor->pProduction_Name);
  sdl_dirty_rect(area);
  
  FREESURFACE(pEditor->pProduction_Progres->theme);
  pEditor->pProduction_Progres->theme =
		  get_progress_icon(stock, cost, &cost);
    
  my_snprintf(cBuf, sizeof(cBuf), "%d%%" , cost);
  copy_chars_to_string16(pEditor->pProduction_Progres->string16, cBuf);
  redraw_label(pEditor->pProduction_Progres);
  sdl_dirty_rect(pEditor->pProduction_Progres->size);
}


/* update and redraw worklist length counter in worklist editor */
static void refresh_worklist_count_label(void)
{
  char cBuf[64];
  SDL_Rect area;
  
  my_snprintf(cBuf, sizeof(cBuf), _("( %d elements )"),
  				worklist_length(pEditor->pCopy_WorkList));
  copy_chars_to_string16(pEditor->pWorkList_Counter->string16, cBuf);

  blit_entire_src(pEditor->pWorkList_Counter->gfx,
		  	pEditor->pWorkList_Counter->dst,
  			pEditor->pWorkList_Counter->size.x,
  			pEditor->pWorkList_Counter->size.y);
  
  remake_label_size(pEditor->pWorkList_Counter);
  
  pEditor->pWorkList_Counter->size.x = pEditor->pEndWidgetList->size.x +
		(130 - pEditor->pWorkList_Counter->size.w)/2 + FRAME_WH;
  
  refresh_widget_background(pEditor->pWorkList_Counter);
  
  redraw_label(pEditor->pWorkList_Counter);
  
  area.x = pEditor->pEndWidgetList->size.x + FRAME_WH;
  area.y = pEditor->pWorkList_Counter->size.y;
  area.w = 130;
  area.h = pEditor->pWorkList_Counter->size.h;
  sdl_dirty_rect(area);
  
}


/* ====================================================================== */


/*
 * Global/City worklist editor.
 * if pCity == NULL then fucnction take pWorklist as global worklist.
 * pWorklist must be not NULL.
 */
void popup_worklist_editor(struct city *pCity, struct worklist *pWorkList)
{
  int count = 0, turns;
  int i, w, h, widget_w = 0, widget_h = 0;
  SDL_String16 *pStr = NULL;
  struct GUI *pBuf = NULL, *pWindow, *pLast;
  SDL_Surface *pText = NULL, *pText_Name = NULL, *pZoom = NULL;
  SDL_Surface *pMain = create_surf(116, 116, SDL_SWSURFACE);
  SDL_Surface *pIcon, *pDest;
  SDL_Color color = {255,255,255,128};
  SDL_Rect dst;
  char cBuf[128];
  struct unit_type *pUnit = NULL;
  struct impr_type *pImpr = NULL;  
  char *state = NULL;
  bool advanced_tech = pCity == NULL;
  bool can_build, can_eventually_build;
  
  if(pEditor) {
    return;
  }
  
  assert(pWorkList != NULL);
  
  pEditor = MALLOC(sizeof(struct EDITOR));
  
  pEditor->pCity = pCity;
  pEditor->pOrginal_WorkList = pWorkList;
  pEditor->pCopy_WorkList = MALLOC(sizeof(struct worklist));
  copy_worklist(pEditor->pCopy_WorkList, pWorkList);
  
  if(pCity) {
    pEditor->is_building_unit = pCity->is_building_unit;
    pEditor->currently_building = pCity->currently_building;
    pEditor->stock = pCity->shield_stock;
  }
  
  /* --------------- */
  /* create Target Background Icon */
  pIcon = SDL_DisplayFormatAlpha(pMain);
  SDL_FillRect(pIcon, NULL, SDL_MapRGBA(pIcon->format, color.r,
					    color.g, color.b, color.unused));
  putframe(pIcon, 0, 0, pIcon->w - 1, pIcon->h - 1, 0xFF000000);
  FREESURFACE(pMain);
  pMain = pIcon;
  pIcon = NULL;
    
  /* ---------------- */
  /* Create Main Window */
  pWindow = create_window(NULL, NULL, 10, 10, 0);
  pWindow->action = window_worklist_editor_callback;
  pDest = pWindow->dst;
  w = pWindow->size.w;
  h = pWindow->size.h;
  set_wstate(pWindow, FC_WS_NORMAL);
  
  add_to_gui_list(ID_WINDOW, pWindow);
  pEditor->pEndWidgetList = pWindow;
  
  /* ---------------- */
  if(pCity) {
    my_snprintf(cBuf, sizeof(cBuf), _("Worklist of\n%s"), pCity->name);
  } else {
    my_snprintf(cBuf, sizeof(cBuf), "%s", pWorkList->name);
  }
    
  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  
  pBuf = create_iconlabel(NULL, pDest, pStr, 0);
  
  add_to_gui_list(ID_LABEL, pBuf);
  /* --------------------------- */
  
  count = worklist_length(pWorkList);
  my_snprintf(cBuf, sizeof(cBuf), _("( %d elements )"), count);
  count = 0;
  pStr = create_str16_from_char(cBuf, 10);
  pBuf = create_iconlabel(NULL, pDest, pStr, WF_DRAW_THEME_TRANSPARENT);
  pEditor->pWorkList_Counter = pBuf;
  add_to_gui_list(ID_LABEL, pBuf);
  /* --------------------------- */
  /* create production proggres label or rename worklist edit */
  if(pCity) {
    /* count == cost */
    /* turns == progress */
    const char *name = get_production_name(pCity,
    				pCity->currently_building,
    				pCity->is_building_unit, &count);
    
    if (!pCity->is_building_unit && pCity->currently_building == B_CAPITAL)
    {
      my_snprintf(cBuf, sizeof(cBuf),
      	_("%s\n%d gold per turn"), name, MAX(0, pCity->shield_surplus));
    } else {
      if(pCity->shield_stock < count) {
        turns = city_turns_to_build(pCity,
    	  pCity->currently_building, pCity->is_building_unit, TRUE);
        if(turns == 999)
        {
          my_snprintf(cBuf, sizeof(cBuf), _("%s\nblocked!"), name);
        } else {
          my_snprintf(cBuf, sizeof(cBuf), _("%s\n%d %s"),
		    name, turns, PL_("turn", "turns", turns));
        }
      } else {
        my_snprintf(cBuf, sizeof(cBuf), _("%s\nfinished!"), name);
      }
    }
    pStr = create_str16_from_char(cBuf, 10);
    pStr->style |= SF_CENTER;
    pBuf = create_iconlabel(NULL, pDest, pStr, WF_DRAW_THEME_TRANSPARENT);
    
    pEditor->pProduction_Name = pBuf;
    add_to_gui_list(ID_LABEL, pBuf);
    
    pIcon = get_progress_icon(pCity->shield_stock, count, &turns);
    
    my_snprintf(cBuf, sizeof(cBuf), "%d%%" , turns);
    pStr = create_str16_from_char(cBuf, 12);
    pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    
    pBuf = create_iconlabel(pIcon, pDest, pStr,
    		(WF_DRAW_THEME_TRANSPARENT|WF_ICON_CENTER|WF_FREE_THEME));
    
    pIcon = NULL;
    turns = 0;
    pEditor->pProduction_Progres = pBuf;
    add_to_gui_list(ID_LABEL, pBuf);
  } else {
    pBuf = create_edit_from_chars(NULL, pDest,  pWorkList->name, 10, 120,
						WF_DRAW_THEME_TRANSPARENT);
    pBuf->action = rename_worklist_editor_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    add_to_gui_list(ID_EDIT, pBuf);
  }
  
  /* --------------------------- */
  /* Commit Widget */
  pBuf = create_themeicon(pTheme->OK_Icon, pDest, WF_DRAW_THEME_TRANSPARENT);
  
  pBuf->action = ok_worklist_editor_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_RETURN;
    
  add_to_gui_list(ID_BUTTON, pBuf);
  /* --------------------------- */
  /* Cancel Widget */
  pBuf = create_themeicon(pTheme->CANCEL_Icon, pDest,
				  WF_DRAW_THEME_TRANSPARENT);
  
  pBuf->action = popdown_worklist_editor_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
    
  add_to_gui_list(ID_BUTTON, pBuf);
  /* --------------------------- */
  /* work list */
  
  /*
     pWidget->data filed will contains postion of target in worklist all
     action on worklist (swap/romove/add) must correct this fields
     
     Production Widget Label in worklist Widget list
     will have this field NULL
  
     turns == unit/impv. id 
     can_build == is_unit 
  */
  
  pEditor->pWork = MALLOC(sizeof(struct ADVANCED_DLG));
  pEditor->pWork->pScroll = MALLOC(sizeof(struct ScrollBar));
  pEditor->pWork->pScroll->count = 0;
  pEditor->pWork->pScroll->active = MAX_LEN_WORKLIST;
  pEditor->pWork->pScroll->step = 1;
  
  if(pCity) {
   /* Production Widget Label */ 
    pStr = create_str16_from_char(get_production_name(pCity,
    				pCity->currently_building,
    				pCity->is_building_unit, &turns), 10);
    pStr->style |= SF_CENTER;
    pBuf = create_iconlabel(NULL, pDest, pStr, WF_DRAW_THEME_TRANSPARENT);
    
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->action = worklist_editor_item_callback;
        
    if(pCity->is_building_unit) {
      add_to_gui_list(MAX_ID - pCity->currently_building, pBuf);
    } else {
      add_to_gui_list(MAX_ID - 1000 - pCity->currently_building, pBuf);
    }
    
    pEditor->pWork->pEndWidgetList = pBuf;
    pEditor->pWork->pBeginWidgetList = pBuf;
    pEditor->pWork->pEndActiveWidgetList = pBuf;
    pEditor->pWork->pBeginActiveWidgetList = pBuf;
    pEditor->pWork->pScroll->count++;
  }
  
  pLast = pBuf;
  pEditor->pDock = pBuf;
  /* create Widget Labels of worklist elements */
  for(count = 0; count < MAX_LEN_WORKLIST; count++) {
    /* end of list */
    if(!worklist_peek_ith(pWorkList, &turns, &can_build, count)) {
      break;
    }
    
    if(can_build) {
      pStr = create_str16_from_char(get_unit_type(turns)->name, 10);
    } else {
      pStr = create_str16_from_char(get_impr_name_ex(pCity, turns), 10);
    }
    pStr->style |= SF_CENTER;
    pBuf = create_iconlabel(NULL, pDest, pStr,
				(WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
    
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->action = worklist_editor_item_callback;
        
    pBuf->data.ptr = MALLOC(sizeof(int));
    *((int *)pBuf->data.ptr) = count;
    
    if(can_build) {
      add_to_gui_list(MAX_ID - turns, pBuf);
    } else {
      add_to_gui_list(MAX_ID - 1000 - turns, pBuf);
    }
    
  }
  
  if(count) {
    if(!pCity) {
      pEditor->pWork->pEndWidgetList = pLast->prev;
      pEditor->pWork->pEndActiveWidgetList = pLast->prev;
    }
    pEditor->pWork->pBeginWidgetList = pBuf;
    pEditor->pWork->pBeginActiveWidgetList = pBuf;
  } else {
    if(!pCity) {
      pEditor->pWork->pEndWidgetList = pLast;
    }
    pEditor->pWork->pBeginWidgetList = pLast;
  }
  
  pEditor->pWork->pScroll->count += count;
  pLast = pEditor->pWork->pBeginWidgetList;
  
  /* --------------------------- */
  /* global worklists */
  if(pCity) {
    count = 0;
    for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
      if (game.player_ptr->worklists[i].is_valid) {
        pBuf = create_iconlabel_from_chars(NULL, pDest, 
      		game.player_ptr->worklists[i].name, 10,
					      WF_DRAW_THEME_TRANSPARENT);
        set_wstate(pBuf, FC_WS_NORMAL);
        add_to_gui_list(MAX_ID - i, pBuf);
        pBuf->string16->style |= SF_CENTER;
        pBuf->action = global_worklist_callback;
        pBuf->string16->fgcol = color;
	
        count++;
    
        if(count>4) {
	  set_wflag(pBuf, WF_HIDDEN);
        }
      }
    }
  
    if(count) {
      pEditor->pGlobal = MALLOC(sizeof(struct ADVANCED_DLG));
      pEditor->pGlobal->pEndWidgetList = pLast->prev;
      pEditor->pGlobal->pEndActiveWidgetList = pLast->prev;
      pEditor->pGlobal->pBeginWidgetList = pBuf;
      pEditor->pGlobal->pBeginActiveWidgetList = pBuf;
    
      if(count > 6) {
        pEditor->pGlobal->pActiveWidgetList = pLast->prev;
        pEditor->pGlobal->pScroll = MALLOC(sizeof(struct ScrollBar));
        pEditor->pGlobal->pScroll->count = count;
        pEditor->pGlobal->pScroll->active = 4;
        pEditor->pGlobal->pScroll->step = 1;
      	
        create_vertical_scrollbar(pEditor->pGlobal, 1, 4, FALSE, TRUE);
	pEditor->pGlobal->pScroll->pUp_Left_Button->size.w = 122;
	pEditor->pGlobal->pScroll->pDown_Right_Button->size.w = 122;
      } else {
	struct GUI *pTmp = pLast;
	do {
	  pTmp = pTmp->prev;
	  clear_wflag(pTmp, WF_HIDDEN);
	} while (pTmp != pBuf);	  
      }
    
      pLast = pEditor->pGlobal->pBeginWidgetList;
    }
  }
  /* ----------------------------- */
  count = 0;
  /* Targets units and imprv. to build */
  pStr = create_string16(NULL, 0, 10);
  pStr->style |= (SF_CENTER|TTF_STYLE_BOLD);
  pStr->render = 3;
  pStr->bgcol = color;
    
  impr_type_iterate(imp) {
    can_build = can_player_build_improvement(game.player_ptr, imp);
    can_eventually_build =
	could_player_eventually_build_improvement(game.player_ptr, imp);
    
    /* If there's a city, can the city build the improvement? */
    if (pCity) {
      can_build = can_build && can_build_improvement(pCity, imp);
      can_eventually_build = can_eventually_build &&
	  can_eventually_build_improvement(pCity, imp);
    }
    
    if ((advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build)) {

      pImpr = get_improvement_type(imp);
      
      pIcon = crop_rect_from_surface(pMain, NULL);
      
      my_snprintf(cBuf, sizeof(cBuf), "%s", pImpr->name);
      copy_chars_to_string16(pStr, cBuf);
      pStr->style |= TTF_STYLE_BOLD;
      pText_Name = create_text_surf_smaller_that_w(pStr, pIcon->w - 4);
      SDL_SetAlpha(pText_Name, 0x0, 0x0);
  
      if (is_wonder(imp)) {
        if (wonder_obsolete(imp)) {
          state = _("Obsolete");
        } else {
          if (game.global_wonders[imp] != 0) {
	    state = _("Built");
          } else {
	    state = _("Wonder");
          }
        }
      } else {
        state = NULL;
      }
  
      if(pCity) {
        if(imp != B_CAPITAL) {
          turns = city_turns_to_build(pCity, imp, FALSE, TRUE);
            
          if (turns == FC_INFINITY) {
	    if(state) {
              my_snprintf(cBuf, sizeof(cBuf), _("(%s)\n%d/%d %s\n%s"),
			  state, pCity->shield_stock,
			  impr_build_shield_cost(imp),
			  PL_("shield", "shields",
			      impr_build_shield_cost(imp)),
			  _("never"));
	    } else {
	      my_snprintf(cBuf, sizeof(cBuf), _("%d/%d %s\n%s"),
			  pCity->shield_stock, impr_build_shield_cost(imp),
			  PL_("shield","shields",
			      impr_build_shield_cost(imp)), _("never"));
	    }	  
          } else {
            if (state) {
	      my_snprintf(cBuf, sizeof(cBuf), _("(%s)\n%d/%d %s\n%d %s"),
			  state, pCity->shield_stock,
			  impr_build_shield_cost(imp),
			  PL_("shield","shields",
			      impr_build_shield_cost(imp)),
			  turns, PL_("turn", "turns", turns));
            } else {
	      my_snprintf(cBuf, sizeof(cBuf), _("%d/%d %s\n%d %s"),
			  pCity->shield_stock, impr_build_shield_cost(imp),
			  PL_("shield","shields",
			      impr_build_shield_cost(imp)),
			  turns, PL_("turn", "turns", turns));
            }
          }
        } else {
          /* capitalization */
          my_snprintf(cBuf, sizeof(cBuf), _("%d gold per turn"),
		    MAX(0, pCity->shield_surplus));
        }
      } else {
        /* non city mode */
        if(imp != B_CAPITAL) {
          if(state) {
            my_snprintf(cBuf, sizeof(cBuf), _("(%s)\n%d %s"),
			state, impr_build_shield_cost(imp),
			PL_("shield","shields",
			    impr_build_shield_cost(imp)));
          } else {
	    my_snprintf(cBuf, sizeof(cBuf), _("%d %s"),
			impr_build_shield_cost(imp),
			PL_("shield","shields",
			    impr_build_shield_cost(imp)));
          }
        } else {
          my_snprintf(cBuf, sizeof(cBuf), _("shields into gold"));
        }
      }
  
      copy_chars_to_string16(pStr, cBuf);
      pStr->style &= ~TTF_STYLE_BOLD;
  
      pText = create_text_surf_from_str16(pStr);
      SDL_SetAlpha(pText, 0x0, 0x0);
      /*-----------------*/
  
      pZoom = ZoomSurface(GET_SURF(pImpr->sprite), 1.5, 1.5, 1);
      dst.x = (pIcon->w - pZoom->w)/2;
      dst.y = (pIcon->h/2 - pZoom->h)/2;
      SDL_BlitSurface(pZoom, NULL, pIcon, &dst);
      dst.y += pZoom->h;
      FREESURFACE(pZoom);
  
      dst.x = (pIcon->w - pText_Name->w)/2;
      dst.y += ((pIcon->h - dst.y) - (pText_Name->h + pText->h))/2;
      SDL_BlitSurface(pText_Name, NULL, pIcon, &dst);

      dst.x = (pIcon->w - pText->w)/2;
      dst.y += pText_Name->h;
      SDL_BlitSurface(pText, NULL, pIcon, &dst);
  
      FREESURFACE(pText);
      FREESURFACE(pText_Name);
            
      pBuf = create_icon2(pIcon, pDest,
    				WF_DRAW_THEME_TRANSPARENT|WF_FREE_THEME);
      set_wstate(pBuf, FC_WS_NORMAL);
    
      widget_w = MAX(widget_w, pBuf->size.w);
      widget_h = MAX(widget_h, pBuf->size.h);
    
      pBuf->data.city = pCity;
      add_to_gui_list(MAX_ID - 1000 - imp, pBuf);
      pBuf->action = worklist_editor_targets_callback;
      
      if(count > (TARGETS_ROW * TARGETS_COL - 1)) {
        set_wflag(pBuf, WF_HIDDEN);
      }
      count++;
    }
  } impr_type_iterate_end;
  /* ------------------------------ */
  unit_type_iterate(un) {
    can_build = can_player_build_unit(game.player_ptr, un);
    can_eventually_build =
	can_player_eventually_build_unit(game.player_ptr, un);

    /* If there's a city, can the city build the unit? */
    if (pCity) {
      can_build = can_build && can_build_unit(pCity, un);
      can_eventually_build = can_eventually_build &&
	  		can_eventually_build_unit(pCity, un);
    }

    if ((advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build)) {
	  
      pUnit = get_unit_type(un);
	
      pIcon = crop_rect_from_surface(pMain, NULL);
      
      my_snprintf(cBuf, sizeof(cBuf), "%s", pUnit->name);
  
      copy_chars_to_string16(pStr, cBuf);
      pStr->style |= TTF_STYLE_BOLD;
      pText_Name = create_text_surf_smaller_that_w(pStr, pIcon->w - 4);
      SDL_SetAlpha(pText_Name, 0x0, 0x0);
  
      if (pCity) {
        turns = city_turns_to_build(pCity, un, TRUE, TRUE);
        if (turns == FC_INFINITY) {
          my_snprintf(cBuf, sizeof(cBuf),
		    _("(%d/%d/%d)\n%d/%d %s\nnever"),
		    pUnit->attack_strength,
		    pUnit->defense_strength, pUnit->move_rate / SINGLE_MOVE,
		    pCity->shield_stock, unit_build_shield_cost(un),
	  	    PL_("shield","shields", unit_build_shield_cost(un)));
        } else {
          my_snprintf(cBuf, sizeof(cBuf),
		    _("(%d/%d/%d)\n%d/%d %s\n%d %s"),
		    pUnit->attack_strength,
		    pUnit->defense_strength, pUnit->move_rate / SINGLE_MOVE,
		    pCity->shield_stock, unit_build_shield_cost(un), 
	  	    PL_("shield","shields", unit_build_shield_cost(un)),
		    turns, PL_("turn", "turns", turns));
        }
      } else {
        my_snprintf(cBuf, sizeof(cBuf),
		    _("(%d/%d/%d)\n%d %s"),
		    pUnit->attack_strength,
		    pUnit->defense_strength, pUnit->move_rate / SINGLE_MOVE,
		    unit_build_shield_cost(un),
		    PL_("shield","shields", unit_build_shield_cost(un)));
      }

      copy_chars_to_string16(pStr, cBuf);
      pStr->style &= ~TTF_STYLE_BOLD;
  
      pText = create_text_surf_from_str16(pStr);
      SDL_SetAlpha(pText, 0x0, 0x0);
  
      pZoom = make_flag_surface_smaler(GET_SURF(pUnit->sprite));
      dst.x = (pIcon->w - pZoom->w)/2;
      dst.y = (pIcon->h/2 - pZoom->h)/2;
      SDL_BlitSurface(pZoom, NULL, pIcon, &dst);
      FREESURFACE(pZoom);
  
      dst.x = (pIcon->w - pText_Name->w)/2;
      dst.y = pIcon->h/2 + (pIcon->h/2 - (pText_Name->h + pText->h))/2;
      SDL_BlitSurface(pText_Name, NULL, pIcon, &dst);

      dst.x = (pIcon->w - pText->w)/2;
      dst.y += pText_Name->h;
      SDL_BlitSurface(pText, NULL, pIcon, &dst);
  
      FREESURFACE(pText);
      FREESURFACE(pText_Name);
      
      pBuf = create_icon2(pIcon, pDest,
    				WF_DRAW_THEME_TRANSPARENT|WF_FREE_THEME);
      set_wstate(pBuf, FC_WS_NORMAL);
    
      widget_w = MAX(widget_w, pBuf->size.w);
      widget_h = MAX(widget_h, pBuf->size.h);
    
      pBuf->data.city = pCity;
      add_to_gui_list(MAX_ID - un, pBuf);
      pBuf->action = worklist_editor_targets_callback;
      
      if(count > (TARGETS_ROW * TARGETS_COL - 1)) {
        set_wflag(pBuf, WF_HIDDEN);
      }
      count++;
      
    }
  } unit_type_iterate_end;
  
  pEditor->pTargets = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pEditor->pTargets->pEndWidgetList = pLast->prev;
  pEditor->pTargets->pBeginWidgetList = pBuf;
  pEditor->pTargets->pEndActiveWidgetList = pLast->prev;
  pEditor->pTargets->pBeginActiveWidgetList = pBuf;
  pEditor->pTargets->pActiveWidgetList = pLast->prev;
    
  /* --------------- */
  if(count > (TARGETS_ROW * TARGETS_COL - 1)) {
    count = create_vertical_scrollbar(pEditor->pTargets,
		    		TARGETS_COL, TARGETS_ROW, TRUE, TRUE);
  } else {
    count = 0;
  }
  /* --------------- */
    
  pEditor->pBeginWidgetList = pEditor->pTargets->pBeginWidgetList;
  
  /* Window */
  w = MAX(w, widget_w * TARGETS_COL + count + 130) + DOUBLE_FRAME_WH;
  h = MAX(h, widget_h * TARGETS_ROW) + FRAME_WH + FRAME_WH;
  
  
  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;
  
  pIcon = get_logo_gfx();
  if(resize_window(pWindow, pIcon, NULL, w, h)) {
    FREESURFACE(pIcon);
  }
  
  pIcon = SDL_DisplayFormat(pWindow->theme);
  FREESURFACE(pWindow->theme);
  pWindow->theme = pIcon;
  pIcon = NULL;
  
  /* Backgrounds */
  dst.x = FRAME_WH;
  dst.y = FRAME_WH + 1;
  dst.w = 130;
  dst.h = 145;
  SDL_FillRect(pWindow->theme, &dst,
		get_game_color(COLOR_STD_BACKGROUND_BROWN, pWindow->theme));
  putframe(pWindow->theme, dst.x, dst.y,
			  dst.x + dst.w - 1, dst.y + dst.h - 1, 0xFF000000);
  putframe(pWindow->theme, dst.x + 2, dst.y + 2,
			  dst.x + dst.w - 3, dst.y + dst.h - 3, 0xFF000000);
  
  dst.x = FRAME_WH;
  dst.y = FRAME_WH + 150;
  dst.w = 130;
  dst.h = 228;
  color.unused = 136;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  putframe(pWindow->theme, dst.x, dst.y,
		  dst.x + dst.w - 1, dst.y + dst.h - 1, 0xFF000000);
  
  if(pEditor->pGlobal) {
    dst.x = FRAME_WH;
    dst.y = FRAME_WH + 380;
    dst.w = 130;
    dst.h = 99;
    SDL_FillRect(pWindow->theme, &dst,
		get_game_color(COLOR_STD_BACKGROUND_BROWN, pWindow->theme));
    putframe(pWindow->theme, dst.x, dst.y,
			  dst.x + dst.w - 1, dst.y + dst.h - 1, 0xFF000000);
    putframe(pWindow->theme, dst.x + 2, dst.y + 2,
			  dst.x + dst.w - 3, dst.y + dst.h - 3, 0xFF000000);
  }
  
  /* name */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + (130 - pBuf->size.w)/2 + FRAME_WH;
  pBuf->size.y = pWindow->size.y + FRAME_WH + 4;
  
  /* size of worklist (without production) */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + (130 - pBuf->size.w)/2 + FRAME_WH;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  
  if(pCity) {
    /* current build and proggrse bar */
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + (130 - pBuf->size.w)/2 + FRAME_WH;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 5;
    
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + (130 - pBuf->size.w)/2 + FRAME_WH;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  } else {
    /* rename worklist */
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + (130 - pBuf->size.w)/2 + FRAME_WH;
    pBuf->size.y = pWindow->size.y + FRAME_WH + 1 + (145 - pBuf->size.h)/2;
  }
  
  /* ok button */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + (65 - pBuf->size.w)/2 + FRAME_WH;
  pBuf->size.y = pWindow->size.y + FRAME_WH + 135 - pBuf->size.h;
  
  /* exit button */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + 65 + (65 - pBuf->size.w)/2 + FRAME_WH;
  pBuf->size.y = pWindow->size.y + FRAME_WH + 135 - pBuf->size.h;
  
  /* worklist */
  if(pCity || pWorkList->wlefs[0] != WEF_END) {
    setup_vertical_widgets_position(1,
      pWindow->size.x + FRAME_WH + 2, pWindow->size.y + FRAME_WH + 152,
	126, 0, pEditor->pWork->pBeginWidgetList,
		  pEditor->pWork->pEndWidgetList);
  }
  
  /* global worklists */
  if(pEditor->pGlobal) {
    setup_vertical_widgets_position(1,
      pWindow->size.x + FRAME_WH + 4,
      pWindow->size.y + FRAME_WH + 384 +
	(pEditor->pGlobal->pScroll ?
	    pEditor->pGlobal->pScroll->pUp_Left_Button->size.h + 1 : 0),
		122, 0, pEditor->pGlobal->pBeginWidgetList,
		  	pEditor->pGlobal->pEndWidgetList);
    
    if(pEditor->pGlobal->pScroll) {
      setup_vertical_scrollbar_area(pEditor->pGlobal->pScroll,
	pWindow->size.x + FRAME_WH + 4,
    	pWindow->size.y + FRAME_WH + 384,
    	93, FALSE);
    }
    
  }
  
  /* Targets */  
  setup_vertical_widgets_position(TARGETS_COL,
	pWindow->size.x + FRAME_WH + 130,
	pWindow->size.y + FRAME_WH,
	  0, 0, pEditor->pTargets->pBeginWidgetList,
			  pEditor->pTargets->pEndWidgetList);
    
  if(pEditor->pTargets->pScroll) {
    setup_vertical_scrollbar_area(pEditor->pTargets->pScroll,
	pWindow->size.x + pWindow->size.w - FRAME_WH,
    	pWindow->size.y + FRAME_WH + 1,
    	pWindow->size.h - (FRAME_WH + FRAME_WH + 1), TRUE);
    
  }
    
  /* ----------------------------------- */
  FREESTRING16(pStr);
  FREESURFACE(pMain);
  
  redraw_group(pEditor->pBeginWidgetList, pWindow, 0);
  flush_rect(pWindow->size);
}
  
void popdown_worklist_editor(void)
{
    if(pEditor) {
    popdown_window_group_dialog(pEditor->pBeginWidgetList,
					    pEditor->pEndWidgetList);
    FREE(pEditor->pTargets->pScroll);
    FREE(pEditor->pWork->pScroll);
    FREE(pEditor->pGlobal->pScroll);
    FREE(pEditor->pGlobal);
    FREE(pEditor->pTargets);
    FREE(pEditor->pWork);
    FREE(pEditor->pCopy_WorkList);
    FREE(pEditor);
  }

}
