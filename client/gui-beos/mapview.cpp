/* mapview.cpp */
#include <Alert.h>	// temporary

#include <Message.h>
#include "MainWindow.hpp"
#include "Defs.hpp"
#include "mapview.h"

bool tile_visible_mapcanvas(int x, int y)	// HOOK
{
	// @@@@ will probably require some direct accesses
	NOT_FINISHED( "tile_visible_mapcanvas(int" );
	return FALSE;
}


bool tile_visible_and_not_on_border_mapcanvas(int x, int y)	// HOOK
{
	// @@@@ will probably require some direct accesses
	NOT_FINISHED( "tile_visible_and_not_on_border_mapcanvas(int" );
	return FALSE;
}



void
update_info_label(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_INFO_LABEL );
}


void
update_unit_info_label(struct unit *punit)	// HOOK
{
    BMessage *msg = new BMessage( UI_UPDATE_UNIT_INFO_LABEL );
    msg->AddPointer( "unit", punit );
    ui->PostMessage( msg );
}


void
update_timeout_label(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_TIMEOUT_LABEL );
}


void
update_unit_pix_label(struct unit *punit)	// HOOK
{
    BMessage *msg = new BMessage( UI_UPDATE_UNIT_PIX_LABEL );
    msg->AddPointer( "unit", punit );
    ui->PostMessage( msg );
}


void
update_turn_done_button(int do_restore)	// HOOK
{
    BMessage *msg = new BMessage( UI_UPDATE_TURN_DONE_BUTTON );
    msg->AddBool( "restore", !!do_restore );
    ui->PostMessage( msg );
}


void
set_bulb_sol_government(int bulb, int sol, int government)	// HOOK
{
    BMessage *msg = new BMessage( UI_SET_BULB_SOL_GOV );
    msg->AddInt32( "bulb", bulb );
    msg->AddInt32( "sol",  sol );
    msg->AddInt32( "gov",  government );
    ui->PostMessage( msg );
}



void
set_overview_dimensions(int x, int y)	// HOOK
{
    BMessage *msg = new BMessage( UI_SET_OVERVIEW_DIMENSIONS );
    msg->AddInt32( "x", x );
    msg->AddInt32( "y", y );
    ui->PostMessage( msg );
}


void
overview_update_tile(int x, int y)	// HOOK
{
    BMessage *msg = new BMessage( UI_OVERVIEW_UPDATE_TILE );
    msg->AddInt32( "x", x );
    msg->AddInt32( "y", y );
    ui->PostMessage( msg );
}



void
center_tile_mapcanvas(int x, int y)	// HOOK
{
    BMessage *msg = new BMessage( UI_CENTER_TILE_MAPCANVAS );
    msg->AddInt32( "x", x );
    msg->AddInt32( "y", y );
    ui->PostMessage( msg );
}


void
get_center_tile_mapcanvas(int *x, int *y)	// HOOK
{
	// @@@@ will probably require some direct accesses
	NOT_FINISHED( "get_center_tile_mapcanvas(int" );
}



void
update_map_canvas(int tile_x, int tile_y, int width, int height,
		  bool write_to_screen)	// HOOK
{
    BMessage *msg = new BMessage( UI_UPDATE_MAP_CANVAS );
    msg->AddInt32( "x", tile_x );
    msg->AddInt32( "y", tile_y );
    msg->AddInt32( "width", width );
    msg->AddInt32( "height", height );
	msg->AddBool( "to_screen", !!write_to_screen );
    ui->PostMessage( msg );
}


void
update_map_canvas_visible(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_MAP_CANVAS_VISIBLE );
}


void
update_map_canvas_scrollbars(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_MAP_CANVAS_SCROLLBARS );
}


void
update_city_descriptions(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_CITY_DESCRIPTIONS );
}


void
put_cross_overlay_tile(int x,int y)	// HOOK
{
    BMessage *msg = new BMessage( UI_PUT_CROSS_OVERLAY_TILE );
    msg->AddInt32( "x", x );
    msg->AddInt32( "y", y );
    ui->PostMessage( msg );
}


void
put_city_workers(struct city *pcity, int color)	// HOOK
{
    BMessage *msg = new BMessage( UI_PUT_CITY_WORKERS );
    msg->AddPointer( "city", pcity );
    msg->AddInt32( "color", color );
    ui->PostMessage( msg );
}



void
move_unit_map_canvas(struct unit *punit, int x0, int y0, int x1, int y1)	// HOOK
{
    BMessage *msg = new BMessage( UI_MOVE_UNIT_MAP_CANVAS );
    msg->AddInt32( "x0", x0 );
    msg->AddInt32( "y0", y0 );
    msg->AddInt32( "x1", x1 );
    msg->AddInt32( "y1", y1 );
    ui->PostMessage( msg );
}


/**************************************************************************
 This function is called to decrease a unit's HP smoothly in battle
 when combat_animation is turned on.
**************************************************************************/
void
decrease_unit_hp_smooth(struct unit *punit0, int hp0,
                             struct unit *punit1, int hp1)	// HOOK
{
    BMessage *msg = new BMessage( UI_DECREASE_UNIT_HP );
	msg->AddPointer( "unit0", punit0 );
    msg->AddInt32( "hp0", hp0 );
	msg->AddPointer( "unit1", punit1 );
    msg->AddInt32( "hp1", hp1 );
    ui->PostMessage( msg );
}


void
put_nuke_mushroom_pixmaps(int abs_x0, int abs_y0)	// HOOK
{
    BMessage *msg = new BMessage( UI_PUT_NUKE_MUSHROOM_PIXMAPS );
    msg->AddInt32( "x", abs_x0 );
    msg->AddInt32( "y", abs_y0 );
    ui->PostMessage( msg );
}



void
refresh_overview_canvas(void)	// HOOK
{
	ui->PostMessage( UI_REFRESH_OVERVIEW_CANVAS );
}


void
refresh_overview_viewrect(void)	// HOOK
{
	ui->PostMessage( UI_REFRESH_OVERVIEW_VIEWRECT );
}


void
refresh_tile_mapcanvas(int x, int y, bool write_to_screen)	// HOOK
{
    BMessage *msg = new BMessage( UI_REFRESH_TILE_MAPCANVAS );
    msg->AddInt32( "x", x );
    msg->AddInt32( "y", y );
	msg->AddBool( "to_screen", !!write_to_screen );
    ui->PostMessage( msg );
}





//---------------------------------------------------------------------
// Work functions
// @@@@

//  UI_UPDATE_INFO_LABEL,
//	UI_UPDATE_UNIT_INFO_LABEL,
//	UI_UPDATE_TIMEOUT_LABEL,
//	UI_UPDATE_UNIT_PIX_LABEL,
//	UI_UPDATE_TURN_DONE_BUTTON,
//	UI_SET_BULB_SOL_GOV,
//	UI_SET_OVERVIEW_DIMENSIONS,
//	UI_OVERVIEW_UPDATE_TILE,
//	UI_CENTER_TILE_MAPCANVAS,
//	UI_UPDATE_MAP_CANVAS,
//	UI_UPDATE_MAP_CANVAS_VISIBLE,
//	UI_UPDATE_MAP_CANVAS_SCROLLBARS,
//	UI_UPDATE_CITY_DESCRIPTIONS,
//	UI_PUT_CROSS_OVERLAY_TILE,
//	UI_PUT_NUKE_MUSHROOM_PIXMAPS,
//	UI_BLINK_ACTIVE_UNIT,
//	UI_MOVE_UNIT_MAP_CANVAS,
//	UI_DECREASE_UNIT_HP,
//	UI_PUT_NUKE_MUSHROOM_PIXMAPS,
//	UI_REFRESH_OVERVIEW_CANVAS,
//	UI_REFRESH_OVERVIEW_VIEWRECT,
//  UI_REFRESH_TILE_MAPCANVAS,
