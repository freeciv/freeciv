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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <intuition/intuitionbase.h>
#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>

#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "civclient.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "support.h"
#include "tech.h"
#include "worklist.h"

#include "citydlg_common.h"
#include "climisc.h"
#include "wldlg.h"

#include "worklistclass.h"
#include "muistuff.h"

#define COLUMNS 4
#define BUFFER_SIZE 100

#define MUIA_Worklistview_City           (TAG_USER+0x554546) /* struct city * */
#define MUIA_Worklistview_CurrentTargets (TAG_USER+0x554547) /* BOOL */

struct Worklistview_Data
{
  struct Hook construct_hook;
  struct Hook destruct_hook;
  struct Hook display_hook;
  struct city *pcity;
  char buf[COLUMNS][BUFFER_SIZE];
  char *row[COLUMNS];
  BOOL current_targets;
};


struct worklist_entry
{
  int type;
  wid wid;
};

/****************************************************************
 Constructor of a new entry in the listview
*****************************************************************/
HOOKPROTONHNO(worklistview_construct, struct worklist_entry *, struct worklist_entry *entry)
{
  struct worklist_entry *newentry = (struct worklist_entry *) AllocVec(sizeof(*newentry), 0);
  if (newentry)
  {
    *newentry = *entry;
    return newentry;
  }
}

/****************************************************************
 Destructor of a entry in the listview
*****************************************************************/
HOOKPROTONHNO(worklistview_destruct, void, struct worklist_entry *entry)
{
  FreeVec(entry);
}

#if 0
/****************************************************************
 Returns a information string about a building
*****************************************************************/
char *get_improvement_info(int id, struct city *pcity)
{
  /* from city.c get_impr_name_ex() */
  if (pcity)
  {
    if (wonder_replacement(pcity, id))
      return "*";
  }

  if (is_wonder(id))
  {
    if (game.global_wonders[id])
      return _("Built");
    if (wonder_obsolete(id))
      return _("Obsolete");
    return _("Wonder");
  }
  return "";
}

/****************************************************************
 Returns a information string about a building
*****************************************************************/
char *get_unit_info(int id)
{
  static char info[64];
  struct unit_type *ptype = get_unit_type(id);

  if (ptype->fuel > 0)
  {
    my_snprintf(info, sizeof(info), "%d/%d/%d(%d)", ptype->attack_strength,
		ptype->defense_strength,
		ptype->move_rate / 3, (ptype->move_rate / 3) * ptype->fuel);

  } else
  {
    my_snprintf(info, sizeof(info), "%d/%d/%d", ptype->attack_strength,
		ptype->defense_strength, ptype->move_rate / 3);
  }
  return info;
}
#endif

/****************************************************************
 Display function for the listview
*****************************************************************/
HOOKPROTO(worklistview_display, void, char **array, struct worklist_entry *entry)
{
  struct Worklistview_Data *data = (struct Worklistview_Data *)hook->h_SubEntry;

  if (entry)
  {
    struct city *pcity = data->pcity;
    struct player *pplr = game.player_ptr;

    switch (entry->type)
    {
      case  0: /* id is a wid */
            if (wid_is_worklist(entry->wid)) {
	      my_snprintf(data->buf[0], BUFFER_SIZE, "%s", pplr->worklists[wid_id(entry->wid)].name);
	      my_snprintf(data->buf[1], BUFFER_SIZE, _("Worklist"));
	      my_snprintf(data->buf[2], BUFFER_SIZE, "---");
	      my_snprintf(data->buf[3], BUFFER_SIZE, "---");
	    } else {
	      get_city_dialog_production_row(data->row, BUFFER_SIZE,
	                                     wid_id(entry->wid), wid_is_unit(entry->wid),pcity);
	    }
	    *array++= data->buf[0];
	    *array++= data->buf[1];
	    *array++= data->buf[2];
            break;

#if 0
      case  0:
            /* id is improvement */
            mystrlcpy(buf,get_improvement_info(id,pcity),64);
	    my_snprintf(buf2, 64, "%d", impr_build_shield_cost(id));

	    *array++ = get_improvement_name(id);
	    *array++ = buf;
	    if (id == B_CAPITAL)
	    {
	      *array = "--";
	    } else *array = buf2;
	    break;

      case  1:
	    /* id is unit */
            mystrlcpy(buf,get_unit_info(id),64);
	    my_snprintf(buf2, 64, "%d", unit_build_shield_cost(id));

	    *array++ = unit_name(id);
	    *array++ = buf;
	    *array = buf2;
	    break;

      case  2:
            {
              /* Worklist */
	      struct player *pplr = game.player_ptr;
              int entries = worklist_length(&pplr->worklists[entry->id]);

	      my_snprintf(buf, 64, "%d %s", entries,
			  PL_("entry", "entries", entries));

	      *array++ = pplr->worklists[entry->id].name;
	      *array++ = buf;
	      *array = "--";
	    }
	    break;
#endif

      case  3:
            { /* TRANS: \033u\0338 before and \033n after the text are required */
              *array++ = _("\033u\0338Units\033n");
              *array++ = "";
              *array = "";
            }
            break;

      case  4:
            { /* TRANS: \033u\0338 before and \033n after the text are required */
              *array++ = _("\033u\0338Improvements\033n");
              *array++ = "";
              *array = "";
            }
            break;

      case  5:
            { /* TRANS: \033u\0338 before and \033n after the text are required */
              *array++ = _("\033u\0338Worklists\033n");
              *array++ = "";
              *array = "";
            }
            break;
    }
  } else
  {
    *array++ = _("Type");
    *array++ = _("Info");
    *array = _("Cost");
  }
}

/****************************************************************
 Initialize the listview
*****************************************************************/
static ULONG Worklistview_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  if ((o = (Object *) DoSuperNew(cl, o,
	MUIA_NList_AutoVisible, TRUE,
	MUIA_NList_DragType, MUIV_NList_DragType_Default,
	MUIA_NList_Format,"BAR,P=\033c BAR,P=\033r NOBAR",
	TAG_MORE, msg->ops_AttrList)))
  {
    struct Worklistview_Data *data = (struct Worklistview_Data *) INST_DATA(cl, o);
    struct TagItem *ti;
    int i;

    if ((ti = FindTagItem(MUIA_Worklistview_CurrentTargets, msg->ops_AttrList)))
      data->current_targets = ti->ti_Data;

    if (data->current_targets)
    {
      set(o,MUIA_NList_ShowDropMarks, TRUE);
      set(o,MUIA_NList_DragSortable, TRUE);
    }

    data->construct_hook.h_Entry = (HOOKFUNC)worklistview_construct;
    data->destruct_hook.h_Entry = (HOOKFUNC)worklistview_destruct;
    data->display_hook.h_Entry = (HOOKFUNC)worklistview_display;
    data->display_hook.h_SubEntry = (HOOKFUNC)data;

    /* So we can use the common functions */
    for (i = 0; i < COLUMNS; i++)
	data->row[i] = data->buf[i];

    SetAttrs(o, MUIA_NList_ConstructHook, &data->construct_hook,
                MUIA_NList_DestructHook, &data->destruct_hook,
                MUIA_NList_DisplayHook, &data->display_hook,
                TAG_DONE);

  }
  return (ULONG)o;
}

static ULONG Worklistview_Set(struct IClass *cl, Object *o, struct opSet *msg)
{
  struct TagItem *ti = FindTagItem(MUIA_Worklistview_City,msg->ops_AttrList);
  struct Worklistview_Data *data = (struct Worklistview_Data *) INST_DATA(cl, o);

  if (ti)
  {
    data->pcity = (struct city*)ti->ti_Data;
  }

  return DoSuperMethodA(cl,o,(Msg)msg);
}

ULONG Worklistview_DragQuery(struct IClass *cl, Object *obj, struct MUIP_DragDrop *msg)
{
  if (msg->obj==obj)
  {
    return DoSuperMethodA(cl,obj,(Msg)msg);
  }
  else if (msg->obj==(Object *)muiUserData(obj))
  {
    return(MUIV_DragQuery_Accept);
  }  else
  {
    return(MUIV_DragQuery_Refuse);
  }
}

/****************************************************************
 Insert a single worklist entry at the given position
 (doesn't insert elements which a city can't build)
*****************************************************************/
VOID Worklistview_Insert(Object *obj, struct worklist_entry *entry, LONG pos, struct city *pcity)
{
  if (entry->type == 0 || entry->type == 1)
    DoMethod(obj,MUIM_List_InsertSingle,entry,pos);
}

ULONG Worklistview_DragDrop(struct IClass *cl,Object *obj,struct MUIP_DragDrop *msg)
{
  if (msg->obj==obj)
  {
    return(DoSuperMethodA(cl,obj,(Msg)msg));
  } else
  {
    struct worklist_entry *entry;
    LONG dropmark;
    struct Worklistview_Data *data = (struct Worklistview_Data *) INST_DATA(cl, obj);
 
    /* get source entry */
    DoMethod(msg->obj,MUIM_List_GetEntry,MUIV_List_GetEntry_Active,&entry);

    /* add the source entry to the destination */
    if (data->current_targets)
    {
      get(obj,MUIA_List_DropMark,&dropmark);
      if (entry->type == 0)
      {
	if (wid_is_worklist(entry->wid)) {
	  struct city *pcity = data->pcity;
	  if (pcity)
	  {
	    struct player *pplr = city_owner(pcity);
	    struct worklist *pwl = &pplr->worklists[wid_id(entry->wid)];
	    int i;

            set(obj,MUIA_NList_Quiet,TRUE);

	    for (i = 0; i < worklist_length(pwl); i++) {
	      int target;
	      bool is_unit;
	      struct worklist_entry newentry;

	      /* end of list */
	      if (!worklist_peek_ith(pwl, &target, &is_unit, i))
	        break;

	      newentry.type = 0;
	      newentry.wid = wid_encode(is_unit, FALSE, target);

	      if (newentry.wid == WORKLIST_END) break;
              Worklistview_Insert(obj,&newentry,dropmark,NULL);
	      dropmark++;
	    }
	  }

	  set(obj,MUIA_NList_Quiet,FALSE);
	} else
	{
	  /* Insert eigher a building or an unit */
      	  Worklistview_Insert(obj,entry,dropmark,NULL);
        }
      }
    }

    /* remove source entry */
    if (!data->current_targets)
      DoMethod(msg->obj,MUIM_List_Remove,MUIV_List_Remove_Active);

    get(obj,MUIA_List_InsertPosition,&dropmark);
    set(obj,MUIA_List_Active,dropmark);
    set(msg->obj,MUIA_List_Active,MUIV_List_Active_Off);
    return 0;
  }
}

#if 0
 else
      {
      	if (entry->type == 2)
      	{
	  /* Insert a worklist */
	  struct city *pcity = data->pcity;
	  if (pcity)
	  {
	    int i;
	    struct player *pplr = city_owner(pcity);
	    struct worklist *pwl = &pplr->worklists[entry->id];
	    for (i=0; i < MAX_LEN_WORKLIST && pwl->wlefs[i] != WEF_END; i++)
	    {
	      struct worklist_entry newentry;
	      newentry.id = pwl->wlids[i];
	      if (pwl->wlefs[i] == WEF_UNIT)
	        newentry.type = 1;
	      else
		newentry.type = 0;

	      Worklistview_Insert(obj,&newentry,dropmark,pcity);
	      dropmark++;
	    }
	  }
#endif

DISPATCHERPROTO(Worklistview_Dispatcher)
{
  switch (msg->MethodID)
  {
    case  OM_NEW:
          return Worklistview_New(cl, obj, (struct opSet *) msg);

    case  OM_SET:
          return Worklistview_Set(cl, obj, (struct opSet *) msg);

    case  MUIM_DragQuery:
      	  return(Worklistview_DragQuery(cl,obj,(struct MUIP_DragDrop *)msg));

    case  MUIM_DragDrop:
	  return(Worklistview_DragDrop (cl,obj,(struct MUIP_DragDrop *)msg));
  }
  return (DoSuperMethodA(cl, obj, msg));
}


#define WorklistviewObject NewObject(CL_Worklistview->mcc_Class, NULL

struct MUI_CustomClass *CL_Worklistview;

/*-------------------------------------*/

struct Worklist_Data
{
  struct worklist *worklist;
  struct city *pcity;

  WorklistOkCallback ok_callback;
  WorklistCancelCallback cancel_callback;
  APTR parent_data;
  int embedded_in_city;

  Object *current_listview;
  Object *available_listview;
  Object *future_check;

  BOOL show_advanced_targets;

  
  wid worklist_avail_wids[B_LAST + U_LAST + MAX_NUM_WORKLISTS + 1];
};


#if 0
void worklist_populate_targets(struct Worklist_Data *data)
{
  int i;
  struct player *pplr = game.player_ptr;
  int advanced_tech;
  int can_build, can_eventually_build;
  struct worklist_entry entry;

  set(data->available_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(data->available_listview, MUIM_NList_Clear);

  /* Is the worklist limited to just the current targets, or
     to any available and future targets? */

  advanced_tech = data->show_advanced_targets;
 
  /*     + First, improvements and Wonders. */
  entry.type = 4;
  entry.id = 0;
  DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);

  impr_type_iterate(i) {
    /* Can the player (eventually) build this improvement? */
    can_build = can_player_build_improvement(pplr,i);
    can_eventually_build = could_player_eventually_build_improvement(pplr,i);

    /* If there's a city, can the city build the improvement? */
    if (data->pcity) {
      can_build = can_build && can_build_improvement(data->pcity, i);
      can_eventually_build = can_eventually_build &&
	can_eventually_build_improvement(data->pcity, i);
    }
    
    if (( advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build))
    {
      entry.type = 0;
      entry.id = i;
      DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
    }
  } impr_type_iterate_end;

  /*     + Second, units. */
  entry.type = 3;
  entry.id = 0;
  DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);

  unit_type_iterate(i) {
    /* Can the player (eventually) build this improvement? */
    can_build = can_player_build_unit(pplr,i);
    can_eventually_build = can_player_eventually_build_unit(pplr,i);

    /* If there's a city, can the city build the improvement? */
    if (data->pcity) {
      can_build = can_build && can_build_improvement(data->pcity, i);
      can_eventually_build = can_eventually_build &&
	can_eventually_build_improvement(data->pcity, i);
    }

    if (( advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build))
    {
      entry.type = 1;
      entry.id = i;
      DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
    }
  } unit_type_iterate_end;

  /*     + Finally, the global worklists. */
  if (data->pcity)
  {
    int title_inserted=FALSE;

    /* Now fill in the global worklists. */
    for (i = 0; i < MAX_NUM_WORKLISTS; i++)
    {
      if (pplr->worklists[i].is_valid)
      {
      	if (!title_inserted)
      	{
	  entry.type = 5;
	  entry.id = 0;
	  DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
	  title_inserted = TRUE;
	}

      	entry.type = 2;
      	entry.id = i;
        DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
      }
    }
  }

  set(data->available_listview, MUIA_NList_Quiet, FALSE);
}
#endif

static void targets_list_update(struct Worklist_Data *data)
{
  int i = 0, wids_used = 0;
  struct player *pplr = game.player_ptr;
  int advanced_tech;
  struct worklist_entry entry;

  /* Is the worklist limited to just the current targets, or */
  /* to any available and future targets?                    */
  advanced_tech = data->show_advanced_targets;

  wids_used = collect_wids1(data->worklist_avail_wids, data->pcity,
			    /*are_worklists_first*/0, advanced_tech);

// checkout what this means
#if 0
  if (!game.player_ptr->worklists[0].is_valid)
    gtk_clist_column_title_passive(GTK_CLIST(peditor->avail), 0);
  else
    gtk_clist_column_title_active(GTK_CLIST(peditor->avail), 0);
#endif

  set(data->available_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(data->available_listview, MUIM_NList_Clear);

  for(i=0;i<wids_used;i++)
  {
    wid wid = data->worklist_avail_wids[i];
    entry.wid = wid;
    entry.type = 0;
    DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
  }

  set(data->available_listview, MUIA_NList_Quiet, FALSE);
}

/****************************************************************

*****************************************************************/
static void worklist_list_update(struct Worklist_Data *data)
{
  int i;
  struct worklist_entry entry;

  set(data->current_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(data->current_listview, MUIM_NList_Clear);

  if (data->embedded_in_city && data->pcity)
  {
     entry.type = 0;
     entry.wid = wid_encode(data->pcity->is_building_unit, FALSE, data->pcity->currently_building);
     DoMethod(data->current_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
  }

  /* Fill in the rest of the worklist list */
  for (i = 0; i < MAX_LEN_WORKLIST - 1; i++) {
    int target;
    bool is_unit;

    /* end of list */
    if (!worklist_peek_ith(data->worklist, &target, &is_unit, i))
      break;

    entry.type = 0;
    entry.wid = wid_encode(is_unit, FALSE, target);

    if (entry.wid == WORKLIST_END) break;
    DoMethod(data->current_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
  }

  set(data->current_listview, MUIA_NList_Quiet, FALSE);
}

static VOID worklist_future_check(struct Worklist_Data **pdata)
{
  struct Worklist_Data *data = *pdata;
  data->show_advanced_targets = xget(data->future_check, MUIA_Selected);
  targets_list_update(data);
}

static VOID worklist_remove_button(struct Worklist_Data **pdata)
{
  struct Worklist_Data *data = *pdata;
  DoMethod(data->current_listview,MUIM_List_Remove,MUIV_List_Remove_Active);
}

static VOID worklist_ok(struct Worklist_Data **pdata)
{
  struct Worklist_Data *data = *pdata;
  struct worklist wl;
  int i;
  int entries = xget(data->current_listview, MUIA_NList_Entries);

  /* Fill in this worklist with the parameters set in the worklist 
     dialog. */
  init_worklist(&wl);

  if (entries > MAX_LEN_WORKLIST-1) entries = MAX_LEN_WORKLIST-1;

  for (i = 0; i < entries; i++)
  {
    struct worklist_entry *entry;
    wid wid;
    DoMethod(data->current_listview, MUIM_NList_GetEntry, i, &entry);
    wid = entry->wid;
    wl.wlefs[i] = wid_is_unit(wid) ? WEF_UNIT : WEF_IMPR;
    wl.wlids[i] = wid_id(wid);
  }

  if (entries != 0)
  {
    if (i < MAX_LEN_WORKLIST)
    {
      wl.wlefs[i] = WEF_END;
      wl.wlids[i] = 0;
    }
  } else
  {
    wl.wlefs[0] = wl.wlefs[1] = WEF_END;
    wl.wlids[0] = wl.wlids[1] = 0;
  }

  sz_strlcpy(wl.name, data->worklist->name);
  wl.is_valid = data->worklist->is_valid;
  
  /* Invoke the dialog's parent-specified callback */
  if (data->ok_callback)
    (*data->ok_callback)(&wl, data->parent_data);
}

static VOID worklist_cancel(struct Worklist_Data **pdata)
{
  struct Worklist_Data *data = *pdata;
  /* Invoke the dialog's parent-specified callback */
  if (data->cancel_callback)
    (*data->cancel_callback)(data->parent_data);
}

static ULONG Worklist_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  Object *clv, *alv, *clist, *alist, *check, *remove_button, *ok_button, *cancel_button, *name_text, *name_label;
  Object *undo_button = NULL, *button_group = NULL;

  int embedded = GetTagData(MUIA_Worklist_Embedded,FALSE,msg->ops_AttrList);

  if (embedded)
  {
    undo_button = MakeButton(_("Undo"));
    ok_button = MakeButton(_("Accept"));
    cancel_button = NULL;
  }  else
  {
    button_group = HGroup,
	Child, ok_button = MakeButton(_("_Ok")),
	Child, cancel_button = MakeButton(_("_Cancel")),
	End;
  }

  if ((o = (Object *) DoSuperNew(cl, o,
  	Child, VGroup,
  	    Child, HGroup,
  	        Child, name_label = MakeLabel(""),
		Child, name_text = TextObject,
  	            TextFrame,
		    End,
		End,
  	    Child, HGroup,
		Child, VGroup,
		    Child, TextObject, MUIA_Text_PreParse, "\033c", MUIA_Text_Contents, _("Current Targets"), End,
		    Child, clv = NListviewObject,
		        MUIA_CycleChain, 1,
		        MUIA_NListview_NList, clist = WorklistviewObject,
			    MUIA_NList_Title, TRUE,
			    MUIA_Worklistview_CurrentTargets, TRUE,
			    End,
			End,
		    Child, HGroup,
		        embedded?Child:TAG_IGNORE, ok_button,
		        Child, remove_button = MakeButton(_("_Remove")),
		        embedded?Child:TAG_IGNORE, undo_button,
	                End,
	            End,
	        Child, VGroup,
		    Child, TextObject, MUIA_Text_PreParse, "\033c", MUIA_Text_Contents, _("Available Targets"), End,
		    Child, alv = NListviewObject,
		        MUIA_CycleChain, 1,
		        MUIA_NListview_NList, alist = WorklistviewObject,
			    MUIA_NList_Title, TRUE,
			    End,
		        End,
		    Child, HGroup,
		        Child, MakeLabel(_("Show future targets")),
		        Child, check = MakeCheck(_("Show future targets"), FALSE),
		        Child, HSpace(0),
		        End,
		    End,
		End,
	    embedded?TAG_IGNORE:Child, button_group,
	    End,
	TAG_MORE, msg->ops_AttrList)))
  {
    struct Worklist_Data *data = (struct Worklist_Data *) INST_DATA(cl, o);
    struct TagItem *tl = msg->ops_AttrList;
    struct TagItem *ti;

    data->current_listview = clv;
    data->available_listview = alv;
    data->future_check = check;
    data->embedded_in_city = embedded;

    set(alist,MUIA_UserData,clist);
    set(clist,MUIA_UserData,alist);

    while ((ti = NextTagItem(&tl)))
    {
      switch (ti->ti_Tag)
      {
      	case  MUIA_Worklist_Worklist:
      	      data->worklist = (struct worklist*)ti->ti_Data;
      	      break;

  	case  MUIA_Worklist_City:
  	      data->pcity = (struct city*)ti->ti_Data;
      	      break;

  	case  MUIA_Worklist_PatentData:
  	      data->parent_data = (APTR)ti->ti_Data;
      	      break;

  	case  MUIA_Worklist_OkCallBack:
	      data->ok_callback = (WorklistOkCallback)ti->ti_Data;
      	      break;

  	case  MUIA_Worklist_CancelCallBack:
  	      data->cancel_callback = (WorklistCancelCallback)ti->ti_Data;
      	      break;

      }
    }

    if (data->worklist)
    {
      DoMethod(check, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, check, 4, MUIM_CallHook, &civstandard_hook, worklist_future_check, data);
      DoMethod(remove_button, MUIM_Notify, MUIA_Pressed, FALSE, remove_button, 4, MUIM_CallHook, &civstandard_hook, worklist_remove_button, data);
      DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, ok_button, 4, MUIM_CallHook, &civstandard_hook, worklist_ok, data);
      if (cancel_button) DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, cancel_button, 4, MUIM_CallHook, &civstandard_hook, worklist_cancel, data);
      if (undo_button) DoMethod(undo_button, MUIM_Notify, MUIA_Pressed, FALSE, o, 1, MUIM_Worklist_Undo);

      set(alist,MUIA_Worklistview_City, data->pcity);
      set(clist,MUIA_Worklistview_City, data->pcity);

      if (data->pcity)
      {
      	settext(name_label, _("Worklist for city"));
      	settext(name_text, data->pcity->name);
      } else
      {
      	settext(name_label, _("Name of Worklist"));
      	settext(name_text, data->worklist->name );
      }

      worklist_list_update(data);
      targets_list_update(data);
      return (ULONG)o;
    }

    CoerceMethod(cl, o, OM_DISPOSE);
  }
  return 0;
}

static VOID Worklist_Dispose(struct IClass * cl, Object * o, Msg msg)
{
  DoSuperMethodA(cl, o, msg);
}

DISPATCHERPROTO(Worklist_Dispatcher)
{
  switch (msg->MethodID)
  {
    case OM_NEW:
         return Worklist_New(cl, obj, (struct opSet *) msg);

    case    MUIM_Worklist_Ok:
	    {
		struct Worklist_Data *data = (struct Worklist_Data *)INST_DATA(cl, obj);
		worklist_ok(&data);
		return 0;
	    }

    case    MUIM_Worklist_Cancel:
	    {
		struct Worklist_Data *data = (struct Worklist_Data *)INST_DATA(cl, obj);
		worklist_cancel(&data);
		return 0;
	    }

    case    MUIM_Worklist_Undo:
	    {
		struct Worklist_Data *data = (struct Worklist_Data *)INST_DATA(cl, obj);
		worklist_list_update(data);
		return 0;
	    }

    case OM_DISPOSE:
         Worklist_Dispose(cl, obj, msg);
         return 0;
  }
  return (DoSuperMethodA(cl, obj, msg));
}

struct MUI_CustomClass *CL_Worklist;

BOOL create_worklist_class(void)
{
  if ((CL_Worklist = MUI_CreateCustomClass(NULL, MUIC_Group, NULL, sizeof(struct Worklist_Data), (APTR) Worklist_Dispatcher)))
    if ((CL_Worklistview = MUI_CreateCustomClass(NULL, MUIC_NList, NULL, sizeof(struct Worklistview_Data), (APTR) Worklistview_Dispatcher)))
      return TRUE;
  return FALSE;
}

VOID delete_worklist_class(void)
{
  if (CL_Worklist)
    MUI_DeleteCustomClass(CL_Worklist);
  if (CL_Worklistview)
    MUI_DeleteCustomClass(CL_Worklistview);
}
