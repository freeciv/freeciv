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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif   
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <assert.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"
 
#include "climisc.h"
#include "cityrep.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapview.h"
#include "options.h"
#include "repodlgs.h"
#include "tilespec.h"
#include "wldlg.h"   
#include "gui_main.h"
#include "citydlg.h"

#define NUM_UNITS_SHOWN  12
#define NUM_CITIZENS_SHOWN 25   
#define ID_CITYOPT_RADIO 100
#define ID_CITYOPT_TOGGLE 200
#define NUM_TABS 4
struct city_dialog {
  struct city *pcity;
  HBITMAP map_bmp;
  HBITMAP citizen_bmp;
  HWND mainwindow;
  HWND tab_ctrl;
  HWND tab_childs[NUM_TABS];
  HWND buildings_list;
  HWND buy_but;
  HWND change_but;
  HWND sell_but;
  HWND close_but;
  HWND rename_but;
  HWND trade_but;
  HWND activate_but;
  HWND unitlist_but;
  HWND configure_but;
  HWND prod_area;
  HWND output_area;
  HWND pollution_area;
  HWND storage_area;
  HWND build_area;
  HWND supported_label;
  HWND present_label;
  HWND trade_label;
  HWND cityopt_dialog;
  struct fcwin_box *opt_winbox;
  int resized;
  struct fcwin_box *listbox_area;
  struct fcwin_box *full_win;
  struct fcwin_box *buttonrow;
  int upper_y;
  int map_x;
  int map_y;
  int map_w;
  int map_h;
  int pop_y;
  int pop_x; /* also supported_x and present_x */
  int supported_y;
  int present_y;
  Impr_Type_id sell_id;
  
  int citizen_type[NUM_CITIZENS_SHOWN];
  int support_unit_ids[NUM_UNITS_SHOWN];
  int present_unit_ids[NUM_UNITS_SHOWN];
  char improvlist_names[B_LAST+1][64];
  char *improvlist_names_ptrs[B_LAST+1];
  
  int change_list_ids[B_LAST+1+U_LAST+1];
  int change_list_num_improvements;
  
  int is_modal;    
};


extern struct connection aconnection; 
extern HFONT font_8courier;
extern HFONT font_12courier;
extern HFONT font_12arial;
extern HINSTANCE freecivhinst;
extern HWND root_window;
static HDC citydlgdc;
static int city_map_width;
static int city_map_height;
static struct genlist dialog_list;
static int city_dialogs_have_been_initialised;
struct city_dialog *get_city_dialog(struct city *pcity);
struct city_dialog *create_city_dialog(struct city *pcity, bool make_modal);   

static void city_dialog_update_improvement_list(struct city_dialog *pdialog);
static void city_dialog_update_supported_units(HDC hdc,struct city_dialog *pdialog, int id);
static void city_dialog_update_present_units(HDC hdc,struct city_dialog *pdialog, int id);
static void city_dialog_update_citizens(HDC hdc,struct city_dialog *pdialog);
static void city_dialog_update_map(HDC hdc,struct city_dialog *pdialog);
static void city_dialog_update_production(struct city_dialog *pdialog);
static void city_dialog_update_output(struct city_dialog *pdialog);
static void city_dialog_update_building(struct city_dialog *pdialog);
static void city_dialog_update_storage(struct city_dialog *pdialog);
static void city_dialog_update_pollution(struct city_dialog *pdialog);        
static void city_dialog_update_tradelist(struct city_dialog *pdialog);
static void commit_city_worklist(struct worklist *pwl, void *data);
static void resize_city_dialog(struct city_dialog *pdialog);
static LONG CALLBACK trade_page_proc(HWND win, UINT message,
				     WPARAM wParam, LPARAM lParam);
static LONG CALLBACK citydlg_overview_proc(HWND win, UINT message,
					   WPARAM wParam, LPARAM lParam);

static LONG APIENTRY citydlg_config_proc(HWND hWnd,
					 UINT message,
					 UINT wParam,
					 LONG lParam);

static WNDPROC tab_procs[NUM_TABS]={citydlg_overview_proc,
				    worklist_editor_proc,
				    /*   happiness_proc,
					 cma_proc, */
				    trade_page_proc,citydlg_config_proc};
enum t_tab_pages {
  PAGE_OVERVIEW=0,
  PAGE_WORKLIST,
  /* PAGE_HAPPINESS,
     PAGE_CMA, */
  PAGE_TRADE,
  PAGE_CONFIG
};


/**************************************************************************
...
**************************************************************************/

void refresh_city_dialog(struct city *pcity)
{
  HDC hdc; 
  struct city_dialog *pdialog;
  if((pdialog=get_city_dialog(pcity))) {
    hdc=GetDC(pdialog->mainwindow);
    city_dialog_update_citizens(hdc,pdialog);
    ReleaseDC(pdialog->mainwindow,hdc);
    hdc=GetDC(pdialog->tab_childs[0]);
    city_dialog_update_supported_units(hdc,pdialog, 0);
    city_dialog_update_present_units(hdc,pdialog, 0);   
 
    city_dialog_update_map(hdc,pdialog);
    ReleaseDC(pdialog->tab_childs[0],hdc);
    city_dialog_update_improvement_list(pdialog);  
    city_dialog_update_production(pdialog);
    city_dialog_update_output(pdialog);
    city_dialog_update_building(pdialog);
    city_dialog_update_storage(pdialog);
    city_dialog_update_pollution(pdialog);    
    city_dialog_update_tradelist(pdialog);
    resize_city_dialog(pdialog);
  }
  if (pcity->owner==game.player_idx)
    {
      city_report_dialog_update_city(pcity);
      economy_report_dialog_update();   
    }
  else if (pdialog)
    {
      EnableWindow(pdialog->buy_but,FALSE);
      EnableWindow(pdialog->change_but,FALSE);
      EnableWindow(pdialog->sell_but,FALSE);
      EnableWindow(pdialog->rename_but,FALSE);
      EnableWindow(pdialog->activate_but,FALSE);
      EnableWindow(pdialog->unitlist_but,FALSE);
      EnableWindow(pdialog->configure_but,FALSE);
    }
}


bool city_dialog_is_open(struct city *pcity)
{
  return get_city_dialog(pcity) != NULL;
}

/**************************************************************************
...
**************************************************************************/

void city_dialog_update_improvement_list(struct city_dialog *pdialog)
{
  int i, n = 0, flag = 0;
  
  built_impr_iterate(pdialog->pcity, i) {
    if (!pdialog->improvlist_names_ptrs[n] ||
	strcmp(pdialog->improvlist_names_ptrs[n],
	       get_impr_name_ex(pdialog->pcity, i)) != 0)
      flag = 1;
    sz_strlcpy(pdialog->improvlist_names[n],
	       get_impr_name_ex(pdialog->pcity, i));
    pdialog->improvlist_names_ptrs[n] = pdialog->improvlist_names[n];
    n++;
  } built_impr_iterate_end;
 
  if(pdialog->improvlist_names_ptrs[n]!=0) {
    pdialog->improvlist_names_ptrs[n]=0;
    flag=1;
  }
 
  if(flag || n==0) {
    ListBox_ResetContent(pdialog->buildings_list);
    for (i=0;i<n;i++)
      {
	int id;
	id=ListBox_AddString(pdialog->buildings_list,
			  pdialog->improvlist_names_ptrs[i]);
	if (id!=LB_ERR)
	  {
	    ListBox_SetItemData(pdialog->buildings_list,id,i);
	  }
      }
  }
}

/**************************************************************************
...
**************************************************************************/

void city_dialog_update_present_units(HDC hdc,struct city_dialog *pdialog, int unitid) 
{
  HBRUSH grey;
  int i;
  struct unit_list *plist;
  struct genlist_iterator myiter;
  struct unit *punit;
  grey=GetStockObject(LTGRAY_BRUSH);
  if(unitid) {
    for(i=0; i<NUM_UNITS_SHOWN; i++)
      if(pdialog->present_unit_ids[i]==unitid)
        break;
    if(i==NUM_UNITS_SHOWN)
      unitid=0;
  }
 
  if(pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_present);
  } else {
    plist = &(map_get_tile(pdialog->pcity->x, pdialog->pcity->y)->units);
  }
  
  genlist_iterator_init(&myiter, &(plist->list), 0);
  
  for(i=0; i<NUM_UNITS_SHOWN&&ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter),i++)
    {
      punit=(struct unit*)ITERATOR_PTR(myiter);
      
      if(unitid && punit->id!=unitid)
	continue;        
      put_unit_pixmap(punit,hdc,
		      pdialog->pop_x+i*(SMALL_TILE_WIDTH+NORMAL_TILE_WIDTH),
		      pdialog->present_y); 
      pdialog->present_unit_ids[i]=punit->id;       
      
    }
  for(; i<NUM_UNITS_SHOWN; i++) {
    RECT rc;
    pdialog->present_unit_ids[i]=0;      
    rc.top=pdialog->present_y;
    rc.left=pdialog->pop_x+i*(SMALL_TILE_WIDTH+NORMAL_TILE_WIDTH);
    rc.right=rc.left+NORMAL_TILE_WIDTH;
    rc.bottom=rc.top+SMALL_TILE_HEIGHT+NORMAL_TILE_HEIGHT;
    FillRect(hdc,&rc,grey);
  }
}

/**************************************************************************
...
**************************************************************************/

void city_dialog_update_supported_units(HDC hdc, struct city_dialog *pdialog,
                                        int unitid)
{
  HBRUSH grey;
  int i;
  struct unit_list *plist;
  struct genlist_iterator myiter;
  struct unit *punit;    
  if(unitid) {
    for(i=0; i<NUM_UNITS_SHOWN; i++)
      if(pdialog->support_unit_ids[i]==unitid)
        break;
    if(i==NUM_UNITS_SHOWN)
      unitid=0;
  } 
  grey=GetStockObject(LTGRAY_BRUSH);
  if(pdialog->pcity->owner != game.player_idx) {
    plist = &(pdialog->pcity->info_units_supported);
  } else {
    plist = &(pdialog->pcity->units_supported);
  }      
  genlist_iterator_init(&myiter, &(plist->list), 0); 
     
  for(i=0; i<NUM_UNITS_SHOWN&&ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter),i++)
    {
      punit=(struct unit*)ITERATOR_PTR(myiter);
      if(unitid && punit->id!=unitid)
	continue;     
      
      put_unit_pixmap(punit,hdc,
		      pdialog->pop_x+i*(SMALL_TILE_WIDTH+NORMAL_TILE_WIDTH),
		      pdialog->supported_y);
      put_unit_city_overlays(punit,hdc,
		      pdialog->pop_x+i*(SMALL_TILE_WIDTH+NORMAL_TILE_WIDTH),
		      pdialog->supported_y);
      pdialog->support_unit_ids[i]=punit->id;    
    }
  for(; i<NUM_UNITS_SHOWN; i++) {   
    RECT rc;
    rc.top=pdialog->supported_y;
    rc.left=pdialog->pop_x+i*(SMALL_TILE_WIDTH+NORMAL_TILE_WIDTH);
    rc.right=rc.left+NORMAL_TILE_WIDTH;
    rc.bottom=rc.top+SMALL_TILE_HEIGHT+NORMAL_TILE_HEIGHT;
    FillRect(hdc,&rc,grey);
    
  }
}   


/**************************************************************************
...
**************************************************************************/

void city_dialog_update_pollution(struct city_dialog *pdialog)   
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  my_snprintf(buf, sizeof(buf), _("Pollution:   %3d"), pcity->pollution);   
  SetWindowText(pdialog->pollution_area,buf);
}
  
/**************************************************************************
...
**************************************************************************/

void city_dialog_update_storage(struct city_dialog *pdialog)   
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  my_snprintf(buf, sizeof(buf), _("Granary: %3d/%-3d"), pcity->food_stock,
          game.foodbox*(pcity->size+1));   
  SetWindowText(pdialog->storage_area,buf);

}

/**************************************************************************
...
**************************************************************************/

void city_dialog_update_production(struct city_dialog *pdialog)   
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  my_snprintf(buf, sizeof(buf),
	      _("Food:    %2d (%+2d)\nProd:    %2d (%+2d)\nTrade:   %2d (%+2d)"),
	      pcity->food_prod, pcity->food_surplus,
	      pcity->shield_prod, pcity->shield_surplus,
	      pcity->trade_prod+pcity->corruption, pcity->trade_prod);              
  SetWindowText(pdialog->prod_area,buf);
  
}
/**************************************************************************
...
**************************************************************************/


void city_dialog_update_output(struct city_dialog *pdialog)   
{
  char buf[512];
  struct city *pcity=pdialog->pcity;
  my_snprintf(buf, sizeof(buf),
	      _("Gold:    %2d (%+2d)\nLuxury:  %2d\nScience: %2d"),
	      pcity->tax_total, city_gold_surplus(pcity),
	      pcity->luxury_total,
	      pcity->science_total);      
  SetWindowText(pdialog->output_area,buf);
  
}

/**************************************************************************
...
**************************************************************************/


void city_dialog_update_building(struct city_dialog *pdialog)
{
  char buf2[100], buf[32], *descr;
  struct city *pcity=pdialog->pcity;    
  
  EnableWindow(pdialog->buy_but,!pcity->did_buy);
  EnableWindow(pdialog->sell_but, !pcity->did_sell);

  get_city_dialog_production(pcity, buf, sizeof(buf));
  
  if (pcity->is_building_unit) {
    descr = get_unit_type(pcity->currently_building)->name;
  } else {
    if (pcity->currently_building == B_CAPITAL) {
      /* You can't buy Capitalization. */
      EnableWindow(pdialog->buy_but, FALSE);
    }
    descr = get_impr_name_ex(pcity, pcity->currently_building);
  }

  my_snprintf(buf2, sizeof(buf2), "%s\r\n%s", descr, buf);
  SetWindowText(pdialog->build_area, buf2);
  resize_city_dialog(pdialog);
 
}

/****************************************************************
Isometric.
*****************************************************************/
static void city_dialog_update_map_iso(HDC hdc,struct city_dialog *pdialog)
{
  struct city *pcity = pdialog->pcity;
  int city_x,city_y;
  
  /* We have to draw the tiles in a particular order, so its best
     to avoid using any iterator macro. */
  for (city_x = 0; city_x<CITY_MAP_SIZE; city_x++)
    for (city_y = 0; city_y<CITY_MAP_SIZE; city_y++) {
      int map_x, map_y;
      if (is_valid_city_coords(city_x, city_y)
          && city_map_to_map(&map_x, &map_y, pcity, city_x, city_y)) {
        if (tile_get_known(map_x, map_y)) {
          int canvas_x, canvas_y;
          city_pos_to_canvas_pos(city_x, city_y, &canvas_x, &canvas_y);
          put_one_tile_full(hdc, map_x, map_y,
                            canvas_x, canvas_y, 1);
        }
      }
    }
  
  /* We have to put the output afterwards or it will be covered. */
  city_map_checked_iterate(pcity->x, pcity->y, x, y, map_x, map_y) {
    if (tile_get_known(map_x, map_y)) {
      int canvas_x, canvas_y;
      city_pos_to_canvas_pos(x, y, &canvas_x, &canvas_y);
      if (pcity->city_map[x][y]==C_TILE_WORKER) {
        put_city_tile_output(hdc,
                             canvas_x, canvas_y,
                             city_get_food_tile(x, y, pcity),
                             city_get_shields_tile(x, y, pcity), 
                             city_get_trade_tile(x, y, pcity));
      }
    }
  } city_map_checked_iterate_end;

  /* This sometimes will draw one of the lines on top of a city or
     unit pixmap. This should maybe be moved to put_one_tile_pixmap()
     to fix this, but maybe it wouldn't be a good idea because the
     lines would get obscured. */
  city_map_checked_iterate(pcity->x, pcity->y, x, y, map_x, map_y) {
    if (tile_get_known(map_x, map_y)) {
      int canvas_x, canvas_y;
      city_pos_to_canvas_pos(x, y, &canvas_x, &canvas_y);
      if (pcity->city_map[x][y]==C_TILE_UNAVAILABLE) {
        pixmap_frame_tile_red(hdc,
			      canvas_x, canvas_y);
      }
    }
  } city_map_checked_iterate_end;

}

/****************************************************************
Overhead
*****************************************************************/
static void city_dialog_update_map_ovh(HDC hdc,struct city_dialog *pdialog)
{
  int y,x;
  struct city *pcity;
  pcity=pdialog->pcity;
  for(y=0; y<CITY_MAP_SIZE; y++)
    for(x=0; x<CITY_MAP_SIZE; x++) {
      int map_x, map_y;

      if (is_valid_city_coords(x, y)
	  && city_map_to_map(&map_x, &map_y, pcity, x, y)
	  && tile_get_known(map_x, map_y)) {
	pixmap_put_tile(citydlgdc, map_x, map_y,
			x*NORMAL_TILE_WIDTH,
			y*NORMAL_TILE_HEIGHT,1);
        if(pcity->city_map[x][y]==C_TILE_WORKER)
          put_city_tile_output(citydlgdc, NORMAL_TILE_WIDTH*x,
			       y*NORMAL_TILE_HEIGHT,
                               city_get_food_tile(x, y, pcity),
                               city_get_shields_tile(x, y, pcity),
                               city_get_trade_tile(x, y, pcity));
	else if(pcity->city_map[x][y]==C_TILE_UNAVAILABLE)
	  pixmap_frame_tile_red(citydlgdc, x, y);
      }
      else {
	BitBlt(citydlgdc,x*NORMAL_TILE_WIDTH,
	       y*NORMAL_TILE_HEIGHT,
	       NORMAL_TILE_WIDTH,
	       NORMAL_TILE_HEIGHT,
	       NULL,0,0,BLACKNESS);
      }
    }
  
}
/**************************************************************************
									   ...
**************************************************************************/
void city_dialog_update_map(HDC hdc,struct city_dialog *pdialog)
{
  HBITMAP oldbit;
  oldbit=SelectObject(citydlgdc,pdialog->map_bmp);
  BitBlt(citydlgdc,0,0,pdialog->map_w,pdialog->map_h,
	 NULL,0,0,BLACKNESS);
  if (is_isometric)
    city_dialog_update_map_iso(citydlgdc,pdialog);
  else
    city_dialog_update_map_ovh(citydlgdc,pdialog);
                           
  BitBlt(hdc,pdialog->map_x,pdialog->map_y,city_map_width,
	 city_map_height,
	 citydlgdc,0,0,SRCCOPY);
  SelectObject(citydlgdc,oldbit);
}

/**************************************************************************
...
**************************************************************************/


void city_dialog_update_citizens(HDC hdc,struct city_dialog *pdialog)
{
  int i, n;
  struct city *pcity=pdialog->pcity;
  RECT rc;
  HBITMAP oldbit;
  oldbit=SelectObject(citydlgdc,pdialog->citizen_bmp);


  for(i=0, n=0; n<pcity->ppl_happy[4] && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=5 && pdialog->citizen_type[i]!=6) {
      pdialog->citizen_type[i]=5+i%2;
      draw_sprite(get_citizen_sprite(pdialog->citizen_type[i]),citydlgdc,
		  SMALL_TILE_WIDTH*i,0);
    }
  for(n=0; n<pcity->ppl_content[4] && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=3 && pdialog->citizen_type[i]!=4) {
      pdialog->citizen_type[i]=3+i%2;
      draw_sprite(get_citizen_sprite(pdialog->citizen_type[i]),citydlgdc,
		  SMALL_TILE_WIDTH*i,0);  
    }
  for(n=0; n<pcity->ppl_unhappy[4] && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=7 && pdialog->citizen_type[i]!=8) {
      pdialog->citizen_type[i]=7+i%2;
      draw_sprite(get_citizen_sprite(pdialog->citizen_type[i]),citydlgdc,
		  SMALL_TILE_WIDTH*i,0);
    }
  for (n = 0; n < pcity->ppl_angry[4] && i < NUM_CITIZENS_SHOWN; n++, i++)
    if (pdialog->citizen_type[i] != 9 && pdialog->citizen_type[i] != 10) {
      pdialog->citizen_type[i] = 9 + i % 2;
      draw_sprite(get_citizen_sprite(pdialog->citizen_type[i]), citydlgdc,
		  SMALL_TILE_WIDTH * i, 0);
    }
  for(n=0; n<pcity->ppl_elvis && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=0) {
      pdialog->citizen_type[i]=0;
      draw_sprite(get_citizen_sprite(pdialog->citizen_type[i]),citydlgdc,
		  SMALL_TILE_WIDTH*i,0);
      
     }
  for(n=0; n<pcity->ppl_scientist && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=1) {
      pdialog->citizen_type[i]=1;
      draw_sprite(get_citizen_sprite(pdialog->citizen_type[i]),citydlgdc,
		  SMALL_TILE_WIDTH*i,0);
    }
  for(n=0; n<pcity->ppl_taxman && i<NUM_CITIZENS_SHOWN; n++, i++)
    if(pdialog->citizen_type[i]!=2) {
      pdialog->citizen_type[i]=2;
      draw_sprite(get_citizen_sprite(pdialog->citizen_type[i]),citydlgdc,
		  SMALL_TILE_WIDTH*i,0);
    }
  
  if (i<NUM_CITIZENS_SHOWN) {
    rc.left=i*SMALL_TILE_WIDTH;
    rc.right=NUM_CITIZENS_SHOWN*SMALL_TILE_WIDTH;
    rc.top=0;
    rc.bottom=SMALL_TILE_HEIGHT;
    FillRect(citydlgdc,&rc,
	     (HBRUSH)GetClassLong(pdialog->mainwindow,GCL_HBRBACKGROUND));
    FrameRect(citydlgdc,&rc,
	      (HBRUSH)GetClassLong(pdialog->mainwindow,GCL_HBRBACKGROUND));
  }
  
  for(; i<NUM_CITIZENS_SHOWN; i++) {
    pdialog->citizen_type[i]=-1;    
  }   
  
  BitBlt(hdc,pdialog->pop_x,pdialog->pop_y,
	 NUM_CITIZENS_SHOWN*SMALL_TILE_WIDTH,
	 SMALL_TILE_HEIGHT,
	 citydlgdc,0,0,SRCCOPY);
  SelectObject(citydlgdc,oldbit);
}

/**************************************************************************
...
**************************************************************************/


static void CityDlgClose(struct city_dialog *pdialog)
{
  ShowWindow(pdialog->mainwindow,SW_HIDE);
  
  DestroyWindow(pdialog->mainwindow);
 
}
			       
/**************************************************************************
...
**************************************************************************/


static void map_minsize(POINT *minsize,void * data)
{
  struct city_dialog *pdialog;
  pdialog=(struct city_dialog *)data;
  minsize->x=pdialog->map_w;
  minsize->y=pdialog->map_h;
}

/**************************************************************************
...
**************************************************************************/


static void map_setsize(LPRECT setsize,void *data)
{
  struct city_dialog *pdialog;
  pdialog=(struct city_dialog *)data;
  pdialog->map_x=setsize->left;
  pdialog->map_y=setsize->top;
 
}

/**************************************************************************
...
**************************************************************************/


static void supported_minsize(LPPOINT minsize,void *data)
{
  minsize->y=SMALL_TILE_HEIGHT+NORMAL_TILE_HEIGHT;
  minsize->x=1;  /* just a dummy value */
}

/**************************************************************************
...
**************************************************************************/


static void supported_setsize(LPRECT setsize,void *data)
{
  struct city_dialog *pdialog;
  pdialog=(struct city_dialog *)data;
  pdialog->supported_y=setsize->top;
}

/**************************************************************************
...
**************************************************************************/


static void present_minsize(POINT * minsize,void *data)
{
  minsize->y=SMALL_TILE_HEIGHT+NORMAL_TILE_HEIGHT;
  minsize->x=1;  /* just a dummy value */
}

/**************************************************************************
...
**************************************************************************/


static void present_setsize(LPRECT setsize,void *data)
{
  struct city_dialog *pdialog;
  pdialog=(struct city_dialog *)data;
  pdialog->present_y=setsize->top;
}

/**************************************************************************
...
**************************************************************************/


static void resize_city_dialog(struct city_dialog *pdialog)
{
  if (!pdialog->full_win) return;
  fcwin_redo_layout(pdialog->mainwindow);
}

/**************************************************************************
...
**************************************************************************/
static void upper_set(RECT *rc, void *data)
{
  
}
/**************************************************************************
...
**************************************************************************/
static void upper_min(POINT *min,void *data)
{
  min->x=1;
  min->y=45;
}
/**************************************************************************
...
**************************************************************************/


static void CityDlgCreate(HWND hWnd,struct city_dialog *pdialog)
{
  int ybut;
  int i;
  HDC hdc;
  struct fcwin_box *lbupper;
  struct fcwin_box *lbunder;
  struct fcwin_box *upper_row;
  struct fcwin_box *child_vbox;
  struct fcwin_box *left_labels;
  struct fcwin_box *grp_box;
  struct worklist_window_init wl_init;
  void *user_data[NUM_TABS];
  static char *titles_[NUM_TABS]={N_("City Overview"),N_("Worklist"),
				  /*  N_("Happiness"),N_("CMA"), */
				  N_("Trade Routes"),
				  N_("Misc. Settings")};
  static char *titles[NUM_TABS];
  if (titles[0] == NULL) {
    for(i=0;i<NUM_TABS;i++) {
      titles[i]=_(titles_[i]);
    }
  }
  pdialog->mainwindow=hWnd;
  pdialog->map_w=city_map_width;
  pdialog->map_h=city_map_height;
  pdialog->upper_y=45;
  pdialog->pop_y=15;
  pdialog->pop_x=20;
  pdialog->supported_y=pdialog->map_y+pdialog->map_h+12;
  pdialog->present_y=pdialog->supported_y+NORMAL_TILE_HEIGHT+12+4+SMALL_TILE_HEIGHT;
  ybut=pdialog->present_y+NORMAL_TILE_HEIGHT+12+4+SMALL_TILE_HEIGHT;
  if (!citydlgdc) {
    hdc=GetDC(pdialog->mainwindow);
    citydlgdc=CreateCompatibleDC(hdc);
    ReleaseDC(pdialog->mainwindow,hdc);
  }
    
  hdc=GetDC(pdialog->mainwindow);
  pdialog->map_bmp=CreateCompatibleBitmap(hdc,5*NORMAL_TILE_WIDTH,
					  5*NORMAL_TILE_HEIGHT);
  pdialog->citizen_bmp=CreateCompatibleBitmap(hdc,
					      NUM_CITIZENS_SHOWN*
					      SMALL_TILE_WIDTH,
					      SMALL_TILE_HEIGHT);
  ReleaseDC(pdialog->mainwindow,hdc);
  for (i=0;i<NUM_CITIZENS_SHOWN;i++)
    {
      pdialog->citizen_type[i]=-1;
    }
  pdialog->full_win=fcwin_vbox_new(hWnd,FALSE);
  fcwin_box_add_generic(pdialog->full_win,upper_min,upper_set,NULL,NULL,
			FALSE,FALSE,0);
  for(i=0;i<NUM_TABS;i++) {
    user_data[i]=pdialog;
  }
  user_data[PAGE_WORKLIST] = &wl_init;
  
  wl_init.pwl = &pdialog->pcity->worklist;
  wl_init.pcity = pdialog->pcity;
  wl_init.parent = pdialog->mainwindow;
  wl_init.user_data = pdialog;
  wl_init.ok_cb = commit_city_worklist;
  wl_init.cancel_cb = NULL;
  pdialog->tab_ctrl=fcwin_box_add_tab(pdialog->full_win,tab_procs,
				      pdialog->tab_childs,
				      titles,user_data,NUM_TABS,
				      0,0,TRUE,TRUE,5);
  
  upper_row=fcwin_hbox_new(pdialog->tab_childs[0],FALSE);
  left_labels=fcwin_vbox_new(pdialog->tab_childs[0],FALSE);
  fcwin_box_add_groupbox(upper_row,_("City info"),left_labels,0,
			 FALSE,FALSE,5);
  
  pdialog->prod_area=fcwin_box_add_static_default(left_labels,
						  " ",
						  ID_CITY_PROD,
						  SS_LEFT);
  pdialog->output_area=fcwin_box_add_static_default(left_labels,
						    " ",
						    ID_CITY_OUTPUT,
						    SS_LEFT);
  pdialog->pollution_area=fcwin_box_add_static_default(left_labels," ",
						       ID_CITY_POLLUTION,
						       SS_LEFT);
  pdialog->storage_area=fcwin_box_add_static_default(left_labels," ",
						     ID_CITY_STORAGE,
						     SS_LEFT);
  
  grp_box=fcwin_vbox_new(pdialog->tab_childs[0],FALSE);
  fcwin_box_add_groupbox(upper_row,_("City map"),grp_box,0,TRUE,FALSE,5);
  fcwin_box_add_generic(grp_box,map_minsize,map_setsize,NULL,pdialog,TRUE,FALSE,0);
  
  lbunder=fcwin_hbox_new(pdialog->tab_childs[0],FALSE);
  pdialog->sell_but=fcwin_box_add_button(lbunder,_("Sell"),ID_CITY_SELL,0,
					 TRUE,TRUE,5);
  lbupper=fcwin_hbox_new(pdialog->tab_childs[0], FALSE);
  pdialog->build_area=fcwin_box_add_static(lbupper,"",0,SS_LEFT,
					   TRUE,TRUE,5);
  pdialog->buy_but=fcwin_box_add_button(lbupper,_("Buy"),ID_CITY_BUY,0,
					TRUE,TRUE,5);
  pdialog->change_but=fcwin_box_add_button(lbupper,_("Change"),ID_CITY_CHANGE,
					   0,TRUE,TRUE,5);
  pdialog->listbox_area=fcwin_vbox_new(pdialog->tab_childs[0],FALSE);
  fcwin_box_add_box(pdialog->listbox_area,lbupper,FALSE,FALSE,0);
  pdialog->buildings_list=CreateWindow("LISTBOX",NULL,
				       WS_CHILD | WS_VSCROLL | WS_VISIBLE |
				       LBS_HASSTRINGS,
				       0/*pdialog->map_x+pdialog->map_w*/,
				       0/*pdialog->map_y+10+15*/,
				       0/*300*/,0/*pdialog->map_h-2*15-10*/,
				       pdialog->tab_childs[0],
				       (HMENU)ID_CITY_BLIST,
				       freecivhinst,
				       NULL);
  fcwin_box_add_win(pdialog->listbox_area,pdialog->buildings_list,
		    TRUE,TRUE,0);
  fcwin_box_add_box(pdialog->listbox_area,lbunder,FALSE,FALSE,0);
  fcwin_box_add_box(upper_row,pdialog->listbox_area,TRUE,TRUE,5);

  child_vbox=fcwin_vbox_new(pdialog->tab_childs[0],FALSE);
  fcwin_box_add_box(child_vbox,upper_row,TRUE,TRUE,0);
  pdialog->supported_label=fcwin_box_add_static_default(child_vbox,
							_("Supported units"),
							-1,SS_LEFT);
  fcwin_box_add_generic(child_vbox,
			supported_minsize,supported_setsize,NULL,pdialog,
			FALSE,FALSE,5);
  pdialog->present_label=fcwin_box_add_static_default(child_vbox,
						      _("Units present"),
						      -1,SS_LEFT);
  fcwin_box_add_generic(child_vbox,
			present_minsize,present_setsize,NULL,pdialog,
			FALSE,FALSE,5);
  pdialog->buttonrow=fcwin_hbox_new(hWnd,TRUE);
  pdialog->close_but=fcwin_box_add_button_default(pdialog->buttonrow,_("Close"),
				       ID_CITY_CLOSE,0);
  pdialog->rename_but=fcwin_box_add_button_default(pdialog->buttonrow,_("Rename"),
					ID_CITY_RENAME,0);
  
  pdialog->activate_but=fcwin_box_add_button_default(pdialog->buttonrow,
					  _("Activate Units"),
					  ID_CITY_ACTIVATE,0);
  pdialog->unitlist_but=fcwin_box_add_button_default(pdialog->buttonrow,
					  _("Unit List"),
					  ID_CITY_UNITLIST,0);
  fcwin_box_add_box(pdialog->full_win,pdialog->buttonrow,FALSE,FALSE,5);
 
  SendMessage(pdialog->supported_label,
	      WM_SETFONT,(WPARAM) font_12courier,MAKELPARAM(TRUE,0)); 
  SendMessage(pdialog->present_label,
	      WM_SETFONT,(WPARAM) font_12courier,MAKELPARAM(TRUE,0)); 
  SendMessage(pdialog->storage_area,
	      WM_SETFONT,(WPARAM) font_12courier,MAKELPARAM(TRUE,0));  
  SendMessage(pdialog->build_area,
	      WM_SETFONT,(WPARAM) font_12courier,MAKELPARAM(TRUE,0));    
  SendMessage(pdialog->pollution_area,
	      WM_SETFONT,(WPARAM) font_12courier,MAKELPARAM(TRUE,0));  
  SendMessage(pdialog->prod_area,
	      WM_SETFONT,(WPARAM) font_12courier,MAKELPARAM(TRUE,0));  
  SendMessage(pdialog->output_area,
	      WM_SETFONT,(WPARAM) font_12courier,MAKELPARAM(TRUE,0)); 
  genlist_insert(&dialog_list, pdialog, 0);    
  for(i=0; i<B_LAST+1; i++)
    pdialog->improvlist_names_ptrs[i]=0;   
  for(i=0; i<NUM_UNITS_SHOWN;i++)
    {
      pdialog->support_unit_ids[i]=-1;  
      pdialog->present_unit_ids[i]=-1;    
    }
  pdialog->mainwindow=hWnd;
  fcwin_set_box(pdialog->tab_childs[0],child_vbox);
  fcwin_set_box(pdialog->mainwindow,pdialog->full_win);
  ShowWindow(pdialog->tab_childs[0],SW_SHOWNORMAL);
}

/**************************************************************************
...
**************************************************************************/


static void activate_unit(struct unit *punit)
{
  if((punit->activity!=ACTIVITY_IDLE || punit->ai.control)
     && can_unit_do_activity(punit, ACTIVITY_IDLE))
    request_new_unit_activity(punit, ACTIVITY_IDLE);
  set_unit_focus(punit);
}    

/**************************************************************************
...
**************************************************************************/


static void buy_callback_yes(HWND w, void * data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;
 
  pdialog=(struct city_dialog *)data;
 
  packet.city_id=pdialog->pcity->id;
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_BUY);
 
  destroy_message_dialog(w);
}
 
 
/****************************************************************
...
*****************************************************************/
static void buy_callback_no(HWND w, void * data)
{
  destroy_message_dialog(w);
}

/**************************************************************************
...
**************************************************************************/
        

void buy_callback(struct city_dialog *pdialog)
{
  int value;
  char *name;
  char buf[512];    
   if(pdialog->pcity->is_building_unit) {
    name=get_unit_type(pdialog->pcity->currently_building)->name;
  }
  else {
    name=get_impr_name_ex(pdialog->pcity, pdialog->pcity->currently_building);
  }
  value=city_buy_cost(pdialog->pcity);
 
  if(game.player_ptr->economic.gold>=value) {
    my_snprintf(buf, sizeof(buf),
            _("Buy %s for %d gold?\nTreasury contains %d gold."),
            name, value, game.player_ptr->economic.gold);
 
    popup_message_dialog(pdialog->mainwindow, /*"buydialog"*/ _("Buy It!"), buf,
                         _("_Yes"), buy_callback_yes, pdialog,
                         _("_No"), buy_callback_no, 0, 0);
  }
  else {
    my_snprintf(buf, sizeof(buf),
            _("%s costs %d gold.\nTreasury contains %d gold."),
            name, value, game.player_ptr->economic.gold);
 
    popup_message_dialog(NULL, /*"buynodialog"*/ _("Buy It!"), buf,
                         _("Darn"), buy_callback_no, 0, 0);
  }      
}
/****************************************************************
...
*****************************************************************/
static void sell_callback_yes(HWND w, void * data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;
 
  pdialog=(struct city_dialog *)data;
 
  packet.city_id=pdialog->pcity->id;
  packet.build_id=pdialog->sell_id;
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_SELL);
 
  destroy_message_dialog(w);
}
 
 
/****************************************************************
...
*****************************************************************/
static void sell_callback_no(HWND w, void * data)
{
  destroy_message_dialog(w);
}
 
 
/****************************************************************
...
*****************************************************************/            
void sell_callback(struct city_dialog *pdialog)
{
  int row,n;
  row=ListBox_GetCurSel(pdialog->buildings_list);
  if (row!=LB_ERR) {
    row=ListBox_GetItemData(pdialog->buildings_list,row);
    n=0;

    built_impr_iterate(pdialog->pcity, i) {
      if (n == row) {
	char buf[512];

	if (is_wonder(i)) {
	  return;
	}

	pdialog->sell_id = i;
	my_snprintf(buf, sizeof(buf), _("Sell %s for %d gold?"),
		    get_impr_name_ex(pdialog->pcity, i),
		    improvement_value(i));

	popup_message_dialog(pdialog->mainwindow, /*"selldialog" */
			     _("Sell It!"), buf, _("_Yes"),
			     sell_callback_yes, pdialog, _("_No"),
			     sell_callback_no, pdialog, 0);
	return;
      }
      n++;
    } built_impr_iterate_end;
  }
}


/**************************************************************************
...
**************************************************************************/


static LONG CALLBACK changedlg_proc(HWND hWnd,
				    UINT message,
				    WPARAM wParam,
				    LPARAM lParam) 
{
  int sel,i,n,idx;
  bool is_unit;
  struct city_dialog *pdialog;
  pdialog=(struct city_dialog *)fcwin_get_user_data(hWnd);
  is_unit=0; /* silence gcc */
  idx=0;     /* silence gcc */
  switch(message)
    {
    case WM_CLOSE:
      DestroyWindow(hWnd);
      break;
    case WM_COMMAND:
      n=ListView_GetItemCount(GetDlgItem(hWnd,ID_PRODCHANGE_LIST));
      sel=-1;
      for(i=0;i<n;i++) {
	if (ListView_GetItemState(GetDlgItem(hWnd,ID_PRODCHANGE_LIST),i,
				  LVIS_SELECTED)) {
	  sel=i;
	  break;
	}
      }
      if (sel>=0) {
	idx=pdialog->change_list_ids[sel];
	is_unit=(sel >= pdialog->change_list_num_improvements);
      }
      switch(LOWORD(wParam))
	{
	case ID_PRODCHANGE_CANCEL:
	  DestroyWindow(hWnd);
	  break;
	case ID_PRODCHANGE_HELP:
	  if (sel>=0)
	    {
	      if (is_unit) {
		popup_help_dialog_typed(get_unit_type(idx)->name,
					HELP_UNIT);
	      } else if(is_wonder(idx)) {
		popup_help_dialog_typed(get_improvement_name(idx),
					HELP_WONDER);
	      } else {
		popup_help_dialog_typed(get_improvement_name(idx),
					HELP_IMPROVEMENT);
	      }                                                                     
	    }
	  else
	    {
	      popup_help_dialog_string(HELP_IMPROVEMENTS_ITEM);  
	    }
	  break;
	case ID_PRODCHANGE_CHANGE:
	  if (sel>=0)
	    {
	      struct packet_city_request packet;  
	      packet.city_id=pdialog->pcity->id;
	      packet.build_id=idx;
	      packet.is_build_id_unit_id=is_unit;
	      
	      send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
	      DestroyWindow(hWnd);
	    }
	  break;
	}
      break;
    case WM_DESTROY:
    case WM_SIZE:
    case WM_GETMINMAXINFO:
      break;
    default:
      return DefWindowProc(hWnd,message,wParam,lParam);
    }
  return 0;
}

/**************************************************************************
...
**************************************************************************/

     
void change_callback(struct city_dialog *pdialog)
{
  HWND dlg;
  
  char *row   [4];
  char  buf   [4][64];

  int i,n;

  dlg=fcwin_create_layouted_window(changedlg_proc,_("Change Production"),
				   WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,pdialog->mainwindow,NULL,pdialog);
  if (dlg)
    {
      struct fcwin_box *hbox;
      struct fcwin_box *vbox;
      HWND lv;
      LV_COLUMN lvc;
      LV_ITEM lvi;
      vbox=fcwin_vbox_new(dlg,FALSE);
      hbox=fcwin_hbox_new(dlg,TRUE);
      fcwin_box_add_button(hbox,_("Change"),ID_PRODCHANGE_CHANGE,
			   0,TRUE,TRUE,20);
      fcwin_box_add_button(hbox,_("Cancel"),ID_PRODCHANGE_CANCEL,
			   0,TRUE,TRUE,20);
      fcwin_box_add_button(hbox,_("Help"),ID_PRODCHANGE_HELP,
			   0,TRUE,TRUE,20);
      lv=fcwin_box_add_listview(vbox,10,ID_PRODCHANGE_LIST,LVS_REPORT | LVS_SINGLESEL,
				TRUE,TRUE,10);
      fcwin_box_add_box(vbox,hbox,FALSE,FALSE,10);
  
      lvc.pszText=_("Type");
      lvc.mask=LVCF_TEXT;
      ListView_InsertColumn(lv,0,&lvc);
      lvc.pszText=_("Info");
      lvc.mask=LVCF_TEXT | LVCF_FMT;
      lvc.fmt=LVCFMT_RIGHT;
      ListView_InsertColumn(lv,1,&lvc);
      lvc.pszText=_("Cost");
      ListView_InsertColumn(lv,2,&lvc);
      lvc.pszText=_("Turns");
      ListView_InsertColumn(lv,3,&lvc);
      lvi.mask=LVIF_TEXT;
      for(i=0; i<4; i++)
	row[i]=buf[i];
	
      n = 0;
      impr_type_iterate(i) {
	if(can_build_improvement(pdialog->pcity, i)) {
	  get_city_dialog_production_row(row, sizeof(buf[0]), i,
	                                 FALSE, pdialog->pcity);
	  fcwin_listview_add_row(lv,n,4,row);
	  pdialog->change_list_ids[n++]=i;
	}
      } impr_type_iterate_end;
	
      pdialog->change_list_num_improvements=n;
      
      unit_type_iterate(i) {
	if(can_build_unit(pdialog->pcity, i)) {
	  get_city_dialog_production_row(row, sizeof(buf[0]), i,
	                                 TRUE, pdialog->pcity);
	  fcwin_listview_add_row(lv,n,4,row);
	  pdialog->change_list_ids[n++]=i;
	}
      } unit_type_iterate_end;
      
      ListView_SetColumnWidth(lv,0,LVSCW_AUTOSIZE);
      for(i=1;i<4;i++) {
	ListView_SetColumnWidth(lv,i,LVSCW_AUTOSIZE_USEHEADER);	
      }
      fcwin_set_box(dlg,vbox);
      ShowWindow(dlg,SW_SHOWNORMAL);
    }
  
}


/****************************************************************
  Commit the changes to the worklist for the city.
*****************************************************************/
static void commit_city_worklist(struct worklist *pwl, void *data)
{
  struct packet_city_request packet;
  struct city_dialog *pdialog = (struct city_dialog *) data;
  int k, id;
  bool is_unit;

  /* Update the worklist.  Remember, though -- the current build
     target really isn't in the worklist; don't send it to the server
     as part of the worklist.  Of course, we have to search through
     the current worklist to find the first _now_available_ build
     target (to cope with players who try mean things like adding a
     Battleship to a city worklist when the player doesn't even yet
     have the Map Making tech).  */

  for (k = 0; k < MAX_LEN_WORKLIST; k++) {
    int same_as_current_build;
    if (!worklist_peek_ith(pwl, &id, &is_unit, k))
      break;

    same_as_current_build = id == pdialog->pcity->currently_building
        && is_unit == pdialog->pcity->is_building_unit;

    /* Very special case: If we are currently building a wonder we
       allow the construction to continue, even if we the wonder is
       finished elsewhere, ie unbuildable. */
    if (k == 0 && !is_unit && is_wonder(id) && same_as_current_build) {
      worklist_remove(pwl, k);
      break;
    }

    /* If it can be built... */
    if ((is_unit && can_build_unit(pdialog->pcity, id)) ||
        (!is_unit && can_build_improvement(pdialog->pcity, id))) {
      /* ...but we're not yet building it, then switch. */
      if (!same_as_current_build) {

        /* Change the current target */
        packet.city_id = pdialog->pcity->id;
        packet.build_id = id;
        packet.is_build_id_unit_id = is_unit;
        send_packet_city_request(&aconnection, &packet,
                                 PACKET_CITY_CHANGE);
      }

      /* This item is now (and may have always been) the current
         build target.  Drop it out of the worklist. */
      worklist_remove(pwl, k);
      break;
    }
  }

  /* Send the rest of the worklist on its way. */
  packet.city_id = pdialog->pcity->id;
  copy_worklist(&packet.worklist, pwl);
  packet.worklist.name[0] = '\0';
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_WORKLIST);
}

/**************************************************************************
...
**************************************************************************/


static void rename_city_callback(HWND w, void * data)
{
  struct city_dialog *pdialog;
  struct packet_city_request packet;
 
  if((pdialog=(struct city_dialog *)data)) {
    packet.city_id=pdialog->pcity->id;
    sz_strlcpy(packet.name, input_dialog_get_input(w));
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_RENAME);
  }
 
  input_dialog_destroy(w);
}
 
/****************************************************************
...
*****************************************************************/    
void rename_callback(struct city_dialog *pdialog)
{
  input_dialog_create(pdialog->mainwindow,
                      /*"shellrenamecity"*/_("Rename City"),
                      _("What should we rename the city to?"),
                      pdialog->pcity->name,
                      (void*)rename_city_callback, (void *)pdialog,
                      (void*)rename_city_callback, (void *)0);    
}

/**************************************************************************

**************************************************************************/
static LONG CALLBACK trade_page_proc(HWND win, UINT message,
				     WPARAM wParam, LPARAM lParam)
{
  struct city_dialog *pdialog;
  struct fcwin_box *vbox;
  switch(message) 
    {
    case WM_CREATE:
      pdialog=(struct city_dialog *)fcwin_get_user_data(win);
      vbox=fcwin_vbox_new(win,FALSE);
      pdialog->trade_label=fcwin_box_add_static(vbox," ",
					       0,SS_LEFT,
					       FALSE,FALSE,0);
      fcwin_set_box(win,vbox);
      break;
    case WM_DESTROY:
    case WM_SIZE:
    case WM_GETMINMAXINFO:
    case WM_COMMAND:
      break;
    default:
      return DefWindowProc(win,message,wParam,lParam);
    }
  return 0;
}

/****************************************************************
...
*****************************************************************/
static void city_dialog_update_tradelist(struct city_dialog *pdialog)
{
  int i, x = 0, total = 0;

  char cityname[64], buf[512];

  buf[0] = '\0';

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    if (pdialog->pcity->trade[i]) {
      struct city *pcity;
      x = 1;
      total += pdialog->pcity->trade_value[i];

      if ((pcity = find_city_by_id(pdialog->pcity->trade[i]))) {
        my_snprintf(cityname, sizeof(cityname), "%s", pcity->name);
      } else {
        my_snprintf(cityname, sizeof(cityname), _("%s"), _("Unknown"));
      }
      my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                  _("Trade with %s gives %d trade.\n"),
                  cityname, pdialog->pcity->trade_value[i]);
    }
  }
  if (!x) {
    my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                _("No trade routes exist."));
  } else {
    my_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                _("Total trade from trade routes: %d"), total);
  }
  SetWindowText(pdialog->trade_label,buf);
}

/**************************************************************************
...
**************************************************************************/


static void supported_units_activate_close_callback(HWND w, void * data){
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;
 
  destroy_message_dialog(w);
 
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data))) {
    activate_unit(punit);
    if((pcity=player_find_city_by_id(game.player_ptr, punit->homecity)))
      if((pdialog=get_city_dialog(pcity)))
        CityDlgClose(pdialog);
  }
}   

/**************************************************************************
...
**************************************************************************/


void activate_callback(struct city_dialog *pdialog)
{
  int x=pdialog->pcity->x,y=pdialog->pcity->y;
  struct unit_list *punit_list = &map_get_tile(x,y)->units;
  struct unit *pmyunit = NULL;
 
  if(unit_list_size(punit_list))  {
    unit_list_iterate((*punit_list), punit) {
      if(game.player_idx==punit->owner) {
        pmyunit = punit;
        request_new_unit_activity(punit, ACTIVITY_IDLE);
      }
    } unit_list_iterate_end;
    if (pmyunit)
      set_unit_focus(pmyunit);
  }
}      

/****************************************************************
...
*****************************************************************/
void show_units_callback(struct city_dialog *pdialog)
{
  struct tile *ptile = map_get_tile(pdialog->pcity->x, pdialog->pcity->y);
 
  if(unit_list_size(&ptile->units))
    popup_unit_select_dialog(ptile);
}
     

/**************************************************************************
...
**************************************************************************/


static void present_units_disband_callback(HWND w, void *data)
{
  struct unit *punit;
 
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    request_unit_disband(punit);
 
  destroy_message_dialog(w);
}
      
/****************************************************************
...
*****************************************************************/
static void present_units_homecity_callback(HWND w, void * data)
{
  struct unit *punit;
 
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    request_unit_change_homecity(punit);
 
  destroy_message_dialog(w);
}
 
/****************************************************************
...
*****************************************************************/
static void present_units_cancel_callback(HWND w, void *data)
{
  destroy_message_dialog(w);
}
 
/****************************************************************
...
*****************************************************************/              

static void present_units_activate_callback(HWND w, void * data)
{
  struct unit *punit;
 
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    activate_unit(punit);
  destroy_message_dialog(w);
}
 
/****************************************************************
...
*****************************************************************/
static void present_units_activate_close_callback(HWND w, void * data)
{
  struct unit *punit;
  struct city *pcity;
  struct city_dialog *pdialog;
 
  destroy_message_dialog(w);
 
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data))) {
    activate_unit(punit);
    if((pcity=map_get_city(punit->x, punit->y)))
      if((pdialog=get_city_dialog(pcity)))
       CityDlgClose(pdialog);
  }
}              

/**************************************************************************
...
**************************************************************************/


static void present_units_sentry_callback(HWND w, void * data)
{
  struct unit *punit;
 
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    request_unit_sentry(punit);
  destroy_message_dialog(w);
}
 
/****************************************************************
...
*****************************************************************/
static void present_units_fortify_callback(HWND w, void * data)
{
  struct unit *punit;
 
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data)))
    request_unit_fortify(punit);
  destroy_message_dialog(w);
}

/**************************************************************************
...
**************************************************************************/


void unitupgrade_callback_yes(HWND w, void * data)
{
  struct unit *punit;
 
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data))) {
    request_unit_upgrade(punit);
  }
  destroy_message_dialog(w);
}
 
 
/****************************************************************
...
*****************************************************************/
void unitupgrade_callback_no(HWND w, void * data)
{
  destroy_message_dialog(w);
}
            
/****************************************************************
...
*****************************************************************/       
void upgrade_callback(HWND w, void * data)
{
  struct unit *punit;
  char buf[512];
  int ut1,ut2;
  int value;
 
  if((punit=player_find_unit_by_id(game.player_ptr, (size_t)data))) {
    ut1=punit->type;
    /* printf("upgrade_callback for %s\n", unit_types[ut1].name); */
 
    ut2=can_upgrade_unittype(game.player_ptr,ut1);
 
    if (ut2==-1) {
      /* this shouldn't generally happen, but it is conceivable */
      my_snprintf(buf, sizeof(buf),
                  _("Sorry: cannot upgrade %s."), unit_types[ut1].name);
      popup_message_dialog(NULL, /*"upgradenodialog"*/_("Upgrade Unit!"), buf,
                           _("Darn"), unitupgrade_callback_no, 0,
                           NULL);
    } else {
      value=unit_upgrade_price(game.player_ptr, ut1, ut2);
 
      if(game.player_ptr->economic.gold>=value) {
        my_snprintf(buf, sizeof(buf), _("Upgrade %s to %s for %d gold?\n"
               "Treasury contains %d gold."),
               unit_types[ut1].name, unit_types[ut2].name,
               value, game.player_ptr->economic.gold);
        popup_message_dialog(NULL,
                             /*"upgradedialog"*/_("Upgrade Obsolete Units"), buf,
                             _("_Yes"),
                               unitupgrade_callback_yes, (void *)(punit->id),
                             _("_No"),
                               unitupgrade_callback_no, 0,
                             NULL);
      } else {
        my_snprintf(buf, sizeof(buf), _("Upgrading %s to %s costs %d gold.\n"
               "Treasury contains %d gold."),
               unit_types[ut1].name, unit_types[ut2].name,
               value, game.player_ptr->economic.gold);
        popup_message_dialog(NULL,
                             /*"upgradenodialog"*/_("Upgrade Unit!"), buf,
                             _("Darn"), unitupgrade_callback_no, 0,
                             NULL);
      }
    }
    destroy_message_dialog(w);
  }
}

/**************************************************************************
...
**************************************************************************/


void city_dlg_click_supported(struct city_dialog *pdialog, int n)
{
  struct unit *punit;
  struct city *pcity;  
  if((punit=player_find_unit_by_id(game.player_ptr, 
				   pdialog->support_unit_ids[n])) &&
     (pcity=map_get_city(punit->x, punit->y)) &&
     (pdialog=get_city_dialog(pcity))) {   
      popup_message_dialog(NULL,
           /*"supportunitsdialog"*/ _("Unit Commands"),
           unit_description(punit),
           _("_Activate unit"),
             present_units_activate_callback, punit->id,
           _("Activate unit, _close dialog"),
             supported_units_activate_close_callback, punit->id, /* act+c */
           _("_Disband unit"),
             present_units_disband_callback, punit->id,
           _("_Cancel"),
             present_units_cancel_callback, 0, 0,NULL);
  }
}

/**************************************************************************
...
**************************************************************************/


void city_dlg_click_present(struct city_dialog *pdialog,int n)
{
  struct unit *punit;
  struct city *pcity;
  HWND wd;
  if((punit=player_find_unit_by_id(game.player_ptr, 
				   pdialog->present_unit_ids[n])) &&
     (pcity=map_get_city(punit->x, punit->y)) &&
     (pdialog=get_city_dialog(pcity))) {   
     wd=popup_message_dialog(NULL,
                           /*"presentunitsdialog"*/_("Unit Commands"),
                           unit_description(punit),
                           _("_Activate unit"),
                             present_units_activate_callback, punit->id,
                           _("Activate unit, _close dialog"),
                             present_units_activate_close_callback, punit->id,
                           _("_Sentry unit"),
                             present_units_sentry_callback, punit->id,
                           _("_Fortify unit"),
                             present_units_fortify_callback, punit->id,
                           _("_Disband unit"),
                             present_units_disband_callback, punit->id,
                           _("Make new _homecity"),
                             present_units_homecity_callback, punit->id,
                           _("_Upgrade unit"),
                             upgrade_callback, punit->id,
                           _("_Cancel"),
                             present_units_cancel_callback, 0,
                           NULL);                   
     if (punit->activity == ACTIVITY_SENTRY
	 || !can_unit_do_activity(punit, ACTIVITY_SENTRY)) {
       message_dialog_button_set_sensitive(wd,2, FALSE);
     }
     if (punit->activity == ACTIVITY_FORTIFYING
	 || !can_unit_do_activity(punit, ACTIVITY_FORTIFYING)) {
       message_dialog_button_set_sensitive(wd,3, FALSE);
     }
     if (punit->homecity == pcity->id) {
       message_dialog_button_set_sensitive(wd,5, FALSE);
     }
     if (can_upgrade_unittype(game.player_ptr,punit->type) == -1) {
       message_dialog_button_set_sensitive(wd,6, FALSE);
     }        
   }
}

/**************************************************************************
...
**************************************************************************/


void city_dlg_click_map(struct city_dialog *pdialog,int x,int y)
{ 
  struct packet_city_request packet;    
  struct city *pcity;
  pcity=pdialog->pcity;
  packet.city_id=pcity->id;
  packet.worker_x=x;
  packet.worker_y=y;
  
  if(pcity->city_map[x][y]==C_TILE_WORKER)
    send_packet_city_request(&aconnection, &packet,
			     PACKET_CITY_MAKE_SPECIALIST);
  else if(pcity->city_map[x][y]==C_TILE_EMPTY)
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_MAKE_WORKER);
  
}

/**************************************************************************
...
**************************************************************************/


void city_dlg_click_citizens(struct city_dialog *pdialog,int n)
{
  struct packet_city_request packet;
  if (pdialog->citizen_type[n]<0)
    return;
  if (pdialog->citizen_type[n]>2)
    return;
  packet.city_id=pdialog->pcity->id;
  switch (pdialog->citizen_type[n])
    {
    case 0: /* elvis */
      packet.specialist_from=SP_ELVIS;
      packet.specialist_to=SP_SCIENTIST;
      send_packet_city_request(&aconnection, &packet,
			       PACKET_CITY_CHANGE_SPECIALIST);   
      break;
    case 1: /* scientist */
      packet.specialist_from=SP_SCIENTIST;
      packet.specialist_to=SP_TAXMAN;   
      send_packet_city_request(&aconnection, &packet,
			       PACKET_CITY_CHANGE_SPECIALIST);  
      break;
    case 2: /* taxman */
      packet.specialist_from=SP_TAXMAN;
      packet.specialist_to=SP_ELVIS;   
      send_packet_city_request(&aconnection, &packet,
			       PACKET_CITY_CHANGE_SPECIALIST);   
      break;
    }
}

/**************************************************************************
...
**************************************************************************/


static void city_dlg_mouse(struct city_dialog *pdialog, int x, int y,
			   bool is_overview)
{
  int xr,yr;
  /* click on citizens */
  
  if ((!is_overview)&&
      (y>=pdialog->pop_y)&&(y<(pdialog->pop_y+SMALL_TILE_HEIGHT)))
    {
      xr=x-pdialog->pop_x;
      if (x>=0)
	{
	  xr/=SMALL_TILE_WIDTH;
	  if (xr<NUM_CITIZENS_SHOWN)
	    {
	      city_dlg_click_citizens(pdialog,xr);
	      return;
	    }
	}
    }
  
  if (!is_overview)
    return;
  /* click on map */
  if ((y>=pdialog->map_y)&&(y<(pdialog->map_y+pdialog->map_h)))
    {
      if ((x>=pdialog->map_x)&&(x<(pdialog->map_x+pdialog->map_w)))
	{
	  int tile_x,tile_y;
	  xr=x-pdialog->map_x;
	  yr=y-pdialog->map_y;
	  canvas_pos_to_city_pos(xr,yr,&tile_x,&tile_y);
	  city_dlg_click_map(pdialog,tile_x,tile_y);
	}
    }
  xr=x-pdialog->pop_x;
  if (xr<0) return;
  if (xr%(NORMAL_TILE_WIDTH+SMALL_TILE_WIDTH)>NORMAL_TILE_WIDTH)
    return;
  xr/=(NORMAL_TILE_WIDTH+SMALL_TILE_WIDTH);
  
  /* click on present units */
  if ((y>=pdialog->present_y)&&(y<(pdialog->present_y+NORMAL_TILE_HEIGHT)))
    {
      city_dlg_click_present(pdialog,xr);
      return;
    }
  if ((y>=pdialog->supported_y)&&
      (y<(pdialog->supported_y+NORMAL_TILE_HEIGHT+SMALL_TILE_HEIGHT)))
    {
      city_dlg_click_supported(pdialog,xr);
      return;
    }
}


/**************************************************************************

**************************************************************************/
static LONG CALLBACK citydlg_overview_proc(HWND win, UINT message,
					   WPARAM wParam, LPARAM lParam)
{
  HBITMAP old;
  HDC hdc;
  PAINTSTRUCT ps;
  struct city_dialog *pdialog;
  pdialog=fcwin_get_user_data(win);
  if (!pdialog) {
    return DefWindowProc(win,message,wParam,lParam);
  }
  
  switch(message)
    {
    case WM_CREATE:
    case WM_SIZE:
    case WM_GETMINMAXINFO:
    case WM_DESTROY:
      break;
    case WM_PAINT:
      hdc=BeginPaint(win,(LPPAINTSTRUCT)&ps);
      old=SelectObject(citydlgdc,pdialog->map_bmp);
      BitBlt(hdc,pdialog->map_x,pdialog->map_y,
	     pdialog->map_w,pdialog->map_h,
	     citydlgdc,0,0,SRCCOPY);
      SelectObject(citydlgdc,old);
      city_dialog_update_supported_units(hdc,pdialog, 0);
      city_dialog_update_present_units(hdc,pdialog, 0); 
      EndPaint(win,(LPPAINTSTRUCT)&ps);
      break;
    case WM_LBUTTONDOWN:
      city_dlg_mouse(pdialog,LOWORD(lParam),HIWORD(lParam),TRUE);
      break;
    case WM_COMMAND:
      switch(LOWORD(wParam)) 
	{
	case ID_CITY_BUY:
	  buy_callback(pdialog);
	  break;
	case ID_CITY_SELL:
	  sell_callback(pdialog);
	  break;
	case ID_CITY_CHANGE:
	  change_callback(pdialog);
	  break;
	}
      break;
    default:
      return DefWindowProc(win,message,wParam,lParam);
    }
  return 0;
}

/**************************************************************************
...
**************************************************************************/


LONG APIENTRY CitydlgWndProc (
                           HWND hWnd,
                           UINT message,
                           UINT wParam,
                           LONG lParam)
{
  HBITMAP old;
  LPNMHDR nmhdr;
  HDC hdc;
  PAINTSTRUCT ps;
  struct city_dialog *pdialog;
  if (message==WM_CREATE)
    {
      CityDlgCreate(hWnd,
		    (struct city_dialog *)
		    fcwin_get_user_data(hWnd));
      return 0;
    }
  pdialog=fcwin_get_user_data(hWnd);
  if (!pdialog) return DefWindowProc(hWnd,message,wParam,lParam);
  switch(message)
    {
    case WM_CLOSE:
      CityDlgClose(pdialog);
      break;
    case WM_DESTROY:
      genlist_unlink(&dialog_list,pdialog);
      DeleteObject(pdialog->map_bmp);
      DeleteObject(pdialog->citizen_bmp);
      free(pdialog);
      break;
    case WM_GETMINMAXINFO:
      break;
    case WM_SIZE:
      break;
    case WM_PAINT:
      hdc=BeginPaint(hWnd,(LPPAINTSTRUCT)&ps);
      old=SelectObject(citydlgdc,pdialog->citizen_bmp);
      BitBlt(hdc,pdialog->pop_x,pdialog->pop_y,
	     SMALL_TILE_WIDTH*NUM_CITIZENS_SHOWN,SMALL_TILE_HEIGHT,
	     citydlgdc,0,0,SRCCOPY);
      SelectObject(citydlgdc,old);
      /*
      city_dialog_update_map(hdc,pdialog);
      city_dialog_update_citizens(hdc,pdialog); */
      /*      city_dialog_update_production(hdc,pdialog);
      city_dialog_update_output(hdc,pdialog);             
      city_dialog_update_storage(hdc,pdialog);
      city_dialog_update_pollution(hdc,pdialog);  */ 
      EndPaint(hWnd,(LPPAINTSTRUCT)&ps);
      break;
    case WM_LBUTTONDOWN:
      city_dlg_mouse(pdialog,LOWORD(lParam),HIWORD(lParam),FALSE);
      break;
    case WM_COMMAND:
      switch(LOWORD(wParam))
	{

	case ID_CITY_CLOSE:
	  CityDlgClose(pdialog);
	  break;
	case ID_CITY_RENAME:
	  rename_callback(pdialog);
	  break;
	case ID_CITY_ACTIVATE:
	  activate_callback(pdialog);
	  break;
	case ID_CITY_UNITLIST:
	  show_units_callback(pdialog);
	  break;
	}
      break;
    case WM_NOTIFY:
      nmhdr=(LPNMHDR)lParam;
      if (nmhdr->hwndFrom==pdialog->tab_ctrl) {
	int i,sel;
	sel=TabCtrl_GetCurSel(pdialog->tab_ctrl);
	for(i=0;i<NUM_TABS;i++) {
	  /*  if (i!=sel) */
	  ShowWindow(pdialog->tab_childs[i],SW_HIDE); 
	}
	if ((sel>=0)&&(sel<NUM_TABS))
	  ShowWindow(pdialog->tab_childs[sel],SW_SHOWNORMAL);
	SendMessage(pdialog->tab_childs[PAGE_WORKLIST],WM_COMMAND,IDOK,0);
      }
      break;
    default:
      return DefWindowProc(hWnd,message,wParam,lParam);
    }
  return (0);
}

/**************************************************************************
...
**************************************************************************/

struct city_dialog *create_city_dialog(struct city *pcity, bool make_modal)
{   
  struct city_dialog *pdialog;
  pdialog=fc_malloc(sizeof(struct city_dialog));
  pdialog->pcity=pcity;
  pdialog->resized=0;
  pdialog->cityopt_dialog=NULL;
  pdialog->mainwindow=
    fcwin_create_layouted_window(CitydlgWndProc,pcity->name,
			   WS_OVERLAPPEDWINDOW,
			   20,20,
			   root_window,
			   NULL,
			   pdialog);


  resize_city_dialog(pdialog);
  refresh_city_dialog(pdialog->pcity); 
  ShowWindow(pdialog->mainwindow,SW_SHOWNORMAL);
  return pdialog;
  
}
/****************************************************************
...
*****************************************************************/
static void initialize_city_dialogs(void)
{
  assert(!city_dialogs_have_been_initialised);

  genlist_init(&dialog_list);
  if (is_isometric) {
      city_map_width=4*NORMAL_TILE_WIDTH;
      city_map_height=4*NORMAL_TILE_HEIGHT;
    } else {
      city_map_width=5*NORMAL_TILE_WIDTH;
      city_map_height=5*NORMAL_TILE_HEIGHT;
    }
  city_dialogs_have_been_initialised=1;
}


/**************************************************************************
...
**************************************************************************/

struct city_dialog *get_city_dialog(struct city *pcity)
{   
  struct genlist_iterator myiter;
  if (!city_dialogs_have_been_initialised)
    initialize_city_dialogs();
    
  genlist_iterator_init(&myiter, &dialog_list, 0);
  
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct city_dialog *)ITERATOR_PTR(myiter))->pcity==pcity)
      return ITERATOR_PTR(myiter);
  
  return 0;      
}

/**************************************************************************
...
**************************************************************************/

void
popup_city_dialog(struct city *pcity, bool make_modal)
{
  struct city_dialog *pdialog;
 
  if(!(pdialog=get_city_dialog(pcity)))
    pdialog=create_city_dialog(pcity, make_modal);                                      
}

/**************************************************************************
...
**************************************************************************/


void
popdown_city_dialog(struct city *pcity)
{
  struct city_dialog *pdialog;
  
  if((pdialog=get_city_dialog(pcity)))
     CityDlgClose(pdialog);
}

/**************************************************************************
...
**************************************************************************/


void popdown_cityopt_dialog()
{
}

/**************************************************************************
...
**************************************************************************/


void
popdown_all_city_dialogs(void)
{
  if(!city_dialogs_have_been_initialised) {
    return;
  }
  while(genlist_size(&dialog_list)) {
    CityDlgClose(genlist_get(&dialog_list,0));
  }
  popdown_cityopt_dialog();     
}

/**************************************************************************
...
**************************************************************************/


void
refresh_unit_city_dialogs(struct unit *punit)
{
  struct city *pcity_sup, *pcity_pre;
  struct city_dialog *pdialog;      
  pcity_sup=player_find_city_by_id(game.player_ptr, punit->homecity);
  pcity_pre=map_get_city(punit->x, punit->y);     
  
  if(pcity_sup && (pdialog=get_city_dialog(pcity_sup)))     
    {
      HDC hdc;
      hdc=GetDC(pdialog->mainwindow);
      city_dialog_update_supported_units(hdc,pdialog,0);
      ReleaseDC(pdialog->mainwindow,hdc);
    }
  if(pcity_pre && (pdialog=get_city_dialog(pcity_pre)))   
    {
      HDC hdc;
      hdc=GetDC(pdialog->mainwindow);
      city_dialog_update_present_units(hdc,pdialog,0);
      ReleaseDC(pdialog->mainwindow,hdc);
    }
}

/** City options dialog **/
#define NUM_CITYOPT_TOGGLES 5  

/**************************************************************************
...
**************************************************************************/


LONG APIENTRY citydlg_config_proc(HWND hWnd,
				   UINT message,
				   UINT wParam,
				   LONG lParam)
{
  struct city_dialog *pdialog;
  pdialog=(struct city_dialog *)fcwin_get_user_data(hWnd);
  switch (message)
    {
    case WM_CREATE:
      {
	int i;
	int ncitizen_idx;
	struct fcwin_box *vbox;
	struct city *pcity;
	pdialog=(struct city_dialog *)fcwin_get_user_data(hWnd);
	pcity=pdialog->pcity;
	pdialog->cityopt_dialog=hWnd;
	vbox=fcwin_vbox_new(hWnd,FALSE);
	pdialog->opt_winbox=vbox;
	fcwin_box_add_static_default(vbox,pdialog->pcity->name,-1,SS_CENTER);
	fcwin_box_add_static_default(vbox,_("New citizens are"),-1,SS_LEFT);
	fcwin_box_add_radiobutton(vbox,_("Elvises"),ID_CITYOPT_RADIO,
				  WS_GROUP,FALSE,FALSE,5);
	fcwin_box_add_radiobutton(vbox,_("Scientists"),ID_CITYOPT_RADIO+1,
				  0,FALSE,FALSE,5);
	fcwin_box_add_radiobutton(vbox,_("Taxmen"),ID_CITYOPT_RADIO+2,
				  0,FALSE,FALSE,5);
	fcwin_box_add_static_default(vbox," ",-1,SS_LEFT | WS_GROUP);
	fcwin_box_add_checkbox(vbox,_("Disband if build settler at size 1"),
				  ID_CITYOPT_TOGGLE+4,0,FALSE,FALSE,5);
	fcwin_box_add_checkbox(vbox,_("Auto-attack vs land units"),
				  ID_CITYOPT_TOGGLE,0,FALSE,FALSE,5);
	fcwin_box_add_checkbox(vbox,_("Auto-attack vs sea units"),
				  ID_CITYOPT_TOGGLE+1,0,FALSE,FALSE,5);
	fcwin_box_add_checkbox(vbox,_("Auto-attack vs air units"),
				  ID_CITYOPT_TOGGLE+3,0,FALSE,FALSE,5);
	fcwin_box_add_checkbox(vbox,_("Auto-attack vs helicopters"),
				  ID_CITYOPT_TOGGLE+2,0,FALSE,FALSE,5);
	fcwin_set_box(hWnd,vbox);
	for(i=0; i<NUM_CITYOPT_TOGGLES; i++) {
	  CheckDlgButton(pdialog->cityopt_dialog,
			 i + ID_CITYOPT_TOGGLE,
			 is_city_option_set(pcity, i) ? BST_CHECKED : BST_UNCHECKED);
	}
	if (is_city_option_set(pcity, CITYO_NEW_EINSTEIN)) {
	  ncitizen_idx = 1;
	} else if (is_city_option_set(pcity, CITYO_NEW_TAXMAN)) {
	  ncitizen_idx = 2;
	} else {
	  ncitizen_idx = 0; 
	}
	CheckRadioButton(pdialog->cityopt_dialog,
			 ID_CITYOPT_RADIO,ID_CITYOPT_RADIO+2,
			 ncitizen_idx+ID_CITYOPT_RADIO);
      }
      break;
    case WM_DESTROY:
      break;
    case WM_COMMAND:
      if (pdialog) {
	struct city *pcity=pdialog->pcity;
	struct packet_generic_values packet;   
	int i,new_options;
	new_options=0;
	for(i=0;i<NUM_CITYOPT_TOGGLES;i++)
	  if (IsDlgButtonChecked(hWnd,ID_CITYOPT_TOGGLE+i))
	    new_options |= (1<<i);
	if (IsDlgButtonChecked(hWnd,ID_CITYOPT_RADIO+1))
	  new_options |= (1<<CITYO_NEW_EINSTEIN); 
	else if (IsDlgButtonChecked(hWnd,ID_CITYOPT_RADIO+2))
	  new_options |= (1<<CITYO_NEW_TAXMAN);  
	packet.value1=pcity->id;
	packet.value2=new_options;
	send_packet_generic_values(&aconnection, PACKET_CITY_OPTIONS,
				   &packet); 
      }
      break;
    default: 
      return DefWindowProc(hWnd,message,wParam,lParam); 
    }
  return 0;
} 
