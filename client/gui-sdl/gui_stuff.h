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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
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
enum WFlags {
  WF_HIDDEN				= (1<<10),	/* 1024 */
  WF_FREE_GFX	 			= (1<<11),	/* 2048 */
  WF_FREE_THEME				= (1<<12),	/* 4096 */
  WF_FREE_STRING			= (1<<13),	/* 8192 */
  WF_FREE_DATA			 	= (1<<14),	/* 16384 */
  WF_FREE_PRIVATE_DATA			= (1<<15),	/* 32768 */
  WF_ICON_ABOVE_TEXT			= (1<<16),	/* 32768 */
  WF_ICON_UNDER_TEXT			= (1<<17),	/* 65536 */
  WF_ICON_CENTER			= (1<<18),	/* 131072 */
  WF_ICON_CENTER_RIGHT			= (1<<19),	/* 262144 */
  WF_DRAW_THEME_TRANSPARENT		= (1<<20),	/* 524288 */
  WF_DRAW_FRAME_AROUND_WIDGET	 	= (1<<21),	/* 1048576 */
  WF_DRAW_TEXT_LABEL_WITH_SPACE		= (1<<22),	/* 2097152 */
  WF_WIDGET_HAS_INFO_LABEL		= (1<<23),	/* 4194304 */
  WF_SELLECT_WITHOUT_BAR		= (1<<24),	/* 8388608 */
  WF_PASSWD_EDIT			= (1<<25),
  WF_EDIT_LOOP				= (1<<26)
};

/* Widget states */
enum WState {
  FC_WS_NORMAL	= 0,
  FC_WS_SELLECTED	= 1,
  FC_WS_PRESSED	= 2,
  FC_WS_DISABLED	= 3
};

/* Widget Types */
enum WTypes {			/* allow 64 widgets type */
  WT_BUTTON	 = 4,		/* Button with Text (not use !!!)
				   ( can be transparent ) */
  WT_I_BUTTON	 = 8,		/* Button with TEXT and ICON
				   ( static; can't be transp. ) */
  WT_TI_BUTTON	= 12,		/* Button with TEXT and ICON
				   (themed; can't be transp. ) */
  WT_EDIT	= 16,		/* edit field   */
  WT_ICON	= 20,		/* flat Button from 4 - state icon */
  WT_VSCROLLBAR = 24,		/* bugy */
  WT_HSCROLLBAR = 28,		/* bugy */
  WT_WINDOW	= 32,
  WT_T_LABEL	= 36,		/* text label with theme backgroud */
  WT_I_LABEL	= 40,		/* text label with icon */
  WT_TI_LABEL	= 44,		/* text label with icon and theme backgroud.
				   NOTE: Not DEFINED- don't use
				   ( can't be transp. ) */
  WT_CHECKBOX	= 48,		/* checkbox. */
  WT_TCHECKBOX	= 52,		/* text label with checkbox. */
  WT_ICON2	= 56,		/* flat Button from 1 - state icon */
  WT_T2_LABEL	= 60
};

enum Edit_Return_Codes {
  ED_RETURN = 1,
  ED_ESC = 2,
  ED_MOUSE = 3,
  ED_FORCE_EXIT = 4
};

struct city;
struct unit;
struct player;
  
struct CONTAINER {
  int id0;
  int id1;
  int value;
};

struct ScrollBar {
  struct GUI *pUp_Left_Button;
  struct GUI *pScrollBar;
  struct GUI *pDown_Right_Button;  
  Uint8 active;		/* used by scroll: numbers of displayed rows */
  Uint8 step;		/* used by scroll: numbers of displayed columns */
  /* total dispalyed widget = active * step */
  Uint16 count;		/* total size of scroll list */
  Sint16 min;		/* used by scroll: min pixel position */
  Sint16 max;		/* used by scroll: max pixel position */
};

struct CHECKBOX {
  SDL_Surface *pTRUE_Theme;
  SDL_Surface *pFALSE_Theme;
  bool state;
};

struct SMALL_DLG;
struct ADVANCED_DLG;
  
struct GUI {
  struct GUI *next;
  struct GUI *prev;
  int (*action) (struct GUI *);	/* defalut callback action */
  SDL_Surface *dst;  		/* destination buffer layer */
  SDL_Surface *theme;
  SDL_Surface *gfx;		/* Icon, background or theme2 */
  SDL_String16 *string16;
  
  /* data/information/transport pointers */
  union {
    struct CONTAINER *cont;
    struct city *city;
    struct unit *unit;
    struct player *player;
    void *ptr;
  } data;
  
  union {
    struct CHECKBOX *cbox;
    struct SMALL_DLG *small_dlg;
    struct ADVANCED_DLG *adv_dlg;
    void *ptr;
  } private_data;
  
  Uint32 state_types_flags;	/* "packed" widget info */

  SDL_Rect size;		/* size.w and size.h are the size of widget   
				   size.x and size.y are the draw pozitions. */
  SDLKey key;			/* key alliased with this widget */
  Uint16 mod;			/* SHIFT, CTRL, ALT, etc */
  Uint16 ID;			/* ID in widget list */
};

#define scrollbar_size(pScroll) 					\
  fc__extension((float)((float)(pScroll->active * pScroll->step) /	\
		(float)pScroll->count) * (pScroll->max - pScroll->min))

/* Struct of basic window group dialog ( without scrollbar ) */
struct SMALL_DLG {
  struct GUI *pBeginWidgetList;
  struct GUI *pEndWidgetList;	/* window */
};

/* Struct of advenced window group dialog ( with scrollbar ) */
struct ADVANCED_DLG {
  struct GUI *pBeginWidgetList;
  struct GUI *pEndWidgetList;/* window */
    
  struct GUI *pBeginActiveWidgetList;
  struct GUI *pEndActiveWidgetList;
  struct GUI *pActiveWidgetList; /* first seen widget */
  struct ScrollBar *pScroll;
};

SDL_Surface *create_bcgnd_surf(SDL_Surface *pTheme, SDL_bool transp,
			       Uint8 state, Uint16 Width, Uint16 High);

void init_gui_list(Uint16 ID, struct GUI *pGUI);
void add_to_gui_list(Uint16 ID, struct GUI *pGUI);
void del_widget_pointer_from_gui_list(struct GUI *pGUI);
void DownAdd(struct GUI *pNew_Widget, struct GUI *pAdd_Dock);
  
bool is_this_widget_first_on_list(struct GUI *pGUI);
void move_widget_to_front_of_gui_list(struct GUI *pGUI);

void del_gui_list(struct GUI *pGUI_List);
void del_main_list(void);

void lock_buffer(SDL_Surface *pBuffer);
void unlock_buffer(void);
SDL_Surface * get_locked_buffer(void);
void remove_locked_buffer(void);

struct GUI *WidgetListScaner(const struct GUI *pGUI_List, int x, int y);
struct GUI *MainWidgetListScaner(int x, int y);
struct GUI *WidgetListKeyScaner(const struct GUI *pGUI_List,
				SDL_keysym Key);
struct GUI *MainWidgetListKeyScaner(SDL_keysym Key);

struct GUI *get_widget_pointer_form_ID(const struct GUI *pGUI_List, Uint16 ID);
struct GUI *get_widget_pointer_form_main_list(Uint16 ID);

void widget_sellected_action(struct GUI *pWidget);
Uint16 widget_pressed_action(struct GUI *pWidget);

void unsellect_widget_action(void);
void draw_widget_info_label(void);

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

bool sellect_window_group_dialog(struct GUI *pBeginWidgetList,
				struct GUI *pWindow);
bool move_window_group_dialog(struct GUI *pBeginGroupWidgetList,
			     struct GUI *pEndGroupWidgetList);
int std_move_window_group_callback(struct GUI *pBeginWidgetList,
				struct GUI *pWindow);
				      
Uint32 create_vertical_scrollbar(struct ADVANCED_DLG *pDlg,
	Uint8 step, Uint8 active, bool create_scrollbar, bool create_buttons);
void setup_vertical_scrollbar_area(struct ScrollBar *pScroll,
	Sint16 start_x, Sint16 start_y, Uint16 hight, bool swap_start_x);
void setup_vertical_scrollbar_default_callbacks(struct ScrollBar *pScroll);
  
int setup_vertical_widgets_position(int step,
	Sint16 start_x, Sint16 start_y, Uint16 w, Uint16 h,
				struct GUI *pBegin, struct GUI *pEnd);


bool add_widget_to_vertical_scroll_widget_list(struct ADVANCED_DLG *pDlg,
				      struct GUI *pNew_Widget,
				      struct GUI *pAdd_Dock, bool dir,
					Sint16 start_x, Sint16 start_y);
				      
bool del_widget_from_vertical_scroll_widget_list(struct ADVANCED_DLG *pDlg, 
  						struct GUI *pWidget);
				      
/* Horizontal scrolling */
Uint32 create_horizontal_scrollbar(struct ADVANCED_DLG *pDlg,
	  Sint16 start_x, Sint16 start_y, Uint16 width, Uint16 active,
	  bool create_scrollbar, bool create_buttons, bool swap_start_y);

/* ICON */
void set_new_icon_theme(struct GUI *pIcon_Widget,
			SDL_Surface *pNew_Theme);
SDL_Surface *create_icon_theme_surf(SDL_Surface *pIcon);
struct GUI *create_themeicon(SDL_Surface *pIcon_theme,
			  SDL_Surface *pDest ,Uint32 flags);
SDL_Surface *create_icon_from_theme(SDL_Surface *pIcon_theme,
				    Uint8 state);
int draw_icon_from_theme(SDL_Surface *pIcon_theme, Uint8 state,
			 SDL_Surface *pDest, Sint16 start_x,
			 Sint16 start_y);
int real_redraw_icon(struct GUI *pIcon);
int draw_icon(struct GUI *pIcon, Sint16 start_x, Sint16 start_y);

/* ICON2 */
void set_new_icon2_theme(struct GUI *pIcon_Widget, SDL_Surface *pNew_Theme,
			  bool free_old_theme);
int real_redraw_icon2(struct GUI *pIcon);
struct GUI *create_icon2(SDL_Surface *pIcon, SDL_Surface *pDest, Uint32 flags);

/* BUTTON */
struct GUI *create_icon_button(SDL_Surface *pIcon,
	  SDL_Surface *pDest, SDL_String16 *pString, Uint32 flags);

struct GUI *create_themeicon_button(SDL_Surface *pIcon_theme,
		SDL_Surface *pDest, SDL_String16 *pString, Uint32 flags);

int real_redraw_ibutton(struct GUI *pTIButton);
int real_redraw_tibutton(struct GUI *pTIButton);

int draw_tibutton(struct GUI *pButton, Sint16 start_x, Sint16 start_y);
int draw_ibutton(struct GUI *pButton, Sint16 start_x, Sint16 start_y);

/* EDIT */
struct GUI *create_edit(SDL_Surface *pBackground, SDL_Surface *pDest,
			SDL_String16 *pString16, Uint16 lenght,
			Uint32 flags);
enum Edit_Return_Codes edit_field(struct GUI *pEdit_Widget);
int redraw_edit(struct GUI *pEdit_Widget);

int draw_edit(struct GUI *pEdit, Sint16 start_x, Sint16 start_y);

/* VERTICAL */
struct GUI *create_vertical(SDL_Surface *pVert_theme, SDL_Surface *pDest,
  				Uint16 high, Uint32 flags);
int redraw_vert(struct GUI *pVert);
int draw_vert(struct GUI *pVert, Sint16 x, Sint16 y);

/* HORIZONTAL */
struct GUI *create_horizontal(SDL_Surface *pHoriz_theme, SDL_Surface *pDest,
  			Uint16 width, Uint32 flags);
int redraw_horiz(struct GUI *pHoriz);
int draw_horiz(struct GUI *pHoriz, Sint16 x, Sint16 y);

/* WINDOW */
struct GUI *create_window(SDL_Surface *pDest, SDL_String16 *pTitle,
	  Uint16 w, Uint16 h, Uint32 flags);

int resize_window(struct GUI *pWindow, SDL_Surface *pBcgd,
		  SDL_Color *pColor, Uint16 new_w, Uint16 new_h);

int redraw_window(struct GUI *pWindow);
bool move_window(struct GUI *pWindow);

/* misc */
void draw_frame(SDL_Surface *pDest, Sint16 start_x, Sint16 start_y,
		Uint16 w, Uint16 h);
void refresh_widget_background(struct GUI *pWidget);
void draw_menubuttons(struct GUI *pFirstButtonOnList);

/* LABEL */
struct GUI *create_themelabel(SDL_Surface *pBackground, SDL_Surface *pDest,
			      SDL_String16 *pText, Uint16 w, Uint16 h,
			      Uint32 flags);
struct GUI * create_themelabel2(SDL_Surface *pIcon, SDL_Surface *pDest,
  		SDL_String16 *pText, Uint16 w, Uint16 h, Uint32 flags);
struct GUI *create_iconlabel(SDL_Surface *pIcon, SDL_Surface *pDest, 
  	SDL_String16 *pText, Uint32 flags);
struct GUI * convert_iconlabel_to_themeiconlabel2(struct GUI *pIconLabel);
int draw_label(struct GUI *pLabel, Sint16 start_x, Sint16 start_y);

int redraw_label(struct GUI *pLabel);
void remake_label_size(struct GUI *pLabel);

/* CHECKBOX */
struct GUI *create_textcheckbox(SDL_Surface *pDest, bool state,
			  SDL_String16 *pStr, Uint32 flags);
struct GUI *create_checkbox(SDL_Surface *pDest, bool state, Uint32 flags);
void togle_checkbox(struct GUI *pCBox);
bool get_checkbox_state(struct GUI *pCBox);
int redraw_textcheckbox(struct GUI *pCBox);
int set_new_checkbox_theme(struct GUI *pCBox ,
				SDL_Surface *pTrue, SDL_Surface *pFalse);

#define set_wstate(pWidget, state)		\
do {						\
  pWidget->state_types_flags &= ~STATE_MASK;	\
  pWidget->state_types_flags |= state;		\
} while(0)

#define set_wtype(pWidget, type)		\
do {						\
  pWidget->state_types_flags &= ~TYPE_MASK;	\
  pWidget->state_types_flags |= type;		\
} while(0)

#define set_wflag(pWidget, flag)	\
	(pWidget)->state_types_flags |= ((flag) & FLAG_MASK)

#define clear_wflag(pWidget, flag)	\
	(pWidget)->state_types_flags &= ~((flag) & FLAG_MASK)

#define get_wstate(pWidget)				\
	fc__extension((enum WState)(pWidget->state_types_flags & STATE_MASK))

#define get_wtype(pWidget)				\
	fc__extension((enum WTypes)(pWidget->state_types_flags & TYPE_MASK))

#define get_wflags(pWidget)				\
	fc__extension((enum WFlags)(pWidget->state_types_flags & FLAG_MASK))


#define hide_scrollbar(scrollbar)				\
do {								\
  if (scrollbar->pUp_Left_Button) {				\
    set_wflag(scrollbar->pUp_Left_Button, WF_HIDDEN);		\
    set_wflag(scrollbar->pDown_Right_Button, WF_HIDDEN);	\
  }								\
  if (scrollbar->pScrollBar) {					\
    set_wflag(scrollbar->pScrollBar, WF_HIDDEN);		\
  }								\
} while(0)

#define show_scrollbar(scrollbar)				\
do {								\
  if (scrollbar->pUp_Left_Button) {				\
    clear_wflag(scrollbar->pUp_Left_Button, WF_HIDDEN);		\
    clear_wflag(scrollbar->pDown_Right_Button, WF_HIDDEN);	\
  }								\
  if (scrollbar->pScrollBar) {					\
    clear_wflag(scrollbar->pScrollBar, WF_HIDDEN);		\
  }								\
} while(0)

#define FREEWIDGET(pGUI)					\
do {								\
  if ((get_wflags(pGUI) & WF_FREE_STRING) == WF_FREE_STRING) {	\
    FREESTRING16(pGUI->string16);				\
  }								\
  if ((get_wflags(pGUI) & WF_FREE_GFX) == WF_FREE_GFX) {		\
    FREESURFACE(pGUI->gfx);					\
  }								\
  if ((get_wflags(pGUI) & WF_FREE_THEME) == WF_FREE_THEME) {	\
    if (get_wtype(pGUI) == WT_CHECKBOX) {			\
      FREESURFACE(pGUI->private_data.cbox->pTRUE_Theme);		\
      FREESURFACE(pGUI->private_data.cbox->pFALSE_Theme);		\
    } else {							\
      FREESURFACE(pGUI->theme);				\
    }								\
  }								\
  if ((get_wflags(pGUI) & WF_FREE_DATA) == WF_FREE_DATA) {	\
    FREE(pGUI->data.ptr);					\
  }								\
  if ((get_wflags(pGUI) & WF_FREE_PRIVATE_DATA) == WF_FREE_PRIVATE_DATA) { 	\
    FREE(pGUI->private_data.ptr);				\
  }								\
  FREE(pGUI);							\
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

#define del_group(pBeginGroupWidgetList, pEndGroupWidgetList)		\
do {									\
  del_group_of_widgets_from_gui_list(pBeginGroupWidgetList,		\
					   pEndGroupWidgetList);	\
  pBeginGroupWidgetList = NULL;						\
  pEndGroupWidgetList = NULL;						\
} while(0)

#define enable_group(pBeginGroupWidgetList, pEndGroupWidgetList)	\
	set_group_state(pBeginGroupWidgetList, 				\
			pEndGroupWidgetList, FC_WS_NORMAL)

#define disable_group(pBeginGroupWidgetList, pEndGroupWidgetList)	\
	set_group_state(pBeginGroupWidgetList,	pEndGroupWidgetList,	\
			FC_WS_DISABLED)

#define set_action(ID, action_callback)	\
	get_widget_pointer_form_main_list(ID)->action = action_callback

#define set_key(ID, keyb)	\
	get_widget_pointer_form_main_list(ID)->key = keyb

#define set_mod(ID, mod)	\
	get_widget_pointer_form_main_list(ID)->mod = mod

#define enable(ID)						\
do {								\
  struct GUI *____pGUI = get_widget_pointer_form_main_list(ID);	\
  set_wstate(____pGUI, FC_WS_NORMAL);				\
} while(0)

#define disable(ID)						\
do {								\
  struct GUI *____pGUI = get_widget_pointer_form_main_list(ID);	\
  set_wstate(____pGUI , FC_WS_DISABLED);				\
} while(0)

#define show(ID)	\
  clear_wflag( get_widget_pointer_form_main_list(ID), WF_HIDDEN)

#define hide(ID)	\
  set_wflag(get_widget_pointer_form_main_list(ID), WF_HIDDEN)

/* BUTTON */
#define create_icon_button_from_unichar(pIcon, pDest, pUniChar, pUniCharSize, iPtsize, flags) \
	create_icon_button(pIcon, pDest, create_string16(pUniChar, pUniCharSize, iPtsize), flags)

#define create_icon_button_from_chars(pIcon, pDest, pCharString, iPtsize, flags) \
	create_icon_button(pIcon, pDest,                                        \
			   create_str16_from_char(pCharString, iPtsize),  \
			   flags)

#define create_themeicon_button_from_unichar(pIcon_theme, pDest, pUniChar, pUniCharSize, iPtsize, flags) \
	create_themeicon_button(pIcon, pDest, create_string16(pUniChar, pUniCharSize, iPtsize), \
				flags)

#define create_themeicon_button_from_chars(pIcon_theme, pDest, pCharString, iPtsize, flags) \
	create_themeicon_button(pIcon_theme, pDest,                 \
				create_str16_from_char(pCharString, \
						       iPtsize),    \
				flags)

#define redraw_tibutton(pButton)	\
	real_redraw_tibutton(pButton)

#define redraw_ibutton(pButton)	\
	real_redraw_ibutton(pButton)

/* EDIT */
#define create_edit_from_chars(pBackground, pDest, pCharString, iPtsize, length, flags)                                                                 \
	create_edit(pBackground, pDest,                                 \
		    create_str16_from_char(pCharString, iPtsize), \
		    length, flags)

#define create_edit_from_unichars(pBackground, pDest, pUniChar, pUniCharSize, iPtsize, length, flags) \
	create_edit(pBackground, pDest, create_string16(pUniChar, pUniCharSize, iPtsize), length, flags )

#define edit(pEdit) edit_field(pEdit)

#define draw_frame_inside_widget_on_surface(pWidget , pDest)		\
	draw_frame(pDest, pWidget->size.x, pWidget->size.y,		\
				pWidget->size.w, pWidget->size.h)

#define draw_frame_inside_widget(pWidget)				\
	draw_frame_inside_widget_on_surface(pWidget , pWidget->dst)

#define draw_frame_around_widget_on_surface(pWidget , pDest)		\
	draw_frame(pDest, pWidget->size.x - FRAME_WH,			\
				pWidget->size.y - FRAME_WH,		\
				pWidget->size.w + DOUBLE_FRAME_WH,	\
				pWidget->size.h + DOUBLE_FRAME_WH)

#define draw_frame_around_widget(pWidget)				\
	draw_frame_around_widget_on_surface(pWidget , pWidget->dst)

/* ICON */
#define redraw_icon(pIcon)	\
	real_redraw_icon(pIcon)

#define redraw_icon2(pIcon)	\
	real_redraw_icon2(pIcon)

/* LABEL */
#define create_iconlabel_from_chars(pIcon, pDest, pCharString, iPtsize, flags) \
	create_iconlabel(pIcon, pDest, create_str16_from_char(pCharString, iPtsize), flags)

#endif	/* FC__GUI_STUFF_H */
