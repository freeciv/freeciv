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

IMPORT struct MUI_CustomClass *CL_Overview;

BOOL create_overview_class(void);
VOID delete_overview_class(void);

Object *MakeOverview(LONG width, LONG height);

#endif /* FC__OVERVIEWCLASS_H */

