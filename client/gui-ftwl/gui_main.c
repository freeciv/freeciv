/********************************************************************** 
 Freeciv - Copyright (C) 2004 - The Freeciv Project
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
#include <assert.h>

#include "fcintl.h"
#include "fciconv.h"
#include "log.h"
#include "registry.h"

#include "chatline.h"
#include "back_end.h"
#include "widget.h"
#include "graphics_g.h"
#include "civclient.h"
#include "shared.h"
#include "support.h"
#include "tilespec.h"
#include "theme_engine.h"
#include "chat.h"
#include "mapview.h"
#include "control.h"
#include "clinet.h"

#include "gui_main.h"

client_option gui_options[] = {
};
const int num_gui_options = ARRAY_SIZE(gui_options);

struct sw_widget *root_window;

const char *client_string = "gui-fs";

#define DEFAULT_THEME		"morgan"
#define DEFAULT_RESOLUTION	"640x480"

static be_color all_colors[COLOR_EXT_LAST];

/**************************************************************************
  Do any necessary pre-initialization of the UI, if necessary.
**************************************************************************/
void ui_init(void)
{
  /* PORTME */
}

/**************************************************************************
  ...
**************************************************************************/
static void timer_callback(void *data)
{
  real_timer_callback();
  //sw_add_timeout(TIMER_INTERVAL, timer_callback, NULL);
  sw_add_timeout(1000, timer_callback, NULL);
}

/**************************************************************************
  ...
**************************************************************************/
be_color enum_color_to_be_color(int color)
{
  return all_colors[color];
}

/**************************************************************************
  ...
**************************************************************************/
static void get_colors(void)
{
  int i;

  struct {
    int r, g, b;
  } all_colors_rgb[] = {
    {  0,   0,   0},  /* Black */
    {255, 255, 255},  /* White */
    {255,   0,   0},  /* Red */
    {255, 255,   0},  /* Yellow */
    {  0, 255, 200},  /* Cyan */
    {  0, 200,   0},  /* Ground (green) */
    {  0,   0, 200},  /* Ocean (blue) */
    { 86,  86,  86},  /* Background (gray) */
    {128,   0,   0},  /* race0 */
    {128, 255, 255},  /* race1 */
    {255,   0,   0},  /* race2 */
    {255,   0, 128},  /* race3 */
    {  0,   0, 128},  /* race4 */
    {255,   0, 255},  /* race5 */
    {255, 128,   0},  /* race6 */
    {255, 255, 128},  /* race7 */
    {255, 128, 128},  /* race8 */
    {  0,   0, 255},  /* race9 */
    {  0, 255,   0},  /* race10 */
    {  0, 128, 128},  /* race11 */
    {  0,  64,  64},  /* race12 */
    {198, 198, 198},  /* race13 */
    {  0,   0, 255},  /* ext blue */
    {  0,   0,   0},
    {244, 237, 139},
    {190, 190, 190},   /* gray */
    {220, 220, 220},   /* gray */
    {119, 255,   0},   /* green */
    {157,  66, 165},   /* background */
    {189, 210, 238},   /* selected row */
    { 21,  52, 211},   /* blue */
    { 48, 224,  48},   /* OuterSpace fg */
    { 32,  64,  32},   /* OuterSpace bg */
  };
   
  assert(ARRAY_SIZE(all_colors_rgb) == COLOR_EXT_LAST);

  for (i = 0; i < COLOR_EXT_LAST; i++) {
    all_colors[i] =
	be_get_color(all_colors_rgb[i].r, all_colors_rgb[i].g,
		     all_colors_rgb[i].b);
  }
}

/**************************************************************************
  ...
**************************************************************************/
static bool my_key_handler(struct sw_widget *widget,
			   const struct be_key *key, void *data)
{
  if (key->type == BE_KEY_NORMAL && key->key == '\'') {
    chat_popup_input();
    return TRUE;
  }
  if (key->type == BE_KEY_RETURN) {
    key_end_turn();
    return TRUE;
  }
  if (key->type == BE_KEY_PRINT) {
    bool dump = !sw_get_dump_screen();

    sw_set_dump_screen(dump);
    if (dump) {
      chat_add("screen-dumper enabled",-1);
    } else {
      chat_add("screen-dumper disabled",-1);
    }
    return TRUE;
  }
  if (key->type == BE_KEY_NORMAL && key->key == 'q') {
    exit(0);
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
  The main loop for the UI.  This is called from main(), and when it
  exits the client will exit.
**************************************************************************/
void ui_main(int argc, char *argv[])
{
  int i = 1;
  char *theme = mystrdup(DEFAULT_THEME);
  char *resolution = mystrdup(DEFAULT_RESOLUTION);
  bool fullscreen = FALSE;
  char *option = NULL;
  struct ct_size res;
  struct ct_size size;

  init_character_encodings("ISO-8859-1", TRUE);

  if (!auto_connect) {
    die("Connection dialog not yet implemented. Start client using "
        "the -a option.");
  }

  while (i < argc) {
    if (is_option("--help", argv[i])) {
      fc_fprintf(stderr, _("  -d, --dump\t\tEnable screen dumper\n"));
      if (be_supports_fullscreen()) {
	fc_fprintf(stderr,
		   _("  -f, --fullscreen\t"
		     "Switch to full-screen at start\n"));
      }
      fc_fprintf(stderr, _("  -h, --help\t\tThis list\n"));
      fc_fprintf(stderr,
		 _("  -r, --res <res>\t"
		   "Use the given resolution [Default: %s]\n"),
		 DEFAULT_RESOLUTION);
      fc_fprintf(stderr,
		 _("  -t, --theme <name>\t"
		   "Use the given theme [Default: %s]\n"),
		 DEFAULT_THEME);
      exit(EXIT_SUCCESS);
    } else if (is_option("--dump", argv[i])) {
      freelog(LOG_NORMAL, "enabling screen dumper");
      sw_set_dump_screen(TRUE);
    } else if (is_option("--fullscreen", argv[i])) {
      fullscreen = TRUE;
    } else if ((option = get_option("--res", argv, &i, argc))) {
      free(resolution);
      resolution = mystrdup(option);
    } else if ((option = get_option("--theme", argv, &i, argc))) {
      free(theme);
      theme = mystrdup(option);
    } else {
      freelog(LOG_ERROR, "unknown option '%s'", argv[i]);
    }
    i++;
  }

  if (sscanf(resolution, "%dx%d", &res.width, &res.height) != 2) {
    freelog(LOG_ERROR, "The resolution '%s' doesn't parse", resolution);
  }
  free(resolution);
  
  sw_init();
  be_init(&res, fullscreen);
  be_screen_get_size(&size);
  if (size.width != res.width || size.height != res.height) {
    die("Instead of the desired screen resolution (%dx%d) "
	"got (%dx%d). This may be a problem with the window-manager.",
	res.width, res.height, size.width, size.height);
  }
  te_init(theme, "mapview.screen");
  free(theme);
  te_init_colormodel("palette.prop");

  get_colors();
  root_window = sw_create_root_window();
  sw_widget_set_background_color(root_window,
				 enum_color_to_be_color(COLOR_EXT_GREEN));

  chat_create(); 
  chatline_create();
  tilespec_load_tiles();
  timer_callback(NULL);
  sw_window_set_key_notify(root_window, my_key_handler, NULL);

  popup_mapcanvas();
  set_client_state(CLIENT_PRE_GAME_STATE);
  sw_mainloop(input_from_server);
}

/**************************************************************************
  Make a bell noise (beep).  This provides low-level sound alerts even
  if there is no real sound support.
**************************************************************************/
void sound_bell(void)
{
  /* PORTME */
}

/**************************************************************************
  Wait for data on the given socket.  Call input_from_server() when data
  is ready to be read.
**************************************************************************/
void add_net_input(int sock)
{
  be_add_net_input(sock);
}

/**************************************************************************
  Stop waiting for any server network data.  See add_net_input().
**************************************************************************/
void remove_net_input(void)
{
  be_remove_net_input();
}

/**************************************************************************
  Set one of the unit icons in the information area based on punit.
  NULL will be pased to clear the icon. idx==-1 will be passed to
  indicate this is the active unit, or idx in [0..num_units_below-1] for
  secondary (inactive) units on the same tile.
**************************************************************************/
void set_unit_icon(int idx, struct unit *punit)
{
  /* PORTME */
}

/**************************************************************************
  Most clients use an arrow (e.g., sprites.right_arrow) to indicate when
  the units_below will not fit. This function is called to activate and
  deactivate the arrow.
**************************************************************************/
void set_unit_icons_more_arrow(bool onoff)
{
  /* PORTME */
}

void update_conn_list_dialog(void)
{
  /* PORTME */
}
