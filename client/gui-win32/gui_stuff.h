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
#ifndef FC__GUISTUFF_H
#define FC__GUISTUFF_H
void mydrawrect(HDC hdc, int x, int y,int w,int h);
char *convertnl2crnl(char *str);
void my_get_win_border(HWND hWnd,int *w,int *h);
int fcwin_listview_add_row(HWND lv,int row_nr,int columns,char **row);
struct fcwin_box;

typedef void (*t_fcminsize)(POINT *,void *);
typedef void (*t_fcsetsize)(RECT *,void *);
typedef void (*t_fcdelwidget)(void *);

/* has to be called once */
void init_layoutwindow();
/* handle windows with layout management */
HWND fcwin_create_layouted_window(WNDPROC user_wndproc,
				  LPCTSTR lpWindowName,
				  DWORD dwStyle, 
				  int x,
				  int y, 
				  HWND hWndParent,
				  HMENU hMenu,
				  void *user_data);
void * fcwin_get_user_data(HWND hWnd);
void fcwin_set_user_data(HWND hWnd,void *data);
void fcwin_redo_layout(HWND hWnd);
void fcwin_set_box(HWND hWnd,struct fcwin_box *fcb);


/* this works like gtkboxes */

struct fcwin_box *fcwin_hbox_new(HWND owner,int same_size);
struct fcwin_box *fcwin_vbox_new(HWND owner,int same_size);
void fcwin_box_free(struct fcwin_box *box);
void fcwin_box_freeitem(struct fcwin_box *box, int n);
void fcwin_box_add_win(struct fcwin_box *box,HWND win,
		       int expand, int fill, int padding);
void fcwin_box_add_win_default(struct fcwin_box *box,HWND win);
void fcwin_box_add_generic(struct fcwin_box *box,
			t_fcminsize minsize,
			t_fcsetsize setsize,
			t_fcdelwidget del,
			void *data,
			int expand,
			int fill,
			int padding);
HWND fcwin_box_add_static_default(struct fcwin_box *box,char *txt,
				  int id, int style);
HWND fcwin_box_add_button_default(struct fcwin_box *box,char *txt,
				  int id, int style);
HWND fcwin_box_add_static(struct fcwin_box *box,char *txt,int id, int style,
			  int expand, int fill, int padding);
HWND fcwin_box_add_button(struct fcwin_box *box,char *txt,int id, int style,
			  int expand, int fill, int padding);
HWND fcwin_box_add_checkbox(struct fcwin_box *box, char *txt,int id,int style,
			    int expand, int fill, int padding);
HWND fcwin_box_add_edit(struct fcwin_box *box, char *txt, int maxchars,
			int id,int style,
			int expand, int fill, int padding);
HWND fcwin_box_add_list(struct fcwin_box *box,
			int rows,
			int id,
			int style,
			int expand, int fill, int padding);
HWND fcwin_box_add_listview(struct fcwin_box *box,
			    int rows,
			    int id,
			    int style,
			    int expand, int fill, int padding);
HWND fcwin_box_add_tab(struct fcwin_box *box,
		       WNDPROC *wndprocs,
		       HWND *wnds,
		       char **titles,
		       void **user_data, int n,
		       int id,int style,
		       int expand, int fill, int padding);
HWND fcwin_box_add_combo(struct fcwin_box *box,
			 int rows,
			 int id,
			 int style,
			 int expand, int fill, int padding);
HWND fcwin_box_add_groupbox(struct fcwin_box *box, char *txt,
			    struct fcwin_box *box_add, int style,
			    bool expand, bool fill,
			    int padding);
HWND fcwin_box_add_radiobutton(struct fcwin_box *box,char *txt, int id,
			       int style,int expand,int fill, int padding);
void fcwin_box_add_box(struct fcwin_box *box, struct fcwin_box *box_to_add,
		       int expand, int fill, int padding);
void fcwin_box_calc_sizes(struct fcwin_box *box, POINT *minsize);
void fcwin_box_do_layout(struct fcwin_box *box, LPRECT size);
void fcwin_box_calc_layout(struct fcwin_box *box, int x, int y);

#endif
