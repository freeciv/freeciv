/*
	UnitsBelowView.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.
*/

#include <support/Debug.h>
#include "UnitsBelowView.hpp"

//--------------------------------------------------------------

UnitsBelowView::UnitsBelowView( int width )
	: BdhView( width, 30, BPoint(0,0), "belowView",
			B_FOLLOW_NONE, B_WILL_DRAW )
{
}


UnitsBelowView::~UnitsBelowView()
{
}

void
UnitsBelowView::Draw( BRect r )
{
	DrawString( "UNITS BELOW" );
}

void
UnitsBelowView::AttachedToWindow(void)
{
        BdhView::AttachedToWindow();
	SetViewColor( 0, 128, 0 );
}
 
void
UnitsBelowView::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			BdhView::MessageReceived(msg);
			break;
		}
}
