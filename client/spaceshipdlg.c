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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/AsciiText.h>  
#include <X11/IntrinsicP.h>

#include <pixcomm.h>
#include <game.h>
#include <player.h>
#include <spaceshipdlg.h>
#include <shared.h>
#include <genlist.h>
#include <canvas.h>
#include <mapview.h>
#include <mapctrl.h>
#include <map.h>
#include <packets.h>
#include <dialogs.h>
#include <inputdlg.h>
#include <xstuff.h>
#include <colors.h>
#include <repodlgs.h>
#include <helpdlg.h>
#include <graphics.h>

extern int SPACE_TILES;
extern GC civ_gc, fill_bg_gc;

extern Display *display;
extern Widget toplevel, main_form;
extern int display_depth;
extern struct connection aconnection;

const int structurals_pos[NUM_SS_STRUCTURALS][2] = {
  {19, 13},
  {19, 15},
  {19, 11},
  {19, 17},
  {19,  9},
  {19, 19},
  {17,  9},
  {17, 19},
  {21, 11},
  {21, 17},
  {15,  9},
  {15, 19},
  {23, 11},
  {23, 17},
  {13,  9},
  {13, 19},
  {11,  9},
  {11, 19},
  { 9,  9},
  { 9, 19},
  { 7,  9},
  { 7, 19},
  {19,  7},
  {19, 21},
  {19,  5},
  {19, 23},
  {21,  5},
  {21, 23},
  {23,  5},
  {23, 23},
  { 5,  9},
  { 5, 19}
};

const int modules_pos[NUM_SS_MODULES][2] = {
  {16, 12},
  {16, 16},
  {14, 6},
  {12, 16},
  {12, 12},
  {14, 22},
  { 8, 12},
  { 8, 16},
  { 6,  6},
  { 4, 16},
  { 4, 12},
  { 6, 22}
};

const int components_pos[NUM_SS_COMPONENTS][2] = {
  {21, 13},
  {24, 13},
  {21, 15},
  {24, 15},
  {21,  9},
  {24,  9},
  {21, 19},
  {24, 19},
  {21,  7},
  {24,  7},
  {21, 21},
  {24, 21},
  {21,  3},
  {24,  3},
  {21, 25},
  {24, 25}
};

struct spaceship_dialog {
  struct player *pplayer;
  Widget shell;
  Widget main_form;
  Widget player_label;
  Widget info_label;
  Widget image_canvas;
  Widget launch_command;
  Widget close_command;
};

struct genlist dialog_list;
int dialog_list_has_been_initialised;

struct spaceship_dialog *get_spaceship_dialog(struct player *pplayer);
struct spaceship_dialog *create_spaceship_dialog(struct player *pplayer);
void close_spaceship_dialog(struct spaceship_dialog *pdialog);

void spaceship_dialog_update_image(struct spaceship_dialog *pdialog);
void spaceship_dialog_update_info(struct spaceship_dialog *pdialog);

void spaceship_close_callback(Widget w, XtPointer client_data, XtPointer call_data);
void spaceship_launch_callback(Widget w, XtPointer client_data, XtPointer call_data);

extern Atom wm_delete_window;

/****************************************************************
...
*****************************************************************/
struct spaceship_dialog *get_spaceship_dialog(struct player *pplayer)
{
  struct genlist_iterator myiter;

  if(!dialog_list_has_been_initialised) {
    genlist_init(&dialog_list);
    dialog_list_has_been_initialised=1;
  }
  
  genlist_iterator_init(&myiter, &dialog_list, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct spaceship_dialog *)ITERATOR_PTR(myiter))->pplayer==pplayer)
      return ITERATOR_PTR(myiter);

  return 0;
}

/****************************************************************
...
*****************************************************************/
void refresh_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;
  struct player_spaceship *pship;

  if(!(pdialog=get_spaceship_dialog(pplayer)))
    return;

  pship=&(pdialog->pplayer->spaceship);

  /* FIXME only if enough thrust etc.  (Falk)
   * Temporary hack by dwp.
   */
  if(game.spacerace
     && pplayer->player_no == game.player_idx
     && pship->state == SSHIP_STARTED
     && pship->structurals >= 4
     && pship->components >= 2
     && pship->modules >= 3 ) {
    XtSetSensitive(pdialog->launch_command, TRUE);
  } else {
    XtSetSensitive(pdialog->launch_command, FALSE);
  }
  
  spaceship_dialog_update_info(pdialog);
  spaceship_dialog_update_image(pdialog);
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;
  
  if(!(pdialog=get_spaceship_dialog(pplayer)))
    pdialog=create_spaceship_dialog(pplayer);

  xaw_set_relative_position(toplevel, pdialog->shell, 10, 10);
  XtPopup(pdialog->shell, XtGrabNone);
}

/****************************************************************
popdown the dialog 
*****************************************************************/
void popdown_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;
  
  if((pdialog=get_spaceship_dialog(pplayer)))
    close_spaceship_dialog(pdialog);
}



/****************************************************************
...
*****************************************************************/
void spaceship_image_canvas_expose(Widget w, XEvent *event, Region exposed, 
				   void *client_data)
{
  struct spaceship_dialog *pdialog;
  
  pdialog=(struct spaceship_dialog *)client_data;
  spaceship_dialog_update_image(pdialog);
}


/****************************************************************
...
*****************************************************************/
struct spaceship_dialog *create_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;
  XtTranslations textfieldtranslations;
  
  pdialog=(struct spaceship_dialog *)malloc(sizeof(struct spaceship_dialog));
  pdialog->pplayer=pplayer;

  pdialog->shell=XtVaCreatePopupShell(pplayer->name,
				      topLevelShellWidgetClass,
				      toplevel, 
				      XtNallowShellResize, True, 
				      NULL);
  
  pdialog->main_form=
    XtVaCreateManagedWidget("spaceshipmainform", 
			    formWidgetClass, 
			    pdialog->shell, 
			    NULL);
  pdialog->player_label=
    XtVaCreateManagedWidget("spaceshipplayerlabel", 
			    labelWidgetClass,
			    pdialog->main_form,
			    NULL);

  XtVaSetValues(pdialog->player_label, XtNlabel, (XtArgVal)pplayer->name, NULL);

  pdialog->image_canvas=
    XtVaCreateManagedWidget("spaceshipimagecanvas",
			    xfwfcanvasWidgetClass,
			    pdialog->main_form,
			    "exposeProc", (XtArgVal)spaceship_image_canvas_expose,
			    "exposeProcData", (XtArgVal)pdialog,
			    XtNwidth,  get_tile_sprite(SPACE_TILES)->width * 7,
			    XtNheight, get_tile_sprite(SPACE_TILES)->height * 7,
			    NULL);

  pdialog->info_label=
    XtVaCreateManagedWidget("spaceshipinfolabel", 
			    labelWidgetClass,
			    pdialog->main_form,
			    NULL);

  
  pdialog->close_command=
    XtVaCreateManagedWidget("spaceshipclosecommand", 
			    commandWidgetClass,
			    pdialog->main_form,
			    NULL);

  pdialog->launch_command=
    XtVaCreateManagedWidget("spaceshiplaunchcommand", 
			    commandWidgetClass,
			    pdialog->main_form,
			    NULL);

  XtAddCallback(pdialog->launch_command, XtNcallback, spaceship_launch_callback,
		(XtPointer)pdialog);

  XtAddCallback(pdialog->close_command, XtNcallback, spaceship_close_callback,
		(XtPointer)pdialog);

  genlist_insert(&dialog_list, pdialog, 0);

  XtRealizeWidget(pdialog->shell);

  refresh_spaceship_dialog(pdialog->pplayer);

  XSetWMProtocols(display, XtWindow(pdialog->shell), &wm_delete_window, 1);
  XtOverrideTranslations(pdialog->shell, 
    XtParseTranslationTable ("<Message>WM_PROTOCOLS: close-spaceshipdialog()"));

  textfieldtranslations = 
    XtParseTranslationTable("<Key>Return: spaceship-dialog-returnkey()");
  XtOverrideTranslations(pdialog->close_command, textfieldtranslations);
  XtSetKeyboardFocus(pdialog->shell, pdialog->close_command);

  return pdialog;
}

/****************************************************************
...
*****************************************************************/
void spaceship_dialog_update_info(struct spaceship_dialog *pdialog)
{
  char buf[512];
  struct player_spaceship *pship=&(pdialog->pplayer->spaceship);

  sprintf(buf, "Structurals:     %4d\n"
	       "Components:      %4d\n"
	       "Modules:         %4d\n"
	       "Year of arrival: %4d",
	  pship->structurals,
	  pship->components,
	  pship->modules,
	  pship->arrival_year);

  xaw_set_label(pdialog->info_label, buf);
}

/****************************************************************
...
*****************************************************************/
void spaceship_dialog_update_image(struct spaceship_dialog *pdialog)
{
  int i;  
  struct Sprite *sprite=get_tile_sprite(SPACE_TILES);

  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_BLACK]);
  XFillRectangle(display, XtWindow(pdialog->image_canvas), fill_bg_gc, 0, 0,
		 sprite->width * 7, sprite->height * 7);

  for (i = 0; i < pdialog->pplayer->spaceship.modules; i++) {
    int x = modules_pos[i][0] * sprite->width  / 4 - sprite->width / 2,
        y = modules_pos[i][1] * sprite->height / 4 - sprite->height / 2;

    sprite = get_tile_sprite(SPACE_TILES + 2 - i % 3);
    XSetClipOrigin(display, civ_gc, x, y);
    XSetClipMask(display, civ_gc, sprite->mask);
    XCopyArea(display, sprite->pixmap, XtWindow(pdialog->image_canvas), 
	      civ_gc, 
	      0, 0,
	      sprite->width, sprite->height, x, y);
    XSetClipMask(display,civ_gc,None);
  }

  for (i = 0; i < pdialog->pplayer->spaceship.components; i++) {
    int x = components_pos[i][0] * sprite->width  / 4 - sprite->width / 2,
        y = components_pos[i][1] * sprite->height / 4 - sprite->height / 2;

    sprite = get_tile_sprite(SPACE_TILES + 4 + i % 2);

    XSetClipOrigin(display, civ_gc, x, y);
    XSetClipMask(display, civ_gc, sprite->mask);
    XCopyArea(display, sprite->pixmap, XtWindow(pdialog->image_canvas), 
	      civ_gc, 
	      0, 0,
	      sprite->width, sprite->height, x, y);
    XSetClipMask(display,civ_gc,None);
  }

  sprite = get_tile_sprite(SPACE_TILES + 3);

  for (i = 0; i < pdialog->pplayer->spaceship.structurals; i++) {
    int x = structurals_pos[i][0] * sprite->width  / 4 - sprite->width / 2,
        y = structurals_pos[i][1] * sprite->height / 4 - sprite->height / 2;

    XSetClipOrigin(display, civ_gc, x, y);
    XSetClipMask(display, civ_gc, sprite->mask);
    XCopyArea(display, sprite->pixmap, XtWindow(pdialog->image_canvas), 
	      civ_gc, 
	      0, 0,
	      sprite->width, sprite->height, x, y);
    XSetClipMask(display,civ_gc,None);
  }
}


/****************************************************************
...
*****************************************************************/
void close_spaceship_dialog(struct spaceship_dialog *pdialog)
{
  XtDestroyWidget(pdialog->shell);
  genlist_unlink(&dialog_list, pdialog);

  free(pdialog);
}

/****************************************************************
...
*****************************************************************/
void spaceship_dialog_returnkey(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  close_spaceship_dialog_action(XtParent(XtParent(w)), 0, 0, 0);
}

/****************************************************************
...
*****************************************************************/
void close_spaceship_dialog_action(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &dialog_list, 0);
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct spaceship_dialog *)ITERATOR_PTR(myiter))->shell==w) {
      close_spaceship_dialog((struct spaceship_dialog *)ITERATOR_PTR(myiter));
      return;
    }
}


/****************************************************************
...
*****************************************************************/
void spaceship_close_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  close_spaceship_dialog((struct spaceship_dialog *)client_data);
}


/****************************************************************
...
*****************************************************************/
void spaceship_launch_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  struct packet_player_request packet;
	
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_LAUNCH_SPACESHIP);
  close_spaceship_dialog((struct spaceship_dialog *)client_data);
}
