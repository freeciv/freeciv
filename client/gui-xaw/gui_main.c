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

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "support.h"
#include "version.h"

#include "actions.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_stuff.h"		/* I_SW() */
#include "helpdata.h"		/* boot_help_texts() */
#include "mapview.h"
#include "menu.h"
#include "optiondlg.h"
#include "options.h"
#include "resources.h"
#include "tilespec.h"

#include "gui_main.h"

#include "freeciv.ico"

AppResources appResources;

extern String fallback_resources[];

/* in civclient.c; FIXME hardcoded sizes */
extern char name[];
extern char server_host[];
extern char metaserver[];
extern int  server_port;

extern int num_units_below;


static void setup_widgets(void);


/**************************************************************************
...
**************************************************************************/
XtResource resources[] = {
    { "GotAppDefFile", "gotAppDefFile", XtRBoolean, sizeof(Boolean),
      XtOffset(AppResources *,gotAppDefFile), XtRImmediate, (XtPointer)False},
    { "log", "Log", XtRString, sizeof(String),
      XtOffset(AppResources *,logfile), XtRImmediate, (XtPointer)False},
    { "name", "name", XtRString, sizeof(String),
      XtOffset(AppResources *,name), XtRImmediate, (XtPointer)False},
    { "port", "Port", XtRInt, sizeof(int),
      XtOffset(AppResources *,port), XtRImmediate, (XtPointer)False},
    { "server", "Server", XtRString, sizeof(String),
      XtOffset(AppResources *,server), XtRImmediate, (XtPointer)False},
    { "metaserver", "Metaserver", XtRString, sizeof(String),
      XtOffset(AppResources *,metaserver), XtRImmediate, (XtPointer)False},
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
 { "-name",    ".name",        XrmoptionSepArg, (XPointer)"True" },
 { "-port",    ".port",        XrmoptionSepArg, (XPointer)"True" },
 { "-server",  ".server",      XrmoptionSepArg, (XPointer)"True" },
 { "-metaserver",".metaserver",XrmoptionSepArg, (XPointer)"True" },
 { "-debug",   ".logLevel",    XrmoptionSepArg, (XPointer)"True" },
 { "-tiles",   ".tileset",     XrmoptionSepArg, (XPointer)"True" },
/* { "-version", ".showVersion", XrmoptionNoArg,  (XPointer)"True" },*/
/* { "--help",    ".showHelp",    XrmoptionNoArg,  (XPointer)"True" },*/
 { "--log",     ".log",         XrmoptionSepArg, (XPointer)"True" },
 { "--name",    ".name",        XrmoptionSepArg, (XPointer)"True" },
 { "--port",    ".port",        XrmoptionSepArg, (XPointer)"True" },
 { "--debug",   ".logLevel",    XrmoptionSepArg, (XPointer)"True" },
 { "--server",  ".server",      XrmoptionSepArg, (XPointer)"True" },
 { "--metaserver",".metaserver",XrmoptionSepArg, (XPointer)"True" },
 { "--tiles",   ".tileset",     XrmoptionSepArg, (XPointer)"True" }
/* { "--version", ".showVersion", XrmoptionNoArg,  (XPointer)"True" }*/
};

/**************************************************************************/

static void end_turn_callback(Widget w, XtPointer client_data,
			      XtPointer call_data);

static void timer_callback(caddr_t client_data, XtIntervalId *id);

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


#ifdef UNUSED
/**************************************************************************
...
**************************************************************************/
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
static Boolean toplevel_work_proc(XtPointer client_data)
{
  /* This will cause the connect dialog to pop-up.
     We do it here so that the main window exists when that happens,
     so that the connect dialog can position itself relative to it. */
  set_client_state(CLIENT_PRE_GAME_STATE);
  return (True);
}

/**************************************************************************
...
**************************************************************************/
void ui_main(int argc, char *argv[])
{
  int i, loglevel;
  Pixmap icon_pixmap; 
  XtTranslations TextFieldTranslations;
  Dimension w,h;
  char *tile_set_name = NULL;
  
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
    freelog(LOG_FATAL, _("No version number in resources."));
    freelog(LOG_FATAL, _("You probably have an old (circa V1.0)"
			 " Freeciv resource file somewhere."));
    exit(1);
  }

  /* TODO: Use capabilities here instead of version numbers */
  if(strncmp(appResources.version, VERSION_STRING,
	     strlen(appResources.version))) {
    freelog(LOG_FATAL, _("Game version does not match Resource version."));
    freelog(LOG_FATAL, _("Game version: %s - Resource version: %s"), 
	    VERSION_STRING, appResources.version);
    freelog(LOG_FATAL, _("You might have an old Freeciv resourcefile"
			 " in /usr/lib/X11/app-defaults"));
    exit(1);
  }
  
  if(!appResources.gotAppDefFile) {
    freelog(LOG_NORMAL, _("Using fallback resources - which is OK"));
  }

  if(appResources.name) {
    mystrlcpy(name, appResources.name, 512);
  } else {
    mystrlcpy(name, user_username(), 512);
  }

  if(appResources.server)
    mystrlcpy(server_host, appResources.server, 512);
  else {
    mystrlcpy(server_host, "localhost", 512);
  }

  if(appResources.metaserver)
    mystrlcpy(metaserver, appResources.metaserver, 256);
  else {
    mystrlcpy(metaserver, METALIST_ADDR, 256);
  }
    
  if(appResources.port)
    server_port=appResources.port;
  else
    server_port=DEFAULT_SOCK_PORT;

  if(appResources.tileset)
    tile_set_name=appResources.tileset; 
  
  boot_help_texts();		/* after log_init */

  display = XtDisplay(toplevel);
  screen_number=XScreenNumberOfScreen(XtScreen(toplevel));
  display_depth=DefaultDepth(display, screen_number);
  root_window=DefaultRootWindow(display);

  display_color_type=get_visual(); 

  
  if(display_color_type!=COLOR_DISPLAY) {
    freelog(LOG_FATAL, _("Only color displays are supported for now..."));
    /*    exit(1); */
  }
  
  icon_pixmap = XCreateBitmapFromData(display,
				      RootWindowOfScreen(XtScreen(toplevel)),
				      freeciv_bits,
				      freeciv_width, freeciv_height );
  XtVaSetValues(toplevel, XtNiconPixmap, icon_pixmap, NULL);

  init_color_system();


  /* get tile sizes etc, required for setup_widgets: */
  /* also, get cityname font name, required to load the font: */
  tilespec_read_toplevel(tile_set_name);

  {
    XGCValues values;

    values.graphics_exposures = False;
    civ_gc = XCreateGC(display, root_window, GCGraphicsExposures, &values);

    main_font_struct=XLoadQueryFont(display, city_names_font);
    if(main_font_struct==0) {
      freelog(LOG_FATAL, _("Failed loading font: %s"), city_names_font);
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
  
  /* 135 below is rough value (could be more intelligent) --dwp */
  num_units_below = 135/(int)NORMAL_TILE_WIDTH;
  num_units_below = MIN(num_units_below,MAX_NUM_UNITS_BELOW);
  num_units_below = MAX(num_units_below,1);
  
  /* do setup_widgets before loading the rest of graphics to ensure that
     setup_widgets() has enough colors available:  (on 256-colour systems)
  */
  setup_widgets();
  tilespec_load_tiles();
  load_intro_gfx();
  load_cursors();

  XtSetKeyboardFocus(bottom_form, inputline_text);
  XtSetKeyboardFocus(below_menu_form, map_canvas);
  
  TextFieldTranslations = XtParseTranslationTable  /*BLAH!*/
		("<Key>Return: key-chatline-return()");
  XtOverrideTranslations(inputline_text, TextFieldTranslations);

  InitializeActions(app_context);

  /* Do this outside setup_widgets() so after tiles are loaded */
  for(i=0;i<10;i++)  {
    XtVaSetValues(econ_label[i], XtNbitmap,
		  get_citizen_pixmap(i<5?1:2), NULL);
    XtAddCallback(econ_label[i], XtNcallback, taxrates_callback,
		  (XtPointer)i);  
  }
		
  XtAddCallback(map_horizontal_scrollbar, XtNjumpProc, 
		scrollbar_jump_callback, NULL);
  XtAddCallback(map_vertical_scrollbar, XtNjumpProc, 
		scrollbar_jump_callback, NULL);
  XtAddCallback(map_horizontal_scrollbar, XtNscrollProc, 
		scrollbar_scroll_callback, NULL);
  XtAddCallback(map_vertical_scrollbar, XtNscrollProc, 
		scrollbar_scroll_callback, NULL);
  XtAddCallback(turn_done_button, XtNcallback, end_turn_callback, NULL);

  XtAppAddWorkProc(app_context, toplevel_work_proc, NULL);

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

  overview_canvas_store=0;
  set_overview_dimensions(80, 50);

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
  XtOverrideTranslations(toplevel,
    XtParseTranslationTable ("<Message>WM_PROTOCOLS: msg-quit-freeciv()"));

  load_options();

  XtSetSensitive(toplevel, FALSE);

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



  /* Don't put the citizens in here yet because not loaded yet */
  for(i=0;i<10;i++)  {
    econ_label[i] = XtVaCreateManagedWidget("econlabels",
					    commandWidgetClass,
					    left_column_form,
					    XtNwidth, SMALL_TILE_WIDTH,
					    XtNheight, SMALL_TILE_HEIGHT,
					    i?XtNfromHoriz:NULL, 
					    i?econ_label[i-1]:NULL,
					    XtNhorizDistance, 1,
					    NULL);  
  }

  turn_done_button = I_L(XtVaCreateManagedWidget("turndonebutton", 
						 commandWidgetClass,
						 left_column_form,
						 NULL));
  
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
    char unit_below_name[32];
    my_snprintf(unit_below_name, sizeof(unit_below_name),
		"unitbelowcanvas%ld", i);
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



  outputwindow_text= I_SW(XtVaCreateManagedWidget("outputwindowtext", 
						  asciiTextWidgetClass, 
						  bottom_form,
						  NULL));


  inputline_text= XtVaCreateManagedWidget("inputlinetext", 
					  asciiTextWidgetClass, 
					  bottom_form,
					  NULL);

}

/**************************************************************************
...
**************************************************************************/
void main_quit_freeciv(void)
{ 
  exit(0);
}

/**************************************************************************
...
**************************************************************************/
void main_show_info_popup(XEvent *event)
{
  XButtonEvent *ev=(XButtonEvent *)event;
  if(ev->button==Button1) {
    Widget  p;
    Position x, y;
    Dimension w, h;
    char buf[512];

    my_snprintf(buf, sizeof(buf),
		 _("%s People\n"
		   "Year: %s\n"
		   "Gold: %d\n"
		   "Net Income: %d\n"
		   "Tax:%d Lux:%d Sci:%d\n"
		   "Researching %s: %d/%d"),
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
static void get_net_input(XtPointer client_data, int *fid, XtInputId *id)
{
  input_from_server(*fid);
}

/**************************************************************************
 This function is called after the client succesfully
 has connected to the server
**************************************************************************/
void add_net_input(int sock)
{
  x_input_id=XtAppAddInput(app_context, sock, 
			   (XtPointer) XtInputReadMask,
			   (XtInputCallbackProc) get_net_input, NULL);
}

/**************************************************************************
 This function is called if the client disconnects
 from the server
**************************************************************************/
void remove_net_input(void)
{
  XtRemoveInput(x_input_id);
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
void timer_callback(caddr_t client_data, XtIntervalId *id)
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
