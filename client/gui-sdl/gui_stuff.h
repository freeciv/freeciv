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

/**********************************************************************
                          gui_stuff.h  -  description
                             -------------------
    begin                : June 30 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__GUI_STUFF_H
#define FC__GUI_STUFF_H

#include "gui_mem.h"

#define	WINDOW_TILE_HIGH	20
#define	FRAME_WH		3
#define	DOUBLE_FRAME_WH		6

#define STATE_MASK		0x03
#define TYPE_MASK		0x03FC
#define FLAG_MASK		0xFFFFFC00

#define MAX_ID			0xFFFF
/* Text cetnter flags has been moved to 'string16->style' */

/* Widget FLAGS -> allowed 20 flags */
/* default: ICON_CENTER_Y, ICON_ON_LEFT */
#define WF_HIDDEN			0x400	/* 1024 */
#define WF_FREE_GFX			0x800	/* 2048 */
#define WF_FREE_THEME			0x1000	/* 4096 */
#define WF_FREE_STRING			0x2000	/* 8192 */
#define WF_DRAW_THEME_TRANSPARENT	0x4000	/* 16384 */
#define WF_ICON_ABOVE_TEXT		0x8000	/* 32768 */
#define WF_ICON_UNDER_TEXT		0x10000	/* 65536 */
#define WF_ICON_CENTER			0x20000	/* 131072 */
#define WF_ICON_CENTER_RIGHT		0x40000	/* 262144 */
#define WF_WIDGET_HAS_INFO_LABEL	0x80000	/* 524288 */
#define WF_DRAW_FRAME_AROUND_WIDGET	0x100000 /* 1048576 */
#define WF_DRAW_TEXT_LABEL_WITH_SPACE	0x200000 /* 2097152 */
#define WF_FREE_DATA			0x400000 /* # */

/* Widget states */
enum WState {
  WS_NORMAL = 0,
  WS_SELLECTED = 1,
  WS_PRESSED = 2,
  WS_DISABLED = 3
};

/* Widget Types */
enum WTypes {			/* allow 64 widgets type */
  WT_BUTTON = 4,		/* Button with Text (not use !!!)
				   ( can be transparent ) */
  WT_I_BUTTON = 8,		/* Button with TEXT and ICON
				   ( static; can't be transp. ) */
  WT_TI_BUTTON = 12,		/* Button with TEXT and ICON
				   (themed; can't be transp. ) */
  WT_EDIT = 16,			/* edit field   */
  WT_ICON = 20,			/* flat Button from 4 - state icon */
  WT_VSCROLLBAR = 24,		/* bugy */
  WT_HSCROLLBAR = 28,		/* not use (Not DEFINED !!) */
  WT_WINDOW = 32,
  WT_T_LABEL = 36,		/* text label with theme backgroud */
  WT_I_LABEL = 40,		/* text label with icon */
  WT_TI_LABEL = 44,		/* text label with icon and theme backgroud.
				   NOTE: Not DEFINED- don't use
				   ( can't be transp. ) */
  WT_CHECKBOX = 48,		/* checkbox. */
  WT_TCHECKBOX = 52,		/* text label with checkbox. */
  WT_ICON2 = 56			/* flat Button from 1 - state icon */
};

struct ScrollBar {
  Uint16 active;		/* used by scroll: active(sean)
				   size of scroll list */
  Uint16 count;			/* total size of scroll list */
  Sint16 min;			/* used by scroll: min position */
  Sint16 max;			/* used by scroll: max position */
};

struct GUI {
  struct GUI *next;
  struct GUI *prev;
  int (*action) (struct GUI *);	/* defalut callback action */
  SDL_Surface *theme;
  SDL_Surface *gfx;		/* Icon, background or theme2 */
  SDL_String16 *string16;
  
  void *data;
  
  Uint32 state_types_flags;	/* "packed" widget info */

  SDL_Rect size;		/* size.w and size.h are the size of widget   
				   size.x and size.y are the draw pozitions. */
  SDLKey key;			/* key alliased with this widget */
  Uint16 mod;			/* SHIFT, CTRL, ALT, etc */
  Uint16 ID;			/* ID in widget list */
};

SDL_Surface *create_bcgnd_surf(SDL_Surface *pTheme, SDL_bool transp,
			       Uint8 state, Uint16 Width, Uint16 High);

void init_gui_list(Uint16 ID, struct GUI *pGUI);
void add_to_gui_list(Uint16 ID, struct GUI *pGUI);
void del_widget_pointer_from_gui_list(struct GUI *pGUI);

int is_this_widget_first_on_list(struct GUI *pGUI);
void move_widget_to_front_of_gui_list(struct GUI *pGUI);

void del_gui_list(struct GUI *pGUI_List);
void del_main_list(void);

struct GUI *WidgetListScaner(const struct GUI *pGUI_List,
			     SDL_MouseMotionEvent *pPosition);
struct GUI *MainWidgetListScaner(SDL_MouseMotionEvent *pPosition);
struct GUI *WidgetListKeyScaner(const struct GUI *pGUI_List,
				SDL_keysym Key);
struct GUI *MainWidgetListKeyScaner(SDL_keysym Key);

struct GUI *get_widget_pointer_form_ID(const struct GUI *pGUI_List,
				       Uint16 ID);
struct GUI *get_widget_pointer_form_main_list(Uint16 ID);

void widget_sellected_action(struct GUI *pWidget);
Uint16 widget_pressed_action(struct GUI *pWidget);

void unsellect_widget_action(void);

/* Group */
Uint16 redraw_group(const struct GUI *pBeginGroupWidgetList,
		    const struct GUI *pEndGroupWidgetList,
		    int add_to_update);
void set_new_group_start_pos(const struct GUI *pBeginGroupWidgetList,
			     const struct GUI *pEndGroupWidgetList,
			     Sint16 Xrel, Sint16 Yrel);
void del_group_of_widgets_from_gui_list(struct GUI *pBeginGroupWidgetList,
					struct GUI *pEndGroupWidgetList);
void move_group_to_front_of_gui_list(struct GUI *pBeginGroupWidgetList,
				     struct GUI *pEndGroupWidgetList);

void set_group_state(struct GUI *pBeginGroupWidgetList,
		     struct GUI *pEndGroupWidgetList, enum WState state);

void show_group(struct GUI *pBeginGroupWidgetList,
		struct GUI *pEndGroupWidgetList);

void hide_group(struct GUI *pBeginGroupWidgetList,
		struct GUI *pEndGroupWidgetList);

int redraw_widget(struct GUI *pWidget);

/* Window Group */
void popdown_window_group_dialog(struct GUI *pBeginGroupWidgetList,
				 struct GUI *pEndGroupWidgetList);

int move_window_group_dialog(struct GUI *pBeginGroupWidgetList,
			     struct GUI *pEndGroupWidgetList);

/* Vertic scrolling */
struct GUI *down_scroll_widget_list(struct GUI *pVscrollBarWidget,
				    struct ScrollBar *pVscroll,
				    struct GUI *pBeginActiveWidgetLIST,
				    struct GUI *pBeginWidgetLIST,
				    struct GUI *pEndWidgetLIST);
struct GUI *up_scroll_widget_list(struct GUI *pVscrollBarWidget,
				  struct ScrollBar *pVscroll,
				  struct GUI *pBeginActiveWidgetLIST,
				  struct GUI *pBeginWidgetLIST,
				  struct GUI *pEndWidgetLIST);
struct GUI *vertic_scroll_widget_list(struct GUI *pVscrollBarWidget,
				      struct ScrollBar *pVscroll,
				      struct GUI *pBeginActiveWidgetLIST,
				      struct GUI *pBeginWidgetLIST,
				      struct GUI *pEndWidgetLIST);

/* ICON */
void set_new_icon_theme(struct GUI *pIcon_Widget,
			SDL_Surface *pNew_Theme);
SDL_Surface *create_icon_theme_surf(SDL_Surface *pIcon);
struct GUI *create_themeicon(SDL_Surface *pIcon_theme, Uint32 flags);
SDL_Surface *create_icon_from_theme(SDL_Surface *pIcon_theme,
				    Uint8 state);
int draw_icon_from_theme(SDL_Surface *pIcon_theme, Uint8 state,
			 SDL_Surface *pDest, Sint16 start_x,
			 Sint16 start_y);
int real_redraw_icon(struct GUI *pIcon, SDL_Surface *pDest);
int draw_icon(struct GUI *pIcon, Sint16 start_x, Sint16 start_y);

/* ICON2 */
int real_redraw_icon2(struct GUI *pIcon, SDL_Surface *pDest);
struct GUI *create_icon2(SDL_Surface *pIcon, Uint32 flags);

/* BUTTON */
struct GUI *create_icon_button(SDL_Surface *pIcon, SDL_String16 *pString,
			       Uint32 flags);

struct GUI *create_themeicon_button(SDL_Surface *pIcon_theme,
				    SDL_String16 *pString, Uint32 flags);

int real_redraw_ibutton(struct GUI *pTIButton, SDL_Surface *pDest);
int real_redraw_tibutton(struct GUI *pTIButton, SDL_Surface *pDest);

int draw_tibutton(struct GUI *pButton, Sint16 start_x, Sint16 start_y);
int draw_ibutton(struct GUI *pButton, Sint16 start_x, Sint16 start_y);

/* EDIT */
struct GUI *create_edit(SDL_Surface *pBackground,
			SDL_String16 *pString16, Uint16 lenght,
			Uint32 flags);
void edit_field(struct GUI *pEdit_Widget);
int redraw_edit(struct GUI *pEdit_Widget);

int draw_edit(struct GUI *pEdit, Sint16 start_x, Sint16 start_y);

/* VERTICAL */
struct GUI *create_vertical(SDL_Surface *pVert_theme, Uint16 high,
			    Uint32 flags);
int redraw_vert(struct GUI *pVert);
void draw_vert(struct GUI *pVert, Sint16 x, Sint16 y);

/* HORIZONTAL */
struct GUI *create_horizontal(SDL_Surface *pHoriz_theme, Uint16 width,
			      Uint32 flags);
int redraw_horiz(struct GUI *pHoriz);
void draw_horiz(struct GUI *pHoriz, Sint16 x, Sint16 y);

/* WINDOW */
struct GUI *create_window(SDL_String16 *pTitle, Uint16 w, Uint16 h,
			  Uint32 flags);

int resize_window(struct GUI *pWindow, SDL_Surface *pBcgd,
		  SDL_Color *pColor, Uint16 new_w, Uint16 new_h);

int redraw_window(struct GUI *pWindow);
int move_window(struct GUI *pWindow);

/* misc */
void draw_frame(SDL_Surface *pDest, Sint16 start_x, Sint16 start_y,
		Uint16 w, Uint16 h);
void refresh_widget_background(struct GUI *pWidget);
void draw_menubuttons(struct GUI *pFirstButtonOnList);

/* LABEL */
struct GUI *create_themelabel(SDL_Surface *pBackground,
			      SDL_String16 *pText, Uint16 w, Uint16 h,
			      Uint32 flags);
struct GUI *create_iconlabel(SDL_Surface *pIcon, SDL_String16 *pText,
			     Uint32 flags);
int draw_label(struct GUI *pLabel, Sint16 start_x, Sint16 start_y);

int redraw_label(struct GUI *pLabel);
void remake_label_size(struct GUI *pLabel);

/* CHECKBOX */
struct GUI *create_textcheckbox(bool state, SDL_String16 *pStr,
				Uint32 flags);
struct GUI *create_checkbox(bool state, Uint32 flags);
void togle_checkbox(struct GUI *pCBox);
bool get_checkbox_state(struct GUI *pCBox);
int redraw_textcheckbox(struct GUI *pCBox);

#define set_wstate(pWidget, state )		\
do {						\
  pWidget->state_types_flags &= ~STATE_MASK;	\
  pWidget->state_types_flags |= state;		\
} while(0)

#define set_wtype(pWidget, type)		\
do {						\
  pWidget->state_types_flags &= ~TYPE_MASK;	\
  pWidget->state_types_flags |= type;		\
} while(0)

#define set_wflag(pWidget, flag )	\
	(pWidget)->state_types_flags |= ((flag) & FLAG_MASK)

#define clear_wflag(pWidget, flag)	\
	(pWidget)->state_types_flags &= ~((flag) & FLAG_MASK)

#define get_wstate(pWidget)				\
	fc__extension(pWidget->state_types_flags & STATE_MASK)

#define get_wtype(pWidget)				\
	fc__extension(pWidget->state_types_flags & TYPE_MASK)

#define get_wflags(pWidget)				\
	fc__extension(pWidget->state_types_flags & FLAG_MASK)


#define FREEWIDGET(pGUI)			\
do {						\
  if (get_wflags(pGUI) & WF_FREE_STRING) {	\
    FREESTRING16(pGUI->string16);		\
  }						\
  if (get_wflags(pGUI) & WF_FREE_GFX) {		\
    FREESURFACE(pGUI->gfx);			\
  }						\
  if (get_wflags(pGUI) & WF_FREE_THEME) {	\
    FREESURFACE(pGUI->theme);			\
  }						\
  if (get_wflags(pGUI) & WF_FREE_DATA) {		\
    FREE(pGUI->data);				\
  }						\
  FREE(pGUI);					\
} while(0)

#define redraw_ID(ID) \
	redraw_widget(get_widget_pointer_form_main_list(ID))

#define refresh_ID_background(ID) \
	refresh_widget_background(get_widget_pointer_form_main_list(ID))

#define del_widget_from_gui_list(__pGUI)	\
do {						\
  del_widget_pointer_from_gui_list(__pGUI);	\
  FREEWIDGET(__pGUI);				\
} while(0)

#define del_ID_from_gui_list(ID)				\
do {								\
  struct GUI *___pTmp = get_widget_pointer_form_main_list(ID);	\
  del_widget_pointer_from_gui_list(___pTmp);			\
  FREEWIDGET(___pTmp);						\
} while(0)

#define move_ID_to_front_of_gui_list(ID)	\
	move_widget_to_front_of_gui_list(       \
          get_widget_pointer_form_main_list(ID))

#define del_group( pBeginGroupWidgetList, pEndGroupWidgetList )		\
do {									\
  del_group_of_widgets_from_gui_list(pBeginGroupWidgetList,		\
					   pEndGroupWidgetList);	\
  pBeginGroupWidgetList = NULL;						\
  pEndGroupWidgetList = NULL;						\
} while(0)

#define enable_group(pBeginGroupWidgetList, pEndGroupWidgetList)	\
	set_group_state(pBeginGroupWidgetList, 				\
			pEndGroupWidgetList, WS_NORMAL)

#define disable_group(pBeginGroupWidgetList, pEndGroupWidgetList)	\
	set_group_state(pBeginGroupWidgetList,	pEndGroupWidgetList,	\
			WS_DISABLED)

#define set_action(ID, action_callback)	\
	get_widget_pointer_form_main_list(ID)->action = action_callback

#define set_key(ID, keyb)	\
	get_widget_pointer_form_main_list(ID)->key = keyb

#define set_mod(ID, mod )	\
	get_widget_pointer_form_main_list(ID)->mod = mod

#define enable(ID)						\
do {								\
  struct GUI *____pGUI = get_widget_pointer_form_main_list(ID);	\
  set_wstate(____pGUI, WS_NORMAL);				\
} while(0)

#define disable(ID)						\
do {								\
  struct GUI *____pGUI = get_widget_pointer_form_main_list(ID);	\
  set_wstate(____pGUI , WS_DISABLED);				\
} while(0)

#define show(ID)	\
  clear_wflag( get_widget_pointer_form_main_list(ID), WF_HIDDEN)

#define hide(ID)	\
  set_wflag(get_widget_pointer_form_main_list(ID), WF_HIDDEN)

/* BUTTON */
#define create_icon_button_from_unichar(pIcon, pUniChar, iPtsize, flags) \
	create_icon_button(pIcon, create_string16(pUniChar, iPtsize), flags)

#define create_icon_button_from_chars(pIcon, pCharString, iPtsize, flags) \
	create_icon_button(pIcon,                                         \
			   create_str16_from_char(pCharString, iPtsize),  \
			   flags)

#define create_themeicon_button_from_unichar(pIcon_theme, pUniChar, iPtsize, flags) \
	create_themeicon_button(pIcon, create_string16(pUniChar, iPtsize), \
				flags)

#define create_themeicon_button_from_chars(pIcon_theme, pCharString, iPtsize, flags) \
	create_themeicon_button(pIcon_theme,                        \
				create_str16_from_char(pCharString, \
						       iPtsize),    \
				flags)

#define redraw_tibutton(pButton)	\
	real_redraw_tibutton(pButton, Main.screen)

#define redraw_ibutton(pButton)	\
	real_redraw_ibutton(pButton, Main.screen)

/* EDIT */
#define create_edit_from_chars(pBackground, pCharString, iPtsize, length, flags)                                                                 \
	create_edit(pBackground,                                  \
		    create_str16_from_char(pCharString, iPtsize), \
		    length, flags)

#define create_edit_from_unichars(pBackground, pUniChar, iPtsize, length, flags) \
	create_edit(pBackground, create_string16(pUniChar, iPtsize), length, flags )

#define edit(pEdit) edit_field(pEdit)

#define draw_frame_around_widget(pWidget)				\
	draw_frame(Main.screen, pWidget->size.x - FRAME_WH,		\
				pWidget->size.y - FRAME_WH,		\
				pWidget->size.w + DOUBLE_FRAME_WH,	\
				pWidget->size.h + DOUBLE_FRAME_WH)

/* ICON */
#define redraw_icon(pIcon)	\
	real_redraw_icon(pIcon, Main.screen)

#define redraw_icon2(pIcon)	\
	real_redraw_icon2(pIcon, Main.screen)

/* LABEL */
#define create_iconlabel_from_chars(pIcon, pCharString, iPtsize, flags) \
	create_iconlabel(pIcon, create_str16_from_char(pCharString, iPtsize), flags)

#endif				/* FC__GUI_STUFF_H */
