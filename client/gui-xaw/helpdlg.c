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
#include <assert.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/SimpleMenu.h> 
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/Tree.h>

#include "city.h"
#include "game.h"
#include "genlist.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "tech.h"
#include "unit.h"
#include "version.h"

#include "climisc.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_stuff.h"

#include "helpdlg.h"

extern int UNIT_TILES;
extern Widget toplevel, main_form;
extern Display *display;
extern Atom wm_delete_window;

Widget help_dialog_shell;
Widget help_form;
Widget help_viewport;
Widget help_list;
Widget help_text;
Widget help_close_command;

Widget help_right_form;
Widget help_title;


Widget help_improvement_cost, help_improvement_cost_data;
Widget help_improvement_upkeep, help_improvement_upkeep_data;
Widget help_improvement_req, help_improvement_req_data;
Widget help_improvement_variant, help_improvement_variant_data;
Widget help_wonder_obsolete, help_wonder_obsolete_data;
Widget help_wonder_variant, help_wonder_variant_data;

Widget help_tech_tree;
Widget help_tree_viewport;

Widget help_unit_attack, help_unit_attack_data;
Widget help_unit_def, help_unit_def_data;
Widget help_unit_move, help_unit_move_data;
Widget help_unit_hp, help_unit_hp_data;
Widget help_unit_fp, help_unit_fp_data;
Widget help_unit_cost, help_unit_cost_data;
Widget help_unit_visrange, help_unit_visrange_data;
Widget help_unit_tile;

char *help_type_names[] = { "", "TEXT", "UNIT", "IMPROVEMENT",
			    "WONDER", "TECH", 0 };
#define MAX_LAST (MAX(MAX(A_LAST,B_LAST),U_LAST))

void create_help_page(enum help_page_type type);

void update_help_dialog(Widget help_list);

void help_close_command_callback(Widget w, XtPointer client_data, 
				 XtPointer call_data);
void help_list_callback(Widget w, XtPointer client_data, 
			         XtPointer call_data);
void help_tree_node_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data);

void create_help_dialog(void);
void select_help_item(int item);
void select_help_item_string(char *item, enum help_page_type htype);

struct help_item {
  char *topic, *text;
  enum help_page_type type;
};
static struct genlist help_nodes;

#define help_list_iterate(helplist, pitem) { \
  struct genlist_iterator myiter; \
  struct help_item *pitem; \
  for( genlist_iterator_init(&myiter, &helplist, 0); \
       (pitem=ITERATOR_PTR(myiter)); \
       ITERATOR_NEXT(myiter) ) {
#define help_list_iterate_end }}

char *topic_list[1024];
char long_buffer[64000];

#define TREE_NODE_UNKNOWN_TECH_BG "red"
#define TREE_NODE_KNOWN_TECH_BG "green"
#define TREE_NODE_REACHABLE_TECH_BG "yellow"
#define TREE_NODE_REMOVED_TECH_BG "magenta"


static struct help_item *find_help_item_position(int pos)
{
  return genlist_get(&help_nodes, pos);
}


static void set_title_topic(struct help_item *pitem)
{
  if(!strcmp(pitem->topic, "Freeciv") || 
     !strcmp(pitem->topic, "About"))
    xaw_set_label(help_title, FREECIV_NAME_VERSION);
  else
  xaw_set_label(help_title, pitem->topic);
}


static struct help_item *new_help_item(int type)
{
  struct help_item *pitem;
  
  pitem = fc_malloc(sizeof(struct help_item));
  pitem->topic = NULL;
  pitem->text = NULL;
  pitem->type = type;
  return pitem;
}

/* for genlist_sort(); sort by topic via strcmp */
static int help_item_compar(const void *a, const void *b)
{
  const struct help_item *ha, *hb;
  ha = (const struct help_item*) *(const void**)a;
  hb = (const struct help_item*) *(const void**)b;
  return strcmp(ha->topic, hb->topic);
}

void boot_help_texts(void)
{
  static int booted=0;
  
  FILE *fs;
  char *dfname;
  char buf[512], *p;
  char expect[32], name[MAX_LENGTH_NAME+2];
  char seen[MAX_LAST], *pname;
  int len;
  struct help_item *pitem = NULL;
  enum help_page_type current_type = HELP_TEXT;
  struct genlist category_nodes;
  int i, filter_this;

  if(help_dialog_shell) {
    /* need to do something like this or bad things happen */
    XtDestroyWidget(help_dialog_shell);
    help_dialog_shell=0;
  }
  
  if(!booted) {
    freelog(LOG_VERBOSE, "Booting help texts");
    genlist_init(&help_nodes);
  } else {
    /* free memory allocated last time booted */
    help_list_iterate(help_nodes, ptmp) {
      free(ptmp->topic);
      free(ptmp->text);
      free(ptmp);
    }
    help_list_iterate_end;
    genlist_unlink_all(&help_nodes);
    freelog(LOG_VERBOSE, "Rebooting help texts");
  }    
  
  dfname = datafilename("helpdata.txt");
  if (dfname == NULL) {
    freelog(LOG_NORMAL, "Could not find readable helpdata.txt in data path");
    freelog(LOG_NORMAL, "The data path may be set via"
	                " the environment variable FREECIV_PATH");
    freelog(LOG_NORMAL, "Current data path is: \"%s\"", datafilename(NULL));
    freelog(LOG_NORMAL, "Did not read help texts");
    return;
  }
  fs = fopen(dfname, "r");
  if (fs == NULL) {
    /* this is now unlikely to happen */
    freelog(LOG_NORMAL, "failed reading help-texts");
    return;
  }

  while(1) {
    fgets(buf, 512, fs);
    buf[strlen(buf)-1]='\0';
    if(!strncmp(buf, "%%", 2))
      continue;
    len=strlen(buf);

    if (len>0 && buf[0] == '@') {
      if(current_type==HELP_TEXT) {
	current_type = -1;
	for(i=2; help_type_names[i]; i++) {
	  sprintf(expect, "START_%sS", help_type_names[i]);
	  if(strcmp(expect,buf+1)==0) {
	    current_type = i;
	    break;
	  }
	}
	if (current_type==-1) {
	  freelog(LOG_NORMAL, "bad help category \"%s\"", buf+1);
	  current_type = HELP_TEXT;
	} else {
	  genlist_init(&category_nodes);
	  for(i=0; i<MAX_LAST; i++) {
	    seen[i] = (booted?0:1); /* on initial boot data tables are empty */
	  }
	  freelog(LOG_DEBUG, "Help category %s",
		  help_type_names[current_type]);
	}
      } else {
	sprintf(expect, "END_%sS", help_type_names[current_type]);
	if(strcmp(expect,buf+1)!=0) {
	  freelog(LOG_FATAL, "bad end to help category \"%s\"", buf+1);
	  exit(1);
	}
	/* add defaults for those not seen: */
	if(current_type==HELP_UNIT) {
	  for(i=0; i<U_LAST; i++) {
	    if(!seen[i] && unit_type_exists(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", unit_name(i));
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_TECH) {
	  for(i=1; i<A_LAST; i++) {                 /* skip A_NONE */
	    if(!seen[i] && tech_exists(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", advances[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_IMPROVEMENT) {
	  for(i=0; i<B_LAST; i++) {
	    if(!seen[i] && improvement_exists(i) && !is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", improvement_types[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else if(current_type==HELP_WONDER) {
	  for(i=0; i<B_LAST; i++) {
	    if(!seen[i] && improvement_exists(i) && is_wonder(i)) {
	      pitem = new_help_item(current_type);
	      sprintf(name, " %s", improvement_types[i].name);
	      pitem->topic = mystrdup(name);
	      pitem->text = mystrdup("");
	      genlist_insert(&category_nodes, pitem, -1);
	    }
	  }
	} else {
	  freelog(LOG_FATAL, "Bad current_type %d", current_type);
	  exit(1);
	}
	genlist_sort(&category_nodes, help_item_compar);
	help_list_iterate(category_nodes, ptmp) {
	  genlist_insert(&help_nodes, ptmp, -1);
	}
	help_list_iterate_end;
	genlist_unlink_all(&category_nodes);
	current_type = HELP_TEXT;
      }
      continue;
    }
    
    p=strchr(buf, '#');
    if(!p) {
      break;
    }
    pname = p;
    while(*(++pname)==' ')
      ;

    /* i==-1 is text; filter_this==1 is to be left out, but we have to
     * read in the help text so we're at the right place
     */
    i = -1;
    filter_this = 0;
    switch(current_type) {
    case HELP_UNIT:
      i = find_unit_type_by_name(pname);
      if(!unit_type_exists(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering unit type %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_TECH:
      i = find_tech_by_name(pname);
      if(!tech_exists(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering tech %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_IMPROVEMENT:
      i = find_improvement_by_name(pname);
      if(!improvement_exists(i) || is_wonder(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering city improvement %s from help", pname);
	filter_this = 1;
      }
      break;
    case HELP_WONDER:
      i = find_improvement_by_name(pname);
      if(!improvement_exists(i) || !is_wonder(i)) {
	if(booted)
	  freelog(LOG_VERBOSE, "Filtering wonder %s from help", pname);
	filter_this = 1;
      }
      break;
    default:
      /* nothing */
      {} /* placate Solaris cc/xmkmf/makedepend */
    }
    if(i>=0) {
      seen[i] = 1;
    }

    if(!filter_this) {
      pitem = new_help_item(current_type);
      pitem->topic = mystrdup(p+1);
    }

    long_buffer[0]='\0';
    while(1) {
      fgets(buf, 512, fs);
      buf[strlen(buf)-1]='\0';
      if(!strncmp(buf, "%%", 2))
	continue;
      if(!strcmp(buf, "---"))
	break;
      if(!filter_this) {
	strcat(long_buffer, buf);
	strcat(long_buffer, "\n");
      }
    } 

    if(!filter_this) {
      pitem->text=mystrdup(long_buffer);
      if(current_type == HELP_TEXT) {
	genlist_insert(&help_nodes, pitem, -1);
      } else {
	genlist_insert(&category_nodes, pitem, -1);
      }
    }
  }

  if(current_type != HELP_TEXT) {
    freelog(LOG_FATAL, "Didn't finish help category %s",
	 help_type_names[current_type]);
    exit(1);
  }
  
  fclose(fs);
  booted = 1;
}

/****************************************************************
...
*****************************************************************/
void popup_help_dialog_typed(char *item, enum help_page_type htype)
{
  Position x, y;
  Dimension width, height;

  if(!help_dialog_shell) {
    create_help_dialog();
    XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
    XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
		      &x, &y);
    XtVaSetValues(help_dialog_shell, XtNx, x, XtNy, y, NULL);
  }

  XtPopup(help_dialog_shell, XtGrabNone);

  select_help_item_string(item, htype);
}


/****************************************************************
...
*****************************************************************/
void popup_help_dialog_string(char *item)
{
  popup_help_dialog_typed(item, HELP_ANY);
}



/**************************************************************************
...
**************************************************************************/
void create_help_dialog(void)
{
  int i=0;

  help_list_iterate(help_nodes, pitem) 
    topic_list[i++]=pitem->topic;
  help_list_iterate_end;
  topic_list[i]=0;

  
  help_dialog_shell = XtCreatePopupShell("helpdialog", 
					 topLevelShellWidgetClass,
					 toplevel, NULL, 0);

  help_form = XtVaCreateManagedWidget("helpform", 
				      formWidgetClass, 
				      help_dialog_shell, NULL);
  

  help_viewport = XtVaCreateManagedWidget("helpviewport", 
					  viewportWidgetClass, 
					  help_form, 
					  NULL);
  
  help_list = XtVaCreateManagedWidget("helplist", 
				      listWidgetClass, 
				      help_viewport, 
				      XtNlist, 
				      (XtArgVal)topic_list,
				      NULL);

  
  help_right_form=XtVaCreateManagedWidget("helprightform", 
					  formWidgetClass, 
					  help_form, NULL);
  

  help_title=XtVaCreateManagedWidget("helptitle", 
				     labelWidgetClass, 
				     help_right_form,
				     NULL);
  
    
  
  help_close_command = XtVaCreateManagedWidget("helpclosecommand", 
					       commandWidgetClass,
					       help_form,
					       NULL);

  XtAddCallback(help_close_command, XtNcallback, 
		help_close_command_callback, NULL);

  XtAddCallback(help_list, XtNcallback, 
		help_list_callback, NULL);

  XtRealizeWidget(help_dialog_shell);

  XSetWMProtocols(display, XtWindow(help_dialog_shell), 
		  &wm_delete_window, 1);
  XtOverrideTranslations(help_dialog_shell,
	 XtParseTranslationTable("<Message>WM_PROTOCOLS: close-helpdialog()"));

  /* create_help_page(HELP_IMPROVEMENT); */
  create_help_page(HELP_TEXT);
}


static void create_tech_tree(Widget tree, Widget parent, int tech, int levels)
{
  Widget l;
  int type;
  char *bg="";
  char label[MAX_LENGTH_NAME+3];
  
  type = (tech==A_LAST) ? TECH_UNKNOWN : get_invention(game.player_ptr, tech);
  switch(type) {
    case TECH_UNKNOWN:
      bg=TREE_NODE_UNKNOWN_TECH_BG;
      break;
    case TECH_KNOWN:
      bg=TREE_NODE_KNOWN_TECH_BG;
      break;
    case TECH_REACHABLE:
      bg=TREE_NODE_REACHABLE_TECH_BG;
      break;
  }
  
  if(tech==A_LAST ||
     (advances[tech].req[0]==A_LAST && advances[tech].req[1]==A_LAST))  {
    strcpy(label,"Removed");
    bg=TREE_NODE_REMOVED_TECH_BG;
    l=XtVaCreateManagedWidget("treenode", commandWidgetClass, 
			      tree,
			      XtNlabel, label,
			      XtNbackground, bg, NULL);
    XtVaSetValues(l, XtVaTypedArg, XtNbackground, 
		  XtRString, bg, strlen(bg)+1, NULL);
     return;
  }
  
  sprintf(label,"%s:%d",advances[tech].name,
                        tech_goal_turns(game.player_ptr,tech));
  

  if(parent) {
    l=XtVaCreateManagedWidget("treenode", 
			      commandWidgetClass, 
			      tree,
			      XtNlabel, label,
			      XtNtreeParent, parent,
			      NULL);
  }
  else {
    l=XtVaCreateManagedWidget("treenode", 
			      commandWidgetClass, 
			      tree,
			      XtNlabel, label,
			      NULL);
  }

  XtAddCallback(l, XtNcallback, help_tree_node_callback, (XtPointer)tech);

  XtVaSetValues(l, XtVaTypedArg, XtNbackground, 
		XtRString, bg, strlen(bg)+1, NULL);

  
  if(--levels>0) {
    if(advances[tech].req[0]!=A_NONE)
      create_tech_tree(tree, l, advances[tech].req[0], levels);
    if(advances[tech].req[1]!=A_NONE)
      create_tech_tree(tree, l, advances[tech].req[1], levels);
  }
  
  
}


void create_help_page(enum help_page_type type)
{
  Dimension w, h, w2, h2, ay, ah;
 
  XtVaGetValues(help_right_form, XtNwidth, &w, NULL);
  XtVaGetValues(help_viewport, XtNheight, &h, NULL);
  XtVaGetValues(help_title, XtNwidth, &w2, XtNheight, &h2, NULL);
 
  XtDestroyWidget(help_right_form);

  help_right_form=XtVaCreateManagedWidget("helprightform", 
					  formWidgetClass, 
					  help_form, 
					  XtNwidth, w,
					  XtNheight, h,
					  NULL);
  

  help_title=XtVaCreateManagedWidget("helptitle", 
				     labelWidgetClass, 
				     help_right_form,
				     XtNwidth, w2,
				     NULL);

  help_tree_viewport=0;
  
  if(type==HELP_TEXT || type==HELP_ANY) {
    help_text = XtVaCreateManagedWidget("helptext", 
					asciiTextWidgetClass, 
					help_right_form,
					XtNeditType, XawtextRead,
					XtNscrollVertical, XawtextScrollAlways, 
					XtNwidth, w2,
					XtNheight, h-h2-15,
					XtNbottom, XawChainBottom,
					NULL);
  }
  else if(type==HELP_IMPROVEMENT || type==HELP_WONDER) {
    
    help_text = XtVaCreateManagedWidget("helptext", 
					asciiTextWidgetClass, 
					help_right_form,
					XtNeditType, XawtextRead,
					XtNscrollVertical, XawtextScrollAlways, 
					XtNwidth, w2,
					XtNheight, 70,
					NULL);

    
    
    help_improvement_cost=XtVaCreateManagedWidget("helpimprcost", 
						  labelWidgetClass, 
						  help_right_form,
						  NULL);
    help_improvement_cost_data=XtVaCreateManagedWidget("helpimprcostdata", 
						  labelWidgetClass, 
						  help_right_form,
						  NULL);
    
    help_improvement_req=XtVaCreateManagedWidget("helpimprreq", 
						  labelWidgetClass, 
						  help_right_form,
						  NULL);
    help_improvement_req_data=XtVaCreateManagedWidget("helpimprreqdata", 
						  labelWidgetClass, 
						  help_right_form,
						  NULL);
    if(type==HELP_IMPROVEMENT) {
      help_improvement_upkeep=XtVaCreateManagedWidget("helpimprupkeep", 
						      labelWidgetClass, 
						      help_right_form,
						      NULL);
      help_improvement_upkeep_data=XtVaCreateManagedWidget("helpimprupkeepdata", 
							   labelWidgetClass, 
							   help_right_form,
							   NULL);
      help_improvement_variant=XtVaCreateManagedWidget("helpimprvariant", 
						       labelWidgetClass, 
						       help_right_form,
						       NULL);
      help_improvement_variant_data=
	XtVaCreateManagedWidget("helpimprvariantdata",
				labelWidgetClass, 
				help_right_form,
				NULL);
    }
    else {
      help_wonder_obsolete=XtVaCreateManagedWidget("helpwonderobsolete", 
						   labelWidgetClass, 
						   help_right_form,
						   NULL);
      help_wonder_obsolete_data=XtVaCreateManagedWidget("helpwonderobsoletedata", 
						   labelWidgetClass, 
						   help_right_form,
						   NULL);
      help_wonder_variant=XtVaCreateManagedWidget("helpwondervariant", 
						  labelWidgetClass, 
						  help_right_form,
						  NULL);
      help_wonder_variant_data=XtVaCreateManagedWidget("helpwondervariantdata",
						       labelWidgetClass, 
						       help_right_form,
						       NULL);
    }

    XtVaGetValues(help_improvement_req, XtNy, &ay, XtNheight, &ah, NULL);
    help_tree_viewport=XtVaCreateManagedWidget("helptreeviewport", 
					       viewportWidgetClass, 
					       help_right_form,
					       XtNwidth, w2,
					       XtNheight, MAX(1,h-(ay+ah)-10),
					       NULL);
    help_tech_tree=XtVaCreateManagedWidget("helptree", 
					   treeWidgetClass, 
					   help_tree_viewport,
					   NULL);
    
    XawTreeForceLayout(help_tech_tree);  
    
  }
  else if(type==HELP_UNIT) {
    help_text = XtVaCreateManagedWidget("helptext", 
					asciiTextWidgetClass, 
					help_right_form,
					XtNeditType, XawtextRead,
					XtNscrollVertical, XawtextScrollAlways, 
					XtNwidth, w2,
					XtNheight, 70,
					NULL);

     
    help_unit_cost=XtVaCreateManagedWidget("helpunitcost", 
					  labelWidgetClass, 
					  help_right_form,
					  NULL);
    help_unit_cost_data=XtVaCreateManagedWidget("helpunitcostdata", 
					       labelWidgetClass, 
					       help_right_form,
					       NULL);
    help_unit_attack=XtVaCreateManagedWidget("helpunitattack", 
					     labelWidgetClass, 
					     help_right_form,
					     NULL);
    help_unit_attack_data=XtVaCreateManagedWidget("helpunitattackdata", 
						  labelWidgetClass, 
						  help_right_form,
						  NULL);
    help_unit_def=XtVaCreateManagedWidget("helpunitdef", 
					  labelWidgetClass, 
					  help_right_form,
					  NULL);
    help_unit_def_data=XtVaCreateManagedWidget("helpunitdefdata", 
					       labelWidgetClass, 
					       help_right_form,
					       NULL);
    help_unit_move=XtVaCreateManagedWidget("helpunitmove", 
					  labelWidgetClass, 
					  help_right_form,
					  NULL);
    help_unit_move_data=XtVaCreateManagedWidget("helpunitmovedata", 
					       labelWidgetClass, 
					       help_right_form,
					       NULL);
    help_unit_tile=XtVaCreateManagedWidget("helpunittile",
    					   labelWidgetClass,
					   help_right_form,
					   XtNwidth, NORMAL_TILE_WIDTH,
					   XtNheight, NORMAL_TILE_HEIGHT,
					   NULL);  
    XtAddCallback(help_unit_tile,
                  XtNdestroyCallback,free_bitmap_destroy_callback,
		  NULL);
    help_unit_fp=XtVaCreateManagedWidget("helpunitfp", 
					  labelWidgetClass, 
					  help_right_form,
					  NULL);
    help_unit_fp_data=XtVaCreateManagedWidget("helpunitfpdata", 
					       labelWidgetClass, 
					       help_right_form,
					       NULL);
    help_unit_hp=XtVaCreateManagedWidget("helpunithp", 
					  labelWidgetClass, 
					  help_right_form,
					  NULL);
    help_unit_hp_data=XtVaCreateManagedWidget("helpunithpdata", 
					      labelWidgetClass, 
					      help_right_form,
					      NULL);
   
    help_unit_visrange=XtVaCreateManagedWidget("helpunitvisrange",
    					       labelWidgetClass,
					       help_right_form,
					       NULL);
    help_unit_visrange_data=XtVaCreateManagedWidget("helpunitvisrangedata",
    						    labelWidgetClass,
						    help_right_form,
						    NULL);
     
    help_improvement_req=XtVaCreateManagedWidget("helpimprreq", 
						  labelWidgetClass, 
						  help_right_form,
						  XtNfromVert,help_unit_hp, 
						  NULL);
     help_improvement_req_data=XtVaCreateManagedWidget("helpimprreqdata", 
						       labelWidgetClass, 
						       help_right_form,
						       XtNfromVert,help_unit_hp, 
						       NULL);
     help_wonder_obsolete=XtVaCreateManagedWidget("helpwonderobsolete", 
						  labelWidgetClass, 
						  help_right_form,
						  XtNfromVert,help_unit_hp, 
						  NULL);
     help_wonder_obsolete_data=XtVaCreateManagedWidget("helpwonderobsoletedata", 
						       labelWidgetClass, 
						       help_right_form,
						       XtNfromVert,help_unit_hp, 
						       NULL);

     XtVaGetValues(help_improvement_req, XtNy, &ay, XtNheight, &ah, NULL);
     help_tree_viewport=XtVaCreateManagedWidget("helptreeviewport", 
					       viewportWidgetClass, 
					       help_right_form,
					       XtNwidth, w2,
					       XtNheight, MAX(1,h-(ay+ah)-10),
					       NULL);
     help_tech_tree=XtVaCreateManagedWidget("helptree", 
					   treeWidgetClass, 
					   help_tree_viewport,
					   NULL);
     XawTreeForceLayout(help_tech_tree);  

  }
  else if(type==HELP_TECH) {
    help_text = XtVaCreateManagedWidget("helptext", 
					asciiTextWidgetClass, 
					help_right_form,
					XtNeditType, XawtextRead,
					XtNscrollVertical, XawtextScrollAlways, 
					XtNwidth, w2,
					XtNheight, 70,
					NULL);
    
    XtVaGetValues(help_text, XtNy, &ay, XtNheight, &ah, NULL);
    help_tree_viewport=XtVaCreateManagedWidget("helptreeviewport", 
					       viewportWidgetClass, 
					       help_right_form,
					       XtNwidth, w2,
					       XtNheight, MAX(1,h-(ay+ah)-10),
					       XtNfromVert,help_text,
					       NULL);
    help_tech_tree=XtVaCreateManagedWidget("helptree", 
					   treeWidgetClass, 
					   help_tree_viewport,
					   NULL);
    XawTreeForceLayout(help_tech_tree);  
  }
}

static void help_cathedral_techs_append(char *buf)
{
  int t;
  
  t=game.rtech.cathedral_minus;
  if(tech_exists(t)) 
    sprintf(buf+strlen(buf),
	    "The discovery of %s will reduce this by 1.\n",
	    advances[t].name);
  t=game.rtech.cathedral_plus;
  if(tech_exists(t)) 
    sprintf(buf+strlen(buf),
	    "The discovery of %s will increase this by 1.\n",
	    advances[t].name);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_improvement(struct help_item *pitem, char *title,
				    int which)
{
  char *buf = &long_buffer[0];
  int t;
  
  create_help_page(HELP_IMPROVEMENT);
  
  if (which<B_LAST) {
    struct improvement_type *imp = &improvement_types[which];
    sprintf(buf, "%d ", imp->build_cost);
    xaw_set_label(help_improvement_cost_data, buf);
    sprintf(buf, "%d ", imp->shield_upkeep);
    xaw_set_label(help_improvement_upkeep_data, buf);
    sprintf(buf, "%d ", imp->variant);
    xaw_set_label(help_improvement_variant_data, buf);
    if (imp->tech_requirement == A_LAST) {
      xaw_set_label(help_improvement_req_data, "(Never)");
    } else {
      xaw_set_label(help_improvement_req_data,
		    advances[imp->tech_requirement].name);
    }
    create_tech_tree(help_tech_tree, 0, imp->tech_requirement, 3);
  }
  else {
    xaw_set_label(help_improvement_cost_data, "0 ");
    xaw_set_label(help_improvement_upkeep_data, "0 ");
    xaw_set_label(help_improvement_variant_data,"0 ");
    xaw_set_label(help_improvement_req_data, "(Never)");
    create_tech_tree(help_tech_tree, 0, A_LAST, 3);
  }
  set_title_topic(pitem);
  buf[0] = '\0';
  if(which==B_AQUEDUCT) {
    sprintf(buf+strlen(buf), "Allows a city to grow larger than size %d.",
	    game.aqueduct_size);
    if(improvement_exists(B_SEWER)) {
      char *s = improvement_types[B_SEWER].name;
      sprintf(buf+strlen(buf), "  (A%s %s is also\n"
	      "required for a city to grow larger than size %d.)",
	      n_if_vowel(*s), s, game.sewer_size);
    }
    strcat(buf,"\n");
  }
  if(which==B_SEWER) {
    sprintf(buf+strlen(buf), "Allows a city to grow larger than size %d.\n",
	    game.sewer_size);
  }
  strcat(buf, pitem->text);
  if(which==B_CATHEDRAL) {
    help_cathedral_techs_append(buf);
  }
  if(which==B_COLOSSEUM) {
    t=game.rtech.colosseum_plus;
    if(tech_exists(t)) {
      int n = strlen(buf);
      if(n && buf[n-1] == '\n') buf[n-1] = ' ';
      sprintf(buf+n, "The discovery of %s will increase this by 1.\n",
	      advances[t].name);
    }
  }
  if(which==B_BARRACKS
     && tech_exists(improvement_types[B_BARRACKS].obsolete_by)
     && tech_exists(improvement_types[B_BARRACKS2].obsolete_by)) {
    sprintf(buf+strlen(buf),
	    "\nNote that discovering %s or %s will obsolete\n"
	    "any existing %s.\n",
	    advances[improvement_types[B_BARRACKS].obsolete_by].name,
	    advances[improvement_types[B_BARRACKS2].obsolete_by].name,
	    improvement_types[B_BARRACKS].name);
  }
  if(which==B_BARRACKS2
     && tech_exists(improvement_types[B_BARRACKS2].obsolete_by)) {
    sprintf(buf+strlen(buf),
	    "\nThe discovery of %s will make %s obsolete.\n",
	    advances[improvement_types[B_BARRACKS2].obsolete_by].name,
	    improvement_types[B_BARRACKS2].name);
  }
  XtVaSetValues(help_text, XtNstring, buf, NULL);
}
  
/**************************************************************************
...
**************************************************************************/
static void help_update_wonder(struct help_item *pitem, char *title, int which)
{
  char *buf = &long_buffer[0];
  
  create_help_page(HELP_WONDER);

  if (which<B_LAST) {
    struct improvement_type *imp = &improvement_types[which];
    sprintf(buf, "%d ", imp->build_cost);
    xaw_set_label(help_improvement_cost_data, buf);
    sprintf(buf, "%d ", imp->variant);
    xaw_set_label(help_wonder_variant_data, buf);
    if (imp->tech_requirement == A_LAST) {
      xaw_set_label(help_improvement_req_data, "(Never)");
    } else {
      xaw_set_label(help_improvement_req_data,
		    advances[imp->tech_requirement].name);
    }
    xaw_set_label(help_wonder_obsolete_data,
		  advances[imp->obsolete_by].name);
    create_tech_tree(help_tech_tree, 0, imp->tech_requirement, 3);
  }
  else {
    /* can't find wonder */
    xaw_set_label(help_improvement_cost_data, "0 ");
    xaw_set_label(help_wonder_variant_data, "0 ");
    xaw_set_label(help_improvement_req_data, "(Never)");
    xaw_set_label(help_wonder_obsolete_data, "None");
    create_tech_tree(help_tech_tree, 0, A_LAST, 3); 
  }
  set_title_topic(pitem);
  buf[0] = '\0';
  if(which==B_MANHATTEN && num_role_units(F_NUCLEAR)>0) {
    int u, t;
    u = get_role_unit(F_NUCLEAR, 0);
    assert(u<U_LAST);
    t = get_unit_type(u)->tech_requirement;
    assert(t<A_LAST);
    sprintf(buf+strlen(buf),
	    "Allows all players with knowledge of %s to build %s units.\n",
	    advances[t].name, get_unit_type(u)->name);
  }
  strcat(buf, pitem->text);
  if(which==B_MICHELANGELO) {
    help_cathedral_techs_append(buf);
  }
  XtVaSetValues(help_text, XtNstring, buf, NULL);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_unit_type(struct help_item *pitem, char *title, int i)
{
  char *buf = &long_buffer[0];
  
  create_help_page(HELP_UNIT);
  if (i<U_LAST) {
    struct unit_type *utype = get_unit_type(i);
    sprintf(buf, "%d ", utype->build_cost);
    xaw_set_label(help_unit_cost_data, buf);
    sprintf(buf, "%d ", utype->attack_strength);
    xaw_set_label(help_unit_attack_data, buf);
    sprintf(buf, "%d ", utype->defense_strength);
    xaw_set_label(help_unit_def_data, buf);
    sprintf(buf, "%d ", utype->move_rate/3);
    xaw_set_label(help_unit_move_data, buf);
    sprintf(buf, "%d ", utype->firepower);
    xaw_set_label(help_unit_fp_data, buf);
    sprintf(buf, "%d ", utype->hp);
    xaw_set_label(help_unit_hp_data, buf);
    sprintf(buf, "%d ", utype->vision_range);
    xaw_set_label(help_unit_visrange_data, buf);
    if(utype->tech_requirement==A_LAST) {
      xaw_set_label(help_improvement_req_data, "(Never)");
    } else {
      xaw_set_label(help_improvement_req_data,
		    advances[utype->tech_requirement].name);
    }
    create_tech_tree(help_tech_tree, 0, utype->tech_requirement, 3);
    if(utype->obsoleted_by==-1) {
      xaw_set_label(help_wonder_obsolete_data, "None");
    } else {
      xaw_set_label(help_wonder_obsolete_data,
		    get_unit_type(utype->obsoleted_by)->name);
    }
    /* add text for transport_capacity, fuel, and flags: */
    buf[0] = '\0';
    if (utype->transport_capacity>0) {
      if (unit_flag(i, F_SUBMARINE)) {
	sprintf(buf+strlen(buf), "* Can carry and refuel %d missile units.\n",
		utype->transport_capacity);
      } else if (unit_flag(i, F_CARRIER)) {
	sprintf(buf+strlen(buf), "* Can carry and refuel %d air units.\n",
		utype->transport_capacity);
      } else {
	sprintf(buf+strlen(buf), "* Can carry %d ground units across water.\n",
		utype->transport_capacity);
      }
    }
    if (unit_flag(i, F_CARAVAN)) {
      sprintf(buf+strlen(buf), "* Can establish trade routes and help build wonders.\n");
    }
    if (unit_flag(i, F_SETTLERS)) {
      sprintf(buf+strlen(buf), "* Can perform settler actions.\n");
    }
    if (unit_flag(i, F_DIPLOMAT)) {
      if (unit_flag(i, F_SPY)) 
	sprintf(buf+strlen(buf), "* Can perform diplomatic actions, plus special spy abilities.\n");
      else 
	sprintf(buf+strlen(buf), "* Can perform diplomatic actions.\n");
    }
    if (unit_flag(i, F_FIGHTER)) {
      sprintf(buf+strlen(buf), "* Can attack enemy air units.\n");
    }
    if (unit_flag(i, F_MARINES)) {
      sprintf(buf+strlen(buf), "* Can attack from aboard sea units: against enemy cities and\n  onto land squares.");
    }
    if (unit_flag(i, F_PIKEMEN)) {
      sprintf(buf+strlen(buf), "* Gets double defense against units specified as 'mounted'.\n");
    }
    if (unit_flag(i, F_HORSE)) {
      sprintf(buf+strlen(buf), "* Counts as 'mounted' against certain defenders.\n");
    }
    if (unit_flag(i, F_MISSILE)) {
      sprintf(buf+strlen(buf), "* A missile unit: gets used up in making an attack.\n");
    } else if(unit_flag(i, F_ONEATTACK)) {
      sprintf(buf+strlen(buf), "* Making an attack ends this unit's turn.\n");
    }
    if (unit_flag(i, F_NUCLEAR)) {
      sprintf(buf+strlen(buf), "* This unit's attack causes a nuclear explosion!\n");
    }
    if (unit_flag(i, F_IGWALL)) {
      sprintf(buf+strlen(buf), "* Ignores the effects of city walls.\n");
    }
    if (unit_flag(i, F_AEGIS)) {
      sprintf(buf+strlen(buf), "* Gets quintuple defence against missiles and aircraft.\n");
    }
    if (unit_flag(i, F_IGTER)) {
      sprintf(buf+strlen(buf), "* Ignores terrain effects (treats all squares as roads).\n");
    }
    if (unit_flag(i, F_IGZOC)) {
      sprintf(buf+strlen(buf), "* Ignores zones of control.\n");
    }
    if (unit_flag(i, F_NONMIL)) {
      sprintf(buf+strlen(buf), "* A non-military unit (no shield upkeep).\n");
    }
    if (unit_flag(i, F_TRIREME)) {
      sprintf(buf+strlen(buf), "* Must end turn in a city or next to land, or has a 50%% risk of\n  being lost at sea.");
    }
    if (utype->fuel>0) {
      sprintf(buf+strlen(buf), "* Must end ");
      if (utype->fuel==2) {
	sprintf(buf+strlen(buf), "second ");
      } else if (utype->fuel==3) {
	sprintf(buf+strlen(buf), "third ");
      } else if (utype->fuel>=4) {
	sprintf(buf+strlen(buf), "%dth ", utype->fuel);
      }
      sprintf(buf+strlen(buf), "turn in a city, or on a Carrier");
      if (unit_flag(i, F_MISSILE) &&
	  num_role_units(F_SUBMARINE)>0 &&
	  get_unit_type(get_role_unit(F_SUBMARINE,0))->transport_capacity) {
	sprintf(buf+strlen(buf), " or Submarine");
      }
      sprintf(buf+strlen(buf), ",\n  or will run out of fuel and be lost.\n");
    }
    if (strlen(buf)) {
      sprintf(buf+strlen(buf), "\n");
    } 
    strcpy(buf+strlen(buf), pitem->text);
    XtVaSetValues(help_text, XtNstring, buf, NULL);
  }
  else {
    xaw_set_label(help_unit_cost_data, "0 ");
    xaw_set_label(help_unit_attack_data, "0 ");
    xaw_set_label(help_unit_def_data, "0 ");
    xaw_set_label(help_unit_move_data, "0 ");
    xaw_set_label(help_unit_fp_data, "0 ");
    xaw_set_label(help_unit_hp_data, "0 ");
    xaw_set_label(help_unit_visrange_data, "0 ");
    xaw_set_label(help_improvement_req_data, "(Never)");
    create_tech_tree(help_tech_tree, 0, A_LAST, 3);
    xaw_set_label(help_wonder_obsolete_data, "None");
    XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
  }
  xaw_set_bitmap(help_unit_tile, create_overlay_unit(i));
  set_title_topic(pitem);
}

/**************************************************************************
...
**************************************************************************/
static void help_update_tech(struct help_item *pitem, char *title, int i)
{
  char buf[4096];
  int j;

  create_help_page(HELP_TECH);
  set_title_topic(pitem);

  if (i<A_LAST) {
    create_tech_tree(help_tech_tree, 0, i, 3);
    strcpy(buf, pitem->text);

    if(game.rtech.get_bonus_tech == i) {
      sprintf(buf+strlen(buf),
	      "The first player to research %s gets an immediate advance.\n",
	      advances[i].name);
    }
    if(game.rtech.boat_fast == i) 
      sprintf(buf+strlen(buf), "Gives sea units one extra move.\n");
    if(game.rtech.cathedral_plus == i) 
      sprintf(buf+strlen(buf), "Improves the effect of Cathedrals.\n");
    if(game.rtech.cathedral_minus == i) 
      sprintf(buf+strlen(buf), "Reduces the effect of Cathedrals.\n");
    if(game.rtech.colosseum_plus == i) 
      sprintf(buf+strlen(buf), "Improves the effect of Colosseums.\n");

    for(j=0; j<B_LAST; ++j) {
      if(i==improvement_types[j].tech_requirement) 
	sprintf(buf+strlen(buf), "Allows %s.\n",
		improvement_types[j].name);
    }
    for(j=0; j<U_LAST; ++j) {
      if(i==get_unit_type(j)->tech_requirement) 
	sprintf(buf+strlen(buf), "Allows %s.\n", 
		get_unit_type(j)->name);
    }

    for(j=0; j<A_LAST; ++j) {
      if(i==advances[j].req[0]) {
	if(advances[j].req[1]==A_NONE)
	  sprintf(buf+strlen(buf), "Allows %s.\n", 
		  advances[j].name);
	else
	  sprintf(buf+strlen(buf), "Allows %s(with %s).\n", 
		  advances[j].name, advances[advances[j].req[1]].name);
      }
      if(i==advances[j].req[1]) {
	sprintf(buf+strlen(buf), "Allows %s(with %s).\n", 
		advances[j].name, advances[advances[j].req[0]].name);
      }
    }
    XtVaSetValues(help_text, XtNstring, buf, NULL);
  }
  else {
    create_help_page(HELP_TECH);
    create_tech_tree(help_tech_tree, 0, A_LAST, 3);
    XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
static void help_update_dialog(struct help_item *pitem)
{
  int i;
  char *top;

  /* figure out what kind of item is required for pitem ingo */

  for(top=pitem->topic; *top==' '; ++top);

  switch(pitem->type) {
  case HELP_IMPROVEMENT:
    i = find_improvement_by_name(top);
    if(i!=B_LAST && is_wonder(i)) i = B_LAST;
    help_update_improvement(pitem, top, i);
    break;
  case HELP_WONDER:
    i = find_improvement_by_name(top);
    if(i!=B_LAST && !is_wonder(i)) i = B_LAST;
    help_update_wonder(pitem, top, i);
    break;
  case HELP_UNIT:
    help_update_unit_type(pitem, top, find_unit_type_by_name(top));
    break;
  case HELP_TECH:
    help_update_tech(pitem, top, find_tech_by_name(top));
    break;
  case HELP_TEXT:
  default:
    /* it was a pure text item */ 
    create_help_page(HELP_TEXT);
    set_title_topic(pitem);
    XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
  }

  if (help_tree_viewport) {
    /* Buggy sun athena may require this? --dwp */
    /* And it possibly looks better anyway... */
    XtVaSetValues(help_tree_viewport, XtNforceBars, True, NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
static int help_tree_destroy_children(Widget w)
{
  Widget *children=0;
  Cardinal cnt;
  int did_destroy=0;
  
  XtVaGetValues(help_tech_tree, 
		XtNchildren, &children, 
		XtNnumChildren, &cnt,
		NULL);

  for(; cnt>0; --cnt, ++children) {
    if(XtIsSubclass(*children, commandWidgetClass)) {
      Widget par;
      XtVaGetValues(*children, XtNtreeParent, &par, NULL);
      if(par==w) {
	help_tree_destroy_children(*children);
	XtDestroyWidget(*children);
	did_destroy=1;
      }
    }
  }
  
  return did_destroy;
}


/**************************************************************************
...
**************************************************************************/
void help_tree_node_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data)
{
  size_t tech=(size_t)client_data;
  
  if(!help_tree_destroy_children(w)) {
    if(advances[tech].req[0]!=A_NONE)
      create_tech_tree(help_tech_tree, w, advances[tech].req[0], 1);
    if(advances[tech].req[1]!=A_NONE)
      create_tech_tree(help_tech_tree, w, advances[tech].req[1], 1);
  }
  
}


/**************************************************************************
...
**************************************************************************/
void help_list_callback(Widget w, XtPointer client_data, 
			XtPointer call_data)
{
  XawListReturnStruct *ret;
  
  ret=XawListShowCurrent(help_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct help_item *pitem=find_help_item_position(ret->list_index);
    if(pitem)  {
      help_update_dialog(pitem);

      set_title_topic(pitem);
/*      XtVaSetValues(help_text, XtNstring, pitem->text, NULL);*/
    }
    XawListHighlight(help_list, ret->list_index); 
  }
}

/**************************************************************************
...
**************************************************************************/
void help_close_command_callback(Widget w, XtPointer client_data, 
				 XtPointer call_data)
{
  XtDestroyWidget(help_dialog_shell);
  help_dialog_shell=0;
}

/****************************************************************
...
*****************************************************************/
void close_help_dialog_action(Widget w, XEvent *event, 
			      String *argv, Cardinal *argc)
{
  help_close_command_callback(w, NULL, NULL);
}

/**************************************************************************
...
**************************************************************************/
void select_help_item(int item)
{
  int nitems, pos;
  Dimension height;
  

  XtVaGetValues(help_list, XtNnumberStrings, &nitems, NULL);
  XtVaGetValues(help_list, XtNheight, &height, NULL);
  pos= (((double)item)/nitems)*height;
  XawViewportSetCoordinates(help_viewport, 0, pos);
  XawListHighlight(help_list, item); 
}

void select_help_item_string(char *item, enum help_page_type htype)
{
  int idx;
  struct help_item *pitem = NULL;
  static struct help_item vitem; /* v = virtual */
  static char vtopic[128];
  static char vtext[256];

  idx = 0;
  help_list_iterate(help_nodes, ptmp) {
    char *p=ptmp->topic;
    while(*p==' ')
      ++p;
    if(strcmp(item, p)==0 && (htype==HELP_ANY || htype==ptmp->type)) {
      pitem = ptmp;
      break;
    }
    ++idx;
  }
  help_list_iterate_end;
  
  if(!pitem) {
    idx=0;
    vitem.topic = vtopic;
    strncpy(vtopic, item, sizeof(vtopic));
    vitem.text = vtext;
    if(htype==HELP_ANY || htype==HELP_TEXT) {
      sprintf(vtext, "Sorry, no help topic for %s.\n", vitem.topic);
      vitem.type = HELP_TEXT;
    } else {
      sprintf(vtext, "Sorry, no help topic for %s.\n"
	      "This page was auto-generated.\n\n",
	      vitem.topic);
      vitem.type = htype;
    }
    pitem = &vitem;
  }
  
  select_help_item(idx);
  help_update_dialog(pitem);
}

