/*
	BdhView.h
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/
#ifndef BDH_VIEW_H
#define BDH_VIEW_H

#include <View.h>
#include <Point.h>
#include <Font.h>

//------------------------------------------
// Standard shared library gibberish
#include "BdhBuild.h"
#undef _EXPIMP
#if _BUILDING_Bdh + 0 > 0
#define _EXPIMP	_EXPORT
#else
#define _EXPIMP	_IMPORT
#endif

#if __POWERPC__ 
#pragma export on
#endif
//------------------------------------------


class _EXPIMP BdhView : public BView {
public:
			BdhView( uint32 width, uint32 height, BPoint offset,
				const char* title, uint32 resizing, uint32 flags );
	virtual ~BdhView();
};



class _EXPIMP BdhBitmapView : public BdhView {
public:
	BdhBitmapView( uint32 dx, uint32 dy, BPoint offset, BBitmap *bm = 0);
	BdhBitmapView( uint32 dx, uint32 dy, BPoint offset,
						const char* filename );
	virtual ~BdhBitmapView();
	void	AttachedToWindow(void);
	void	Draw( BRect );
protected:
	BBitmap *bitmap;
	BFont	*font;
	float	fontSize;
};




#if __POWERPC__ 
#pragma export off
#endif
#endif // BDHVIEW_H
