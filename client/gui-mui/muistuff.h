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
#ifndef MUISTUFF_H
#define MUISTUFF_H

#ifndef LIBRARIES_MUI_H
#include <libraries/mui.h>
#endif

#ifndef LIBRARIES_IFFPARSE_H
#include <libraries/iffparse.h> /* MAKE_ID macro */
#endif

#include "declgate.h"
#include "SDI_compiler.h"

#ifdef __MORPHOS__
# define HOOKPROTO(name, ret, obj, param) static ret name ## PPC(void); static struct EmulLibEntry name = {TRAP_LIB, 0, (void (*)(void)) name ## PPC}; static ret name ## PPC(void)
# define HOOKPROTONH(name, ret, obj, param) static ret name ## PPC(void); static struct EmulLibEntry name = {TRAP_LIB, 0, (void (*)(void)) name ## PPC}; static ret name ## PPC(void)
# define HOOKPROTONHNO(name, ret, param) static ret name ## PPC(void); static struct EmulLibEntry name = {TRAP_LIB, 0, (void (*)(void)) name ## PPC}; static ret name ## PPC(void)
# define DISPATCHERPROTO(name) ULONG name ## PPC(void)
# define DISPATCHERARG (DECLARG_3(a0, struct IClass *, cl, a2, Object *, obj, a1, Msg, msg)
#else
# define HOOKPROTO(name, ret, obj, param) static SAVEDS ASM(ret) name(REG(a0, struct Hook *hook), REG(a2, obj), REG(a1, param))
# define HOOKPROTONH(name, ret, obj, param) static SAVEDS ASM(ret) name(REG(a2, obj), REG(a1, param))
# define HOOKPROTONHNO(name, ret, param) static SAVEDS ASM(ret) name(REG(a1, param))
# define DISPATCHERPROTO(name) static ASM(ULONG) SAVEDS name(REG(a0, struct IClass * cl), REG(a2, Object * obj), REG(a1, Msg msg))
# define DISPATCHERARG
#endif

#define malloc_struct(x) (x*)(malloc(sizeof(x)))

struct MinNode *Node_Next(APTR node);
struct MinNode *Node_Prev(APTR node);
struct MinNode *List_First(APTR list);
struct MinNode *List_Last(APTR list);
ULONG List_Length(APTR list);
struct MinNode *List_Find(APTR list, ULONG num);

STRPTR StrCopy(const STRPTR str);

LONG xget(Object *obj,ULONG attribute);
char *getstring(Object *obj);
BOOL getbool(Object *obj);
VOID activate( Object *obj);
VOID vsettextf(Object *obj, STRPTR format, APTR args);
VOID settextf(Object *obj, STRPTR format, ...);
VOID settext(Object *obj, STRPTR text);
ULONG DoSuperNew(struct IClass *cl,Object *obj,ULONG tag1,...);

Object *MakeMenu(struct NewMenu *menu);
Object *MakeLabel(STRPTR str);
Object *MakeLabelLeft(STRPTR str);
Object *MakeString(STRPTR label, LONG maxlen);
Object *MakeButton(STRPTR str);
Object *MakeInteger(STRPTR label);
Object *MakeCycle(STRPTR label, STRPTR *array);
Object *MakeRadio(STRPTR label, STRPTR *array);
Object *MakeCheck(STRPTR label, ULONG check);

VOID DisposeAllChilds(Object *o);

extern struct Hook civstandard_hook;
void init_civstandard_hook(void);

#define _between(a,x,b) ((x)>=(a) && (x)<=(b))
#define _isinobject(x,y) (_between(_mleft(o),(x),_mright (o)) && _between(_mtop(o) ,(y),_mbottom(o)))
#define _isinwholeobject(x,y) (_between(_left(o),(x),_right (o)) && _between(_top(o) ,(y),_bottom(o)))
#define MAKECOLOR32(x) (((x)<<24)|((x)<<16)|((x)<<8)|(x))

#define HorizLineObject  RectangleObject,MUIA_VertWeight,0,MUIA_Rectangle_HBar, TRUE,End
#define HorizLineTextObject(text)  RectangleObject,MUIA_VertWeight,0,MUIA_Rectangle_HBar, TRUE,MUIA_Rectangle_BarTitle,text,End

/* a list of MUI window ID's used in this client:
'CITO'  citydlg.c       create_city_dialog() - city options
'CITY'  citydlg.c       create_city_dialog() - city view
'CONN'  connectdlg.c    gui_server_connect()
'CTYR'  cityrep.c       create_city_report_dialog()
'GOTO'  gotodlg.c       popup_goto_dialog()
'HELP'  helpdlg.c       popup_help_dialog_typed()
'INPD'  inputdlg.c      input_dialog_create()
'MAIN'  gui_main.c      init_gui()
'MESS'  messagewin.c    create_meswin_dialog()
'MILI'  repodlgs.c      create_activeunits_report_dialog()
'OPTC'  cityrep.c       cityrep_configure()
'OPTI'  gamedlgs.c      create_option_dialog()
'OPTM'  messagedlg.c    popup_messageopt_dialog()
'POGO'  dialogs.c       popup_notify_goto_dialog()
'POPU'  dialogs.c       popup_notify_dialog()
'PROD'  citydlg.c       popup_city_production_dialog()
'SCNC'  repodlgs.c      popup_science_dialog()
'SNAT'  dialogs.c       popup_races_dialog()
'SPA\0' spaceshipdlg.c  create_spaceship_dialog() - last character is player number
'SPIP'  dialogs.c       create_improvements_list()
'SPST'  dialogs.c       create_advances_list()
'TRAD'  repodlgs.c      reate_trade_report_dialog()
'WRKL'  worklistclass.c Worklist_New()
*/

#endif
