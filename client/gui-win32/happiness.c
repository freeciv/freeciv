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

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "mem.h"
#include "support.h"

#include "text.h"
#include "tilespec.h"

#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "happiness.h"

#define HAPPINESS_PIX_WIDTH 23

#define NUM_HAPPINESS_MODIFIERS 5
enum { CITIES, LUXURIES, BUILDINGS, UNITS, WONDERS };

struct happiness_dlg {
  struct city *pcity;
  HWND win;
  HBITMAP mod_bmp[NUM_HAPPINESS_MODIFIERS];
  HWND mod_label[NUM_HAPPINESS_MODIFIERS];
  POINT mod_bmp_pos[NUM_HAPPINESS_MODIFIERS];
};

static void happiness_dialog_update_cities(struct happiness_dlg
                                           *pdialog);
static void happiness_dialog_update_luxury(struct happiness_dlg
                                           *pdialog);
static void happiness_dialog_update_buildings(struct happiness_dlg
                                              *pdialog);
static void happiness_dialog_update_units(struct happiness_dlg
                                          *pdialog);
static void happiness_dialog_update_wonders(struct happiness_dlg
                                            *pdialog);
static void refresh_happiness_bitmap(HBITMAP bmp,
				     struct city *pcity, int index);

/**************************************************************************
...
**************************************************************************/
static void bmp_row_minsize(POINT *minsize, void *data)
{
  minsize->x=HAPPINESS_PIX_WIDTH*SMALL_TILE_WIDTH;
  minsize->y=SMALL_TILE_HEIGHT;
}

/**************************************************************************
...
**************************************************************************/
static void bmp_row_setsize(RECT *size, void *data)
{
  POINT *pt;
  pt=(POINT *)data;
  pt->x=size->left;
  pt->y=size->top;
}

/**************************************************************************
...
**************************************************************************/
struct happiness_dlg *create_happiness_box(struct city *pcity,
					   struct fcwin_box *hbox,
					   HWND win)
{
  int i;
  HDC hdc;
  struct happiness_dlg *dlg;
  struct fcwin_box *vbox;
  vbox=fcwin_vbox_new(win,FALSE);
  fcwin_box_add_groupbox(hbox,_("Happiness"),vbox,0,
			 TRUE,TRUE,0);
  dlg=fc_malloc(sizeof(struct happiness_dlg));
  dlg->win=win;
  dlg->pcity=pcity;
  for(i=0;i<NUM_HAPPINESS_MODIFIERS;i++) {
    fcwin_box_add_generic(vbox,bmp_row_minsize,bmp_row_setsize,NULL,
			  &(dlg->mod_bmp_pos[i]),FALSE,FALSE,0);
    dlg->mod_label[i]=fcwin_box_add_static(vbox," ",0,SS_LEFT,
					   FALSE,FALSE,5);
  }
  hdc=GetDC(win);
  for(i=0;i<NUM_HAPPINESS_MODIFIERS;i++) {
    dlg->mod_bmp[i]=
      CreateCompatibleBitmap(hdc,
			     HAPPINESS_PIX_WIDTH*SMALL_TILE_WIDTH,
			     SMALL_TILE_HEIGHT);
  }
  ReleaseDC(win,hdc);
  return dlg;
}

/****************************************************************************

****************************************************************************/
void cleanup_happiness_box(struct happiness_dlg *dlg)
{
  int i;
  for(i=0;i<NUM_HAPPINESS_MODIFIERS;i++) {
    DeleteObject(dlg->mod_bmp[i]);
  }
  free(dlg);
}

/****************************************************************************

****************************************************************************/
void repaint_happiness_box(struct happiness_dlg *dlg, HDC hdc)
{
  int i;
  HDC hdcsrc;
  HBITMAP old;
  hdcsrc=CreateCompatibleDC(hdc);
  for(i=0;i<NUM_HAPPINESS_MODIFIERS;i++) {
    old=SelectObject(hdcsrc,dlg->mod_bmp[i]);
    BitBlt(hdc,dlg->mod_bmp_pos[i].x,dlg->mod_bmp_pos[i].y,
	   HAPPINESS_PIX_WIDTH*SMALL_TILE_WIDTH,
	   SMALL_TILE_HEIGHT,hdcsrc,0,0,SRCCOPY);
    SelectObject(hdcsrc,old);
  }
  DeleteDC(hdcsrc);
}

/****************************************************************
...
*****************************************************************/
void refresh_happiness_box(struct happiness_dlg *dlg)
{
  HDC hdc;
  int i;
  for(i=0;i<NUM_HAPPINESS_MODIFIERS;i++) {
    refresh_happiness_bitmap(dlg->mod_bmp[i],dlg->pcity,i);
  }
  hdc=GetDC(dlg->win);
  repaint_happiness_box(dlg,hdc);
  ReleaseDC(dlg->win,hdc);
  happiness_dialog_update_cities(dlg);
  happiness_dialog_update_luxury(dlg);
  happiness_dialog_update_buildings(dlg);
  happiness_dialog_update_units(dlg);
  happiness_dialog_update_wonders(dlg);
}

/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_cities(struct happiness_dlg
                                           *pdialog)
{
  char buf[512], *bptr = buf;
  int nleft = sizeof(buf);

  struct city *pcity = pdialog->pcity;
  struct player *pplayer = &game.players[pcity->owner];
  struct government *g = get_gov_pcity(pcity);
  int cities = city_list_size(&pplayer->cities);
  int content = game.unhappysize;
  int basis = game.cityfactor + g->empire_size_mod;
  int step = g->empire_size_inc;
  int excess = cities - basis;
  int penalty = 0;

  if (excess > 0) {
    if (step > 0)
      penalty = 1 + (excess - 1) / step;
    else
      penalty = 1;
  } else {
    excess = 0;
    penalty = 0;
  }

  my_snprintf(bptr, nleft,
              _("Cities: %d total, %d over threshold of %d cities.\n"),
              cities, excess, basis);
  bptr = end_of_strn(bptr, &nleft);

  my_snprintf(bptr, nleft, _("%d content before penalty with "), content);
  bptr = end_of_strn(bptr, &nleft);
  my_snprintf(bptr, nleft, _("%d additional unhappy citizens."), penalty);
  bptr = end_of_strn(bptr, &nleft);

  SetWindowText(pdialog->mod_label[CITIES], buf);
}


/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_luxury(struct happiness_dlg
                                           *pdialog)
{
  char buf[512], *bptr = buf;
  int nleft = sizeof(buf);
  struct city *pcity = pdialog->pcity;

  my_snprintf(bptr, nleft, _("Luxury: %d total."),
              pcity->luxury_total);

  SetWindowText(pdialog->mod_label[LUXURIES], buf);
}

/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_buildings(struct happiness_dlg
                                              *pdialog)
{
  SetWindowText(pdialog->mod_label[BUILDINGS],
		get_happiness_buildings(pdialog->pcity));
}

/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_units(struct happiness_dlg *pdialog)
{
  char buf[512], *bptr = buf;
  int nleft = sizeof(buf);
  struct city *pcity = pdialog->pcity;
  struct government *g = get_gov_pcity(pcity);
  int mlmax = g->martial_law_max;
  int uhcfac = g->unit_happy_cost_factor;

  my_snprintf(bptr, nleft, _("Units: "));
  bptr = end_of_strn(bptr, &nleft);

  if (mlmax > 0) {
    my_snprintf(bptr, nleft, _("Martial law in effect ("));
    bptr = end_of_strn(bptr, &nleft);

    if (mlmax == 100)
      my_snprintf(bptr, nleft, _("no maximum, "));
    else
      my_snprintf(bptr, nleft, PL_("%d unit maximum, ",
                                   "%d units maximum", mlmax), mlmax);
    bptr = end_of_strn(bptr, &nleft);

    my_snprintf(bptr, nleft, _("%d per unit). "), g->martial_law_per);
  }
  else if (uhcfac > 0) {
    my_snprintf(bptr, nleft,
                _("Military units in the field may cause unhappiness. "));
  }
  else {
    my_snprintf(bptr, nleft,
                _("Military units have no happiness effect. "));
  }

  SetWindowText(pdialog->mod_label[UNITS], buf);
}

/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_wonders(struct happiness_dlg
                                            *pdialog)
{
  SetWindowText(pdialog->mod_label[WONDERS],
		get_happiness_wonders(pdialog->pcity));
}

/**************************************************************************
...
**************************************************************************/
static void refresh_happiness_bitmap(HBITMAP bmp,
				     struct city *pcity, int index)
{
  HDC hdc;
  HBITMAP old;
  RECT rc;
  int i;
  struct citizen_type citizens[MAX_CITY_SIZE];
  int num_citizens = pcity->size;
  int pix_width = HAPPINESS_PIX_WIDTH * SMALL_TILE_WIDTH;
  int offset = MIN(SMALL_TILE_WIDTH, pix_width / num_citizens);
  /* int true_pix_width = (num_citizens - 1) * offset + SMALL_TILE_WIDTH; */
  hdc=CreateCompatibleDC(NULL);
  old=SelectObject(hdc,bmp);
  rc.left=0;
  rc.top=0;
  rc.right=pix_width;
  rc.bottom=SMALL_TILE_HEIGHT;
  FillRect(hdc,&rc,(HBRUSH)GetClassLong(root_window,GCL_HBRBACKGROUND));

  get_city_citizen_types(pcity, index, citizens);

  for (i = 0; i < num_citizens; i++) {
    draw_sprite(get_citizen_sprite(citizens[i], i, pcity),
		hdc, i * offset, 0);
  }

  SelectObject(hdc,old);
  DeleteDC(hdc);
}
