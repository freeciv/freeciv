#ifndef FC__SCROLLBUTTONCLASS_H
#define FC__SCROLLBUTTONCLASS_H

#define ScrollButtonObject NewObject(CL_ScrollButton->mcc_Class, NULL

#define MUIA_ScrollButton_NewPosition		(TAG_USER+0x182a000) /* ULONG (packed)*/
#define MUIA_ScrollButton_XPosition			(TAG_USER+0x182a001) /* WORD */
#define MUIA_ScrollButton_YPosition			(TAG_USER+0x182a002) /* WORD */

IMPORT struct MUI_CustomClass *CL_ScrollButton;

int create_scrollbutton_class(void);
void delete_scrollbutton_class(void);

#endif  /* FC__SCROLLBUTTONCLASS_H */

