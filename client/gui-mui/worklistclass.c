#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <intuition/intuitionbase.h>
#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>

#define USE_SYSBASE
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "civclient.h"
#include "game.h"
#include "map.h"
#include "support.h"
#include "tech.h"
#include "worklist.h"

#include "wldlg.h"

#include "worklistclass.h"
#include "muistuff.h"

#define MUIA_Worklistview_City           (TAG_USER+0x554546) /* struct city * */
#define MUIA_Worklistview_CurrentTargets (TAG_USER+0x554547) /* BOOL */

struct Worklistview_Data
{
  struct Hook construct_hook;
  struct Hook destruct_hook;
  struct Hook display_hook;
  struct city *pcity;
  char buf[256];

/*  STRPTR title;*/

  BOOL current_targets;
};


struct worklist_entry
{
  int type;
  int id;
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
      return "Built";
    if (wonder_obsolete(id))
      return "Obsolette";
    return "Wonder";
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

  if (ptype->fuel)
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

/****************************************************************
 Returns the length of a worklist
*****************************************************************/
int worklist_length(struct worklist *pwl)
{
  int i;
  for (i=0; i < MAX_LEN_WORKLIST && pwl->ids[i] != WORKLIST_END; i++);
  return i;
}

/****************************************************************
 Display function for the listview
*****************************************************************/
HOOKPROTO(worklistview_display, void, char **array, struct worklist_entry *entry)
{
  struct Worklistview_Data *data = (struct Worklistview_Data *)hook->h_SubEntry;

  if (entry)
  {
    struct city *pcity = data->pcity;
    char *buf = data->buf;
    char *buf2 = buf + 128;
    int id = entry->id;

    switch (entry->type)
    {
      case  0:
            /* id is improvement */
            mystrlcpy(buf,get_improvement_info(id,pcity),64);
	    my_snprintf(buf2, 64, "%d", get_improvement_type(id)->build_cost);

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
	    my_snprintf(buf2, 64, "%d", get_unit_type(id)->build_cost);

	    *array++ = unit_name(id);
	    *array++ = buf;
	    *array = buf2;
	    break;

      case  2:
            {
              /* Worklist */
	      struct player *pplr = game.player_ptr;
              int entries = worklist_length(&pplr->worklists[entry->id]);

	      my_snprintf(buf,64,"%d %s",entries,entries==1?"entry":"entries");

	      *array++ = pplr->worklists[entry->id].name;
	      *array++ = buf;
	      *array = "--";
	    }
	    break;

      case  3:
            {
              *array++ = "\33u\338Units\33n";
              *array++ = "";
              *array = "";
            }
            break;

      case  4:
	    {
              *array++ = "\33u\338Improvements\33n";
              *array++ = "";
              *array = "";
            }
            break;

      case  5:
            {
	      *array++ = "\33u\338Worklists\33n";
              *array++ = "";
              *array = "";
            }
            break;
    }
  } else
  {
    *array++ = "Type";
    *array++ = "Info";
    *array = "Cost";
  }
}

/****************************************************************
 Initialize the listview
*****************************************************************/
STATIC ULONG Worklistview_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  if ((o = (Object *) DoSuperNew(cl, o,
	MUIA_NList_AutoVisible, TRUE,
	MUIA_NList_DragType, MUIV_NList_DragType_Default,
	MUIA_NList_Format,"BAR,P=\33c BAR,P=\33r NOBAR",
	TAG_MORE, msg->ops_AttrList)))
  {
    struct Worklistview_Data *data = (struct Worklistview_Data *) INST_DATA(cl, o);
    struct TagItem *ti;
    if ((ti = FindTagItem(MUIA_Worklistview_CurrentTargets, msg->ops_AttrList)))
      data->current_targets = ti->ti_Data;

/*
    if ((ti = FindTagItem(MUIA_NList_Title, msg->ops_AttrList)))
      data->title = (STRPTR)ti->ti_Data;
*/

    if (data->current_targets)
    {
      set(o,MUIA_NList_ShowDropMarks, TRUE);
      set(o,MUIA_NList_DragSortable, TRUE);
    }

    data->construct_hook.h_Entry = (HOOKFUNC)worklistview_construct;
    data->destruct_hook.h_Entry = (HOOKFUNC)worklistview_destruct;
    data->display_hook.h_Entry = (HOOKFUNC)worklistview_display;
    data->display_hook.h_SubEntry = (HOOKFUNC)data;

    SetAttrs(o, MUIA_NList_ConstructHook, &data->construct_hook,
                MUIA_NList_DestructHook, &data->destruct_hook,
                MUIA_NList_DisplayHook, &data->display_hook,
                TAG_DONE);
  }
  return (ULONG)o;
}

STATIC ULONG Worklistview_Set(struct IClass *cl, Object *o, struct opSet *msg)
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
      if (entry->type != 2)
      {
      	/* Insert eigher a building or an unit */
      	Worklistview_Insert(obj,entry,dropmark,NULL);
      } else
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
	    for (i=0; i < MAX_LEN_WORKLIST && pwl->ids[i] != WORKLIST_END; i++)
	    {
	      struct worklist_entry newentry;
	      newentry.id = pwl->ids[i];
	      if (newentry.id >= B_LAST)
	      {
	        newentry.id -= B_LAST;
	        newentry.type = 1;
	      } else newentry.type = 0;

	      Worklistview_Insert(obj,&newentry,dropmark,pcity);
	      dropmark++;
	    }
	  }
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

  Object *current_listview;
  Object *available_listview;
  Object *future_check;

  BOOL show_advanced_targets;
};


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
  for(i=0; i<B_LAST; i++)
  {
    /* Can the player (eventually) build this improvement? */
    can_build = can_player_build_improvement(pplr,i);
    can_eventually_build = could_player_eventually_build_improvement(pplr,i);

    /* If there's a city, can the city build the improvement? */
    if (data->pcity)
    {
      can_build = can_build && can_build_improvement(data->pcity, i);
    /* !!! Note, this is an "issue":
       I'm not performing this check right now b/c I've heard that the
       can_build_* stuff in common/city.c is in flux. */
      /*
      can_eventually_build = can_eventually_build &&
	can_eventually_build_improvement(pdialog->pcity, id);
	*/
    }
    
    if (( advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build))
    {
      entry.type = 0;
      entry.id = i;
      DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
    }
  }

  /*     + Second, units. */
  entry.type = 3;
  entry.id = 0;
  DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
  for(i=0; i<game.num_unit_types; i++)
  {
    /* Can the player (eventually) build this improvement? */
    can_build = can_player_build_unit(pplr,i);
    can_eventually_build = can_player_eventually_build_unit(pplr,i);

    /* If there's a city, can the city build the improvement? */
    if (data->pcity)
    {
      can_build = can_build && can_build_unit(data->pcity, i);
    /* !!! Note, this is another "issue" (same as above). */
      /*
      can_eventually_build = can_eventually_build &&
	can_eventually_build_unit(pdialog->pcity, id);
	*/
    }

    if (( advanced_tech && can_eventually_build) ||
	(!advanced_tech && can_build))
    {
      entry.type = 1;
      entry.id = i;
      DoMethod(data->available_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
    }
  }

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

void worklist_populate_worklist(struct Worklist_Data *data)
{
  int i, n;
  int id;
  int target, is_unit;
  struct worklist_entry entry;

  set(data->available_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(data->available_listview, MUIM_NList_Clear);

  n = 0;
  if (data->pcity)
  {
    /* First element is the current build target of the city. */
    id = data->pcity->currently_building;
    entry.type = data->pcity->is_building_unit;
    entry.id = id;
    DoMethod(data->current_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
  }

  /* Fill in the rest of the worklist list */
  for (i = 0; n < MAX_LEN_WORKLIST &&
	 data->worklist->ids[i] != WORKLIST_END; i++, n++)
  {
    worklist_peek_ith(data->worklist, &target, &is_unit, i);
    entry.type = is_unit;
    entry.id = target;
    DoMethod(data->current_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
  }

  set(data->available_listview, MUIA_NList_Quiet, FALSE);
}

STATIC VOID worklist_close_real(Object **pobj)
{
  Object *obj = *pobj;
  Object *app = (Object*)xget(obj, MUIA_ApplicationObject);
  if (app) DoMethod(app,OM_REMMEMBER,obj);
  MUI_DisposeObject(obj);
}

STATIC VOID worklist_close(Object **pobj)
{
  Object *obj = *pobj;
  Object *app = (Object*)xget(obj, MUIA_ApplicationObject);
  if (app)
  {
    DoMethod(app,MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &civstandard_hook, worklist_close_real, obj);
  }
}

STATIC VOID worklist_future_check(struct Worklist_Data **pdata)
{
  struct Worklist_Data *data = *pdata;
  data->show_advanced_targets = xget(data->future_check, MUIA_Selected);
  worklist_populate_targets(data);
}

STATIC VOID worklist_remove_button(struct Worklist_Data **pdata)
{
  struct Worklist_Data *data = *pdata;
  DoMethod(data->current_listview,MUIM_List_Remove,MUIV_List_Remove_Active);
}

STATIC VOID worklist_ok(struct Worklist_Data **pdata)
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
    DoMethod(data->current_listview, MUIM_NList_GetEntry, i, &entry);
    wl.ids[i] = entry->id;
    if (entry->type) wl.ids[i] += B_LAST;
  }

  if (entries != 0)
  {
    if (i < MAX_LEN_WORKLIST)
      wl.ids[i] = WORKLIST_END;
  } else
  {
    wl.ids[0] = wl.ids[1] = WORKLIST_END;
  }

  strcpy(wl.name, data->worklist->name);
  wl.is_valid = data->worklist->is_valid;
  
  /* Invoke the dialog's parent-specified callback */
  if (data->ok_callback)
    (*data->ok_callback)(&wl, data->parent_data);
}

STATIC VOID worklist_cancel(struct Worklist_Data **pdata)
{
  struct Worklist_Data *data = *pdata;
  /* Invoke the dialog's parent-specified callback */
  if (data->cancel_callback)
    (*data->cancel_callback)(data->parent_data);
}

STATIC ULONG Worklist_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  Object *clv, *alv, *clist, *alist, *check, *remove_button, *ok_button, *cancel_button, *name_text, *name_label;

  if ((o = (Object *) DoSuperNew(cl, o,
        MUIA_Window_Title, "Freeciv - Edit Worklist",
        MUIA_Window_ID, MAKE_ID('W','R','K','L'),
  	WindowContents, VGroup,
  	    Child, HGroup,
  	        Child, name_label = MakeLabel(""),
		Child, name_text = TextObject,
  	            TextFrame,
		    End,
		End,
  	    Child, HGroup,
		Child, VGroup,
		    Child, TextObject, MUIA_Text_PreParse, "\33c", MUIA_Text_Contents, "Current Targets", End,
		    Child, clv = NListviewObject,
		        MUIA_CycleChain, 1,
		        MUIA_NListview_NList, clist = WorklistviewObject,
			    MUIA_NList_Title, TRUE,
			    MUIA_Worklistview_CurrentTargets, TRUE,
			    End,
			End,
		    Child, HGroup,
		        Child, remove_button = MakeButton("_Remove"),
	                End,
	            End,
	        Child, VGroup,
		    Child, TextObject, MUIA_Text_PreParse, "\33c", MUIA_Text_Contents, "Available Targets", End,
		    Child, alv = NListviewObject,
		        MUIA_CycleChain, 1,
		        MUIA_NListview_NList, alist = WorklistviewObject,
			    MUIA_NList_Title, TRUE,
			    End,
		        End,
		    Child, HGroup,
		        Child, MakeLabel("Show future targets"),
		        Child, check = MakeCheck("Show future targets", FALSE),
		        Child, HSpace(0),
		        End,
		    End,
		End,
	    Child, HGroup,
		Child, ok_button = MakeButton("_Ok"),
		Child, cancel_button = MakeButton("_Cancel"),
		End,
	    End,
	TAG_MORE, msg->ops_AttrList)))
  {
    struct Worklist_Data *data = (struct Worklist_Data *) INST_DATA(cl, o);
    struct TagItem *tl = msg->ops_AttrList;
    struct TagItem *ti;

    data->current_listview = clv;
    data->available_listview = alv;
    data->future_check = check;

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
      DoMethod(o,MUIM_Notify,MUIA_Window_CloseRequest, TRUE, o, 4, MUIM_CallHook, &civstandard_hook, worklist_cancel, data);
      DoMethod(o,MUIM_Notify,MUIA_Window_CloseRequest, TRUE, o, 4, MUIM_CallHook, &civstandard_hook, worklist_close, o);
      DoMethod(check, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, check, 4, MUIM_CallHook, &civstandard_hook, worklist_future_check, data);
      DoMethod(remove_button, MUIM_Notify, MUIA_Pressed, FALSE, remove_button, 4, MUIM_CallHook, &civstandard_hook, worklist_remove_button, data);
      DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, ok_button, 4, MUIM_CallHook, &civstandard_hook, worklist_ok, data);
      DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, ok_button, 4, MUIM_CallHook, &civstandard_hook, worklist_close, o);
      DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, cancel_button, 4, MUIM_CallHook, &civstandard_hook, worklist_cancel, data);
      DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, cancel_button, 4, MUIM_CallHook, &civstandard_hook, worklist_close, o);

      set(alist,MUIA_Worklistview_City, data->pcity);
      set(clist,MUIA_Worklistview_City, data->pcity);

      if (data->pcity)
      {
      	settext(name_label, "Worklist for city");
      	settext(name_text, data->pcity->name);
      } else
      {
      	settext(name_label, "Name of Worklist");
      	settext(name_text, data->worklist->name );
      }

      worklist_populate_worklist(data);
      worklist_populate_targets(data);
      return (ULONG)o;
    }

    CoerceMethod(cl, o, OM_DISPOSE);
  }
  return 0;
}

STATIC VOID Worklist_Dispose(struct IClass * cl, Object * o, Msg msg)
{
/*  struct Worklist_Data *data = (struct Worklist_Data *) INST_DATA(cl, o); */
  DoSuperMethodA(cl, o, msg);
}

DISPATCHERPROTO(Worklist_Dispatcher)
{
  switch (msg->MethodID)
  {
    case OM_NEW:
         return Worklist_New(cl, obj, (struct opSet *) msg);

    case OM_DISPOSE:
         Worklist_Dispose(cl, obj, msg);
         return 0;
  }
  return (DoSuperMethodA(cl, obj, msg));
}

struct MUI_CustomClass *CL_Worklist;

BOOL create_worklist_class(void)
{
  if ((CL_Worklist = MUI_CreateCustomClass(NULL, MUIC_Window, NULL, sizeof(struct Worklist_Data), (APTR) Worklist_Dispatcher)))
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
