#ifndef WORKLISTCLASS_H
#define WORKLISTCLASS_H

#define WorklistObject NewObject(CL_Worklist->mcc_Class, NULL

IMPORT struct MUI_CustomClass *CL_Worklist;

#define MUIA_Worklist_Worklist			(TAG_USER+0x454545) /* struct worklist * */
#define MUIA_Worklist_City					(TAG_USER+0x454546) /* struct city * */
#define MUIA_Worklist_PatentData		(TAG_USER+0x454547) /* APTR */
#define MUIA_Worklist_OkCallBack    (TAG_USER+0x454548)
#define MUIA_Worklist_CancelCallBack     (TAG_USER+0x454549)

BOOL create_worklist_class(void);
VOID delete_worklist_class(void);

#endif
