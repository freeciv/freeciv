#ifndef FC__UNIT_H
#include "unit.h"
#endif

#define TilePopWindowObject NewObject(CL_TilePopWindow->mcc_Class, NULL
#define MapObject NewObject(CL_Map->mcc_Class, NULL
#define CityMapObject NewObject(CL_CityMap->mcc_Class, NULL
#define SpaceShipObject NewObject(CL_SpaceShip->mcc_Class, NULL
#define SpriteObject NewObject(CL_Sprite->mcc_Class, NULL
#define UnitObject NewObject(CL_Unit->mcc_Class, NULL
#define MyGaugeObject NewObject(CL_MyGauge->mcc_Class, NULL

IMPORT struct MUI_CustomClass *CL_Map;
IMPORT struct MUI_CustomClass *CL_CityMap;
IMPORT struct MUI_CustomClass *CL_SpaceShip;
IMPORT struct MUI_CustomClass *CL_Sprite;
IMPORT struct MUI_CustomClass *CL_Unit;
IMPORT struct MUI_CustomClass *CL_MyGauge;

struct Map_Click
{
	ULONG x;
	ULONG y;
};

#define MUIA_TilePopWindow_X			(TAG_USER+0x1234400)
#define MUIA_TilePopWindow_Y			(TAG_USER+0x1234401)

#define MUIA_Map_HorizVisible		(TAG_USER+0x1234500)
#define MUIA_Map_VertVisible			(TAG_USER+0x1234501)
#define MUIA_Map_HorizFirst			(TAG_USER+0x1234502)
#define MUIA_Map_VertFirst				(TAG_USER+0x1234503)
#define MUIA_Map_Overview				(TAG_USER+0x1234504) /* .S.. Object */
#define MUIA_Map_HScroller				(TAG_USER+0x1234505) /* .S.. Object */
#define MUIA_Map_VScroller				(TAG_USER+0x1234506) /* .S.. Object */
#define MUIA_Map_Click						(TAG_USER+0x1234507) /* ..GN struct Map_Click * */

#define MUIM_Map_Refresh					(0x7878787)
#define MUIM_Map_MoveUnit				(0x7878788)
#define MUIM_Map_DrawMushroom		(0x7878789)
#define MUIM_Map_ShowCityNames    (0x7878790)
#define MUIM_Map_PutCityWorkers	(0x7878791)
#define MUIM_Map_PutCrossTile		(0x7878792)

struct MUIP_Map_Refresh {ULONG MethodID; LONG tilex; LONG tiley; LONG width; LONG height; LONG write_to_screen;};
struct MUIP_Map_MoveUnit {ULONG MethodID; struct unit *punit; LONG x0; LONG y0; LONG dx; LONG dy;LONG dest_x;LONG dest_y;};
struct MUIP_Map_DrawMushroom {ULONG MethodID; LONG abs_x0; LONG abs_y0;};
struct MUIP_Map_ShowCityNames {ULONG MethodID;};
struct MUIP_Map_PutCityWorkers {ULONG MethodID; struct city *pcity; LONG color;};
struct MUIP_Map_PutCrossTile {ULONG MethodID; LONG abs_x0; LONG abs_y0;};

#define MUIA_CityMap_City				(TAG_USER+0x1234700) /* N... struct city * */
#define MUIA_CityMap_Click				(TAG_USER+0x1234701) /* ...N struct Map_Click * */

#define MUIM_CityMap_Refresh			(0x7878790)

#define MUIA_SpaceShip_Ship       (TAG_USER+0x1235000) /* N... struct player_spaceship * */

#define MUIA_Sprite_Sprite				(TAG_USER+0x1234800) /* NS.. struct Sprite * */

#define MUIA_Unit_Unit						(TAG_USER+0x1234900) /* NS.. struct unit * */
#define MUIA_Unit_Upkeep					(TAG_USER+0x1234901) /* N... BOOL */


#define MUIM_MyGauge_SetGauge		(0x7878800)

struct MUIP_MyGauge_SetGauge {ULONG MethodID; LONG current; LONG max; STRPTR info;};

BOOL create_map_class(void);
VOID delete_map_class(void);

struct city;
struct player_spaceship;
struct Sprite;

Object *MakeMap(void);
Object *MakeCityMap(struct city *city);
Object *MakeSpaceShip(struct player_spaceship *ship);
Object *MakeSprite(struct Sprite *sprite);
Object *MakeBorderSprite(struct Sprite *sprite);
Object *MakeUnit(struct unit *punit, LONG upkeep);

// Something which should be otherwere

int get_normal_tile_height(void);
int get_normal_tile_width(void);

struct Sprite *get_citizen_sprite(int frame);
struct Sprite *get_thumb_sprite(int onoff);

// void put_sprite(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y);

int load_all_sprites(void);
void free_all_sprites(void);
