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
#include <ctype.h>
#include <stdarg.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Toggle.h>     

#include <game.h>
#include <player.h>
#include <mapview.h>
#include <optiondlg.h>
#include <shared.h>
#include <packets.h>
#include <xstuff.h>
#include <events.h>
#include <chatline.h>
#include <messagedlg.h>
#include <repodlgs.h>
#include <log.h>

extern Widget toplevel, main_form;

extern struct connection aconnection;
extern Display	*display;

extern int use_solid_color_behind_units;
extern int sound_bell_at_new_turn;
extern int smooth_move_units;
extern int flags_are_transparent;
extern int ai_popup_windows;
extern int ai_manual_turn_done;
extern int auto_center_on_unit;
extern int wakeup_focus;
extern int draw_diagonal_roads;

/******************************************************************/

/* definitions for options save/restore */

typedef struct {
  int *var;
  char *name;
} opt_def;

#define GEN_OPT(var) { &var, #var }

static opt_def opts[]= {
  GEN_OPT(use_solid_color_behind_units),
  GEN_OPT(sound_bell_at_new_turn),
  GEN_OPT(smooth_move_units),
  GEN_OPT(flags_are_transparent),
  GEN_OPT(ai_popup_windows),
  GEN_OPT(ai_manual_turn_done),
  GEN_OPT(auto_center_on_unit),
  GEN_OPT(wakeup_focus),
  GEN_OPT(draw_diagonal_roads),
  { NULL, NULL }
};

/******************************************************************/
Widget option_dialog_shell;
Widget option_bg_toggle;
Widget option_bell_toggle;
Widget option_move_toggle;
Widget option_flag_toggle;
Widget option_aipopup_toggle;
Widget option_aiturndone_toggle;
Widget option_autocenter_toggle;
Widget option_wakeup_focus_toggle;
Widget option_diagonal_roads_toggle;

/******************************************************************/
void create_option_dialog(void);

void option_ok_command_callback(Widget w, XtPointer client_data, 
			        XtPointer call_data);

/****************************************************************
... 
*****************************************************************/
void popup_option_dialog(void)
{
  create_option_dialog();
  XtVaSetValues(option_bg_toggle, XtNstate, use_solid_color_behind_units, 
                XtNlabel, use_solid_color_behind_units?"Yes":"No", NULL);
  XtVaSetValues(option_bell_toggle, XtNstate, sound_bell_at_new_turn,
                XtNlabel, sound_bell_at_new_turn?"Yes":"No", NULL);
  XtVaSetValues(option_move_toggle, XtNstate, smooth_move_units,
                XtNlabel, smooth_move_units?"Yes":"No", NULL);
  XtVaSetValues(option_flag_toggle, XtNstate, flags_are_transparent,
                XtNlabel, flags_are_transparent?"Yes":"No", NULL);
  XtVaSetValues(option_aipopup_toggle, XtNstate, ai_popup_windows,
                XtNlabel, ai_popup_windows?"Yes":"No", NULL);
  XtVaSetValues(option_aiturndone_toggle, XtNstate, ai_manual_turn_done,
                XtNlabel, ai_manual_turn_done?"Yes":"No", NULL);
  XtVaSetValues(option_autocenter_toggle, XtNstate, auto_center_on_unit,
                XtNlabel, auto_center_on_unit?"Yes":"No", NULL);
  XtVaSetValues(option_wakeup_focus_toggle, XtNstate, wakeup_focus,
                XtNlabel, wakeup_focus?"Yes":"No", NULL);
  XtVaSetValues(option_diagonal_roads_toggle, XtNstate, draw_diagonal_roads,
                XtNlabel, draw_diagonal_roads?"Yes":"No", NULL);

  xaw_set_relative_position(toplevel, option_dialog_shell, 25, 25);
  XtPopup(option_dialog_shell, XtGrabNone);
  XtSetSensitive(main_form, FALSE);
}




/****************************************************************
...
*****************************************************************/
void create_option_dialog(void)
{
  Widget option_form, option_label;
  Widget option_ok_command;
  
  option_dialog_shell = XtCreatePopupShell("optionpopup", 
					  transientShellWidgetClass,
					  toplevel, NULL, 0);

  option_form = XtVaCreateManagedWidget("optionform", 
				        formWidgetClass, 
				        option_dialog_shell, NULL);   

  option_label = XtVaCreateManagedWidget("optionlabel", 
					 labelWidgetClass, 
					 option_form, NULL);   
  
  XtVaCreateManagedWidget("optionbglabel", 
			  labelWidgetClass, 
			  option_form, NULL);
  option_bg_toggle = XtVaCreateManagedWidget("optionbgtoggle", 
					     toggleWidgetClass, 
					     option_form,
					     NULL);
  
  XtVaCreateManagedWidget("optionbelllabel", 
			  labelWidgetClass, 
			  option_form, NULL);
  option_bell_toggle = XtVaCreateManagedWidget("optionbelltoggle", 
					       toggleWidgetClass, 
					       option_form,
					       NULL);
  
  
  XtVaCreateManagedWidget("optionmovelabel", 
			  labelWidgetClass, 
			  option_form, NULL);
  option_move_toggle = XtVaCreateManagedWidget("optionmovetoggle", 
						toggleWidgetClass, 
						option_form,
						NULL);
  XtVaCreateManagedWidget("optionflaglabel",
                          labelWidgetClass,
			  option_form, NULL);
  option_flag_toggle = XtVaCreateManagedWidget("optionflagtoggle",
					       toggleWidgetClass,
					       option_form,
					       NULL);
  XtVaCreateManagedWidget("optionaipopuplabel",
                          labelWidgetClass,
			  option_form, NULL);
  option_aipopup_toggle = XtVaCreateManagedWidget("optionaipopuptoggle",
					          toggleWidgetClass,
					          option_form,
					          NULL);
  XtVaCreateManagedWidget("optionaiturndonelabel",
                          labelWidgetClass,
			  option_form, NULL);
  option_aiturndone_toggle = XtVaCreateManagedWidget("optionaiturndonetoggle",
					             toggleWidgetClass,
					             option_form,
					             NULL);
  XtVaCreateManagedWidget("optionautocenterlabel",
  			  labelWidgetClass,
			  option_form, NULL);
  option_autocenter_toggle = XtVaCreateManagedWidget("optionautocentertoggle",
						     toggleWidgetClass,
						     option_form,
						     NULL);
  XtVaCreateManagedWidget("optionwakeupfocuslabel",
  			  labelWidgetClass,
			  option_form, NULL);
  option_wakeup_focus_toggle=XtVaCreateManagedWidget("optionwakeupfocustoggle",
						     toggleWidgetClass,
						     option_form,
						     NULL);
  XtVaCreateManagedWidget("optiondiagonalroadslabel",
                          labelWidgetClass,
			  option_form, NULL);
  option_diagonal_roads_toggle = XtVaCreateManagedWidget("optiondiagonalroadstoggle",
                                                         toggleWidgetClass,
							 option_form,
							 NULL);

  option_ok_command = XtVaCreateManagedWidget("optionokcommand", 
					      commandWidgetClass,
					      option_form,
					      NULL);
  
  XtAddCallback(option_ok_command, XtNcallback, 
		option_ok_command_callback, NULL);

  XtAddCallback(option_bg_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(option_bell_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(option_move_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(option_flag_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(option_aipopup_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(option_aiturndone_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(option_autocenter_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(option_wakeup_focus_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(option_diagonal_roads_toggle, XtNcallback, toggle_callback, NULL);
  

  XtRealizeWidget(option_dialog_shell);

  xaw_horiz_center(option_label);
}


/**************************************************************************
 Changes the label of the toggle widget to Yes/No depending on the state of
 the toggle.
**************************************************************************/
void toggle_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  Boolean b;

  XtVaGetValues(w, XtNstate, &b, NULL);
  XtVaSetValues(w, XtNlabel, b?"Yes":"No", NULL);
}

/**************************************************************************
...
**************************************************************************/
void option_ok_command_callback(Widget w, XtPointer client_data, 
			       XtPointer call_data)
{
  Boolean b;
  
  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(option_dialog_shell);

  XtVaGetValues(option_bg_toggle, XtNstate, &b, NULL);
  use_solid_color_behind_units=b;
  XtVaGetValues(option_bell_toggle, XtNstate, &b, NULL);
  sound_bell_at_new_turn=b;
  XtVaGetValues(option_move_toggle, XtNstate, &b, NULL);
  smooth_move_units=b;
  XtVaGetValues(option_flag_toggle, XtNstate, &b, NULL);
  flags_are_transparent=b;
  XtVaGetValues(option_aipopup_toggle, XtNstate, &b, NULL);
  ai_popup_windows=b;
  XtVaGetValues(option_aiturndone_toggle, XtNstate, &b, NULL);
  ai_manual_turn_done=b;
  XtVaGetValues(option_autocenter_toggle, XtNstate, &b, NULL);
  auto_center_on_unit=b;
  XtVaGetValues(option_wakeup_focus_toggle, XtNstate, &b, NULL);
  wakeup_focus=b;
  XtVaGetValues(option_diagonal_roads_toggle, XtNstate, &b, NULL);
  draw_diagonal_roads=b;
}

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
  opt_def *o;
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

    for (o=opts; o->name; o++) {
      if (strstr(buffer, o->name)) {
	*(o->var) = val;
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
  opt_def* o;
  int i;

  option_file = open_option_file("w");
  if (option_file==NULL) {
    append_output_window("Cannot save settings.");
    return;
  }

  fprintf(option_file, "# settings file for freeciv client version %s\n#\n",
	  VERSION_STRING);
  
  fprintf(option_file, "[client]\n");

  for (o=opts; o->name; o++) {
    fprintf(option_file, "%s = %d\n", o->name, *(o->var));
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
