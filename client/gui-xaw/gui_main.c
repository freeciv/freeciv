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

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include <X11/Xaw/Command.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Box.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/MenuButton.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xatom.h>

#include "canvas.h"
#include "pixcomm.h"

#include "game.h"
#include "log.h"
#include "map.h"
#include "version.h"

#include "chatline.h"
#include "citydlg.h"
#include "cityrep.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "colors.h"
#include "connectdlg.h"
#include "dialogs.h"
#include "diplodlg.h"
#include "gotodlg.h"
#include "graphics.h"
#include "helpdata.h"		/* boot_help_texts() */
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "options.h"
#include "plrdlg.h"
#include "repodlgs.h"
#include "resources.h"
#include "spaceshipdlg.h"

#include "gui_main.h"

#include "freeciv.ico"

AppResources appResources;

extern String fallback_resources[];
extern char name[];
extern char server_host[];
extern int  server_port;
extern char *tile_set_dir;

extern int num_units_below;

/**************************************************************************
...
**************************************************************************/
XtResource resources[] = {
    { "GotAppDefFile", "gotAppDefFile", XtRBoolean, sizeof(Boolean),
      XtOffset(AppResources *,gotAppDefFile), XtRImmediate, (XtPointer)False},
    { "log", "Log", XtRString, sizeof(String),
      XtOffset(AppResources *,logfile), XtRImmediate, (XtPointer)False},
    { "name", "Name", XtRString, sizeof(String),
      XtOffset(AppResources *,name), XtRImmediate, (XtPointer)False},
    { "port", "Port", XtRInt, sizeof(int),
      XtOffset(AppResources *,port), XtRImmediate, (XtPointer)False},
    { "server", "Server", XtRString, sizeof(String),
      XtOffset(AppResources *,server), XtRImmediate, (XtPointer)False},
    { "logLevel", "LogLevel", XtRString, sizeof(String),
      XtOffset(AppResources *,loglevel_str), XtRImmediate, (XtPointer)False},
    { "version", "Version", XtRString, sizeof(String),
      XtOffset(AppResources *,version), XtRImmediate, (XtPointer)False},
/*
    { "showHelp", "ShowHelp", XtRBoolean, sizeof(Boolean),
      XtOffset(AppResources *,showHelp), XtRImmediate, (XtPointer)False},
    { "showVersion", "ShowVersion", XtRBoolean, sizeof(Boolean),
      XtOffset(AppResources *,showVersion), XtRImmediate, (XtPointer)False},
*/
    { "tileset", "TileSet", XtRString, sizeof(String),
      XtOffset(AppResources *,tileset), XtRImmediate, (XtPointer)False},
};

/**************************************************************************
...
**************************************************************************/
static XrmOptionDescRec cmd_options[] = {
/* { "-help",    ".showHelp",    XrmoptionNoArg,  (XPointer)"True" },*/
 { "-log",     ".log",         XrmoptionSepArg, (XPointer)"True" },
 { "-Name",    ".Name",        XrmoptionSepArg, (XPointer)"True" },
 { "-port",    ".port",        XrmoptionSepArg, (XPointer)"True" },
 { "-server",  ".server",      XrmoptionSepArg, (XPointer)"True" },
 { "-debug",   ".logLevel",    XrmoptionSepArg, (XPointer)"True" },
 { "-tiles",   ".tileset",     XrmoptionSepArg, (XPointer)"True" },
/* { "-version", ".showVersion", XrmoptionNoArg,  (XPointer)"True" },*/
/* { "--help",    ".showHelp",    XrmoptionNoArg,  (XPointer)"True" },*/
 { "--log",     ".log",         XrmoptionSepArg, (XPointer)"True" },
 { "--Name",    ".Name",        XrmoptionSepArg, (XPointer)"True" },
 { "--port",    ".port",        XrmoptionSepArg, (XPointer)"True" },
 { "--debug",   ".logLevel",    XrmoptionSepArg, (XPointer)"True" },
 { "--server",  ".server",      XrmoptionSepArg, (XPointer)"True" },
 { "--tiles",   ".tileset",     XrmoptionSepArg, (XPointer)"True" }
/* { "--version", ".showVersion", XrmoptionNoArg,  (XPointer)"True" }*/
};

static void timer_callback(caddr_t client_data, XtIntervalId *id);
static void show_info_popup(Widget w, XEvent *event, String *argv,
			    Cardinal *argc);
static void quit_freeciv(Widget w, XEvent *event, String *argv,
			 Cardinal *argc);

/**************************************************************************/

struct game_data {
  int year;
  int population;
  int researchedpoints;
  int money;
  int tax, luxurary, research;
};

/**************************************************************************/

Display	*display;
int display_depth;
int screen_number;
enum Display_color_type display_color_type;
XtAppContext app_context;
extern Colormap cmap;
extern Widget info_command;

/* this GC will be the default one all thru freeciv */
GC civ_gc; 

/* and this one is used for filling with the bg color */
GC fill_bg_gc;
GC fill_tile_gc;
Pixmap gray50,gray25;

/* overall font GC                                    */
GC font_gc;
XFontStruct *main_font_struct;


Widget toplevel, main_form, menu_form, below_menu_form, left_column_form;
Widget bottom_form;
Widget map_form;
Widget map_canvas;
Widget overview_canvas;
Widget map_vertical_scrollbar, map_horizontal_scrollbar;
Widget inputline_text, outputwindow_text;
Widget econ_label[10];
Widget turn_done_button;
Widget info_command, bulb_label, sun_label, government_label, timeout_label;
Widget unit_info_label;
Widget unit_pix_canvas;
Widget unit_below_canvas[MAX_NUM_UNITS_BELOW];
Widget main_vpane;
Pixmap unit_below_pixmap[MAX_NUM_UNITS_BELOW];
Widget more_arrow_label;
Window root_window;

/* this pixmap acts as a backing store for the map_canvas widget */
Pixmap map_canvas_store;
int map_canvas_store_twidth, map_canvas_store_theight;

/* this pixmap acts as a backing store for the overview_canvas widget */
Pixmap overview_canvas_store;
int overview_canvas_store_width, overview_canvas_store_height;

/* this pixmap is used when moving units etc */
Pixmap single_tile_pixmap;
int single_tile_pixmap_width, single_tile_pixmap_height;

extern int seconds_to_turndone;

XtInputId x_input_id;
XtIntervalId x_interval_id;
Atom wm_delete_window;

XtActionsRec Actions[] = {
  { "show-info-popup",  show_info_popup},
  { "select-mapcanvas", butt_down_mapcanvas},
  { "select-overviewcanvas", butt_down_overviewcanvas},
  { "focus-to-next-unit", focus_to_next_unit },
  { "center-on-unit", center_on_unit },
  { "inputline-return", inputline_return },
  { "input-dialog-returnkey", input_dialog_returnkey},
  { "races-dialog-returnkey", races_dialog_returnkey},
  { "diplo-dialog-returnkey", diplo_dialog_returnkey},
  { "city-dialog-returnkey",  city_dialog_returnkey},
  { "connect-dialog-returnkey",  connect_dialog_returnkey},
  { "key-unit-irrigate", xaw_key_unit_irrigate },
  { "key-unit-road", xaw_key_unit_road },
  { "key-unit-mine", xaw_key_unit_mine },
  { "key-unit-transform", xaw_key_unit_transform },
  { "key-unit-homecity", xaw_key_unit_homecity },
  { "key-unit-clean-pollution", xaw_key_unit_clean_pollution },
  { "key-unit-auto", xaw_key_unit_auto },
  { "key-unit-pillage", xaw_key_unit_pillage },
  { "key-unit-explore", xaw_key_unit_explore },
  { "key-unit-disband", xaw_key_unit_disband },
  { "key-unit-fortify", xaw_key_unit_fortify },
  { "key-unit-goto", xaw_key_unit_goto },
  { "key-unit-sentry", xaw_key_unit_sentry },
  { "key-unit-wait", xaw_key_unit_wait },
  { "key-unit-done", xaw_key_unit_done },
  { "key-unit-north", xaw_key_unit_north },
  { "key-unit-north-east", xaw_key_unit_north_east },
  { "key-unit-east", xaw_key_unit_east },
  { "key-unit-south-east", xaw_key_unit_south_east },
  { "key-unit-south", xaw_key_unit_south },
  { "key-unit-south-west", xaw_key_unit_south_west },
  { "key-unit-west", xaw_key_unit_west },
  { "key-unit-north-west", xaw_key_unit_north_west },
  { "key-unit-build-city", xaw_key_unit_build_city },
  { "key-unit-unload", xaw_key_unit_unload },
  { "key-unit-nuke", xaw_key_unit_nuke },
  { "key-unit-wakeup", xaw_key_unit_wakeup },
  { "wakeup", butt_down_wakeup },
  { "key-end-turn", xaw_key_end_turn },
  { "select-citymap", button_down_citymap},
  { "quit-freeciv", quit_freeciv},
  { "close-citydialog", close_city_dialog_action},
  { "close-sciencedialog", close_science_dialog_action },
  { "close-tradedialog", close_trade_dialog_action },
  { "close-activeunitsdialog", close_activeunits_dialog_action },
  { "key-goto-dialog", popup_goto_dialog_action },
  { "key-city-workers", key_city_workers },
  { "adjust-workers", adjust_workers },
  { "close-spaceshipdialog", close_spaceship_dialog_action },
  { "spaceship-dialog-returnkey", spaceship_dialog_returnkey },
  { "key-map-grid", xaw_key_map_grid },
  { "close-helpdialog", close_help_dialog_action },
  { "close-meswindialog", close_meswin_dialog_action },
  { "close-playersdialog", close_players_dialog_action },
  { "close-cityreport", close_city_report_action }
};

#ifdef UNUSED
/* This is used below in ui_main(), in commented out code. */
static int myerr(Display *p, XErrorEvent *e)
{
  puts("error");
  return 0;
}
#endif

/**************************************************************************
...
**************************************************************************/
void ui_main(int argc, char *argv[])
{
  int i, loglevel;
  Pixmap icon_pixmap; 
  XtTranslations TextFieldTranslations;
  Dimension w,h;
  
  /* include later - pain to see the warning at every run */
  /* XtSetLanguageProc(NULL, (XtLanguageProc)NULL, NULL); */
  
  toplevel = XtVaAppInitialize(
	       &app_context,               /* Application context */
	       "Freeciv",                  /* application class name */
	       cmd_options, XtNumber(cmd_options),
	                                   /* command line option list */
	       &argc, argv,                /* command line args */
	       &fallback_resources[1],     /* for missing app-defaults file */
	       XtNallowShellResize, True,
	       NULL);              

  XtGetApplicationResources(toplevel, &appResources, resources,
                            XtNumber(resources), NULL, 0);

  loglevel = log_parse_level_str(appResources.loglevel_str);
  if (loglevel==-1) {
    exit(1);
  }
  log_init(appResources.logfile, loglevel, NULL);

/*  XSynchronize(display, 1); 
  XSetErrorHandler(myerr);*/

  if(appResources.version==NULL)  {
    freelog(LOG_FATAL, "No version number in resources");
    freelog(LOG_FATAL, "You probably have an old (circa V1.0) Freeciv resource file somewhere");
    exit(1);
  }

  /* TODO: Use capabilities here instead of version numbers */
  if(strncmp(appResources.version, VERSION_STRING,strlen(appResources.version))) {
    freelog(LOG_FATAL, "Game version does not match Resource version");
    freelog(LOG_FATAL, "Game version: %s - Resource version: %s", 
	VERSION_STRING, appResources.version);
    freelog(LOG_FATAL, "You might have an old Freeciv resourcefile in /usr/lib/X11/app-defaults");
    exit(1);
  }
  
  if(!appResources.gotAppDefFile) {
    freelog(LOG_NORMAL, "Using fallback resources - which is OK");
  }

  if(appResources.name) {
    strcpy(name, appResources.name);
  }
  else {
    struct passwd *password;
    password=getpwuid(getuid());
    if (password)
      strcpy(name, password->pw_name);
    else {
      freelog(LOG_NORMAL, "Your getpwuid call failed.  Please report this.");
      strcpy(name, "operator 00000");
      sprintf(name+9, "%05i", (int)getuid());
    }
  }

  if(appResources.server)
    strcpy(server_host, appResources.server);
  else {
    strcpy(server_host, "localhost");
  }
    
  if(appResources.port)
    server_port=appResources.port;
  else
    server_port=DEFAULT_SOCK_PORT;

  if(appResources.tileset)
    tile_set_dir=appResources.tileset; 
  
  boot_help_texts();		/* after log_init */

  display = XtDisplay(toplevel);
  screen_number=XScreenNumberOfScreen(XtScreen(toplevel));
  display_depth=DefaultDepth(display, screen_number);
  root_window=DefaultRootWindow(display);

  display_color_type=get_visual(); 

  
  if(display_color_type!=COLOR_DISPLAY) {
    freelog(LOG_FATAL, "only color displays are supported for now...");
    /*    exit(1); */
  }
  
  icon_pixmap = XCreateBitmapFromData(display,
				      RootWindowOfScreen(XtScreen(toplevel)),
				      freeciv_bits,
				      freeciv_width, freeciv_height );
  XtVaSetValues(toplevel, XtNiconPixmap, icon_pixmap, NULL);

  init_color_system();

  

  {
    XGCValues values;

    values.graphics_exposures = False;
    civ_gc = XCreateGC(display, root_window, GCGraphicsExposures, &values);

    main_font_struct=XLoadQueryFont(display, CITY_NAMES_FONT);
    if(main_font_struct==0) {
      freelog(LOG_FATAL, "failed loading font: " CITY_NAMES_FONT);
      exit(1);
    }
    values.foreground = colors_standard[COLOR_STD_WHITE];
    values.background = colors_standard[COLOR_STD_BLACK];
    values.font = main_font_struct->fid;
    font_gc= XCreateGC(display, root_window, 
		       GCForeground|GCBackground|GCFont|GCGraphicsExposures, 
		       &values);

    values.foreground = 0;
    values.background = 0;
    fill_bg_gc= XCreateGC(display, root_window, 
			  GCForeground|GCBackground|GCGraphicsExposures,
			  &values);

    values.fill_style=FillStippled;
    fill_tile_gc= XCreateGC(display, root_window, 
    			    GCForeground|GCBackground|GCFillStyle|GCGraphicsExposures,
			    &values);
  }

  {
    unsigned char d1[]={0x03,0x0c,0x03,0x0c};
    unsigned char d2[]={0x08,0x02,0x08,0x02};
    gray50=XCreateBitmapFromData(display,root_window,d1,4,4);
    gray25=XCreateBitmapFromData(display,root_window,d2,4,4);
  }

  /* get NORMAL_TILE_WIDTH etc, small.xpm, required for setup_widgets: */
  load_tile_gfx_first();

  /* 135 below is rough value (could be more intelligent) --dwp */
  num_units_below = 135/(int)NORMAL_TILE_WIDTH;
  num_units_below = MIN(num_units_below,MAX_NUM_UNITS_BELOW);
  num_units_below = MAX(num_units_below,1);
  
  /* do setup_widgets before loading the rest of graphics to ensure that
     setup_widgets() has enough colors available:  (on 256-colour systems)
  */
  setup_widgets();

  load_tile_gfx_rest();
  load_intro_gfx();
  load_cursors();

  XtSetKeyboardFocus(bottom_form, inputline_text);
  XtSetKeyboardFocus(below_menu_form, map_canvas);
  
  TextFieldTranslations = XtParseTranslationTable  /*BLAH!*/
		("<Key>Return: inputline-return()");
  XtOverrideTranslations(inputline_text, TextFieldTranslations);

  XtAppAddActions(app_context, Actions, XtNumber(Actions));

		
  XtAddCallback(map_horizontal_scrollbar, XtNjumpProc, 
		scrollbar_jump_callback, NULL);
  XtAddCallback(map_vertical_scrollbar, XtNjumpProc, 
		scrollbar_jump_callback, NULL);
  XtAddCallback(map_horizontal_scrollbar, XtNscrollProc, 
		scrollbar_scroll_callback, NULL);
  XtAddCallback(map_vertical_scrollbar, XtNscrollProc, 
		scrollbar_scroll_callback, NULL);
  XtAddCallback(turn_done_button, XtNcallback, end_turn_callback, NULL);

  XtRealizeWidget(toplevel);


  x_interval_id=XtAppAddTimeOut(app_context, 500,
				(XtTimerCallbackProc)timer_callback, NULL);

  XtVaGetValues(map_canvas, XtNheight, &h, XtNwidth, &w, NULL);
  map_canvas_store_twidth=w/NORMAL_TILE_WIDTH;
  map_canvas_store_theight=h/NORMAL_TILE_HEIGHT;
  map_canvas_store=XCreatePixmap(display, XtWindow(map_canvas), 
				 map_canvas_store_twidth*NORMAL_TILE_WIDTH,
				 map_canvas_store_theight*NORMAL_TILE_HEIGHT,
				 display_depth);

  overview_canvas_store_width=2*80;
  overview_canvas_store_height=2*50;

  overview_canvas_store=XCreatePixmap(display, XtWindow(overview_canvas), 
				      overview_canvas_store_width,
				      overview_canvas_store_height, 
				      display_depth);

  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_WHITE]);
  XFillRectangle(display, overview_canvas_store, fill_bg_gc, 0, 0, 
		 overview_canvas_store_width, overview_canvas_store_height);

  single_tile_pixmap=XCreatePixmap(display, XtWindow(overview_canvas), 
				   NORMAL_TILE_WIDTH,
				   NORMAL_TILE_HEIGHT,
				   display_depth);


  for(i=0; i<num_units_below; i++)
    unit_below_pixmap[i]=XCreatePixmap(display, XtWindow(overview_canvas), 
				       NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, 
				       display_depth);  
  
  set_bulb_sol_government(0, 0, 0);

  wm_delete_window = XInternAtom(XtDisplay(toplevel), "WM_DELETE_WINDOW", 0);
  XSetWMProtocols(display, XtWindow(toplevel), &wm_delete_window, 1);
  XtOverrideTranslations(toplevel, XtParseTranslationTable ("<Message>WM_PROTOCOLS: quit-freeciv()"));

  load_options();

  set_client_state(CLIENT_PRE_GAME_STATE);
  
  XtAppMainLoop(app_context);
}


/**************************************************************************
...
**************************************************************************/
void setup_widgets(void)
{
  long i;

  main_form = XtVaCreateManagedWidget("mainform", formWidgetClass, 
				      toplevel, 
				      NULL);   

  menu_form = XtVaCreateManagedWidget("menuform", formWidgetClass,	
				      main_form,        
				      NULL);	        
  setup_menus(menu_form); 

/*
 main_vpane= XtVaCreateManagedWidget("mainvpane", 
				      panedWidgetClass, 
				      main_form,
				      NULL);
  
  below_menu_form = XtVaCreateManagedWidget("belowmenuform", 
					    formWidgetClass, 
					    main_vpane,
					    NULL);
*/
  below_menu_form = XtVaCreateManagedWidget("belowmenuform", 
					    formWidgetClass, 
					    main_form,
					    NULL);

  left_column_form = XtVaCreateManagedWidget("leftcolumnform", 
					     formWidgetClass, 
					     below_menu_form, 
					     NULL);

  map_form = XtVaCreateManagedWidget("mapform", 
				     formWidgetClass, 
				     below_menu_form, 
				     NULL);

  bottom_form = XtVaCreateManagedWidget("bottomform", 
					formWidgetClass, 
					/*main_vpane,*/ main_form, 
					NULL);
  
  overview_canvas = XtVaCreateManagedWidget("overviewcanvas", 
					    xfwfcanvasWidgetClass,
					    left_column_form,
					    "exposeProc", 
					    (XtArgVal)overview_canvas_expose,
					    "exposeProcData", 
					    (XtArgVal)NULL,
					    NULL);
  
  info_command = XtVaCreateManagedWidget("infocommand", 
				       commandWidgetClass, 
				       left_column_form, 
				       XtNfromVert, 
				       (XtArgVal)overview_canvas,
				       NULL);   


  
  for(i=0;i<10;i++)  {
    econ_label[i] = XtVaCreateManagedWidget("econlabels",
					    commandWidgetClass,
					    left_column_form,
					    XtNbitmap, 
					      get_citizen_pixmap(i<5?1:2),
					    i?XtNfromHoriz:NULL, 
					    i?econ_label[i-1]:NULL,
					    XtNhorizDistance, 1,
					    NULL);  
    XtAddCallback(econ_label[i], XtNcallback, taxrates_callback,
		  (XtPointer)i);  
  }

  turn_done_button = XtVaCreateManagedWidget("turndonebutton", 
					     commandWidgetClass,
					     left_column_form,
					     NULL);
  
  bulb_label = XtVaCreateManagedWidget("bulblabel", 
				       labelWidgetClass,
				       left_column_form,
				       NULL);
  
  sun_label = XtVaCreateManagedWidget("sunlabel", 
				      labelWidgetClass, 
				      left_column_form,
				      NULL);

  government_label = XtVaCreateManagedWidget("governmentlabel", 
					    labelWidgetClass, 
					    left_column_form,
					    NULL);

  timeout_label = XtVaCreateManagedWidget("timeoutlabel", 
					  labelWidgetClass, 
					  left_column_form,
					  NULL);

  
  
  unit_info_label = XtVaCreateManagedWidget("unitinfolabel", 
					    labelWidgetClass, 
					    left_column_form, 
					    NULL);

  unit_pix_canvas = XtVaCreateManagedWidget("unitpixcanvas", 
					   pixcommWidgetClass,
					   left_column_form, 
					   XtNwidth, NORMAL_TILE_WIDTH,
					   XtNheight, NORMAL_TILE_HEIGHT,
					   NULL);

  for(i=0; i<num_units_below; i++) {
    char unit_below_name[32] = "\0";
    sprintf(unit_below_name, "unitbelowcanvas%ld", i);
    unit_below_canvas[i] = XtVaCreateManagedWidget(unit_below_name,
						   pixcommWidgetClass,
						   left_column_form, 
						   XtNwidth, NORMAL_TILE_WIDTH,
						   XtNheight, NORMAL_TILE_HEIGHT,
						   NULL);
  }

  more_arrow_label =
    XtVaCreateManagedWidget("morearrowlabel", 
			    labelWidgetClass,
			    left_column_form,
			    XtNfromHoriz,
			    (XtArgVal)unit_below_canvas[num_units_below-1],
			    NULL);

  map_vertical_scrollbar = XtVaCreateManagedWidget("mapvertiscrbar", 
						   scrollbarWidgetClass, 
						   map_form,
						   NULL);

  map_canvas = XtVaCreateManagedWidget("mapcanvas", 
				       xfwfcanvasWidgetClass,
				       map_form,
				       "exposeProc", 
				       (XtArgVal)map_canvas_expose,
				       "exposeProcData", 
				       (XtArgVal)NULL,
				       NULL);

  map_horizontal_scrollbar = XtVaCreateManagedWidget("maphorizscrbar", 
						     scrollbarWidgetClass, 
						     map_form,
						     NULL);



  outputwindow_text= XtVaCreateManagedWidget("outputwindowtext", 
					     asciiTextWidgetClass, 
					     bottom_form,
					     NULL);


  inputline_text= XtVaCreateManagedWidget("inputlinetext", 
					  asciiTextWidgetClass, 
					  bottom_form,
					  NULL);

}

/**************************************************************************
...
**************************************************************************/
void quit_civ(Widget w, XtPointer client_data, XtPointer call_data)
{ 
  exit(0);
}


/**************************************************************************
...
**************************************************************************/
void remove_net_input(void)
{
  XtRemoveInput(x_input_id);
}

/**************************************************************************
...
**************************************************************************/
static void quit_freeciv(Widget w, XEvent *event, String *argv,
			 Cardinal *argc)
{
  exit(0);
}

/**************************************************************************
...
**************************************************************************/
static void show_info_popup(Widget w, XEvent *event, String *argv,
			    Cardinal *argc)
{
  XButtonEvent *ev=&event->xbutton;

  if(ev->button==Button1) {
    Widget  p;
    Position x, y;
    Dimension w, h;
    char buf[512];
    
    sprintf(buf, "%s People\nYear: %s\nGold: %d\nNet Income: %d\nTax:%d Lux:%d Sci:%d\nResearching %s: %d/%d",
	    int_to_text(civ_population(game.player_ptr)),
	    textyear(game.year),
	    game.player_ptr->economic.gold,
	    turn_gold_difference,
	    game.player_ptr->economic.tax,
	    game.player_ptr->economic.luxury,
	    game.player_ptr->economic.science,
	    
	    advances[game.player_ptr->research.researching].name,
	    game.player_ptr->research.researched,
	    research_time(game.player_ptr));
    
    p=XtCreatePopupShell("popupinfo", 
			 overrideShellWidgetClass, 
			 info_command, NULL, 0);

    XtAddCallback(p,XtNpopdownCallback,destroy_me_callback,NULL);

    XtVaCreateManagedWidget("fullinfopopuplabel", 
			      labelWidgetClass,
			      p,
			      XtNlabel, buf,
			      NULL);
    
    
    
    XtRealizeWidget(p);
    
    XtVaGetValues(p, XtNwidth, &w, XtNheight, &h,  NULL);
    XtTranslateCoords(info_command, ev->x, ev->y, &x, &y);
    XtVaSetValues(p, XtNx, x-w/2, XtNy, y-h/2, NULL);
    XtPopupSpringLoaded(p);
  }
  
}



/**************************************************************************
...
**************************************************************************/
void enable_turn_done_button(void)
{
  if(game.player_ptr->ai.control && !ai_manual_turn_done)
    user_ended_turn();
  XtSetSensitive(turn_done_button, 
                 !game.player_ptr->ai.control||ai_manual_turn_done);

  if(sound_bell_at_new_turn)
    XBell(display, 100);
}


/**************************************************************************
...
**************************************************************************/
void end_turn_callback(Widget w, XtPointer client_data, XtPointer call_data)
{ 
  XtSetSensitive(turn_done_button, FALSE);

  user_ended_turn();
}



/**************************************************************************
...
**************************************************************************/
static void timer_callback(caddr_t client_data, XtIntervalId *id)
{
  static int flip;
  
  x_interval_id=XtAppAddTimeOut(app_context, 500,
				(XtTimerCallbackProc)timer_callback, NULL);  
  
  if(get_client_state()==CLIENT_GAME_RUNNING_STATE) {
  
    if(game.player_ptr->is_connected && game.player_ptr->is_alive && 
       !game.player_ptr->turn_done) { 
      int i, is_waiting, is_moving;
      
      for(i=0, is_waiting=0, is_moving=0; i<game.nplayers; i++)
	if(game.players[i].is_alive && game.players[i].is_connected) {
	  if(game.players[i].turn_done)
	    is_waiting++;
	  else
	    is_moving++;
	}
      
      if(is_moving==1 && is_waiting) 
	update_turn_done_button(0);  /* stress the slow player! */
    }
    
    blink_active_unit();
    
    if(flip) {
      update_timeout_label();
    if(seconds_to_turndone)
	seconds_to_turndone--;
    }
    
    flip=!flip;
  }
}

