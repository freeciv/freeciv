/*
	SquareView.h
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/
#ifndef BDH_SQUAREVIEW_H
#define BDH_SQUAREVIEW_H 1

#include <View.h>
#include <Font.h>
#include <List.h>
#include <stdio.h>

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


// view containing cartesian grid
class _EXPIMP SquareView : public BView
{
public:
	SquareView( const uint16 columns, const uint16 rows, const uint16 dx,
		const uint16 dy, const BPoint offset, const char* title );
	virtual ~SquareView();
	virtual void	MoveMarkTo( int16, int16 );
	virtual void	MoveMarkBy( int16, int16 );
	virtual void	Draw( BRect );
	uint16 columns(void) { return horizCount; }
	uint16 rows(   void) { return vertCount;  }
	uint16 stepWidth( void) { return horizStep; }
	uint16 stepHeight(void) { return vertStep; }
protected:
	int horizCount;
	int vertCount;
	int	horizStep;
	int vertStep;
};


#if __POWERPC__ 
#pragma export off
#endif
#endif // BDH_SQUAREVIEW_H
