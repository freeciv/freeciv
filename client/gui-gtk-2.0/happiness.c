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

#include <gtk/gtk.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "mem.h"
#include "support.h"

#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "happiness.h"
#include "tilespec.h"

/* semi-arbitrary number that controls the width of the happiness widget */
#define HAPPINESS_PIX_WIDTH 23

#define NUM_HAPPINESS_MODIFIERS 5

static struct genlist happiness_list;
static int happiness_list_has_been_initialised;
enum { CITIES, LUXURIES, BUILDINGS, UNITS, WONDERS };

struct happiness_dialog {
  struct city *pcity;
  GtkWidget *shell;
  GtkWidget *cityname_label;
  GtkWidget *hpixmaps[NUM_HAPPINESS_MODIFIERS];
  GtkWidget *hlabels[NUM_HAPPINESS_MODIFIERS];
  GtkWidget *close;
};

static GdkPixmap *create_happiness_pixmap(struct city *pcity, int index);
static struct happiness_dialog *get_happiness_dialog(struct city *pcity);
static struct happiness_dialog *create_happiness_dialog(struct city
							*pcity);
static void happiness_dialog_update_cities(struct happiness_dialog
					   *pdialog);
static void happiness_dialog_update_luxury(struct happiness_dialog
					   *pdialog);
static void happiness_dialog_update_buildings(struct happiness_dialog
					      *pdialog);
static void happiness_dialog_update_units(struct happiness_dialog
					  *pdialog);
static void happiness_dialog_update_wonders(struct happiness_dialog
					    *pdialog);

/****************************************************************
...
*****************************************************************/
static struct happiness_dialog *get_happiness_dialog(struct city *pcity)
{
  struct genlist_iterator myiter;

  if (!happiness_list_has_been_initialised) {
    genlist_init(&happiness_list);
    happiness_list_has_been_initialised = 1;
  }

  genlist_iterator_init(&myiter, &happiness_list, 0);

  for (; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if (((struct happiness_dialog *) ITERATOR_PTR(myiter))->pcity == pcity)
      return ITERATOR_PTR(myiter);

  return NULL;
}

/**************************************************************************
...
**************************************************************************/
static struct happiness_dialog *create_happiness_dialog(struct city *pcity)
{
  int i;
  struct happiness_dialog *pdialog;
  GtkWidget *vbox;
  /* GtkAccelGroup *accel=gtk_accel_group_new(); */

  pdialog = fc_malloc(sizeof(struct happiness_dialog));
  pdialog->pcity = pcity;

  pdialog->shell = gtk_vbox_new(FALSE, 0);

  pdialog->cityname_label = gtk_frame_new(_("Happiness"));
  gtk_box_pack_start(GTK_BOX(pdialog->shell),
		     pdialog->cityname_label, TRUE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(pdialog->cityname_label), vbox);
  gtk_container_border_width(GTK_CONTAINER(vbox), 2);


  for (i = 0; i < NUM_HAPPINESS_MODIFIERS; i++) {
    pdialog->hpixmaps[i] =
	gtk_pixmap_new(create_happiness_pixmap(pcity, i), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), pdialog->hpixmaps[i], FALSE, FALSE,
		       0);

    pdialog->hlabels[i] = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox), pdialog->hlabels[i], TRUE, FALSE, 0);

    gtk_misc_set_alignment(GTK_MISC(pdialog->hpixmaps[i]), 0, 0);
    gtk_misc_set_alignment(GTK_MISC(pdialog->hlabels[i]), 0, 0);
    gtk_label_set_justify(GTK_LABEL(pdialog->hlabels[i]),
			  GTK_JUSTIFY_LEFT);

    /* gtk_label_set_line_wrap(GTK_LABEL(pdialog->hlabels[i]), TRUE); */
  }

  gtk_widget_show_all(pdialog->shell);

  if (!happiness_list_has_been_initialised) {
    genlist_init(&happiness_list);
    happiness_list_has_been_initialised = 1;
  }

  genlist_insert(&happiness_list, pdialog, 0);

  refresh_happiness_dialog(pcity);

  return pdialog;
}

/**************************************************************************
...
**************************************************************************/
static GdkPixmap *create_happiness_pixmap(struct city *pcity, int index)
{
  int i;
  int citizen_type;
  int n1 = pcity->ppl_happy[index];
  int n2 = n1 + pcity->ppl_content[index];
  int n3 = n2 + pcity->ppl_unhappy[index];
  int n4 = n3 + pcity->ppl_elvis;
  int n5 = n4 + pcity->ppl_scientist;
  int num_citizens = pcity->size;
  int pix_width = HAPPINESS_PIX_WIDTH * SMALL_TILE_WIDTH;
  int offset = MIN(SMALL_TILE_WIDTH, pix_width / num_citizens);
  int true_pix_width = (num_citizens - 1) * offset + SMALL_TILE_WIDTH;

  GdkPixmap *happiness_pixmap = gdk_pixmap_new(root_window, true_pix_width,
					       SMALL_TILE_HEIGHT, -1);

  for (i = 0; i < num_citizens; i++) {
    if (i < n1)
      citizen_type = 5 + i % 2;
    else if (i < n2)
      citizen_type = 3 + i % 2;
    else if (i < n3)
      citizen_type = 7 + 1 % 2;
    else if (i < n4)
      citizen_type = 0;
    else if (i < n5)
      citizen_type = 1;
    else
      citizen_type = 2;

    gdk_draw_pixmap(happiness_pixmap, civ_gc,
		    sprites.citizen[citizen_type]->pixmap,
		    0, 0, i * offset, 0, SMALL_TILE_WIDTH,
		    SMALL_TILE_HEIGHT);
  }

  return happiness_pixmap;
}

/**************************************************************************
...
**************************************************************************/
void refresh_happiness_dialog(struct city *pcity)
{
  int i;

  struct happiness_dialog *pdialog = get_happiness_dialog(pcity);

  for (i = 0; i < 5; i++) {
    gtk_pixmap_set(GTK_PIXMAP(pdialog->hpixmaps[i]),
		   create_happiness_pixmap(pdialog->pcity, i), NULL);
  }

  happiness_dialog_update_cities(pdialog);
  happiness_dialog_update_luxury(pdialog);
  happiness_dialog_update_buildings(pdialog);
  happiness_dialog_update_units(pdialog);
  happiness_dialog_update_wonders(pdialog);
}

/**************************************************************************
...
**************************************************************************/
void close_happiness_dialog(struct city *pcity)
{
  struct happiness_dialog *pdialog = get_happiness_dialog(pcity);

  gtk_widget_hide(pdialog->shell);
  genlist_unlink(&happiness_list, pdialog);

  gtk_widget_destroy(pdialog->shell);
  free(pdialog);
}

/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_cities(struct happiness_dialog
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

  gtk_set_label(pdialog->hlabels[CITIES], buf);
}

/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_luxury(struct happiness_dialog
					   *pdialog)
{
  char buf[512], *bptr = buf;
  int nleft = sizeof(buf);
  struct city *pcity = pdialog->pcity;

  my_snprintf(bptr, nleft, _("Luxury: %d total (maximum %d usable). "),
	      pcity->luxury_total, 2 * pcity->size);

  gtk_set_label(pdialog->hlabels[LUXURIES], buf);
}

/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_buildings(struct happiness_dialog
					      *pdialog)
{
  int faces = 0;
  char buf[512], *bptr = buf;
  int nleft = sizeof(buf);
  struct city *pcity = pdialog->pcity;
  struct government *g = get_gov_pcity(pcity);

  my_snprintf(bptr, nleft, _("Buildings: "));
  bptr = end_of_strn(bptr, &nleft);

  if (city_got_building(pcity, B_TEMPLE)) {
    faces++;
    my_snprintf(bptr, nleft, get_improvement_name(B_TEMPLE));
    bptr = end_of_strn(bptr, &nleft);
    my_snprintf(bptr, nleft, _(". "));
    bptr = end_of_strn(bptr, &nleft);
  }
  if (city_got_building(pcity, B_COURTHOUSE) && g->corruption_level == 0) {
    faces++;
    my_snprintf(bptr, nleft, get_improvement_name(B_COURTHOUSE));
    bptr = end_of_strn(bptr, &nleft);
    my_snprintf(bptr, nleft, _(". "));
    bptr = end_of_strn(bptr, &nleft);
  }
  if (city_got_building(pcity, B_COLOSSEUM)) {
    faces++;
    my_snprintf(bptr, nleft, get_improvement_name(B_COLOSSEUM));
    bptr = end_of_strn(bptr, &nleft);
    my_snprintf(bptr, nleft, _(". "));
    bptr = end_of_strn(bptr, &nleft);
  }
  /* hack for eliminating gtk_set_line_wrap() -mck */
  if (faces > 2) {
    /* sizeof("Buildings: ") */
    my_snprintf(bptr, nleft, _("\n              "));
    bptr = end_of_strn(bptr, &nleft);
  }
  if (city_got_effect(pcity, B_CATHEDRAL)) {
    faces++;
    my_snprintf(bptr, nleft, get_improvement_name(B_CATHEDRAL));
    bptr = end_of_strn(bptr, &nleft);
    if (!city_got_building(pcity, B_CATHEDRAL)) {
      my_snprintf(bptr, nleft, _("("));
      bptr = end_of_strn(bptr, &nleft);
      my_snprintf(bptr, nleft, get_improvement_name(B_MICHELANGELO));
      bptr = end_of_strn(bptr, &nleft);
      my_snprintf(bptr, nleft, _(")"));
      bptr = end_of_strn(bptr, &nleft);
    }
    my_snprintf(bptr, nleft, _(". "));
    bptr = end_of_strn(bptr, &nleft);
  }

  if (faces == 0) {
    my_snprintf(bptr, nleft, _("None. "));
    bptr = end_of_strn(bptr, &nleft);
  }

  gtk_set_label(pdialog->hlabels[BUILDINGS], buf);
}

/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_units(struct happiness_dialog *pdialog)
{
  char buf[512], *bptr = buf;
  int nleft = sizeof(buf);
  struct city *pcity = pdialog->pcity;
  struct government *g = get_gov_pcity(pcity);
  int mlmax = g->martial_law_max;

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
  } else {
    my_snprintf(bptr, nleft,
		_("Military units in the field may cause unhappiness. "));
  }

  gtk_set_label(pdialog->hlabels[UNITS], buf);
}

/**************************************************************************
...
**************************************************************************/
static void happiness_dialog_update_wonders(struct happiness_dialog
					    *pdialog)
{
  int faces = 0;
  char buf[512], *bptr = buf;
  int nleft = sizeof(buf);
  struct city *pcity = pdialog->pcity;

  my_snprintf(bptr, nleft, _("Wonders: "));
  bptr = end_of_strn(bptr, &nleft);

  if (city_affected_by_wonder(pcity, B_HANGING)) {
    faces++;
    my_snprintf(bptr, nleft, get_improvement_name(B_HANGING));
    bptr = end_of_strn(bptr, &nleft);
    my_snprintf(bptr, nleft, _(". "));
    bptr = end_of_strn(bptr, &nleft);
  }
  if (city_affected_by_wonder(pcity, B_BACH)) {
    faces++;
    my_snprintf(bptr, nleft, get_improvement_name(B_BACH));
    bptr = end_of_strn(bptr, &nleft);
    my_snprintf(bptr, nleft, _(". "));
    bptr = end_of_strn(bptr, &nleft);
  }
  /* hack for eliminating gtk_set_line_wrap() -mck */
  if (faces > 1) {
    /* sizeof("Wonders: ") */
    my_snprintf(bptr, nleft, _("\n              "));
    bptr = end_of_strn(bptr, &nleft);
  }
  if (city_affected_by_wonder(pcity, B_SHAKESPEARE)) {
    faces++;
    my_snprintf(bptr, nleft, get_improvement_name(B_SHAKESPEARE));
    bptr = end_of_strn(bptr, &nleft);
    my_snprintf(bptr, nleft, _(". "));
    bptr = end_of_strn(bptr, &nleft);
  }
  if (city_affected_by_wonder(pcity, B_CURE)) {
    faces++;
    my_snprintf(bptr, nleft, get_improvement_name(B_CURE));
    bptr = end_of_strn(bptr, &nleft);
    my_snprintf(bptr, nleft, _(". "));
    bptr = end_of_strn(bptr, &nleft);
  }

  if (faces == 0) {
    my_snprintf(bptr, nleft, _("None. "));
  }

  gtk_set_label(pdialog->hlabels[WONDERS], buf);
}

/**************************************************************************
...
**************************************************************************/
GtkWidget *get_top_happiness_display(struct city *pcity)
{
  return create_happiness_dialog(pcity)->shell;
}
