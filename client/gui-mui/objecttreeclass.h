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
#ifndef FC__OBJECTTREECLASS_H
#define FC__OBJECTTREECLASS_H

#define ObjectTreeObject NewObject(CL_ObjectTree->mcc_Class, NULL

extern struct MUI_CustomClass *CL_ObjectTree;

#define MUIM_ObjectTree_AddNode    (0x7287830) /* returns APTR */
#define MUIM_ObjectTree_Clear      (0x7287831)
#define MUIM_ObjectTree_FindObject (0x7287832) /* returns APTR */
#define MUIM_ObjectTree_ClearSubNodes  (0x7287833)
#define MUIM_ObjectTree_HasSubNodes (0x7287834) /* returns BOOL */

struct MUIP_ObjectTree_AddNode{ULONG MethodID; APTR parent; Object *object;};
struct MUIP_ObjectTree_FindObject{ULONG MethodID; Object *object;};
struct MUIP_ObjectTree_ClearSubNodes{ULONG MethodID; APTR parent;};
struct MUIP_ObjectTree_HasSubNodes{ULONG MethodID; APTR parent;};

BOOL create_objecttree_class(void);
VOID delete_objecttree_class(void);

#endif  /* FC__OBJECTTREECLASS_H */
