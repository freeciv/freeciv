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
#ifndef FC__WORKLISTCLASS_H
#define FC__WORKLISTCLASS_H

#define WorklistObject NewObject(CL_Worklist->mcc_Class, NULL

extern struct MUI_CustomClass *CL_Worklist;

#define MUIA_Worklist_Worklist			 (TAG_USER+0x454545) /* struct worklist * */
#define MUIA_Worklist_City					 (TAG_USER+0x454546) /* struct city * */
#define MUIA_Worklist_PatentData		 (TAG_USER+0x454547) /* APTR */
#define MUIA_Worklist_OkCallBack     (TAG_USER+0x454548)
#define MUIA_Worklist_CancelCallBack (TAG_USER+0x454549)
#define MUIA_Worklist_Embedded       (TAG_USER+0x45454a) /* BOOL */

#define MUIM_Worklist_Ok             (TAG_USER+0x2671)
#define MUIM_Worklist_Cancel         (TAG_USER+0x2672)
#define MUIM_Worklist_Undo           (TAG_USER+0x2673)

BOOL create_worklist_class(void);
VOID delete_worklist_class(void);

#endif  /* FC__WORKLISTCLASS_H */
