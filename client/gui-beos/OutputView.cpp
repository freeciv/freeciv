/*
	OutputView.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.

	LATER REFINEMENT:  BScrollView parenting BTextView
*/

#include <support/Debug.h>
#include "OutputView.hpp"
#include "Defs.hpp"
#include <TextView.h>
#include <Font.h>

extern "C" {
#include "log.h"
}


//--------------------------------------------------------------
#define ROWS	 	 6
#define MAX_LINES	25

OutputView::OutputView( const BFont *font, float width )
	: BScrollView( "OutputView",
		new BTextView(
				BRect( 0, 0,
						width/2-B_V_SCROLL_BAR_WIDTH,   4+ROWS*HeightOf(font) ),
				"OutputText",
				BRect( 2, 2,
						width/2-B_V_SCROLL_BAR_WIDTH-2, 2+ROWS*HeightOf(font) ),
				B_FOLLOW_NONE, B_WILL_DRAW ),
		B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW, false, true, B_NO_BORDER ),
	lineSize( HeightOf(font) ), text(0)
{
	text = cast_to( BTextView, ChildAt(0) );
}


OutputView::~OutputView()
{
}


void
OutputView::AttachedToWindow(void)
{
    BScrollView::AttachedToWindow();
	if ( ! text ) return;

	text->MakeEditable( false );
	text->SetViewColor( 220, 255, 220 );
	ScrollBar( B_VERTICAL )->SetRange( 0, MAX_LINES-ROWS );
	ScrollBar( B_VERTICAL )->SetSteps( lineSize, ROWS*lineSize );
	ScrollBar( B_VERTICAL )->SetProportion( 1.0 );
	ScrollBar( B_VERTICAL )->SetValue( 0.0 );
}
 

void
OutputView::Append( const char *str )
{
	if ( ! text ) return;

	// Append text
	text->Insert( text->TextLength(), str, strlen(str) );
	text->Insert( text->TextLength(), "\n", 1 );

	// Remove excess lines
	int excess = text->CountLines() - MAX_LINES;
	if ( excess > 0 ) {
		text->Delete( text->LineAt(0), text->LineAt(excess) );
	}

	// Adjust scroll bar proportion ...
	float lines = text->CountLines();
	if ( lines < ROWS ) {
		lines = ROWS;
	}
	ScrollBar( B_VERTICAL )->SetProportion( ROWS/lines );

	// ... and value (that is, which line is top line)
	lines -= ROWS - 1;
	if ( lines < 0.0 ) {
		lines = 0;
	}
	ScrollBar( B_VERTICAL )->SetValue( lines * lineSize );
	Invalidate();
}

void 
OutputView::ExportToLog( void )
{
	freelog( LOG_NORMAL, "---- EXPORT: start ----------------------------" );
	freelog( LOG_NORMAL, "%s", text->Text() );
	freelog( LOG_NORMAL, "---- EXPORT: end ------------------------------" );
	Append( "---- EXPORTED TO LOG -------------------------------------" );
}


void 
OutputView::Clear( void )
{
	if ( ! text ) return;
	text->SetText( NULL );
	ScrollBar( B_VERTICAL )->SetProportion( 1.0 );
	ScrollBar( B_VERTICAL )->SetValue( 0.0 );
	Invalidate();
}

void
OutputView::MessageReceived( BMessage *msg )
{
        switch (msg->what) {
		// @@@@
		default:
			BScrollView::MessageReceived(msg);
			break;
		}
}
 

