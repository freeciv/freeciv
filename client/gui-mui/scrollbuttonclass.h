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
#ifndef FC__SCROLLBUTTONCLASS_H
#define FC__SCROLLBUTTONCLASS_H

#define ScrollButtonObject NewObject(CL_ScrollButton->mcc_Class, NULL

#define MUIA_ScrollButton_NewPosition		(TAG_USER+0x182a000) /* ULONG (packed)*/
#define MUIA_ScrollButton_XPosition			(TAG_USER+0x182a001) /* WORD */
#define MUIA_ScrollButton_YPosition			(TAG_USER+0x182a002) /* WORD */

extern struct MUI_CustomClass *CL_ScrollButton;

int create_scrollbutton_class(void);
void delete_scrollbutton_class(void);

#endif  /* FC__SCROLLBUTTONCLASS_H */
