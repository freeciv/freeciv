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
#ifndef FC__COLORTEXTCLASS_H
#define FC__COLORTEXTCLASS_H

#define ColorTextObject NewObject(CL_ColorText->mcc_Class, NULL

#define MUIA_ColorText_Contents       MUIA_Text_Contents
#define MUIA_ColorText_Foreground     (TAG_USER+0x1234901)
#define MUIA_ColorText_Background     (TAG_USER+0x1234902)
#define MUIA_ColorText_Align          (TAG_USER+0x1234903)

#define MUIV_ColorText_Align_Left 0
#define MUIV_ColorText_Align_Center 1

extern struct MUI_CustomClass *CL_ColorText;

BOOL create_colortext_class(void);
VOID delete_colortext_class(void);

#endif  /* FC__COLORTEXTCLASS_H */
