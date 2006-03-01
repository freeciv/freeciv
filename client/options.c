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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "registry.h"
#include "shared.h"
#include "support.h"
#include "version.h"

#include "audio.h"
#include "chatline_g.h"
#include "cityrepdata.h"
#include "civclient.h"
#include "clinet.h"
#include "cma_fec.h"
#include "plrdlg_common.h"
#include "tilespec.h"

#include "options.h"
 
/** Defaults for options normally on command line **/

char default_user_name[512] = "\0";
char default_server_host[512] = "localhost";
int  default_server_port = DEFAULT_SOCK_PORT;
char default_metaserver[512] = METALIST_ADDR;
char default_tileset_name[512] = "\0";
char default_sound_set_name[512] = "stdsounds";
char default_sound_plugin_name[512] = "\0";

/** Local Options: **/

bool solid_color_behind_units = FALSE;
bool sound_bell_at_new_turn = FALSE;
int  smooth_move_unit_msec = 30;
int smooth_center_slide_msec = 200;
bool do_combat_animation = TRUE;
bool ai_popup_windows = FALSE;
bool ai_manual_turn_done = TRUE;
bool auto_center_on_unit = TRUE;
bool auto_center_on_combat = FALSE;
bool wakeup_focus = TRUE;
bool goto_into_unknown = TRUE;
bool center_when_popup_city = TRUE;
bool concise_city_production = FALSE;
bool auto_turn_done = FALSE;
bool meta_accelerators = TRUE;
bool map_scrollbars = TRUE;
bool dialogs_on_top = TRUE;
bool ask_city_name = TRUE;
bool popup_new_cities = TRUE;
bool keyboardless_goto = TRUE;
bool show_task_icons = TRUE;

/* This option is currently set by the client - not by the user. */
bool update_city_text_in_refresh_tile = TRUE;

static client_option common_options[] = {
  GEN_STR_OPTION(default_user_name,        N_("Default player's login name"),
		 NULL, NULL), 
  GEN_STR_OPTION(default_server_host,       N_("Default server"),
		 NULL, NULL),
  GEN_INT_OPTION(default_server_port,       N_("Default server's port")),
  GEN_STR_OPTION(default_metaserver,        N_("Default metaserver"),
		 NULL, NULL),
  GEN_STR_OPTION(default_sound_set_name,    N_("Default name of sound set"),
		 get_soundset_list, NULL),
  GEN_STR_OPTION(default_sound_plugin_name, N_("Default sound plugin"),
		 get_soundplugin_list, NULL),
  GEN_STR_OPTION(default_tileset_name,     N_("Tileset"),
		 get_tileset_list, tilespec_reread_callback),

  GEN_BOOL_OPTION(solid_color_behind_units, N_("Solid unit background color")),
  GEN_BOOL_OPTION(sound_bell_at_new_turn,   N_("Sound bell at new turn")),
  GEN_INT_OPTION(smooth_move_unit_msec,
		 N_("Unit movement animation time (milliseconds)")),
  GEN_INT_OPTION(smooth_center_slide_msec,
		 N_("Mapview recentering time (milliseconds)")),
  GEN_BOOL_OPTION(do_combat_animation,      N_("Show combat animation")),
  GEN_BOOL_OPTION(ai_popup_windows,         N_("Popup dialogs in AI Mode")),
  GEN_BOOL_OPTION(ai_manual_turn_done,      N_("Manual Turn Done in AI Mode")),
  GEN_BOOL_OPTION(auto_center_on_unit,      N_("Auto Center on Units")),
  GEN_BOOL_OPTION(auto_center_on_combat,    N_("Auto Center on Combat")),
  GEN_BOOL_OPTION(wakeup_focus,             N_("Focus on Awakened Units")),
  GEN_BOOL_OPTION(goto_into_unknown, N_("Allow goto into the unknown")),
  GEN_BOOL_OPTION(center_when_popup_city,   N_("Center map when Popup city")),
  GEN_BOOL_OPTION(concise_city_production,  N_("Concise City Production")),
  GEN_BOOL_OPTION(auto_turn_done,           N_("End Turn when done moving")),
  GEN_BOOL_OPTION(ask_city_name,            N_("Prompt for city names")),
  GEN_BOOL_OPTION(popup_new_cities,         N_("Pop up city dialog for new cities")),
};
#undef GEN_INT_OPTION
#undef GEN_BOOL_OPTION
#undef GEN_STR_OPTION

int num_options;
client_option *options;

/** View Options: **/

bool draw_map_grid = FALSE;
bool draw_city_names = TRUE;
bool draw_city_growth = TRUE;
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
bool draw_borders = TRUE;

#define VIEW_OPTION(name) { #name, &name }
#define VIEW_OPTION_TERMINATOR { NULL, NULL }

view_option view_options[] = {
  VIEW_OPTION(draw_map_grid),
  VIEW_OPTION(draw_city_names),
  VIEW_OPTION(draw_city_growth),
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
  VIEW_OPTION(draw_borders),
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
 * Holds information about all event types. The entries don't have
 * to be sorted.
 */
static struct {
  const char *enum_name;
  char *tag_name;
  const char *descr_orig;
  const char *descr;
  enum event_type event;
} events[] = {
  GEN_EV(N_("City: Building Unavailable Item"),       E_CITY_CANTBUILD),
  GEN_EV(N_("City: Captured/Destroyed"),              E_CITY_LOST),
  GEN_EV(N_("City: Celebrating"),                     E_CITY_LOVE),
  GEN_EV(N_("City: Civil Disorder"),                  E_CITY_DISORDER),
  GEN_EV(N_("City: Famine"),                          E_CITY_FAMINE),
  GEN_EV(N_("City: Famine Feared"),       	      E_CITY_FAMINE_FEARED),
  GEN_EV(N_("City: Growth"),                          E_CITY_GROWTH),
  GEN_EV(N_("City: May Soon Grow"),                   E_CITY_MAY_SOON_GROW),
  GEN_EV(N_("City: Needs Aqueduct"),                  E_CITY_AQUEDUCT),
  GEN_EV(N_("City: Needs Aqueduct Being Built"),      E_CITY_AQ_BUILDING),
  GEN_EV(N_("City: Normal"),                          E_CITY_NORMAL),
  GEN_EV(N_("City: Nuked"),                           E_CITY_NUKED),
  GEN_EV(N_("City: Released from CMA"),               E_CITY_CMA_RELEASE),
  GEN_EV(N_("City: Suggest Growth Throttling"),       E_CITY_GRAN_THROTTLE),
  GEN_EV(N_("City: Transfer"),                        E_CITY_TRANSFER),
  GEN_EV(N_("City: Was Built"),                       E_CITY_BUILD),
  GEN_EV(N_("City: Worklist Events"),                 E_WORKLIST),
  GEN_EV(N_("Civ: Barbarian Uprising"),               E_UPRISING ),
  GEN_EV(N_("Civ: Civil War"),                        E_CIVIL_WAR),
  GEN_EV(N_("Civ: Collapse to Anarchy"),              E_ANARCHY),
  GEN_EV(N_("Civ: First Contact"),                    E_FIRST_CONTACT),
  GEN_EV(N_("Civ: Learned New Government"),	      E_NEW_GOVERNMENT),
  GEN_EV(N_("Civ: Low Funds"),                        E_LOW_ON_FUNDS),
  GEN_EV(N_("Civ: Pollution"),                        E_POLLUTION),
  GEN_EV(N_("Civ: Revolt Ended"),                     E_REVOLT_DONE),
  GEN_EV(N_("Civ: Revolt Started"),                   E_REVOLT_START),
  GEN_EV(N_("Civ: Spaceship Events"),                 E_SPACESHIP),
  GEN_EV(N_("Diplomat Action: Bribe"),              E_MY_DIPLOMAT_BRIBE),
  GEN_EV(N_("Diplomat Action: Caused Incident"),    E_DIPLOMATIC_INCIDENT),
  GEN_EV(N_("Diplomat Action: Escape"),             E_MY_DIPLOMAT_ESCAPE),
  GEN_EV(N_("Diplomat Action: Embassy"),            E_MY_DIPLOMAT_EMBASSY),
  GEN_EV(N_("Diplomat Action: Failed"),             E_MY_DIPLOMAT_FAILED),
  GEN_EV(N_("Diplomat Action: Incite"),             E_MY_DIPLOMAT_INCITE),
  GEN_EV(N_("Diplomat Action: Poison"),             E_MY_DIPLOMAT_POISON),
  GEN_EV(N_("Diplomat Action: Sabotage"),           E_MY_DIPLOMAT_SABOTAGE),
  GEN_EV(N_("Diplomat Action: Theft"),              E_MY_DIPLOMAT_THEFT),
  GEN_EV(N_("Enemy Diplomat: Bribe"),               E_ENEMY_DIPLOMAT_BRIBE),
  GEN_EV(N_("Enemy Diplomat: Embassy"),             E_ENEMY_DIPLOMAT_EMBASSY),
  GEN_EV(N_("Enemy Diplomat: Failed"),              E_ENEMY_DIPLOMAT_FAILED),
  GEN_EV(N_("Enemy Diplomat: Incite"),              E_ENEMY_DIPLOMAT_INCITE),
  GEN_EV(N_("Enemy Diplomat: Poison"),              E_ENEMY_DIPLOMAT_POISON),
  GEN_EV(N_("Enemy Diplomat: Sabotage"),            E_ENEMY_DIPLOMAT_SABOTAGE),
  GEN_EV(N_("Enemy Diplomat: Theft"),               E_ENEMY_DIPLOMAT_THEFT),
  GEN_EV(N_("Game: Broadcast Report"),                E_BROADCAST_REPORT),
  GEN_EV(N_("Game: Game Ended"),                      E_GAME_END),
  GEN_EV(N_("Game: Game Started"),                    E_GAME_START),
  GEN_EV(N_("Game: Message from Server Operator"),    E_MESSAGE_WALL),
  GEN_EV(N_("Game: Nation Selected"),                 E_NATION_SELECTED),
  GEN_EV(N_("Game: Player Destroyed"),                E_DESTROYED),
  GEN_EV(N_("Game: Report"),                          E_REPORT),
  GEN_EV(N_("Game: Turn Bell"),                       E_TURN_BELL),
  GEN_EV(N_("Game: Year Advance"),                    E_NEXT_YEAR),
  GEN_EV(N_("Global: Eco-Disaster"),                  E_GLOBAL_ECO),
  GEN_EV(N_("Global: Nuke Detonated"),                E_NUKE),
  GEN_EV(N_("Hut: Barbarians in a Hut Roused"),       E_HUT_BARB),
  GEN_EV(N_("Hut: City Founded from Hut"),            E_HUT_CITY),
  GEN_EV(N_("Hut: Gold Found in Hut"),                E_HUT_GOLD),
  GEN_EV(N_("Hut: Killed by Barbarians in a Hut"),    E_HUT_BARB_KILLED),
  GEN_EV(N_("Hut: Mercenaries Found in Hut"),         E_HUT_MERC),
  GEN_EV(N_("Hut: Settler Found in Hut"),             E_HUT_SETTLER),
  GEN_EV(N_("Hut: Tech Found in Hut"),                E_HUT_TECH),
  GEN_EV(N_("Hut: Unit Spared by Barbarians"),        E_HUT_BARB_CITY_NEAR),
  GEN_EV(N_("Improvement: Bought"),                   E_IMP_BUY),
  GEN_EV(N_("Improvement: Built"),                    E_IMP_BUILD),
  GEN_EV(N_("Improvement: Forced to Sell"),           E_IMP_AUCTIONED),
  GEN_EV(N_("Improvement: New Improvement Selected"), E_IMP_AUTO),
  GEN_EV(N_("Improvement: Sold"),                     E_IMP_SOLD),
  GEN_EV(N_("Tech: Learned From Great Library"),      E_TECH_GAIN),
  GEN_EV(N_("Tech: Learned New Tech"),                E_TECH_LEARNED),
  GEN_EV(N_("Treaty: Alliance"),                      E_TREATY_ALLIANCE),
  GEN_EV(N_("Treaty: Broken"),                        E_TREATY_BROKEN),
  GEN_EV(N_("Treaty: Ceasefire"),                     E_TREATY_CEASEFIRE),
  GEN_EV(N_("Treaty: Peace"),                         E_TREATY_PEACE),
  GEN_EV(N_("Treaty: Shared Vision"),                 E_TREATY_SHARED_VISION),
  GEN_EV(N_("Unit: Attack Failed"),                   E_UNIT_LOST_ATT),
  GEN_EV(N_("Unit: Attack Succeeded"),                E_UNIT_WIN_ATT),
  GEN_EV(N_("Unit: Bought"),                          E_UNIT_BUY),
  GEN_EV(N_("Unit: Built"),                           E_UNIT_BUILT),
  GEN_EV(N_("Unit: Defender Destroyed"),              E_UNIT_LOST),
  GEN_EV(N_("Unit: Defender Survived"),               E_UNIT_WIN),
  GEN_EV(N_("Unit: Became More Veteran"),             E_UNIT_BECAME_VET),
  GEN_EV(N_("Unit: Production Upgraded"),             E_UNIT_UPGRADED),
  GEN_EV(N_("Unit: Relocated"),                       E_UNIT_RELOCATED),
  GEN_EV(N_("Unit: Orders / goto events"),            E_UNIT_ORDERS),
  GEN_EV(N_("Wonder: Finished"),                      E_WONDER_BUILD),
  GEN_EV(N_("Wonder: Made Obsolete"),                 E_WONDER_OBSOLETE),
  GEN_EV(N_("Wonder: Started"),                       E_WONDER_STARTED),
  GEN_EV(N_("Wonder: Stopped"),                       E_WONDER_STOPPED),
  GEN_EV(N_("Wonder: Will Finish Next Turn"),         E_WONDER_WILL_BE_BUILT),
  GEN_EV(N_("Diplomatic Message"),                    E_DIPLOMACY),
  GEN_EV(N_("City: Production changed"),              E_CITY_PRODUCTION_CHANGED),
  GEN_EV(N_("Treaty: Embassy"),                       E_TREATY_EMBASSY),
  GEN_EV_TERMINATOR
};

/* 
 * Maps from enum event_type to indexes of events[]. Set by
 * init_messages_where. 
 */
static int event_to_index[E_LAST];

static void save_cma_preset(struct section_file *file, char *name,
			    const struct cm_parameter *const pparam,
			    int inx);
static void load_cma_preset(struct section_file *file, int inx);

static void save_global_worklist(struct section_file *file, const char *path, 
                                 int wlinx, struct worklist *pwl);

static void load_global_worklist(struct section_file *file, const char *path,
				 int wlinx, struct worklist *pwl);

/**************************************************************************
  Returns the translated description of the given event.
**************************************************************************/
const char *get_message_text(enum event_type event)
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
  int out_only[] = { E_IMP_BUY, E_IMP_SOLD, E_UNIT_BUY,
		     E_UNIT_LOST_ATT, E_UNIT_WIN_ATT, E_GAME_START,
		     E_NATION_SELECTED, E_CITY_BUILD, E_NEXT_YEAR,
		     E_CITY_PRODUCTION_CHANGED,
		     E_CITY_MAY_SOON_GROW, E_WORKLIST};
  int all[] = { E_MESSAGE_WALL };
  int i;

  for(i=0; i<E_LAST; i++) {
    messages_where[i] = MW_MESSAGES;
  }
  for (i = 0; i < ARRAY_SIZE(out_only); i++) {
    messages_where[out_only[i]] = 0;
  }
  for (i = 0; i < ARRAY_SIZE(all); i++) {
    messages_where[all[i]] = MW_MESSAGES | MW_POPUP;
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
      events[i].tag_name[j] = my_tolower(events[i].tag_name[j]);
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
 message options, city report settings, cma settings, and 
 saved global worklists
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
 this loads from the rc file any options which are not ruleset specific 
 it is called on client init.
*****************************************************************/
void load_general_options(void)
{
  struct section_file sf;
  const char * const prefix = "client";
  char *name;
  int i, num;
  view_option *v;

  assert(options == NULL);
  num_options = ARRAY_SIZE(common_options) + num_gui_options;
  options = fc_malloc(num_options * sizeof(*options));
  memcpy(options, common_options, sizeof(common_options));
  memcpy(options + ARRAY_SIZE(common_options), gui_options,
	 num_gui_options * sizeof(*options));

  name = option_file_name();
  if (!name) {
    /* fail silently */
    return;
  }
  if (!section_file_load(&sf, name)) {
    create_default_cma_presets();
    return;  
  }

  /* a "secret" option for the lazy. TODO: make this saveable */
  sz_strlcpy(password, 
             secfile_lookup_str_default(&sf, "", "%s.password", prefix));

  for (i = 0; i < num_options; i++) {
    client_option *o = options + i;

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
    case COT_STR:
      mystrlcpy(o->p_string_value,
                     secfile_lookup_str_default(&sf, o->p_string_value, "%s.%s",
                     prefix, o->name), o->string_length);
      break;
    }
  }
  for (v = view_options; v->name; v++) {
    *(v->p_value) =
	secfile_lookup_bool_default(&sf, *(v->p_value), "%s.%s", prefix,
				    v->name);
  }
  for (i = 0; i < E_LAST; i++) {
    messages_where[i] =
      secfile_lookup_int_default(&sf, messages_where[i],
				 "%s.message_where_%02d", prefix, i);
  }
  for (i = 1; i < num_city_report_spec(); i++) {
    bool *ip = city_report_spec_show_ptr(i);
    *ip = secfile_lookup_bool_default(&sf, *ip, "%s.city_report_%s", prefix,
				     city_report_spec_tagname(i));
  }
  
  for(i = 1; i < num_player_dlg_columns; i++) {
    bool *show = &(player_dlg_columns[i].show);
    *show = secfile_lookup_bool_default(&sf, *show, "%s.player_dlg_%s", prefix,
                                        player_dlg_columns[i].tagname);
  }

  /* Load cma presets. If cma.number_of_presets doesn't exist, don't load 
   * any, the order here should be reversed to keep the order the same */
  num = secfile_lookup_int_default(&sf, -1, "cma.number_of_presets");
  if (num == -1) {
    create_default_cma_presets();
  } else {
    for (i = num - 1; i >= 0; i--) {
      load_cma_preset(&sf, i);
    }
  }
 
  section_file_free(&sf);
}

/****************************************************************
 this loads from the rc file any options which need to know what the 
 current ruleset is. It's called the first time client goes into
 CLIENT_GAME_RUNNING_STATE
*****************************************************************/
void load_ruleset_specific_options(void)
{
  struct section_file sf;
  char *name;
  int i;

  name = option_file_name();
  if (!name) {
    /* fail silently */
    return;
  }
  if (!section_file_load(&sf, name))
    return;

  /* load global worklists */
  for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
    game.player_ptr->worklists[i].is_valid =
	secfile_lookup_bool_default(&sf, FALSE,
				    "worklists.worklist%d.is_valid", i);
    strcpy(game.player_ptr->worklists[i].name,
           secfile_lookup_str_default(&sf, "",
                                      "worklists.worklist%d.name", i));
    load_global_worklist(&sf, "worklists.worklist%d", i, 
                         &(game.player_ptr->worklists[i]));
  }

  section_file_free(&sf);
}

/****************************************************************
... 
*****************************************************************/
void save_options(void)
{
  struct section_file sf;
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

  for (i = 0; i < num_options; i++) {
    client_option *o = options + i;

    switch (o->type) {
    case COT_BOOL:
      secfile_insert_bool(&sf, *(o->p_bool_value), "client.%s", o->name);
      break;
    case COT_INT:
      secfile_insert_int(&sf, *(o->p_int_value), "client.%s", o->name);
      break;
    case COT_STR:
      secfile_insert_str(&sf, o->p_string_value, "client.%s", o->name);
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
    secfile_insert_bool(&sf, *(city_report_spec_show_ptr(i)),
		       "client.city_report_%s",
		       city_report_spec_tagname(i));
  }
  
  for (i = 1; i < num_player_dlg_columns; i++) {
    secfile_insert_bool(&sf, player_dlg_columns[i].show,
                        "client.player_dlg_%s",
                        player_dlg_columns[i].tagname);
  }

  /* insert global worklists */
  for(i = 0; i < MAX_NUM_WORKLISTS; i++){
    if (game.player_ptr->worklists[i].is_valid) {
      secfile_insert_bool(&sf, game.player_ptr->worklists[i].is_valid,
			  "worklists.worklist%d.is_valid", i);
      secfile_insert_str(&sf, game.player_ptr->worklists[i].name,
                         "worklists.worklist%d.name", i);
      save_global_worklist(&sf, "worklists.worklist%d", i, 
                           &(game.player_ptr->worklists[i]));
    }
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
  struct cm_parameter parameter;
  const char *name;
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
      secfile_lookup_bool_default(file, FALSE, "cma.preset%d.reqhappy", inx);
  parameter.happy_factor =
      secfile_lookup_int_default(file, 0, "cma.preset%d.happyfactor", inx);
  parameter.allow_disorder = FALSE;
  parameter.allow_specialists = TRUE;

  cmafec_preset_add(name, &parameter);
}

/****************************************************************
 Does heavy lifting for inserting a preset.
*****************************************************************/
static void save_cma_preset(struct section_file *file, char *name,
			    const struct cm_parameter *const pparam,
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
  secfile_insert_bool(file, pparam->require_happy,
		      "cma.preset%d.reqhappy", inx);
  secfile_insert_int(file, pparam->happy_factor,
		     "cma.preset%d.happyfactor", inx);
}

/****************************************************************
... 
*****************************************************************/
const char *get_sound_tag_for_event(enum event_type event)
{
  if (event == E_NOEVENT) {
    return NULL;
  }

  assert(event >= 0 && event < E_LAST);

  if (events[event_to_index[event]].event == event) {
    return events[event_to_index[event]].tag_name;
  }
  freelog(LOG_ERROR, "unknown event %d", event);
  return NULL;
}

/****************************************************************
 loads global worklist from rc file
*****************************************************************/
static void load_global_worklist(struct section_file *file, const char *path,
				 int wlinx, struct worklist *pwl)
{
  char efpath[64];
  char idpath[64];
  int i;
  bool end = FALSE;

  sz_strlcpy(efpath, path);
  sz_strlcat(efpath, ".wlef%d");
  sz_strlcpy(idpath, path);
  sz_strlcat(idpath, ".wlid%d");

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    if (end) {
      pwl->wlefs[i] = WEF_END;
      pwl->wlids[i] = 0;
      section_file_lookup(file, efpath, wlinx, i);
      section_file_lookup(file, idpath, wlinx, i);
    } else {
      pwl->wlefs[i] =
        secfile_lookup_int_default(file, WEF_END, efpath, wlinx, i);
      pwl->wlids[i] =
        secfile_lookup_int_default(file, 0, idpath,wlinx, i);

      if ((pwl->wlefs[i] <= WEF_END) || (pwl->wlefs[i] >= WEF_LAST) ||
          ((pwl->wlefs[i] == WEF_UNIT) && !unit_type_exists(pwl->wlids[i])) ||
          ((pwl->wlefs[i] == WEF_IMPR) && !improvement_exists(pwl->wlids[i]))) {
        pwl->wlefs[i] = WEF_END;
        pwl->wlids[i] = 0;
        end = TRUE;
      }
    }
  }
}

/****************************************************************
 saves global worklist to rc file
*****************************************************************/
static void save_global_worklist(struct section_file *file, const char *path,
				 int wlinx, struct worklist *pwl)
{
  char efpath[64];
  char idpath[64];
  int i;

  sz_strlcpy(efpath, path);
  sz_strlcat(efpath, ".wlef%d");
  sz_strlcpy(idpath, path);
  sz_strlcat(idpath, ".wlid%d");

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    secfile_insert_int(file, pwl->wlefs[i], efpath, wlinx, i);
    secfile_insert_int(file, pwl->wlids[i], idpath, wlinx, i);
  }
}

/****************************************************************
 If is_city_event is FALSE this event doesn't effect a city even if
 there is a city at the event location.
*****************************************************************/
bool is_city_event(enum event_type event)
{
  switch (event) {
  case E_GLOBAL_ECO:
  case E_CITY_LOST:
  case E_UNIT_LOST:
  case E_UNIT_WIN:
  case E_ENEMY_DIPLOMAT_FAILED:
  case E_ENEMY_DIPLOMAT_EMBASSY:
  case E_ENEMY_DIPLOMAT_POISON:
  case E_ENEMY_DIPLOMAT_BRIBE:
  case E_ENEMY_DIPLOMAT_INCITE:
  case E_ENEMY_DIPLOMAT_SABOTAGE:
  case E_ENEMY_DIPLOMAT_THEFT:
  case E_MY_DIPLOMAT_FAILED:
  case E_MY_DIPLOMAT_EMBASSY:
  case E_MY_DIPLOMAT_POISON:
  case E_MY_DIPLOMAT_BRIBE:
  case E_MY_DIPLOMAT_INCITE:
  case E_MY_DIPLOMAT_SABOTAGE:
  case E_MY_DIPLOMAT_THEFT:
  case E_MY_DIPLOMAT_ESCAPE:
  case E_UNIT_LOST_ATT:
  case E_UNIT_WIN_ATT:
  case E_UPRISING:
  case E_UNIT_RELOCATED:
    return FALSE;

  default:
    return TRUE;
  }
}
