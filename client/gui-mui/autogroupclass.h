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
#ifndef FC__AUTOGROUPCLASS_H
#define FC__AUTOGROUPCLASS_H

#define AutoGroup NewObject(CL_AutoGroup->mcc_Class, NULL

extern struct MUI_CustomClass *CL_AutoGroup;

#define MUIA_AutoGroup_DefVertObjects   (TAG_USER+0x3000101) /* I.. LONG */
#define MUIA_AutoGroup_NumObjects       (TAG_USER+0x3000102) /* ..G LONG */

#define MUIM_AutoGroup_DisposeChilds (0x7287840)

BOOL create_autogroup_class(void);
VOID delete_autogroup_class(void);

#endif  /* FC__AUTOGROUPCLASS_H */
