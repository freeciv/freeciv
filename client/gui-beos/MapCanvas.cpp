/*
	MapCanvas.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.
*/

#include <support/Debug.h>
#include "MapCanvas.hpp"

//--------------------------------------------------------------

MapCanvas::MapCanvas( int tiles_wide, int tiles_high,
	 				  int tile_width, int tile_height )
	: BScrollView( "mapCanvas",
			new MapView( tiles_wide, tiles_high, tile_width, tile_height ),
			B_FOLLOW_NONE, 0, true, true, B_NO_BORDER ),
	tile_width(tile_width), tile_height(tile_height),
	tiles_wide(tiles_wide), tiles_high(tiles_high)
{
}


MapCanvas::~MapCanvas()
{
}

void
MapCanvas::Draw( BRect r )
{
	BScrollView::Draw(r);
}

void
MapCanvas::AttachedToWindow(void)
{
	BScrollView::AttachedToWindow();
}
 
void
MapCanvas::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			BScrollView::MessageReceived(msg);
			break;
		}
}
 

//--------------------------------------------------------------

MapView::MapView( int tiles_wide, int tiles_high,
	 			  int tile_width, int tile_height )
	: SquareView( tiles_wide, tiles_high, tile_width,
                tile_height, BPoint(0,0), "mapView" ),
	tile_width(tile_width), tile_height(tile_height),
	tiles_wide(tiles_wide), tiles_high(tiles_high)
{
}

MapView::~MapView()
{
}


void
MapView::Draw( BRect r )
{
	DrawString( "MAP CANVAS" );
}

void
MapView::AttachedToWindow(void)
{
    BView::AttachedToWindow();
	SetViewColor( 128, 128, 128 );
}
 
void
MapView::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			BView::MessageReceived(msg);
			break;
		}
}
 


