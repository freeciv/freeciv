/*
	BdhView.cpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/

#define _BUILDING_Bdh 1
#include <storage/File.h>
#include "BdhView.h"

// --------------------------------------------------------------
// BdhView

BdhView::BdhView( uint32 width, uint32 height, BPoint offset,
		const char* title, uint32 resizing, uint32 flags )
	: BView( BRect(offset, offset+BPoint(width,height)),
			title, resizing, flags )
{
}

BdhView::~BdhView()
{
}

// --------------------------------------------------------------
// BdhBitmapView

BdhBitmapView::BdhBitmapView( uint32 dx, uint32 dy,
			BPoint offset, BBitmap *bm )
	: BdhView( dx, dy, offset, "bitmap", B_FOLLOW_NONE, B_WILL_DRAW ),
		bitmap(bm), font(0), fontSize(10.0)
{
	float fx = dx * 0.5;
	float fy = dy * 0.9;
	fontSize = floor((fx<fy)?fx:fy);
}

BdhBitmapView::BdhBitmapView( uint32 dx, uint32 dy,
			BPoint offset, const char* filename )
	: BdhView( dx, dy, offset, filename, B_FOLLOW_NONE, B_WILL_DRAW ),
		bitmap(0), font(0), fontSize(10.0)
{
	BFile bmFile( filename, B_READ_ONLY );
	if ( bmFile.IsReadable() ) {	
// @@@@ suck out bits into bitmap
	} else {
		float fx = dx * 0.5;
		float fy = dy * 0.9;
		fontSize = floor((fx<fy)?fx:fy);
	}
}


BdhBitmapView::~BdhBitmapView()
{
	delete font;
}


void
BdhBitmapView::AttachedToWindow(void)
{
	BdhView::AttachedToWindow();
	
	font = new BFont( be_fixed_font );
	font->SetSize( fontSize );
	if ( bitmap ) {
		SetViewColor( B_TRANSPARENT_32_BIT );
	} else {
		SetViewColor(0,0,0);
		SetLowColor(0,0,0);
	}
}


void
BdhBitmapView::Draw( BRect /*rect*/ )
{
	if ( bitmap ) {
		DrawBitmap( bitmap, BPoint(0,0) );
		return;
	}
	
	// change out old settings
	rgb_color oldColor = HighColor();
	BFont oldFont;
	GetFont(&oldFont);
	// put in new settings
	SetFont(font);
	// paint in logo
	float dy = Frame().Height()/8;
	float dx = (Frame().Width()-StringWidth("BDH"))/2;
	SetHighColor(255,0,0);
	DrawChar( 'B', BPoint(dx,5.0*dy) );
	SetHighColor(0,255,0);
	DrawChar( 'D' );
	SetHighColor(0,0,255);
	DrawChar( 'H' );
	SetHighColor(255,0,255);
	FillRect( BRect(dx,6.0*dy, Frame().Width()-dx,6.5*dy) );
	// change back old settings
	SetHighColor(oldColor);
	SetFont(&oldFont);
}
