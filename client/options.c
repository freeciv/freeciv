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
#include "cma_fec.h"
#include "cityrepdata.h"

#include "options.h"

/** Local Options: **/

bool solid_color_behind_units = FALSE;
bool sound_bell_at_new_turn = FALSE;
bool smooth_move_units = TRUE;
int smooth_move_unit_steps = 3;
bool do_combat_animation = TRUE;
bool ai_popup_windows = FALSE;
bool ai_manual_turn_done = TRUE;
bool auto_center_on_unit = TRUE;
bool auto_center_on_combat = FALSE;
bool wakeup_focus = TRUE;
bool draw_diagonal_roads = TRUE;
bool center_when_popup_city = TRUE;
bool concise_city_production = FALSE;
bool auto_turn_done = FALSE;
bool meta_accelerators = TRUE;

#define GEN_OPTION(name, desc, type) { #name, desc, type, &name, NULL }
#define GEN_OPTION_TERMINATOR { NULL, NULL, COT_BOOL, NULL, NULL }

client_option options[] = {
  GEN_OPTION(solid_color_behind_units,	N_("Solid unit background color"), COT_BOOL),
  GEN_OPTION(sound_bell_at_new_turn,	N_("Sound bell at new turn"), COT_BOOL),
  GEN_OPTION(smooth_move_units,		N_("Smooth unit moves"), COT_BOOL),
  GEN_OPTION(smooth_move_unit_steps,	N_("Smooth unit move steps"), COT_INT),
  GEN_OPTION(do_combat_animation,	N_("Show combat animation"), COT_BOOL),
  GEN_OPTION(ai_popup_windows,		N_("Popup dialogs in AI Mode"), COT_BOOL),
  GEN_OPTION(ai_manual_turn_done,	N_("Manual Turn Done in AI Mode"), COT_BOOL),
  GEN_OPTION(auto_center_on_unit,	N_("Auto Center on Units"), COT_BOOL),
  GEN_OPTION(auto_center_on_combat,	N_("Auto Center on Combat"), COT_BOOL),
  GEN_OPTION(wakeup_focus,		N_("Focus on Awakened Units"), COT_BOOL),
  GEN_OPTION(draw_diagonal_roads,	N_("Draw Diagonal Roads/Rails"), COT_BOOL),
  GEN_OPTION(center_when_popup_city,	N_("Center map when Popup city"), COT_BOOL),
  GEN_OPTION(concise_city_production,	N_("Concise City Production"), COT_BOOL),
  GEN_OPTION(auto_turn_done, 		N_("End Turn when done moving"), COT_BOOL),
  GEN_OPTION(meta_accelerators, 	N_("Use Alt/Meta for accelerators (GTK only)"), COT_BOOL),
  GEN_OPTION_TERMINATOR
};

/** View Options: **/

bool draw_map_grid = FALSE;
bool draw_city_names = TRUE;
bool draw_city_productions = FALSE;
bool draw_terrain = TRUE;
bool draw_coastline = FALSE;
bool draw_roads_rails = TRUE;
bool draw_irrigation = TRUE;
bool draw_mines = TRUE;
bool draw_fortress_airbase = TRUE;
bool draw_specials = TRUE;
bool draw_pollution = TRUE;
bool draw_cities = TRUE;
bool draw_units = TRUE;
bool draw_focus_unit = FALSE;
bool draw_fog_of_war = TRUE;

#define VIEW_OPTION(name) { #name, &name }
#define VIEW_OPTION_TERMINATOR { NULL, NULL }

view_option view_options[] = {
  VIEW_OPTION(draw_map_grid),
  VIEW_OPTION(draw_city_names),
  VIEW_OPTION(draw_city_productions),
  VIEW_OPTION(draw_terrain),
  VIEW_OPTION(draw_coastline),
  VIEW_OPTION(draw_roads_rails),
  VIEW_OPTION(draw_irrigation),
  VIEW_OPTION(draw_mines),
  VIEW_OPTION(draw_fortress_airbase),
  VIEW_OPTION(draw_specials),
  VIEW_OPTION(draw_pollution),
  VIEW_OPTION(draw_cities),
  VIEW_OPTION(draw_units),
  VIEW_OPTION(draw_focus_unit),
  VIEW_OPTION(draw_fog_of_war),
  VIEW_OPTION_TERMINATOR
};

/** Message Options: **/

unsigned int messages_where[E_LAST];
int sorted_events[E_LAST];

const char *message_text[E_LAST]={
  N_("Low Funds"),                   /* E_LOW_ON_FUNDS */
  N_("Pollution"),                   /* E_POLLUTION */
  N_("Global Eco-Disaster"),         /* E_GLOBAL_ECO */
  N_("Civil Disorder"),              /* E_CITY_DISORDER */
  N_("City Celebrating"),            /* E_CITY_LOVE */
  N_("City Normal"),                 /* E_CITY_NORMAL */
  N_("City Growth"),                 /* E_CITY_GROWTH */
  N_("City Needs Aqueduct"),         /* E_CITY_AQUEDUCT */
  N_("Famine in City"),              /* E_CITY_FAMINE */
  N_("City Captured/Destroyed"),     /* E_CITY_LOST */
  N_("Building Unavailable Item"),   /* E_CITY_CANTBUILD */
  N_("Wonder Started"),              /* E_WONDER_STARTED */
  N_("Wonder Finished"),             /* E_WONDER_BUILD */
  N_("Improvement Built"),           /* E_IMP_BUILD */
  N_("New Improvement Selected"),    /* E_IMP_AUTO */
  N_("Forced Improvement Sale"),     /* E_IMP_AUCTIONED */
  N_("Production Upgraded"),         /* E_UNIT_UPGRADED */
  N_("Unit Built"),                  /* E_UNIT_BUILD */
  N_("Unit Defender Destroyed"),     /* E_UNIT_LOST */
  N_("Unit Defender Survived"),      /* E_UNIT_WIN */
  N_("Collapse to Anarchy"),         /* E_ANARCHY */
  N_("Diplomat Actions - Enemy"),    /* E_DIPLOMATED */
  N_("Tech from Great Library"),     /* E_TECH_GAIN */
  N_("Player Destroyed"),            /* E_DESTROYED */
  N_("Improvement Bought"),          /* E_IMP_BUY */
  N_("Improvement Sold"),            /* E_IMP_SOLD */
  N_("Unit Bought"),                 /* E_UNIT_BUY */
  N_("Wonder Stopped"),              /* E_WONDER_STOPPED */
  N_("City Needs Aq Being Built"),   /* E_CITY_AQ_BUILDING */
  N_("Diplomat Actions - Own"),      /* E_MY_DIPLOMAT */
  N_("Unit Attack Failed"),          /* E_UNIT_LOST_ATT */
  N_("Unit Attack Succeeded"),       /* E_UNIT_WIN_ATT */
  N_("Suggest Growth Throttling"),   /* E_CITY_GRAN_THROTTLE */
  N_("Spaceship Events"),            /* E_SPACESHIP */
  N_("Barbarian Uprising"),          /* E_UPRISING  */
  N_("Worklist Events"),             /* E_WORKLIST */
  N_("Pact Cancelled"),              /* E_CANCEL_PACT */
  N_("Diplomatic Incident"),         /* E_DIPL_INCIDENT */
  N_("First Contact"),               /* E_FIRST_CONTACT */
  N_("City May Soon Grow"),          /* E_CITY_MAY_SOON_GROW */
  N_("Wonder Made Obsolete"),        /* E_WONDER_OBSOLETE */
  N_("Famine Feared in City"),       /* E_CITY_FAMINE_FEARED */
  N_("Wonder Will Be Finished Next Turn"),   /* E_CITY_WONDER_WILL_BE_BUILT */
  N_("Learned New Government"),	     /* E_NEW_GOVERNMENT */
  N_("City Nuked"),                  /* E_CITY_NUKED */
  N_("Messages from the Server Operator"), /* E_MESSAGE_WALL*/
  N_("City Released from CMA"),      /* E_CITY_CMA_RELEASE */
};

static void save_cma_preset(struct section_file *file, char *name,
			    const struct cma_parameter *const pparam,
			    int inx);
static void load_cma_preset(struct section_file *file, int inx);

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
  int all[] = { E_MESSAGE_WALL };
  int i;

  for(i=0; i<E_LAST; i++) {
    messages_where[i] = MW_OUTPUT | MW_MESSAGES;
  }
  for (i = 0; i < ARRAY_SIZE(out_only); i++) {
    messages_where[out_only[i]] = MW_OUTPUT;
  }
  for (i = 0; i < ARRAY_SIZE(all); i++) {
    messages_where[all[i]] = MW_OUTPUT | MW_MESSAGES | MW_POPUP;
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
void load_options(void)
{
  struct section_file sf;
  const char * const prefix = "client";
  char *name;
  int i, num;
  client_option *o;
  view_option *v;

  name = option_file_name();
  if (!name) {
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
				 "%s.message_where_%02d", prefix, i);
  }
  for (i = 1; i < num_city_report_spec(); i++) {
    int *ip = city_report_spec_show_ptr(i);
    *ip = secfile_lookup_int_default(&sf, *ip, "%s.city_report_%s", prefix,
				     city_report_spec_tagname(i));
  }

  /* 
   * Load cma presets. If cma.number_of_presets doesn't exist, don't
   * load any, the order here should be reversed to keep the order the
   * same */
  num = secfile_lookup_int_default(&sf, 0, "cma.number_of_presets");
  for (i = num - 1; i >= 0; i--) {
    load_cma_preset(&sf, i);
  }
 
  /* avoid warning for unused: */
  section_file_lookup(&sf, "client.flags_are_transparent");
  section_file_lookup(&sf, "client.version");
  
  section_file_check_unused(&sf, name);
  section_file_free(&sf);
}

/****************************************************************
... 
*****************************************************************/
void save_options(void)
{
  struct section_file sf;
  client_option *o;
  char *name = option_file_name();
  char output_buffer[256];
  view_option *v;
  int i;

  if(!name) {
    append_output_window(_("Save failed, cannot find a filename."));
    return;
  }

  section_file_init(&sf);
  secfile_insert_str(&sf, VERSION_STRING, "client.version");

  for (o = options; o->name; o++) {
    secfile_insert_int(&sf, *(o->p_value), "client.%s", o->name);
  }

  for (v = view_options; v->name; v++) {
    secfile_insert_int(&sf, *(v->p_value), "client.%s", v->name);
  }

  for (i = 0; i < E_LAST; i++) {
    secfile_insert_int_comment(&sf, messages_where[i], message_text[i],
			       "client.message_where_%02d", i);
  }

  for (i = 1; i < num_city_report_spec(); i++) {
    secfile_insert_int(&sf, *(city_report_spec_show_ptr(i)),
		       "client.city_report_%s",
		       city_report_spec_tagname(i));
  }

  /* insert cma presets */
  secfile_insert_int_comment(&sf, cmafec_preset_num(),
			     _("If you add a preset by "
			       "hand, also update \"number_of_presets\""),
			     "cma.number_of_presets");
  for (i = 0; i < cmafec_preset_num(); i++) {
    save_cma_preset(&sf, cmafec_preset_get_descr(i),
		    cmafec_preset_get_parameter(i), i);
  }

  /* save to disk */
  if (!section_file_save(&sf, name, 0)) {
    my_snprintf(output_buffer, sizeof(output_buffer),
		_("Save failed, cannot write to file %s"), name);
  } else {
    my_snprintf(output_buffer, sizeof(output_buffer),
		_("Saved settings to file %s"), name);
  }

  append_output_window(output_buffer);
  section_file_free(&sf);
}

/****************************************************************
 Does heavy lifting for looking up a preset.
*****************************************************************/
static void load_cma_preset(struct section_file *file, int inx)
{
  struct cma_parameter parameter;
  char *name;
  int i;

  name = secfile_lookup_str_default(file, "preset", 
				    "cma.preset%d.name", inx);
  for (i = 0; i < NUM_STATS; i++) {
    parameter.minimal_surplus[i] =
	secfile_lookup_int_default(file, 0, "cma.preset%d.minsurp%d", inx, i);
    parameter.factor[i] =
	secfile_lookup_int_default(file, 0, "cma.preset%d.factor%d", inx, i);
  }
  parameter.require_happy =
      secfile_lookup_int_default(file, 0, "cma.preset%d.reqhappy", inx);
  parameter.factor_target =
      secfile_lookup_int_default(file, 0, "cma.preset%d.factortarget", inx);
  parameter.happy_factor =
      secfile_lookup_int_default(file, 0, "cma.preset%d.happyfactor", inx);

  cmafec_preset_add(name, &parameter);
}

/****************************************************************
 Does heavy lifting for inserting a preset.
*****************************************************************/
static void save_cma_preset(struct section_file *file, char *name,
			    const struct cma_parameter *const pparam,
			    int inx)
{
  int i;

  secfile_insert_str(file, name, "cma.preset%d.name", inx);
  for (i = 0; i < NUM_STATS; i++) {
    secfile_insert_int(file, pparam->minimal_surplus[i],
		       "cma.preset%d.minsurp%d", inx, i);
    secfile_insert_int(file, pparam->factor[i],
		       "cma.preset%d.factor%d", inx, i);
  }
  secfile_insert_int(file, pparam->require_happy,
		     "cma.preset%d.reqhappy", inx);
  secfile_insert_int(file, pparam->factor_target,
		     "cma.preset%d.factortarget", inx);
  secfile_insert_int(file, pparam->happy_factor,
		     "cma.preset%d.happyfactor", inx);
}
