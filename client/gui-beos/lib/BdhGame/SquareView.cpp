/*
	SquareView.cpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/

#include <File.h>
#include <Entry.h>
#include <Alert.h>
#include <string.h>
#include <errno.h>
#include "SquareView.h"

// -------------------------------------------------------------
// SquareView
//		view maintaining cartesian grid

SquareView::SquareView( const uint16 columns, const uint16 rows,
		const uint16 dx, const uint16 dy,
		const BPoint offset, const char* title )
	: BView( BRect(offset, offset+BPoint(columns*dx, rows*dy)),
			title, B_FOLLOW_NONE, B_WILL_DRAW ),
		horizCount(columns), vertCount(rows),
		horizStep(dx), vertStep(dy)
{
}

SquareView::~SquareView()
{
}


void
SquareView::MoveMarkTo( int16 row, int16 column )
{
	MovePenTo( horizStep*column, vertStep*row );
}

void
SquareView::MoveMarkBy( int16 deltaRow, int16 deltaColumn )
{
	MovePenBy( (horizStep*deltaColumn), (vertStep*deltaRow) );
}

void
SquareView::Draw(BRect /*updateRect*/)
{
	MoveMarkTo( horizStep, 2*vertStep );
	DrawString( "SquareView::Draw" );
}


