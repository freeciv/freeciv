#ifndef MUISTUFF_H
#define MUISTUFF_H

#ifndef LIBRARIES_MUI_H
#include <libraries/mui.h>
#endif

#define malloc_struct(x) (x*)(malloc(sizeof(x)))

struct MinNode *Node_Next(APTR node);
struct MinNode *Node_Prev(APTR node);
struct MinNode *List_First(APTR list);
struct MinNode *List_Last(APTR list);
ULONG List_Length(APTR list);
struct MinNode *List_Find(APTR list, ULONG num);

STRPTR StrCopy(const STRPTR str);

LONG xget(Object *obj,ULONG attribute);
char *getstring(Object *obj);
BOOL getbool(Object *obj);
VOID activate( Object *obj);
VOID vsettextf(Object *obj, STRPTR format, APTR args);
VOID settextf(Object *obj, STRPTR format, ...);
VOID settext(Object *obj, STRPTR text);
ULONG DoSuperNew(struct IClass *cl,Object *obj,ULONG tag1,...);

Object *MakeMenu( struct NewMenu *menu);
Object *MakeLabel(STRPTR str);
Object *MakeString(STRPTR label, LONG maxlen);
Object *MakeButton(STRPTR str);
Object *MakeInteger(STRPTR label);
Object *MakeCycle(STRPTR label, STRPTR *array);
Object *MakeRadio(STRPTR label, STRPTR *array);
Object *MakeCheck(STRPTR label, ULONG check);

IMPORT struct Hook standart_hook;
void init_standart_hook(void);

#define _between(a,x,b) ((x)>=(a) && (x)<=(b))
#define _isinobject(x,y) (_between(_mleft(o),(x),_mright (o)) && _between(_mtop(o) ,(y),_mbottom(o)))
#define _isinwholeobject(x,y) (_between(_left(o),(x),_right (o)) && _between(_top(o) ,(y),_bottom(o)))
#define MAKECOLOR32(x) ((x<<24)|(x<<16)|(x<<8)|x)

#define HorizLineObject  RectangleObject,MUIA_VertWeight,0,MUIA_Rectangle_HBar, TRUE,End
#define HorizLineTextObject(text)  RectangleObject,MUIA_VertWeight,0,MUIA_Rectangle_HBar, TRUE,MUIA_Rectangle_BarTitle,text,End

#endif
