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
#ifndef FC__MAPCLASS_H
#define FC__MAPCLASS_H

#ifndef FC__UNIT_H
#include "unit.h"
#endif

#define TilePopWindowObject     NewObject(CL_TilePopWindow->mcc_Class, NULL
#define MapObject               NewObject(CL_Map->mcc_Class, NULL
#define CityMapObject           NewObject(CL_CityMap->mcc_Class, NULL
#define SpaceShipObject         NewObject(CL_SpaceShip->mcc_Class, NULL
#define SpriteObject            NewObject(CL_Sprite->mcc_Class, NULL
#define UnitObject              NewObject(CL_Unit->mcc_Class, NULL
#define PresentUnitObject       NewObject(CL_PresentUnit->mcc_Class, NULL
#define SupportedUnitObject     NewObject(CL_SupportedUnit->mcc_Class, NULL
#define MyGaugeObject           NewObject(CL_MyGauge->mcc_Class, NULL

extern struct MUI_CustomClass *CL_Map;
extern struct MUI_CustomClass *CL_CityMap;
extern struct MUI_CustomClass *CL_SpaceShip;
extern struct MUI_CustomClass *CL_Sprite;
extern struct MUI_CustomClass *CL_Unit;
extern struct MUI_CustomClass *CL_PresentUnit;
extern struct MUI_CustomClass *CL_SupportedUnit;
extern struct MUI_CustomClass *CL_MyGauge;

struct Map_Click
{
  ULONG x;
  ULONG y;
};

#define MUIA_TilePopWindow_X            (TAG_USER+0x1234400)
#define MUIA_TilePopWindow_Y            (TAG_USER+0x1234401)

#define MUIA_Map_HorizVisible           (TAG_USER+0x1234500)
#define MUIA_Map_VertVisible            (TAG_USER+0x1234501)
#define MUIA_Map_HorizFirst             (TAG_USER+0x1234502)
#define MUIA_Map_VertFirst              (TAG_USER+0x1234503)
#define MUIA_Map_Overview               (TAG_USER+0x1234504) /* .S.. Object */
#define MUIA_Map_HScroller              (TAG_USER+0x1234505) /* .S.. Object */
#define MUIA_Map_VScroller              (TAG_USER+0x1234506) /* .S.. Object */
#define MUIA_Map_Click                  (TAG_USER+0x1234507) /* ..GN struct Map_Click * */
#define MUIA_Map_ScrollButton           (TAG_USER+0x1234508) /* .S.. Object */
#define MUIA_Map_MouseX                 (TAG_USER+0x1234509) /* ..G. LONG */
#define MUIA_Map_MouseY                 (TAG_USER+0x123450a) /* ..G. LONG */
#define MUIA_Map_MouseMoved             (TAG_USER+0x123450b) /* ...N BOOL */

#define MUIM_Map_Refresh                (0x7878787)
#define MUIM_Map_DrawMushroom           (0x7878789)
#define MUIM_Map_ShowCityDesc           (0x7878790)
#define MUIM_Map_PutCityWorkers         (0x7878791)
#define MUIM_Map_PutCrossTile           (0x7878792)
#define MUIM_Map_ExplodeUnit            (0x7878793)
#define MUIM_Map_DrawSegment            (0x7878794)
#define MUIM_Map_DrawUnitAnimationFrame (0x7878795)

struct MUIP_Map_Refresh         {ULONG MethodID; LONG x; LONG y; LONG width; LONG height;
                                 LONG write_to_screen;};
struct MUIP_Map_DrawUnitAnimationFrame {ULONG MethodID; struct unit *punit; bool first_frame; bool last_frame;
			       int old_canvas_x; int old_canvas_y; int new_canvas_x; int new_canvas_y; };
struct MUIP_Map_DrawMushroom    {ULONG MethodID; LONG abs_x0; LONG abs_y0;};
struct MUIP_Map_ShowCityDesc    {ULONG MethodID; struct city *pcity; int canvas_x; int canvas_y;};
struct MUIP_Map_PutCityWorkers  {ULONG MethodID; struct city *pcity; LONG color;};
struct MUIP_Map_PutCrossTile    {ULONG MethodID; LONG abs_x0; LONG abs_y0;};
struct MUIP_Map_ExplodeUnit     {ULONG MethodID; struct unit *punit;};

struct MUIP_Map_DrawSegment     {ULONG MethodID; LONG src_x; LONG src_y; LONG dir;};
struct MUIP_Map_UndrawSegment   {ULONG MethodID; LONG src_x; LONG src_y; LONG dir;};

#define MUIA_CityMap_City               (TAG_USER+0x1234700) /* N... struct city * */
#define MUIA_CityMap_Click              (TAG_USER+0x1234701) /* ...N struct Map_Click * */

#define MUIM_CityMap_Refresh            (0x7878790)

#define MUIA_SpaceShip_Ship             (TAG_USER+0x1235000) /* N... struct player_spaceship * */

#define MUIA_Sprite_Sprite              (TAG_USER+0x1234800) /* NS.. struct Sprite * */
#define MUIA_Sprite_Transparent         (TAG_USER+0x1234801) /* N... BOOL */
#define MUIA_Sprite_OverlaySprite       (TAG_USER+0x1234802) /* N... struct Sprite * */
#define MUIA_Sprite_Background          (TAG_USER+0x1234803) /* NS.. ULONG */

#define MUIA_Unit_Unit                  (TAG_USER+0x1234900) /* NSG. struct unit * */
#define MUIA_Unit_Upkeep                (TAG_USER+0x1234901) /* N... BOOL */

#define MUIM_MyGauge_SetGauge           (0x7878800)

struct MUIP_MyGauge_SetGauge    {ULONG MethodID; LONG current; LONG max; STRPTR info;};

extern BOOL create_map_class(void);
extern VOID delete_map_class(void);

extern Object *MakeMap(void);
extern Object *MakeCityMap(struct city *city);
extern Object *MakeSpaceShip(struct player_spaceship *ship);
extern Object *MakeSprite(struct Sprite *sprite);
extern Object *MakeBorderSprite(struct Sprite *sprite);
extern Object *MakeUnit(struct unit *punit, LONG upkeep);
extern Object *MakePresentUnit(struct unit *punit);
extern Object *MakeSupportedUnit(struct unit *punit);

#endif  /* FC__MAPCLASS_H */
