/*
	BdhBitmap.h
	Copyright (c) 1997 Be Do Have Software.  All rights reserved.
 */
 
#define _BUILDING_Bdh 1
#include "BdhBitmap.h"

//-------------------------------------------------------------
// BdhBitmap

BdhBitmap::BdhBitmap( void *bits, int16 dx, int16 dy,
			color_space space )
	: BBitmap( BRect(0,0,dx-1,dy-1), space )
{
	SetBits( bits, dx*dy, 0, space );
}

BdhBitmap::~BdhBitmap()
{
}

