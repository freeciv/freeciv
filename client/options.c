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
#include <assert.h>

#include "events.h"
#include "fcintl.h"
#include "log.h"
#include "registry.h"
#include "shared.h"
#include "support.h"
#include "version.h"
#include "mem.h"

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

#define GEN_INT_OPTION(name, desc) { #name, desc, COT_INT, &name, NULL, NULL }
#define GEN_BOOL_OPTION(name, desc) { #name, desc, COT_BOOL, NULL, &name, NULL }
#define GEN_OPTION_TERMINATOR { NULL, NULL, COT_BOOL, NULL, NULL, NULL }

client_option options[] = {
  GEN_BOOL_OPTION(solid_color_behind_units, N_("Solid unit background color")),
  GEN_BOOL_OPTION(sound_bell_at_new_turn,   N_("Sound bell at new turn")),
  GEN_BOOL_OPTION(smooth_move_units,        N_("Smooth unit moves")),
  GEN_INT_OPTION(smooth_move_unit_steps,    N_("Smooth unit move steps")),
  GEN_BOOL_OPTION(do_combat_animation,      N_("Show combat animation")),
  GEN_BOOL_OPTION(ai_popup_windows,         N_("Popup dialogs in AI Mode")),
  GEN_BOOL_OPTION(ai_manual_turn_done,      N_("Manual Turn Done in AI Mode")),
  GEN_BOOL_OPTION(auto_center_on_unit,      N_("Auto Center on Units")),
  GEN_BOOL_OPTION(auto_center_on_combat,    N_("Auto Center on Combat")),
  GEN_BOOL_OPTION(wakeup_focus,             N_("Focus on Awakened Units")),
  GEN_BOOL_OPTION(draw_diagonal_roads,      N_("Draw Diagonal Roads/Rails")),
  GEN_BOOL_OPTION(center_when_popup_city,   N_("Center map when Popup city")),
  GEN_BOOL_OPTION(concise_city_production,  N_("Concise City Production")),
  GEN_BOOL_OPTION(auto_turn_done,           N_("End Turn when done moving")),
  GEN_BOOL_OPTION(meta_accelerators,        N_("Use Alt/Meta for accelerators (GTK only)")),
  GEN_OPTION_TERMINATOR
};
#undef GEN_INT_OPTION
#undef GEN_BOOL_OPTION
#undef GEN_OPTION_TERMINATOR

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

#undef VIEW_OPTION
#undef VIEW_OPTION_TERMINATOR

/** Message Options: **/

unsigned int messages_where[E_LAST];
int sorted_events[E_LAST];

#define GEN_EV(descr, event) { #event, NULL, descr, NULL, event }
#define GEN_EV_TERMINATOR { NULL, NULL, NULL, NULL, E_NOEVENT }

/*
 * Holds information about all event types. The entries doesn't have
 * to be sorted.
 */
static struct {
  char *enum_name, *tag_name, *descr_orig, *descr;
  enum event_type event;
} events[] = {
  GEN_EV(N_("Low Funds"),                         E_LOW_ON_FUNDS),
  GEN_EV(N_("Pollution"),                         E_POLLUTION),
  GEN_EV(N_("Global Eco-Disaster"),               E_GLOBAL_ECO),
  GEN_EV(N_("Civil Disorder"),                    E_CITY_DISORDER),
  GEN_EV(N_("City Celebrating"),                  E_CITY_LOVE),
  GEN_EV(N_("City Normal"),                       E_CITY_NORMAL),
  GEN_EV(N_("City Growth"),                       E_CITY_GROWTH),
  GEN_EV(N_("City Needs Aqueduct"),               E_CITY_AQUEDUCT),
  GEN_EV(N_("Famine in City"),                    E_CITY_FAMINE),
  GEN_EV(N_("City Captured/Destroyed"),           E_CITY_LOST),
  GEN_EV(N_("Building Unavailable Item"),         E_CITY_CANTBUILD),
  GEN_EV(N_("Wonder Started"),                    E_WONDER_STARTED),
  GEN_EV(N_("Wonder Finished"),                   E_WONDER_BUILD),
  GEN_EV(N_("Improvement Built"),                 E_IMP_BUILD),
  GEN_EV(N_("New Improvement Selected"),          E_IMP_AUTO),
  GEN_EV(N_("Forced Improvement Sale"),           E_IMP_AUCTIONED),
  GEN_EV(N_("Production Upgraded"),               E_UNIT_UPGRADED),
  GEN_EV(N_("Unit Built"),                        E_UNIT_BUILD),
  GEN_EV(N_("Unit Defender Destroyed"),           E_UNIT_LOST),
  GEN_EV(N_("Unit Defender Survived"),            E_UNIT_WIN),
  GEN_EV(N_("Collapse to Anarchy"),               E_ANARCHY),
  GEN_EV(N_("Diplomat Actions - Enemy"),          E_DIPLOMATED),
  GEN_EV(N_("Tech from Great Library"),           E_TECH_GAIN),
  GEN_EV(N_("Player Destroyed"),                  E_DESTROYED),
  GEN_EV(N_("Improvement Bought"),                E_IMP_BUY),
  GEN_EV(N_("Improvement Sold"),                  E_IMP_SOLD),
  GEN_EV(N_("Unit Bought"),                       E_UNIT_BUY),
  GEN_EV(N_("Wonder Stopped"),                    E_WONDER_STOPPED),
  GEN_EV(N_("City Needs Aq Being Built"),         E_CITY_AQ_BUILDING),
  GEN_EV(N_("Diplomat Actions - Own"),            E_MY_DIPLOMAT),
  GEN_EV(N_("Unit Attack Failed"),                E_UNIT_LOST_ATT),
  GEN_EV(N_("Unit Attack Succeeded"),             E_UNIT_WIN_ATT),
  GEN_EV(N_("Suggest Growth Throttling"),         E_CITY_GRAN_THROTTLE),
  GEN_EV(N_("Spaceship Events"),                  E_SPACESHIP),
  GEN_EV(N_("Barbarian Uprising"),                E_UPRISING ),
  GEN_EV(N_("Worklist Events"),                   E_WORKLIST),
  GEN_EV(N_("Pact Cancelled"),                    E_CANCEL_PACT),
  GEN_EV(N_("Diplomatic Incident"),               E_DIPL_INCIDENT),
  GEN_EV(N_("First Contact"),                     E_FIRST_CONTACT),
  GEN_EV(N_("City May Soon Grow"),                E_CITY_MAY_SOON_GROW),
  GEN_EV(N_("Wonder Made Obsolete"),              E_WONDER_OBSOLETE),
  GEN_EV(N_("Famine Feared in City"),       	  E_CITY_FAMINE_FEARED),
  GEN_EV(N_("Wonder Will Be Finished Next Turn"), E_CITY_WONDER_WILL_BE_BUILT),
  GEN_EV(N_("Learned New Government"),	          E_NEW_GOVERNMENT),
  GEN_EV(N_("City Nuked"),                        E_CITY_NUKED),
  GEN_EV(N_("Messages from the Server Operator"), E_MESSAGE_WALL),
  GEN_EV(N_("City Released from CMA"),            E_CITY_CMA_RELEASE),
  GEN_EV(N_("City Was Built"),                    E_CITY_BUILD),
  GEN_EV(N_("Revolt Started"),                    E_REVOLT_START),
  GEN_EV(N_("Revolt Ended"),                      E_REVOLT_DONE),
  GEN_EV(N_("Nuke Detonated"),                    E_NUKE),
  GEN_EV(N_("Gold Found in Hut"),                 E_HUT_GOLD),
  GEN_EV(N_("Tech Found in Hut"),                 E_HUT_TECH),
  GEN_EV(N_("Mercenaries Found in Hut"),          E_HUT_MERC),
  GEN_EV(N_("Unit Spared by Barbarians"),         E_HUT_BARB_CITY_NEAR),
  GEN_EV(N_("Barbarians in a Hut Roused"),        E_HUT_BARB),
  GEN_EV(N_("Killed by Barbarians in a Hut"),     E_HUT_BARB_KILLED),
  GEN_EV(N_("City Founded from Hut"),             E_HUT_CITY),
  GEN_EV(N_("Settler Found in Hut"),              E_HUT_SETTLER),
  GEN_EV(N_("Game Started"),                      E_GAME_START),
  GEN_EV(N_("Year Advance"),                      E_NEXT_YEAR),
  GEN_EV(N_("Report"),                            E_REPORT),
  GEN_EV(N_("Broadcast Report"),                  E_BROADCAST_REPORT),
  GEN_EV(N_("Nation Selected"),                   E_NATION_SELECTED),
  GEN_EV_TERMINATOR
};

/* 
 * Maps from enum event_type to indexes of events[]. Set by
 * init_messages_where. 
 */
static int event_to_index[E_LAST];

static void save_cma_preset(struct section_file *file, char *name,
			    const struct cma_parameter *const pparam,
			    int inx);
static void load_cma_preset(struct section_file *file, int inx);

/**************************************************************************
  Returns the translated description of the given event.
**************************************************************************/
const char *const get_message_text(enum event_type event)
{
  assert(event >= 0 && event < E_LAST);

  if (events[event_to_index[event]].event == event) {
    return events[event_to_index[event]].descr;
  }
  freelog(LOG_ERROR, "unknown event %d", event);
  return "UNKNOWN EVENT";
}

/**************************************************************************
  Comparison function for qsort; i1 and i2 are pointers to an event
  (enum event_type).
**************************************************************************/
static int compar_message_texts(const void *i1, const void *i2)
{
  int j1 = *(const int*)i1;
  int j2 = *(const int*)i2;
  
  return mystrcasecmp(get_message_text(j1), get_message_text(j2));
}

/****************************************************************
  These could be a static table initialisation, except
  its easier to do it this way.
  Now also initialise sorted_events[].
*****************************************************************/
void init_messages_where(void)
{
  int out_only[] = { E_IMP_BUY, E_IMP_SOLD, E_UNIT_BUY, E_MY_DIPLOMAT,
		     E_UNIT_LOST_ATT, E_UNIT_WIN_ATT, E_GAME_START,
		     E_NATION_SELECTED, E_CITY_BUILD, E_NEXT_YEAR};
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
  
  for (i = 0; i < ARRAY_SIZE(event_to_index); i++) {
    event_to_index[i] = 0;
  }

  for (i = 0;; i++) {
    int j;

    if (events[i].event == E_NOEVENT) {
      break;
    }
    events[i].descr = _(events[i].descr_orig);
    event_to_index[events[i].event] = i;
    events[i].tag_name = mystrdup(events[i].enum_name);
    for (j = 0; j < strlen(events[i].tag_name); j++) {
      events[i].tag_name[j] = tolower(events[i].tag_name[j]);
    }
    freelog(LOG_DEBUG,
	    "event[%d]=%d: name='%s' / '%s'\n\tdescr_orig='%s'\n\tdescr='%s'",
	    i, events[i].event, events[i].enum_name, events[i].tag_name,
	    events[i].descr_orig, events[i].descr);
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
    switch (o->type) {
    case COT_BOOL:
      *(o->p_bool_value) =
	  secfile_lookup_bool_default(&sf, *(o->p_bool_value), "%s.%s",
				      prefix, o->name);
      break;
    case COT_INT:
      *(o->p_int_value) =
	  secfile_lookup_int_default(&sf, *(o->p_int_value), "%s.%s",
				      prefix, o->name);
      break;
    }
  }
  for (v=view_options; v->name; v++) {
    *(v->p_value) =
	secfile_lookup_bool_default(&sf, *(v->p_value), "%s.%s", prefix,
				    v->name);
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
  (void) section_file_lookup(&sf, "client.flags_are_transparent");
  (void) section_file_lookup(&sf, "client.version");
  
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
    switch (o->type) {
    case COT_BOOL:
      secfile_insert_bool(&sf, *(o->p_bool_value), "client.%s", o->name);
      break;
    case COT_INT:
      secfile_insert_int(&sf, *(o->p_int_value), "client.%s", o->name);
      break;
    }
  }

  for (v = view_options; v->name; v++) {
    secfile_insert_bool(&sf, *(v->p_value), "client.%s", v->name);
  }

  for (i = 0; i < E_LAST; i++) {
    secfile_insert_int_comment(&sf, messages_where[i],
			       get_message_text(i),
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
