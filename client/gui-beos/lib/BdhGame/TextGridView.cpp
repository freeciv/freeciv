/*
	TextGridView.cpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/

#include <File.h>
#include <Entry.h>
#include <Alert.h>
#include <string.h>
#include <errno.h>
#include "TextGridView.h"


// -------------------------------------------------------------
// TextGridView
//		view maintaining cartesian grid

TextGridView::TextGridView( const uint16 columns, const uint16 rows,
		const BFont *font, const BPoint offset, const char* title )
	: SquareView( columns, rows, font->StringWidth("W"),
			heightOf(font), offset, title )
{
}

TextGridView::~TextGridView()
{
}

float
TextGridView::heightOf( const BFont *font )
{
	font->GetHeight(&fh);
	return (fh.ascent + fh.descent + fh.leading);
}

void
TextGridView::MoveMarkTo( int16 row, int16 column )
{
	SquareView::MoveMarkTo( row, column );
	MovePenBy( 1, fh.ascent + fh.leading );
}



