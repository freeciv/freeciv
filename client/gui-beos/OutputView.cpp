/*
	OutputView.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.

	LATER REFINEMENT:  BScrollView parenting BTextView
*/

#include <support/Debug.h>
#include "OutputView.hpp"

//--------------------------------------------------------------

OutputView::OutputView()
	: TextGridView( 60, 5, be_plain_font, BPoint(0,0), "OutputView" )
{
}


OutputView::~OutputView()
{
}


void
OutputView::Draw( BRect r )
{
	MoveMarkTo( 0, 0 );
	DrawString( "OUTPUT VIEW" );
}


void
OutputView::AttachedToWindow(void)
{
    TextGridView::AttachedToWindow();
	SetViewColor( 200, 200, 200 );
}
 
void
OutputView::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			TextGridView::MessageReceived(msg);
			break;
		}
}
 

