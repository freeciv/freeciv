/*
	BdhWindow.cpp
	Copyright 1996-1997 Be Do Have Software. All Rights Reserved.
*/

#define _BUILDING_Bdh 1
#include <interface/StringView.h>
#include "BdhViewList.h"

// -----------------------------------------------------------
// BdhViewList

BdhViewList::BdhViewList(void)
	: BList()
{
}

BdhViewList::~BdhViewList()
{
}

BRect
BdhViewList::bounds(void)
{
	BRect r = frame();
	r.OffsetTo(0,0);
	return r;
}

BRect
BdhViewList::frame(void)
{
	BRect r;
	if ( CountItems() > 0 ) {
		r = viewAt(0)->Frame();
	}
	for ( long c=1; c < CountItems(); c++ ) {
		r = r | viewAt(c)->Frame();
	}
	return r;
}

void
BdhViewList::offsetBy( float dx, float dy )
{
	for ( long c=0; c < CountItems(); c++ ) {
		viewAt(c)->MoveBy( dx, dy );
	}
}


BView *
BdhViewList::popView( void )
{
	void *v = FirstItem();
	RemoveItems(0,1);
	return asView(v);
}

bool
BdhViewList::addAtTop(    BView* vp, bool asFirst )
{
	BPoint ref = frame().LeftTop();
	float delta = vp->Frame().Height();
	offsetBy( 0, delta );
	vp->MoveTo( ref );
	return addView( vp, asFirst );
}

bool
BdhViewList::addAtLeft(   BView* vp, bool asFirst )
{
	BPoint ref = frame().LeftTop();
	float delta = vp->Frame().Width();
	offsetBy( delta, 0 );
	vp->MoveTo( ref );
	return addView( vp, asFirst );
}

bool
BdhViewList::addAtBottom( BView* vp, bool asFirst )
{
	vp->MoveTo( frame().LeftBottom() );
	return addView( vp, asFirst );
}

bool
BdhViewList::addAtRight(  BView* vp, bool asFirst )
{
	vp->MoveTo( frame().RightTop() );
	return addView( vp, asFirst );
}


bool
BdhViewList::addAt( BView* vp, BPoint ul, bool asFirst )
{
	BRect r = frame();
	float dx = ( r.top  - ul.x );	// offset needed
	float dy = ( r.left - ul.y );	// offset needed
	if ( dx>0 || dy>0 ) {
		if ( dx<0 ) { dx = 0; }
		if ( dy<0 ) { dy = 0; }
		offsetBy( dx, dy );
		vp->MoveTo( r.LeftTop() );
	} else {
		vp->MoveTo(ul);
	}
	return addView( vp, asFirst );
}


void
BdhViewList::pad( float dLeft, float dTop,
		float dRight, float dBottom )
{
	if ( dLeft > 0 || dTop > 0 ) {
		float dl = ( (dLeft>0) ? dLeft : 1 );
		float dt = ( (dTop>0)  ? dTop  : 1 );
		BStringView *sv = new BStringView( BRect(0,0,dl,dt),
			"spacer", " " );
		if ( ! (dTop>0) ) {
			addAtLeft( sv );
		} else if ( ! (dLeft>0) ) {
			addAtTop( sv );
		} else {
			BPoint p = frame().LeftTop() - BPoint(dl,dt);
			addAt( sv, p );
		}
	}
	if ( dRight > 0 || dBottom > 0 ) {
		float dr = ( (dRight>0) ? dRight  : 1 );
		float db = ( (dBottom>0)? dBottom : 1 );
		BStringView *sv = new BStringView( BRect(0,0,dr,db),
			"spacer", " " );
		if ( ! (dBottom>0) ) {
			addAtRight( sv );
		} else if ( ! (dRight>0) ) {
			addAtBottom( sv );
		} else {
			BPoint p = frame().RightBottom();
			addAt( sv, p );
		}
	}
}

