/*
	InputView.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.

	LATER REFINEMENT:  BButton and one-line BTextView
*/

#include <support/Debug.h>
#include "InputView.hpp"

//--------------------------------------------------------------

InputView::InputView()
	: TextGridView( 60, 1, be_plain_font, BPoint(0,0), "InputView" )
{
}


InputView::~InputView()
{
}


void
InputView::Draw( BRect r )
{
	MoveMarkTo( 0, 0 );
	DrawString( "INPUT VIEW" );
}


void
InputView::AttachedToWindow(void)
{
    TextGridView::AttachedToWindow();
	SetViewColor( 240, 255, 240 );
}
 
void
InputView::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			TextGridView::MessageReceived(msg);
			break;
		}
}
 
