/*
	UnitInfoView.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.
*/

#include <support/Debug.h>
#include "UnitInfoView.hpp"

//--------------------------------------------------------------

UnitInfoView::UnitInfoView( int width )
	: BdhView( width, 60, BPoint(0,0), "unitInfoView",
			B_FOLLOW_NONE, B_WILL_DRAW )
{
}


UnitInfoView::~UnitInfoView()
{
}

void
UnitInfoView::Draw( BRect r )
{
	DrawString( "UNIT INFO" );
}

void
UnitInfoView::AttachedToWindow(void)
{
        BdhView::AttachedToWindow();
	SetViewColor( 128, 0, 0 );
}
 
void
UnitInfoView::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			BdhView::MessageReceived(msg);
			break;
		}
}
 
