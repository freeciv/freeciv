/*
	SummaryView.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.
*/

#include <support/Debug.h>
#include "SummaryView.hpp"

//--------------------------------------------------------------

SummaryView::SummaryView( int width )
	: BdhView( width, 50, BPoint(0,0), "summaryView",
				B_FOLLOW_NONE, B_WILL_DRAW )
{
}


SummaryView::~SummaryView()
{
}


void
SummaryView::Draw( BRect r )
{
	DrawString( "SUMMARY" );
}


void
SummaryView::AttachedToWindow(void)
{
        BdhView::AttachedToWindow();
	SetViewColor( 128, 0, 128 );
}
 
void
SummaryView::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			BdhView::MessageReceived(msg);
			break;
		}
}
