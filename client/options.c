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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "events.h"
#include "fcintl.h"
#include "log.h"
#include "registry.h"
#include "shared.h"
#include "support.h"
#include "version.h"

#include "chatline_g.h"
#include "cityrepdata.h"

#include "options.h"

/** Local Options: **/

int solid_color_behind_units=0;
int sound_bell_at_new_turn=0;
int smooth_move_units=1;
int smooth_move_unit_steps=3;
int do_combat_animation=1;
int ai_popup_windows=0;
int ai_manual_turn_done=1;
int auto_center_on_unit=1;
int auto_center_on_combat=0;
int wakeup_focus=1;
int draw_diagonal_roads=1;
int center_when_popup_city=1;
int concise_city_production=0;
int auto_turn_done=0;

#define GEN_OPTION(name, desc, type) { #name, desc, type, &name, NULL }
#define GEN_OPTION_TERMINATOR { NULL, NULL, COT_BOOL, NULL, NULL }

client_option options[] = {
  GEN_OPTION(solid_color_behind_units,	N_("Solid unit background color"), COT_BOOL),
  GEN_OPTION(sound_bell_at_new_turn,	N_("Sound bell at new turn     "), COT_BOOL),
  GEN_OPTION(smooth_move_units,		N_("Smooth unit moves          "), COT_BOOL),
  GEN_OPTION(smooth_move_unit_steps,	N_("Smooth unit move steps     "), COT_INT),
  GEN_OPTION(do_combat_animation,	N_("Show combat animation      "), COT_BOOL),
  GEN_OPTION(ai_popup_windows,		N_("Popup dialogs in AI Mode   "), COT_BOOL),
  GEN_OPTION(ai_manual_turn_done,	N_("Manual Turn Done in AI Mode"), COT_BOOL),
  GEN_OPTION(auto_center_on_unit,	N_("Auto Center on Units       "), COT_BOOL),
  GEN_OPTION(auto_center_on_combat,	N_("Auto Center on Combat      "), COT_BOOL),
  GEN_OPTION(wakeup_focus,		N_("Focus on Awakened Units    "), COT_BOOL),
  GEN_OPTION(draw_diagonal_roads,	N_("Draw Diagonal Roads/Rails  "), COT_BOOL),
  GEN_OPTION(center_when_popup_city,	N_("Center map when Popup city "), COT_BOOL),
  GEN_OPTION(concise_city_production,	N_("Concise City Production    "), COT_BOOL),
  GEN_OPTION(auto_turn_done, 		N_("End Turn when done moving  "), COT_BOOL),
  GEN_OPTION_TERMINATOR
};

/** View Options: **/

int draw_map_grid=0;
int draw_city_names=1;
int draw_city_productions=0;

#define VIEW_OPTION(name) { #name, &name }
#define VIEW_OPTION_TERMINATOR { NULL, NULL }

view_option view_options[] = {
  VIEW_OPTION(draw_map_grid),
  VIEW_OPTION(draw_city_names),
  VIEW_OPTION(draw_city_productions),
  VIEW_OPTION_TERMINATOR
};

/** Message Options: **/

unsigned int messages_where[E_LAST];
int sorted_events[E_LAST];

char *message_text[E_LAST]={
  N_("Low Funds                "), 	/* E_LOW_ON_FUNDS */
  N_("Pollution                "),
  N_("Global Eco-Disaster      "),	/* E_GLOBAL_ECO */
  N_("Civil Disorder           "),
  N_("City Celebrating         "),
  N_("City Normal              "),
  N_("City Growth              "),
  N_("City Needs Aqueduct      "),
  N_("Famine in City           "),
  N_("City Captured/Destroyed  "),
  N_("Building Unavailable Item"),
  N_("Wonder Started           "),
  N_("Wonder Finished          "),
  N_("Improvement Built        "),
  N_("New Improvement Selected "),
  N_("Forced Improvement Sale  "),
  N_("Production Upgraded      "),
  N_("Unit Built               "),
  N_("Unit Defender Destroyed  "),
  N_("Unit Defender Survived   "),
  N_("Collapse to Anarchy      "),
  N_("Diplomat Actions - Enemy "),
  N_("Tech from Great Library  "),
  N_("Player Destroyed         "),	/* E_DESTROYED */
  N_("Improvement Bought       "),	/* E_IMP_BUY */
  N_("Improvement Sold         "),	/* E_IMP_SOLD */
  N_("Unit Bought              "),	/* E_UNIT_BUY */
  N_("Wonder Stopped           "),	/* E_WONDER_STOPPED */
  N_("City Needs Aq Being Built"),	/* E_CITY_AQ_BUILDING */
  N_("Diplomat Actions - Own   "),	/* E_MY_DIPLOMAT */
  N_("Unit Attack Failed       "),	/* E_UNIT_LOST_ATT */
  N_("Unit Attack Succeeded    "),	/* E_UNIT_WIN_ATT */
  N_("Suggest Growth Throttling"),	/* E_CITY_GRAN_THROTTLE */
  N_("Spaceship Events         "),	/* E_SPACESHIP */
  N_("Barbarian Uprising       "),	/* E_UPRISING  */
  N_("Worklist Events          "),	/* E_WORKLIST */
  N_("Pact Cancelled           "),	/* E_CANCEL_PACT */
  N_("Diplomatic Incident      "),	/* E_DIPL_INCIDENT */
  N_("First Contact            "),	/* E_FIRST_CONTACT */
  N_("City May Soon Grow       "),	/* E_CITY_MAY_SOON_GROW */
  N_("Wonder Made Obsolete     "),      /* E_WONDER_OBSOLETE */
};

/**************************************************************************
  Comparison function for qsort; i1 and i2 are pointers to integers which
  index message_text[].
**************************************************************************/
static int compar_message_texts(const void *i1, const void *i2)
{
  int j1 = *(const int*)i1;
  int j2 = *(const int*)i2;
  
  return mystrcasecmp(_(message_text[j1]), _(message_text[j2]));
}

/****************************************************************
  These could be a static table initialisation, except
  its easier to do it this way.
  Now also initialise sorted_events[].
*****************************************************************/
void init_messages_where(void)
{
  int out_only[] = {E_IMP_BUY, E_IMP_SOLD, E_UNIT_BUY, E_MY_DIPLOMAT,
		   E_UNIT_LOST_ATT, E_UNIT_WIN_ATT};
  int i;

  for(i=0; i<E_LAST; i++) {
    messages_where[i] = MW_OUTPUT | MW_MESSAGES;
  }
  for(i=0; i<sizeof(out_only)/sizeof(int); i++) {
    messages_where[out_only[i]] = MW_OUTPUT;
  }
  
  for(i=0;i<E_LAST;i++)  {
    sorted_events[i] = i;
  }
  qsort(sorted_events, E_LAST, sizeof(int), compar_message_texts);
}


/****************************************************************
 The "options" file handles actual "options", and also view options,
 message options, and city report settings
*****************************************************************/

/****************************************************************
 Returns pointer to static memory containing name of option file.
 Ie, based on FREECIV_OPT env var, and home dir. (or a
 OPTION_FILE_NAME define defined in config.h)
 Or NULL if problem.
*****************************************************************/
static char *option_file_name(void)
{
  static char name_buffer[256];
  char *name;

  name = getenv("FREECIV_OPT");

  if (name) {
    sz_strlcpy(name_buffer, name);
  } else {
#ifndef OPTION_FILE_NAME
    name = user_home_dir();
    if (!name) {
      append_output_window(_("Cannot find your home directory"));
      return NULL;
    }
    mystrlcpy(name_buffer, name, 231);
    sz_strlcat(name_buffer, "/.civclientrc");
#else
    mystrlcpy(name_buffer,OPTION_FILE_NAME,sizeof(name_buffer));
#endif
  }
  freelog(LOG_VERBOSE, "settings file is %s", name_buffer);
  return name_buffer;
}
  
/****************************************************************
...								
*****************************************************************/
static FILE *open_option_file(char *mode)
{
  char *name;
  FILE *f;

  name = option_file_name();
  if (name==NULL) {
    return NULL;
  }
  f = fopen(name, mode);

  if(mode[0]=='w') {
    char output_buffer[256];
    if (f) {
      my_snprintf(output_buffer, sizeof(output_buffer),
		  _("Settings file is %s"), name);
    } else {
      my_snprintf(output_buffer, sizeof(output_buffer),
		  _("Cannot write to file %s"), name);
    }
    append_output_window(output_buffer);
  }
  
  return f;
}

/****************************************************************
... 
*****************************************************************/
void load_options(void)
{
  struct section_file sf;
  const char * const prefix = "client";
  char *name;
  int i;
  client_option *o;
  view_option *v;

  name = option_file_name();
  if (name==NULL) {
    /* fail silently */
    return;
  }
  if (!section_file_load(&sf, name))
    return;  

  for (o=options; o->name; o++) {
    *(o->p_value) =
      secfile_lookup_int_default(&sf, *(o->p_value), "%s.%s", prefix, o->name);
  }
  for (v=view_options; v->name; v++) {
    *(v->p_value) =
      secfile_lookup_int_default(&sf, *(v->p_value), "%s.%s", prefix, v->name);
  }
  for (i=0; i<E_LAST; i++) {
    messages_where[i] =
      secfile_lookup_int_default(&sf, messages_where[i],
				 "%s.message_where_%2.2d", prefix, i);
  }
  for (i=1; i<num_city_report_spec(); i++) {
    int *ip = city_report_spec_show_ptr(i);
    *ip = secfile_lookup_int_default(&sf, *ip, "%s.city_report_%s", prefix,
				     city_report_spec_tagname(i));
  }
  /* avoid warning for unused: */
  section_file_lookup(&sf, "client.flags_are_transparent");
  
  section_file_check_unused(&sf, name);
  section_file_free(&sf);
}

/****************************************************************
... 
*****************************************************************/
void save_options(void)
{
  FILE *option_file;
  client_option *o;
  view_option *v;
  int i;

  option_file = open_option_file("w");
  if (option_file==NULL) {
    append_output_window(_("Cannot save settings."));
    return;
  }

  fprintf(option_file, "# settings file for freeciv client version %s\n#\n",
	 VERSION_STRING);
  
  fprintf(option_file, "[client]\n");

  for (o=options; o->name; o++) {
    fprintf(option_file, "%s = %d\n", o->name, *(o->p_value));
  }
  for (v=view_options; v->name; v++) {
    fprintf(option_file, "%s = %d\n", v->name, *(v->p_value));
  }
  for (i=0; i<E_LAST; i++) {
    fprintf(option_file, "message_where_%2.2d = %d  # %s\n",
	   i, messages_where[i], message_text[i]);
  }
  for (i=1; i<num_city_report_spec(); i++) {
    fprintf(option_file, "city_report_%s = %d\n",
	   city_report_spec_tagname(i), *(city_report_spec_show_ptr(i)));
  }

  fclose(option_file);
  
  append_output_window(_("Saved settings."));
}
