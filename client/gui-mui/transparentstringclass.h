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
#ifndef FC__TRANSPARENTSTRINGCLASS_H
#define FC__TRANSPARENTSTRINGCLASS_H

#define MUIA_TransparentString_Contents 		(TAG_USER+0x1000030)
#define MUIA_TransparentString_Acknowledge	(TAG_USER+0x1000031)

extern struct MUI_CustomClass *CL_TransparentString;

#define TransparentStringObject (Object*)NewObject(CL_TransparentString->mcc_Class, NULL

int create_transparentstring_class(void);
void delete_transparentstring_class(void);

#endif  /* FC__TRANSPARENTSTRINGCLASS_H */
