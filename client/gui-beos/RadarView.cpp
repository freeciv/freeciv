/*
	RadarView.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.
*/

#include <support/Debug.h>
#include "RadarView.hpp"

//--------------------------------------------------------------

RadarView::RadarView( Sprite *initial )
	: BdhView(	initial->Width(), initial->Height(),
				BPoint(0,0), "RadarView", B_FOLLOW_ALL, B_WILL_DRAW )
{
}


RadarView::~RadarView()
{
}

void
RadarView::Draw( BRect r )
{
	DrawString( "RADAR" );
}


void
RadarView::AttachedToWindow(void)
{
    BdhView::AttachedToWindow();
	SetViewColor( 0, 0, 0 );
}
 
void
RadarView::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			BdhView::MessageReceived(msg);
			break;
		}
}
