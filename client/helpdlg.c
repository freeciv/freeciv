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

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/SimpleMenu.h> 
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/Tree.h>

#include <xstuff.h>

#include <climisc.h>
#include <log.h>
#include <shared.h>
#include <unit.h>
#include <city.h>
#include <tech.h>
#include <game.h>
#include <helpdlg.h>
#include <dialogs.h>
#include <graphics.h>

extern int UNIT_TILES;
extern Widget toplevel, main_form;

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
Widget help_wonder_obsolete, help_wonder_obsolete_data;
Widget help_improvement_tree;

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

extern struct improvement_type improvement_types[];
extern struct advance advances[];

struct help_item {
  char *topic, *text;
  int indent;
  struct help_item *next;
  enum help_page_type type;
};

struct help_item *head, *tail;

char *topic_list[1024];


#define TREE_NODE_UNKNOWN_TECH_BG "red"
#define TREE_NODE_KNOWN_TECH_BG "green"
#define TREE_NODE_REACHABLE_TECH_BG "yellow"
#define TREE_NODE_REMOVED_TECH_BG "magenta"


#if 0
/* This isn't used anywhere, which is a good thing because
 * it looks broken to me -- dwp
 */ 
struct help_item *find_help_item_by_topic(char *topic)
{
  struct help_item *pitem=head;

  while(pitem) {
    char *p=pitem->topic;
    while(*p==' ')
      ++p;
    if(!strcmp(topic, p))
      return pitem;
  }
  return 0;
}
#endif

struct help_item *find_help_item_position(int pos)
{
  struct help_item *pitem=head;
  while(pitem && pos)
    pitem=pitem->next, --pos;
  return pitem;
}




void set_title_topic(struct help_item *pitem)
{
  if(!strcmp(pitem->topic, "Freeciv") || 
     !strcmp(pitem->topic, "About"))
    xaw_set_label(help_title, FREECIV_NAME_VERSION);
  else
  xaw_set_label(help_title, pitem->topic);
}




void boot_help_texts(void)
{
  FILE *fs;
  char buf[512], *p, text[64000];
  int len;
  struct help_item *pitem;
  enum help_page_type current_type = HELP_TEXT;
  int i;
  
  fs=fopen(datafilename("helpdata.txt"), "r");
  if(fs==NULL) {
    flog(LOG_NORMAL, "failed reading help-texts");
    return;
  }

  while(1) {
    fgets(buf, 512, fs);
    buf[strlen(buf)-1]='\0';
    if(!strncmp(buf, "%%", 2))
      continue;
    len=strlen(buf);

    if (len>0 && buf[0] == '@') {
      current_type = -1;
      for(i=1; help_type_names[i]; i++) {
	if(strcmp(help_type_names[i],buf+1)==0) {
	  current_type = i;
	  break;
	}
      }
      if (current_type==-1) {
	flog(LOG_NORMAL, "bad help category \"%s\"", buf);
	current_type = HELP_TEXT;
      }
      continue;
    }
    
    p=strchr(buf, '#');
    if(!p) {
      break;
    }

    pitem=(struct help_item *)malloc(sizeof(struct help_item));
    
    pitem->indent=p-buf;
    pitem->topic=mystrdup(p+1);
    pitem->next=0;
    pitem->type=current_type;
    
    text[0]='\0';
    while(1) {
      fgets(buf, 512, fs);
      buf[strlen(buf)-1]='\0';
      if(!strncmp(buf, "%%", 2))
	continue;
      if(!strcmp(buf, "---"))
	break;
      strcat(text, buf);
      strcat(text, "\n");
    } 
    
    pitem->text=mystrdup(text);
  
    if(head==0) {
      head=pitem;
      tail=pitem;
    }
    else {
      tail->next=pitem;
      tail=pitem;
    }
    
  }
  
  fclose(fs);
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
  int i;
  struct help_item *pitem;
  
  for(pitem=head, i=0; pitem; pitem=pitem->next)
    topic_list[i++]=pitem->topic;
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

  /* create_help_page(HELP_IMPROVEMENT); */
  create_help_page(HELP_TEXT);
}


void create_tech_tree(Widget tree, Widget parent, int tech, int levels)
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
  Dimension w, h, w2, h2;
 
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

  if(type==HELP_TEXT || type==HELP_ANY) {
    help_text = XtVaCreateManagedWidget("helptext", 
					asciiTextWidgetClass, 
					help_right_form,
					XtNeditType, XawtextRead,
					XtNscrollVertical, XawtextScrollAlways, 
					XtNwidth, w2,
					XtNheight, h-h2-15,
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
    }
    
    help_improvement_tree=XtVaCreateManagedWidget("helptree", 
						  treeWidgetClass, 
						  help_right_form,
						  XtNwidth, w2,
						  XtNheight, 2*70,
						  NULL);

    
    XawTreeForceLayout(help_improvement_tree);  
    
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
     help_improvement_tree=XtVaCreateManagedWidget("helptree", 
						   treeWidgetClass, 
						   help_right_form,
						   XtNwidth, w2,
						   XtNheight, 2*70,
						  NULL);
     XawTreeForceLayout(help_improvement_tree);  

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
    
    help_improvement_tree=XtVaCreateManagedWidget("helptree", 
						  treeWidgetClass, 
						  help_right_form,
						  XtNwidth, w2,
						  XtNheight, 2*70,
						  XtNfromVert,help_text,
						  NULL);
    XawTreeForceLayout(help_improvement_tree);  
  
  						   

  
  }
}

/**************************************************************************
...
**************************************************************************/
void help_update_improvement(struct help_item *pitem, char *title, int which)
{
  char buf[64];
  
  create_help_page(HELP_IMPROVEMENT);
  
  if (which<B_LAST) {
    struct improvement_type *imp = &improvement_types[which];
    sprintf(buf, "%d ", imp->build_cost);
    xaw_set_label(help_improvement_cost_data, buf);
    sprintf(buf, "%d ", imp->shield_upkeep);
    xaw_set_label(help_improvement_upkeep_data, buf);
    if (imp->tech_requirement == A_LAST) {
      xaw_set_label(help_improvement_req_data, "(Never)");
    } else {
      xaw_set_label(help_improvement_req_data,
		    advances[imp->tech_requirement].name);
    }
    create_tech_tree(help_improvement_tree, 0, imp->tech_requirement, 3);
  }
  else {
    xaw_set_label(help_improvement_cost_data, "0 ");
    xaw_set_label(help_improvement_upkeep_data, "0 ");
    xaw_set_label(help_improvement_req_data, "(Never)");
    create_tech_tree(help_improvement_tree, 0, A_LAST, 3);
  }
  set_title_topic(pitem);
  XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
}
  
/**************************************************************************
...
**************************************************************************/
void help_update_wonder(struct help_item *pitem, char *title, int which)
{
  char buf[64];
  
  create_help_page(HELP_WONDER);

  if (which<B_LAST) {
    struct improvement_type *imp = &improvement_types[which];
    sprintf(buf, "%d ", imp->build_cost);
    xaw_set_label(help_improvement_cost_data, buf);
    if (imp->tech_requirement == A_LAST) {
      xaw_set_label(help_improvement_req_data, "(Never)");
    } else {
      xaw_set_label(help_improvement_req_data,
		    advances[imp->tech_requirement].name);
    }
    xaw_set_label(help_wonder_obsolete_data,
		  advances[imp->obsolete_by].name);
    create_tech_tree(help_improvement_tree, 0, imp->tech_requirement, 3);
  }
  else {
    /* can't find wonder */
    xaw_set_label(help_improvement_cost_data, "0 ");
    xaw_set_label(help_improvement_req_data, "(Never)");
    xaw_set_label(help_wonder_obsolete_data, "None");
    create_tech_tree(help_improvement_tree, 0, A_LAST, 3); 
  }
  set_title_topic(pitem);
  XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
}

/**************************************************************************
...
**************************************************************************/
void help_update_unit_type(struct help_item *pitem, char *title, int i)
{
  char buf[1024];
  
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
    create_tech_tree(help_improvement_tree, 0, utype->tech_requirement, 3);
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
      sprintf(buf+strlen(buf), "* Can perform diplomatic actions.\n");
    }
    if (unit_flag(i, F_FIGHTER)) {
      sprintf(buf+strlen(buf), "* Can attack enemy air units.\n");
    }
    if (unit_flag(i, F_MARINES)) {
      sprintf(buf+strlen(buf), "* Can attack from aboad sea units: against enemy cities and\n  onto land squares.");
    }
    if (unit_flag(i, F_PIKEMEN)) {
      sprintf(buf+strlen(buf), "* Gets double defense against horseback units (other than Cavalry).\n");
    }
    if (unit_flag(i, F_HORSE)) {
      sprintf(buf+strlen(buf), "* Counts as a horseback unit against Pikemen.\n");
    }
    if (unit_flag(i, F_MISSILE)) {
      sprintf(buf+strlen(buf), "* A missile unit: gets used up in making an attack.\n");
    } else if(unit_flag(i, F_ONEATTACK)) {
      sprintf(buf+strlen(buf), "* Making an attack ends this unit's turn.\n");
    }
    if (unit_flag(i, F_IGWALL)) {
      sprintf(buf+strlen(buf), "* Ignores the effects of city walls.\n");
    }
    if (unit_flag(i, F_AEGIS)) {
      sprintf(buf+strlen(buf), "* Gets double defence against missiles and aircraft.\n");
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
	  get_unit_type(U_SUBMARINE)->transport_capacity) {
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
    create_tech_tree(help_improvement_tree, 0, A_LAST, 3);
    xaw_set_label(help_wonder_obsolete_data, "None");
    XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
  }
  xaw_set_bitmap(help_unit_tile, create_overlay_unit(i));
  set_title_topic(pitem);
}

/**************************************************************************
...
**************************************************************************/
void help_update_tech(struct help_item *pitem, char *title, int i)
{
  char buf[4096];
  int j;

  create_help_page(HELP_TECH);
  set_title_topic(pitem);

  if (i<A_LAST) {
    create_tech_tree(help_improvement_tree, 0, i, 3);
    strcpy(buf, pitem->text);

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
    create_tech_tree(help_improvement_tree, 0, A_LAST, 3);
    XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
void help_update_dialog(struct help_item *pitem)
{
  int i;
  char *top;

  /* figure out what kind of item is required for pitem ingo */

  for(top=pitem->topic; *top==' '; ++top);

  switch(pitem->type) {
  case HELP_IMPROVEMENT:
    for(i=0; i<B_LAST; ++i) {	/* need to catch Capitalization */
      struct improvement_type *imp = &improvement_types[i];
      if(!strcmp(top, imp->name)) {
	help_update_improvement(pitem, top, i);
	return;
      }
    }
    /* can't find improvement: */
    help_update_improvement(pitem, top, B_LAST);
    return;
  case HELP_WONDER:
    for(i=B_APOLLO; i<B_LAST; ++i) {
      struct improvement_type *imp = &improvement_types[i];
      if(!strcmp(top, imp->name)) {
	help_update_wonder(pitem, top, i);
	return;
      }
    }
    /* can't find wonder */
    help_update_wonder(pitem, top, B_LAST);
    return;
  case HELP_UNIT:
    for(i=0; i<U_LAST; ++i) {
      struct unit_type *utype = get_unit_type(i);
      if(!strcmp(top, utype->name)) {
	help_update_unit_type(pitem, top, i);
	return;
      }
    }
    /* can't find unit */
    help_update_unit_type(pitem, top, U_LAST);
    return;
  case HELP_TECH:
    for(i=0; i<A_LAST; ++i) {
      if(!strcmp(top, advances[i].name)) {
	help_update_tech(pitem, top, i);
	return;
      }
    }
    /* can't find tech */
    help_update_tech(pitem, top, A_LAST);
    return;
  case HELP_TEXT:
  default:
    /* it was a pure text item */ 
    create_help_page(HELP_TEXT);
    set_title_topic(pitem);
    XtVaSetValues(help_text, XtNstring, pitem->text, NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
int help_tree_destroy_children(Widget w)
{
  Widget *children=0;
  Cardinal cnt;
  int did_destroy=0;
  
  XtVaGetValues(help_improvement_tree, 
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
  int tech=(int)client_data;
  
  if(!help_tree_destroy_children(w)) {
    if(advances[tech].req[0]!=A_NONE)
      create_tech_tree(help_improvement_tree, w, advances[tech].req[0], 1);
    if(advances[tech].req[1]!=A_NONE)
      create_tech_tree(help_improvement_tree, w, advances[tech].req[1], 1);
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
  struct help_item *pitem;
  static struct help_item vitem; /* v = virtual */
  static char vtopic[128];
  static char vtext[256];
  
  for(idx=0, pitem=head; pitem; pitem=pitem->next, ++idx) {
    char *p=pitem->topic;
    while(*p==' ')
      ++p;
    if(strcmp(item, p)==0 && (htype==HELP_ANY || htype==pitem->type))
      break;
  }

  if(!pitem) {
    idx=0;
    vitem.topic = vtopic;
    strncpy(vtopic, item, sizeof(vtopic));
    vitem.indent = 0;
    vitem.next = 0;
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

