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
#ifndef FC__OVERVIEWCLASS_H
#define FC__OVERVIEWCLASS_H

#define OverviewObject NewObject(CL_Overview->mcc_Class, NULL

#ifndef LIBRARIES_MUI_H
#include <libraries/mui.h>
#endif

#define MUIA_Overview_Width           (TAG_USER+0x3000001) /* IG LONG */
#define MUIA_Overview_Height          (TAG_USER+0x3000002) /* IG LONG */
#define MUIA_Overview_Scale           (TAG_USER+0x3000003) /* I  LONG */

#define MUIA_Overview_RectLeft        (TAG_USER+0x3000005) /* GS */
#define MUIA_Overview_RectTop         (TAG_USER+0x3000006) /* GS */
#define MUIA_Overview_RectWidth       (TAG_USER+0x3000007) /* GS */
#define MUIA_Overview_RectHeight      (TAG_USER+0x3000008) /* GS */
#define MUIA_Overview_NewPos          (TAG_USER+0x3000009) /* N */

#define MUIA_Overview_RadarPicture    (TAG_USER+0x3000010) /* S */

#define MUIM_Overview_Refresh         (0x7287822) /* Refresh complete */
#define MUIM_Overview_RefreshSingle   (0x7287823) /* Refresh single */

struct MUIP_Overview_RefreshSingle { ULONG MethodID; int x; int y;};

extern struct MUI_CustomClass *CL_Overview;

BOOL create_overview_class(void);
VOID delete_overview_class(void);

Object *MakeOverview(LONG width, LONG height);

#endif /* FC__OVERVIEWCLASS_H */
