/*
	StatusView.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.
*/

#include <support/Debug.h>
#include "StatusView.hpp"

//--------------------------------------------------------------

StatusView::StatusView( int width )
	: BdhView( width, 60, BPoint(0,0), "statusView",
			B_FOLLOW_NONE, B_WILL_DRAW )
{
}


StatusView::~StatusView()
{
}

void
StatusView::Draw( BRect r )
{
	DrawString( "STATUS" );
}

void
StatusView::AttachedToWindow(void)
{
        BdhView::AttachedToWindow();
	SetViewColor( 0, 128, 128 );
}
 
void
StatusView::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			BdhView::MessageReceived(msg);
			break;
		}
}
 
