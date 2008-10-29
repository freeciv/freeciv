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
#include <string.h>

#include "events.h"
#include "fcintl.h"
#include "ioz.h"
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
#include "cma_fec.h"
#include "dialogs_g.h"
#include "mapview_common.h"
#include "menu_g.h"
#include "options.h"
#include "overview_common.h"
#include "plrdlg_common.h"
#include "repodlgs_common.h"
#include "servers.h"
#include "themes_common.h"
#include "tilespec.h"

/****************************************************************
 The "options" file handles actual "options", and also view options,
 message options, dialog/report settings, cma settings, server settings,
 and global worklists.
*****************************************************************/

/** Defaults for options normally on command line **/

char default_user_name[512] = "\0";
char default_server_host[512] = "localhost";
int  default_server_port = DEFAULT_SOCK_PORT;
char default_metaserver[512] = META_URL;
char default_theme_name[512] = "\0";
char default_tileset_name[512] = "\0";
char default_sound_set_name[512] = "stdsounds";
char default_sound_plugin_name[512] = "\0";

bool save_options_on_exit = TRUE;
bool fullscreen_mode = FALSE;

/** Local Options: **/

bool solid_color_behind_units = FALSE;
bool sound_bell_at_new_turn = FALSE;
int  smooth_move_unit_msec = 30;
int smooth_center_slide_msec = 200;
bool do_combat_animation = TRUE;
bool ai_manual_turn_done = TRUE;
bool auto_center_on_unit = TRUE;
bool auto_center_on_combat = FALSE;
bool auto_center_each_turn = TRUE;
bool wakeup_focus = TRUE;
bool goto_into_unknown = TRUE;
bool center_when_popup_city = TRUE;
bool concise_city_production = FALSE;
bool auto_turn_done = FALSE;
bool meta_accelerators = TRUE;
bool map_scrollbars = FALSE;
bool dialogs_on_top = TRUE;
bool ask_city_name = TRUE;
bool popup_new_cities = TRUE;
bool keyboardless_goto = TRUE;
bool show_task_icons = TRUE;

/* This option is currently set by the client - not by the user. */
bool update_city_text_in_refresh_tile = TRUE;

static void reqtree_show_icons_callback(struct client_option *option);
static void draw_full_citybar_changed_callback(struct client_option *option);

const char *client_option_class_names[COC_MAX] = {
  N_("Graphics"),
  N_("Overview"),
  N_("Sound"),
  N_("Interface"),
  N_("Network"),
  N_("Font")
};

static client_option common_options[] = {
  GEN_STR_OPTION(default_user_name,
		 N_("Login name"),
		 N_("This is the default login username that will be used "
		    "in the connection dialogs or with the -a command-line "
		    "parameter."),
		 COC_NETWORK, NULL, NULL),
  GEN_STR_OPTION(default_server_host,
		 N_("Server"),
		 N_("This is the default server hostname that will be used "
		    "in the connection dialogs or with the -a command-line "
		    "parameter."),
		 COC_NETWORK, NULL, NULL),
  GEN_INT_OPTION(default_server_port,
		 N_("Server port"),
		 N_("This is the default server port that will be used "
		    "in the connection dialogs or with the -a command-line "
		    "parameter."),
		 COC_NETWORK),
  GEN_STR_OPTION(default_metaserver,
		 N_("Metaserver"),
		 N_("The metaserver is a host that the client contacts to "
		    "find out about games on the internet.  Don't change "
		    "this from its default value unless you know what "
		    "you're doing."),
		 COC_NETWORK, NULL, NULL),
  GEN_STR_OPTION(default_sound_set_name,
		 N_("Soundset"),
		 N_("This is the soundset that will be used.  Changing "
		    "this is the same as using the -S command-line "
		    "parameter."),
		 COC_SOUND, get_soundset_list, NULL),
  GEN_STR_OPTION(default_sound_plugin_name,
		 N_("Sound plugin"),
		 N_("If you have a problem with sound, try changing the "
		    "sound plugin.  The new plugin won't take effect until "
		    "you restart Freeciv.  Changing this is the same as "
		    "using the -P command-line option."),
		 COC_SOUND, get_soundplugin_list, NULL),
  GEN_STR_OPTION(default_theme_name, N_("Theme"),
		 N_("By changing this option you change the active theme."),
		 COC_GRAPHICS,
		 get_themes_list, theme_reread_callback),
  GEN_STR_OPTION(default_tileset_name, N_("Tileset"),
		 N_("By changing this option you change the active tileset. "
		    "This is the same as using the -t command-line "
		    "parameter."),
		 COC_GRAPHICS,
		 get_tileset_list, tilespec_reread_callback),

  GEN_BOOL_OPTION_CB(solid_color_behind_units,
		     N_("Solid unit background color"),
		     N_("Setting this option will cause units on the map "
			"view to be drawn with a solid background color "
			"instead of the flag backdrop."),
		     COC_GRAPHICS, mapview_redraw_callback),
  GEN_BOOL_OPTION(sound_bell_at_new_turn, N_("Sound bell at new turn"),
		  N_("Set this option to have a \"bell\" event be generated "
		     "at the start of a new turn.  You can control the "
		     "behavior of the \"bell\" event by editing the message "
		     "options."),
		  COC_SOUND),
  GEN_INT_OPTION(smooth_move_unit_msec,
		 N_("Unit movement animation time (milliseconds)"),
		 N_("This option controls how long unit \"animation\" takes "
		    "when a unit moves on the map view.  Set it to 0 to "
		    "disable animation entirely."),
		 COC_GRAPHICS),
  GEN_INT_OPTION(smooth_center_slide_msec,
		 N_("Mapview recentering time (milliseconds)"),
		 N_("When the map view is recentered, it will slide "
		    "smoothly over the map to its new position.  This "
		    "option controls how long this slide lasts.  Set it to "
		    "0 to disable mapview sliding entirely."),
		 COC_GRAPHICS),
  GEN_BOOL_OPTION(do_combat_animation, N_("Show combat animation"),
		  N_("Disabling this option will turn off combat animation "
		     "between units on the mapview."),
		  COC_GRAPHICS),
  GEN_BOOL_OPTION_CB(draw_full_citybar, N_("Draw the citybar"),
		     N_("Setting this option will display a 'citybar' "
			"containing useful information beneath each city. "
			"Disabling this option will display only the city's "
			"name and optionally, production."),
		     COC_GRAPHICS, draw_full_citybar_changed_callback),
  GEN_BOOL_OPTION_CB(reqtree_show_icons,
                     N_("Show icons in the technology tree"),
                     N_("Setting this option will display icons "
		        "on the technology tree diagram. Turning "
		        "this option off makes the technology tree "
		        "more compact."),
		     COC_GRAPHICS, reqtree_show_icons_callback),
  GEN_BOOL_OPTION_CB(reqtree_curved_lines,
                     N_("Use curved lines in the technology tree"),
                     N_("Setting this option make the technology tree "
                        "diagram use curved lines to show technology "
                        "relations. Turning this option off causes "
                        "the lines to be drawn straight."),
		     COC_GRAPHICS, reqtree_show_icons_callback),
  GEN_BOOL_OPTION_CB(draw_unit_shields, N_("Draw shield graphics for units"),
		     N_("Setting this option will draw a shield icon "
			"as the flags on units.  If unset, the full flag will "
			"be drawn."),
		     COC_GRAPHICS, mapview_redraw_callback),
  GEN_BOOL_OPTION(ai_manual_turn_done, N_("Manual Turn Done in AI Mode"),
		  N_("Disable this option if you do not want to "
		     "press the Turn Done button manually when watching "
		     "an AI player."),
		  COC_INTERFACE),
  GEN_BOOL_OPTION(auto_center_on_unit, N_("Auto Center on Units"),
		  N_("Set this option to have the active unit centered "
		     "automatically when the unit focus changes."),
		  COC_INTERFACE),
  GEN_BOOL_OPTION(auto_center_on_combat, N_("Auto Center on Combat"),
		  N_("Set this option to have any combat be centered "
		     "automatically.  Disabled this will speed up the time "
		     "between turns but may cause you to miss combat "
		     "entirely."),
		  COC_INTERFACE),
  GEN_BOOL_OPTION(auto_center_each_turn, N_("Auto Center on New Turn"),
                  N_("Set this option to have the client automatically "
                     "recenter the map on a suitable location at the "
                     "start of each turn."),
                  COC_INTERFACE),
  GEN_BOOL_OPTION(wakeup_focus, N_("Focus on Awakened Units"),
		  N_("Set this option to have newly awoken units be "
		     "focused automatically."),
		  COC_INTERFACE),
  GEN_BOOL_OPTION(goto_into_unknown, N_("Allow goto into the unknown"),
		  N_("Setting this option will make the game consider "
		     "moving into unknown tiles.  If not, then goto routes "
		     "will detour around or be blocked by unknown tiles."),
		  COC_INTERFACE),
  GEN_BOOL_OPTION(center_when_popup_city, N_("Center map when Popup city"),
		  N_("Setting this option makes the mapview center on a "
		     "city when its city dialog is popped up."),
		  COC_INTERFACE),
  GEN_BOOL_OPTION(concise_city_production, N_("Concise City Production"),
		  N_("Set this option to make the city production (as shown "
		     "in the city dialog) to be more compact."),
		  COC_INTERFACE),
  GEN_BOOL_OPTION(auto_turn_done, N_("End Turn when done moving"),
		  N_("Setting this option makes your turn end automatically "
		     "when all your units are done moving."),
		  COC_INTERFACE),
  GEN_BOOL_OPTION(ask_city_name, N_("Prompt for city names"),
		  N_("Disabling this option will make the names of newly "
		     "founded cities chosen automatically by the server."),
		  COC_INTERFACE),
  GEN_BOOL_OPTION(popup_new_cities, N_("Pop up city dialog for new cities"),
		  N_("Setting this option will pop up a newly-founded "
		     "city's city dialog automatically."),
		  COC_INTERFACE),

  GEN_BOOL_OPTION(overview.layers[OLAYER_BACKGROUND],
		  N_("Background layer"),
		  N_("The background layer of the overview shows just "
		     "ocean and land."), COC_OVERVIEW),
  GEN_BOOL_OPTION_CB(overview.layers[OLAYER_RELIEF],
		     N_("Terrain relief map layer"),
		     N_("The relief layer shows all terrains on the map."),
		     COC_OVERVIEW, overview_redraw_callback),
  GEN_BOOL_OPTION_CB(overview.layers[OLAYER_BORDERS],
		     N_("Borders layer"),
		     N_("The borders layer of the overview shows which tiles "
			"are owned by each player."),
		     COC_OVERVIEW, overview_redraw_callback),
  GEN_BOOL_OPTION_CB(overview.layers[OLAYER_BORDERS_ON_OCEAN],
                     N_("Borders layer on ocean tiles"),
                     N_("The borders layer of the overview are drawn on "
                        "ocean tiles as well (this may look ugly with many "
                        "islands). This option is only of interest if you "
                        "have set the option \"Borders layer\" already."),
                     COC_OVERVIEW, overview_redraw_callback),
  GEN_BOOL_OPTION_CB(overview.layers[OLAYER_UNITS],
		     N_("Units layer"),
		     N_("Enabling this will draw units on the overview."),
		     COC_OVERVIEW, overview_redraw_callback),
  GEN_BOOL_OPTION_CB(overview.layers[OLAYER_CITIES],
		     N_("Cities layer"),
		     N_("Enabling this will draw cities on the overview."),
		     COC_OVERVIEW, overview_redraw_callback),
  GEN_BOOL_OPTION_CB(overview.fog,
		     N_("Overview fog of war"),
		     N_("Enabling this will show fog of war on the "
		        "overview."),
		     COC_OVERVIEW, overview_redraw_callback)
};
#undef GEN_INT_OPTION
#undef GEN_BOOL_OPTION
#undef GEN_STR_OPTION

static client_option *fc_options = NULL;
static int num_options = 0;

/** View Options: **/

bool draw_city_outlines = TRUE;
bool draw_city_output = FALSE;
bool draw_map_grid = FALSE;
bool draw_city_names = TRUE;
bool draw_city_growth = TRUE;
bool draw_city_productions = FALSE;
bool draw_city_traderoutes = FALSE;
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
bool draw_full_citybar = TRUE;
bool draw_unit_shields = TRUE;
bool player_dlg_show_dead_players = TRUE;
bool reqtree_show_icons = TRUE;
bool reqtree_curved_lines = FALSE;

#define VIEW_OPTION(name) { #name, &name }
#define VIEW_OPTION_TERMINATOR { NULL, NULL }

view_option view_options[] = {
  VIEW_OPTION(draw_city_outlines),
  VIEW_OPTION(draw_city_output),
  VIEW_OPTION(draw_map_grid),
  VIEW_OPTION(draw_city_names),
  VIEW_OPTION(draw_city_growth),
  VIEW_OPTION(draw_city_productions),
  VIEW_OPTION(draw_city_traderoutes),
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
  VIEW_OPTION(player_dlg_show_dead_players),
  VIEW_OPTION_TERMINATOR
};

#undef VIEW_OPTION
#undef VIEW_OPTION_TERMINATOR

/** Message Options: **/

unsigned int messages_where[E_LAST];


/**************************************************************************
  Return the first item of fc_options.
**************************************************************************/
struct client_option *client_option_array_first(void)
{
  if (num_options > 0) {
    return fc_options;
  }
  return NULL;
}

/**************************************************************************
  Return the last item of fc_options.
**************************************************************************/
const struct client_option *client_option_array_last(void)
{
  if (num_options > 0) {
    return &fc_options[num_options - 1];
  }
  return NULL;
}

/****************************************************************
  These could be a static table initialisation, except
  its easier to do it this way.
*****************************************************************/
void message_options_init(void)
{
  int none[] = {
    E_IMP_BUY, E_IMP_SOLD, E_UNIT_BUY,
    E_UNIT_LOST_ATT, E_UNIT_WIN_ATT, E_GAME_START,
    E_NATION_SELECTED, E_CITY_BUILD, E_NEXT_YEAR,
    E_CITY_PRODUCTION_CHANGED,
    E_CITY_MAY_SOON_GROW, E_WORKLIST, E_AI_DEBUG
  };
  int out_only[] = {
    E_CHAT_MSG, E_CHAT_ERROR, E_CONNECTION, E_LOG_ERROR, E_SETTING
  };
  int all[] = {
    E_LOG_FATAL, E_TUTORIAL
  };
  int i;

  for (i = 0; i < E_LAST; i++) {
    messages_where[i] = MW_MESSAGES;
  }
  for (i = 0; i < ARRAY_SIZE(none); i++) {
    messages_where[none[i]] = 0;
  }
  for (i = 0; i < ARRAY_SIZE(out_only); i++) {
    messages_where[out_only[i]] = MW_OUTPUT;
  }
  for (i = 0; i < ARRAY_SIZE(all); i++) {
    messages_where[all[i]] = MW_MESSAGES | MW_POPUP;
  }

  events_init();
}

/****************************************************************
... 
*****************************************************************/
void message_options_free(void)
{
  events_free();
}

/****************************************************************
... 
*****************************************************************/
static void message_options_load(struct section_file *file, const char *prefix)
{
  int i;

  for (i = 0; i < E_LAST; i++) {
    messages_where[i] =
      secfile_lookup_int_default(file, messages_where[i],
				 "%s.message_where_%02d", prefix, i);
  }
}

/****************************************************************
... 
*****************************************************************/
static void message_options_save(struct section_file *file, const char *prefix)
{
  int i;

  for (i = 0; i < E_LAST; i++) {
    secfile_insert_int_comment(file, messages_where[i],
			       get_event_message_text(i),
			       "%s.message_where_%02d", prefix, i);
  }
}


/****************************************************************
 Does heavy lifting for looking up a preset.
*****************************************************************/
static void load_cma_preset(struct section_file *file, int i)
{
  struct cm_parameter parameter;
  const char *name =
    secfile_lookup_str_default(file, "preset",
                               "cma.preset%d.name", i);

  output_type_iterate(o) {
    parameter.minimal_surplus[o] =
        secfile_lookup_int_default(file, 0, "cma.preset%d.minsurp%d", i, o);
    parameter.factor[o] =
        secfile_lookup_int_default(file, 0, "cma.preset%d.factor%d", i, o);
  } output_type_iterate_end;
  parameter.require_happy =
      secfile_lookup_bool_default(file, FALSE, "cma.preset%d.reqhappy", i);
  parameter.happy_factor =
      secfile_lookup_int_default(file, 0, "cma.preset%d.happyfactor", i);
  parameter.allow_disorder = FALSE;
  parameter.allow_specialists = TRUE;

  cmafec_preset_add(name, &parameter);
}

/****************************************************************
 Does heavy lifting for inserting a preset.
*****************************************************************/
static void save_cma_preset(struct section_file *file, int i)
{
  const struct cm_parameter *const pparam = cmafec_preset_get_parameter(i);
  char *name = cmafec_preset_get_descr(i);

  secfile_insert_str(file, name, "cma.preset%d.name", i);

  output_type_iterate(o) {
    secfile_insert_int(file, pparam->minimal_surplus[o],
                       "cma.preset%d.minsurp%d", i, o);
    secfile_insert_int(file, pparam->factor[o],
                       "cma.preset%d.factor%d", i, o);
  } output_type_iterate_end;
  secfile_insert_bool(file, pparam->require_happy,
                      "cma.preset%d.reqhappy", i);
  secfile_insert_int(file, pparam->happy_factor,
                     "cma.preset%d.happyfactor", i);
}

/****************************************************************
 Insert all cma presets.
*****************************************************************/
static void save_cma_presets(struct section_file *file)
{
  int i;

  secfile_insert_int_comment(file, cmafec_preset_num(),
                             _("If you add a preset by hand,"
                               " also update \"number_of_presets\""),
                             "cma.number_of_presets");
  for (i = 0; i < cmafec_preset_num(); i++) {
    save_cma_preset(file, i);
  }
}


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
      freelog(LOG_ERROR, _("Cannot find your home directory"));
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
 Load settable per connection/server options.
*****************************************************************/
void load_settable_options(bool send_it)
{
  char buffer[MAX_LEN_MSG];
  struct section_file sf;
  char *name;
  char *desired_string;
  int i = 0;

  name = option_file_name();
  if (!name) {
    /* fail silently */
    return;
  }
  if (!section_file_load(&sf, name))
    return;

  for (; i < num_settable_options; i++) {
    struct options_settable *o = &settable_options[i];
    bool changed = FALSE;

    my_snprintf(buffer, sizeof(buffer), "/set %s ", o->name);

    switch (o->stype) {
    case SSET_BOOL:
      o->desired_val = secfile_lookup_bool_default(&sf, o->default_val,
                                                   "server.%s", o->name);
      changed = (o->desired_val != o->default_val
              && o->desired_val != o->val);
      if (changed) {
        sz_strlcat(buffer, o->desired_val ? "1" : "0");
      }
      break;
    case SSET_INT:
      o->desired_val = secfile_lookup_int_default(&sf, o->default_val,
                                                  "server.%s", o->name);
      changed = (o->desired_val != o->default_val
              && o->desired_val != o->val);
      if (changed) {
        cat_snprintf(buffer, sizeof(buffer), "%d", o->desired_val);
      }
      break;
    case SSET_STRING:
      desired_string = secfile_lookup_str_default(&sf, o->default_strval,
                                                  "server.%s", o->name);
      if (NULL != desired_string) {
        if (NULL != o->desired_strval) {
          free(o->desired_strval);
        }
        o->desired_strval = mystrdup(desired_string);
        changed = (0 != strcmp(desired_string, o->default_strval)
                && 0 != strcmp(desired_string, o->strval));
        if (changed) {
          sz_strlcat(buffer, desired_string);
        }
      }
      break;
    default:
      freelog(LOG_ERROR,
              "load_settable_options() bad type %d.",
              o->stype);
      break;
    };

    if (changed && send_it) {
      send_chat(buffer);
    }
  }

  section_file_free(&sf);
}

/****************************************************************
 Save settable per connection/server options.
*****************************************************************/
static void save_settable_options(struct section_file *sf)
{
  int i = 0;

  for (; i < num_settable_options; i++) {
    struct options_settable *o = &settable_options[i];

    switch (o->stype) {
    case SSET_BOOL:
      if (o->desired_val != o->default_val) {
        secfile_insert_bool(sf, o->desired_val, "server.%s", o->name);
      }
      break;
    case SSET_INT:
      if (o->desired_val != o->default_val) {
        secfile_insert_int(sf, o->desired_val, "server.%s",  o->name);
      }
      break;
    case SSET_STRING:
      if (NULL != o->desired_strval
       && 0 != strcmp(o->desired_strval, o->default_strval)) {
        secfile_insert_str(sf, o->desired_strval, "server.%s", o->name);
      }
      break;
    default:
      freelog(LOG_ERROR,
              "save_settable_options() bad type %d.",
              o->stype);
      break;
    };
  }
}


/****************************************************************
 Load from the rc file any options that are not ruleset specific.
 It is called after ui_init(), yet before ui_main().
 Unfortunately, this means that some clients cannot display.
 Instead, use freelog().
*****************************************************************/
void load_general_options(void)
{
  struct section_file sf;
  int i, num;
  view_option *v;
  char *name;
  const char * const prefix = "client";

  assert(fc_options == NULL);
  num_options = ARRAY_SIZE(common_options) + num_gui_options;
  fc_options = fc_calloc(num_options, sizeof(*fc_options));
  memcpy(fc_options, common_options, sizeof(common_options));
  memcpy(fc_options + ARRAY_SIZE(common_options), gui_options,
	 num_gui_options * sizeof(*fc_options));

  name = option_file_name();
  if (!name) {
    /* FIXME: need better messages */
    freelog(LOG_ERROR, _("Save failed, cannot find a filename."));
    return;
  }
  if (!section_file_load(&sf, name)) {
    /* try to create the rc file */
    section_file_init(&sf);
    secfile_insert_str(&sf, VERSION_STRING, "client.version");

    create_default_cma_presets();
    save_cma_presets(&sf);

    /* FIXME: need better messages */
    if (!section_file_save(&sf, name, 0, FZ_PLAIN)) {
      freelog(LOG_ERROR, _("Save failed, cannot write to file %s"), name);
    } else {
      freelog(LOG_NORMAL, _("Saved settings to file %s"), name);
    }
    section_file_free(&sf);
    return;
  }

  /* a "secret" option for the lazy. TODO: make this saveable */
  sz_strlcpy(password, 
             secfile_lookup_str_default(&sf, "", "%s.password", prefix));

  save_options_on_exit =
    secfile_lookup_bool_default(&sf, save_options_on_exit,
				"%s.save_options_on_exit", prefix);
  fullscreen_mode =
    secfile_lookup_bool_default(&sf, fullscreen_mode,
				"%s.fullscreen_mode", prefix);

  client_options_iterate(o) {
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
    case COT_FONT:
      mystrlcpy(o->p_string_value,
                     secfile_lookup_str_default(&sf, o->p_string_value, "%s.%s",
                     prefix, o->name), o->string_length);
      break;
    }
  } client_options_iterate_end;

  for (v = view_options; v->name; v++) {
    *(v->p_value) =
	secfile_lookup_bool_default(&sf, *(v->p_value), "%s.%s", prefix,
				    v->name);
  }

  message_options_load(&sf, prefix);
  
  /* Players dialog */
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
 C_S_RUNNING
*****************************************************************/
void load_ruleset_specific_options(void)
{
  struct section_file sf;
  int i;
  char *name = option_file_name();

  if (!name) {
    /* fail silently */
    return;
  }
  if (!section_file_load(&sf, name))
    return;

  if (NULL != client.conn.playing) {
    /* load global worklists */
    for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
      worklist_load(&sf, &client.worklists[i],
		    "worklists.worklist%d", i);
    }
  }

  /* Load city report columns (which include some ruleset data). */
  for (i = 0; i < num_city_report_spec(); i++) {
    bool *ip = city_report_spec_show_ptr(i);

    *ip = secfile_lookup_bool_default(&sf, *ip, "client.city_report_%s",
				     city_report_spec_tagname(i));
  }

  section_file_free(&sf);
}

/****************************************************************
... 
*****************************************************************/
void save_options(void)
{
  struct section_file sf;
  char output_buffer[256];
  int i;
  view_option *v;
  char *name = option_file_name();

  if(!name) {
    append_output_window(_("Save failed, cannot find a filename."));
    return;
  }

  section_file_init(&sf);
  secfile_insert_str(&sf, VERSION_STRING, "client.version");

  secfile_insert_bool(&sf, save_options_on_exit, "client.save_options_on_exit");
  secfile_insert_bool(&sf, fullscreen_mode, "client.fullscreen_mode");

  client_options_iterate(o) {
    switch (o->type) {
    case COT_BOOL:
      secfile_insert_bool(&sf, *(o->p_bool_value), "client.%s", o->name);
      break;
    case COT_INT:
      secfile_insert_int(&sf, *(o->p_int_value), "client.%s", o->name);
      break;
    case COT_STR:
    case COT_FONT:
      secfile_insert_str(&sf, o->p_string_value, "client.%s", o->name);
      break;
    }
  } client_options_iterate_end;

  for (v = view_options; v->name; v++) {
    secfile_insert_bool(&sf, *(v->p_value), "client.%s", v->name);
  }

  message_options_save(&sf, "client");

  for (i = 0; i < num_city_report_spec(); i++) {
    secfile_insert_bool(&sf, *(city_report_spec_show_ptr(i)),
		       "client.city_report_%s",
		       city_report_spec_tagname(i));
  }
  
  /* Players dialog */
  for (i = 1; i < num_player_dlg_columns; i++) {
    secfile_insert_bool(&sf, player_dlg_columns[i].show,
                        "client.player_dlg_%s",
                        player_dlg_columns[i].tagname);
  }

  /* server settings */
  save_cma_presets(&sf);
  save_settable_options(&sf);

  /* insert global worklists */
  if (NULL != client.conn.playing) {
    for(i = 0; i < MAX_NUM_WORKLISTS; i++){
      if (client.worklists[i].is_valid) {
	worklist_save(&sf, &client.worklists[i], client.worklists[i].length,
		      "worklists.worklist%d", i);
      }
    }
  }

  /* save to disk */
  if (!section_file_save(&sf, name, 0, FZ_PLAIN)) {
    my_snprintf(output_buffer, sizeof(output_buffer),
		_("Save failed, cannot write to file %s"), name);
  } else {
    my_snprintf(output_buffer, sizeof(output_buffer),
		_("Saved settings to file %s"), name);
  }
  append_output_window(output_buffer);
  section_file_free(&sf);
}

/****************************************************************************
  Callback when a mapview graphics option is changed (redraws the canvas).
****************************************************************************/
void mapview_redraw_callback(struct client_option *option)
{
  update_map_canvas_visible();
}

/****************************************************************************
   Callback when the reqtree  show icons option is changed.
   The tree is recalculated.
****************************************************************************/
static void reqtree_show_icons_callback(struct client_option *option)
{
  /* This will close research dialog, when it's open again the techtree will
   * be recalculated */
  popdown_all_game_dialogs();
}

/****************************************************************************
  Callback for when the draw_full_citybar option is changed.
****************************************************************************/
static void draw_full_citybar_changed_callback(struct client_option *option)
{
  update_menus();
  update_map_canvas_visible();
}
