/*
	BdhBitmap.h
	Copyright (c) 1997 Be Do Have Software.  All rights reserved.
 */
#ifndef BDH_BITMAP_H
#define BDH_BITMAP_H 1

#include <Bitmap.h>
 
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


class _EXPIMP BdhBitmap : public BBitmap {
public:
	BdhBitmap( void *bits, int16 dx, int16 dy,
		color_space space );
	virtual ~BdhBitmap();
};
 

#if __POWERPC__ 
#pragma export off
#endif
#endif // BDH_BITMAP_H
