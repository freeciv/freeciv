#ifndef OBJECTTREECLASS_H
#define OBJECTTREECLASS_H

#define ObjectTreeObject NewObject(CL_ObjectTree->mcc_Class, NULL

IMPORT struct MUI_CustomClass *CL_ObjectTree;

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

#endif
