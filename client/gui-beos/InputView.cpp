/*
	InputView.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.

	LATER REFINEMENT:  BButton and one-line BTextView
*/

#include <support/Debug.h>
#include "InputView.hpp"
#include "Defs.hpp"
#include "Backend.hpp"


//-------------------------------------------------------------------

InputView::InputView( const BFont *font, float width )
	: BTextView(
			BRect( 0, 0, width,   4+HeightOf(font) ),
			"InputView",
			BRect( 2, 2, width-2, 2+HeightOf(font) ),
			B_FOLLOW_LEFT | B_FOLLOW_TOP,
			B_WILL_DRAW | B_NAVIGABLE | B_NAVIGABLE_JUMP )
{
	SetWordWrap(false);
}


InputView::~InputView()
{
}


void
InputView::AttachedToWindow(void)
{
    BTextView::AttachedToWindow();

	SetViewColor( 240, 255, 240 );
	SetText( "Hello, Freeciv!" );
	SelectAll();
}
 

void
InputView::KeyDown( const char *bytes, int32 numBytes )
{
	if ( numBytes == 1 && *bytes == B_ENTER ) {
		BMessage *msg = new BMessage ( INPUTVIEW_GENERATES_TEXT );
		msg->AddString( "input", Text() );
		backend->PostMessage( msg );
		Clear();
	} else {
		BTextView::KeyDown( bytes, numBytes );
	}
}
