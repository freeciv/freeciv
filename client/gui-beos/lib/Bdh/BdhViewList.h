/*
	BdhWindow.h
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/
#ifndef BDH_VIEWLIST_H
#define BDH_VIEWLIST_H 1

#include <View.h>
#include <List.h>

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


// -- List of views
class _EXPIMP BdhViewList : public BList {
public:
	BdhViewList(void);
	~BdhViewList();
	BView *asView(void *ptr)
		{ return reinterpret_cast<BView*>(ptr);}
	BView *viewAt(long ind)
		{ return asView(ItemAt(ind)); }
	BView *popView(void);
	BRect bounds(void);
	BRect frame(void);
	bool addAtTop(    BView* vp, bool asFirst = false );
	bool addAtLeft(   BView* vp, bool asFirst = false );
	bool addAtBottom( BView* vp, bool asFirst = false );
	bool addAtRight(  BView* vp, bool asFirst = false );
	bool addAt( BView* vp, BPoint ul, bool asFirst = false );
	bool addView( BView* vp, bool asFirst = false )
		{ return ( asFirst	? AddItem(vp,0) : AddItem(vp) ); }
	void offsetBy ( BPoint p )
		{ offsetBy( p.x, p.y ); }
	void offsetBy( float dx, float dy );
	void pad( float dLeft, float dTop,
		float dRight, float dBottom  );
};


#if __POWERPC__ 
#pragma export off
#endif
#endif // BDH_VIEWLIST_H
