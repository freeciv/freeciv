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

#include <events.h>
#include <options.h>
#include <cityrep.h>
#include <log.h>
#include <chatline.h>
#include <messagedlg.h>

int use_solid_color_behind_units;
int sound_bell_at_new_turn;
int smooth_move_units=1;
int flags_are_transparent=1;
int ai_popup_windows=0;
int ai_manual_turn_done=1;
int auto_center_on_unit=1;
int wakeup_focus=1;
int draw_diagonal_roads=1;
int center_when_popup_city=1;
int draw_map_grid=0;

client_option options[] = {
	GEN_OPTION(use_solid_color_behind_units, "Solid unit background color"),
	GEN_OPTION(sound_bell_at_new_turn, "Sound bell at new turn"),
	GEN_OPTION(smooth_move_units, "Smooth unit moves"),
	GEN_OPTION(flags_are_transparent, "Flags are transparent"),
	GEN_OPTION(ai_popup_windows, "Popup dialogs in AI Mode"),
	GEN_OPTION(ai_manual_turn_done, "Manual Turn Done in AI Mode"),
	GEN_OPTION(auto_center_on_unit, "Auto Center on Units"),
	GEN_OPTION(wakeup_focus, "Focus on Awakened Units"),
	GEN_OPTION(draw_diagonal_roads, "Draw Diagonal Roads/Rails"),
	GEN_OPTION(center_when_popup_city, "Center map when Popup city"),
	NULL_OPTION
};

/****************************************************************
 The "options" file handles actual "options", and also message
 options, and city report settings
*****************************************************************/

/****************************************************************
...								
*****************************************************************/
FILE *open_option_file(char *mode)
{
  char name_buffer[256];
  char output_buffer[256];
  char *name;
  FILE *f;

  name = getenv("FREECIV_OPT");

  if (!name) {
    name = getenv("HOME");
    if (!name) {
      append_output_window("Cannot find your home directory");
      return NULL;
    }
    strncpy(name_buffer, name, 230);
    name_buffer[230] = '\0';
    strcat(name_buffer, "/.civclientrc");
    name = name_buffer;
  }
  
  freelog(LOG_DEBUG, "settings file is %s", name);

  f = fopen(name, mode);

  if(mode[0]=='w') {
    if (f) {
      sprintf(output_buffer, "Settings file is ");
      strncat(output_buffer, name, 255-strlen(output_buffer));
    } else {
      sprintf(output_buffer, "Cannot write to file ");
      strncat(output_buffer, name, 255-strlen(output_buffer));
    }
    output_buffer[255] = '\0';
    append_output_window(output_buffer);
  }
  
  return f;
}

/****************************************************************
... 
*****************************************************************/
void load_options(void)
{
  char buffer[256];
  char orig_buffer[256];
  char *s;
  FILE *option_file;
  client_option *o;
  int val, ind;

  option_file = open_option_file("r");
  if (option_file==NULL) {
    /* fail silently */
    return;
  }

  while (fgets(buffer,255,option_file)) {
    buffer[255] = '\0';
    strcpy(orig_buffer, buffer);    /* save original for error messages */
    
    /* handle comments */
    if ((s = strstr(buffer, "#"))) {
      *s = '\0';
    }

    /* skip blank lines */
    for (s=buffer; *s && isspace(*s); s++) ;
    if(!*s) continue;

    /* ignore [client] header */
    if (*s == '[') continue;

    /* parse value */
    s = strstr(buffer, "=");
    if (s == NULL || sscanf(s+1, "%d", &val) != 1) {
      append_output_window("Parse error while loading option file: input is:");
      append_output_window(orig_buffer);
      continue;
    }

    /* parse variable names */
    if ((s = strstr(buffer, "message_where_"))) {
      if (sscanf(s+14, "%d", &ind) == 1) {
       messages_where[ind] = val;
       goto next_line;
      }
    }

    for (o=options; o->name; o++) {
      if (strstr(buffer, o->name)) {
       *(o->p_value) = val;
       goto next_line;
      }
    }
    
    if ((s = strstr(buffer, "city_report_"))) {
      s += 12;
      for (ind=1; ind<num_city_report_spec(); ind++) {
       if (strstr(s, city_report_spec_tagname(ind))) {
	 *(city_report_spec_show_ptr(ind)) = val;
	 goto next_line;
       }
      }
    }
    
    append_output_window("Unknown variable found in option file: input is:");
    append_output_window(orig_buffer);

  next_line:
    {} /* placate Solaris cc/xmkmf/makedepend */
  }

  fclose(option_file);
}

/****************************************************************
... 
*****************************************************************/
void save_options(void)
{
  FILE *option_file;
  client_option *o;
  int i;

  option_file = open_option_file("w");
  if (option_file==NULL) {
    append_output_window("Cannot save settings.");
    return;
  }

  fprintf(option_file, "# settings file for freeciv client version %s\n#\n",
	 VERSION_STRING);
  
  fprintf(option_file, "[client]\n");

  for (o=options; o->name; o++) {
    fprintf(option_file, "%s = %d\n", o->name, *(o->p_value));
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
  
  append_output_window("Saved settings.");
}
