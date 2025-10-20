/***********************************************************************
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
                          widget.h  -  description
                             -------------------
    begin                : June 30 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
**********************************************************************/

#ifndef FC__WIDGET_H
#define FC__WIDGET_H

/* utility */
#include "fc_types.h"

/* gui-sdl2 */
#include "gui_main.h"
#include "gui_string.h"

#ifdef GUI_SDL2_SMALL_SCREEN
#define WINDOW_TITLE_HEIGHT 10
#else
#define WINDOW_TITLE_HEIGHT 20
#endif

#define MAX_ID              0xFFFFFFFF

/* Widget Types */
enum widget_type {		/* allow 64 widgets type */
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
  WT_T_LABEL	= 36,		/* text label with theme background */
  WT_I_LABEL	= 40,		/* text label with icon */
  WT_TI_LABEL	= 44,		/* text label with icon and theme background.
				   NOTE: Not DEFINED- don't use
				   ( can't be transp. ) */
  WT_CHECKBOX	= 48,		/* checkbox. */
  WT_TCHECKBOX	= 52,		/* text label with checkbox. */
  WT_ICON2	= 56,		/* flat Button from 1 - state icon */
  WT_T2_LABEL   = 60,
  WT_COMBO      = 64
};

/* Widget FLAGS -> allowed 20 flags */
/* default: ICON_CENTER_Y, ICON_ON_LEFT */
enum widget_flag {
  WF_HIDDEN				= (1<<10),
  /* widget->gfx may be freed together with the widget */
  WF_FREE_GFX	 			= (1<<11),
  /* widget->theme may be freed  together with the widget*/
  WF_FREE_THEME				= (1<<12),
  /* widget->theme2 may be freed  together with the widget*/
  WF_FREE_THEME2                        = (1<<13),
  /* widget->string may be freed  together with the widget*/
  WF_FREE_STRING			= (1<<14),
  /* widget->data may be freed  together with the widget*/
  WF_FREE_DATA			 	= (1<<15),
  /* widget->private_data may be freed  together with the widget*/
  WF_FREE_PRIVATE_DATA			= (1<<16),
  WF_ICON_ABOVE_TEXT			= (1<<17),
  WF_ICON_UNDER_TEXT			= (1<<18),
  WF_ICON_CENTER			= (1<<19),
  WF_ICON_CENTER_RIGHT			= (1<<20),
  WF_RESTORE_BACKGROUND	                = (1<<21),
  WF_DRAW_FRAME_AROUND_WIDGET	 	= (1<<22),
  WF_DRAW_TEXT_LABEL_WITH_SPACE		= (1<<23),
  WF_WIDGET_HAS_INFO_LABEL		= (1<<24),
  WF_SELECT_WITHOUT_BAR	                = (1<<25),
  WF_PASSWD_EDIT			= (1<<26),
  WF_EDIT_LOOP				= (1<<27)
};

/* Widget states */
enum widget_state {
  FC_WS_NORMAL          = 0,
  FC_WS_SELECTED        = 1,
  FC_WS_PRESSED         = 2,
  FC_WS_DISABLED        = 3
};

struct container {
  int id0;
  int id1;
  int value;
};

struct small_dialog;
struct advanced_dialog;
struct checkbox;

struct widget {
  struct widget *next;
  struct widget *prev;

  struct gui_layer *dst;      /* destination buffer layer */

  SDL_Surface *theme;
  SDL_Surface *theme2;        /* Icon or theme2 */
  SDL_Surface *gfx;           /* saved background */
  utf8_str *string_utf8;
  utf8_str *info_label;   /* optional info label. */

  /* data/information/transport pointers */
  union {
    const struct strvec *vector;
    struct container *cont;
    struct city *city;
    struct unit *unit;
    struct player *player;
    struct tile *tile;
    struct widget *widget;
    void *ptr;
  } data;

  union {
    struct checkbox *cbox;
    struct small_dialog *small_dlg;
    struct advanced_dialog *adv_dlg;
    void *ptr;
  } private_data;

  Uint32 state_types_flags;	/* "packed" widget info */

  SDL_Rect size;		/* size.w and size.h are the size of widget
                                   size.x and size.y are the draw positions
                                   (relative to dst) */

  SDL_Rect area;                /* position (relative to dst) and size of the
                                   area the widget resides in (or, for
                                   WT_WINDOW, the client area inside it) */

  SDL_Keycode key;              /* key aliased with this widget */
  Uint16 mod;			/* SHIFT, CTRL, ALT, etc */
  widget_id id;			/* id in widget list */

  int (*action) (struct widget *);	/* default callback action */

  void (*set_area) (struct widget *pwidget, SDL_Rect area);
  void (*set_position) (struct widget *pwidget, int x, int y);
  void (*resize) (struct widget *pwidget, int w, int h);
  void (*draw_frame) (struct widget *pwidget);
  int (*redraw) (struct widget *pwidget);
  void (*mark_dirty) (struct widget *pwidget);
  void (*flush) (struct widget *pwidget);
  void (*undraw) (struct widget *pwidget);
  void (*select) (struct widget *pwidget);
  void (*unselect) (struct widget *pwidget);
  void (*press) (struct widget *pwidget);
  void (*destroy) (struct widget *pwidget);
};

/* Struct of basic window group dialog ( without scrollbar ) */
struct small_dialog {
  struct widget *begin_widget_list;
  struct widget *end_widget_list;    /* window */
};

/* Struct of advanced window group dialog ( with scrollbar ) */
struct advanced_dialog {
  struct widget *begin_widget_list;
  struct widget *end_widget_list; /* window */

  struct widget *begin_active_widget_list;
  struct widget *end_active_widget_list;
  struct widget *active_widget_list; /* first seen widget */
  struct scroll_bar *scroll;
};

enum scan_direction {
  SCAN_FORWARD,
  SCAN_BACKWARD
};

void add_to_gui_list(widget_id id, struct widget *gui);
void del_widget_pointer_from_gui_list(struct widget *gui);
void widget_add_as_prev(struct widget *new_widget, struct widget *add_dock);

bool is_this_widget_first_on_list(struct widget *gui);
void move_widget_to_front_of_gui_list(struct widget *gui);

void del_gui_list(struct widget *gui_list);
void del_main_list(void);

struct widget *find_next_widget_at_pos(struct widget *start_widget, int x, int y);
struct widget *find_next_widget_for_key(struct widget *start_widget, SDL_Keysym key);

struct widget *get_widget_pointer_from_id(const struct widget *gui_list,
                                          Uint16 id,
                                          enum scan_direction direction);

struct widget *get_widget_pointer_from_main_list(Uint16 id);

#define set_action(id, action_callback)	\
	get_widget_pointer_from_main_list(id)->action = action_callback

#define set_key(id, keyb)	\
	get_widget_pointer_from_main_list(id)->key = keyb

#define set_mod(id, mod)	\
	get_widget_pointer_from_main_list(id)->mod = mod

#define enable(id)						\
do {								\
  struct widget *____gui = get_widget_pointer_from_main_list(id);	\
  set_wstate(____gui, FC_WS_NORMAL);				\
} while (FALSE)

#define disable(id)						\
do {								\
  struct widget *____gui = get_widget_pointer_from_main_list(id);	\
  set_wstate(____gui , FC_WS_DISABLED);				\
} while (FALSE)

#define show(id)	\
  clear_wflag( get_widget_pointer_from_main_list(id), WF_HIDDEN)

#define hide(id)	\
  set_wflag(get_widget_pointer_from_main_list(id), WF_HIDDEN)

void widget_selected_action(struct widget *pwidget);
Uint16 widget_pressed_action(struct widget *pwidget);

void unselect_widget_action(void);

#define draw_widget_info_label() redraw_widget_info_label(NULL);
void redraw_widget_info_label(SDL_Rect *area);

/* Widget */
void set_wstate(struct widget *pwidget, enum widget_state state);
enum widget_state get_wstate(const struct widget *pwidget);

void set_wtype(struct widget *pwidget, enum widget_type type);
enum widget_type get_wtype(const struct widget *pwidget);

void set_wflag(struct widget *pwidget, enum widget_flag flag);
void clear_wflag(struct widget *pwidget, enum widget_flag flag);

enum widget_flag get_wflags(const struct widget *pwidget);

static inline void widget_set_area(struct widget *pwidget, SDL_Rect area)
{
  pwidget->set_area(pwidget, area);
}

static inline void widget_set_position(struct widget *pwidget, int x, int y)
{
  pwidget->set_position(pwidget, x, y);
}

static inline void widget_resize(struct widget *pwidget, int w, int h)
{
  pwidget->resize(pwidget, w, h);
}

static inline int widget_redraw(struct widget *pwidget)
{
  return pwidget->redraw(pwidget);
}

static inline void widget_draw_frame(struct widget *pwidget)
{
  pwidget->draw_frame(pwidget);
}

static inline void widget_mark_dirty(struct widget *pwidget)
{
  pwidget->mark_dirty(pwidget);
}

static inline void widget_flush(struct widget *pwidget)
{
  pwidget->flush(pwidget);
}

static inline void widget_undraw(struct widget *pwidget)
{
  pwidget->undraw(pwidget);
}

void widget_free(struct widget **gui);

void refresh_widget_background(struct widget *pwidget);

#define FREEWIDGET(pwidget)					\
do {								\
  widget_free(&pwidget);                                        \
} while (FALSE)

#define draw_frame_inside_widget_on_surface(pwidget , pdest)		\
do {                                                                    \
  draw_frame(pdest, pwidget->size.x, pwidget->size.y, pwidget->size.w, pwidget->size.h);  \
} while (FALSE);

#define draw_frame_inside_widget(pwidget)				\
	draw_frame_inside_widget_on_surface(pwidget , pwidget->dst->surface)

#define draw_frame_around_widget_on_surface(pwidget , pdest)		\
do {                                                                    \
  draw_frame(pdest, pwidget->size.x - ptheme->FR_Left->w, pwidget->size.y - ptheme->FR_Top->h, \
             pwidget->size.w + ptheme->FR_Left->w + ptheme->FR_Right->w,\
             pwidget->size.h + ptheme->FR_Top->h + ptheme->FR_Bottom->h);  \
} while (FALSE);

#define draw_frame_around_widget(pwidget)				\
	draw_frame_around_widget_on_surface(pwidget, pwidget->dst->surface)

/* Group */
Uint16 redraw_group(const struct widget *begin_group_widget_list,
                    const struct widget *end_group_widget_list,
                    int add_to_update);

void undraw_group(struct widget *begin_group_widget_list,
                  struct widget *end_group_widget_list);

void set_new_group_start_pos(const struct widget *begin_group_widget_list,
                             const struct widget *end_group_widget_list,
                             Sint16 Xrel, Sint16 Yrel);
void del_group_of_widgets_from_gui_list(struct widget *begin_group_widget_list,
                                        struct widget *end_group_widget_list);
void move_group_to_front_of_gui_list(struct widget *begin_group_widget_list,
                                     struct widget *end_group_widget_list);

void set_group_state(struct widget *begin_group_widget_list,
                     struct widget *end_group_widget_list, enum widget_state state);

void show_group(struct widget *begin_group_widget_list,
                struct widget *end_group_widget_list);

void hide_group(struct widget *begin_group_widget_list,
                struct widget *end_group_widget_list);

void group_set_area(struct widget *begin_group_widget_list,
                    struct widget *end_group_widget_list,
                    SDL_Rect area);

/* Window Group */
void popdown_window_group_dialog(struct widget *begin_group_widget_list,
                                 struct widget *end_group_widget_list);

bool select_window_group_dialog(struct widget *begin_widget_list,
                                struct widget *pwindow);
bool move_window_group_dialog(struct widget *begin_group_widget_list,
                              struct widget *end_group_widget_list);
void move_window_group(struct widget *begin_widget_list, struct widget *pwindow);

int setup_vertical_widgets_position(int step,
                                    Sint16 start_x, Sint16 start_y,
                                    Uint16 w, Uint16 h,
                                    struct widget *begin, struct widget *end);

#define del_widget_from_gui_list(__gui)	\
do {						\
  del_widget_pointer_from_gui_list(__gui);	\
  FREEWIDGET(__gui);				\
} while (FALSE)

#define del_ID_from_gui_list(id)				\
do {								\
  struct widget *___ptmp = get_widget_pointer_from_main_list(id);	\
  del_widget_pointer_from_gui_list(___ptmp);			\
  FREEWIDGET(___ptmp);						\
} while (FALSE)

#define move_ID_to_front_of_gui_list(id)	\
	move_widget_to_front_of_gui_list(       \
          get_widget_pointer_from_main_list(id))

#define del_group(begin_group_widget_list, end_group_widget_list)		\
do {									\
  del_group_of_widgets_from_gui_list(begin_group_widget_list,		\
                                     end_group_widget_list);	\
  begin_group_widget_list = NULL;						\
  end_group_widget_list = NULL;						\
} while (FALSE)

#define enable_group(begin_group_widget_list, end_group_widget_list)    \
        set_group_state(begin_group_widget_list,                        \
                        end_group_widget_list, FC_WS_NORMAL)

#define disable_group(begin_group_widget_list, end_group_widget_list)   \
        set_group_state(begin_group_widget_list, end_group_widget_list, \
                        FC_WS_DISABLED)

/* Advanced Dialog */
bool add_widget_to_vertical_scroll_widget_list(struct advanced_dialog *dlg,
                                               struct widget *new_widget,
                                               struct widget *add_dock, bool dir,
                                               Sint16 start_x, Sint16 start_y)
  fc__attribute((nonnull (2)));

bool del_widget_from_vertical_scroll_widget_list(struct advanced_dialog *dlg,
                                                 struct widget *pwidget)
  fc__attribute((nonnull (2)));

/* Misc */
SDL_Surface *create_bcgnd_surf(SDL_Surface *ptheme, enum widget_state state,
                               Uint16 width, Uint16 height);
void draw_frame(SDL_Surface *pdest, Sint16 start_x, Sint16 start_y,
                Uint16 w, Uint16 h);

#include "widget_button.h"
#include "widget_checkbox.h"
#include "widget_combo.h"
#include "widget_edit.h"
#include "widget_icon.h"
#include "widget_label.h"
#include "widget_scrollbar.h"
#include "widget_window.h"

#endif /* FC__WIDGET_H */
