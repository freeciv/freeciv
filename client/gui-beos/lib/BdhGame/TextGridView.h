/*
	TextGridView.h
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/
#ifndef BDH_TEXTGRIDVIEW_H
#define BDH_TEXTGRIDVIEW_H 1

#include <Font.h>
#include <List.h>
#include <stdio.h>
#include "SquareView.h"

//------------------------------------------
// Standard shared library gibberish
#include "BdhBuild.h"
#undef _EXPIMP
#if _BUILDING_BdhGame + 0 > 0
#define _EXPIMP	_EXPORT
#else
#define _EXPIMP	_IMPORT
#endif

#if __POWERPC__ 
#pragma export on
#endif
//------------------------------------------


// view containing text
class _EXPIMP TextGridView : public SquareView {
public:
	TextGridView( const uint16 columns, const uint16 rows,
		const BFont *font, const BPoint offset, const char* title );
	~TextGridView();
	virtual void	MoveMarkTo( int16, int16 );
protected:
	float heightOf( const BFont *font );
private:
	font_height fh;
};


#if __POWERPC__ 
#pragma export off
#endif
#endif // BDH_TEXTGRIDVIEW_H
